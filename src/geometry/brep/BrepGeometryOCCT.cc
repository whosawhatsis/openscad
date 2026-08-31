#include "geometry/brep/BrepGeometryData.h"
#include "geometry/brep/BrepBoolean.h"

#include <array>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <utility>

#include <BRepBndLib.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepLib.hxx>
#include <BRepBuilderAPI_GTransform.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepLib_ToolTriangulatedShape.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRepFill.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <Poly_Triangulation.hxx>
#include <Poly_PolygonOnTriangulation.hxx>
#include <Precision.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <gp_GTrsf.hxx>
#include <gp_Pln.hxx>

namespace {

const TopoDS_Shape& shapeFrom(const std::shared_ptr<void>& shape)
{
  return *std::static_pointer_cast<TopoDS_Shape>(shape);
}

// Both caps are transformed copies of one profile, with corresponding edge order.
std::shared_ptr<void> ruledSolid(const TopoDS_Face& first, const TopoDS_Face& last)
{
  BRepBuilderAPI_Sewing sewing(Precision::Confusion());
  sewing.Add(first.Reversed());
  sewing.Add(last);
  TopExp_Explorer aEdges(first, TopAbs_EDGE), bEdges(last, TopAbs_EDGE);
  for (; aEdges.More() && bEdges.More(); aEdges.Next(), bEdges.Next()) {
    const auto a = TopoDS::Edge(aEdges.Current()), b = TopoDS::Edge(bEdges.Current());
    const BRepAdaptor_Curve curveA(a), curveB(b);
    if (curveA.GetType() == GeomAbs_Line && curveB.GetType() == GeomAbs_Line) {
      std::vector<gp_Pnt> points;
      for (const auto& point :
           {curveA.Value(curveA.FirstParameter()), curveA.Value(curveA.LastParameter()),
            curveB.Value(curveB.LastParameter()), curveB.Value(curveB.FirstParameter())}) {
        if (points.empty() || point.Distance(points.back()) > Precision::Confusion())
          points.push_back(point);
      }
      if (points.size() > 1 && points.front().Distance(points.back()) <= Precision::Confusion())
        points.pop_back();
      if (points.size() < 3) continue;  // Fixed axis edges sweep no area.
      BRepBuilderAPI_MakePolygon boundary;
      for (const auto& point : points) boundary.Add(point);
      boundary.Close();
      BRepBuilderAPI_MakeFace planar(boundary.Wire(), true);
      if (planar.IsDone()) {
        sewing.Add(planar.Face());
        continue;
      }
    }
    sewing.Add(BRepFill::Face(a, b));
  }
  if (aEdges.More() || bEdges.More()) throw std::runtime_error("B-Rep sweep cap edges do not match");
  sewing.Perform();
  if (sewing.NbFreeEdges() || sewing.NbMultipleEdges() ||
      sewing.SewedShape().ShapeType() != TopAbs_SHELL)
    throw std::runtime_error("B-Rep ruled sweep is not a closed shell");
  auto solid = BRepBuilderAPI_MakeSolid(TopoDS::Shell(sewing.SewedShape())).Solid();
  if (!BRepLib::OrientClosedSolid(solid) || !BRepCheck_Analyzer(solid).IsValid())
    throw std::runtime_error("B-Rep ruled sweep does not form a valid solid");
  return std::make_shared<TopoDS_Shape>(solid);
}

}  // namespace

bool brepIsEmpty(const std::shared_ptr<void>& shape)
{
  return !shape || shapeFrom(shape).IsNull() || !TopExp_Explorer(shapeFrom(shape), TopAbs_FACE).More();
}

std::shared_ptr<void> brepMakeCube(double x, double y, double z)
{
  return std::make_shared<TopoDS_Shape>(BRepPrimAPI_MakeBox(x, y, z).Shape());
}

std::shared_ptr<void> brepMakeCylinder(double radius, double height)
{
  return std::make_shared<TopoDS_Shape>(BRepPrimAPI_MakeCylinder(radius, height).Shape());
}

std::shared_ptr<void> brepMakeSphere(double radius)
{
  return std::make_shared<TopoDS_Shape>(BRepPrimAPI_MakeSphere(radius).Shape());
}

std::shared_ptr<void> brepMakeCone(double radius1, double radius2, double height)
{
  return std::make_shared<TopoDS_Shape>(BRepPrimAPI_MakeCone(radius1, radius2, height).Shape());
}

