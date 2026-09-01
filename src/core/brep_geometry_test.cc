#ifdef ENABLE_OPENCSCADE

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <optional>
#include <stdexcept>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "geometry/PolySet.h"
#include "geometry/brep/BrepGeometry.h"
#include "geometry/GeometryEvaluator.h"
#include "glview/RenderSettings.h"
#include "core/CsgOpNode.h"
#include "core/CurveDiscretizer.h"
#include "core/ModuleInstantiation.h"
#include "core/LinearExtrudeNode.h"
#include "core/RotateExtrudeNode.h"
#include "core/TransformNode.h"
#include "core/Tree.h"
#include "core/ImportNode.h"
#include "core/SurfaceNode.h"
#include "core/OffsetNode.h"
#include "core/ProjectionNode.h"
#include "core/TextNode.h"
#include "core/CgalAdvNode.h"
#include "platform/PlatformUtils.h"
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

TEST_CASE("Intentional polygonal primitives remain planar B-Rep solids", "[brep]")
{
  ModuleInstantiation inst("primitive");
  std::shared_ptr<AbstractNode> primitive;
  SECTION("explicit fn cylinder")
  {
    auto cylinder = std::make_shared<CylinderNode>(&inst, CurveDiscretizer(6.0));
    cylinder->r1 = cylinder->r2 = 4.0;
    cylinder->h = 8.0;
    primitive = cylinder;
  }
  SECTION("explicit fn cone")
  {
    auto cone = std::make_shared<CylinderNode>(&inst, CurveDiscretizer(6.0));
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
    primitive = cone;
  }
  SECTION("explicit fn sphere")
  {
    auto sphere = std::make_shared<SphereNode>(&inst, CurveDiscretizer(6.0));
    sphere->r = 4.0;
    primitive = sphere;
  }
  SECTION("polyhedron")
  {
    auto tetrahedron = std::make_shared<PolyhedronNode>(&inst);
    tetrahedron->points = {{0, 0, 0}, {10, 0, 0}, {0, 10, 0}, {0, 0, 10}};
    tetrahedron->faces = {{0, 1, 2}, {0, 3, 1}, {0, 2, 3}, {1, 3, 2}};
    primitive = tetrahedron;
  }
  const auto previousBackend = RenderSettings::inst()->backend3D;
  RenderSettings::inst()->backend3D = RenderBackend3D::OpenCASCADEBackend;
  Tree tree(primitive);
  GeometryEvaluator evaluator(tree);
  const auto result = evaluator.evaluateGeometry(*primitive, true);
  RenderSettings::inst()->backend3D = previousBackend;
  const auto brep = std::dynamic_pointer_cast<const BrepGeometry>(result);
  REQUIRE(brep);
  REQUIRE(brep->surfaceCount(BrepSurfaceType::Plane) >= 4);
  REQUIRE(brep->surfaceCount(BrepSurfaceType::Cylinder) == 0);
  REQUIRE(brep->surfaceCount(BrepSurfaceType::Cone) == 0);
  REQUIRE(brep->surfaceCount(BrepSurfaceType::Sphere) == 0);
  if (const auto *cylinder = dynamic_cast<const CylinderNode *>(primitive.get())) {
    REQUIRE(brep->surfaceCount(BrepSurfaceType::Plane) ==
            6 + (cylinder->r1 > 0.0) + (cylinder->r2 > 0.0));
  }
  BrepFilletDiagnostics diagnostics;
  const auto clipped = BrepGeometry::boolean({*brep, BrepGeometry::cube(2.0, 2.0, 2.0)},
                                             BrepOperation::Intersection, 0.0, diagnostics);
  REQUIRE_FALSE(clipped.isEmpty());
  REQUIRE_FALSE(clipped.toDisplayMesh(0.1, 0.2).triangles.empty());
}

TEST_CASE("Polygon-to-B-Rep conversion rejects invalid inputs", "[brep]")
{
  PolySet mesh(3);
  mesh.vertices = {{0, 0, 0}, {10, 0, 0}, {0, 10, 0}, {0, 0, 10}};
  mesh.indices = {{0, 1, 2}, {0, 3, 1}, {0, 2, 3}, {1, 3, 2}};
  SECTION("open shell")
  {
    mesh.indices.pop_back();
    REQUIRE_THROWS_AS(BrepGeometry::fromPolySet(mesh), std::runtime_error);
  }
  SECTION("invalid index")
  {
    mesh.indices[0][0] = 20;
    REQUIRE_THROWS_AS(BrepGeometry::fromPolySet(mesh), std::invalid_argument);
  }
  SECTION("disconnected shells")
  {
    for (int index = 0; index < 4; ++index) {
      mesh.vertices.push_back(mesh.vertices[index] + Vector3d(20, 0, 0));
      auto face = mesh.indices[index];
      for (auto& vertex : face) vertex += 4;
      mesh.indices.push_back(face);
    }
    REQUIRE_FALSE(BrepGeometry::fromPolySet(mesh).isEmpty());
  }
}

TEST_CASE("B-Rep mesh shells preserve disconnected regions and nested cavities", "[brep]")
{
  ModuleInstantiation inst("cube");
  PolySet mesh(3);
  const auto addBox = [&](double start, double size) {
    CubeNode cube(&inst);
    cube.x = cube.y = cube.z = size;
    const auto geometry = cube.createGeometry();
    const auto& box = dynamic_cast<const PolySet&>(*geometry);
    const int first = mesh.vertices.size();
    for (const auto& p : box.vertices) mesh.vertices.push_back(p + Vector3d(start, start, start));
    for (auto indices : box.indices) {
      for (auto& index : indices) index += first;
      mesh.indices.push_back(indices);
    }
  };
  addBox(0, 10);
  bool island = false, separate = false;
  SECTION("cavity")
  {
    addBox(2, 6);
  }
  SECTION("island in cavity")
  {
    addBox(2, 6);
    addBox(4, 2);
    island = true;
  }
  SECTION("disconnected")
  {
    addBox(20, 2);
    separate = true;
  }
  SECTION("intersecting shells are invalid")
  {
    addBox(5, 10);
    REQUIRE_THROWS_AS(BrepGeometry::fromPolySet(mesh), std::runtime_error);
    return;
  }
  const auto shape = BrepGeometry::fromPolySet(mesh);
  const auto occupied = [&](double p) {
    auto probe = BrepGeometry::cube(0.1, 0.1, 0.1);
    Transform3d transform = Transform3d::Identity();
    transform.translate(Vector3d(p, p, p));
    probe.transform(transform);
    BrepFilletDiagnostics diagnostics;
    return !BrepGeometry::boolean({shape, probe}, BrepOperation::Intersection, 0, diagnostics).isEmpty();
  };
  CHECK(occupied(1));
  CHECK(occupied(3) == separate);
  CHECK(occupied(5) == (separate || island));
  CHECK(occupied(20.5) == separate);
  CHECK_FALSE(shape.toDisplayMesh(0.1, 0.2).triangles.empty());
}

TEST_CASE("B-Rep evaluator retains mesh imports and height fields", "[brep]")
{
  ModuleInstantiation inst("input");
  const auto data =
    std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "tests/data";
  std::shared_ptr<AbstractNode> node;
  SECTION("OFF import")
  {
    auto imported = std::make_shared<ImportNode>(&inst, ImportType::OFF, CurveDiscretizer(6.0));
    imported->filename = Filename((data / "off/brep-tetrahedron.off").string());
    imported->convexity = 1;
    imported->center = false;
    node = imported;
  }
  SECTION("height field")
  {
    auto surface = std::make_shared<SurfaceNode>(&inst);
    surface->filename = Filename((data / "scad/3D/features/surface-simple.dat").string());
    node = surface;
  }
  const auto previousBackend = RenderSettings::inst()->backend3D;
  RenderSettings::inst()->backend3D = RenderBackend3D::OpenCASCADEBackend;
  Tree tree(node);
  GeometryEvaluator evaluator(tree);
  const auto result = evaluator.evaluateGeometry(*node, true);
  RenderSettings::inst()->backend3D = previousBackend;
  const auto brep = std::dynamic_pointer_cast<const BrepGeometry>(result);
  REQUIRE(brep);
  REQUIRE_FALSE(brep->isEmpty());
  CHECK(brep->surfaceCount(BrepSurfaceType::Plane) > 0);
  CHECK_FALSE(brep->toDisplayMesh(0.1, 0.2).triangles.empty());
}

TEST_CASE("B-Rep multipart import does not combine objects with a mesh kernel", "[brep]")
{
  PlatformUtils::registerApplicationPath(
    std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().string());
  ModuleInstantiation inst("import");
  ImportNode node(&inst, ImportType::AMF, CurveDiscretizer(6.0));
  node.filename = Filename((std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() /
                            "tests/data/amf/brep-multipart.amf")
                             .string());
  node.center = false;
  node.convexity = 1;
  const auto previous = RenderSettings::inst()->backend3D;
  RenderSettings::inst()->backend3D = RenderBackend3D::OpenCASCADEBackend;
  const auto result = node.createGeometry();
  RenderSettings::inst()->backend3D = previous;
  const auto *brep = dynamic_cast<const BrepGeometry *>(result.get());
  REQUIRE(brep);
  REQUIRE_FALSE(brep->isEmpty());
  CHECK(brep->getBoundingBox().max().x() == Catch::Approx(4).margin(1e-5));
}

