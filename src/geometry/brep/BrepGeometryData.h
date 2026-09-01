#pragma once

#include <array>
#include <memory>
#include "geometry/brep/BrepGeometry.h"

struct BrepDifferenceData {
  std::shared_ptr<void> shape;
  BrepFilletDiagnostics diagnostics;
};

bool brepIsEmpty(const std::shared_ptr<void>& shape);
std::shared_ptr<void> brepHull(const std::vector<std::shared_ptr<void>>& operands);
std::shared_ptr<void> brepMinkowski(const std::vector<std::shared_ptr<void>>& operands);
std::shared_ptr<void> brepBezierPrism(
  const std::vector<std::vector<std::vector<std::array<double, 2>>>>& contours, double height);
std::shared_ptr<void> brepRationalPrism(
  const std::vector<std::vector<std::vector<std::array<double, 3>>>>& contours, double height,
  bool evenOdd);
std::shared_ptr<void> brepStrokePrism(
  const std::vector<std::vector<std::vector<std::array<double, 3>>>>& centerlines, double width,
  double height, int lineCap, int lineJoin);
std::shared_ptr<void> brepOffset2d(const std::shared_ptr<void>& shape, double delta, bool round,
                                   double height, bool chamfer = false);
std::shared_ptr<void> brepCutProjection(const std::shared_ptr<void>& shape, double height);
std::shared_ptr<void> brepShadowProjection(const std::shared_ptr<void>& shape, double height);
std::shared_ptr<void> brepTaper(const std::shared_ptr<void>& profile, double height, double scaleX,
                                double scaleY, double twist, unsigned int minimumSpans);
std::shared_ptr<void> brepMakeCube(double x, double y, double z);
std::shared_ptr<void> brepMakeCylinder(double radius, double height);
std::shared_ptr<void> brepMakeSphere(double radius);
std::shared_ptr<void> brepMakeCone(double radius1, double radius2, double height);
std::shared_ptr<void> brepFromMesh(const BrepMeshData& mesh);
std::shared_ptr<void> brepMakePrism(const std::vector<std::array<double, 2>>& outline, double height);
std::shared_ptr<void> brepRevolve(const std::shared_ptr<void>& profile, double angle, double start,
                                  int segments);
size_t brepSurfaceCount(const std::shared_ptr<void>& shape, BrepSurfaceType type);
std::array<double, 6> brepBounds(const std::shared_ptr<void>& shape);
std::shared_ptr<void> brepTransform(const std::shared_ptr<void>& shape,
                                    const std::array<double, 12>& matrix);
BrepMeshData brepMesh(const std::shared_ptr<void>& shape, double linearDeflection,
                      double angularDeflection);
BrepDifferenceData brepDifference(const std::shared_ptr<void>& object, const std::shared_ptr<void>& tool,
                                  double filletRadius);
BrepDifferenceData brepBoolean(const std::vector<std::shared_ptr<void>>& operands,
                               BrepOperation operation, double filletRadius);
