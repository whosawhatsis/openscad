// Tests for the USD (USDA / USDZ) exporter.
//
// These are written before the exporter exists, per the project's test-first rule. The
// assertions describe structure rather than comparing against a golden blob, because the
// facts that matter are the ones Blender's importer was verified to depend on:
//
//   - color must reach Blender as a bound UsdPreviewSurface, NOT as primvars:displayColor
//     (Blender 3.3.1 silently drops displayColor), and
//   - opacity must survive, because OpenSCAD's color() carries an alpha.
//
// A byte-exact regression test over the emitted text lives separately, as a cmdline test.

#include <catch2/catch_all.hpp>

#include <memory>
#include <sstream>
#include <string>

#include "Feature.h"
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

//! Two disjoint triangles carrying different colors, to exercise per-face color.
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

//! Count non-overlapping occurrences, so "one material per distinct color" is checkable.
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
  // OpenSCAD is Z-up and works in millimeters; both must be declared or the model
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

TEST_CASE("USDA export emits color as a bound UsdPreviewSurface", "[export][usd]")
{
  const std::string usda = exportToString(makeTwoColouredTriangles());

  // Verified on Blender 3.3.1: primvars:displayColor is NOT imported, so color expressed
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

TEST_CASE("USDA export reuses one material per distinct color", "[export][usd]")
{
  auto ps = std::make_unique<PolySet>(3);
  ps->vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {2, 0, 0}, {3, 0, 0}, {2, 1, 0}};
  ps->indices = {{0, 1, 2}, {3, 4, 5}};
  ps->colors = {Color4f(1.0f, 0.0f, 0.0f, 1.0f)};
  ps->color_indices = {0, 0};  // both faces share a color

  const std::string usda = exportToString(std::move(ps));

  // One Shader prim, not one per face -- otherwise a large model emits a material per triangle.
  REQUIRE(countOccurrences(usda, "uniform token info:id = \"UsdPreviewSurface\"") == 1);
}

TEST_CASE("USDA export falls back to the default color when the PolySet has none", "[export][usd]")
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

// --- Animation ---------------------------------------------------------------------------
//
// OpenSCAD re-evaluates the whole script per frame, so topology may change arbitrarily
// between frames. USD models that directly: points/faceVertexCounts/faceVertexIndices are
// time-sampled attributes. Verified on Blender 3.3.1, which imports such a mesh via an
// automatically-attached MESH_SEQUENCE_CACHE modifier.

namespace {

//! Frame 0 is a triangle, frame 1 a quad: the vertex *count* changes, which is the whole point.
std::vector<std::shared_ptr<const Geometry>> makeVaryingTopologyFrames()
{
  auto tri = std::make_unique<PolySet>(3);
  tri->vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
  tri->indices = {{0, 1, 2}};

  auto quad = std::make_unique<PolySet>(3);
  quad->vertices = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
  quad->indices = {{0, 1, 2, 3}};

  return {std::shared_ptr<const Geometry>(std::move(tri)),
          std::shared_ptr<const Geometry>(std::move(quad))};
}

std::string exportAnimationToString(const std::vector<std::shared_ptr<const Geometry>>& frames,
                                    unsigned fps = 30)
{
  std::ostringstream out;
  export_usda_animation(frames, fps, out, usdaExportInfo());
  return out.str();
}

std::string exportAnimationToString(const std::vector<UsdAnimationFrame>& frames, unsigned fps = 30)
{
  std::ostringstream out;
  export_usda_animation(frames, fps, out, usdaExportInfo());
  return out.str();
}

}  // namespace

TEST_CASE("Animated USDA declares the time range and rate", "[export][usd]")
{
  const std::string usda = exportAnimationToString(makeVaryingTopologyFrames(), 30);

  REQUIRE(usda.find("startTimeCode = 0") != std::string::npos);
  REQUIRE(usda.find("endTimeCode = 1") != std::string::npos);
  REQUIRE(usda.find("timeCodesPerSecond = 30") != std::string::npos);
}

TEST_CASE("Animated USDA time-samples topology, not just points", "[export][usd]")
{
  const std::string usda = exportAnimationToString(makeVaryingTopologyFrames());

  // All three must be time-sampled. Sampling points alone would be a constant-topology
  // deformation, which cannot represent a frame whose vertex count changed.
  REQUIRE(usda.find("point3f[] points.timeSamples = {") != std::string::npos);
  REQUIRE(usda.find("int[] faceVertexCounts.timeSamples = {") != std::string::npos);
  REQUIRE(usda.find("int[] faceVertexIndices.timeSamples = {") != std::string::npos);

  // Frame 0: a triangle. Frame 1: a quad.
  REQUIRE(usda.find("0: [3],") != std::string::npos);
  REQUIRE(usda.find("1: [4],") != std::string::npos);
  REQUIRE(usda.find("0: [(0, 0, 0), (1, 0, 0), (0, 1, 0)],") != std::string::npos);
}

