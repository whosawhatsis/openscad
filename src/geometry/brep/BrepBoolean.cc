#include "geometry/brep/BrepBoolean.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <optional>
#include <stdexcept>
#include <vector>

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>

namespace {

std::optional<TopoDS_Shape> tryFillet(const TopoDS_Shape& shape, const std::vector<TopoDS_Edge>& edges,
                                      double radius)
{
  try {
    BRepFilletAPI_MakeFillet fillet(shape);
    for (const auto& edge : edges) fillet.Add(radius, edge);
    fillet.Build();
    if (!fillet.IsDone()) return std::nullopt;
    TopoDS_Shape result = fillet.Shape();
    if (!BRepCheck_Analyzer(result).IsValid()) return std::nullopt;
    return result;
  } catch (const Standard_Failure&) {
    return std::nullopt;
  }
}

double measureClearance(const TopoDS_Shape& result, const std::vector<TopoDS_Edge>& filletEdges)
{
  double clearance = std::numeric_limits<double>::infinity();
  // Two simultaneously filleted contours can each consume at most half their separation.
  for (std::size_t first = 0; first < filletEdges.size(); ++first) {
    for (std::size_t second = first + 1; second < filletEdges.size(); ++second) {
      BRepExtrema_DistShapeShape distance(filletEdges[first], filletEdges[second]);
      if (distance.IsDone() && distance.Value() > Precision::Confusion()) {
        clearance = std::min(clearance, distance.Value() * 0.5);
      }
    }
  }
  for (const auto& filletEdge : filletEdges) {
    for (TopExp_Explorer explorer(result, TopAbs_EDGE); explorer.More(); explorer.Next()) {
      const TopoDS_Edge blocker = TopoDS::Edge(explorer.Current());
      if (filletEdge.IsSame(blocker)) continue;

      BRepExtrema_DistShapeShape distance(filletEdge, blocker);
      if (distance.IsDone() && distance.Value() > Precision::Confusion()) {
        clearance = std::min(clearance, distance.Value());
      }
    }
  }
  return clearance;
}

}  // namespace

BrepBooleanResult applyBrepDifference(std::initializer_list<TopoDS_Shape> operands, double filletRadius)
{
  if (operands.size() != 2)
    throw std::invalid_argument("B-Rep difference currently requires two operands");
  if (filletRadius < 0.0) throw std::invalid_argument("B-Rep fillet radius cannot be negative");

  auto operand = operands.begin();
  BRepAlgoAPI_Cut difference(*operand, *std::next(operand));
  difference.Build();
  if (!difference.IsDone()) throw std::runtime_error("OpenCASCADE difference failed");

  std::vector<TopoDS_Edge> generatedEdges;
  for (const TopoDS_Shape& shape : difference.SectionEdges()) {
    if (shape.ShapeType() == TopAbs_EDGE) {
      generatedEdges.push_back(TopoDS::Edge(shape));
    }
  }
  if (filletRadius == 0.0 || generatedEdges.empty()) return {difference.Shape(), 0, 0.0, 0.0};

  if (auto result = tryFillet(difference.Shape(), generatedEdges, filletRadius)) {
    return {*result, generatedEdges.size(), filletRadius, filletRadius};
  }

  const double measuredClearance = measureClearance(difference.Shape(), generatedEdges);
  const double clearanceRadiusUpperBound =
    std::min(filletRadius, std::isfinite(measuredClearance) ? measuredClearance : filletRadius);
  double failedRadius = clearanceRadiusUpperBound;
  double achievedRadius = failedRadius;
  std::optional<TopoDS_Shape> achievedShape;
  if (failedRadius < filletRadius) {
    achievedRadius *= 0.99;
    achievedShape = tryFillet(difference.Shape(), generatedEdges, achievedRadius);
  }
  for (int attempt = 0; attempt < 24 && !achievedShape; ++attempt) {
    failedRadius = achievedRadius;
    achievedRadius *= 0.5;
    achievedShape = tryFillet(difference.Shape(), generatedEdges, achievedRadius);
  }
  if (!achievedShape) throw std::runtime_error("OpenCASCADE fillet failed at every positive radius");

  for (int attempt = 0; attempt < 8; ++attempt) {
    const double radius = (achievedRadius + failedRadius) * 0.5;
    if (auto shape = tryFillet(difference.Shape(), generatedEdges, radius)) {
      achievedRadius = radius;
      achievedShape = std::move(shape);
    } else {
      failedRadius = radius;
    }
  }

  return {*achievedShape, generatedEdges.size(), achievedRadius, clearanceRadiusUpperBound};
}
