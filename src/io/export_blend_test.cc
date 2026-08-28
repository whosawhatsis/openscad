#include <catch2/catch_all.hpp>

#include <memory>
#include <sstream>
#include <string>

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

TEST_CASE("BLEND export writes a Blender 5.0.1 scene", "[export][blend]")
{
  PlatformUtils::registerApplicationPath(
    fs::path(__FILE__).parent_path().parent_path().parent_path().string());
  const std::vector<UsdAnimationFrame> frames{
    {.geometry = triangle()}, {.geometry = quad()}, {.geometry = triangle()}};
  std::ostringstream output;

  export_blend_animation(frames, 24, output);

  const std::string blend = output.str();
  REQUIRE(blend.compare(0, 17, "BLENDER17-01v0500") == 0);
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
  REQUIRE(output.str().compare(0, 17, "BLENDER17-01v0500") == 0);
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
