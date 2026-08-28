#pragma once

#include <cstddef>
#include <initializer_list>

#include <TopoDS_Shape.hxx>

struct BrepBooleanResult {
  TopoDS_Shape shape;
  std::size_t filletedEdgeCount;
};

BrepBooleanResult applyBrepDifference(std::initializer_list<TopoDS_Shape> operands, double filletRadius);