TEST_CASE("B-Rep offset preserves circular profiles and holes", "[brep]")
{
  ModuleInstantiation inst("offset");
  auto offset = std::make_shared<OffsetNode>(&inst, CurveDiscretizer(6.0));
  offset->delta = 1;
  auto circle = std::make_shared<CircleNode>(
    &inst, CurveDiscretizer([](const char *) -> std::optional<double> { return std::nullopt; }));
  circle->r = 4;
  offset->children = {circle};
  bool hole = false;
  SECTION("expand")
  {
  }
  SECTION("shrink")
  {
    offset->delta = -1;
  }
  SECTION("vanish")
  {
    offset->delta = -5;
  }
  SECTION("annulus")
  {
    auto inner = std::make_shared<CircleNode>(&inst, circle->discretizer);
    inner->r = 2;
    auto difference = std::make_shared<CsgOpNode>(&inst, OpenSCADOperator::DIFFERENCE);
    difference->children = {circle, inner};
    offset->children = {difference};
    hole = true;
  }
  auto extrusion = std::make_shared<LinearExtrudeNode>(&inst, CurveDiscretizer(6.0));
  extrusion->height = Vector3d(0, 0, 5);
  extrusion->children = {offset};
  const auto previous = RenderSettings::inst()->backend3D;
  RenderSettings::inst()->backend3D = RenderBackend3D::OpenCASCADEBackend;
  Tree tree(extrusion);
  GeometryEvaluator evaluator(tree);
  const auto result = evaluator.evaluateGeometry(*extrusion, true);
  RenderSettings::inst()->backend3D = previous;
  const auto brep = std::dynamic_pointer_cast<const BrepGeometry>(result);
  REQUIRE(brep);
  if (offset->delta == -5) {
    CHECK(brep->isEmpty());
    return;
  }
  REQUIRE_FALSE(brep->isEmpty());
  CHECK(brep->surfaceCount(BrepSurfaceType::Cylinder) == (hole ? 2 : 1));
  CHECK(brep->getBoundingBox().max().x() == Catch::Approx(4 + offset->delta).margin(1e-5));
  BrepFilletDiagnostics diagnostics;
  CHECK(BrepGeometry::boolean({*brep, BrepGeometry::cube(0.1, 0.1, 0.1)}, BrepOperation::Intersection, 0,
                              diagnostics)
          .isEmpty() == hole);
}

TEST_CASE("B-Rep chamfer offsets use square joins without faceting existing circles", "[brep]")
{
  ModuleInstantiation inst("offset");
  const CurveDiscretizer smooth([](const char *) -> std::optional<double> { return std::nullopt; });
  auto offset = std::make_shared<OffsetNode>(&inst, smooth);
  offset->delta = 1;
  offset->chamfer = true;
  offset->join_type = Clipper2Lib::JoinType::Square;
  auto square = std::make_shared<SquareNode>(&inst);
  square->x = square->y = 4;
  offset->children = {square};
  bool circle = false, hole = false, polygon = false;
  SECTION("convex square corners")
  {
  }
  SECTION("existing circle")
  {
    auto profile = std::make_shared<CircleNode>(&inst, smooth);
    profile->r = 4;
    offset->children = {profile};
    circle = true;
  }
  SECTION("explicit polygonal circle")
  {
    auto profile = std::make_shared<CircleNode>(&inst, CurveDiscretizer(6.0));
    profile->r = 4;
    offset->children = {profile};
    polygon = true;
  }
  SECTION("expanding square hole")
  {
    square->x = square->y = 10;
    auto inner = std::make_shared<SquareNode>(&inst);
    inner->x = inner->y = 4;
    auto translate = std::make_shared<TransformNode>(&inst, "translate");
    translate->matrix.translate(Vector3d(3, 3, 0));
    translate->children = {inner};
    auto difference = std::make_shared<CsgOpNode>(&inst, OpenSCADOperator::DIFFERENCE);
    difference->children = {square, translate};
    offset->children = {difference};
    offset->delta = -1;
    hole = true;
  }
  auto extrusion = std::make_shared<LinearExtrudeNode>(&inst, smooth);
  extrusion->height = Vector3d(0, 0, 3);
  extrusion->children = {offset};
  const auto previous = RenderSettings::inst()->backend3D;
  RenderSettings::inst()->backend3D = RenderBackend3D::OpenCASCADEBackend;
  Tree tree(extrusion);
  GeometryEvaluator evaluator(tree);
  const auto result = evaluator.evaluateGeometry(*extrusion, true);
  RenderSettings::inst()->backend3D = previous;
  const auto brep = std::dynamic_pointer_cast<const BrepGeometry>(result);
  REQUIRE(brep);
  REQUIRE_FALSE(brep->isEmpty());
  CHECK(brep->surfaceCount(BrepSurfaceType::Cylinder) == (circle ? 1 : 0));
  const auto occupied = [&](double x, double y) {
    auto probe = BrepGeometry::cube(0.005, 0.005, 0.005);
    Transform3d transform = Transform3d::Identity();
    transform.translate(Vector3d(x, y, 1));
    probe.transform(transform);
    BrepFilletDiagnostics diagnostics;
    return !BrepGeometry::boolean({*brep, probe}, BrepOperation::Intersection, 0, diagnostics).isEmpty();
  };
  if (circle || polygon) CHECK(brep->getBoundingBox().max().x() == Catch::Approx(5).margin(1e-5));
  else if (hole) {
    CHECK_FALSE(occupied(2.05, 2.57));
    CHECK(occupied(2.1, 2.1));
  } else {
    CHECK(occupied(4.95, -0.43));
    CHECK_FALSE(occupied(4.9, -0.9));
  }
  CHECK_FALSE(brep->toDisplayMesh(0.1, 0.2).triangles.empty());
}

TEST_CASE("B-Rep cut projection retains curved sections", "[brep]")
{
  ModuleInstantiation inst("projection");
  auto sphere = std::make_shared<SphereNode>(
    &inst, CurveDiscretizer([](const char *) -> std::optional<double> { return std::nullopt; }));
  sphere->r = 4;
  auto projection = std::make_shared<ProjectionNode>(&inst);
  projection->cut_mode = true;
  projection->children = {sphere};
  auto extrusion = std::make_shared<LinearExtrudeNode>(&inst, CurveDiscretizer(6.0));
  extrusion->height = Vector3d(0, 0, 5);
  extrusion->children = {projection};
  const auto previous = RenderSettings::inst()->backend3D;
  RenderSettings::inst()->backend3D = RenderBackend3D::OpenCASCADEBackend;
  Tree tree(extrusion);
  GeometryEvaluator evaluator(tree);
  const auto result = evaluator.evaluateGeometry(*extrusion, true);
  RenderSettings::inst()->backend3D = previous;
  const auto brep = std::dynamic_pointer_cast<const BrepGeometry>(result);
  REQUIRE(brep);
  REQUIRE_FALSE(brep->isEmpty());
  CHECK(brep->surfaceCount(BrepSurfaceType::Cylinder) == 1);
  CHECK(brep->getBoundingBox().max().x() == Catch::Approx(4).margin(1e-5));
}

TEST_CASE("B-Rep shadow projection preserves silhouettes and visible holes", "[brep]")
{
  ModuleInstantiation inst("projection");
  const CurveDiscretizer smooth([](const char *) -> std::optional<double> { return std::nullopt; });
  auto sphere = std::make_shared<SphereNode>(&inst, smooth);
  sphere->r = 4;
  std::shared_ptr<AbstractNode> object = sphere;
  bool hole = false;
  SECTION("sphere above the projection plane")
  {
  }
  SECTION("annulus")
  {
    auto outer = std::make_shared<CylinderNode>(&inst, smooth);
    outer->r1 = outer->r2 = 4;
    outer->h = 2;
    auto inner = std::make_shared<CylinderNode>(&inst, smooth);
    inner->r1 = inner->r2 = 2;
    inner->h = 2;
    auto difference = std::make_shared<CsgOpNode>(&inst, OpenSCADOperator::DIFFERENCE);
    difference->children = {outer, inner};
    object = difference;
    hole = true;
  }
  auto translate = std::make_shared<TransformNode>(&inst, "translate");
  translate->matrix.translate(Vector3d(0, 0, 10));
  translate->children = {object};
  auto projection = std::make_shared<ProjectionNode>(&inst);
  projection->children = {translate};
  auto extrusion = std::make_shared<LinearExtrudeNode>(&inst, CurveDiscretizer(6.0));
  extrusion->height = Vector3d(0, 0, 5);
  extrusion->children = {projection};
  const auto previous = RenderSettings::inst()->backend3D;
  RenderSettings::inst()->backend3D = RenderBackend3D::OpenCASCADEBackend;
  Tree tree(extrusion);
  GeometryEvaluator evaluator(tree);
  const auto result = evaluator.evaluateGeometry(*extrusion, true);
  RenderSettings::inst()->backend3D = previous;
  const auto brep = std::dynamic_pointer_cast<const BrepGeometry>(result);
  REQUIRE(brep);
  REQUIRE_FALSE(brep->isEmpty());
  CHECK(brep->surfaceCount(BrepSurfaceType::Cylinder) == (hole ? 2 : 1));
  CHECK(brep->getBoundingBox().max().x() == Catch::Approx(4).margin(1e-5));
  BrepFilletDiagnostics diagnostics;
  CHECK(BrepGeometry::boolean({*brep, BrepGeometry::cube(0.1, 0.1, 0.1)}, BrepOperation::Intersection, 0,
                              diagnostics)
          .isEmpty() == hole);
}