std::shared_ptr<void> brepMakePrism(const std::vector<std::array<double, 2>>& outline, double height)
{
  BRepBuilderAPI_MakePolygon polygon;
  for (const auto& point : outline) polygon.Add(gp_Pnt(point[0], point[1], 0));
  polygon.Close();
  if (!polygon.IsDone()) throw std::runtime_error("B-Rep extrusion profile is invalid");
  BRepBuilderAPI_MakeFace face(polygon.Wire(), true);
  if (!face.IsDone()) throw std::runtime_error("B-Rep extrusion face construction failed");
  BRepPrimAPI_MakePrism prism(face.Face(), gp_Vec(0, 0, height));
  if (!prism.IsDone()) throw std::runtime_error("B-Rep prism construction failed");
  auto solid = TopoDS::Solid(prism.Shape());
  if (!BRepLib::OrientClosedSolid(solid) || !BRepCheck_Analyzer(solid).IsValid())
    throw std::runtime_error("B-Rep extrusion does not form a valid solid");
  return std::make_shared<TopoDS_Shape>(solid);
}

std::shared_ptr<void> brepRevolve(const std::shared_ptr<void>& profile, double angle, double start,
                                  int segments)
{
  if (brepIsEmpty(profile) || angle == 0.0) return {};
  if (!std::isfinite(angle) || !std::isfinite(start) || std::abs(angle) > 2 * M_PI)
    throw std::invalid_argument("B-Rep revolution angle is invalid");
  try {
    Bnd_Box bounds;
    BRepBndLib::AddOptimal(shapeFrom(profile), bounds, false, false);
    if (bounds.CornerMin().X() < -Precision::Confusion() &&
        bounds.CornerMax().X() > Precision::Confusion())
      throw std::invalid_argument("rotate_extrude profile crosses the Y axis");

    gp_Trsf toXZ, rotation;
    toXZ.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(1, 0, 0)), M_PI / 2);
    rotation.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), start);
    const gp_Ax1 axis(gp_Pnt(0, 0, 0), gp_Dir(0, 0, angle > 0 ? 1 : -1));
    std::vector<std::shared_ptr<void>> solids;
    for (TopExp_Explorer faces(shapeFrom(profile), TopAbs_FACE); faces.More(); faces.Next()) {
      const auto face = TopoDS::Face(faces.Current());
      const BRepAdaptor_Surface surface(face);
      if (surface.GetType() != GeomAbs_Plane) continue;
      const auto plane = surface.Plane();
      if (std::abs(plane.Axis().Direction().Z()) < 1.0 - Precision::Angular() ||
          std::abs(plane.Location().Z()) > Precision::Confusion())
        continue;
      if (segments > 0) {
        for (int segment = 0; segment < segments; ++segment) {
          gp_Trsf first, last;
          const gp_Ax1 zAxis(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1));
          first.SetRotation(zAxis, start + angle * segment / segments);
          last.SetRotation(zAxis, start + angle * (segment + 1) / segments);
          solids.push_back(
            ruledSolid(TopoDS::Face(BRepBuilderAPI_Transform(face, first * toXZ, true).Shape()),
                       TopoDS::Face(BRepBuilderAPI_Transform(face, last * toXZ, true).Shape())));
        }
        continue;
      }
      const auto base = BRepBuilderAPI_Transform(face, rotation * toXZ, true).Shape();
      BRepPrimAPI_MakeRevol revolution(base, axis, std::abs(angle), true);
      if (!revolution.IsDone()) throw std::runtime_error("B-Rep revolution construction failed");
      auto solid = TopoDS::Solid(revolution.Shape());
      if (!BRepLib::OrientClosedSolid(solid) || !BRepCheck_Analyzer(solid).IsValid())
        throw std::runtime_error("B-Rep revolution does not form a valid solid");
      solids.push_back(std::make_shared<TopoDS_Shape>(solid));
    }
    if (solids.empty()) throw std::runtime_error("B-Rep revolution has no planar profile faces");
    auto result = brepBoolean(solids, BrepOperation::Union, 0.0).shape;
    if (segments > 0 && !brepIsEmpty(result)) {
      ShapeUpgrade_UnifySameDomain unify(shapeFrom(result), true, true, false);
      unify.Build();
      return std::make_shared<TopoDS_Shape>(unify.Shape());
    }
    return result;
  } catch (const Standard_Failure& error) {
    throw std::runtime_error(error.GetMessageString());
  }
}

