#include "geometry/brep/BrepBoolean.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Common.hxx>
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

BrepBooleanResult applyBrepBoolean(const std::vector<TopoDS_Shape>& operands,
                                   BrepBooleanOperation operation, double filletRadius)
{
  if (operands.empty()) throw std::invalid_argument("B-Rep boolean requires an operand");
  if (filletRadius < 0.0) throw std::invalid_argument("B-Rep fillet radius cannot be negative");

  TopoDS_Shape shape = operands.front();
  std::vector<TopoDS_Edge> generatedEdges;
  for (auto operand = std::next(operands.begin()); operand != operands.end(); ++operand) {
    std::unique_ptr<BRepAlgoAPI_BooleanOperation> boolean;
    if (operation == BrepBooleanOperation::Union)
      boolean = std::make_unique<BRepAlgoAPI_Fuse>(shape, *operand);
    else if (operation == BrepBooleanOperation::Difference)
      boolean = std::make_unique<BRepAlgoAPI_Cut>(shape, *operand);
    else boolean = std::make_unique<BRepAlgoAPI_Common>(shape, *operand);
    boolean->Build();
    if (!boolean->IsDone()) throw std::runtime_error("OpenCASCADE boolean failed");

    std::vector<TopoDS_Edge> candidates;
    for (const auto& edge : generatedEdges) {
      const auto& modified = boolean->Modified(edge);
      if (modified.IsEmpty() && !boolean->IsDeleted(edge)) candidates.push_back(edge);
      for (const auto& replacement : modified) {
        if (replacement.ShapeType() == TopAbs_EDGE) candidates.push_back(TopoDS::Edge(replacement));
      }
    }
    for (const auto& edge : boolean->SectionEdges()) {
      if (edge.ShapeType() == TopAbs_EDGE) candidates.push_back(TopoDS::Edge(edge));
    }
    shape = boolean->Shape();
    generatedEdges.clear();
    for (const auto& edge : candidates) {
      if (std::any_of(generatedEdges.begin(), generatedEdges.end(),
                      [&](const auto& existing) { return existing.IsSame(edge); }))
        continue;
      for (TopExp_Explorer explorer(shape, TopAbs_EDGE); explorer.More(); explorer.Next()) {
        if (edge.IsSame(explorer.Current())) {
          generatedEdges.push_back(edge);
          break;
        }
      }
    }
  }
  if (filletRadius == 0.0 || generatedEdges.empty()) return {shape, 0, 0.0, 0.0};

  if (auto result = tryFillet(shape, generatedEdges, filletRadius)) {
    return {*result, generatedEdges.size(), filletRadius, filletRadius};
  }

  const double measuredClearance = measureClearance(shape, generatedEdges);
  const double clearanceRadiusUpperBound =
    std::min(filletRadius, std::isfinite(measuredClearance) ? measuredClearance : filletRadius);
  double failedRadius = clearanceRadiusUpperBound;
  double achievedRadius = failedRadius;
  std::optional<TopoDS_Shape> achievedShape;
  if (failedRadius < filletRadius) {
    achievedRadius *= 0.99;
    achievedShape = tryFillet(shape, generatedEdges, achievedRadius);
  }
  for (int attempt = 0; attempt < 24 && !achievedShape; ++attempt) {
    failedRadius = achievedRadius;
    achievedRadius *= 0.5;
    achievedShape = tryFillet(shape, generatedEdges, achievedRadius);
  }
  if (!achievedShape) throw std::runtime_error("OpenCASCADE fillet failed at every positive radius");

  for (int attempt = 0; attempt < 8; ++attempt) {
    const double radius = (achievedRadius + failedRadius) * 0.5;
    if (auto filletedShape = tryFillet(shape, generatedEdges, radius)) {
      achievedRadius = radius;
      achievedShape = std::move(filletedShape);
    } else {
      failedRadius = radius;
    }
  }

  return {*achievedShape, generatedEdges.size(), achievedRadius, clearanceRadiusUpperBound};
}

BrepBooleanResult applyBrepDifference(std::initializer_list<TopoDS_Shape> operands, double filletRadius)
{
  return applyBrepBoolean({operands.begin(), operands.end()}, BrepBooleanOperation::Difference,
                          filletRadius);
}
