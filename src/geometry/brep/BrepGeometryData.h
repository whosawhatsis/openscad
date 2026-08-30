#pragma once

#include <array>
#include <memory>
#include "geometry/brep/BrepGeometry.h"

struct BrepDifferenceData {
  std::shared_ptr<void> shape;
  BrepFilletDiagnostics diagnostics;
};

bool brepIsEmpty(const std::shared_ptr<void>& shape);
std::shared_ptr<void> brepMakeCube(double x, double y, double z);
std::shared_ptr<void> brepMakeCylinder(double radius, double height);
size_t brepSurfaceCount(const std::shared_ptr<void>& shape, BrepSurfaceType type);
std::array<double, 6> brepBounds(const std::shared_ptr<void>& shape);
std::shared_ptr<void> brepTransform(const std::shared_ptr<void>& shape,
                                    const std::array<double, 12>& matrix);
BrepMeshData brepMesh(const std::shared_ptr<void>& shape, double linearDeflection,
                      double angularDeflection);
BrepDifferenceData brepDifference(const std::shared_ptr<void>& object, const std::shared_ptr<void>& tool,
                                  double filletRadius);
