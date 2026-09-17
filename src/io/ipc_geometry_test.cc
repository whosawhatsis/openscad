// Binary geometry codec for the compute worker (row 59).
//
// What is being defended here is parity with the in-process path. Everything a PolySet or
// Polygon2d carries has to survive the process boundary, because anything that does not is a
// visible regression against running the geometry in-process -- not a "minimal first version".
// Color and convexity in particular: dropping them costs ~16 lines and loses per-face color and
// correct preview of concave objects, which blocks a merge.

#include "io/ipc_geometry.h"
#include <map>
#include "geometry/SurfaceFinish.h"

#include <catch2/catch_all.hpp>
#include <memory>
#include <sstream>
#include <string>

#include "geometry/Geometry.h"
#include "io/export.h"
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

// Export parity: the round trip through the channel must be invisible to the exporters.
//
// This is what a user actually notices. Under isolation an F6 result reaches the window as a payload
// and is decoded back into a PolySet, so a later export goes through a round trip the in-process path
// never performs. The codec tests above pin each field individually; this pins the whole of it at the
// surface a user sees, and would catch a field that survives decoding but is written differently.
//
// Ported from the old implementation's tests/test_compute_worker_parity.py, which decoded payloads in
// Python. Done here instead because decoding is free on this side and the Python decoder was written
// against a payload layout this branch does not use.
TEST_CASE("An export after the IPC round trip is byte-identical", "[io][IPC][IPC-Geometry]")
{
  const auto exportOff = [](const std::shared_ptr<const Geometry>& geom) {
    std::ostringstream out;
    export_off(geom, out);
    return out.str();
  };
  const auto exportAsciiStl = [](const std::shared_ptr<const Geometry>& geom) {
    std::ostringstream out;
    export_stl(geom, out, false);
    return out.str();
  };

  SECTION("a colored, concave PolySet")
  {
    const std::shared_ptr<const Geometry> original = coloredTetrahedron();
    const auto decoded = decode(encode(original));
    REQUIRE(decoded);
    CHECK(exportOff(decoded) == exportOff(original));
    CHECK(exportAsciiStl(decoded) == exportAsciiStl(original));
  }

  SECTION("a 2D outline with a hole")
  {
    // Through DXF, which is what a 2D result is actually exported as. (OFF is 3D-only: handing it a
    // Polygon2d crashes, here and on master alike, which is why do_export() gates on dimension.)
    const auto exportDxf = [](const std::shared_ptr<const Geometry>& geom) {
      std::ostringstream out;
      export_dxf(geom, out);
      return out.str();
    };
    const std::shared_ptr<const Geometry> original = squareWithHole();
    const auto decoded = decode(encode(original));
    REQUIRE(decoded);
    CHECK(exportDxf(decoded) == exportDxf(original));
  }
}

// Surface finish and body material. Geometry carries more than shape and color here: a per-surface
// SurfaceFinish (parallel to colors), and on the Geometry itself a material name, roughness, metallic,
// finish parameters, smoothing angle, and body boundary/color. None of it survived the transport, so
// with process isolation on, material shading and smooth shading silently fell back to their defaults.
// Every field is checked individually: one that is written but not read looks exactly like one never
// written.
TEST_CASE("IPC geometry codec preserves surface finish and body material", "[io][IPC][IPC-Geometry]")
{
  SECTION("per-surface finishes on a PolySet, parallel to its colors")
  {
    auto ps = coloredTetrahedron();
    ps->finishes = {SurfaceFinish{0.2f, 0.9f, 0.5f, 0.0f}, SurfaceFinish{0.8f, 0.0f, 0.04f, 1.5f},
                    SurfaceFinish{}};
    REQUIRE(ps->finishes.size() == ps->colors.size());
    const auto decoded = std::dynamic_pointer_cast<const PolySet>(decode(encode(ps)));
    REQUIRE(decoded);
    REQUIRE(decoded->finishes.size() == ps->finishes.size());
    for (size_t i = 0; i < ps->finishes.size(); ++i) CHECK(decoded->finishes[i] == ps->finishes[i]);
  }

  SECTION("a PolySet with no finishes stays with none, rather than gaining defaults")
  {
    const auto decoded = std::dynamic_pointer_cast<const PolySet>(decode(encode(coloredTetrahedron())));
    REQUIRE(decoded);
    CHECK(decoded->finishes.empty());
  }

  const auto setMaterial = [](Geometry& g) {
    g.setMaterialName("brushed steel");
    g.setRoughness(0.35f);
    g.setMetallic(0.75f);
    g.setFinishParams({{"ior", 1.45}, {"specular", 0.6}});
    g.setSmoothAngle(40.0);
    g.setBodyBoundary(true);
    g.setBodyColor(Color4f(0.1f, 0.2f, 0.3f, 0.4f));
  };
  const auto checkMaterial = [](const Geometry& g) {
    CHECK(g.materialName() == "brushed steel");
    CHECK(g.hasRoughness());
    CHECK(g.roughness() == 0.35f);
    CHECK(g.metallic() == 0.75f);
    CHECK(g.finishParams() == std::map<std::string, double>{{"ior", 1.45}, {"specular", 0.6}});
    CHECK(g.smoothAngle() == 40.0);
    CHECK(g.isBodyBoundary());
    CHECK(g.hasBodyColor());
    CHECK(g.bodyColor() == Color4f(0.1f, 0.2f, 0.3f, 0.4f));
  };

  SECTION("body material on a PolySet")
  {
    auto ps = coloredTetrahedron();
    setMaterial(*ps);
    const auto decoded = decode(encode(ps));
    REQUIRE(decoded);
    checkMaterial(*decoded);
  }

  SECTION("body material on a Polygon2d")
  {
    auto poly = squareWithHole();
    setMaterial(*poly);
    const auto decoded = decode(encode(poly));
    REQUIRE(decoded);
    checkMaterial(*decoded);
  }

  SECTION("body material on a preview leaf, which uses the single-PolySet writer")
  {
    auto ps = coloredTetrahedron();
    setMaterial(*ps);
    std::ostringstream out(std::ios::binary);
    export_ipc_geometry(*ps, out);
    const auto bytes = out.str();
    const auto decoded = import_ipc_polyset_buffer(bytes.data(), bytes.size(), "leaf");
    REQUIRE(decoded);
    checkMaterial(*decoded);
  }

  SECTION("an unset material decodes as unset, not as explicit defaults")
  {
    // roughness = 0 means a mirror, so "not set" must stay distinguishable from "set to 0".
    const auto decoded = decode(encode(coloredTetrahedron()));
    REQUIRE(decoded);
    CHECK_FALSE(decoded->hasRoughness());
    CHECK_FALSE(decoded->hasBodyColor());
    CHECK_FALSE(decoded->isBodyBoundary());
    CHECK(decoded->materialName().empty());
    CHECK(decoded->finishParams().empty());
  }
}
