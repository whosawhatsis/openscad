#include "io/ipc_geometry.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include "geometry/Geometry.h"
#include "geometry/PolySet.h"
#include "geometry/PolySetUtils.h"
#include "geometry/Polygon2d.h"
#include "geometry/linalg.h"
#include "utils/printutils.h"

namespace {

// "OSIG": OpenSCAD internal geometry. Bumping kVersion is enough to invalidate a payload written by
// a different build, which is all the compatibility this format owes anyone -- both ends are the
// same binary on the same machine.
constexpr uint32_t kMagic = 0x4749534f;
constexpr uint32_t kVersion = 1;

// What a body is. The transport carries meshes and 2D outlines; anything else (Manifold, Nef) is
// converted to a PolySet on the way out, which is the one lossy step left here.
constexpr uint32_t kKindPolySet = 0;
constexpr uint32_t kKindPolygon2d = 1;

// A rendered model can be a list of separate bodies. The payload therefore always begins with this
// container, even for one body, so the reader never has to guess which shape it is holding.
struct ListHeader {
  uint32_t magic;
  uint32_t version;
  uint32_t bodyCount;
  uint32_t reserved;
};

struct PolySetHeader {
  uint32_t dimension;
  int32_t convexity;
  uint32_t flags;  // bit 0 triangular, bit 1 manifold
  uint32_t vertexCount;
  uint32_t polygonCount;
  uint32_t colorCount;
  uint32_t colorIndexCount;
  uint32_t reserved;
};

struct Polygon2dHeader {
  int32_t convexity;
  uint32_t flags;  // bit 0 sanitized
  uint32_t outlineCount;
  uint32_t reserved;
};

template <typename T>
void append(std::vector<char>& out, const T& value)
{
  const auto offset = out.size();
  out.resize(offset + sizeof(T));
  std::memcpy(out.data() + offset, &value, sizeof(T));
}

void appendBytes(std::vector<char>& out, const void *data, size_t bytes)
{
  const auto offset = out.size();
  out.resize(offset + bytes);
  std::memcpy(out.data() + offset, data, bytes);
}

// Reads from a bounds-checked cursor. Every read goes through this, so a truncated payload -- what
// a worker killed mid-write leaves behind -- fails at the first short read instead of walking off
// the end of the buffer or producing a plausible-looking mesh from whatever arrived.
class Cursor
{
public:
  Cursor(const char *data, size_t size) : data(data), remaining(size) {}
  bool read(void *destination, size_t bytes)
  {
    if (bytes > remaining) return false;
    std::memcpy(destination, data, bytes);
    data += bytes;
    remaining -= bytes;
    return true;
  }
  template <typename T>
  bool read(T& value)
  {
    return read(&value, sizeof(T));
  }

private:
  const char *data;
  size_t remaining;
};

void appendPolygon2d(std::vector<char>& buffer, const Polygon2d& polygon)
{
  append(buffer, Polygon2dHeader{static_cast<int32_t>(polygon.getConvexity()),
                                 static_cast<uint32_t>(polygon.isSanitized() ? 1u : 0u),
                                 static_cast<uint32_t>(polygon.outlines().size()), 0});
  for (const auto& outline : polygon.outlines()) {
    append(buffer, static_cast<uint32_t>(outline.vertices.size()));
    // An outline that comes back positive when it was a hole silently fills the hole in, so
    // orientation travels explicitly rather than being inferred from winding at the far end.
    append(buffer, static_cast<uint32_t>(outline.positive ? 1u : 0u));
    for (const auto& vertex : outline.vertices) {
      const double xy[2]{vertex.x(), vertex.y()};
      appendBytes(buffer, xy, sizeof(xy));
    }
  }
}

void appendPolySet(std::vector<char>& buffer, const PolySet& polyset)
{
  uint32_t indexCount = 0;
  for (const auto& face : polyset.indices) indexCount += face.size();

  append(buffer,
         PolySetHeader{
           polyset.getDimension(), static_cast<int32_t>(polyset.getConvexity()),
           static_cast<uint32_t>((polyset.isTriangular() ? 1u : 0u) | (polyset.isManifold() ? 2u : 0u)),
           static_cast<uint32_t>(polyset.vertices.size()), static_cast<uint32_t>(polyset.indices.size()),
           static_cast<uint32_t>(polyset.colors.size()),
           static_cast<uint32_t>(polyset.color_indices.size()), 0});
  buffer.reserve(buffer.size() + polyset.vertices.size() * 3 * sizeof(double) +
                 (polyset.indices.size() + indexCount) * sizeof(uint32_t) +
                 polyset.colors.size() * 4 * sizeof(float) +
                 polyset.color_indices.size() * sizeof(int32_t));

  for (const auto& vertex : polyset.vertices) {
    const double xyz[3]{vertex.x(), vertex.y(), vertex.z()};
    appendBytes(buffer, xyz, sizeof(xyz));
  }
  // Length-prefixed per polygon: PolySet holds arbitrary n-gons, and a reader that assumed
  // triangles would silently drop every quad a real model produces.
  for (const auto& face : polyset.indices) {
    append(buffer, static_cast<uint32_t>(face.size()));
    for (const auto index : face) append(buffer, static_cast<int32_t>(index));
  }
  // Color is a palette plus one index per polygon, where -1 means "no specific color". Both halves
  // are needed: the palette alone loses which face is which, the indices alone lose the colors.
  for (const auto& color : polyset.colors) {
    const float rgba[4]{color.r(), color.g(), color.b(), color.a()};
    appendBytes(buffer, rgba, sizeof(rgba));
  }
  for (const auto index : polyset.color_indices) append(buffer, static_cast<int32_t>(index));
}

void appendBody(std::vector<char>& buffer, const std::shared_ptr<const Geometry>& body)
{
  if (const auto polygon = std::dynamic_pointer_cast<const Polygon2d>(body)) {
    append(buffer, kKindPolygon2d);
    appendPolygon2d(buffer, *polygon);
    return;
  }
  // The same normalization the OFF exporter applies, so switching formats does not change which
  // geometry the parent receives -- only how it is encoded. Manifold and Nef arrive here.
  const auto polyset = PolySetUtils::getGeometryAsPolySet(body);
  append(buffer, kKindPolySet);
  appendPolySet(buffer, *polyset);
}

bool readListHeader(Cursor& cursor, const std::string& name, ListHeader& listHeader)
{
  if (!cursor.read(listHeader)) return false;
  if (listHeader.magic != kMagic || listHeader.version != kVersion) {
    LOG(message_group::Error, "Compute worker geometry '%1$s' is not a usable payload.", name);
    return false;
  }
  return true;
}

std::unique_ptr<Polygon2d> readPolygon2d(Cursor& cursor)
{
  Polygon2dHeader header{};
  if (!cursor.read(header)) return {};
  auto polygon = std::make_unique<Polygon2d>();
  polygon->setConvexity(header.convexity);
  polygon->setSanitized(header.flags & 1u);
  for (uint32_t i = 0; i < header.outlineCount; ++i) {
    uint32_t vertexCount = 0;
    uint32_t positive = 0;
    if (!cursor.read(vertexCount) || !cursor.read(positive)) return {};
    Outline2d outline;
    outline.positive = positive != 0;
    outline.vertices.resize(vertexCount);
    for (auto& vertex : outline.vertices) {
      double xy[2];
      if (!cursor.read(xy, sizeof(xy))) return {};
      vertex = Vector2d(xy[0], xy[1]);
    }
    polygon->addOutline(std::move(outline));
  }
  return polygon;
}

std::unique_ptr<PolySet> readPolySet(Cursor& cursor)
{
  PolySetHeader header{};
  if (!cursor.read(header)) return {};

  auto polyset = std::make_unique<PolySet>(header.dimension);
  polyset->setConvexity(header.convexity);
  polyset->setTriangular(header.flags & 1u);
  polyset->setManifold(header.flags & 2u);

  polyset->vertices.resize(header.vertexCount);
  for (auto& vertex : polyset->vertices) {
    double xyz[3];
    if (!cursor.read(xyz, sizeof(xyz))) return {};
    vertex = Vector3d(xyz[0], xyz[1], xyz[2]);
  }
  polyset->indices.resize(header.polygonCount);
  for (auto& face : polyset->indices) {
    uint32_t count = 0;
    if (!cursor.read(count)) return {};
    face.resize(count);
    for (auto& index : face) {
      int32_t value = 0;
      if (!cursor.read(value)) return {};
      index = value;
    }
  }
  polyset->colors.resize(header.colorCount);
  for (auto& color : polyset->colors) {
    float rgba[4];
    if (!cursor.read(rgba, sizeof(rgba))) return {};
    color = Color4f(rgba[0], rgba[1], rgba[2], rgba[3]);
  }
  polyset->color_indices.resize(header.colorIndexCount);
  for (auto& index : polyset->color_indices) {
    int32_t value = 0;
    if (!cursor.read(value)) return {};
    index = value;
  }
  return polyset;
}

}  // namespace

