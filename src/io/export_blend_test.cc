#include <catch2/catch_all.hpp>

#include <array>
#include <cmath>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "geometry/PolySet.h"
#include "io/export.h"
#include "platform/PlatformUtils.h"

namespace {

std::shared_ptr<const PolySet> triangle()
{
  auto mesh = std::make_shared<PolySet>(3);
  mesh->vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
  mesh->indices = {{0, 1, 2}};
  return mesh;
}

std::shared_ptr<const PolySet> quad()
{
  auto mesh = std::make_shared<PolySet>(3);
  mesh->vertices = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
  mesh->indices = {{0, 1, 2, 3}};
  return mesh;
}

}  // namespace

TEST_CASE("BLEND export writes a Blender 5.x scene", "[export][blend]")
{
  PlatformUtils::registerApplicationPath(
    fs::path(__FILE__).parent_path().parent_path().parent_path().string());
  const std::vector<UsdAnimationFrame> frames{
    {.geometry = triangle()}, {.geometry = quad()}, {.geometry = triangle()}};
  std::ostringstream output;

  export_blend_animation(frames, 24, output);

  const std::string blend = output.str();
  REQUIRE(blend.compare(0, 15, "BLENDER17-01v05") == 0);
  REQUIRE(blend.find("SC\0\0", 17) != std::string::npos);
  REQUIRE(blend.find("OB\0\0", 17) != std::string::npos);
  REQUIRE(blend.find("ME\0\0", 17) != std::string::npos);
  REQUIRE(blend.find("DNA1", 17) != std::string::npos);
  REQUIRE(blend.find("ENDB", 17) != std::string::npos);
}

TEST_CASE("BLEND is a 3D animation-capable file format", "[export][blend]")
{
  REQUIRE(fileformat::toSuffix(FileFormat::BLEND) == "blend");
  REQUIRE(fileformat::is3D(FileFormat::BLEND));
  REQUIRE(fileformat::canAnimate(FileFormat::BLEND));
}

TEST_CASE("BLEND export samples long animations into its authored mesh capacity", "[export][blend]")
{
  PlatformUtils::registerApplicationPath(
    fs::path(__FILE__).parent_path().parent_path().parent_path().string());
  std::vector<UsdAnimationFrame> frames(300);
  for (size_t frame = 0; frame < frames.size(); ++frame) {
    frames[frame].geometry = frame % 2 ? quad() : triangle();
  }
  std::ostringstream output;

  REQUIRE_NOTHROW(export_blend_animation(frames, 24, output));
  REQUIRE(output.str().compare(0, 15, "BLENDER17-01v05") == 0);
}

TEST_CASE("BLEND export keeps stable geometry as a transform-animated object", "[export][blend]")
{
  PlatformUtils::registerApplicationPath(
    fs::path(__FILE__).parent_path().parent_path().parent_path().string());
  const auto mesh = triangle();
  std::vector<UsdAnimationFrame> frames(300);
  for (size_t frame = 0; frame < frames.size(); ++frame) {
    Transform3d transform = Transform3d::Identity();
    transform.translate(Vector3d(frame, 0, 0));
    frames[frame] = {
      .geometry = mesh,
      .objects = {
        {.geometry = mesh, .transform = transform, .color = Color4f(1, 0, 0, 1), .nodeIndex = 7}}};
  }
  std::ostringstream output;

  export_blend_animation(frames, 24, output);

  REQUIRE(output.str().find("OBStable 0001") != std::string::npos);
}

TEST_CASE("BLEND export combines transform animation with shared remesh samples", "[export][blend]")
{
  PlatformUtils::registerApplicationPath(
    fs::path(__FILE__).parent_path().parent_path().parent_path().string());
  const auto stable = triangle();
  const std::vector<std::shared_ptr<const PolySet>> changing{triangle(), quad()};
  Transform3d separated = Transform3d::Identity();
  separated.translate(Vector3d(10, 0, 0));
  std::vector<UsdAnimationFrame> frames(300);
  for (size_t frame = 0; frame < frames.size(); ++frame) {
    frames[frame] = {.geometry = stable,
                     .objects = {{stable, Transform3d::Identity(), Color4f(1, 0, 0, 1), 1},
                                 {changing[frame % 2], separated, Color4f(0, 0, 1, 1), 2}}};
  }
  std::ostringstream output;

  REQUIRE_NOTHROW(export_blend_animation(frames, 24, output));
  REQUIRE(output.str().find("OBStable 0001") != std::string::npos);
  REQUIRE(output.str().find("OBFrame 0256") != std::string::npos);
}