std::shared_ptr<void> brepTaper(const std::shared_ptr<void>& profile, double height, double scaleX,
                                double scaleY)
{
  if (brepIsEmpty(profile)) return {};
  const bool apex = scaleX == 0.0 && scaleY == 0.0;
  if (!std::isfinite(height) || !std::isfinite(scaleX) || !std::isfinite(scaleY) || height <= 0 ||
      (!apex && (scaleX <= 0 || scaleY <= 0)))
    throw std::invalid_argument("B-Rep taper requires finite positive scales or a zero-scale apex");
  try {
    std::vector<std::shared_ptr<void>> solids;
    for (TopExp_Explorer faces(shapeFrom(profile), TopAbs_FACE); faces.More(); faces.Next()) {
      const auto face = TopoDS::Face(faces.Current());
      const BRepAdaptor_Surface surface(face);
      if (surface.GetType() != GeomAbs_Plane) continue;
      const auto plane = surface.Plane();
      if (std::abs(plane.Axis().Direction().Z()) < 1.0 - Precision::Angular() ||
          std::abs(plane.Location().Z()) > Precision::Confusion())
        continue;
      TopoDS_Face top;
      if (apex) {
        const auto outer = BRepTools::OuterWire(face);
        const auto loft = [&](const TopoDS_Wire& wire) -> std::shared_ptr<void> {
          BRepOffsetAPI_ThruSections builder(true, true);
          builder.AddWire(wire);
          builder.AddVertex(BRepBuilderAPI_MakeVertex(gp_Pnt(0, 0, height)).Vertex());
          builder.Build();
          if (!builder.IsDone()) throw std::runtime_error("B-Rep apex construction failed");
          auto solid = TopoDS::Solid(builder.Shape());
          if (!BRepLib::OrientClosedSolid(solid) || !BRepCheck_Analyzer(solid).IsValid())
            throw std::runtime_error("B-Rep apex does not form a valid solid");
          return std::make_shared<TopoDS_Shape>(solid);
        };
        std::vector<std::shared_ptr<void>> contours{loft(outer)};
        for (TopExp_Explorer wires(face, TopAbs_WIRE); wires.More(); wires.Next()) {
          if (!wires.Current().IsSame(outer)) contours.push_back(loft(TopoDS::Wire(wires.Current())));
        }
        solids.push_back(brepBoolean(contours, BrepOperation::Difference, 0.0).shape);
        continue;
      }
      if (scaleX == scaleY) {
        gp_Trsf placement;
        placement.SetScale(gp_Pnt(0, 0, 0), scaleX);
        placement.SetTranslationPart(gp_Vec(0, 0, height));
        top = TopoDS::Face(BRepBuilderAPI_Transform(face, placement, true).Shape());
      } else {
        gp_GTrsf placement;
        placement.SetValue(1, 1, scaleX);
        placement.SetValue(2, 2, scaleY);
        placement.SetValue(3, 4, height);
        top = TopoDS::Face(BRepBuilderAPI_GTransform(face, placement, true).Shape());
      }
      solids.push_back(ruledSolid(face, top));
    }
    if (solids.empty()) throw std::runtime_error("B-Rep taper has no planar profile faces");
    return brepBoolean(solids, BrepOperation::Union, 0.0).shape;
  } catch (const Standard_Failure& error) {
    throw std::runtime_error(error.GetMessageString());
  }
}

