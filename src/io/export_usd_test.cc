// Tests for the USD (USDA / USDZ) exporter.
//
// These are written before the exporter exists, per the project's test-first rule. The
// assertions describe structure rather than comparing against a golden blob, because the
// facts that matter are the ones Blender's importer was verified to depend on:
//
//   - colour must reach Blender as a bound UsdPreviewSurface, NOT as primvars:displayColor
//     (Blender 3.3.1 silently drops displayColor), and
//   - opacity must survive, because OpenSCAD's color() carries an alpha.
//
// A byte-exact regression test over the emitted text lives separately, as a cmdline test.

#include <catch2/catch_all.hpp>

#include <memory>
#include <sstream>
#include <string>

#include "geometry/PolySet.h"
#include "geometry/linalg.h"
#include "io/export.h"

namespace {

//! A single triangle, the smallest thing with a face.
std::unique_ptr<PolySet> makeTriangle()
{
  auto ps = std::make_unique<PolySet>(3);
  ps->vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
  ps->indices = {{0, 1, 2}};
  return ps;
}

//! Two disjoint triangles carrying different colours, to exercise per-face colour.
std::unique_ptr<PolySet> makeTwoColouredTriangles()
{
  auto ps = std::make_unique<PolySet>(3);
  ps->vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {2, 0, 0}, {3, 0, 0}, {2, 1, 0}};
  ps->indices = {{0, 1, 2}, {3, 4, 5}};
  ps->colors = {Color4f(1.0f, 0.0f, 0.0f, 1.0f), Color4f(0.0f, 0.0f, 1.0f, 0.5f)};
  ps->color_indices = {0, 1};
  return ps;
}

ExportInfo usdaExportInfo()
{
  return ExportInfo{
    .format = FileFormat::USDA,
    .info = fileformat::info(FileFormat::USDA),
    .title = "test",
    .sourceFilePath = "test.scad",
    .camera = nullptr,
    .defaultColor = Color4f(0.8f, 0.8f, 0.8f, 1.0f),
    .colorScheme = nullptr,
  };
}

std::string exportToString(const std::shared_ptr<const Geometry>& geom)
{
  std::ostringstream out;
  export_usda(geom, out, usdaExportInfo());
  return out.str();
}

//! Count non-overlapping occurrences, so "one material per distinct colour" is checkable.
size_t countOccurrences(const std::string& haystack, const std::string& needle)
{
  size_t count = 0;
  for (size_t pos = haystack.find(needle); pos != std::string::npos;
       pos = haystack.find(needle, pos + needle.size())) {
    ++count;
  }
  return count;
}

}  // namespace

TEST_CASE("USDA export emits a parseable header", "[export][usd]")
{
  const std::string usda = exportToString(makeTriangle());

  // The magic line is mandatory and must be first: a USD reader rejects the file otherwise.
  REQUIRE(usda.rfind("#usda 1.0", 0) == 0);
  // OpenSCAD is Z-up and works in millimetres; both must be declared or the model
  // arrives rotated and 1000x the wrong size.
  REQUIRE(usda.find("upAxis = \"Z\"") != std::string::npos);
  REQUIRE(usda.find("metersPerUnit = 0.001") != std::string::npos);
}

TEST_CASE("USDA export emits mesh topology matching the PolySet", "[export][usd]")
{
  const std::string usda = exportToString(makeTriangle());

  REQUIRE(usda.find("def Mesh") != std::string::npos);
  REQUIRE(usda.find("int[] faceVertexCounts = [3]") != std::string::npos);
  REQUIRE(usda.find("int[] faceVertexIndices = [0, 1, 2]") != std::string::npos);
  REQUIRE(usda.find("point3f[] points = [(0, 0, 0), (1, 0, 0), (0, 1, 0)]") != std::string::npos);
}

TEST_CASE("USDA export emits colour as a bound UsdPreviewSurface", "[export][usd]")
{
  const std::string usda = exportToString(makeTwoColouredTriangles());

  // Verified on Blender 3.3.1: primvars:displayColor is NOT imported, so colour expressed
  // that way is silently lost. It must be a bound material instead.
  REQUIRE(usda.find("primvars:displayColor") == std::string::npos);

  REQUIRE(countOccurrences(usda, "uniform token info:id = \"UsdPreviewSurface\"") == 2);
  REQUIRE(usda.find("color3f inputs:diffuseColor = (1, 0, 0)") != std::string::npos);
  REQUIRE(usda.find("color3f inputs:diffuseColor = (0, 0, 1)") != std::string::npos);

  // Alpha must survive: OpenSCAD's color() carries one and Blender maps opacity to Alpha.
  REQUIRE(usda.find("float inputs:opacity = 0.5") != std::string::npos);

  // A material is useless unless it is actually bound to the geometry.
  REQUIRE(usda.find("rel material:binding") != std::string::npos);
  REQUIRE(usda.find("MaterialBindingAPI") != std::string::npos);
}

TEST_CASE("USDA export reuses one material per distinct colour", "[export][usd]")
{
  auto ps = std::make_unique<PolySet>(3);
  ps->vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {2, 0, 0}, {3, 0, 0}, {2, 1, 0}};
  ps->indices = {{0, 1, 2}, {3, 4, 5}};
  ps->colors = {Color4f(1.0f, 0.0f, 0.0f, 1.0f)};
  ps->color_indices = {0, 0};  // both faces share a colour

  const std::string usda = exportToString(std::move(ps));

  // One Shader prim, not one per face -- otherwise a large model emits a material per triangle.
  REQUIRE(countOccurrences(usda, "uniform token info:id = \"UsdPreviewSurface\"") == 1);
}

TEST_CASE("USDA export falls back to the default colour when the PolySet has none", "[export][usd]")
{
  const std::string usda = exportToString(makeTriangle());

  // defaultColor above is 0.8 grey; an uncoloured model must still get a material rather
  // than arriving in Blender untextured.
  REQUIRE(usda.find("color3f inputs:diffuseColor = (0.8, 0.8, 0.8)") != std::string::npos);
}

TEST_CASE("USDZ is a zip whose first entry is an uncompressed, aligned USDA", "[export][usd]")
{
  std::ostringstream out;
  auto info = usdaExportInfo();
  info.format = FileFormat::USDZ;
  info.info = fileformat::info(FileFormat::USDZ);
  export_usdz(makeTriangle(), out, info);
  const std::string usdz = out.str();

  // Local file header magic.
  REQUIRE(usdz.rfind("PK\x03\x04", 0) == 0);

  // The USDZ spec requires stored (uncompressed) entries: compression method 0 at
  // offset 8 of the local file header.
  REQUIRE(usdz[8] == '\0');
  REQUIRE(usdz[9] == '\0');

  // ...and each file's data must start on a 64-byte boundary so readers can mmap it.
  const size_t nameLength =
    static_cast<unsigned char>(usdz[26]) | (static_cast<unsigned char>(usdz[27]) << 8);
  const size_t extraLength =
    static_cast<unsigned char>(usdz[28]) | (static_cast<unsigned char>(usdz[29]) << 8);
  const size_t dataOffset = 30 + nameLength + extraLength;
  REQUIRE(dataOffset % 64 == 0);

  // The first entry must be the USDA itself, and it must be the file the stage resolves to.
  REQUIRE(usdz.find(".usdc") == std::string::npos);
  REQUIRE(usdz.compare(dataOffset, 9, "#usda 1.0") == 0);
}