TEST_CASE("B-Rep SVG extrusion retains original Bezier curves", "[brep]")
{
  ModuleInstantiation inst("import");
  auto imported = std::make_shared<ImportNode>(
    &inst, ImportType::SVG,
    CurveDiscretizer([](const char *) -> std::optional<double> { return std::nullopt; }));
  imported->filename =
    Filename((std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() /
              "tests/data/svg/brep-bezier.svg")
               .string());
  imported->id = "profile";
  imported->dpi = 72;
  imported->center = false;
  bool unsupported = false;
  bool faceted = false;
  bool checkFill = false, hole = true;
  size_t curvedFaces = 2;
  size_t cylindricalFaces = 0;
  double expectedMinX = 3, expectedMaxX = 11;
  bool curvedStroke = false;
  std::optional<std::pair<Vector3d, bool>> joinProbe;
  SECTION("absolute controls")
  {
  }
  SECTION("relative controls")
  {
    imported->id = "relative";
  }
  SECTION("centered")
  {
    imported->center = true;
  }
  SECTION("elliptical arc")
  {
    imported->id = "arc";
  }
  SECTION("circle")
  {
    imported->id = "circle";
    curvedFaces = 4;
  }
  SECTION("ellipse")
  {
    imported->id = "ellipse";
    curvedFaces = 4;
  }
  SECTION("rectangle")
  {
    imported->id = "rect";
    curvedFaces = 0;
  }
  SECTION("rounded rectangle")
  {
    imported->id = "rounded";
    curvedFaces = 4;
  }
  SECTION("polygon")
  {
    imported->id = "polygon";
    curvedFaces = 0;
  }
  SECTION("zero-radius arc becomes a line")
  {
    imported->id = "zeroarc";
    curvedFaces = 0;
  }
  SECTION("self-intersecting even-odd contour")
  {
    imported->id = "self-intersecting";
    curvedFaces = 0;
  }
  SECTION("self-intersecting nonzero contour")
  {
    imported->id = "self-intersecting-nonzero";
    curvedFaces = 0;
  }
  SECTION("opposite-winding circular hole")
  {
    imported->id = "ring";
    curvedFaces = 8;
  }
  SECTION("inline even-odd fill overrides presentation attribute")
  {
    imported->id = "evenodd";
    curvedFaces = 8;
    checkFill = true;
  }
  SECTION("inherited even-odd fill")
  {
    imported->id = "inherited";
    curvedFaces = 8;
    checkFill = true;
  }
  SECTION("child nonzero overrides inherited even-odd")
  {
    imported->id = "nonzero";
    curvedFaces = 4;
    checkFill = true;
    hole = false;
  }
  SECTION("even-odd is per shape not across separate shapes")
  {
    imported->id = "group-overlap";
    curvedFaces = 4;
    checkFill = true;
    hole = false;
  }
  SECTION("explicit circle facets")
  {
    imported->id = "circle";
    imported->discretizer = CurveDiscretizer(6.0);
    faceted = true;
    curvedFaces = 0;
  }
  SECTION("strokes do not silently become fills")
  {
    imported->id = "stroke";
    unsupported = true;
  }
  SECTION("open round-capped stroke")
  {
    imported->id = "open-stroke";
    curvedFaces = 0;
    cylindricalFaces = 2;
    expectedMinX = 1;
    expectedMaxX = 13;
  }
  SECTION("open curved stroke")
  {
    imported->id = "curved-stroke";
    curvedStroke = true;
    cylindricalFaces = 2;
    expectedMinX = 1;
    expectedMaxX = 13;
  }
  SECTION("round join between straight stroke segments")
  {
    imported->id = "joined-stroke";
    curvedFaces = 0;
    cylindricalFaces = 1;
    expectedMinX = 3 - std::sqrt(2.0);
    expectedMaxX = 11 + std::sqrt(2.0);
    joinProbe = {{7, 21.8, 1}, true};
  }
  SECTION("miter join between straight stroke segments")
  {
    imported->id = "miter-stroke";
    curvedFaces = 0;
    expectedMinX = 3 - std::sqrt(2.0);
    expectedMaxX = 11 + std::sqrt(2.0);
    joinProbe = {{7, 22.5, 1}, true};
  }
  SECTION("bevel join between straight stroke segments")
  {
    imported->id = "bevel-stroke";
    curvedFaces = 0;
    expectedMinX = 3 - std::sqrt(2.0);
    expectedMaxX = 11 + std::sqrt(2.0);
    joinProbe = {{7, 21.8, 1}, false};
  }
  SECTION("closed stroke joins its final and initial segments")
  {
    imported->id = "closed-stroke";
    curvedFaces = 0;
    cylindricalFaces = 3;
    expectedMinX = 1;
    expectedMaxX = 13;
  }
  SECTION("square stroke caps")
  {
    imported->id = "square-stroke";
    curvedFaces = 0;
    expectedMinX = 1;
    expectedMaxX = 13;
  }
  SECTION("inherited stroke properties")
  {
    imported->id = "inherited-stroke";
    curvedFaces = 0;
    expectedMinX = 3 - 2 * std::sqrt(2.0);
    expectedMaxX = 11 + 2 * std::sqrt(2.0);
  }
  SECTION("inherited stroke miter limit")
  {
    imported->id = "limited-miter-stroke";
    curvedFaces = 0;
    expectedMinX = 3 - std::sqrt(2.0);
    expectedMaxX = 11 + std::sqrt(2.0);
    joinProbe = {{7, 22.5, 1}, false};
  }
  auto extrusion = std::make_shared<LinearExtrudeNode>(&inst, CurveDiscretizer(6.0));
  extrusion->height = Vector3d(0, 0, 5);
  extrusion->children = {imported};
  const auto previous = RenderSettings::inst()->backend3D;
  RenderSettings::inst()->backend3D = RenderBackend3D::OpenCASCADEBackend;
  Tree tree(extrusion);
  GeometryEvaluator evaluator(tree);
  const auto result = evaluator.evaluateGeometry(*extrusion, true);
  RenderSettings::inst()->backend3D = previous;
  const auto brep = std::dynamic_pointer_cast<const BrepGeometry>(result);
  REQUIRE(brep);
  if (unsupported) {
    CHECK(brep->isEmpty());
    return;
  }
  REQUIRE_FALSE(brep->isEmpty());
  if (curvedStroke) CHECK(brep->surfaceCount(BrepSurfaceType::Other) >= 2);
  else CHECK(brep->surfaceCount(BrepSurfaceType::Other) == curvedFaces);
  if (cylindricalFaces) CHECK(brep->surfaceCount(BrepSurfaceType::Cylinder) >= cylindricalFaces);
  else CHECK(brep->surfaceCount(BrepSurfaceType::Cylinder) == 0);
  if (faceted) {
    CHECK(brep->surfaceCount(BrepSurfaceType::Plane) == 8);
    return;
  }
  if (imported->id.get() == "ring" || checkFill) {
    auto probe = BrepGeometry::cube(0.1, 0.1, 0.1);
    Transform3d placement = Transform3d::Identity();
    placement.translate(Vector3d(7, 16, 1));
    probe.transform(placement);
    BrepFilletDiagnostics diagnostics;
    CHECK(BrepGeometry::boolean({*brep, probe}, BrepOperation::Intersection, 0, diagnostics).isEmpty() ==
          hole);
  }
  if (imported->id.get() == "self-intersecting" || imported->id.get() == "self-intersecting-nonzero") {
    const auto occupied = [&](const Vector3d& point) {
      auto probe = BrepGeometry::cube(0.05, 0.05, 0.05);
      Transform3d placement = Transform3d::Identity();
      placement.translate(point);
      probe.transform(placement);
      BrepFilletDiagnostics diagnostics;
      return !BrepGeometry::boolean({*brep, probe}, BrepOperation::Intersection, 0, diagnostics)
                .isEmpty();
    };
    CHECK(occupied({5, 15, 1}));
    CHECK(occupied({5, 9, 1}));
    CHECK_FALSE(occupied({4, 12, 1}));
  }
  if (imported->id.get() == "circle") {
    const auto occupied = [&](double offset) {
      auto probe = BrepGeometry::cube(0.01, 0.01, 0.01);
      Transform3d placement = Transform3d::Identity();
      placement.translate(Vector3d(7 + offset, 16 + offset, 1));
      probe.transform(placement);
      BrepFilletDiagnostics diagnostics;
      return !BrepGeometry::boolean({*brep, probe}, BrepOperation::Intersection, 0, diagnostics)
                .isEmpty();
    };
    CHECK(occupied(2.7));
    CHECK_FALSE(occupied(2.9));  // An unweighted quadratic would bulge into this probe.
  }
  if (joinProbe) {
    auto probe = BrepGeometry::cube(0.01, 0.01, 0.01);
    Transform3d placement = Transform3d::Identity();
    placement.translate(joinProbe->first);
    probe.transform(placement);
    BrepFilletDiagnostics diagnostics;
    CHECK(
      !BrepGeometry::boolean({*brep, probe}, BrepOperation::Intersection, 0, diagnostics).isEmpty() ==
      joinProbe->second);
  }
  CHECK(brep->getBoundingBox().min().x() ==
        Catch::Approx(imported->center ? -4 : expectedMinX).margin(1e-5));
  CHECK(brep->getBoundingBox().max().x() ==
        Catch::Approx(imported->center ? 4 : expectedMaxX).margin(1e-5));
}