void export_ipc_geometry(const std::shared_ptr<const Geometry>& geom, std::ostream& output)
{
  // Separate bodies are kept separate. Flattening them here is invisible in a preview but destroys
  // everything downstream that works per body, such as multi-file export.
  std::vector<std::shared_ptr<const Geometry>> bodies;
  if (const auto list = std::dynamic_pointer_cast<const GeometryList>(geom)) {
    for (const auto& child : list->flatten()) bodies.push_back(child.second);
  } else {
    bodies.push_back(geom);
  }

  std::vector<char> buffer;
  append(buffer, ListHeader{kMagic, kVersion, static_cast<uint32_t>(bodies.size()), 0});
  for (const auto& body : bodies) appendBody(buffer, body);
  output.write(buffer.data(), buffer.size());
}

std::shared_ptr<const Geometry> import_ipc_geometry_buffer(const char *data, const std::size_t size,
                                                           const std::string& name)
{
  Cursor cursor(data, size);
  ListHeader listHeader{};
  if (!readListHeader(cursor, name, listHeader)) return {};

  Geometry::Geometries bodies;
  for (uint32_t i = 0; i < listHeader.bodyCount; ++i) {
    uint32_t kind = 0;
    if (!cursor.read(kind)) return {};
    std::shared_ptr<Geometry> body;
    if (kind == kKindPolygon2d) body = readPolygon2d(cursor);
    else if (kind == kKindPolySet) body = readPolySet(cursor);
    else {
      LOG(message_group::Error, "Compute worker geometry '%1$s' carries an unknown body kind.", name);
      return {};
    }
    if (!body) return {};
    bodies.emplace_back(nullptr, std::shared_ptr<const Geometry>(std::move(body)));
  }

  // One body stays a bare PolySet: everything downstream reads that as "one body", and wrapping it
  // would change behavior for every ordinary model.
  if (bodies.size() == 1) return bodies.front().second;
  return std::make_shared<GeometryList>(bodies);
}

std::unique_ptr<PolySet> import_ipc_polyset_buffer(const char *data, const std::size_t size,
                                                   const std::string& name)
{
  Cursor cursor(data, size);
  ListHeader listHeader{};
  if (!readListHeader(cursor, name, listHeader)) return {};
  if (listHeader.bodyCount != 1) {
    LOG(message_group::Error,
        "Compute worker geometry '%1$s' carries %2$d bodies where one was expected.", name,
        static_cast<int>(listHeader.bodyCount));
    return {};
  }
  uint32_t kind = 0;
  if (!cursor.read(kind) || kind != kKindPolySet) return {};
  return readPolySet(cursor);
}
