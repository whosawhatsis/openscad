// Portions of this file are Copyright 2023 Google LLC, and licensed under GPL2+. See COPYING.
#include "geometry/manifold/ManifoldGeometry.h"

#include <manifold/cross_section.h>
#include <manifold/manifold.h>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <utility>

#include "geometry/Geometry.h"
#include "geometry/PolySet.h"
#include "geometry/PolySetBuilder.h"
#include "geometry/PolySetUtils.h"
#include "geometry/Polygon2d.h"
#include "geometry/linalg.h"
#include "geometry/manifold/manifoldutils.h"
#include "glview/ColorMap.h"
#include "glview/RenderSettings.h"
#include "utils/printutils.h"
#ifdef ENABLE_CGAL
#include "geometry/cgal/cgalutils.h"
#endif

namespace {

template <typename Result, typename V>
Result vector_convert(V const& v)
{
  return Result(v[0], v[1], v[2]);
}

}  // namespace

ManifoldGeometry::ManifoldGeometry() : manifold_(manifold::Manifold())
{
}

ManifoldGeometry::ManifoldGeometry(manifold::Manifold mani, const std::set<uint32_t>& originalIDs,
                                   const std::map<uint32_t, Color4f>& originalIDToColor,
                                   const std::set<uint32_t>& subtractedIDs,
                                   const std::map<uint32_t, SurfaceFinish>& originalIDToFinish)
  : manifold_(std::move(mani)),
    originalIDs_(originalIDs),
    originalIDToColor_(originalIDToColor),
    originalIDToFinish_(originalIDToFinish),
    subtractedIDs_(subtractedIDs)
{
}

std::unique_ptr<Geometry> ManifoldGeometry::copy() const
{
  return std::make_unique<ManifoldGeometry>(*this);
}

const manifold::Manifold& ManifoldGeometry::getManifold() const
{
  return manifold_;
}

bool ManifoldGeometry::isEmpty() const
{
  return getManifold().IsEmpty();
}

size_t ManifoldGeometry::numFacets() const
{
  return getManifold().NumTri();
}

size_t ManifoldGeometry::numVertices() const
{
  return getManifold().NumVert();
}

bool ManifoldGeometry::isManifold() const
{
  return getManifold().Status() == manifold::Manifold::Error::NoError;
}

bool ManifoldGeometry::isValid() const
{
  return manifold_.Status() == manifold::Manifold::Error::NoError;
}

void ManifoldGeometry::clear()
{
  manifold_ = manifold::Manifold();
}

// Note: We promise to only call memsize if we've already evaluated the object.
// However, there is no way of querying this on the Manifold object itself.
size_t ManifoldGeometry::memsize() const
{
  // Estimated memory usage per vertex:
  // - Position: 24 bytes
  // - Halfedges (approx 6 per vert): 6 * 16 = 96 bytes
  // - Normals (vert + 2*face): 24 + 48 = 72 bytes
  // - Mesh Relation (2*face): 32 bytes
  // Total ~ 224 bytes + vector overhead + properties
  return getManifold().NumVert() * 250;
}

std::string ManifoldGeometry::dump() const
{
  std::ostringstream out;
  auto& manifold = getManifold();
  auto meshgl = manifold.GetMeshGL64();
  out << "Manifold:" << "\n status: " << ManifoldUtils::statusToString(manifold.Status())
      << "\n genus: " << manifold.Genus() << "\n num vertices: " << meshgl.NumVert()
      << "\n num polygons: " << meshgl.NumTri() << "\n polygons data:";

  for (size_t faceid = 0; faceid < meshgl.NumTri(); faceid++) {
    out << "\n  polygon begin:";
    for (const int j : {0, 1, 2}) {
      auto v = vector_convert<Vector3d>(meshgl.GetVertPos(meshgl.GetTriVerts(faceid)[j]));
      out << "\n   vertex:" << v;
    }
  }
  out << "Manifold end";
  return out.str();
}