TEST_CASE("B-Rep DXF extrusion retains curves and placement", "[brep]")
{
  ModuleInstantiation inst("import");
  auto imported = std::make_shared<ImportNode>(
    &inst, ImportType::DXF,
    CurveDiscretizer([](const char *) -> std::optional<double> { return std::nullopt; }));
  imported->filename =
    Filename((std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() /
              "tests/data/dxf/brep-curves.dxf")
               .string());
  imported->layer = "circle";
  bool unsupported = false;
  imported->origin_x = 1;
  imported->origin_y = 2;
  imported->scale = 2;
  imported->center = false;
  size_t curvedFaces = 4;
  double minY = 0, maxY = 8;
  SECTION("circle")
  {
  }
  SECTION("arc with closing line")
  {
    imported->layer = "arc";
    curvedFaces = 2;
    minY = 4;
  }
  SECTION("ellipse vector and ratio are not translated or scaled twice")
  {
    imported->layer = "ellipse";
    minY = 2;
    maxY = 6;
  }
  SECTION("scaled rotated block")
  {
    imported->layer = "block";
    minY = -4;
    maxY = 12;
  }
  SECTION("even-odd circular hole")
  {
    imported->layer = "ring";
    curvedFaces = 8;
  }
  SECTION("explicit circle facets")
  {
    imported->discretizer = CurveDiscretizer(6.0);
    curvedFaces = 0;
    minY = 4 - 2 * std::sqrt(3.0);
    maxY = 4 + 2 * std::sqrt(3.0);
  }
  SECTION("polyline bulge retains its circular arc")
  {
    imported->layer = "bulge";
    curvedFaces = 2;
    minY = -2;
  }
  SECTION("explicit polyline bulge facets")
  {
    imported->layer = "bulge";
    imported->discretizer = CurveDiscretizer(6.0);
    curvedFaces = 0;
    minY = -2;
  }
  auto extrusion = std::make_shared<LinearExtrudeNode>(&inst, CurveDiscretizer(6.0));
  extrusion->height = Vector3d(0, 0, 5);
  extrusion->children = {imported};
  const auto previous = RenderSettings::inst()->backend3D;
  RenderSettings::inst()->backend3D = RenderBackend3D::OpenCASCADEBackend;
  Tree tree(extrusion);
  GeometryEvaluator evaluator(tree);
  const auto geometry = evaluator.evaluateGeometry(*extrusion, true);
  RenderSettings::inst()->backend3D = previous;
  const auto brep = std::dynamic_pointer_cast<const BrepGeometry>(geometry);
  REQUIRE(brep);
  if (unsupported) {
    CHECK(brep->isEmpty());
    return;
  }
  REQUIRE_FALSE(brep->isEmpty());
  CHECK(brep->surfaceCount(BrepSurfaceType::Other) == curvedFaces);
  CHECK(brep->getBoundingBox().min().x() == Catch::Approx(0).margin(1e-5));
  CHECK(brep->getBoundingBox().max().x() == Catch::Approx(8).margin(1e-5));
  CHECK(brep->getBoundingBox().min().y() == Catch::Approx(minY).margin(1e-5));
  CHECK(brep->getBoundingBox().max().y() == Catch::Approx(maxY).margin(1e-5));
  if (imported->layer.get() == "ring") {
    auto probe = BrepGeometry::cube(0.01, 0.01, 0.01);
    Transform3d placement = Transform3d::Identity();
    placement.translate(Vector3d(4, 4, 1));
    probe.transform(placement);
    BrepFilletDiagnostics diagnostics;
    CHECK(BrepGeometry::boolean({*brep, probe}, BrepOperation::Intersection, 0, diagnostics).isEmpty());
  }
}

TEST_CASE("B-Rep text extrusion retains font Bezier curves", "[brep]")
{
  PlatformUtils::registerApplicationPath(
    std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().string());
  ModuleInstantiation inst("text");
  FreetypeRenderer::Params::ParamsOptions options;
  options.text = "Oo";
  options.font = "Liberation Sans";
  FreetypeRenderer::Params params(options);
  params.detect_properties();
  auto text = std::make_shared<TextNode>(&inst, std::move(params));
  auto extrusion = std::make_shared<LinearExtrudeNode>(&inst, CurveDiscretizer(6.0));
  extrusion->height = Vector3d(0, 0, 5);
  extrusion->children = {text};
  const auto previous = RenderSettings::inst()->backend3D;
  RenderSettings::inst()->backend3D = RenderBackend3D::OpenCASCADEBackend;
  Tree tree(extrusion);
  GeometryEvaluator evaluator(tree);
  const auto result = evaluator.evaluateGeometry(*extrusion, true);
  RenderSettings::inst()->backend3D = previous;
  const auto brep = std::dynamic_pointer_cast<const BrepGeometry>(result);
  REQUIRE(brep);
  REQUIRE_FALSE(brep->isEmpty());
  CHECK(brep->surfaceCount(BrepSurfaceType::Other) + brep->surfaceCount(BrepSurfaceType::Bezier) +
          brep->surfaceCount(BrepSurfaceType::BSpline) >
        0);
  CHECK_FALSE(brep->toDisplayMesh(0.1, 0.2).triangles.empty());
  const auto glyphs = text->createPolygonList();
  REQUIRE(glyphs.size() == 2);
  for (const auto& glyph : glyphs) {
    auto probe = BrepGeometry::cube(0.01, 0.01, 0.01);
    Transform3d transform = Transform3d::Identity();
    transform.translate(glyph->getBoundingBox().center() + Vector3d(0, 0, 1));
    probe.transform(transform);
    BrepFilletDiagnostics diagnostics;
    CHECK(BrepGeometry::boolean({*brep, probe}, BrepOperation::Intersection, 0, diagnostics).isEmpty());
  }
}

TEST_CASE("B-Rep font contours use nonzero winding in overlaps", "[brep]")
{
  using Point = std::array<double, 2>;
  const auto rectangle = [](double x) {
    return std::vector<std::vector<Point>>{
      {{x, 0}, {x + 2, 0}}, {{x + 2, 0}, {x + 2, 2}}, {{x + 2, 2}, {x, 2}}, {{x, 2}, {x, 0}}};
  };
  auto a = rectangle(0), b = rectangle(1);
  bool opposite = false;
  SECTION("same winding")
  {
  }
  SECTION("opposite winding")
  {
    std::reverse(b.begin(), b.end());
    for (auto& curve : b) std::reverse(curve.begin(), curve.end());
    opposite = true;
  }
  const auto shape = BrepGeometry::bezierPrism({a, b}, 1);
  auto probe = BrepGeometry::cube(0.1, 0.1, 0.1);
  Transform3d transform = Transform3d::Identity();
  transform.translate(Vector3d(1.5, 1, 0.5));
  probe.transform(transform);
  BrepFilletDiagnostics diagnostics;
  CHECK(BrepGeometry::boolean({shape, probe}, BrepOperation::Intersection, 0, diagnostics).isEmpty() ==
        opposite);
}

TEST_CASE("B-Rep hull and Minkowski retain native solids", "[brep]")
{
  ModuleInstantiation inst("advanced");
  auto first = std::make_shared<CubeNode>(&inst);
  first->x = first->y = first->z = 2;
  auto second = std::make_shared<CubeNode>(&inst);
  second->x = second->y = second->z = 2;
  auto advanced = std::make_shared<CgalAdvNode>(&inst, CgalAdvType::HULL);
  double expectedMin = 0, expectedMax = 6;
  bool rounded = false;
  SECTION("polyhedral hull")
  {
    auto transform = std::make_shared<TransformNode>(&inst, "translate");
    transform->matrix.translate(Vector3d(4, 0, 0));
    transform->children = {second};
    advanced->children = {first, transform};
  }
  SECTION("convex polyhedral Minkowski")
  {
    advanced->type = CgalAdvType::MINKOWSKI;
    advanced->children = {first, second};
    expectedMax = 4;
  }
  SECTION("spherical Minkowski")
  {
    advanced->type = CgalAdvType::MINKOWSKI;
    auto sphere = std::make_shared<SphereNode>(
      &inst, CurveDiscretizer([](const char *) -> std::optional<double> { return std::nullopt; }));
    sphere->r = 1;
    advanced->children = {first, sphere};
    expectedMin = -1;
    expectedMax = 3;
    rounded = true;
  }
  const auto previous = RenderSettings::inst()->backend3D;
  RenderSettings::inst()->backend3D = RenderBackend3D::OpenCASCADEBackend;
  Tree tree(advanced);
  GeometryEvaluator evaluator(tree);
  const auto result = evaluator.evaluateGeometry(*advanced, true);
  RenderSettings::inst()->backend3D = previous;
  const auto brep = std::dynamic_pointer_cast<const BrepGeometry>(result);
  REQUIRE(brep);
  REQUIRE_FALSE(brep->isEmpty());
  CHECK(brep->getBoundingBox().min().x() == Catch::Approx(expectedMin).margin(1e-5));
  CHECK(brep->getBoundingBox().max().x() == Catch::Approx(expectedMax).margin(1e-5));
  CHECK((brep->surfaceCount(BrepSurfaceType::Sphere) > 0) == rounded);
  CHECK_FALSE(brep->toDisplayMesh(0.1, 0.2).triangles.empty());
  const auto occupied = [&](Vector3d p) {
    auto probe = BrepGeometry::cube(0.01, 0.01, 0.01);
    Transform3d transform = Transform3d::Identity();
    transform.translate(p);
    probe.transform(transform);
    BrepFilletDiagnostics diagnostics;
    return !BrepGeometry::boolean({*brep, probe}, BrepOperation::Intersection, 0, diagnostics).isEmpty();
  };
  if (rounded) {
    CHECK(occupied({2.4, 2.4, 2.4}));
    CHECK_FALSE(occupied({2.8, 2.8, 2.8}));
  } else {
    CHECK(occupied({3, 1, 1}));
    CHECK_FALSE(occupied({expectedMax + 0.1, 1, 1}));
  }
}

