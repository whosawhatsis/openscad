#include "geometry/brep/BrepGeometry.h"

#include <array>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include "geometry/PolySet.h"
#include "geometry/PolySetBuilder.h"
#include "geometry/PolySetUtils.h"
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

BrepGeometry BrepGeometry::sphere(double radius)
{
  return BrepGeometry(brepMakeSphere(radius));
}

BrepGeometry BrepGeometry::cone(double radius1, double radius2, double height)
{
  return BrepGeometry(brepMakeCone(radius1, radius2, height));
}

BrepGeometry BrepGeometry::fromPolySet(const PolySet& mesh)
{
  if (mesh.isEmpty()) return BrepGeometry(nullptr);
  for (const auto& vertex : mesh.vertices) {
    if (!vertex.allFinite()) throw std::invalid_argument("B-Rep mesh vertices must be finite");
  }
  for (const auto& face : mesh.indices) {
    if (face.size() < 3) throw std::invalid_argument("B-Rep mesh faces require three vertices");
    for (const auto index : face) {
      if (index < 0 || static_cast<size_t>(index) >= mesh.vertices.size())
        throw std::invalid_argument("B-Rep mesh face index is out of range");
    }
  }
  const auto triangles = PolySetUtils::tessellate_faces(mesh);
  BrepMeshData data;
  for (const auto& vertex : triangles->vertices) {
    data.vertices.push_back({vertex.x(), vertex.y(), vertex.z()});
  }
  for (const auto& face : triangles->indices) {
    if (face.size() != 3) throw std::runtime_error("B-Rep mesh triangulation failed");
    data.triangles.push_back({face[0], face[1], face[2]});
  }
  return BrepGeometry(brepFromMesh(data));
}

BrepGeometry BrepGeometry::prism(const std::vector<std::array<double, 2>>& outline, double height)
{
  return BrepGeometry(brepMakePrism(outline, height));
}

BrepGeometry BrepGeometry::offset2d(double delta, bool round, double height, bool chamfer) const
{
  return BrepGeometry(brepOffset2d(shape_, delta, round, height, chamfer));
}

BrepGeometry BrepGeometry::bezierPrism(
  const std::vector<std::vector<std::vector<std::array<double, 2>>>>& contours, double height)
{
  return BrepGeometry(brepBezierPrism(contours, height));
}

BrepGeometry BrepGeometry::cutProjection(double height) const
{
  return BrepGeometry(brepCutProjection(shape_, height));
}

BrepGeometry BrepGeometry::rationalPrism(
  const std::vector<std::vector<std::vector<std::array<double, 3>>>>& contours, double height,
  bool evenOdd)
{
  return BrepGeometry(brepRationalPrism(contours, height, evenOdd));
}

BrepGeometry BrepGeometry::strokePrism(
  const std::vector<std::vector<std::vector<std::array<double, 3>>>>& centerlines, double width,
  double height, int lineCap, int lineJoin, double miterLimit)
{
  return BrepGeometry(brepStrokePrism(centerlines, width, height, lineCap, lineJoin, miterLimit));
}

BrepGeometry BrepGeometry::shadowProjection(double height) const
{
  return BrepGeometry(brepShadowProjection(shape_, height));
}

BrepGeometry BrepGeometry::hull(const std::vector<BrepGeometry>& operands, double fa, double fs,
                                bool *approximated)
{
  std::vector<std::shared_ptr<void>> shapes;
  for (const auto& operand : operands) shapes.push_back(operand.opaqueShape());
  return BrepGeometry(brepHull(shapes, fa, fs, approximated));
}

BrepGeometry BrepGeometry::minkowski(const std::vector<BrepGeometry>& operands)
{
  std::vector<std::shared_ptr<void>> shapes;
  for (const auto& operand : operands) shapes.push_back(operand.opaqueShape());
  return BrepGeometry(brepMinkowski(shapes));
}

BrepGeometry BrepGeometry::revolve(double angle, double start, int segments) const
{
  return BrepGeometry(brepRevolve(shape_, angle, start, segments));
}

size_t BrepGeometry::memsize() const
{
  return sizeof(*this) + brepMemsize(shape_);
}

BrepGeometry BrepGeometry::taper(double height, double scaleX, double scaleY, double twist,
                                 unsigned int minimumSpans) const
{
  return BrepGeometry(brepTaper(shape_, height, scaleX, scaleY, twist, minimumSpans));
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
  mesh_.reset();
}

BrepGeometry BrepGeometry::difference(const BrepGeometry& tool, double filletRadius,
                                      BrepFilletDiagnostics& diagnostics) const
{
  BrepDifferenceData result = brepDifference(shape_, tool.shape_, filletRadius);
  diagnostics = result.diagnostics;
  return BrepGeometry(std::move(result.shape));
}

BrepGeometry BrepGeometry::boolean(const std::vector<BrepGeometry>& operands, BrepOperation operation,
                                   double filletRadius, BrepFilletDiagnostics& diagnostics)
{
  diagnostics = {};
  std::vector<std::shared_ptr<void>> shapes;
  shapes.reserve(operands.size());
  for (const auto& operand : operands) {
    if (operand.isEmpty()) {
      if (operation == BrepOperation::Intersection ||
          (operation == BrepOperation::Difference && &operand == &operands.front()))
        return BrepGeometry(nullptr);
      continue;
    }
    shapes.push_back(operand.shape_);
  }
  if (shapes.empty()) return BrepGeometry(nullptr);
  if (shapes.size() == 1) return BrepGeometry(shapes.front());
  BrepDifferenceData result = brepBoolean(shapes, operation, filletRadius);
  diagnostics = result.diagnostics;
  return BrepGeometry(std::move(result.shape));
}

std::unique_ptr<PolySet> BrepGeometry::toPolySet(double linearDeflection, double angularDeflection) const
{
  const BrepMeshData mesh = toDisplayMesh(linearDeflection, angularDeflection);
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

BrepMeshData BrepGeometry::toDisplayMesh(double linearDeflection, double angularDeflection) const
{
  if (isEmpty()) return {};
  if (mesh_ && meshLinearDeflection_ == linearDeflection && meshAngularDeflection_ == angularDeflection)
    return *mesh_;
  auto mesh =
    std::make_shared<const BrepMeshData>(brepMesh(shape_, linearDeflection, angularDeflection));
  mesh_ = mesh;
  meshLinearDeflection_ = linearDeflection;
  meshAngularDeflection_ = angularDeflection;
  return *mesh;
}
