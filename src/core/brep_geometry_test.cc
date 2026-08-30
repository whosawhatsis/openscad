#ifdef ENABLE_OPENCSCADE

#include <algorithm>
#include <cmath>
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

TEST_CASE("BrepGeometry display mesh carries analytic normals and boundary edges", "[brep]")
{
  BrepGeometry geometry = BrepGeometry::cylinder(4.0, 10.0);

  const auto display = geometry.toDisplayMesh(0.2, 0.35);

  REQUIRE(display.vertices.size() == display.normals.size());
  REQUIRE_FALSE(display.triangles.empty());
  REQUIRE_FALSE(display.edges.empty());
  REQUIRE(std::any_of(display.normals.begin(), display.normals.end(),
                      [](const auto& normal) { return std::abs(normal[2]) < 0.1; }));
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

TEST_CASE("GeometryEvaluator composes recursive and n-ary B-Rep booleans", "[brep]")
{
  const auto previousBackend = RenderSettings::inst()->backend3D;
  RenderSettings::inst()->backend3D = RenderBackend3D::OpenCASCADEBackend;
  ModuleInstantiation unionInstantiation("union");
  ModuleInstantiation intersectionInstantiation("intersection");
  ModuleInstantiation differenceInstantiation("difference");
  ModuleInstantiation cubeInstantiation("cube");
  ModuleInstantiation transformInstantiation("translate");

  const auto cube = [&](double size, double x) {
    auto transform = std::make_shared<TransformNode>(&transformInstantiation, "translate");
    transform->matrix.translate(Vector3d(x, 0.0, 0.0));
    auto node = std::make_shared<CubeNode>(&cubeInstantiation);
    node->x = node->y = node->z = size;
    transform->children.push_back(node);
    return transform;
  };

  auto unionNode = std::make_shared<CsgOpNode>(&unionInstantiation, OpenSCADOperator::UNION);
  unionNode->children = {cube(10.0, 0.0), cube(10.0, 5.0)};
  auto intersection =
    std::make_shared<CsgOpNode>(&intersectionInstantiation, OpenSCADOperator::INTERSECTION);
  intersection->children = {unionNode, cube(10.0, 7.0), cube(10.0, 9.0)};

  Tree intersectionTree(intersection);
  GeometryEvaluator intersectionEvaluator(intersectionTree);
  const auto intersectionResult = intersectionEvaluator.evaluateGeometry(*intersection, true);
  REQUIRE(std::dynamic_pointer_cast<const BrepGeometry>(intersectionResult));
  REQUIRE_FALSE(intersectionResult->isEmpty());
  REQUIRE(intersectionResult->getBoundingBox().min().x() == Catch::Approx(9.0).margin(0.001));

  auto difference = std::make_shared<CsgOpNode>(&differenceInstantiation, OpenSCADOperator::DIFFERENCE);
  difference->children = {cube(20.0, 0.0), cube(4.0, 3.0), cube(4.0, 13.0)};
  Tree differenceTree(difference);
  GeometryEvaluator differenceEvaluator(differenceTree);
  const auto differenceResult = differenceEvaluator.evaluateGeometry(*difference, true);
  REQUIRE(std::dynamic_pointer_cast<const BrepGeometry>(differenceResult));
  REQUIRE_FALSE(differenceResult->isEmpty());

  BrepFilletDiagnostics diagnostics;
  auto shifted = BrepGeometry::cube(2.0, 2.0, 2.0);
  Transform3d translation = Transform3d::Identity();
  translation.translate(Vector3d(10.0, 0.0, 0.0));
  shifted.transform(translation);
  const auto empty = BrepGeometry::boolean({BrepGeometry::cube(2.0, 2.0, 2.0), shifted},
                                           BrepOperation::Intersection, 0.0, diagnostics);
  REQUIRE(empty.isEmpty());
  REQUIRE(BrepGeometry::boolean({}, BrepOperation::Union, 0.0, diagnostics).isEmpty());
  REQUIRE_FALSE(
    BrepGeometry::boolean({BrepGeometry(nullptr), shifted}, BrepOperation::Union, 0.0, diagnostics)
      .isEmpty());

  RenderSettings::inst()->backend3D = previousBackend;
}

TEST_CASE("OpenCASCADE is an available 3D backend when compiled in", "[brep]")
{
  REQUIRE(renderBackend3DFromString("opencascade") == RenderBackend3D::OpenCASCADEBackend);
  REQUIRE(renderBackend3DToString(RenderBackend3D::OpenCASCADEBackend) == "OpenCASCADE");
}

TEST_CASE("B-Rep wrappers preserve grouping and list semantics", "[brep]")
{
  ModuleInstantiation inst("group");
  ModuleInstantiation backgroundInst("cube");
  backgroundInst.tag_background = true;
  const auto box = [&](double size, double x) {
    auto cube = std::make_shared<CubeNode>(&inst);
    cube->x = cube->y = cube->z = size;
    auto translated = std::make_shared<TransformNode>(&inst, "translate");
    translated->matrix.translate(Vector3d(x, 0.0, 0.0));
    translated->children = {cube};
    return translated;
  };
  auto group = std::make_shared<GroupNode>(&inst, "user_module");
  group->children = {box(10.0, 0.0), box(10.0, 15.0)};
  auto transform = std::make_shared<TransformNode>(&inst, "translate");
  transform->matrix.translate(Vector3d(100.0, 0.0, 0.0));
  transform->children = {group, box(3.0, 30.0)};
  auto background = std::make_shared<CubeNode>(&backgroundInst);
  background->x = background->y = background->z = 500.0;
  auto root = std::make_shared<RootNode>();
  root->children = {transform, background};

  auto list = std::make_shared<ListNode>(&inst);
  list->children = {box(10.0, 0.0), box(10.0, 5.0)};
  auto difference = std::make_shared<CsgOpNode>(&inst, OpenSCADOperator::DIFFERENCE);
  difference->children = {list};

  const auto previousBackend = RenderSettings::inst()->backend3D;
  RenderSettings::inst()->backend3D = RenderBackend3D::OpenCASCADEBackend;
  Tree rootTree(root);
  GeometryEvaluator rootEvaluator(rootTree);
  const auto result = rootEvaluator.evaluateGeometry(*root, true);
  Tree differenceTree(difference);
  GeometryEvaluator differenceEvaluator(differenceTree);
  const auto differenceResult = differenceEvaluator.evaluateGeometry(*difference, true);
  RenderSettings::inst()->backend3D = previousBackend;

  REQUIRE(std::dynamic_pointer_cast<const BrepGeometry>(result));
  REQUIRE(result->getBoundingBox().min().x() == Catch::Approx(100.0).margin(0.001));
  REQUIRE(result->getBoundingBox().max().x() == Catch::Approx(133.0).margin(0.001));
  REQUIRE(std::dynamic_pointer_cast<const BrepGeometry>(differenceResult));
  REQUIRE(differenceResult->getBoundingBox().max().x() == Catch::Approx(5.0).margin(0.001));
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

TEST_CASE("OpenCASCADE retains analytic spheres and tapered cylinders", "[brep]")
{
  ModuleInstantiation inst("primitive");
  const auto discretizer = [] {
    return CurveDiscretizer([](const char *) -> std::optional<double> { return std::nullopt; });
  };
  std::shared_ptr<AbstractNode> primitive;
  BrepSurfaceType surface;
  SECTION("sphere")
  {
    auto sphere = std::make_shared<SphereNode>(&inst, discretizer());
    sphere->r = 4.0;
    primitive = sphere;
    surface = BrepSurfaceType::Sphere;
  }
  SECTION("tapered cylinder")
  {
    auto cone = std::make_shared<CylinderNode>(&inst, discretizer());
    cone->r1 = 4.0;
    cone->r2 = 2.0;
    SECTION("frustum")
    {
    }
    SECTION("upper apex")
    {
      cone->r2 = 0.0;
    }
    SECTION("lower apex")
    {
      cone->r1 = 0.0;
    }
    cone->h = 8.0;
    cone->center = true;
    primitive = cone;
    surface = BrepSurfaceType::Cone;
  }
  const auto previousBackend = RenderSettings::inst()->backend3D;
  RenderSettings::inst()->backend3D = RenderBackend3D::OpenCASCADEBackend;
  Tree tree(primitive);
  GeometryEvaluator evaluator(tree);
  const auto result = evaluator.evaluateGeometry(*primitive, true);
  RenderSettings::inst()->backend3D = previousBackend;
  const auto brep = std::dynamic_pointer_cast<const BrepGeometry>(result);
  REQUIRE(brep);
  REQUIRE(brep->surfaceCount(surface) == 1);
  REQUIRE(brep->numFacets() == 0);
  REQUIRE(brep->getBoundingBox().min().z() == Catch::Approx(-4.0).margin(0.001));
  REQUIRE(brep->getBoundingBox().max().z() == Catch::Approx(4.0).margin(0.001));
  REQUIRE_FALSE(brep->toDisplayMesh(0.1, 0.2).triangles.empty());
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