TEST_CASE("Animated USDA prevents interpolation between recalculated point arrays", "[export][usd]")
{
  const std::vector<std::shared_ptr<const Geometry>> frames{
    std::shared_ptr<const Geometry>(makeTriangle()), std::shared_ptr<const Geometry>(makeTriangle())};
  const std::string usda = exportAnimationToString(frames);

  // USD holds numeric arrays of unequal lengths. The unused duplicate final point makes that
  // explicit without changing topology or bounds; unlike .999 samples, there is no morph window.
  REQUIRE(usda.find(".999:") == std::string::npos);
  REQUIRE(usda.find("0: [(0, 0, 0), (1, 0, 0), (0, 1, 0)],") != std::string::npos);
  REQUIRE(usda.find("1: [(0, 0, 0), (1, 0, 0), (0, 1, 0), (0, 0, 0)],") != std::string::npos);
  REQUIRE(usda.find("1: [0, 1, 2],") != std::string::npos);
}

TEST_CASE("Animated USDA reuses cache-stable geometry and samples only its transform", "[export][usd]")
{
  const auto triangle = std::shared_ptr<const PolySet>(makeTriangle());
  Transform3d moved = Transform3d::Identity();
  moved.translate(Vector3d(10, 20, 30));

  const std::vector<UsdAnimationFrame> frames{
    {.geometry = triangle,
     .objects = {{.geometry = triangle,
                  .transform = Transform3d::Identity(),
                  .color = Color4f(1, 0, 0, 1),
                  .nodeIndex = 7}}},
    {.geometry = triangle,
     .objects =
       {{.geometry = triangle, .transform = moved, .color = Color4f(1, 0, 0, 1), .nodeIndex = 7}}},
  };

  const std::string usda = exportAnimationToString(frames);

  REQUIRE(usda.find("double3 xformOp:translate.timeSamples = {") != std::string::npos);
  REQUIRE(usda.find("quatf xformOp:orient.timeSamples = {") != std::string::npos);
  REQUIRE(usda.find("uniform token[] xformOpOrder = [\"xformOp:translate\", \"xformOp:orient\"]") !=
          std::string::npos);
  REQUIRE(usda.find("matrix4d xformOp:transform") == std::string::npos);
  REQUIRE(usda.find("point3f[] points.timeSamples") == std::string::npos);
  REQUIRE(countOccurrences(usda, "point3f[] points =") == 1);
}

TEST_CASE("Animated USDA optimizes stable objects beside changing geometry", "[export][usd]")
{
  const auto stable = std::shared_ptr<const PolySet>(makeTriangle());
  const auto changing0 = std::shared_ptr<const PolySet>(makeTriangle());
  auto quad = std::make_shared<PolySet>(3);
  quad->vertices = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
  quad->indices = {{0, 1, 2, 3}};

  const auto object = [](const std::shared_ptr<const PolySet>& geometry, int nodeIndex, double x) {
    Transform3d transform = Transform3d::Identity();
    transform.translate(Vector3d(x, 0, 0));
    return UsdAnimationObject{geometry, transform, Color4f(1, 0, 0, 1), nodeIndex};
  };
  const std::vector<UsdAnimationFrame> frames{
    {.geometry = stable, .objects = {object(stable, 1, 0), object(changing0, 2, 10)}},
    {.geometry = stable, .objects = {object(stable, 1, 0), object(quad, 2, 10)}},
  };

  const std::string usda = exportAnimationToString(frames);

  REQUIRE(countOccurrences(usda, "point3f[] points =") == 1);
  REQUIRE(countOccurrences(usda, "point3f[] points.timeSamples = {") == 1);
  REQUIRE(usda.find("1: [(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0)],") != std::string::npos);
}

TEST_CASE("Animated USDA keeps overlapping union members on the flattened fallback", "[export][usd]")
{
  const auto first = std::shared_ptr<const PolySet>(makeTriangle());
  const auto second = std::shared_ptr<const PolySet>(makeTriangle());
  const auto object = [](const std::shared_ptr<const PolySet>& geometry, int nodeIndex) {
    return UsdAnimationObject{geometry, Transform3d::Identity(), Color4f(1, 0, 0, 1), nodeIndex};
  };
  const std::vector<UsdAnimationFrame> frames{
    {.geometry = first, .objects = {object(first, 1), object(second, 2)}},
    {.geometry = first, .objects = {object(first, 1), object(second, 2)}},
  };

  const std::string usda = exportAnimationToString(frames);

  REQUIRE(usda.find("xformOp:") == std::string::npos);
  REQUIRE(usda.find("point3f[] points.timeSamples = {") != std::string::npos);
}