std::shared_ptr<void> brepFromMesh(const BrepMeshData& mesh)
{
  BRepBuilderAPI_Sewing sewing(Precision::Confusion());
  for (const auto& triangle : mesh.triangles) {
    BRepBuilderAPI_MakePolygon polygon;
    for (const auto index : triangle) {
      const auto& vertex = mesh.vertices.at(index);
      polygon.Add(gp_Pnt(vertex[0], vertex[1], vertex[2]));
    }
    polygon.Close();
    if (!polygon.IsDone()) throw std::runtime_error("B-Rep mesh contains a degenerate triangle");
    BRepBuilderAPI_MakeFace face(polygon.Wire(), true);
    if (!face.IsDone()) throw std::runtime_error("B-Rep mesh face construction failed");
    sewing.Add(face.Face());
  }
  sewing.Perform();
  if (sewing.NbFreeEdges() || sewing.NbMultipleEdges())
    throw std::runtime_error("B-Rep mesh must be closed and manifold");
  TopExp_Explorer shells(sewing.SewedShape(), TopAbs_SHELL);
  if (!shells.More()) throw std::runtime_error("B-Rep mesh has no closed shell");
  TopoDS_Solid solid = BRepBuilderAPI_MakeSolid(TopoDS::Shell(shells.Current())).Solid();
  shells.Next();
  if (shells.More()) throw std::runtime_error("B-Rep mesh currently requires a single shell");
  if (!BRepLib::OrientClosedSolid(solid) || !BRepCheck_Analyzer(solid).IsValid())
    throw std::runtime_error("B-Rep mesh does not form a valid solid");
  // Merge coplanar triangles, without smoothing intentionally faceted surfaces.
  ShapeUpgrade_UnifySameDomain unify(solid, true, true, false);
  unify.Build();
  return std::make_shared<TopoDS_Shape>(unify.Shape());
}

size_t brepSurfaceCount(const std::shared_ptr<void>& shape, BrepSurfaceType type)
{
  size_t count = 0;
  for (TopExp_Explorer explorer(shapeFrom(shape), TopAbs_FACE); explorer.More(); explorer.Next()) {
    const GeomAbs_SurfaceType surfaceType =
      BRepAdaptor_Surface(TopoDS::Face(explorer.Current())).GetType();
    const bool matches = (type == BrepSurfaceType::Plane && surfaceType == GeomAbs_Plane) ||
                         (type == BrepSurfaceType::Cylinder && surfaceType == GeomAbs_Cylinder) ||
                         (type == BrepSurfaceType::Cone && surfaceType == GeomAbs_Cone) ||
                         (type == BrepSurfaceType::Sphere && surfaceType == GeomAbs_Sphere) ||
                         (type == BrepSurfaceType::Torus && surfaceType == GeomAbs_Torus) ||
                         (type == BrepSurfaceType::Bezier && surfaceType == GeomAbs_BezierSurface) ||
                         (type == BrepSurfaceType::BSpline && surfaceType == GeomAbs_BSplineSurface) ||
                         (type == BrepSurfaceType::Other && surfaceType == GeomAbs_OtherSurface);
    if (matches) ++count;
  }
  return count;
}

std::array<double, 6> brepBounds(const std::shared_ptr<void>& shape)
{
  Bnd_Box box;
  BRepBndLib::Add(shapeFrom(shape), box);
  std::array<double, 6> result;
  box.Get(result[0], result[1], result[2], result[3], result[4], result[5]);
  return result;
}

std::shared_ptr<void> brepTransform(const std::shared_ptr<void>& shape,
                                    const std::array<double, 12>& matrix)
{
  double squaredScale = 0.0;
  for (int row = 0; row < 3; ++row) squaredScale += matrix[row * 4] * matrix[row * 4];
  bool similarity = std::isfinite(squaredScale) && squaredScale > 0.0;
  for (int column = 0; column < 3; ++column) {
    for (int other = 0; other < 3; ++other) {
      double dot = 0.0;
      for (int row = 0; row < 3; ++row) dot += matrix[row * 4 + column] * matrix[row * 4 + other];
      similarity &= std::isfinite(dot) &&
                    std::abs(dot - (column == other ? squaredScale : 0.0)) <= 1e-12 * squaredScale;
    }
  }
  // gp_Trsf::SetValues orthogonalizes its input, silently losing a shear/nonuniform scale.
  if (similarity) {
    try {
      gp_Trsf transform;
      transform.SetValues(matrix[0], matrix[1], matrix[2], matrix[3], matrix[4], matrix[5], matrix[6],
                          matrix[7], matrix[8], matrix[9], matrix[10], matrix[11]);
      BRepBuilderAPI_Transform operation(shapeFrom(shape), transform, true);
      if (!operation.IsDone()) throw std::runtime_error("OpenCASCADE transform failed");
      return std::make_shared<TopoDS_Shape>(operation.Shape());
    } catch (const Standard_Failure&) {
    }
  }

  gp_GTrsf transform;
  for (int row = 0; row < 3; ++row) {
    for (int column = 0; column < 4; ++column) {
      transform.SetValue(row + 1, column + 1, matrix[row * 4 + column]);
    }
  }
  BRepBuilderAPI_GTransform operation(shapeFrom(shape), transform, true);
  if (!operation.IsDone()) throw std::runtime_error("OpenCASCADE transform failed");
  return std::make_shared<TopoDS_Shape>(operation.Shape());
}

