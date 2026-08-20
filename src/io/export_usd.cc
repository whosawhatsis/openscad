/*
 *  OpenSCAD (www.openscad.org)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  As a special exception, you have permission to link this program
 *  with the CGAL library and distribute executables, as long as you
 *  follow the requirements of the GNU GPL in regard to all of the
 *  software in the executable aside from CGAL.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

#include "geometry/Geometry.h"
#include "geometry/PolySet.h"
#include "geometry/PolySetUtils.h"
#include "geometry/linalg.h"
#include "io/export.h"

namespace {

// ponytail: hand-written USDA text rather than linking a USD library. The subset of the
// format an OpenSCAD model needs is a few hundred lines of ASCII, and tinyusdz/OpenUSD are
// both large dependencies to take on for it. Revisit if we ever need to *read* USD.

//! Distinct materials, keyed by colour, so a model does not emit one material per face.
struct MaterialTable {
  std::vector<Color4f> colors;
  //! Parallel to PolySet::indices: which entry of `colors` each face uses.
  std::vector<size_t> faceMaterial;
};

MaterialTable buildMaterialTable(const PolySet& ps, const Color4f& defaultColor)
{
  MaterialTable table;
  std::map<std::tuple<float, float, float, float>, size_t> seen;

  const auto intern = [&](const Color4f& c) {
    const auto key = std::make_tuple(c.r(), c.g(), c.b(), c.a());
    const auto it = seen.find(key);
    if (it != seen.end()) return it->second;
    const size_t index = table.colors.size();
    table.colors.push_back(c);
    seen.emplace(key, index);
    return index;
  };

  const bool hasColor = !ps.color_indices.empty();
  for (size_t face = 0; face < ps.indices.size(); ++face) {
    Color4f color = defaultColor;
    if (hasColor && face < ps.color_indices.size()) {
      const auto colorIndex = ps.color_indices[face];
      if (colorIndex >= 0 && static_cast<size_t>(colorIndex) < ps.colors.size()) {
        color = ps.colors[colorIndex];
      }
    }
    table.faceMaterial.push_back(intern(color));
  }
  return table;
}

void writeMaterial(std::ostream& output, size_t index, const Color4f& color)
{
  const std::string name = "mat" + std::to_string(index);
  output << "    def Material \"" << name << "\"\n";
  output << "    {\n";
  output << "        token outputs:surface.connect = </root/Materials/" << name
         << "/Shader.outputs:surface>\n\n";
  output << "        def Shader \"Shader\"\n";
  output << "        {\n";
  // Verified against Blender 3.3.1: these four inputs land on the Principled BSDF as
  // Base Color / Metallic / Roughness / Alpha. primvars:displayColor does NOT survive
  // the same trip, which is why colour goes through a bound material instead.
  output << "            uniform token info:id = \"UsdPreviewSurface\"\n";
  output << "            color3f inputs:diffuseColor = (" << color.r() << ", " << color.g() << ", "
         << color.b() << ")\n";
  output << "            float inputs:metallic = 0\n";
  output << "            float inputs:roughness = 0.4\n";
  output << "            float inputs:opacity = " << color.a() << "\n";
  output << "            token outputs:surface\n";
  output << "        }\n";
  output << "    }\n";
}

//! Emits one Mesh prim holding every face that uses `material`, re-indexed to its own points.
void writeMesh(std::ostream& output, const PolySet& ps, const MaterialTable& table, size_t material)
{
  std::vector<size_t> faces;
  for (size_t face = 0; face < table.faceMaterial.size(); ++face) {
    if (table.faceMaterial[face] == material) faces.push_back(face);
  }
  if (faces.empty()) return;

  // Re-index: a per-material mesh carries only the vertices its own faces touch.
  std::vector<int> remap(ps.vertices.size(), -1);
  std::vector<size_t> meshVertices;
  std::vector<std::vector<int>> meshFaces;
  for (const auto face : faces) {
    std::vector<int> indices;
    for (const auto vertex : ps.indices[face]) {
      if (remap[vertex] < 0) {
        remap[vertex] = static_cast<int>(meshVertices.size());
        meshVertices.push_back(vertex);
      }
      indices.push_back(remap[vertex]);
    }
    meshFaces.push_back(std::move(indices));
  }

  const std::string name = "mesh" + std::to_string(material);
  output << "    def Mesh \"" << name << "\" (\n";
  output << "        prepend apiSchemas = [\"MaterialBindingAPI\"]\n";
  output << "    )\n";
  output << "    {\n";

  output << "        int[] faceVertexCounts = [";
  for (size_t i = 0; i < meshFaces.size(); ++i) {
    if (i) output << ", ";
    output << meshFaces[i].size();
  }
  output << "]\n";

  output << "        int[] faceVertexIndices = [";
  bool first = true;
  for (const auto& face : meshFaces) {
    for (const auto index : face) {
      if (!first) output << ", ";
      first = false;
      output << index;
    }
  }
  output << "]\n";

  output << "        point3f[] points = [";
  for (size_t i = 0; i < meshVertices.size(); ++i) {
    if (i) output << ", ";
    const auto& v = ps.vertices[meshVertices[i]];
    output << "(" << v.x() << ", " << v.y() << ", " << v.z() << ")";
  }
  output << "]\n";

  output << "        rel material:binding = </root/Materials/mat" << material << ">\n";
  // Default is catmullClark, which would silently round off every hard edge of a CAD model.
  output << "        uniform token subdivisionScheme = \"none\"\n";
  output << "    }\n";
}

}  // namespace

void export_usda(const std::shared_ptr<const Geometry>& geom, std::ostream& output,
                 const ExportInfo& exportInfo)
{
  const std::shared_ptr<const PolySet> ps = PolySetUtils::getGeometryAsPolySet(geom);
  const MaterialTable table = buildMaterialTable(*ps, exportInfo.defaultColor);

  output << "#usda 1.0\n";
  output << "(\n";
  output << "    doc = \"Generated by " << EXPORT_CREATOR << " from " << exportInfo.title << "\"\n";
  output << "    defaultPrim = \"root\"\n";
  // OpenSCAD is Z-up and its unit is the millimetre. USD defaults to Y-up and metres, so
  // omitting either of these delivers a model that is rotated and 1000x too large.
  output << "    metersPerUnit = 0.001\n";
  output << "    upAxis = \"Z\"\n";
  output << ")\n\n";

  output << "def Xform \"root\"\n";
  output << "{\n";

  output << "    def Scope \"Materials\"\n";
  output << "    {\n";
  for (size_t i = 0; i < table.colors.size(); ++i) {
    if (i) output << "\n";
    writeMaterial(output, i, table.colors[i]);
  }
  output << "    }\n\n";

  for (size_t i = 0; i < table.colors.size(); ++i) {
    if (i) output << "\n";
    writeMesh(output, *ps, table, i);
  }

  output << "}\n";
}

namespace {

uint32_t crc32(const std::string& data)
{
  static const auto kTable = [] {
    std::vector<uint32_t> table(256);
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = i;
      for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xedb88320u ^ (c >> 1)) : (c >> 1);
      table[i] = c;
    }
    return table;
  }();

  uint32_t c = 0xffffffffu;
  for (const auto ch : data) {
    c = kTable[(c ^ static_cast<unsigned char>(ch)) & 0xff] ^ (c >> 8);
  }
  return c ^ 0xffffffffu;
}

void putU16(std::string& out, uint16_t v)
{
  out.push_back(static_cast<char>(v & 0xff));
  out.push_back(static_cast<char>((v >> 8) & 0xff));
}

void putU32(std::string& out, uint32_t v)
{
  putU16(out, static_cast<uint16_t>(v & 0xffff));
  putU16(out, static_cast<uint16_t>((v >> 16) & 0xffff));
}

}  // namespace

void export_usdz(const std::shared_ptr<const Geometry>& geom, std::ostream& output,
                 const ExportInfo& exportInfo)
{
  std::ostringstream usdaStream;
  export_usda(geom, usdaStream, exportInfo);
  const std::string usda = usdaStream.str();
  const std::string name = "model.usda";

  // ponytail: a stored-only, single-entry zip writer, ~60 lines, rather than a zip
  // dependency. USDZ forbids compression anyway, so nothing here would ever use it.
  // The alignment rule is the whole reason the format bans compression: a reader mmaps
  // each asset in place, so every file's data must start on a 64-byte boundary.
  std::string zip;
  const uint32_t crc = crc32(usda);

  const size_t headerSize = 30 + name.size();
  const size_t padding = (64 - (headerSize % 64)) % 64;

  zip += "PK\x03\x04";
  putU16(zip, 20);  // version needed
  putU16(zip, 0);   // flags
  putU16(zip, 0);   // method: stored, required by USDZ
  putU16(zip, 0);   // mod time
  putU16(zip, 0);   // mod date
  putU32(zip, crc);
  putU32(zip, static_cast<uint32_t>(usda.size()));  // compressed size
  putU32(zip, static_cast<uint32_t>(usda.size()));  // uncompressed size
  putU16(zip, static_cast<uint16_t>(name.size()));
  putU16(zip, static_cast<uint16_t>(padding));  // extra field, used purely as padding
  zip += name;
  zip.append(padding, '\0');
  zip += usda;

  const size_t centralDirectoryOffset = zip.size();
  zip += "PK\x01\x02";
  putU16(zip, 20);  // version made by
  putU16(zip, 20);  // version needed
  putU16(zip, 0);   // flags
  putU16(zip, 0);   // method
  putU16(zip, 0);   // mod time
  putU16(zip, 0);   // mod date
  putU32(zip, crc);
  putU32(zip, static_cast<uint32_t>(usda.size()));
  putU32(zip, static_cast<uint32_t>(usda.size()));
  putU16(zip, static_cast<uint16_t>(name.size()));
  putU16(zip, static_cast<uint16_t>(padding));
  putU16(zip, 0);  // comment length
  putU16(zip, 0);  // disk number
  putU16(zip, 0);  // internal attributes
  putU32(zip, 0);  // external attributes
  putU32(zip, 0);  // offset of local header
  zip += name;
  zip.append(padding, '\0');
  const size_t centralDirectorySize = zip.size() - centralDirectoryOffset;

  zip += "PK\x05\x06";
  putU16(zip, 0);  // disk number
  putU16(zip, 0);  // disk with central directory
  putU16(zip, 1);  // entries on this disk
  putU16(zip, 1);  // entries total
  putU32(zip, static_cast<uint32_t>(centralDirectorySize));
  putU32(zip, static_cast<uint32_t>(centralDirectoryOffset));
  putU16(zip, 0);  // comment length

  output.write(zip.data(), static_cast<std::streamsize>(zip.size()));
}