std::shared_ptr<PolySet> ManifoldGeometry::toPolySet() const
{
  manifold::MeshGL64 mesh = getManifold().GetMeshGL64();
  auto ps = std::make_shared<PolySet>(3);
  ps->setTriangular(true);
  ps->vertices.reserve(mesh.NumVert());
  ps->indices.reserve(mesh.NumTri());
  ps->setConvexity(convexity);
  ps->setManifold(true);

  // first 3 channels are xyz coordinate
  for (size_t i = 0; i < mesh.vertProperties.size(); i += mesh.numProp)
    ps->vertices.emplace_back(mesh.vertProperties[i], mesh.vertProperties[i + 1],
                              mesh.vertProperties[i + 2]);

  ps->colors.reserve(originalIDToColor_.size());
  ps->color_indices.reserve(ps->indices.size());

  auto colorScheme = ColorMap::instance().findColorScheme(RenderSettings::inst()->colorscheme);
  int32_t faceFrontColorIndex = -1;
  int32_t faceBackColorIndex = -1;

  // Keyed on the pair: two bodies may agree on color and differ in finish, and
  // one entry cannot describe both.
  std::map<std::pair<Color4f, SurfaceFinish>, int32_t> surfaceToIndex;
  std::map<uint32_t, int32_t> originalIDToColorIndex;
  const bool hasFinishes = !originalIDToFinish_.empty();
  // Kept exactly as long as colors, or empty. Every push to one pushes to the other.
  const auto addSurface = [&](const Color4f& color, const SurfaceFinish& finish) {
    ps->colors.push_back(color);
    if (hasFinishes) ps->finishes.push_back(finish);
  };

  auto getFaceFrontColorIndex = [&]() -> int {
    if (faceFrontColorIndex < 0) {
      faceFrontColorIndex = ps->colors.size();
      addSurface(ColorMap::getColor(*colorScheme, RenderColor::CGAL_FACE_FRONT_COLOR), {});
    }
    return faceFrontColorIndex;
  };
  auto getFaceBackColorIndex = [&]() -> int {
    if (faceBackColorIndex < 0) {
      faceBackColorIndex = ps->colors.size();
      addSurface(ColorMap::getColor(*colorScheme, RenderColor::CGAL_FACE_BACK_COLOR), {});
    }
    return faceBackColorIndex;
  };

  auto getColorIndex = [&](uint32_t originalID) -> int32_t {
    if (subtractedIDs_.find(originalID) != subtractedIDs_.end()) {
      return getFaceBackColorIndex();
    }
    auto colorIndexIt = originalIDToColorIndex.find(originalID);
    if (colorIndexIt != originalIDToColorIndex.end()) {
      return colorIndexIt->second;
    }
    auto colorIt = originalIDToColor_.find(originalID);
    auto finishIt = originalIDToFinish_.find(originalID);
    if (colorIt == originalIDToColor_.end() && finishIt == originalIDToFinish_.end()) {
      return getFaceFrontColorIndex();
    }
    // A material() need not name a color. An invalid one reads as "use the
    // default color", which is what it means, and still gives the finish an
    // entry to ride on.
    const Color4f color = colorIt == originalIDToColor_.end() ? Color4f() : colorIt->second;
    const SurfaceFinish finish =
      finishIt == originalIDToFinish_.end() ? SurfaceFinish() : finishIt->second;

    auto pair = surfaceToIndex.insert({{color, finish}, static_cast<int32_t>(ps->colors.size())});
    if (pair.second) {
      addSurface(color, finish);
    }
    int32_t color_index = pair.first->second;
    originalIDToColorIndex[originalID] = color_index;
    return color_index;
  };

  auto start = mesh.runIndex[0];
  for (int run = 0, numRun = mesh.runIndex.size() - 1; run < numRun; ++run) {
    const auto id = mesh.runOriginalID[run];
    const auto end = mesh.runIndex[run + 1];
    const size_t numTri = (end - start) / 3;
    if (numTri == 0) {
      continue;
    }

    auto colorIndex = getColorIndex(id);
    for (size_t i = start; i < end; i += 3) {
      ps->indices.push_back({static_cast<int>(mesh.triVerts[i]), static_cast<int>(mesh.triVerts[i + 1]),
                             static_cast<int>(mesh.triVerts[i + 2])});
      ps->color_indices.push_back(colorIndex);
    }
    start = end;
  }
  return ps;
}

#ifdef ENABLE_CGAL
template <typename Polyhedron>
class CGALPolyhedronBuilderFromManifold : public CGAL::Modifier_base<typename Polyhedron::HalfedgeDS>
{
  using HDS = typename Polyhedron::HalfedgeDS;
  using CGAL_Polybuilder = CGAL::Polyhedron_incremental_builder_3<typename Polyhedron::HalfedgeDS>;

public:
  using CGALPoint = typename CGAL_Polybuilder::Point_3;

  const manifold::MeshGL64& meshgl;
  CGALPolyhedronBuilderFromManifold(const manifold::MeshGL64& mesh) : meshgl(mesh) {}