BrepMeshData brepMesh(const std::shared_ptr<void>& shape, double linearDeflection,
                      double angularDeflection)
{
  BRepBuilderAPI_Copy copy(shapeFrom(shape), true, false);
  TopoDS_Shape meshedShape = copy.Shape();
  BRepMesh_IncrementalMesh mesher(meshedShape, linearDeflection, false, angularDeflection, true);
  if (!mesher.IsDone()) throw std::runtime_error("OpenCASCADE tessellation failed");

  BrepMeshData result;
  for (TopExp_Explorer explorer(meshedShape, TopAbs_FACE); explorer.More(); explorer.Next()) {
    const TopoDS_Face face = TopoDS::Face(explorer.Current());
    TopLoc_Location location;
    const Handle(Poly_Triangulation) triangulation = BRep_Tool::Triangulation(face, location);
    if (triangulation.IsNull()) continue;

    BRepLib_ToolTriangulatedShape::ComputeNormals(face, triangulation);

    const int vertexOffset = result.vertices.size();
    const gp_Trsf transform = location.Transformation();
    for (int nodeIndex = 1; nodeIndex <= triangulation->NbNodes(); ++nodeIndex) {
      const gp_Pnt point = triangulation->Node(nodeIndex).Transformed(transform);
      gp_Dir normal(triangulation->Normal(nodeIndex));
      normal.Transform(transform);
      if (face.Orientation() == TopAbs_REVERSED) normal.Reverse();
      result.vertices.push_back({point.X(), point.Y(), point.Z()});
      result.normals.push_back({normal.X(), normal.Y(), normal.Z()});
    }
    for (int triangleIndex = 1; triangleIndex <= triangulation->NbTriangles(); ++triangleIndex) {
      int first, second, third;
      triangulation->Triangle(triangleIndex).Get(first, second, third);
      if (face.Orientation() == TopAbs_REVERSED) std::swap(second, third);
      result.triangles.push_back(
        {vertexOffset + first - 1, vertexOffset + second - 1, vertexOffset + third - 1});
    }
    for (TopExp_Explorer edgeExplorer(face, TopAbs_EDGE); edgeExplorer.More(); edgeExplorer.Next()) {
      const TopoDS_Edge edge = TopoDS::Edge(edgeExplorer.Current());
      const Handle(Poly_PolygonOnTriangulation) polygon =
        BRep_Tool::PolygonOnTriangulation(edge, triangulation, location);
      if (polygon.IsNull()) continue;
      const TColStd_Array1OfInteger& nodes = polygon->Nodes();
      for (int index = nodes.Lower() + 1; index <= nodes.Upper(); ++index) {
        result.edges.push_back({vertexOffset + nodes(index - 1) - 1, vertexOffset + nodes(index) - 1});
      }
    }
  }
  return result;
}

BrepDifferenceData brepDifference(const std::shared_ptr<void>& object, const std::shared_ptr<void>& tool,
                                  double filletRadius)
{
  BrepBooleanResult result = applyBrepDifference({shapeFrom(object), shapeFrom(tool)}, filletRadius);
  return {std::make_shared<TopoDS_Shape>(std::move(result.shape)),
          {result.filletedEdgeCount, result.achievedFilletRadius, result.clearanceRadiusUpperBound}};
}

BrepDifferenceData brepBoolean(const std::vector<std::shared_ptr<void>>& operands,
                               BrepOperation operation, double filletRadius)
{
  std::vector<TopoDS_Shape> shapes;
  shapes.reserve(operands.size());
  for (const auto& operand : operands) shapes.push_back(shapeFrom(operand));
  const auto booleanOperation = operation == BrepOperation::Union ? BrepBooleanOperation::Union
                                : operation == BrepOperation::Difference
                                  ? BrepBooleanOperation::Difference
                                  : BrepBooleanOperation::Intersection;
  BrepBooleanResult result = applyBrepBoolean(shapes, booleanOperation, filletRadius);
  return {std::make_shared<TopoDS_Shape>(std::move(result.shape)),
          {result.filletedEdgeCount, result.achievedFilletRadius, result.clearanceRadiusUpperBound}};
}