TEST_CASE("B-Rep nonconvex Minkowski preserves recesses", "[brep]")
{
  BrepFilletDiagnostics diagnostics;
  auto notch = BrepGeometry::cube(4, 4, 4);
  Transform3d placement = Transform3d::Identity();
  placement.translate(Vector3d(2, 2, -1));
  notch.transform(placement);
  const auto concave = BrepGeometry::boolean({BrepGeometry::cube(5, 5, 2), notch},
                                             BrepOperation::Difference, 0, diagnostics);
  auto kernel = BrepGeometry::cube(1, 1, 1);
  SECTION("polyhedral kernel")
  {
  }
  SECTION("spherical kernel")
  {
    kernel = BrepGeometry::sphere(0.5);
  }
  SECTION("nonconvex kernel")
  {
    kernel = concave;
  }
  const auto result = BrepGeometry::minkowski({concave, kernel});
  REQUIRE_FALSE(result.isEmpty());
  const auto occupied = [&](double x, double y) {
    auto probe = BrepGeometry::cube(0.01, 0.01, 0.01);
    auto transform = Transform3d::Identity();
    transform.translate(Vector3d(x, y, 1));
    probe.transform(transform);
    return !BrepGeometry::boolean({result, probe}, BrepOperation::Intersection, 0, diagnostics)
              .isEmpty();
  };
  CHECK(occupied(1, 4));
  CHECK(occupied(4, 1));
  CHECK_FALSE(occupied(8, 8));
  if (kernel.getBoundingBox().max().x() < 2) CHECK_FALSE(occupied(4, 4));
}

TEST_CASE("B-Rep hull of two spheres keeps the tangent envelope smooth", "[brep]")
{
  ModuleInstantiation inst("hull");
  const CurveDiscretizer smooth([](const char *) -> std::optional<double> { return std::nullopt; });
  auto first = std::make_shared<SphereNode>(&inst, smooth);
  first->r = 2;
  auto second = std::make_shared<SphereNode>(&inst, smooth);
  second->r = 1;
  auto translate = std::make_shared<TransformNode>(&inst, "translate");
  translate->matrix.translate(Vector3d(6, 0, 0));
  translate->children = {second};
  auto hull = std::make_shared<CgalAdvNode>(&inst, CgalAdvType::HULL);
  hull->children = {first, translate};
  const auto previous = RenderSettings::inst()->backend3D;
  RenderSettings::inst()->backend3D = RenderBackend3D::OpenCASCADEBackend;
  Tree tree(hull);
  GeometryEvaluator evaluator(tree);
  const auto result = evaluator.evaluateGeometry(*hull, true);
  RenderSettings::inst()->backend3D = previous;
  const auto brep = std::dynamic_pointer_cast<const BrepGeometry>(result);
  REQUIRE(brep);
  REQUIRE_FALSE(brep->isEmpty());
  CHECK(brep->surfaceCount(BrepSurfaceType::Sphere) >= 2);
  CHECK(brep->surfaceCount(BrepSurfaceType::Cone) == 1);
  CHECK(brep->getBoundingBox().min().x() == Catch::Approx(-2).margin(1e-5));
  CHECK(brep->getBoundingBox().max().x() == Catch::Approx(7).margin(1e-5));
}

TEST_CASE("B-Rep hull of separated coaxial cylinders keeps the tangent envelope smooth", "[brep]")
{
  ModuleInstantiation inst("hull");
  const CurveDiscretizer smooth([](const char *) -> std::optional<double> { return std::nullopt; });
  auto lower = std::make_shared<CylinderNode>(&inst, smooth);
  lower->r1 = lower->r2 = 2;
  lower->h = 2;
  auto upper = std::make_shared<CylinderNode>(&inst, smooth);
  upper->r1 = upper->r2 = 1;
  upper->h = 2;
  auto translate = std::make_shared<TransformNode>(&inst, "translate");
  translate->matrix.translate(Vector3d(0, 0, 4));
  translate->children = {upper};
  auto hull = std::make_shared<CgalAdvNode>(&inst, CgalAdvType::HULL);
  hull->children = {lower, translate};
  const auto previous = RenderSettings::inst()->backend3D;
  RenderSettings::inst()->backend3D = RenderBackend3D::OpenCASCADEBackend;
  Tree tree(hull);
  GeometryEvaluator evaluator(tree);
  const auto result = evaluator.evaluateGeometry(*hull, true);
  RenderSettings::inst()->backend3D = previous;
  const auto brep = std::dynamic_pointer_cast<const BrepGeometry>(result);
  REQUIRE(brep);
  REQUIRE_FALSE(brep->isEmpty());
  CHECK(brep->surfaceCount(BrepSurfaceType::Cylinder) >= 2);
  CHECK(brep->surfaceCount(BrepSurfaceType::Cone) == 1);
  CHECK(brep->getBoundingBox().min().z() == Catch::Approx(0).margin(1e-5));
  CHECK(brep->getBoundingBox().max().z() == Catch::Approx(6).margin(1e-5));
}

TEST_CASE("B-Rep planar hull and Minkowski preserve circles", "[brep]")
{
  ModuleInstantiation inst("advanced");
  const CurveDiscretizer smooth([](const char *) -> std::optional<double> { return std::nullopt; });
  auto circle = std::make_shared<CircleNode>(&inst, smooth);
  circle->r = 1;
  auto advanced = std::make_shared<CgalAdvNode>(&inst, CgalAdvType::HULL);
  double expectedMax = 7;
  SECTION("two-circle hull")
  {
    auto second = std::make_shared<CircleNode>(&inst, smooth);
    second->r = 2;
    auto translate = std::make_shared<TransformNode>(&inst, "translate");
    translate->matrix.translate(Vector3d(5, 0, 0));
    translate->children = {second};
    advanced->children = {circle, translate};
  }
  SECTION("circle Minkowski")
  {
    advanced->type = CgalAdvType::MINKOWSKI;
    auto square = std::make_shared<SquareNode>(&inst);
    square->x = square->y = 4;
    advanced->children = {circle, square};
    expectedMax = 5;
  }
  auto extrusion = std::make_shared<LinearExtrudeNode>(&inst, smooth);
  extrusion->height = Vector3d(0, 0, 3);
  extrusion->children = {advanced};
  const auto previous = RenderSettings::inst()->backend3D;
  RenderSettings::inst()->backend3D = RenderBackend3D::OpenCASCADEBackend;
  Tree tree(extrusion);
  GeometryEvaluator evaluator(tree);
  const auto result = evaluator.evaluateGeometry(*extrusion, true);
  RenderSettings::inst()->backend3D = previous;
  const auto brep = std::dynamic_pointer_cast<const BrepGeometry>(result);
  REQUIRE(brep);
  REQUIRE_FALSE(brep->isEmpty());
  CHECK(brep->surfaceCount(BrepSurfaceType::Cylinder) > 0);
  CHECK(brep->getBoundingBox().min().x() == Catch::Approx(-1).margin(1e-5));
  CHECK(brep->getBoundingBox().max().x() == Catch::Approx(expectedMax).margin(1e-5));
  CHECK(brep->getBoundingBox().max().z() == Catch::Approx(3).margin(1e-5));
}

TEST_CASE("B-Rep evaluator reports open polyhedra without throwing", "[brep]")
{
  ModuleInstantiation inst("polyhedron");
  auto open = std::make_shared<PolyhedronNode>(&inst);
  open->points = {{0, 0, 0}, {10, 0, 0}, {0, 10, 0}};
  open->faces = {{0, 1, 2}};
  const auto previousBackend = RenderSettings::inst()->backend3D;
  RenderSettings::inst()->backend3D = RenderBackend3D::OpenCASCADEBackend;
  Tree tree(open);
  GeometryEvaluator evaluator(tree);
  std::shared_ptr<const Geometry> result;
  CHECK_NOTHROW(result = evaluator.evaluateGeometry(*open, true));
  RenderSettings::inst()->backend3D = previousBackend;
  REQUIRE(result);
  REQUIRE(result->isEmpty());
}

