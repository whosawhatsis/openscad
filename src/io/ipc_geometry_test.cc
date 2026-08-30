// Binary geometry codec for the compute worker (row 59).
//
// What is being defended here is parity with the in-process path. Everything a PolySet or
// Polygon2d carries has to survive the process boundary, because anything that does not is a
// visible regression against running the geometry in-process -- not a "minimal first version".
// Color and convexity in particular: dropping them costs ~16 lines and loses per-face color and
// correct preview of concave objects, which blocks a merge.

#include "io/ipc_geometry.h"

#include <catch2/catch_all.hpp>
#include <memory>
#include <sstream>
#include <string>

#include "geometry/Geometry.h"
#include "geometry/PolySet.h"
#include "geometry/Polygon2d.h"
#include "geometry/linalg.h"

namespace {

std::string encode(const std::shared_ptr<const Geometry>& geom)
{
  std::ostringstream out(std::ios::binary);
  export_ipc_geometry(geom, out);
  return out.str();
}

std::shared_ptr<const Geometry> decode(const std::string& bytes, const std::string& name = "test")
{
  return import_ipc_geometry_buffer(bytes.data(), bytes.size(), name);
}

// A tetrahedron with a distinct color per face, so a codec that drops either the palette or the
// per-face indices fails rather than coincidentally passing.
std::shared_ptr<PolySet> coloredTetrahedron()
{
  auto ps = std::make_shared<PolySet>(3);
  ps->vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
  ps->indices = {{0, 2, 1}, {0, 1, 3}, {0, 3, 2}, {1, 2, 3}};
  ps->colors = {Color4f(1.0f, 0.0f, 0.0f, 1.0f), Color4f(0.0f, 1.0f, 0.0f, 0.5f),
                Color4f(0.0f, 0.0f, 1.0f, 1.0f)};
  // Deliberately includes -1 ("no specific color") and a repeated index, which is what the real
  // per-polygon representation looks like.
  ps->color_indices = {0, 1, -1, 2};
  ps->setConvexity(7);
  return ps;
}

std::shared_ptr<Polygon2d> squareWithHole()
{
  auto poly = std::make_shared<Polygon2d>();
  Outline2d outer;
  outer.vertices = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};
  outer.positive = true;
  Outline2d hole;
  hole.vertices = {{3, 3}, {3, 6}, {6, 6}, {6, 3}};
  hole.positive = false;
  poly->addOutline(outer);
  poly->addOutline(hole);
  poly->setConvexity(5);
  return poly;
}

}  // namespace

TEST_CASE("IPC geometry codec preserves mesh color", "[io][IPC][IPC-Geometry]")
{
  const auto original = coloredTetrahedron();
  const auto decoded = std::dynamic_pointer_cast<const PolySet>(decode(encode(original)));
  REQUIRE(decoded);

  SECTION("the color palette survives, including alpha")
  {
    REQUIRE(decoded->colors.size() == original->colors.size());
    for (std::size_t i = 0; i < original->colors.size(); ++i) {
      CHECK(decoded->colors[i].r() == Catch::Approx(original->colors[i].r()));
      CHECK(decoded->colors[i].g() == Catch::Approx(original->colors[i].g()));
      CHECK(decoded->colors[i].b() == Catch::Approx(original->colors[i].b()));
      CHECK(decoded->colors[i].a() == Catch::Approx(original->colors[i].a()));
    }
  }

  SECTION("per-polygon color indices survive, including -1 for no specific color")
  {
    CHECK(decoded->color_indices == original->color_indices);
  }

  SECTION("an uncolored PolySet round-trips with empty color data, not a default palette")
  {
    auto plain = std::make_shared<PolySet>(3);
    plain->vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    plain->indices = {{0, 1, 2}};
    const auto back = std::dynamic_pointer_cast<const PolySet>(decode(encode(plain)));
    REQUIRE(back);
    CHECK(back->colors.empty());
    CHECK(back->color_indices.empty());
  }
}

TEST_CASE("IPC geometry codec preserves convexity", "[io][IPC][IPC-Geometry]")
{
  // Convexity drives the number of depth-peeling passes in the OpenCSG preview. Losing it renders
  // concave objects wrongly, so it is carried for both geometry types.
  SECTION("on a PolySet")
  {
    const auto decoded = std::dynamic_pointer_cast<const PolySet>(decode(encode(coloredTetrahedron())));
    REQUIRE(decoded);
    CHECK(decoded->getConvexity() == 7);
  }

  SECTION("on a Polygon2d")
  {
    const auto decoded = std::dynamic_pointer_cast<const Polygon2d>(decode(encode(squareWithHole())));
    REQUIRE(decoded);
    CHECK(decoded->getConvexity() == 5);
  }
}

TEST_CASE("IPC geometry codec preserves mesh topology", "[io][IPC][IPC-Geometry]")
{
  const auto original = coloredTetrahedron();
  const auto decoded = std::dynamic_pointer_cast<const PolySet>(decode(encode(original)));
  REQUIRE(decoded);
  CHECK(decoded->vertices == original->vertices);
  CHECK(decoded->indices == original->indices);
  CHECK(decoded->getDimension() == 3);
}

TEST_CASE("IPC geometry codec preserves 2D geometry", "[io][IPC][IPC-Geometry]")
{
  const auto original = squareWithHole();
  const auto decoded = std::dynamic_pointer_cast<const Polygon2d>(decode(encode(original)));
  REQUIRE(decoded);

  REQUIRE(decoded->outlines().size() == 2);
  CHECK(decoded->outlines()[0].vertices == original->outlines()[0].vertices);
  CHECK(decoded->outlines()[0].positive);
  // A hole that comes back positive silently fills in, which is the failure mode worth naming.
  CHECK_FALSE(decoded->outlines()[1].positive);
  CHECK(decoded->outlines()[1].vertices == original->outlines()[1].vertices);
  CHECK(decoded->getDimension() == 2);
}

TEST_CASE("IPC geometry codec round-trips a multi-body result", "[io][IPC][IPC-Geometry]")
{
  // A render can produce several bodies. Preview leaves are always a single body, which is why
  // import_ipc_polyset_buffer exists as the unwrapped form -- but the list form has to work too.
  Geometry::Geometries bodies;
  bodies.emplace_back(nullptr, coloredTetrahedron());
  bodies.emplace_back(nullptr, squareWithHole());

  const auto decoded = std::dynamic_pointer_cast<const GeometryList>(
    decode(encode(std::make_shared<GeometryList>(bodies))));
  REQUIRE(decoded);
  CHECK(decoded->getChildren().size() == 2);
}

TEST_CASE("IPC geometry codec rejects a truncated payload", "[io][IPC][IPC-Geometry]")
{
  // A worker killed mid-write leaves a partial payload. Decoding it must fail rather than produce
  // a plausible-looking mesh from whatever bytes arrived.
  const std::string bytes = encode(coloredTetrahedron());
  CHECK(decode(bytes.substr(0, bytes.size() / 2)) == nullptr);
  CHECK(decode("") == nullptr);
  CHECK(decode("not an ipc payload at all") == nullptr);
}

TEST_CASE("IPC geometry single-body decode skips the list wrapper", "[io][IPC][IPC-Geometry]")
{
  // The preview path decodes straight to a mutable PolySet so no mesh has to be copied.
  const std::string bytes = encode(coloredTetrahedron());
  const auto ps = import_ipc_polyset_buffer(bytes.data(), bytes.size(), "leaf/0.osig");
  REQUIRE(ps);
  CHECK(ps->getConvexity() == 7);
  CHECK(ps->colors.size() == 3);
  CHECK(ps->vertices.size() == 4);
}
