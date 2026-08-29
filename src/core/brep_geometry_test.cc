#ifdef ENABLE_OPENCSCADE

#include <optional>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "geometry/PolySet.h"
#include "geometry/brep/BrepGeometry.h"
#include "geometry/GeometryEvaluator.h"
#include "glview/RenderSettings.h"
#include "core/CsgOpNode.h"
#include "core/CurveDiscretizer.h"
#include "core/ModuleInstantiation.h"
#include "core/TransformNode.h"
#include "core/Tree.h"
#include "core/primitives.h"

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

TEST_CASE("GeometryEvaluator routes filleted difference through B-Rep", "[brep]")
{
  const auto previousBackend = RenderSettings::inst()->backend3D;
  RenderSettings::inst()->backend3D = RenderBackend3D::OpenCASCADEBackend;
  ModuleInstantiation differenceInstantiation("difference");
  ModuleInstantiation cubeInstantiation("cube");
  ModuleInstantiation transformInstantiation("translate");
  ModuleInstantiation cylinderInstantiation("cylinder");

  auto difference =
    std::make_shared<CsgOpNode>(&differenceInstantiation, OpenSCADOperator::DIFFERENCE, 10.0, true);
  auto cube = std::make_shared<CubeNode>(&cubeInstantiation);
  cube->x = 20.0;
  cube->y = 20.0;
  cube->z = 10.0;
  auto transform = std::make_shared<TransformNode>(&transformInstantiation, "translate");
  transform->matrix.translate(Vector3d(10.0, 10.0, -1.0));
  auto cylinder = std::make_shared<CylinderNode>(
    &cylinderInstantiation,
    CurveDiscretizer([](const char *) -> std::optional<double> { return std::nullopt; }));
  cylinder->r1 = cylinder->r2 = 4.0;
  cylinder->h = 12.0;
  transform->children.push_back(cylinder);
  difference->children.push_back(cube);
  difference->children.push_back(transform);

  Tree tree(difference);
  GeometryEvaluator evaluator(tree);
  const auto result = evaluator.evaluateGeometry(*difference, true);
  const auto brep = std::dynamic_pointer_cast<const BrepGeometry>(result);

  REQUIRE(brep);
  REQUIRE(brep->surfaceCount(BrepSurfaceType::Cylinder) == 1);
  REQUIRE(brep->surfaceCount(BrepSurfaceType::Torus) == 2);
  REQUIRE(brep->numFacets() == 0);

  const auto meshResult = evaluator.evaluateGeometry(*difference, false);
  const auto mesh = std::dynamic_pointer_cast<const PolySet>(meshResult);
  REQUIRE(mesh);
  REQUIRE(mesh->numFacets() > 0);

  difference->fa = 4.0;
  difference->fs = 0.25;
  const auto fineMeshResult = evaluator.evaluateGeometry(*difference, false);
  const auto fineMesh = std::dynamic_pointer_cast<const PolySet>(fineMeshResult);
  REQUIRE(fineMesh);
  REQUIRE(fineMesh->numFacets() > mesh->numFacets());

  RenderSettings::inst()->backend3D = previousBackend;
}

TEST_CASE("OpenCASCADE is an available 3D backend when compiled in", "[brep]")
{
  REQUIRE(renderBackend3DFromString("opencascade") == RenderBackend3D::OpenCASCADEBackend);
  REQUIRE(renderBackend3DToString(RenderBackend3D::OpenCASCADEBackend) == "OpenCASCADE");
}

TEST_CASE("OpenCASCADE backend retains a primitive as B-Rep", "[brep]")
{
  ModuleInstantiation cubeInstantiation("cube");
  auto cube = std::make_shared<CubeNode>(&cubeInstantiation);
  cube->x = cube->y = cube->z = 10.0;

  const auto previousBackend = RenderSettings::inst()->backend3D;
  RenderSettings::inst()->backend3D = RenderBackend3D::OpenCASCADEBackend;
  Tree tree(cube);
  GeometryEvaluator evaluator(tree);
  const auto result = evaluator.evaluateGeometry(*cube, true);
  RenderSettings::inst()->backend3D = previousBackend;

  REQUIRE(std::dynamic_pointer_cast<const BrepGeometry>(result));
}

TEST_CASE("Non-OpenCASCADE backends warn and ignore fillets", "[brep]")
{
  ModuleInstantiation differenceInstantiation("difference");
  ModuleInstantiation cubeInstantiation("cube");
  ModuleInstantiation cylinderInstantiation("cylinder");

  auto difference =
    std::make_shared<CsgOpNode>(&differenceInstantiation, OpenSCADOperator::DIFFERENCE, 2.0, true);
  auto cube = std::make_shared<CubeNode>(&cubeInstantiation);
  cube->x = cube->y = cube->z = 10.0;
  auto cylinder = std::make_shared<CylinderNode>(
    &cylinderInstantiation,
    CurveDiscretizer([](const char *) -> std::optional<double> { return std::nullopt; }));
  cylinder->r1 = cylinder->r2 = 2.0;
  cylinder->h = 10.0;
  difference->children = {cube, cylinder};

  const auto previousBackend = RenderSettings::inst()->backend3D;
  RenderSettings::inst()->backend3D = RenderBackend3D::ManifoldBackend;
  Tree tree(difference);
  GeometryEvaluator evaluator(tree);
  const auto result = evaluator.evaluateGeometry(*difference, true);
  RenderSettings::inst()->backend3D = previousBackend;

  REQUIRE(result);
  REQUIRE_FALSE(result->isEmpty());
}

#endif
