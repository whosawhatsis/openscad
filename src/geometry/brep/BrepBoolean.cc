#include "geometry/brep/BrepBoolean.h"

#include <iterator>
#include <stdexcept>

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <TopoDS.hxx>

BrepBooleanResult applyBrepDifference(std::initializer_list<TopoDS_Shape> operands, double filletRadius)
{
  if (operands.size() != 2)
    throw std::invalid_argument("B-Rep difference currently requires two operands");

  auto operand = operands.begin();
  BRepAlgoAPI_Cut difference(*operand, *std::next(operand));
  difference.Build();
  if (!difference.IsDone()) throw std::runtime_error("OpenCASCADE difference failed");

  const TopTools_ListOfShape& generatedEdges = difference.SectionEdges();
  if (filletRadius == 0.0 || generatedEdges.IsEmpty()) {
    return {difference.Shape(), 0};
  }

  BRepFilletAPI_MakeFillet fillet(difference.Shape());
  std::size_t filletedEdgeCount = 0;
  for (const TopoDS_Shape& shape : generatedEdges) {
    if (shape.ShapeType() == TopAbs_EDGE) {
      fillet.Add(filletRadius, TopoDS::Edge(shape));
      ++filletedEdgeCount;
    }
  }
  fillet.Build();
  if (!fillet.IsDone()) throw std::runtime_error("OpenCASCADE fillet failed");

  return {fillet.Shape(), filletedEdgeCount};
}
