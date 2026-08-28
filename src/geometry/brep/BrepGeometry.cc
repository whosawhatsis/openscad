#include "geometry/brep/BrepGeometry.h"

#include <array>
#include <memory>
#include <string>
#include <utility>

#include "geometry/PolySet.h"
#include "geometry/PolySetBuilder.h"
#include "geometry/brep/BrepGeometryData.h"

BrepGeometry::BrepGeometry(std::shared_ptr<void> shape) : shape_(std::move(shape))
{
}

BrepGeometry BrepGeometry::cube(double x, double y, double z)
{
  return BrepGeometry(brepMakeCube(x, y, z));
}

BrepGeometry BrepGeometry::cylinder(double radius, double height)
{
  return BrepGeometry(brepMakeCylinder(radius, height));
}

size_t BrepGeometry::memsize() const
{
  return sizeof(*this);
}

BoundingBox BrepGeometry::getBoundingBox() const
{
  BoundingBox result;
  if (isEmpty()) return result;
  const auto bounds = brepBounds(shape_);
  result.extend(Vector3d(bounds[0], bounds[1], bounds[2]));
  result.extend(Vector3d(bounds[3], bounds[4], bounds[5]));
  return result;
}

std::string BrepGeometry::dump() const
{
  return "BrepGeometry";
}

bool BrepGeometry::isEmpty() const
{
  return brepIsEmpty(shape_);
}

size_t BrepGeometry::surfaceCount(BrepSurfaceType type) const
{
  return brepSurfaceCount(shape_, type);
}

std::unique_ptr<Geometry> BrepGeometry::copy() const
{
  return std::make_unique<BrepGeometry>(shape_);
}

void BrepGeometry::transform(const Transform3d& matrix)
{
  std::array<double, 12> values;
  for (int row = 0; row < 3; ++row) {
    for (int column = 0; column < 4; ++column) values[row * 4 + column] = matrix(row, column);
  }
  shape_ = brepTransform(shape_, values);
}

BrepGeometry BrepGeometry::difference(const BrepGeometry& tool, double filletRadius,
                                      BrepFilletDiagnostics& diagnostics) const
{
  BrepDifferenceData result = brepDifference(shape_, tool.shape_, filletRadius);
  diagnostics = result.diagnostics;
  return BrepGeometry(std::move(result.shape));
}

std::unique_ptr<PolySet> BrepGeometry::toPolySet(double linearDeflection, double angularDeflection) const
{
  const BrepMeshData mesh = brepMesh(shape_, linearDeflection, angularDeflection);
  PolySetBuilder builder(mesh.vertices.size(), mesh.triangles.size());
  for (const auto& triangle : mesh.triangles) {
    builder.beginPolygon(3);
    for (const int index : triangle) {
      const auto& vertex = mesh.vertices[index];
      builder.addVertex(Vector3d(vertex[0], vertex[1], vertex[2]));
    }
    builder.endPolygon();
  }
  return builder.build();
}
