#pragma once

#include <array>
#include <memory>
#include <vector>

#include "geometry/brep/BrepGeometry.h"

struct BrepMeshData {
  std::vector<std::array<double, 3>> vertices;
  std::vector<std::array<int, 3>> triangles;
};

bool brepIsEmpty(const std::shared_ptr<void>& shape);
std::shared_ptr<void> brepMakeCylinder(double radius, double height);
size_t brepSurfaceCount(const std::shared_ptr<void>& shape, BrepSurfaceType type);
std::array<double, 6> brepBounds(const std::shared_ptr<void>& shape);
std::shared_ptr<void> brepTransform(const std::shared_ptr<void>& shape,
                                    const std::array<double, 12>& matrix);
BrepMeshData brepMesh(const std::shared_ptr<void>& shape, double linearDeflection,
                      double angularDeflection);