TEST_CASE("BLEND export applies camera and body colours", "[export][blend]")
{
  PlatformUtils::registerApplicationPath(
    fs::path(__FILE__).parent_path().parent_path().parent_path().string());
  const auto mesh = triangle();
  Camera camera;
  camera.setVpt(12.3456, 23.4567, 34.5678);
  camera.setVpr(17.25, 28.5, 39.75);
  camera.setVpd(246.8135);
  camera.setVpf(31.4159);
  Camera secondCamera = camera;
  secondCamera.setVpf(60);
  secondCamera.setProjection(Camera::ProjectionType::ORTHOGONAL);
  const Color4f color(0.123456f, 0.234567f, 0.345678f, 0.456789f);
  const Color4f defaultColor(0.654321f, 0.54321f, 0.4321f, 1.0f);
  Transform3d shifted = Transform3d::Identity();
  shifted.translate(Vector3d(10, 0, 0));
  const std::vector<UsdAnimationObject> objects{{mesh, Transform3d::Identity(), color, 1},
                                                {mesh, shifted, Color4f(), 2}};
  const std::vector<UsdAnimationFrame> frames{
    {.geometry = mesh, .objects = objects, .camera = camera},
    {.geometry = mesh, .objects = objects, .camera = secondCamera}};
  std::ostringstream output;

  export_blend_animation(frames, 24, output, {.defaultColor = defaultColor});

  const std::string blend = output.str();
  const std::array<float, 4> components{color.r(), color.g(), color.b(), color.a()};
  const std::string rgba(reinterpret_cast<const char *>(components.data()), 4 * sizeof(float));
  const std::array<float, 4> defaultComponents{defaultColor.r(), defaultColor.g(), defaultColor.b(),
                                               defaultColor.a()};
  const std::string defaultRgba(reinterpret_cast<const char *>(defaultComponents.data()),
                                4 * sizeof(float));
  REQUIRE(blend.find(rgba) != std::string::npos);
  REQUIRE(blend.find(defaultRgba) != std::string::npos);
  const float secondLens = 36.0f / (2 * std::tan(float(std::acos(-1.0) / 6)));
  const float secondOrthoScale = 2 * secondCamera.zoomValue() * std::tan(float(std::acos(-1.0) / 6));
  REQUIRE(blend.find(std::string(reinterpret_cast<const char *>(&secondLens), sizeof(secondLens))) !=
          std::string::npos);
  REQUIRE(blend.find(std::string(reinterpret_cast<const char *>(&secondOrthoScale),
                                 sizeof(secondOrthoScale))) != std::string::npos);
  REQUIRE(blend.find("OpenSCAD Camera") != std::string::npos);
}

TEST_CASE("BLEND remesh sample budget is configurable", "[export][blend]")
{
  PlatformUtils::registerApplicationPath(
    fs::path(__FILE__).parent_path().parent_path().parent_path().string());
  std::vector<UsdAnimationFrame> frames(300);
  for (size_t frame = 0; frame < frames.size(); ++frame) {
    frames[frame].geometry = frame % 2 ? quad() : triangle();
  }
  std::ostringstream threeSamples;
  std::ostringstream fourSamples;

  export_blend_animation(frames, 24, threeSamples, {.remeshSamples = 3});
  export_blend_animation(frames, 24, fourSamples, {.remeshSamples = 4});

  REQUIRE(threeSamples.str() != fourSamples.str());
  REQUIRE_THROWS_WITH(export_blend_animation(frames, 24, threeSamples, {.remeshSamples = 0}),
                      "Blender remesh samples must be in range 1..256");
}

TEST_CASE("BLEND transform fast path supports more than 256 objects", "[export][blend]")
{
  PlatformUtils::registerApplicationPath(
    fs::path(__FILE__).parent_path().parent_path().parent_path().string());
  const auto mesh = triangle();
  std::vector<UsdAnimationObject> objects;
  for (int object = 0; object < 257; ++object) {
    Transform3d transform = Transform3d::Identity();
    transform.translate(Vector3d(object * 2, 0, 0));
    objects.push_back({mesh, transform, Color4f(1, 0, 0, 1), object});
  }
  const std::vector<UsdAnimationFrame> frames{{.geometry = mesh, .objects = objects},
                                              {.geometry = mesh, .objects = objects}};
  std::ostringstream output;

  export_blend_animation(frames, 24, output);

  REQUIRE(output.str().find("OBStable 0257") != std::string::npos);
}

TEST_CASE("BLEND export computes sharp edges from smooth angle threshold", "[export][blend]")
{
  PlatformUtils::registerApplicationPath(
    fs::path(__FILE__).parent_path().parent_path().parent_path().string());
  auto mesh = std::make_shared<PolySet>(3);
  mesh->vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
  mesh->indices = {{0, 1, 2}, {0, 3, 1}};

  std::ostringstream outputSharp;
  BlendExportOptions optionsSharp;
  optionsSharp.smoothAngle = 24.0;
  export_blend_animation({{.geometry = mesh}}, 24, outputSharp, optionsSharp);

  std::ostringstream outputSmooth;
  BlendExportOptions optionsSmooth;
  optionsSmooth.smoothAngle = 100.0;
  export_blend_animation({{.geometry = mesh}}, 24, outputSmooth, optionsSmooth);

  REQUIRE(outputSharp.str() != outputSmooth.str());
}

TEST_CASE("BLEND export takes the smooth angle from the geometry", "[export][blend]")
{
  PlatformUtils::registerApplicationPath(
    fs::path(__FILE__).parent_path().parent_path().parent_path().string());

  // Same mesh twice, differing only in the angle it records. The two faces meet at
  // 90 degrees, so a tolerance either side of that decides whether the edge is sharp.
  const auto build = [](double smoothAngle) {
    auto mesh = std::make_shared<PolySet>(3);
    mesh->vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    mesh->indices = {{0, 1, 2}, {0, 3, 1}};
    mesh->setSmoothAngle(smoothAngle);
    return mesh;
  };

  std::ostringstream sharp;
  export_blend_animation({{.geometry = build(24.0)}}, 24, sharp, BlendExportOptions{});

  std::ostringstream smooth;
  export_blend_animation({{.geometry = build(120.0)}}, 24, smooth, BlendExportOptions{});

  // Without this, both runs use the hardcoded 24 degree default and the geometry's own
  // angle - the $fa it was tessellated at - is ignored, so a model shades one way in
  // the viewport and another in Blender.
  REQUIRE(sharp.str() != smooth.str());
}