  void operator()(HDS& hds) override
  {
    CGAL_Polybuilder B(hds, true);

    B.begin_surface(meshgl.NumVert(), meshgl.NumTri());
    for (size_t vertid = 0; vertid < meshgl.NumVert(); vertid++)
      B.add_vertex(CGALUtils::vector_convert<CGALPoint>(meshgl.GetVertPos(vertid)));

    for (size_t faceid = 0; faceid < meshgl.NumTri(); faceid++) {
      const auto tv = meshgl.GetTriVerts(faceid);
      B.begin_facet();
      for (const int j : {0, 1, 2}) {
        B.add_vertex_to_facet(tv[j]);
      }
      B.end_facet();
    }
    B.end_surface();
  }
};

template <class Polyhedron>
std::shared_ptr<Polyhedron> ManifoldGeometry::toPolyhedron() const
{
  auto p = std::make_shared<Polyhedron>();
  try {
    auto meshgl = getManifold().GetMeshGL64();
    CGALPolyhedronBuilderFromManifold<Polyhedron> builder(meshgl);
    p->delegate(builder);
  } catch (const CGAL::Assertion_exception& e) {
    LOG(message_group::Error, "CGAL error in ManifoldGeometry::toPolyhedron(): %1$s", e.what());
  }
  return p;
}

template std::shared_ptr<CGAL::Polyhedron_3<CGAL_Kernel3>> ManifoldGeometry::toPolyhedron() const;

#endif

ManifoldGeometry ManifoldGeometry::binOp(const ManifoldGeometry& lhs, const ManifoldGeometry& rhs,
                                         manifold::OpType opType) const
{
  auto mani = lhs.manifold_.Boolean(rhs.manifold_, opType);
  auto originalIDToColor = lhs.originalIDToColor_;
  auto originalIDToFinish = lhs.originalIDToFinish_;
  auto subtractedIDs = lhs.subtractedIDs_;

  auto originalIDs = lhs.originalIDs_;
  originalIDs.insert(rhs.originalIDs_.begin(), rhs.originalIDs_.end());

  if (opType == manifold::OpType::Subtract) {
    // Mark all the original ids coming from rhs as subtracted, unless they're mapped to a color.
    for (const auto id : rhs.originalIDs_) {
      auto it = rhs.originalIDToColor_.find(id);
      auto fit = rhs.originalIDToFinish_.find(id);
      if (fit != rhs.originalIDToFinish_.end()) {
        originalIDToFinish[id] = fit->second;
      }
      // A subtracted body whose cut faces carry a color keeps its identity so
      // those faces render as its own material; one that does not is marked
      // subtracted and picks up the back-face color. The finish follows the
      // same decision, which is the color map's alone to make.
      if (it != rhs.originalIDToColor_.end()) {
        originalIDToColor[id] = it->second;
      } else {
        subtractedIDs.insert(id);
      }
    }
  } else {
    // Add the id -> color mapping from the rhs.
    originalIDToColor.insert(rhs.originalIDToColor_.begin(), rhs.originalIDToColor_.end());
    originalIDToFinish.insert(rhs.originalIDToFinish_.begin(), rhs.originalIDToFinish_.end());
    subtractedIDs.insert(rhs.subtractedIDs_.begin(), rhs.subtractedIDs_.end());
  }
  return {mani, originalIDs, originalIDToColor, subtractedIDs, originalIDToFinish};
}

std::shared_ptr<ManifoldGeometry> minkowskiOp(const ManifoldGeometry& lhs, const ManifoldGeometry& rhs)
{
// FIXME: How to deal with operation not supported?
#ifdef ENABLE_CGAL
  auto lhs_nef =
    std::shared_ptr<CGALNefGeometry>(CGALUtils::createNefPolyhedronFromPolySet(*lhs.toPolySet()));
  auto rhs_nef =
    std::shared_ptr<CGALNefGeometry>(CGALUtils::createNefPolyhedronFromPolySet(*rhs.toPolySet()));
  if (lhs_nef->isEmpty() || rhs_nef->isEmpty()) {
    return {};
  }
  std::shared_ptr<const PolySet> ps;
  try {
    lhs_nef->minkowski(*rhs_nef);
    ps = PolySetUtils::getGeometryAsPolySet(lhs_nef);
    if (ps) {
      return ManifoldUtils::createManifoldFromPolySet(*ps);
    }
  } catch (const std::exception& e) {
    LOG(message_group::Error, "Nef minkoswki operation failed: %1$s\n", e.what());
  } catch (...) {
    LOG(message_group::Warning, "Nef minkowski hard-crashed");
  }
#endif
  return {};
}

