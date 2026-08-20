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
#include <tuple>
#include <utility>
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

//! One material's geometry within one frame, re-indexed to its own point list.
struct MeshData {
  std::vector<Vector3d> points;
  std::vector<size_t> faceVertexCounts;
  std::vector<int> faceVertexIndices;
};

//! meshes[frame][material]. A material absent from a frame has an empty MeshData there.
struct Scene {
  std::vector<Color4f> materials;
  std::vector<std::vector<MeshData>> meshes;
};

/*!
   Groups every frame's faces by colour, interning colours across *all* frames so that a
   colour appearing in only some frames still gets exactly one material.

   Splitting per colour (rather than emitting one mesh with a UsdGeomSubset per material)
   keeps the writer simple, and matters more for animation: each material's mesh carries its
   own time-sampled topology, so a colour that vanishes for a few frames simply has empty
   samples there.
 */
Scene buildScene(const std::vector<std::shared_ptr<const Geometry>>& frames, const Color4f& defaultColor)
{
  Scene scene;
  std::map<std::tuple<float, float, float, float>, size_t> seen;

  const auto intern = [&](const Color4f& c) {
    const auto key = std::make_tuple(c.r(), c.g(), c.b(), c.a());
    const auto it = seen.find(key);
    if (it != seen.end()) return it->second;
    const size_t index = scene.materials.size();
    scene.materials.push_back(c);
    seen.emplace(key, index);
    return index;
  };

  // Pass 1: intern colours across every frame, and record each face's material.
  std::vector<std::shared_ptr<const PolySet>> polysets;
  std::vector<std::vector<size_t>> faceMaterials;
  for (const auto& frame : frames) {
    const std::shared_ptr<const PolySet> ps = PolySetUtils::getGeometryAsPolySet(frame);
    std::vector<size_t> faceMaterial;
    const bool hasColor = ps && !ps->color_indices.empty();
    if (ps) {
      for (size_t face = 0; face < ps->indices.size(); ++face) {
        Color4f color = defaultColor;
        if (hasColor && face < ps->color_indices.size()) {
          const auto colorIndex = ps->color_indices[face];
          if (colorIndex >= 0 && static_cast<size_t>(colorIndex) < ps->colors.size()) {
            color = ps->colors[colorIndex];
          }
        }
        faceMaterial.push_back(intern(color));
      }
    }
    polysets.push_back(ps);
    faceMaterials.push_back(std::move(faceMaterial));
  }

  // An empty model still needs one material, or it exports with no shading at all.
  if (scene.materials.empty()) scene.materials.push_back(defaultColor);

  // Pass 2: build the per-frame, per-material meshes now that the material count is known.
  for (size_t frame = 0; frame < polysets.size(); ++frame) {
    std::vector<MeshData> perMaterial(scene.materials.size());
    const auto& ps = polysets[frame];
    if (!ps) {
      scene.meshes.push_back(std::move(perMaterial));
      continue;
    }
    // -1 means "this vertex is not in this material's mesh yet".
    std::vector<std::vector<int>> remap(scene.materials.size(),
                                        std::vector<int>(ps->vertices.size(), -1));
    for (size_t face = 0; face < ps->indices.size(); ++face) {
      const size_t material = faceMaterials[frame][face];
      auto& mesh = perMaterial[material];
      auto& indexOf = remap[material];
      for (const auto vertex : ps->indices[face]) {
        if (indexOf[vertex] < 0) {
          indexOf[vertex] = static_cast<int>(mesh.points.size());
          mesh.points.push_back(ps->vertices[vertex]);
        }
        mesh.faceVertexIndices.push_back(indexOf[vertex]);
      }
      mesh.faceVertexCounts.push_back(ps->indices[face].size());
    }
    scene.meshes.push_back(std::move(perMaterial));
  }
  return scene;
}

std::string formatPoints(const std::vector<Vector3d>& points)
{
  std::ostringstream out;
  out << "[";
  for (size_t i = 0; i < points.size(); ++i) {
    if (i) out << ", ";
    out << "(" << points[i].x() << ", " << points[i].y() << ", " << points[i].z() << ")";
  }
  out << "]";
  return out.str();
}