TEST_CASE("Linear extrusion preserves analytic and polygonal profiles", "[brep]")
{
  ModuleInstantiation inst("linear_extrude");
  const auto smooth = [] {
    return CurveDiscretizer([](const char *) -> std::optional<double> { return std::nullopt; });
  };
  auto extrusion = std::make_shared<LinearExtrudeNode>(&inst, smooth());
  extrusion->height = Vector3d(0, 0, 8);
  extrusion->center = true;
  size_t cylinders = 0;
  double minX = -4.0, maxX = 4.0;
  SECTION("analytic annulus")
  {
    auto outer = std::make_shared<CircleNode>(&inst, smooth());
    outer->r = 4.0;
    auto inner = std::make_shared<CircleNode>(&inst, smooth());
    inner->r = 2.0;
    auto difference = std::make_shared<CsgOpNode>(&inst, OpenSCADOperator::DIFFERENCE);
    difference->children = {outer, inner};
    extrusion->children = {difference};
    cylinders = 2;
  }
  SECTION("intentional hexagon")
  {
    auto circle = std::make_shared<CircleNode>(&inst, CurveDiscretizer(6.0));
    circle->r = 4.0;
    extrusion->children = {circle};
  }
  SECTION("centered square")
  {
    auto square = std::make_shared<SquareNode>(&inst);
    square->x = square->y = 8.0;
    square->center = true;
    extrusion->children = {square};
  }
  SECTION("concave polygon")
  {
    auto polygon = std::make_shared<PolygonNode>(&inst);
    polygon->points = {{0, 0}, {4, 0}, {4, 2}, {2, 2}, {2, 4}, {0, 4}};
    extrusion->children = {polygon};
    minX = 0.0;
  }
  SECTION("profile transform projects to XY")
  {
    auto circle = std::make_shared<CircleNode>(&inst, smooth());
    circle->r = 4.0;
    auto transform = std::make_shared<TransformNode>(&inst, "translate");
    transform->matrix.translate(Vector3d(3, 0, 100));
    transform->children = {circle};
    extrusion->children = {transform};
    cylinders = 1;
    minX = -1.0;
    maxX = 7.0;
  }
  SECTION("oblique extrusion")
  {
    auto square = std::make_shared<SquareNode>(&inst);
    square->x = square->y = 8.0;
    square->center = true;
    extrusion->children = {square};
    extrusion->height = Vector3d(4, 2, 8);
    minX = -6.0;
    maxX = 6.0;
  }
  const auto previousBackend = RenderSettings::inst()->backend3D;
  RenderSettings::inst()->backend3D = RenderBackend3D::OpenCASCADEBackend;
  Tree tree(extrusion);
  GeometryEvaluator evaluator(tree);
  const auto result = evaluator.evaluateGeometry(*extrusion, true);
  RenderSettings::inst()->backend3D = previousBackend;
  const auto brep = std::dynamic_pointer_cast<const BrepGeometry>(result);
  REQUIRE(brep);
  REQUIRE_FALSE(brep->isEmpty());
  REQUIRE(brep->surfaceCount(BrepSurfaceType::Cylinder) == cylinders);
  REQUIRE(brep->getBoundingBox().min().x() == Catch::Approx(minX).margin(0.001));
  REQUIRE(brep->getBoundingBox().max().x() == Catch::Approx(maxX).margin(0.001));
  REQUIRE(brep->getBoundingBox().min().z() == Catch::Approx(-4.0).margin(0.001));
  REQUIRE(brep->getBoundingBox().max().z() == Catch::Approx(4.0).margin(0.001));
  REQUIRE_FALSE(brep->toDisplayMesh(0.1, 0.2).triangles.empty());
}

TEST_CASE("B-Rep extrusion preserves polygon contour nesting", "[brep]")
{
  ModuleInstantiation inst("linear_extrude");
  auto polygon = std::make_shared<PolygonNode>(&inst);
  polygon->points = {{0, 0}, {10, 0}, {10, 10}, {0, 10}, {2, 2},  {8, 2},  {8, 8},  {2, 8},
                     {4, 4}, {6, 4},  {6, 6},   {4, 6},  {12, 0}, {14, 0}, {14, 2}, {12, 2}};
  polygon->paths = {{0, 1, 2, 3}, {4, 5, 6, 7}};
  bool island = false, disconnected = false;
  SECTION("hole")
  {
  }
  SECTION("reversed hole before outer contour")
  {
    polygon->paths = {{7, 6, 5, 4}, {3, 2, 1, 0}};
  }
  SECTION("island inside a hole and disconnected region")
  {
    polygon->paths = {{8, 9, 10, 11}, {4, 5, 6, 7}, {12, 13, 14, 15}, {0, 1, 2, 3}};
    island = disconnected = true;
  }
  auto extrusion = std::make_shared<LinearExtrudeNode>(&inst, CurveDiscretizer(0.0));
  extrusion->height = Vector3d(0, 0, 8);
  extrusion->children = {polygon};
  const auto previousBackend = RenderSettings::inst()->backend3D;
  RenderSettings::inst()->backend3D = RenderBackend3D::OpenCASCADEBackend;
  Tree tree(extrusion);
  GeometryEvaluator evaluator(tree);
  const auto result = evaluator.evaluateGeometry(*extrusion, true);
  RenderSettings::inst()->backend3D = previousBackend;
  const auto brep = std::dynamic_pointer_cast<const BrepGeometry>(result);
  REQUIRE(brep);
  REQUIRE_FALSE(brep->isEmpty());
  REQUIRE(brep->numFacets() == 0);
  const auto occupied = [&](double x, double y) {
    auto probe = BrepGeometry::cube(0.5, 0.5, 1.0);
    Transform3d transform = Transform3d::Identity();
    transform.translate(Vector3d(x, y, 3.0));
    probe.transform(transform);
    BrepFilletDiagnostics diagnostics;
    return !BrepGeometry::boolean({*brep, probe}, BrepOperation::Intersection, 0.0, diagnostics)
              .isEmpty();
  };
  CHECK(occupied(0.5, 0.5));
  CHECK_FALSE(occupied(2.5, 2.5));
  CHECK(occupied(4.5, 4.5) == island);
  CHECK(occupied(12.5, 0.5) == disconnected);
  CHECK_FALSE(occupied(10.5, 0.5));
  CHECK_FALSE(brep->toDisplayMesh(0.1, 0.2).triangles.empty());
}

TEST_CASE("B-Rep rotational extrusion retains smooth surfaces", "[brep]")
{
  ModuleInstantiation inst("rotate_extrude");
  const auto smooth = [] {
    return CurveDiscretizer([](const char *) -> std::optional<double> { return std::nullopt; });
  };
  auto revolution = std::make_shared<RotateExtrudeNode>(&inst, smooth());
  auto profile = std::make_shared<TransformNode>(&inst, "translate");
  profile->matrix.translate(Vector3d(5, 0, 0));
  auto circle = std::make_shared<CircleNode>(&inst, smooth());
  circle->r = 1.0;
  profile->children = {circle};
  revolution->children = {profile};
  size_t tori = 1, cylinders = 0;
  bool positiveY = true, negativeY = true;
  SECTION("full torus")
  {
  }
  SECTION("partial torus")
  {
    revolution->angle = 180;
    negativeY = false;
  }
  SECTION("negative angle")
  {
    revolution->angle = -180;
    positiveY = false;
  }
  SECTION("start angle")
  {
    revolution->angle = 180;
    revolution->start = 180;
    positiveY = false;
  }
  SECTION("negative-side profile")
  {
    profile->matrix.translation().x() = -5;
    revolution->angle = 180;
    positiveY = false;
  }
  SECTION("polygonal profile remains a smooth revolution")
  {
    auto square = std::make_shared<SquareNode>(&inst);
    square->x = square->y = 2;
    square->center = true;
    profile->children = {square};
    tori = 0;
    cylinders = 2;
  }
  SECTION("explicitly faceted circle profile")
  {
    circle->discretizer = CurveDiscretizer(6.0);
    tori = 0;
  }
  SECTION("hollow torus")
  {
    auto inner = std::make_shared<CircleNode>(&inst, smooth());
    inner->r = 0.5;
    auto difference = std::make_shared<CsgOpNode>(&inst, OpenSCADOperator::DIFFERENCE);
    difference->children = {circle, inner};
    profile->children = {difference};
    tori = 2;
    positiveY = negativeY = false;
  }
  const auto previousBackend = RenderSettings::inst()->backend3D;
  RenderSettings::inst()->backend3D = RenderBackend3D::OpenCASCADEBackend;
  Tree tree(revolution);
  GeometryEvaluator evaluator(tree);
  const auto result = evaluator.evaluateGeometry(*revolution, true);
  RenderSettings::inst()->backend3D = previousBackend;
  const auto brep = std::dynamic_pointer_cast<const BrepGeometry>(result);
  REQUIRE(brep);
  REQUIRE_FALSE(brep->isEmpty());
  CHECK(brep->surfaceCount(BrepSurfaceType::Torus) == tori);
  CHECK(brep->surfaceCount(BrepSurfaceType::Cylinder) == cylinders);
  CHECK(brep->numFacets() == 0);
  for (const auto y : {-5.0, 5.0}) {
    auto probe = BrepGeometry::cube(0.1, 0.1, 0.1);
    Transform3d transform = Transform3d::Identity();
    transform.translate(Vector3d(0, y, 0));
    probe.transform(transform);
    BrepFilletDiagnostics diagnostics;
    const auto intersection =
      BrepGeometry::boolean({*brep, probe}, BrepOperation::Intersection, 0, diagnostics);
    CHECK(!intersection.isEmpty() == (y > 0 ? positiveY : negativeY));
  }
  CHECK_FALSE(brep->toDisplayMesh(0.1, 0.2).triangles.empty());
}

