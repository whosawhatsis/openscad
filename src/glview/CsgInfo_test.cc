#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <memory>

#include "core/CSGNode.h"
#include "geometry/PolySet.h"
#include "glview/CsgInfo.h"

namespace fs = std::filesystem;

namespace {

std::shared_ptr<PolySet> makeTriangle(double smoothAngle)
{
  auto ps = std::make_shared<PolySet>(3);
  ps->vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
  ps->indices = {{0, 1, 2}, {0, 3, 1}};
  ps->setSmoothAngle(smoothAngle);
  return ps;
}

}  // namespace

TEST_CASE("preview products carry the smoothing angle over the wire", "[csginfo]")
{
  // The compute worker rebuilds every leaf from this format, so anything the format
  // does not carry is silently replaced by a default on the far side. That is what
  // happened to roughness and metallic, and the same hole swallowed the smoothing
  // tolerance: a model previewed through a worker shaded at the default no matter
  // what $fa it was built with.
  const double angle = 7.5;

  CsgInfo written;
  written.root_products = std::make_shared<CSGProducts>();
  auto leaf = std::make_shared<CSGLeaf>(makeTriangle(angle), Transform3d::Identity(), Color4f(), "",
                                        -1.0f, 0.0f, "leaf", 0);
  written.root_products->products.back().intersections.emplace_back(leaf);

  const auto path = (fs::temp_directory_path() / "openscad-csginfo-smoothangle.json").string();
  REQUIRE(written.write_products(path));

  CsgInfo read;
  REQUIRE(read.read_products(path));
  REQUIRE(read.root_products);
  REQUIRE(!read.root_products->products.empty());
  REQUIRE(!read.root_products->products.front().intersections.empty());

  const auto& polyset = read.root_products->products.front().intersections.front().leaf->polyset;
  REQUIRE(polyset);
  CHECK(polyset->smoothAngle() == angle);
}
