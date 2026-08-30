#pragma once

#include <cstddef>
#include <initializer_list>
#include <vector>

#include <TopoDS_Shape.hxx>

struct BrepBooleanResult {
  TopoDS_Shape shape;
  std::size_t filletedEdgeCount;
  double achievedFilletRadius;
  double clearanceRadiusUpperBound;
};

enum class BrepBooleanOperation { Union, Difference, Intersection };

BrepBooleanResult applyBrepBoolean(const std::vector<TopoDS_Shape>& operands,
                                   BrepBooleanOperation operation, double filletRadius);
BrepBooleanResult applyBrepDifference(std::initializer_list<TopoDS_Shape> operands, double filletRadius);