TEST_CASE("B-Rep explicit sweep facets remain intentional geometry", "[brep]")
{
  ModuleInstantiation inst("rotate_extrude");
  auto revolution = std::make_shared<RotateExtrudeNode>(&inst, CurveDiscretizer(6.0));
  auto square = std::make_shared<SquareNode>(&inst);
  square->x = 4;
  square->y = 2;
  revolution->children = {square};
  size_t planes = 8;
  SECTION("hexagonal solid touching axis")
  {
  }
  SECTION("partial sweep")
  {
    revolution->angle = 180;
    planes = 6;  // The two radial caps merge into one coplanar face.
  }
  SECTION("negative sweep with start")
  {
    revolution->angle = -180;
    revolution->start = 90;
    planes = 6;
  }
  SECTION("hollow hexagonal solid")
  {
    auto polygon = std::make_shared<PolygonNode>(&inst);
    polygon->points = {{2, 0}, {4, 0}, {4, 2}, {2, 2}};
    revolution->children = {polygon};
    planes = 14;
  }
  SECTION("smooth profile with polygonal sweep")
  {
    auto circle = std::make_shared<CircleNode>(
      &inst, CurveDiscretizer([](const char *) -> std::optional<double> { return std::nullopt; }));
    circle->r = 1;
    auto translate = std::make_shared<TransformNode>(&inst, "translate");
    translate->matrix.translate(Vector3d(5, 0, 0));
    translate->children = {circle};
    revolution->children = {translate};
    planes = 0;
  }
  SECTION("profile with a hole")
  {
    auto polygon = std::make_shared<PolygonNode>(&inst);
    polygon->points = {{2, 0}, {5, 0}, {5, 4}, {2, 4}, {3, 1}, {4, 1}, {4, 3}, {3, 3}};
    polygon->paths = {{0, 1, 2, 3}, {4, 5, 6, 7}};
    revolution->children = {polygon};
    planes = 28;
  }
  const auto previousBackend = RenderSettings::inst()->backend3D;
  RenderSettings::inst()->backend3D = RenderBackend3D::OpenCASCADEBackend;
  Tree tree(revolution);
  GeometryEvaluator evaluator(tree);
  const auto result = evaluator.evaluateGeometry(*revolution, true);
  RenderSettings::inst()->backend3D = previousBackend;
  const auto brep = std::dynamic_pointer_cast<const BrepGeometry>(result);
  REQUIRE(brep);
  REQUIRE_FALSE(brep->isEmpty());
  CHECK(brep->surfaceCount(BrepSurfaceType::Plane) == planes);
  CHECK(brep->surfaceCount(BrepSurfaceType::Cylinder) == 0);
  CHECK(brep->surfaceCount(BrepSurfaceType::Torus) == 0);
  CHECK(brep->numFacets() == 0);
  CHECK_FALSE(brep->toDisplayMesh(0.1, 0.2).triangles.empty());
}

TEST_CASE("B-Rep rotational extrusion rejects invalid sweeps", "[brep]")
{
  ModuleInstantiation inst("rotate_extrude");
  auto revolution = std::make_shared<RotateExtrudeNode>(&inst, CurveDiscretizer(0.0));
  auto square = std::make_shared<SquareNode>(&inst);
  square->x = square->y = 2;
  revolution->children = {square};
  SECTION("axis crossing")
  {
    square->center = true;
  }
  SECTION("zero angle")
  {
    revolution->angle = 0;
  }
  const auto previousBackend = RenderSettings::inst()->backend3D;
  RenderSettings::inst()->backend3D = RenderBackend3D::OpenCASCADEBackend;
  Tree tree(revolution);
  GeometryEvaluator evaluator(tree);
  const auto result = evaluator.evaluateGeometry(*revolution, true);
  RenderSettings::inst()->backend3D = previousBackend;
  REQUIRE(std::dynamic_pointer_cast<const BrepGeometry>(result));
  REQUIRE(result->isEmpty());
}

TEST_CASE("B-Rep tapered extrusion preserves profiles and holes", "[brep]")
{
  ModuleInstantiation inst("linear_extrude");
  auto extrusion = std::make_shared<LinearExtrudeNode>(&inst, CurveDiscretizer(0.0));
  extrusion->height = Vector3d(0, 0, 8);
  extrusion->scale_x = extrusion->scale_y = 0.5;
  auto circle = std::make_shared<CircleNode>(
    &inst, CurveDiscretizer([](const char *) -> std::optional<double> { return std::nullopt; }));
  circle->r = 4;
  extrusion->children = {circle};
  bool hole = false, curved = true;
  SECTION("smooth circular taper")
  {
  }
  SECTION("nonuniform scale")
  {
    extrusion->scale_y = 0.75;
  }
  SECTION("widening taper")
  {
    extrusion->scale_x = extrusion->scale_y = 1.5;
  }
  SECTION("nonuniform polygon with diagonal edges")
  {
    auto polygon = std::make_shared<PolygonNode>(&inst);
    polygon->points = {{4, 0}, {0, 4}, {-4, 0}, {0, -4}};
    extrusion->children = {polygon};
    extrusion->scale_y = 0.75;
    curved = false;
  }
  SECTION("centered oblique taper")
  {
    extrusion->center = true;
    extrusion->height = Vector3d(4, 2, 8);
  }
  SECTION("hollow taper")
  {
    auto inner = std::make_shared<CircleNode>(*circle);
    inner->r = 2;
    auto difference = std::make_shared<CsgOpNode>(&inst, OpenSCADOperator::DIFFERENCE);
    difference->children = {circle, inner};
    extrusion->children = {difference};
    hole = true;
  }
  SECTION("intentional hexagonal profile")
  {
    circle->discretizer = CurveDiscretizer(6.0);
    curved = false;
  }
  const auto previousBackend = RenderSettings::inst()->backend3D;
  RenderSettings::inst()->backend3D = RenderBackend3D::OpenCASCADEBackend;
  Tree tree(extrusion);
  GeometryEvaluator evaluator(tree);
  const auto result = evaluator.evaluateGeometry(*extrusion, true);
  RenderSettings::inst()->backend3D = previousBackend;
  const auto brep = std::dynamic_pointer_cast<const BrepGeometry>(result);
  REQUIRE(brep);
  REQUIRE_FALSE(brep->isEmpty());
  CHECK(brep->numFacets() == 0);
  // Probe at both ends: the base is wide, while the top has narrowed.
  const auto occupied = [&](double x, double z, double y = 0.0) {
    auto probe = BrepGeometry::cube(0.1, 0.1, 0.1);
    Vector3d point(x, y, z);
    point.x() += extrusion->height.x() * z / 8;
    point.y() += extrusion->height.y() * z / 8;
    if (extrusion->center) point -= extrusion->height / 2;
    Transform3d transform = Transform3d::Identity();
    transform.translate(point);
    probe.transform(transform);
    BrepFilletDiagnostics diagnostics;
    return !BrepGeometry::boolean({*brep, probe}, BrepOperation::Intersection, 0, diagnostics).isEmpty();
  };
  CHECK(occupied(3, 0.2));
  CHECK(occupied(3, 7.5) == (extrusion->scale_x > 1));
  CHECK(occupied(1.5, 7.5));
  CHECK(occupied(0, 4) == !hole);
  if (curved) {
    CHECK(occupied(0, 7.5, 2.5) == (extrusion->scale_y > 0.5));
    CHECK(brep->surfaceCount(BrepSurfaceType::Cone) + brep->surfaceCount(BrepSurfaceType::BSpline) +
            brep->surfaceCount(BrepSurfaceType::Bezier) + brep->surfaceCount(BrepSurfaceType::Other) >
          0);
  }
  CHECK_FALSE(brep->toDisplayMesh(0.1, 0.2).triangles.empty());
}

TEST_CASE("B-Rep zero-scale extrusion closes at an apex", "[brep]")
{
  ModuleInstantiation inst("linear_extrude");
  auto extrusion = std::make_shared<LinearExtrudeNode>(&inst, CurveDiscretizer(6.0));
  extrusion->height = Vector3d(0, 0, 8);
  extrusion->scale_x = extrusion->scale_y = 0;
  auto circle = std::make_shared<CircleNode>(
    &inst, CurveDiscretizer([](const char *) -> std::optional<double> { return std::nullopt; }));
  circle->r = 4;
  extrusion->children = {circle};
  bool hollow = false;
  double offset = 0;
  SECTION("smooth cone")
  {
  }
  SECTION("hexagonal pyramid")
  {
    circle->discretizer = CurveDiscretizer(6.0);
  }
  SECTION("offset profile converges to the origin apex")
  {
    offset = 5;
    auto translate = std::make_shared<TransformNode>(&inst, "translate");
    translate->matrix.translate(Vector3d(offset, 0, 0));
    translate->children = {circle};
    extrusion->children = {translate};
  }
  SECTION("hollow cone")
  {
    auto inner = std::make_shared<CircleNode>(&inst, circle->discretizer);
    inner->r = 2;
    auto difference = std::make_shared<CsgOpNode>(&inst, OpenSCADOperator::DIFFERENCE);
    difference->children = {circle, inner};
    extrusion->children = {difference};
    hollow = true;
  }
  const auto previousBackend = RenderSettings::inst()->backend3D;
  RenderSettings::inst()->backend3D = RenderBackend3D::OpenCASCADEBackend;
  Tree tree(extrusion);
  GeometryEvaluator evaluator(tree);
  const auto result = evaluator.evaluateGeometry(*extrusion, true);
  RenderSettings::inst()->backend3D = previousBackend;
  const auto brep = std::dynamic_pointer_cast<const BrepGeometry>(result);
  REQUIRE(brep);
  REQUIRE_FALSE(brep->isEmpty());
  CHECK(brep->numFacets() == 0);
  CHECK(brep->getBoundingBox().max().z() == Catch::Approx(8).margin(0.001));
  if (circle->discretizer.isFnSpecified()) {
    CHECK(brep->surfaceCount(BrepSurfaceType::Plane) == 7);
  } else {
    CHECK(brep->surfaceCount(BrepSurfaceType::Cone) + brep->surfaceCount(BrepSurfaceType::BSpline) +
            brep->surfaceCount(BrepSurfaceType::Bezier) + brep->surfaceCount(BrepSurfaceType::Other) >
          0);
  }
  const auto occupied = [&](double x, double z) {
    auto probe = BrepGeometry::cube(0.1, 0.1, 0.1);
    Transform3d transform = Transform3d::Identity();
    transform.translate(Vector3d(x + offset * (1 - z / 8), 0, z));
    probe.transform(transform);
    BrepFilletDiagnostics diagnostics;
    return !BrepGeometry::boolean({*brep, probe}, BrepOperation::Intersection, 0, diagnostics).isEmpty();
  };
  CHECK(occupied(1.5, 4));
  CHECK_FALSE(occupied(3, 4));
  CHECK_FALSE(occupied(1, 7));
  CHECK(occupied(0, 4) == !hollow);
  CHECK_FALSE(brep->toDisplayMesh(0.1, 0.2).triangles.empty());
}

