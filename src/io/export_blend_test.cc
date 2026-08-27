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

TEST_CASE("BLEND export rejects animations beyond its authored frame capacity", "[export][blend]")
{
  PlatformUtils::registerApplicationPath(
    fs::path(__FILE__).parent_path().parent_path().parent_path().string());
  std::vector<UsdAnimationFrame> frames(257);
  for (auto& frame : frames) frame.geometry = triangle();
  std::ostringstream output;
  REQUIRE_THROWS_WITH(export_blend_animation(frames, 24, output),
                      "Blender export supports at most 256 frames");
}
