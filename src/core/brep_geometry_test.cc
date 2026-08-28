#ifdef ENABLE_OPENCSCADE

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "geometry/PolySet.h"
#include "geometry/brep/BrepGeometry.h"

TEST_CASE("BrepGeometry retains analytic surfaces until tessellation", "[brep]")
{
  BrepGeometry geometry = BrepGeometry::cylinder(4.0, 10.0);

  const auto mesh = geometry.toPolySet(0.1, 0.2);

  REQUIRE(geometry.surfaceCount(BrepSurfaceType::Cylinder) == 1);
  REQUIRE(mesh->numFacets() > 0);
  REQUIRE(geometry.numFacets() == 0);
}

TEST_CASE("BrepGeometry performs an exact filleted difference", "[brep]")
{
  BrepGeometry box = BrepGeometry::cube(20.0, 20.0, 10.0);
  BrepGeometry cylinder = BrepGeometry::cylinder(4.0, 12.0);
  Transform3d transform = Transform3d::Identity();
  transform.translate(Vector3d(10.0, 10.0, -1.0));
  cylinder.transform(transform);

  BrepFilletDiagnostics diagnostics;
  BrepGeometry result = box.difference(cylinder, 10.0, diagnostics);

  REQUIRE(diagnostics.filletedEdgeCount == 2);
  REQUIRE(diagnostics.radiusUpperBound == Catch::Approx(5.0));
  REQUIRE(diagnostics.achievedRadius == Catch::Approx(5.0).margin(0.001));
  REQUIRE(result.surfaceCount(BrepSurfaceType::Cylinder) == 1);
  REQUIRE(result.surfaceCount(BrepSurfaceType::Torus) == 2);
  REQUIRE(result.numFacets() == 0);
}

#endif