TEST_CASE("Animated USDA keeps per-color materials across frames", "[export][usd]")
{
  auto red = std::make_unique<PolySet>(3);
  red->vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
  red->indices = {{0, 1, 2}};
  red->colors = {Color4f(1.0f, 0.0f, 0.0f, 1.0f)};
  red->color_indices = {0};

  auto blue = std::make_unique<PolySet>(3);
  blue->vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
  blue->indices = {{0, 1, 2}};
  blue->colors = {Color4f(0.0f, 0.0f, 1.0f, 1.0f)};
  blue->color_indices = {0};

  const std::vector<std::shared_ptr<const Geometry>> frames{
    std::shared_ptr<const Geometry>(std::move(red)), std::shared_ptr<const Geometry>(std::move(blue))};

  const std::string usda = exportAnimationToString(frames);

  // A color that appears in only one frame must still get its own material, or the model
  // changes color by losing its material rather than by switching mesh.
  REQUIRE(countOccurrences(usda, "uniform token info:id = \"UsdPreviewSurface\"") == 2);
  REQUIRE(usda.find("color3f inputs:diffuseColor = (1, 0, 0)") != std::string::npos);
  REQUIRE(usda.find("color3f inputs:diffuseColor = (0, 0, 1)") != std::string::npos);
}

TEST_CASE("A single-frame animation still emits time samples", "[export][usd]")
{
  // --animate 1 is legal; it must not silently produce a static file with a different shape.
  const std::string usda = exportAnimationToString({makeVaryingTopologyFrames()[0]});

  REQUIRE(usda.find("startTimeCode = 0") != std::string::npos);
  REQUIRE(usda.find("endTimeCode = 0") != std::string::npos);
  REQUIRE(usda.find(".timeSamples = {") != std::string::npos);
}

TEST_CASE("Static USDA has no time samples at all", "[export][usd]")
{
  // Guard against the animated path leaking into the static one.
  const std::string usda = exportToString(makeTriangle());

  REQUIRE(usda.find("timeSamples") == std::string::npos);
  REQUIRE(usda.find("startTimeCode") == std::string::npos);
}

TEST_CASE("Animated USDZ wraps the animated stage", "[export][usd]")
{
  std::ostringstream out;
  auto info = usdaExportInfo();
  info.format = FileFormat::USDZ;
  info.info = fileformat::info(FileFormat::USDZ);
  export_usdz_animation(makeVaryingTopologyFrames(), 30, out, info);
  const std::string usdz = out.str();

  REQUIRE(usdz.rfind("PK\x03\x04", 0) == 0);
  REQUIRE(usdz.find("timeCodesPerSecond = 30") != std::string::npos);
}

TEST_CASE("USD formats are animatable but do not require --animate", "[export][usd]")
{
  // USD differs from the video containers: it is a valid static file too. isAnimation()
  // means "needs --animate" and must stay false, or a plain export starts erroring.
  REQUIRE_FALSE(fileformat::isAnimation(FileFormat::USDA));
  REQUIRE_FALSE(fileformat::isAnimation(FileFormat::USDZ));
  REQUIRE(fileformat::canAnimate(FileFormat::USDA));
  REQUIRE(fileformat::canAnimate(FileFormat::USDZ));

  // The video containers are both.
  REQUIRE(fileformat::canAnimate(FileFormat::GIF));
}

TEST_CASE("USDA export honors predictible-output", "[export][usd]")
{
  // Every other mesh exporter (STL, OBJ, OFF, WRL, POV, 3MF) sorts its PolySet when this
  // feature is on. Without it the emitted vertex order depends on the geometry backend's
  // internal ordering, which makes the cmdline regression tests non-deterministic.
  auto ps = std::make_unique<PolySet>(3);
  ps->vertices = {{5, 5, 5}, {0, 0, 0}, {1, 1, 1}};
  ps->indices = {{0, 1, 2}};

  Feature::enable_feature("predictible-output", true);
  const std::string sorted = exportToString(std::move(ps));
  Feature::enable_feature("predictible-output", false);

  // Sorted export must emit the lowest vertex first, whatever order it arrived in.
  REQUIRE(sorted.find("point3f[] points = [(0, 0, 0),") != std::string::npos);
}