ManifoldGeometry ManifoldGeometry::operator+(const ManifoldGeometry& other) const
{
  return binOp(*this, other, manifold::OpType::Add);
}

ManifoldGeometry ManifoldGeometry::operator*(const ManifoldGeometry& other) const
{
  return binOp(*this, other, manifold::OpType::Intersect);
}

ManifoldGeometry ManifoldGeometry::operator-(const ManifoldGeometry& other) const
{
  return binOp(*this, other, manifold::OpType::Subtract);
}

ManifoldGeometry ManifoldGeometry::minkowski(const ManifoldGeometry& other) const
{
#if defined(USE_MANIFOLD_MINKOWSKI)
  auto result = getManifold().MinkowskiSum(other.getManifold());
  std::set<uint32_t> originalIDs;
  auto id = result.OriginalID();
  if (id >= 0) {
    originalIDs.insert(id);
  }
  return {result, originalIDs};
#else
  std::shared_ptr<ManifoldGeometry> geom = minkowskiOp(*this, other);
  if (geom) return *geom;
  else return {};
#endif
}

Polygon2d ManifoldGeometry::slice() const
{
  auto cross_section = manifold::CrossSection(manifold_.Slice());
  return ManifoldUtils::polygonsToPolygon2d(cross_section.ToPolygons());
}

Polygon2d ManifoldGeometry::project() const
{
  auto cross_section = manifold::CrossSection(manifold_.Project());
  return ManifoldUtils::polygonsToPolygon2d(cross_section.ToPolygons());
}

void ManifoldGeometry::transform(const Transform3d& mat)
{
  manifold::mat3x4 glMat(
    // Column-major ordering
    {mat(0, 0), mat(1, 0), mat(2, 0)}, {mat(0, 1), mat(1, 1), mat(2, 1)},
    {mat(0, 2), mat(1, 2), mat(2, 2)}, {mat(0, 3), mat(1, 3), mat(2, 3)});
  manifold_ = getManifold().Transform(glMat);
}

void ManifoldGeometry::setColor(const Color4f& c)
{
  if (manifold_.OriginalID() == -1) {
    manifold_ = manifold_.AsOriginal();
  }
  originalIDs_.clear();
  originalIDs_.insert(manifold_.OriginalID());
  originalIDToColor_.clear();
  originalIDToColor_[manifold_.OriginalID()] = c;
  subtractedIDs_.clear();
}

// Deliberately does not clear originalIDToColor_: a material() sets a color and
// a finish in that order on the same geometry, and both collapse it to the same
// single original ID, so whichever runs second must leave the first alone.
void ManifoldGeometry::setFinish(const SurfaceFinish& f)
{
  if (manifold_.OriginalID() == -1) {
    manifold_ = manifold_.AsOriginal();
  }
  originalIDs_.clear();
  originalIDs_.insert(manifold_.OriginalID());
  originalIDToFinish_.clear();
  originalIDToFinish_[manifold_.OriginalID()] = f;
  subtractedIDs_.clear();
}

void ManifoldGeometry::toOriginal()
{
  if (manifold_.OriginalID() == -1) {
    manifold_ = manifold_.AsOriginal();
  }
  originalIDs_.clear();
  originalIDs_.insert(manifold_.OriginalID());
  originalIDToColor_.clear();
  originalIDToFinish_.clear();
  subtractedIDs_.clear();
}

BoundingBox ManifoldGeometry::getBoundingBox() const
{
  BoundingBox result;
  manifold::Box bbox = getManifold().BoundingBox();
  result.extend(vector_convert<Eigen::Vector3d>(bbox.min));
  result.extend(vector_convert<Eigen::Vector3d>(bbox.max));
  return result;
}

void ManifoldGeometry::resize(const Vector3d& newsize, const Eigen::Matrix<bool, 3, 1>& autosize)
{
  transform(GeometryUtils::getResizeTransform(this->getBoundingBox(), newsize, autosize));
}

/*! Iterate over all vertices' points until the function returns true (for done). */
void ManifoldGeometry::foreachVertexUntilTrue(
  const std::function<bool(const manifold::vec3& pt)>& f) const
{
  auto mesh = getManifold().GetMeshGL64();
  const auto numVert = mesh.NumVert();
  for (size_t v = 0; v < numVert; ++v) {
    if (f(mesh.GetVertPos(v))) {
      return;
    }
  }
}
