#pragma once

#include <array>
#include <memory>
#include <vector>

#include "geometry/Geometry.h"

class PolySet;

enum class BrepSurfaceType { Plane, Cylinder, Cone, Sphere, Torus, Bezier, BSpline, Other };
enum class BrepOperation { Union, Difference, Intersection };

struct BrepFilletDiagnostics {
  size_t filletedEdgeCount{0};
  double achievedRadius{0.0};
  double radiusUpperBound{0.0};
};

struct BrepMeshData {
  std::vector<std::array<double, 3>> vertices;
  std::vector<std::array<double, 3>> normals;
  std::vector<std::array<int, 3>> triangles;
  std::vector<std::array<int, 2>> edges;
};

class BrepGeometry : public Geometry
{
public:
  VISITABLE_GEOMETRY();

  explicit BrepGeometry(std::shared_ptr<void> shape);
  static BrepGeometry cube(double x, double y, double z);
  static BrepGeometry cylinder(double radius, double height);
  static BrepGeometry sphere(double radius);
  static BrepGeometry cone(double radius1, double radius2, double height);
  static BrepGeometry fromPolySet(const PolySet& mesh);
  static BrepGeometry prism(const std::vector<std::array<double, 2>>& outline, double height);
  // Map the z=0 profile faces to XZ, then revolve about Z; angles are in radians.
  // Zero segments means a smooth revolution; positive counts create chordal sweep segments.
  [[nodiscard]] BrepGeometry revolve(double angle, double start, int segments = 0) const;

  [[nodiscard]] size_t memsize() const override;
  [[nodiscard]] BoundingBox getBoundingBox() const override;
  [[nodiscard]] std::string dump() const override;
  [[nodiscard]] unsigned int getDimension() const override { return 3; }
  [[nodiscard]] bool isEmpty() const override;
  [[nodiscard]] std::unique_ptr<Geometry> copy() const override;
  [[nodiscard]] size_t numFacets() const override { return 0; }
  [[nodiscard]] size_t surfaceCount(BrepSurfaceType type) const;

  void transform(const Transform3d& matrix) override;
  [[nodiscard]] BrepGeometry difference(const BrepGeometry& tool, double filletRadius,
                                        BrepFilletDiagnostics& diagnostics) const;
  [[nodiscard]] static BrepGeometry boolean(const std::vector<BrepGeometry>& operands,
                                            BrepOperation operation, double filletRadius,
                                            BrepFilletDiagnostics& diagnostics);

  [[nodiscard]] std::unique_ptr<PolySet> toPolySet(double linearDeflection,
                                                   double angularDeflection) const;
  [[nodiscard]] BrepMeshData toDisplayMesh(double linearDeflection, double angularDeflection) const;

  [[nodiscard]] const std::shared_ptr<void>& opaqueShape() const { return shape_; }

private:
  std::shared_ptr<void> shape_;
};