TEST_CASE("B-Rep twisted extrusion follows the profile throughout its height", "[brep]")
{
  ModuleInstantiation inst("linear_extrude");
  auto extrusion = std::make_shared<LinearExtrudeNode>(&inst, CurveDiscretizer(6.0));
  extrusion->height = Vector3d(0, 0, 20);
  extrusion->twist = 360;
  auto circle = std::make_shared<CircleNode>(
    &inst, CurveDiscretizer([](const char *) -> std::optional<double> { return std::nullopt; }));
  circle->r = 0.4;
  auto translate = std::make_shared<TransformNode>(&inst, "translate");
  translate->matrix.translate(Vector3d(3, 0, 0));
  translate->children = {circle};
  extrusion->children = {translate};
  bool hollow = false;
  SECTION("full turn")
  {
  }
  SECTION("hollow twisted profile")
  {
    auto inner = std::make_shared<CircleNode>(&inst, circle->discretizer);
    inner->r = 0.2;
    auto difference = std::make_shared<CsgOpNode>(&inst, OpenSCADOperator::DIFFERENCE);
    difference->children = {circle, inner};
    translate->children = {difference};
    hollow = true;
  }
  SECTION("negative multiple turns")
  {
    extrusion->twist = -720;
  }
  SECTION("twist combined with one-axis collapse")
  {
    extrusion->scale_x = 0;
  }
  SECTION("nonuniform taper")
  {
    extrusion->scale_x = 0.5;
    extrusion->scale_y = 1.5;
  }
  SECTION("apex")
  {
    extrusion->scale_x = extrusion->scale_y = 0;
  }
  SECTION("faceted profile")
  {
    circle->discretizer = CurveDiscretizer(6.0);
  }
  const auto previousBackend = RenderSettings::inst()->backend3D;
  RenderSettings::inst()->backend3D = RenderBackend3D::OpenCASCADEBackend;
  Tree tree(extrusion);
  GeometryEvaluator evaluator(tree);
  const auto result = evaluator.evaluateGeometry(*extrusion, true);
  RenderSettings::inst()->backend3D = previousBackend;
  const auto brep = std::dynamic_pointer_cast<const BrepGeometry>(result);
  REQUIRE(brep);
  REQUIRE_FALSE(brep->isEmpty());
  CHECK(brep->numFacets() == 0);
  for (double t : {0.137, 0.371, 0.613, 0.887}) {
    const double angle = -extrusion->twist * t * M_PI / 180;
    const double sx = 1 + (extrusion->scale_x - 1) * t;
    const double sy = 1 + (extrusion->scale_y - 1) * t;
    auto probe = BrepGeometry::cube(0.02, 0.02, 0.02);
    Transform3d transform = Transform3d::Identity();
    const double radius = hollow ? 3.3 : 3.0;
    transform.translate(Vector3d(radius * sx * std::cos(angle), radius * sy * std::sin(angle), 20 * t));
    probe.transform(transform);
    BrepFilletDiagnostics diagnostics;
    CHECK_FALSE(
      BrepGeometry::boolean({*brep, probe}, BrepOperation::Intersection, 0, diagnostics).isEmpty());
  }
  const auto display = brep->toDisplayMesh(0.05, 0.2);
  CHECK_FALSE(display.triangles.empty());
  if (!circle->discretizer.isFnSpecified()) {
    double maxError = 0;
    size_t samples = 0;
    for (const auto& p : display.vertices) {
      const double t = p[2] / 20;
      if (t <= 0.01 || t >= 0.99) continue;
      ++samples;
      const double angle = extrusion->twist * t * M_PI / 180;
      const double x = p[0] / (1 + (extrusion->scale_x - 1) * t);
      const double y = p[1] / (1 + (extrusion->scale_y - 1) * t);
      const double radius = std::hypot(x * std::cos(angle) - y * std::sin(angle) - 3,
                                       x * std::sin(angle) + y * std::cos(angle));
      const double error =
        hollow ? std::min(std::abs(radius - 0.4), std::abs(radius - 0.2)) : std::abs(radius - 0.4);
      maxError = std::max(maxError, error);
    }
    REQUIRE(samples > 0);
    CHECK(maxError < 0.00001);
  }
}

TEST_CASE("B-Rep one-axis collapse retains a closed solid", "[brep]")
{
  ModuleInstantiation inst("linear_extrude");
  auto extrusion = std::make_shared<LinearExtrudeNode>(&inst, CurveDiscretizer(6.0));
  extrusion->height = Vector3d(0, 0, 8);
  extrusion->scale_x = 0;
  auto circle = std::make_shared<CircleNode>(
    &inst, CurveDiscretizer([](const char *) -> std::optional<double> { return std::nullopt; }));
  circle->r = 4;
  extrusion->children = {circle};
  SECTION("circle collapsed along X")
  {
  }
  SECTION("circle collapsed along Y")
  {
    extrusion->scale_x = 1;
    extrusion->scale_y = 0;
  }
  SECTION("polygon")
  {
    circle->discretizer = CurveDiscretizer(6.0);
  }
  SECTION("twisted collapse")
  {
    extrusion->twist = 90;
  }
  const auto previousBackend = RenderSettings::inst()->backend3D;
  RenderSettings::inst()->backend3D = RenderBackend3D::OpenCASCADEBackend;
  Tree tree(extrusion);
  GeometryEvaluator evaluator(tree);
  const auto result = evaluator.evaluateGeometry(*extrusion, true);
  RenderSettings::inst()->backend3D = previousBackend;
  const auto brep = std::dynamic_pointer_cast<const BrepGeometry>(result);
  REQUIRE(brep);
  REQUIRE_FALSE(brep->isEmpty());
  CHECK(brep->numFacets() == 0);
  const auto occupied = [&](double x, double y) {
    auto probe = BrepGeometry::cube(0.05, 0.05, 0.05);
    Transform3d transform = Transform3d::Identity();
    transform.translate(Vector3d(x, y, 7));
    probe.transform(transform);
    BrepFilletDiagnostics diagnostics;
    return !BrepGeometry::boolean({*brep, probe}, BrepOperation::Intersection, 0, diagnostics).isEmpty();
  };
  CHECK(occupied(0, 0));
  CHECK_FALSE(occupied(extrusion->scale_x == 0 ? 1 : 0, extrusion->scale_y == 0 ? 1 : 0));
  CHECK(occupied(extrusion->scale_x == 0 ? 0 : 2, extrusion->scale_y == 0 ? 0 : 2));
  CHECK_FALSE(brep->toDisplayMesh(0.05, 0.2).triangles.empty());
}

TEST_CASE("Invalid B-Rep extrusion parameters do not silently become meshes", "[brep]")
{
  ModuleInstantiation inst("linear_extrude");
  auto extrusion = std::make_shared<LinearExtrudeNode>(&inst, CurveDiscretizer(0.0));
  extrusion->children = {std::make_shared<SquareNode>(&inst)};
  SECTION("negative X scale")
  {
    extrusion->scale_x = -1;
  }
  SECTION("negative Y scale")
  {
    extrusion->scale_y = -1;
  }
  SECTION("excessive explicit slices")
  {
    extrusion->twist = 90;
    extrusion->has_slices = true;
    extrusion->slices = 4097;
  }
  const auto previousBackend = RenderSettings::inst()->backend3D;
  RenderSettings::inst()->backend3D = RenderBackend3D::OpenCASCADEBackend;
  Tree tree(extrusion);
  GeometryEvaluator evaluator(tree);
  const auto result = evaluator.evaluateGeometry(*extrusion, true);
  RenderSettings::inst()->backend3D = previousBackend;
  REQUIRE(std::dynamic_pointer_cast<const BrepGeometry>(result));
  REQUIRE(result->isEmpty());
}

#endif