template <typename T>
std::string formatList(const std::vector<T>& values)
{
  std::ostringstream out;
  out << "[";
  for (size_t i = 0; i < values.size(); ++i) {
    if (i) out << ", ";
    out << values[i];
  }
  out << "]";
  return out.str();
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

//! Emits `attribute = value` for a still, or `attribute.timeSamples = { t: value, ... }`.
template <typename Format>
void writeAttribute(std::ostream& output, const std::string& declaration, const Scene& scene,
                    size_t material, bool animated, Format&& format)
{
  if (!animated) {
    output << "        " << declaration << " = " << format(scene.meshes[0][material]) << "\n";
    return;
  }
  output << "        " << declaration << ".timeSamples = {\n";
  for (size_t frame = 0; frame < scene.meshes.size(); ++frame) {
    output << "            " << frame << ": " << format(scene.meshes[frame][material]) << ",\n";
  }
  output << "        }\n";
}

void writeMesh(std::ostream& output, const Scene& scene, size_t material, bool animated)
{
  const std::string name = "mesh" + std::to_string(material);
  output << "    def Mesh \"" << name << "\" (\n";
  output << "        prepend apiSchemas = [\"MaterialBindingAPI\"]\n";
  output << "    )\n";
  output << "    {\n";

  writeAttribute(output, "int[] faceVertexCounts", scene, material, animated,
                 [](const MeshData& m) { return formatList(m.faceVertexCounts); });
  writeAttribute(output, "int[] faceVertexIndices", scene, material, animated,
                 [](const MeshData& m) { return formatList(m.faceVertexIndices); });
  writeAttribute(output, "point3f[] points", scene, material, animated,
                 [](const MeshData& m) { return formatPoints(m.points); });

  output << "        rel material:binding = </root/Materials/mat" << material << ">\n";
  // Default is catmullClark, which would silently round off every hard edge of a CAD model.
  output << "        uniform token subdivisionScheme = \"none\"\n";
  output << "    }\n";
}

void writeStage(const std::vector<std::shared_ptr<const Geometry>>& frames, unsigned fps, bool animated,
                std::ostream& output, const ExportInfo& exportInfo)
{
  const Scene scene = buildScene(frames, exportInfo.defaultColor);

  output << "#usda 1.0\n";
  output << "(\n";
  output << "    doc = \"Generated by " << EXPORT_CREATOR << " from " << exportInfo.title << "\"\n";
  output << "    defaultPrim = \"root\"\n";
  // OpenSCAD is Z-up and its unit is the millimetre. USD defaults to Y-up and metres, so
  // omitting either of these delivers a model that is rotated and 1000x too large.
  output << "    metersPerUnit = 0.001\n";
  output << "    upAxis = \"Z\"\n";
  if (animated) {
    output << "    startTimeCode = 0\n";
    output << "    endTimeCode = " << (scene.meshes.empty() ? 0 : scene.meshes.size() - 1) << "\n";
    // Note: Blender does NOT read this into scene.render.fps -- verified on 3.3.1. A
    // companion script has to set the frame rate on the importing side.
    output << "    timeCodesPerSecond = " << fps << "\n";
  }
  output << ")\n\n";

  output << "def Xform \"root\"\n";
  output << "{\n";

  output << "    def Scope \"Materials\"\n";
  output << "    {\n";
  for (size_t i = 0; i < scene.materials.size(); ++i) {
    if (i) output << "\n";
    writeMaterial(output, i, scene.materials[i]);
  }
  output << "    }\n\n";

  for (size_t i = 0; i < scene.materials.size(); ++i) {
    if (i) output << "\n";
    writeMesh(output, scene, i, animated);
  }

  output << "}\n";
}

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

/*!
   Wraps one USDA stage in a USDZ container.

   ponytail: a stored-only, single-entry zip writer rather than a zip dependency. USDZ
   forbids compression anyway, so nothing here would ever use it. The alignment rule is the
   whole reason the format bans compression: a reader mmaps each asset in place, so every
   file's data must start on a 64-byte boundary. Padding goes in the local header's *extra*
   field, which is the conventional way to buy those bytes without corrupting the entry.
 */
void writeUsdz(const std::string& usda, std::ostream& output)
{
  const std::string name = "model.usda";
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

}  // namespace

void export_usda(const std::shared_ptr<const Geometry>& geom, std::ostream& output,
                 const ExportInfo& exportInfo)
{
  writeStage({geom}, 0, false, output, exportInfo);
}

void export_usdz(const std::shared_ptr<const Geometry>& geom, std::ostream& output,
                 const ExportInfo& exportInfo)
{
  std::ostringstream usda;
  export_usda(geom, usda, exportInfo);
  writeUsdz(usda.str(), output);
}

void export_usda_animation(const std::vector<std::shared_ptr<const Geometry>>& frames, unsigned fps,
                           std::ostream& output, const ExportInfo& exportInfo)
{
  writeStage(frames, fps, true, output, exportInfo);
}

void export_usdz_animation(const std::vector<std::shared_ptr<const Geometry>>& frames, unsigned fps,
                           std::ostream& output, const ExportInfo& exportInfo)
{
  std::ostringstream usda;
  export_usda_animation(frames, fps, usda, exportInfo);
  writeUsdz(usda.str(), output);
}
