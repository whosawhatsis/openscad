#include "geometry/brep/BrepGeometryData.h"
#include "geometry/brep/BrepBoolean.h"

#include <array>
#include <memory>
#include <stdexcept>
#include <utility>

#include <BRepBndLib.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_GTransform.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <Poly_Triangulation.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <gp_GTrsf.hxx>

namespace {

const TopoDS_Shape& shapeFrom(const std::shared_ptr<void>& shape)
{
  return *std::static_pointer_cast<TopoDS_Shape>(shape);
}

}  // namespace

bool brepIsEmpty(const std::shared_ptr<void>& shape)
{
  return !shape || shapeFrom(shape).IsNull();
}

std::shared_ptr<void> brepMakeCube(double x, double y, double z)
{
  return std::make_shared<TopoDS_Shape>(BRepPrimAPI_MakeBox(x, y, z).Shape());
}

std::shared_ptr<void> brepMakeCylinder(double radius, double height)
{
  return std::make_shared<TopoDS_Shape>(BRepPrimAPI_MakeCylinder(radius, height).Shape());
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
  try {
    gp_Trsf transform;
    transform.SetValues(matrix[0], matrix[1], matrix[2], matrix[3], matrix[4], matrix[5], matrix[6],
                        matrix[7], matrix[8], matrix[9], matrix[10], matrix[11]);
    BRepBuilderAPI_Transform operation(shapeFrom(shape), transform, true);
    if (!operation.IsDone()) throw std::runtime_error("OpenCASCADE transform failed");
    return std::make_shared<TopoDS_Shape>(operation.Shape());
  } catch (const Standard_Failure&) {
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

    const int vertexOffset = result.vertices.size();
    const gp_Trsf transform = location.Transformation();
    for (int nodeIndex = 1; nodeIndex <= triangulation->NbNodes(); ++nodeIndex) {
      const gp_Pnt point = triangulation->Node(nodeIndex).Transformed(transform);
      result.vertices.push_back({point.X(), point.Y(), point.Z()});
    }
    for (int triangleIndex = 1; triangleIndex <= triangulation->NbTriangles(); ++triangleIndex) {
      int first, second, third;
      triangulation->Triangle(triangleIndex).Get(first, second, third);
      if (face.Orientation() == TopAbs_REVERSED) std::swap(second, third);
      result.triangles.push_back(
        {vertexOffset + first - 1, vertexOffset + second - 1, vertexOffset + third - 1});
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
