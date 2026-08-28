#pragma once

#include <memory>

#include "geometry/Geometry.h"

class PolySet;

enum class BrepSurfaceType { Plane, Cylinder, Cone, Sphere, Torus, Bezier, BSpline, Other };

class BrepGeometry : public Geometry
{
public:
  VISITABLE_GEOMETRY();

  explicit BrepGeometry(std::shared_ptr<void> shape);
  static BrepGeometry cylinder(double radius, double height);

  [[nodiscard]] size_t memsize() const override;
  [[nodiscard]] BoundingBox getBoundingBox() const override;
  [[nodiscard]] std::string dump() const override;
  [[nodiscard]] unsigned int getDimension() const override { return 3; }
  [[nodiscard]] bool isEmpty() const override;
  [[nodiscard]] std::unique_ptr<Geometry> copy() const override;
  [[nodiscard]] size_t numFacets() const override { return 0; }
  [[nodiscard]] size_t surfaceCount(BrepSurfaceType type) const;

  void transform(const Transform3d& matrix) override;

  [[nodiscard]] std::unique_ptr<PolySet> toPolySet(double linearDeflection,
                                                   double angularDeflection) const;

  [[nodiscard]] const std::shared_ptr<void>& opaqueShape() const { return shape_; }

private:
  std::shared_ptr<void> shape_;
};
