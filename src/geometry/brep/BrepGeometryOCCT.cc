#include "geometry/brep/BrepGeometryData.h"
#include "geometry/brep/BrepBoolean.h"

#include <array>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <utility>

#include <BRepBndLib.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRep_Builder.hxx>
#include <BRepLib.hxx>
#include <BRepBuilderAPI_GTransform.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepLib_ToolTriangulatedShape.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRepFill.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <BRepOffsetAPI_MakeOffset.hxx>
#include <BRepOffsetAPI_MakeOffsetShape.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Splitter.hxx>
#include <HLRBRep_Algo.hxx>
#include <HLRBRep_HLRToShape.hxx>
#include <HLRAlgo_Projector.hxx>
#include <optional>
#include <BRepTools.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <Poly_Triangulation.hxx>
#include <Poly_PolygonOnTriangulation.hxx>
#include <Precision.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <gp_GTrsf.hxx>
#include <gp_Pln.hxx>
#include <Geom_BSplineSurface.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_BezierCurve.hxx>
#include <Geom_OffsetCurve.hxx>
#include <math_Function.hxx>
#include <math_GaussSingleIntegration.hxx>
#include <TColStd_Array1OfReal.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <Geom_Line.hxx>
#include <GeomConvert.hxx>
#include <GeomAPI_ExtremaCurveCurve.hxx>
#include <TColgp_Array2OfPnt.hxx>
#include <TColStd_Array2OfReal.hxx>
#include <algorithm>
#include <map>
#include <set>

namespace {

const TopoDS_Shape& shapeFrom(const std::shared_ptr<void>& shape)
{
  return *std::static_pointer_cast<TopoDS_Shape>(shape);
}

// Both caps are transformed copies of one profile, with corresponding edge order.
std::shared_ptr<void> ruledSolid(const TopoDS_Face& first, const TopoDS_Face& last)
{
  BRepBuilderAPI_Sewing sewing(Precision::Confusion());
  sewing.Add(first.Reversed());
  sewing.Add(last);
  TopExp_Explorer aEdges(first, TopAbs_EDGE), bEdges(last, TopAbs_EDGE);
  for (; aEdges.More() && bEdges.More(); aEdges.Next(), bEdges.Next()) {
    const auto a = TopoDS::Edge(aEdges.Current()), b = TopoDS::Edge(bEdges.Current());
    const BRepAdaptor_Curve curveA(a), curveB(b);
    if (curveA.GetType() == GeomAbs_Line && curveB.GetType() == GeomAbs_Line) {
      std::vector<gp_Pnt> points;
      for (const auto& point :
           {curveA.Value(curveA.FirstParameter()), curveA.Value(curveA.LastParameter()),
            curveB.Value(curveB.LastParameter()), curveB.Value(curveB.FirstParameter())}) {
        if (points.empty() || point.Distance(points.back()) > Precision::Confusion())
          points.push_back(point);
      }
      if (points.size() > 1 && points.front().Distance(points.back()) <= Precision::Confusion())
        points.pop_back();
      if (points.size() < 3) continue;  // Fixed axis edges sweep no area.
      BRepBuilderAPI_MakePolygon boundary;
      for (const auto& point : points) boundary.Add(point);
      boundary.Close();
      BRepBuilderAPI_MakeFace planar(boundary.Wire(), true);
      if (planar.IsDone()) {
        sewing.Add(planar.Face());
        continue;
      }
    }
    sewing.Add(BRepFill::Face(a, b));
  }
  if (aEdges.More() || bEdges.More()) throw std::runtime_error("B-Rep sweep cap edges do not match");
  sewing.Perform();
  if (sewing.NbFreeEdges() || sewing.NbMultipleEdges() ||
      sewing.SewedShape().ShapeType() != TopAbs_SHELL)
    throw std::runtime_error("B-Rep ruled sweep is not a closed shell");
  auto solid = BRepBuilderAPI_MakeSolid(TopoDS::Shell(sewing.SewedShape())).Solid();
  if (!BRepLib::OrientClosedSolid(solid) || !BRepCheck_Analyzer(solid).IsValid())
    throw std::runtime_error("B-Rep ruled sweep does not form a valid solid");
  return std::make_shared<TopoDS_Shape>(solid);
}

}  // namespace

bool brepIsEmpty(const std::shared_ptr<void>& shape)
{
  return !shape || shapeFrom(shape).IsNull() || !TopExp_Explorer(shapeFrom(shape), TopAbs_FACE).More();
}

std::shared_ptr<void> brepMakeCube(double x, double y, double z)
{
  return std::make_shared<TopoDS_Shape>(BRepPrimAPI_MakeBox(x, y, z).Shape());
}

std::shared_ptr<void> brepMakeCylinder(double radius, double height)
{
  return std::make_shared<TopoDS_Shape>(BRepPrimAPI_MakeCylinder(radius, height).Shape());
}

std::shared_ptr<void> brepMakeSphere(double radius)
{
  return std::make_shared<TopoDS_Shape>(BRepPrimAPI_MakeSphere(radius).Shape());
}

std::shared_ptr<void> brepMakeCone(double radius1, double radius2, double height)
{
  return std::make_shared<TopoDS_Shape>(BRepPrimAPI_MakeCone(radius1, radius2, height).Shape());
}

std::shared_ptr<void> brepMakePrism(const std::vector<std::array<double, 2>>& outline, double height)
{
  BRepBuilderAPI_MakePolygon polygon;
  for (const auto& point : outline) polygon.Add(gp_Pnt(point[0], point[1], 0));
  polygon.Close();
  if (!polygon.IsDone()) throw std::runtime_error("B-Rep extrusion profile is invalid");
  BRepBuilderAPI_MakeFace face(polygon.Wire(), true);
  if (!face.IsDone()) throw std::runtime_error("B-Rep extrusion face construction failed");
  BRepPrimAPI_MakePrism prism(face.Face(), gp_Vec(0, 0, height));
  if (!prism.IsDone()) throw std::runtime_error("B-Rep prism construction failed");
  auto solid = TopoDS::Solid(prism.Shape());
  if (!BRepLib::OrientClosedSolid(solid) || !BRepCheck_Analyzer(solid).IsValid())
    throw std::runtime_error("B-Rep extrusion does not form a valid solid");
  return std::make_shared<TopoDS_Shape>(solid);
}

std::shared_ptr<void> brepRevolve(const std::shared_ptr<void>& profile, double angle, double start,
                                  int segments)
{
  if (brepIsEmpty(profile) || angle == 0.0) return {};
  if (!std::isfinite(angle) || !std::isfinite(start) || std::abs(angle) > 2 * M_PI)
    throw std::invalid_argument("B-Rep revolution angle is invalid");
  try {
    Bnd_Box bounds;
    BRepBndLib::AddOptimal(shapeFrom(profile), bounds, false, false);
    if (bounds.CornerMin().X() < -Precision::Confusion() &&
        bounds.CornerMax().X() > Precision::Confusion())
      throw std::invalid_argument("rotate_extrude profile crosses the Y axis");

    gp_Trsf toXZ, rotation;
    toXZ.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(1, 0, 0)), M_PI / 2);
    rotation.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), start);
    const gp_Ax1 axis(gp_Pnt(0, 0, 0), gp_Dir(0, 0, angle > 0 ? 1 : -1));
    std::vector<std::shared_ptr<void>> solids;
    for (TopExp_Explorer faces(shapeFrom(profile), TopAbs_FACE); faces.More(); faces.Next()) {
      const auto face = TopoDS::Face(faces.Current());
      const BRepAdaptor_Surface surface(face);
      if (surface.GetType() != GeomAbs_Plane) continue;
      const auto plane = surface.Plane();
      if (std::abs(plane.Axis().Direction().Z()) < 1.0 - Precision::Angular() ||
          std::abs(plane.Location().Z()) > Precision::Confusion())
        continue;
      if (segments > 0) {
        for (int segment = 0; segment < segments; ++segment) {
          gp_Trsf first, last;
          const gp_Ax1 zAxis(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1));
          first.SetRotation(zAxis, start + angle * segment / segments);
          last.SetRotation(zAxis, start + angle * (segment + 1) / segments);
          solids.push_back(
            ruledSolid(TopoDS::Face(BRepBuilderAPI_Transform(face, first * toXZ, true).Shape()),
                       TopoDS::Face(BRepBuilderAPI_Transform(face, last * toXZ, true).Shape())));
        }
        continue;
      }
      const auto base = BRepBuilderAPI_Transform(face, rotation * toXZ, true).Shape();
      BRepPrimAPI_MakeRevol revolution(base, axis, std::abs(angle), true);
      if (!revolution.IsDone()) throw std::runtime_error("B-Rep revolution construction failed");
      auto solid = TopoDS::Solid(revolution.Shape());
      if (!BRepLib::OrientClosedSolid(solid) || !BRepCheck_Analyzer(solid).IsValid())
        throw std::runtime_error("B-Rep revolution does not form a valid solid");
      solids.push_back(std::make_shared<TopoDS_Shape>(solid));
    }
    if (solids.empty()) throw std::runtime_error("B-Rep revolution has no planar profile faces");
    auto result = brepBoolean(solids, BrepOperation::Union, 0.0).shape;
    if (segments > 0 && !brepIsEmpty(result)) {
      ShapeUpgrade_UnifySameDomain unify(shapeFrom(result), true, true, false);
      unify.Build();
      return std::make_shared<TopoDS_Shape>(unify.Shape());
    }
    return result;
  } catch (const Standard_Failure& error) {
    throw std::runtime_error(error.GetMessageString());
  }
}

namespace {

// Map exact rational profile curves into a tensor-product surface. Cubic Hermite spans
// approximate rotation in height; zero twist is exactly linear, including singular end scales.
std::shared_ptr<void> deformedProfile(const TopoDS_Face& face, double height, double sx, double sy,
                                      double twist, unsigned int minimumSpans)
{
  const auto position = [&](const gp_Pnt& p, double t) {
    const double c = std::cos(twist * t), s = std::sin(twist * t);
    return gp_Pnt((1 + (sx - 1) * t) * (c * p.X() - s * p.Y()),
                  (1 + (sy - 1) * t) * (s * p.X() + c * p.Y()), height * t);
  };
  const auto tangent = [&](const gp_Pnt& p, double t) {
    const double c = std::cos(twist * t), s = std::sin(twist * t);
    const double x = c * p.X() - s * p.Y(), y = s * p.X() + c * p.Y();
    return gp_Vec((sx - 1) * x - (1 + (sx - 1) * t) * twist * y,
                  (sy - 1) * y + (1 + (sy - 1) * t) * twist * x, height);
  };
  // Hermite error <= max|f''''| / (384*n^4); tolerance is 1e-7 of the profile control-point radius.
  const double w = std::abs(twist);
  const double bound = std::pow(w, 4) * std::max({1.0, sx, sy}) +
                       4 * std::pow(w, 3) * std::max(std::abs(sx - 1), std::abs(sy - 1));
  const double required =
    std::max({1.0, static_cast<double>(minimumSpans), std::ceil(std::pow(bound / (384e-7), 0.25))});
  if (required > 4096) throw std::runtime_error("B-Rep twist exceeds the surface complexity limit");
  const int spans = static_cast<int>(required);
  TColStd_Array1OfReal vKnots(1, spans + 1);
  TColStd_Array1OfInteger vMults(1, spans + 1);
  for (int i = 0; i <= spans; ++i) {
    vKnots(i + 1) = static_cast<double>(i) / spans;
    vMults(i + 1) = i == 0 || i == spans ? 4 : 3;
  }
  BRepBuilderAPI_Sewing sewing(Precision::Confusion());
  sewing.Add(face);
  if (sx > 0 && sy > 0) {
    gp_GTrsf top;
    top.SetValue(1, 1, sx * std::cos(twist));
    top.SetValue(1, 2, -sx * std::sin(twist));
    top.SetValue(2, 1, sy * std::sin(twist));
    top.SetValue(2, 2, sy * std::cos(twist));
    top.SetValue(3, 4, height);
    sewing.Add(BRepBuilderAPI_GTransform(face.Reversed(), top, true).Shape());
  }
  for (TopExp_Explorer edges(face, TopAbs_EDGE); edges.More(); edges.Next()) {
    double first, last;
    auto curve = BRep_Tool::Curve(TopoDS::Edge(edges.Current()), first, last);
    std::vector<double> cuts{first, last};
    if (((sx == 0) != (sy == 0)) &&
        BRepAdaptor_Curve(TopoDS::Edge(edges.Current())).GetType() != GeomAbs_Line) {
      // Split at extrema of the surviving coordinate so collapsed top edges cannot retrace.
      const gp_Dir axis = sx == 0 ? gp_Dir(std::cos(twist), -std::sin(twist), 0)
                                  : gp_Dir(std::sin(twist), std::cos(twist), 0);
      Handle(Geom_Curve) trimmed = new Geom_TrimmedCurve(curve, first, last);
      GeomAPI_ExtremaCurveCurve extrema(trimmed, new Geom_Line(gp_Ax1(gp_Pnt(0, 0, 0), axis)));
      for (int i = 1; !extrema.IsParallel() && i <= extrema.NbExtrema(); ++i) {
        double u, unused;
        extrema.Parameters(i, u, unused);
        if (u > first + Precision::PConfusion() && u < last - Precision::PConfusion()) cuts.push_back(u);
      }
    }
    std::sort(cuts.begin(), cuts.end());
    cuts.erase(
      std::unique(cuts.begin(), cuts.end(),
                  [](double a, double b) { return std::abs(a - b) <= Precision::PConfusion(); }),
      cuts.end());
    for (size_t part = 1; part < cuts.size(); ++part) {
      const auto section =
        GeomConvert::CurveToBSplineCurve(new Geom_TrimmedCurve(curve, cuts[part - 1], cuts[part]));
      TColgp_Array2OfPnt poles(1, section->NbPoles(), 1, 3 * spans + 1);
      TColStd_Array2OfReal weights(1, section->NbPoles(), 1, 3 * spans + 1);
      for (int u = 1; u <= section->NbPoles(); ++u) {
        const auto p = section->Pole(u);
        for (int v = 0; v < spans; ++v) {
          const double t0 = static_cast<double>(v) / spans, t1 = static_cast<double>(v + 1) / spans;
          poles(u, 3 * v + 1) = position(p, t0);
          poles(u, 3 * v + 2) = position(p, t0).Translated(tangent(p, t0) / (3 * spans));
          poles(u, 3 * v + 3) = position(p, t1).Translated(-tangent(p, t1) / (3 * spans));
          poles(u, 3 * v + 4) = position(p, t1);
        }
        for (int v = 1; v <= 3 * spans + 1; ++v) weights(u, v) = section->Weight(u);
      }
      Handle(Geom_BSplineSurface) surface =
        new Geom_BSplineSurface(poles, weights, section->Knots(), vKnots, section->Multiplicities(),
                                vMults, section->Degree(), 3);
      BRepBuilderAPI_MakeFace side(surface, Precision::Confusion());
      if (!side.IsDone()) throw std::runtime_error("B-Rep deformation face construction failed");
      sewing.Add(side.Face());
    }
  }
  sewing.Perform();
  if (sewing.NbFreeEdges() || sewing.NbMultipleEdges() ||
      sewing.SewedShape().ShapeType() != TopAbs_SHELL)
    throw std::runtime_error("B-Rep deformed extrusion is not a closed shell");
  auto solid = BRepBuilderAPI_MakeSolid(TopoDS::Shell(sewing.SewedShape())).Solid();
  if (!BRepLib::OrientClosedSolid(solid) || !BRepCheck_Analyzer(solid).IsValid())
    throw std::runtime_error("B-Rep deformed extrusion does not form a valid solid");
  return std::make_shared<TopoDS_Shape>(solid);
}

}  // namespace

std::shared_ptr<void> brepTaper(const std::shared_ptr<void>& profile, double height, double scaleX,
                                double scaleY, double twist, unsigned int minimumSpans)
{
  if (brepIsEmpty(profile)) return {};
  const bool apex = scaleX == 0.0 && scaleY == 0.0;
  if (!std::isfinite(height) || !std::isfinite(scaleX) || !std::isfinite(scaleY) ||
      !std::isfinite(twist) || height <= 0 || scaleX < 0 || scaleY < 0)
    throw std::invalid_argument(
      "B-Rep extrusion requires finite twist, positive height and nonnegative scales");
  try {
    std::vector<std::shared_ptr<void>> solids;
    for (TopExp_Explorer faces(shapeFrom(profile), TopAbs_FACE); faces.More(); faces.Next()) {
      const auto face = TopoDS::Face(faces.Current());
      const BRepAdaptor_Surface surface(face);
      if (surface.GetType() != GeomAbs_Plane) continue;
      const auto plane = surface.Plane();
      if (std::abs(plane.Axis().Direction().Z()) < 1.0 - Precision::Angular() ||
          std::abs(plane.Location().Z()) > Precision::Confusion())
        continue;
      TopoDS_Face top;
      if (twist != 0 || ((scaleX == 0) != (scaleY == 0))) {
        solids.push_back(deformedProfile(face, height, scaleX, scaleY, twist, minimumSpans));
        continue;
      }
      if (apex) {
        const auto outer = BRepTools::OuterWire(face);
        const auto loft = [&](const TopoDS_Wire& wire) -> std::shared_ptr<void> {
          BRepOffsetAPI_ThruSections builder(true, true);
          builder.AddWire(wire);
          builder.AddVertex(BRepBuilderAPI_MakeVertex(gp_Pnt(0, 0, height)).Vertex());
          builder.Build();
          if (!builder.IsDone()) throw std::runtime_error("B-Rep apex construction failed");
          auto solid = TopoDS::Solid(builder.Shape());
          if (!BRepLib::OrientClosedSolid(solid) || !BRepCheck_Analyzer(solid).IsValid())
            throw std::runtime_error("B-Rep apex does not form a valid solid");
          return std::make_shared<TopoDS_Shape>(solid);
        };
        std::vector<std::shared_ptr<void>> contours{loft(outer)};
        for (TopExp_Explorer wires(face, TopAbs_WIRE); wires.More(); wires.Next()) {
          if (!wires.Current().IsSame(outer)) contours.push_back(loft(TopoDS::Wire(wires.Current())));
        }
        solids.push_back(brepBoolean(contours, BrepOperation::Difference, 0.0).shape);
        continue;
      }
      if (scaleX == scaleY) {
        gp_Trsf placement;
        placement.SetScale(gp_Pnt(0, 0, 0), scaleX);
        placement.SetTranslationPart(gp_Vec(0, 0, height));
        top = TopoDS::Face(BRepBuilderAPI_Transform(face, placement, true).Shape());
      } else {
        gp_GTrsf placement;
        placement.SetValue(1, 1, scaleX);
        placement.SetValue(2, 2, scaleY);
        placement.SetValue(3, 4, height);
        top = TopoDS::Face(BRepBuilderAPI_GTransform(face, placement, true).Shape());
      }
      solids.push_back(ruledSolid(face, top));
    }
    if (solids.empty()) throw std::runtime_error("B-Rep taper has no planar profile faces");
    return brepBoolean(solids, BrepOperation::Union, 0.0).shape;
  } catch (const Standard_Failure& error) {
    throw std::runtime_error(error.GetMessageString());
  }
}

namespace {

std::vector<gp_Pnt> polyhedralVertices(const TopoDS_Shape& shape, bool requireConvex)
{
  std::set<std::array<double, 3>> unique;
  for (TopExp_Explorer vertices(shape, TopAbs_VERTEX); vertices.More(); vertices.Next()) {
    const auto p = BRep_Tool::Pnt(TopoDS::Vertex(vertices.Current()));
    unique.insert({p.X(), p.Y(), p.Z()});
  }
  std::vector<gp_Pnt> points;
  for (const auto& p : unique) points.emplace_back(p[0], p[1], p[2]);
  for (TopExp_Explorer faces(shape, TopAbs_FACE); faces.More(); faces.Next()) {
    const auto face = TopoDS::Face(faces.Current());
    const BRepAdaptor_Surface surface(face);
    if (surface.GetType() != GeomAbs_Plane)
      throw std::runtime_error("B-Rep hull/general Minkowski currently requires planar faces");
    if (requireConvex) {
      const auto plane = surface.Plane();
      gp_Vec normal(plane.Axis().Direction());
      if (face.Orientation() == TopAbs_REVERSED) normal.Reverse();
      for (const auto& p : points) {
        if (gp_Vec(plane.Location(), p).Dot(normal) > Precision::Confusion())
          throw std::runtime_error("B-Rep general Minkowski currently requires convex operands");
      }
    }
  }
  for (TopExp_Explorer edges(shape, TopAbs_EDGE); edges.More(); edges.Next()) {
    if (BRepAdaptor_Curve(TopoDS::Edge(edges.Current())).GetType() != GeomAbs_Line)
      throw std::runtime_error("B-Rep polyhedral hull requires straight edges");
  }
  return points;
}

std::vector<std::shared_ptr<void>> convexParts(const std::shared_ptr<void>& shape)
{
  polyhedralVertices(shapeFrom(shape), false);
  try {
    polyhedralVertices(shapeFrom(shape), true);
    return {shape};
  } catch (const std::runtime_error&) {
    // Split by the supporting planes of every face: each occupied arrangement cell
    // is convex. This preserves cavities and disconnected components without meshing.
  }
  TopTools_ListOfShape arguments, planes;
  arguments.Append(shapeFrom(shape));
  for (TopExp_Explorer faces(shapeFrom(shape), TopAbs_FACE); faces.More(); faces.Next()) {
    if (planes.Size() >= 256)
      throw std::runtime_error("B-Rep Minkowski decomposition plane limit exceeded");
    planes.Append(
      BRepBuilderAPI_MakeFace(BRepAdaptor_Surface(TopoDS::Face(faces.Current())).Plane()).Face());
  }
  // ponytail: full plane arrangements can grow cubically; use selective reflex-face
  // splitting if the explicit size limits become restrictive in real models.
  BRepAlgoAPI_Splitter splitter;
  splitter.SetArguments(arguments);
  splitter.SetTools(planes);
  splitter.Build();
  if (!splitter.IsDone() || !BRepCheck_Analyzer(splitter.Shape()).IsValid())
    throw std::runtime_error("B-Rep Minkowski convex decomposition failed");
  std::vector<std::shared_ptr<void>> parts;
  for (TopExp_Explorer solids(splitter.Shape(), TopAbs_SOLID); solids.More(); solids.Next()) {
    if (parts.size() >= 4096)
      throw std::runtime_error("B-Rep Minkowski decomposition cell limit exceeded");
    polyhedralVertices(solids.Current(), true);
    parts.push_back(std::make_shared<TopoDS_Shape>(solids.Current()));
  }
  if (parts.empty()) throw std::runtime_error("B-Rep Minkowski decomposition produced no solids");
  return parts;
}

std::shared_ptr<void> pointHull(const std::vector<gp_Pnt>& points)
{
  if (points.size() < 4) return {};
  const double tolerance = Precision::Confusion();
  const auto farthest = [&](const auto& distance) {
    size_t best = 0;
    for (size_t i = 1; i < points.size(); ++i)
      if (distance(points[i]) > distance(points[best])) best = i;
    return best;
  };
  const size_t a = 0, b = farthest([&](const gp_Pnt& p) { return points[a].SquareDistance(p); });
  gp_Vec axis(points[a], points[b]);
  if (axis.Magnitude() <= tolerance) return {};
  axis.Normalize();
  const size_t c =
    farthest([&](const gp_Pnt& p) { return axis.Crossed(gp_Vec(points[a], p)).SquareMagnitude(); });
  auto normal = axis.Crossed(gp_Vec(points[a], points[c]));
  if (normal.Magnitude() <= tolerance) return {};
  normal.Normalize();
  const size_t d = farthest([&](const gp_Pnt& p) { return std::abs(normal.Dot(gp_Vec(points[a], p))); });
  if (std::abs(normal.Dot(gp_Vec(points[a], points[d]))) <= tolerance) return {};
  const gp_Pnt interior((points[a].XYZ() + points[b].XYZ() + points[c].XYZ() + points[d].XYZ()) / 4);
  std::vector<std::array<int, 3>> faces;
  const auto addFace = [&](int i, int j, int k) {
    if (gp_Vec(points[i], points[j])
          .Crossed(gp_Vec(points[i], points[k]))
          .Dot(gp_Vec(points[i], interior)) > 0)
      std::swap(j, k);
    faces.push_back({i, j, k});
  };
  addFace(a, b, c);
  addFace(a, c, d);
  addFace(a, d, b);
  addFace(b, d, c);
  // Incremental hull of authored vertices only: never sample an analytic surface.
  for (size_t i = 0; i < points.size(); ++i) {
    if (i == a || i == b || i == c || i == d) continue;
    std::map<std::pair<int, int>, bool> horizon;
    auto face = faces.begin();
    while (face != faces.end()) {
      const auto n = gp_Vec(points[(*face)[0]], points[(*face)[1]])
                       .Crossed(gp_Vec(points[(*face)[0]], points[(*face)[2]]));
      if (n.Dot(gp_Vec(points[(*face)[0]], points[i])) <= tolerance * n.Magnitude()) {
        ++face;
        continue;
      }
      for (int e = 0; e < 3; ++e) {
        const int u = (*face)[e], v = (*face)[(e + 1) % 3];
        if (!horizon.erase({v, u})) horizon[{u, v}] = true;
      }
      face = faces.erase(face);
    }
    for (const auto& edge : horizon) addFace(edge.first.first, edge.first.second, i);
  }
  BrepMeshData mesh;
  for (const auto& p : points) mesh.vertices.push_back({p.X(), p.Y(), p.Z()});
  mesh.triangles = std::move(faces);
  return brepFromMesh(mesh);
}

std::optional<gp_Sphere> fullSphere(const TopoDS_Shape& shape)
{
  TopExp_Explorer faces(shape, TopAbs_FACE);
  if (!faces.More()) return {};
  const BRepAdaptor_Surface surface(TopoDS::Face(faces.Current()));
  if (surface.GetType() != GeomAbs_Sphere) return {};
  const auto sphere = surface.Sphere();
  faces.Next();
  if (faces.More()) return {};
  return sphere;
}

// Recognize constant vertical sections without converting their curved boundaries to polygons.
std::optional<std::array<double, 6>> verticalPrism(const std::shared_ptr<void>& shape)
{
  auto bounds = brepBounds(shape);
  std::vector<double> caps;
  for (TopExp_Explorer faces(shapeFrom(shape), TopAbs_FACE); faces.More(); faces.Next()) {
    const BRepAdaptor_Surface surface(TopoDS::Face(faces.Current()));
    if (surface.GetType() == GeomAbs_Plane) {
      const auto plane = surface.Plane();
      const double z = std::abs(plane.Axis().Direction().Z());
      if (z < Precision::Angular()) continue;
      if (z < 1 - Precision::Angular()) return {};
      caps.push_back(plane.Location().Z());
    } else if (surface.GetType() != GeomAbs_Cylinder ||
               std::abs(surface.Cylinder().Axis().Direction().Z()) < 1 - Precision::Angular())
      return {};
  }
  if (caps.size() < 2) return {};
  const auto [bottom, top] = std::minmax_element(caps.begin(), caps.end());
  bounds[2] = *bottom;
  bounds[5] = *top;
  for (double z : caps)
    if (std::abs(z - bounds[2]) > Precision::Confusion() &&
        std::abs(z - bounds[5]) > Precision::Confusion())
      return {};
  return bounds;
}

std::optional<gp_Cylinder> fullCylinder(const std::shared_ptr<void>& shape)
{
  if (!verticalPrism(shape)) return {};
  std::optional<gp_Cylinder> cylinder;
  int planes = 0;
  for (TopExp_Explorer faces(shapeFrom(shape), TopAbs_FACE); faces.More(); faces.Next()) {
    const BRepAdaptor_Surface surface(TopoDS::Face(faces.Current()));
    if (surface.GetType() == GeomAbs_Cylinder) {
      if (cylinder) return {};
      cylinder = surface.Cylinder();
    } else ++planes;
  }
  return planes == 2 ? cylinder : std::nullopt;
}

}  // namespace

std::shared_ptr<void> brepHull(const std::vector<std::shared_ptr<void>>& operands)
{
  try {
    if (operands.size() == 2 && !brepIsEmpty(operands[0]) && !brepIsEmpty(operands[1])) {
      const auto first = fullSphere(shapeFrom(operands[0])), second = fullSphere(shapeFrom(operands[1]));
      if (first && second) {
        gp_Vec axis(first->Location(), second->Location());
        const double distance = axis.Magnitude(), r1 = first->Radius(), r2 = second->Radius();
        if (distance <= std::abs(r2 - r1) + Precision::Confusion()) return operands[r1 >= r2 ? 0 : 1];
        axis.Normalize();
        const double slope = (r2 - r1) / distance, radial = std::sqrt(1 - slope * slope);
        const gp_Ax2 placement(first->Location().Translated(axis * (-slope * r1)), gp_Dir(axis));
        const double height = distance * radial * radial;
        TopoDS_Shape envelope =
          r1 == r2 ? BRepPrimAPI_MakeCylinder(placement, r1, height).Shape()
                   : BRepPrimAPI_MakeCone(placement, r1 * radial, r2 * radial, height).Shape();
        return brepBoolean({operands[0], operands[1], std::make_shared<TopoDS_Shape>(envelope)},
                           BrepOperation::Union, 0)
          .shape;
      }
      const auto c1 = fullCylinder(operands[0]), c2 = fullCylinder(operands[1]);
      if (c1 && c2) {
        const auto b1 = *verticalPrism(operands[0]), b2 = *verticalPrism(operands[1]);
        if (std::abs(b1[2] - b2[2]) <= Precision::Confusion() &&
            std::abs(b1[5] - b2[5]) <= Precision::Confusion()) {
          const double dx = c2->Location().X() - c1->Location().X(),
                       dy = c2->Location().Y() - c1->Location().Y();
          const double distance = std::hypot(dx, dy), r1 = c1->Radius(), r2 = c2->Radius();
          if (distance <= std::abs(r2 - r1) + Precision::Confusion()) return operands[r1 >= r2 ? 0 : 1];
          const double k = (r1 - r2) / distance, t = std::sqrt(1 - k * k);
          const std::array<double, 2> n1{(k * dx - t * dy) / distance, (k * dy + t * dx) / distance};
          const std::array<double, 2> n2{(k * dx + t * dy) / distance, (k * dy - t * dx) / distance};
          auto bridge =
            brepMakePrism({{c1->Location().X() + r1 * n1[0], c1->Location().Y() + r1 * n1[1]},
                           {c2->Location().X() + r2 * n1[0], c2->Location().Y() + r2 * n1[1]},
                           {c2->Location().X() + r2 * n2[0], c2->Location().Y() + r2 * n2[1]},
                           {c1->Location().X() + r1 * n2[0], c1->Location().Y() + r1 * n2[1]}},
                          b1[5] - b1[2]);
          gp_Trsf placement;
          placement.SetTranslation(gp_Vec(0, 0, b1[2]));
          bridge = std::make_shared<TopoDS_Shape>(
            BRepBuilderAPI_Transform(shapeFrom(bridge), placement, true).Shape());
          return brepBoolean({operands[0], operands[1], bridge}, BrepOperation::Union, 0).shape;
        }
      }
    }
    std::vector<gp_Pnt> points;
    for (const auto& operand : operands) {
      if (brepIsEmpty(operand)) continue;
      const auto vertices = polyhedralVertices(shapeFrom(operand), false);
      points.insert(points.end(), vertices.begin(), vertices.end());
    }
    return pointHull(points);
  } catch (const Standard_Failure& error) {
    throw std::runtime_error(error.GetMessageString());
  }
}

std::shared_ptr<void> brepMinkowski(const std::vector<std::shared_ptr<void>>& operands)
{
  if (operands.empty()) return {};
  try {
    for (const auto& operand : operands)
      if (brepIsEmpty(operand)) return {};
    auto result = operands.front();
    for (size_t i = 1; i < operands.size(); ++i) {
      auto a = result, b = operands[i];
      auto cylinder = fullCylinder(b);
      if (!cylinder && fullCylinder(a)) {
        std::swap(a, b);
        cylinder = fullCylinder(b);
      }
      if (cylinder) {
        if (const auto bounds = verticalPrism(a)) {
          const auto cb = *verticalPrism(b);
          gp_Trsf down;
          down.SetTranslation(gp_Vec(0, 0, -(*bounds)[2]));
          auto profile =
            std::make_shared<TopoDS_Shape>(BRepBuilderAPI_Transform(shapeFrom(a), down, true).Shape());
          auto offset =
            brepOffset2d(profile, cylinder->Radius(), true, (*bounds)[5] - (*bounds)[2] + cb[5] - cb[2]);
          gp_Trsf up;
          up.SetTranslation(
            gp_Vec(cylinder->Location().X(), cylinder->Location().Y(), (*bounds)[2] + cb[2]));
          result = std::make_shared<TopoDS_Shape>(
            BRepBuilderAPI_Transform(shapeFrom(offset), up, true).Shape());
          continue;
        }
      }
      auto sphere = fullSphere(shapeFrom(b));
      if (!sphere && fullSphere(shapeFrom(a))) {
        std::swap(a, b);
        sphere = fullSphere(shapeFrom(b));
      }
      if (sphere) {
        if (const auto other = fullSphere(shapeFrom(a))) {
          result = std::make_shared<TopoDS_Shape>(
            BRepPrimAPI_MakeSphere(gp_Pnt(other->Location().XYZ() + sphere->Location().XYZ()),
                                   other->Radius() + sphere->Radius())
              .Shape());
          continue;
        }
        const auto parts = convexParts(a);
        if (parts.size() > 1) {
          std::vector<std::shared_ptr<void>> sums;
          for (const auto& part : parts) sums.push_back(brepMinkowski({part, b}));
          result = brepBoolean(sums, BrepOperation::Union, 0).shape;
          continue;
        }
        BRepOffsetAPI_MakeOffsetShape offset;
        offset.PerformByJoin(shapeFrom(a), sphere->Radius(), Precision::Confusion());
        if (!offset.IsDone() || !BRepCheck_Analyzer(offset.Shape()).IsValid())
          throw std::runtime_error("B-Rep spherical Minkowski offset failed");
        gp_Trsf translation;
        translation.SetTranslation(gp_Vec(sphere->Location().XYZ()));
        result = std::make_shared<TopoDS_Shape>(
          BRepBuilderAPI_Transform(offset.Shape(), translation, true).Shape());
      } else {
        const auto leftParts = convexParts(a), rightParts = convexParts(b);
        if (leftParts.size() > 1 || rightParts.size() > 1) {
          if (leftParts.size() > 4096 / rightParts.size())
            throw std::runtime_error("B-Rep Minkowski component-pair limit exceeded");
          std::vector<std::shared_ptr<void>> sums;
          for (const auto& left : leftParts)
            for (const auto& right : rightParts) sums.push_back(brepMinkowski({left, right}));
          result = brepBoolean(sums, BrepOperation::Union, 0).shape;
          continue;
        }
        const auto lhs = polyhedralVertices(shapeFrom(a), true),
                   rhs = polyhedralVertices(shapeFrom(b), true);
        if (!rhs.empty() && lhs.size() > 1000000 / rhs.size())
          throw std::runtime_error("B-Rep Minkowski vertex-pair limit exceeded");
        std::vector<gp_Pnt> sums;
        for (const auto& p : lhs)
          for (const auto& q : rhs) sums.emplace_back(p.XYZ() + q.XYZ());
        result = pointHull(sums);
      }
    }
    return result;
  } catch (const Standard_Failure& error) {
    throw std::runtime_error(error.GetMessageString());
  }
}

template <size_t N>
static std::shared_ptr<void> bezierPrismImpl(
  const std::vector<std::vector<std::vector<std::array<double, N>>>>& contours, double height,
  bool evenOdd = false)
{
  try {
    std::vector<std::pair<std::shared_ptr<void>, int>> regions;
    for (const auto& contour : contours) {
      if (contour.empty()) continue;
      BRepBuilderAPI_MakeWire wire;
      double signedArea = 0;
      for (const auto& curve : contour) {
        if (curve.size() < 2 || curve.size() > 4)
          throw std::invalid_argument("Invalid font curve degree");
        TColgp_Array1OfPnt poles(1, curve.size());
        for (size_t i = 0; i < curve.size(); ++i) poles(i + 1) = gp_Pnt(curve[i][0], curve[i][1], 0);
        if constexpr (N == 3) {
          TColStd_Array1OfReal weights(1, curve.size());
          for (size_t i = 0; i < curve.size(); ++i) {
            if (!std::isfinite(curve[i][2]) || curve[i][2] <= 0)
              throw std::invalid_argument("Invalid rational profile weight");
            weights(i + 1) = curve[i][2];
          }
          Handle(Geom_BezierCurve) rational = new Geom_BezierCurve(poles, weights);
          if (curve.size() == 2) wire.Add(BRepBuilderAPI_MakeEdge(poles(1), poles(2)).Edge());
          else wire.Add(BRepBuilderAPI_MakeEdge(rational).Edge());
          class AreaIntegrand : public math_Function
          {
          public:
            Handle(Geom_BezierCurve) curve;
            gp_Pnt origin;
            Standard_Boolean Value(Standard_Real t, Standard_Real& value) override
            {
              gp_Pnt p;
              gp_Vec d;
              curve->D1(t, p, d);
              value = (p.X() - origin.X()) * d.Y() - (p.Y() - origin.Y()) * d.X();
              return std::isfinite(value);
            }
          } integrand;
          integrand.curve = rational;
          integrand.origin = gp_Pnt(contour.front().front()[0], contour.front().front()[1], 0);
          // Numerical integration determines winding only; the retained geometry is exact.
          math_GaussSingleIntegration integral(integrand, 0, 1, 16, 1e-10);
          if (!integral.IsDone()) throw std::runtime_error("Rational profile winding failed");
          signedArea += integral.Value();
        } else {
          if (curve.size() == 2) wire.Add(BRepBuilderAPI_MakeEdge(poles(1), poles(2)).Edge());
          else wire.Add(BRepBuilderAPI_MakeEdge(new Geom_BezierCurve(poles)).Edge());
          // Three-point Gaussian integration is exact for the degree-five area integrand
          // of a cubic Bezier curve. Preserve the font's nonzero-winding fill rule.
          const auto evaluate = [](std::vector<std::array<double, 2>> p, double t) {
            for (size_t n = p.size(); n > 1; --n)
              for (size_t i = 0; i < n - 1; ++i)
                for (int c = 0; c < 2; ++c) p[i][c] = (1 - t) * p[i][c] + t * p[i + 1][c];
            return p.front();
          };
          std::vector<std::array<double, 2>> derivative;
          for (size_t i = 1; i < curve.size(); ++i)
            derivative.push_back({(curve.size() - 1) * (curve[i][0] - curve[i - 1][0]),
                                  (curve.size() - 1) * (curve[i][1] - curve[i - 1][1])});
          const double q = std::sqrt(15.0) / 10;
          for (const auto& sample :
               {std::pair{0.5 - q, 5.0 / 18}, std::pair{0.5, 4.0 / 9}, std::pair{0.5 + q, 5.0 / 18}}) {
            const auto p = evaluate(curve, sample.first), d = evaluate(derivative, sample.first);
            signedArea += sample.second * (p[0] * d[1] - p[1] * d[0]);
          }
        }
      }
      if (!wire.IsDone()) throw std::runtime_error("Font outline is disconnected");
      BRepBuilderAPI_MakeFace face(wire.Wire(), true);
      if (!face.IsDone()) throw std::runtime_error("Font outline face construction failed");
      auto solid = TopoDS::Solid(BRepPrimAPI_MakePrism(face.Face(), gp_Vec(0, 0, height)).Shape());
      if (!BRepLib::OrientClosedSolid(solid) || !BRepCheck_Analyzer(solid).IsValid())
        throw std::runtime_error("Font outline does not form a valid prism");
      std::shared_ptr<void> remaining = std::make_shared<TopoDS_Shape>(solid);
      const auto contourShape = remaining;
      const int sign = evenOdd || signedArea >= 0 ? 1 : -1;
      std::vector<std::pair<std::shared_ptr<void>, int>> next;
      for (const auto& [region, winding] : regions) {
        auto outside = brepBoolean({region, contourShape}, BrepOperation::Difference, 0).shape;
        if (!brepIsEmpty(outside)) next.emplace_back(outside, winding);
        const int nextWinding = evenOdd ? (winding + 1) % 2 : winding + sign;
        if (nextWinding != 0) {
          auto overlap = brepBoolean({region, contourShape}, BrepOperation::Intersection, 0).shape;
          if (!brepIsEmpty(overlap)) next.emplace_back(overlap, nextWinding);
        }
        remaining = brepBoolean({remaining, region}, BrepOperation::Difference, 0).shape;
      }
      if (!brepIsEmpty(remaining)) next.emplace_back(remaining, sign);
      regions = std::move(next);
    }
    std::vector<std::shared_ptr<void>> filled;
    for (const auto& region : regions) filled.push_back(region.first);
    return brepBoolean(filled, BrepOperation::Union, 0).shape;
  } catch (const Standard_Failure& error) {
    throw std::runtime_error(error.GetMessageString());
  }
}

std::shared_ptr<void> brepBezierPrism(
  const std::vector<std::vector<std::vector<std::array<double, 2>>>>& contours, double height)
{
  return bezierPrismImpl(contours, height);
}

std::shared_ptr<void> brepRationalPrism(
  const std::vector<std::vector<std::vector<std::array<double, 3>>>>& contours, double height,
  bool evenOdd)
{
  return bezierPrismImpl(contours, height, evenOdd);
}

std::shared_ptr<void> brepStrokePrism(
  const std::vector<std::vector<std::vector<std::array<double, 3>>>>& centerlines, double width,
  double height, int lineCap, int lineJoin)
{
  if (!std::isfinite(width) || width <= 0) throw std::invalid_argument("Invalid SVG stroke width");
  if (lineCap != 2 && lineCap != 4)
    throw std::invalid_argument("Native SVG strokes currently support butt and round caps");
  try {
    std::vector<std::shared_ptr<void>> strokes;
    for (const auto& centerline : centerlines) {
      if (centerline.size() > 1 && lineJoin != 2)
        throw std::invalid_argument("Native multi-segment SVG strokes currently require round joins");
      for (size_t segment = 0; segment < centerline.size(); ++segment) {
        const auto& curve = centerline[segment];
        if (curve.size() < 2 || curve.size() > 4)
          throw std::invalid_argument("Invalid SVG stroke curve degree");
        TColgp_Array1OfPnt poles(1, curve.size());
        TColStd_Array1OfReal weights(1, curve.size());
        for (size_t i = 0; i < curve.size(); ++i) {
          poles(i + 1) = gp_Pnt(curve[i][0], curve[i][1], 0);
          weights(i + 1) = curve[i][2];
        }
        std::shared_ptr<void> stroke;
        if (curve.size() == 2) {
          gp_Vec direction(poles(1), poles(2));
          if (direction.Magnitude() <= Precision::Confusion()) continue;
          direction.Normalize();
          const gp_Vec normal(-direction.Y() * width / 2, direction.X() * width / 2, 0);
          BRepBuilderAPI_MakePolygon boundary;
          boundary.Add(poles(1).Translated(normal));
          boundary.Add(poles(2).Translated(normal));
          boundary.Add(poles(2).Translated(-normal));
          boundary.Add(poles(1).Translated(-normal));
          boundary.Close();
          const auto face = BRepBuilderAPI_MakeFace(boundary.Wire(), true).Face();
          stroke = std::make_shared<TopoDS_Shape>(
            TopoDS::Solid(BRepPrimAPI_MakePrism(face, gp_Vec(0, 0, height)).Shape()));
        } else {
          Handle(Geom_Curve) basis = new Geom_BezierCurve(poles, weights);
          Handle(Geom_OffsetCurve) positive = new Geom_OffsetCurve(basis, width / 2, gp_Dir(0, 0, 1));
          Handle(Geom_OffsetCurve) negative = new Geom_OffsetCurve(basis, -width / 2, gp_Dir(0, 0, 1));
          const double firstParameter = basis->FirstParameter(), lastParameter = basis->LastParameter();
          const gp_Pnt positiveStart = positive->Value(firstParameter),
                       positiveEnd = positive->Value(lastParameter),
                       negativeStart = negative->Value(firstParameter),
                       negativeEnd = negative->Value(lastParameter);
          BRepBuilderAPI_MakeWire boundary;
          boundary.Add(BRepBuilderAPI_MakeEdge(positive).Edge());
          boundary.Add(BRepBuilderAPI_MakeEdge(positiveEnd, negativeEnd).Edge());
          boundary.Add(TopoDS::Edge(BRepBuilderAPI_MakeEdge(negative).Edge().Reversed()));
          boundary.Add(BRepBuilderAPI_MakeEdge(negativeStart, positiveStart).Edge());
          if (!boundary.IsDone()) throw std::runtime_error("SVG stroke boundary is disconnected");
          BRepBuilderAPI_MakeFace face(boundary.Wire(), true);
          if (!face.IsDone()) throw std::runtime_error("SVG stroke face construction failed");
          stroke = std::make_shared<TopoDS_Shape>(
            TopoDS::Solid(BRepPrimAPI_MakePrism(face.Face(), gp_Vec(0, 0, height)).Shape()));
        }
        const bool roundStart = lineCap == 4 || segment > 0;
        const bool roundEnd = lineCap == 4 || segment + 1 < centerline.size();
        std::vector<std::shared_ptr<void>> parts{stroke};
        if (roundStart)
          parts.push_back(std::make_shared<TopoDS_Shape>(
            BRepPrimAPI_MakeCylinder(gp_Ax2(poles(1), gp_Dir(0, 0, 1)), width / 2, height).Shape()));
        if (roundEnd)
          parts.push_back(std::make_shared<TopoDS_Shape>(
            BRepPrimAPI_MakeCylinder(gp_Ax2(poles(curve.size()), gp_Dir(0, 0, 1)), width / 2, height)
              .Shape()));
        auto joined = brepBoolean(parts, BrepOperation::Union, 0).shape;
        if (!BRepCheck_Analyzer(shapeFrom(joined)).IsValid())
          throw std::runtime_error("SVG stroke prism is invalid");
        strokes.push_back(joined);
      }
    }
    return brepBoolean(strokes, BrepOperation::Union, 0).shape;
  } catch (const Standard_Failure& error) {
    throw std::runtime_error(error.GetMessageString());
  }
}

std::shared_ptr<void> brepShadowProjection(const std::shared_ptr<void>& shape, double height)
{
  if (brepIsEmpty(shape)) return {};
  try {
    Handle(HLRBRep_Algo) hlr = new HLRBRep_Algo;
    hlr->Add(shapeFrom(shape));
    hlr->Projector(HLRAlgo_Projector(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0))));
    hlr->Update();
    hlr->Hide();
    HLRBRep_HLRToShape outlines(hlr);
    const auto bounds = brepBounds(shape);
    const auto plane = BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), bounds[0] - 1,
                                               bounds[3] + 1, bounds[1] - 1, bounds[4] + 1)
                         .Face();
    TopTools_ListOfShape tools;
    for (auto edges :
         {outlines.VCompound(), outlines.HCompound(), outlines.OutLineVCompound(),
          outlines.OutLineHCompound(), outlines.Rg1LineVCompound(), outlines.Rg1LineHCompound()}) {
      if (edges.IsNull()) continue;
      if (!BRepLib::BuildCurves3d(edges))
        throw std::runtime_error("B-Rep projected curve construction failed");
      for (TopExp_Explorer e(edges, TopAbs_EDGE); e.More(); e.Next()) {
        BRepLib::BuildPCurveForEdgeOnPlane(TopoDS::Edge(e.Current()), plane);
        tools.Append(e.Current());
      }
    }
    if (tools.IsEmpty()) return {};
    TopTools_ListOfShape arguments;
    arguments.Append(plane);
    BRepAlgoAPI_Splitter splitter;
    splitter.SetArguments(arguments);
    splitter.SetTools(tools);
    splitter.Build();
    if (!splitter.IsDone()) throw std::runtime_error("B-Rep silhouette arrangement failed");
    std::vector<std::shared_ptr<void>> regions;
    for (TopExp_Explorer faces(splitter.Shape(), TopAbs_FACE); faces.More(); faces.Next()) {
      // The silhouette/edge arrangement partitions the plane into constant-occupancy regions.
      // Classify each region by intersecting its vertical prism, without sampling or meshing.
      gp_Trsf translation;
      translation.SetTranslation(gp_Vec(0, 0, bounds[2] - 1));
      auto bottom = BRepBuilderAPI_Transform(faces.Current(), translation, true).Shape();
      auto probe = BRepPrimAPI_MakePrism(bottom, gp_Vec(0, 0, bounds[5] - bounds[2] + 2)).Shape();
      BRepAlgoAPI_Common occupied(shapeFrom(shape), probe);
      if (!occupied.IsDone()) throw std::runtime_error("B-Rep projection occupancy test failed");
      if (!TopExp_Explorer(occupied.Shape(), TopAbs_SOLID).More()) continue;
      auto solid = TopoDS::Solid(BRepPrimAPI_MakePrism(faces.Current(), gp_Vec(0, 0, height)).Shape());
      if (!BRepLib::OrientClosedSolid(solid) || !BRepCheck_Analyzer(solid).IsValid())
        throw std::runtime_error("B-Rep projected silhouette is invalid");
      regions.push_back(std::make_shared<TopoDS_Shape>(solid));
    }
    return brepBoolean(regions, BrepOperation::Union, 0).shape;
  } catch (const Standard_Failure& error) {
    throw std::runtime_error(error.GetMessageString());
  }
}

std::shared_ptr<void> brepCutProjection(const std::shared_ptr<void>& shape, double height)
{
  if (brepIsEmpty(shape)) return {};
  try {
    const auto bounds = brepBounds(shape);
    BRepBuilderAPI_MakeFace plane(gp_Pln(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), bounds[0] - 1, bounds[3] + 1,
                                  bounds[1] - 1, bounds[4] + 1);
    BRepAlgoAPI_Common section(shapeFrom(shape), plane.Face());
    if (!section.IsDone()) throw std::runtime_error("B-Rep cut projection failed");
    std::vector<std::shared_ptr<void>> regions;
    for (TopExp_Explorer faces(section.Shape(), TopAbs_FACE); faces.More(); faces.Next()) {
      BRepPrimAPI_MakePrism prism(faces.Current(), gp_Vec(0, 0, height));
      auto solid = TopoDS::Solid(prism.Shape());
      if (!BRepLib::OrientClosedSolid(solid) || !BRepCheck_Analyzer(solid).IsValid())
        throw std::runtime_error("B-Rep projected section is invalid");
      regions.push_back(std::make_shared<TopoDS_Shape>(solid));
    }
    return brepBoolean(regions, BrepOperation::Union, 0).shape;
  } catch (const Standard_Failure& error) {
    throw std::runtime_error(error.GetMessageString());
  }
}

namespace {

TopoDS_Wire squareOffsetJoins(const TopoDS_Wire& wire, const TopTools_IndexedMapOfShape& joins)
{
  BRepBuilderAPI_MakeWire result;
  for (BRepTools_WireExplorer edges(wire); edges.More(); edges.Next()) {
    const auto edge = edges.Current();
    const BRepAdaptor_Curve curve(edge);
    if (!joins.Contains(edge) || curve.GetType() != GeomAbs_Circle) {
      result.Add(edge);
      continue;
    }
    gp_Pnt start, end;
    gp_Vec firstTangent, lastTangent;
    curve.D1(curve.FirstParameter(), start, firstTangent);
    curve.D1(curve.LastParameter(), end, lastTangent);
    const auto middle = curve.Value((curve.FirstParameter() + curve.LastParameter()) / 2);
    const gp_Vec normal(curve.Circle().Location(), middle);
    // Square joins meet the tangent at the middle of the round join, not its chord.
    const auto intersect = [&](const gp_Pnt& p, const gp_Vec& tangent) {
      const double denominator = normal.Dot(tangent);
      if (std::abs(denominator) <= Precision::Confusion() * tangent.Magnitude())
        throw std::runtime_error("B-Rep chamfer tangents are degenerate");
      return p.Translated(tangent * (normal.Dot(gp_Vec(p, middle)) / denominator));
    };
    std::array<gp_Pnt, 4> points{start, intersect(start, firstTangent), intersect(end, lastTangent),
                                 end};
    if (edge.Orientation() == TopAbs_REVERSED) std::reverse(points.begin(), points.end());
    for (size_t i = 1; i < points.size(); ++i)
      if (points[i - 1].Distance(points[i]) > Precision::Confusion())
        result.Add(BRepBuilderAPI_MakeEdge(points[i - 1], points[i]).Edge());
  }
  if (!result.IsDone()) throw std::runtime_error("B-Rep chamfer wire construction failed");
  return result.Wire();
}

}  // namespace

std::shared_ptr<void> brepOffset2d(const std::shared_ptr<void>& shape, double delta, bool round,
                                   double height, bool chamfer)
{
  if (brepIsEmpty(shape) || delta == 0) return shape;
  if (!std::isfinite(delta)) throw std::invalid_argument("B-Rep offset must be finite");
  try {
    std::vector<std::shared_ptr<void>> regions;
    for (TopExp_Explorer faces(shapeFrom(shape), TopAbs_FACE); faces.More(); faces.Next()) {
      auto face = TopoDS::Face(faces.Current());
      const BRepAdaptor_Surface surface(face);
      if (surface.GetType() != GeomAbs_Plane ||
          std::abs(surface.Plane().Axis().Direction().Z()) < 1 - Precision::Angular() ||
          std::abs(surface.Plane().Location().Z()) > Precision::Confusion())
        continue;
      face.Orientation(TopAbs_FORWARD);
      BRepOffsetAPI_MakeOffset offset(face, (round || chamfer) ? GeomAbs_Arc : GeomAbs_Intersection);
      offset.Perform(delta);
      if (!offset.IsDone()) throw std::runtime_error("B-Rep offset construction failed");
      TopTools_IndexedMapOfShape joins;
      if (chamfer) {
        for (TopExp_Explorer vertices(face, TopAbs_VERTEX); vertices.More(); vertices.Next())
          for (const auto& generated : offset.Generated(vertices.Current()))
            for (TopExp_Explorer edges(generated, TopAbs_EDGE); edges.More(); edges.Next())
              joins.Add(edges.Current());
      }
      std::shared_ptr<void> region;
      for (TopExp_Explorer wires(offset.Shape(), TopAbs_WIRE); wires.More(); wires.Next()) {
        const auto wire = TopoDS::Wire(wires.Current());
        BRepBuilderAPI_MakeFace cap(chamfer ? squareOffsetJoins(wire, joins) : wire, true);
        if (!cap.IsDone()) throw std::runtime_error("B-Rep offset cap construction failed");
        auto solid = TopoDS::Solid(BRepPrimAPI_MakePrism(cap.Face(), gp_Vec(0, 0, height)).Shape());
        if (!BRepLib::OrientClosedSolid(solid) || !BRepCheck_Analyzer(solid).IsValid())
          throw std::runtime_error("B-Rep offset contour is invalid");
        auto contour = std::make_shared<TopoDS_Shape>(solid);
        if (!region) region = contour;
        else {
          // Even/odd contour nesting handles holes and islands without relying on wire order.
          const auto a = brepBoolean({region, contour}, BrepOperation::Difference, 0).shape;
          const auto b = brepBoolean({contour, region}, BrepOperation::Difference, 0).shape;
          region = brepBoolean({a, b}, BrepOperation::Union, 0).shape;
        }
      }
      regions.push_back(region);
    }
    return brepBoolean(regions, BrepOperation::Union, 0).shape;
  } catch (const Standard_Failure& error) {
    throw std::runtime_error(error.GetMessageString());
  }
}

std::shared_ptr<void> brepFromMesh(const BrepMeshData& mesh)
{
  BRepBuilderAPI_Sewing sewing(Precision::Confusion());
  for (const auto& triangle : mesh.triangles) {
    BRepBuilderAPI_MakePolygon polygon;
    for (const auto index : triangle) {
      const auto& vertex = mesh.vertices.at(index);
      polygon.Add(gp_Pnt(vertex[0], vertex[1], vertex[2]));
    }
    polygon.Close();
    if (!polygon.IsDone()) throw std::runtime_error("B-Rep mesh contains a degenerate triangle");
    BRepBuilderAPI_MakeFace face(polygon.Wire(), true);
    if (!face.IsDone()) throw std::runtime_error("B-Rep mesh face construction failed");
    sewing.Add(face.Face());
  }
  sewing.Perform();
  if (sewing.NbFreeEdges() || sewing.NbMultipleEdges())
    throw std::runtime_error("B-Rep mesh must be closed and manifold");
  std::vector<TopoDS_Solid> solids;
  std::vector<TopoDS_Shell> boundaries;
  for (TopExp_Explorer shells(sewing.SewedShape(), TopAbs_SHELL); shells.More(); shells.Next()) {
    auto solid = BRepBuilderAPI_MakeSolid(TopoDS::Shell(shells.Current())).Solid();
    if (!BRepLib::OrientClosedSolid(solid) || !BRepCheck_Analyzer(solid).IsValid())
      throw std::runtime_error("B-Rep mesh does not form a valid solid");
    solids.push_back(solid);
    boundaries.push_back(TopoDS::Shell(TopExp_Explorer(solid, TopAbs_SHELL).Current()));
  }
  if (solids.empty()) throw std::runtime_error("B-Rep mesh has no closed shell");
  // Boundary shells must be disjoint. Once this is checked, one boundary point suffices
  // to determine containment, independently of the input shell order or winding.
  std::vector<std::vector<bool>> inside(solids.size(), std::vector<bool>(solids.size()));
  std::vector<size_t> depth(solids.size());
  for (size_t i = 0; i < solids.size(); ++i) {
    const auto vertex = TopoDS::Vertex(TopExp_Explorer(boundaries[i], TopAbs_VERTEX).Current());
    for (size_t j = 0; j < solids.size(); ++j) {
      if (i == j) continue;
      if (i < j) {
        BRepExtrema_DistShapeShape distance(boundaries[i], boundaries[j]);
        if (!distance.IsDone() || distance.Value() <= Precision::Confusion())
          throw std::runtime_error("B-Rep mesh shells intersect or touch");
      }
      BRepClass3d_SolidClassifier classifier(solids[j], BRep_Tool::Pnt(vertex), Precision::Confusion());
      if (classifier.State() == TopAbs_UNKNOWN || classifier.State() == TopAbs_ON)
        throw std::runtime_error("B-Rep mesh shell containment is ambiguous");
      inside[i][j] = classifier.State() == TopAbs_IN;
      depth[i] += inside[i][j];
    }
  }
  BRep_Builder builder;
  TopoDS_Compound regions;
  builder.MakeCompound(regions);
  for (size_t i = 0; i < solids.size(); ++i) {
    if (depth[i] % 2) continue;
    BRepBuilderAPI_MakeSolid region(boundaries[i]);
    for (size_t j = 0; j < solids.size(); ++j) {
      if (inside[j][i] && depth[j] == depth[i] + 1) region.Add(TopoDS::Shell(boundaries[j].Reversed()));
    }
    if (!BRepCheck_Analyzer(region.Solid()).IsValid())
      throw std::runtime_error("B-Rep mesh cavity construction failed");
    builder.Add(regions, region.Solid());
  }
  // Merge coplanar triangles, without smoothing intentionally faceted surfaces.
  ShapeUpgrade_UnifySameDomain unify(regions, true, true, false);
  unify.Build();
  return std::make_shared<TopoDS_Shape>(unify.Shape());
}

size_t brepSurfaceCount(const std::shared_ptr<void>& shape, BrepSurfaceType type)
{
  size_t count = 0;
  for (TopExp_Explorer explorer(shapeFrom(shape), TopAbs_FACE); explorer.More(); explorer.Next()) {
    const GeomAbs_SurfaceType surfaceType =
      BRepAdaptor_Surface(TopoDS::Face(explorer.Current())).GetType();
    const bool matches =
      (type == BrepSurfaceType::Plane && surfaceType == GeomAbs_Plane) ||
      (type == BrepSurfaceType::Cylinder && surfaceType == GeomAbs_Cylinder) ||
      (type == BrepSurfaceType::Cone && surfaceType == GeomAbs_Cone) ||
      (type == BrepSurfaceType::Sphere && surfaceType == GeomAbs_Sphere) ||
      (type == BrepSurfaceType::Torus && surfaceType == GeomAbs_Torus) ||
      (type == BrepSurfaceType::Bezier && surfaceType == GeomAbs_BezierSurface) ||
      (type == BrepSurfaceType::BSpline && surfaceType == GeomAbs_BSplineSurface) ||
      (type == BrepSurfaceType::Other &&
       (surfaceType == GeomAbs_OtherSurface || surfaceType == GeomAbs_SurfaceOfExtrusion ||
        surfaceType == GeomAbs_SurfaceOfRevolution || surfaceType == GeomAbs_OffsetSurface));
    if (matches) ++count;
  }
  return count;
}

std::array<double, 6> brepBounds(const std::shared_ptr<void>& shape)
{
  Bnd_Box box;
  BRepBndLib::Add(shapeFrom(shape), box);
  std::array<double, 6> result;
  box.Get(result[0], result[1], result[2], result[3], result[4], result[5]);
  return result;
}

std::shared_ptr<void> brepTransform(const std::shared_ptr<void>& shape,
                                    const std::array<double, 12>& matrix)
{
  double squaredScale = 0.0;
  for (int row = 0; row < 3; ++row) squaredScale += matrix[row * 4] * matrix[row * 4];
  bool similarity = std::isfinite(squaredScale) && squaredScale > 0.0;
  for (int column = 0; column < 3; ++column) {
    for (int other = 0; other < 3; ++other) {
      double dot = 0.0;
      for (int row = 0; row < 3; ++row) dot += matrix[row * 4 + column] * matrix[row * 4 + other];
      similarity &= std::isfinite(dot) &&
                    std::abs(dot - (column == other ? squaredScale : 0.0)) <= 1e-12 * squaredScale;
    }
  }
  // gp_Trsf::SetValues orthogonalizes its input, silently losing a shear/nonuniform scale.
  if (similarity) {
    try {
      gp_Trsf transform;
      transform.SetValues(matrix[0], matrix[1], matrix[2], matrix[3], matrix[4], matrix[5], matrix[6],
                          matrix[7], matrix[8], matrix[9], matrix[10], matrix[11]);
      BRepBuilderAPI_Transform operation(shapeFrom(shape), transform, true);
      if (!operation.IsDone()) throw std::runtime_error("OpenCASCADE transform failed");
      return std::make_shared<TopoDS_Shape>(operation.Shape());
    } catch (const Standard_Failure&) {
    }
  }

  gp_GTrsf transform;
  for (int row = 0; row < 3; ++row) {
    for (int column = 0; column < 4; ++column) {
      transform.SetValue(row + 1, column + 1, matrix[row * 4 + column]);
    }
  }
  BRepBuilderAPI_GTransform operation(shapeFrom(shape), transform, true);
  if (!operation.IsDone()) throw std::runtime_error("OpenCASCADE transform failed");
  return std::make_shared<TopoDS_Shape>(operation.Shape());
}

BrepMeshData brepMesh(const std::shared_ptr<void>& shape, double linearDeflection,
                      double angularDeflection)
{
  BRepBuilderAPI_Copy copy(shapeFrom(shape), true, false);
  TopoDS_Shape meshedShape = copy.Shape();
  BRepMesh_IncrementalMesh mesher(meshedShape, linearDeflection, false, angularDeflection, true);
  if (!mesher.IsDone()) throw std::runtime_error("OpenCASCADE tessellation failed");

  BrepMeshData result;
  for (TopExp_Explorer explorer(meshedShape, TopAbs_FACE); explorer.More(); explorer.Next()) {
    const TopoDS_Face face = TopoDS::Face(explorer.Current());
    TopLoc_Location location;
    const Handle(Poly_Triangulation) triangulation = BRep_Tool::Triangulation(face, location);
    if (triangulation.IsNull()) continue;

    BRepLib_ToolTriangulatedShape::ComputeNormals(face, triangulation);

    const int vertexOffset = result.vertices.size();
    const gp_Trsf transform = location.Transformation();
    for (int nodeIndex = 1; nodeIndex <= triangulation->NbNodes(); ++nodeIndex) {
      const gp_Pnt point = triangulation->Node(nodeIndex).Transformed(transform);
      gp_Dir normal(triangulation->Normal(nodeIndex));
      normal.Transform(transform);
      if (face.Orientation() == TopAbs_REVERSED) normal.Reverse();
      result.vertices.push_back({point.X(), point.Y(), point.Z()});
      result.normals.push_back({normal.X(), normal.Y(), normal.Z()});
    }
    for (int triangleIndex = 1; triangleIndex <= triangulation->NbTriangles(); ++triangleIndex) {
      int first, second, third;
      triangulation->Triangle(triangleIndex).Get(first, second, third);
      if (face.Orientation() == TopAbs_REVERSED) std::swap(second, third);
      result.triangles.push_back(
        {vertexOffset + first - 1, vertexOffset + second - 1, vertexOffset + third - 1});
    }
    for (TopExp_Explorer edgeExplorer(face, TopAbs_EDGE); edgeExplorer.More(); edgeExplorer.Next()) {
      const TopoDS_Edge edge = TopoDS::Edge(edgeExplorer.Current());
      const Handle(Poly_PolygonOnTriangulation) polygon =
        BRep_Tool::PolygonOnTriangulation(edge, triangulation, location);
      if (polygon.IsNull()) continue;
      const TColStd_Array1OfInteger& nodes = polygon->Nodes();
      for (int index = nodes.Lower() + 1; index <= nodes.Upper(); ++index) {
        result.edges.push_back({vertexOffset + nodes(index - 1) - 1, vertexOffset + nodes(index) - 1});
      }
    }
  }
  return result;
}

BrepDifferenceData brepDifference(const std::shared_ptr<void>& object, const std::shared_ptr<void>& tool,
                                  double filletRadius)
{
  BrepBooleanResult result = applyBrepDifference({shapeFrom(object), shapeFrom(tool)}, filletRadius);
  return {std::make_shared<TopoDS_Shape>(std::move(result.shape)),
          {result.filletedEdgeCount, result.achievedFilletRadius, result.clearanceRadiusUpperBound}};
}

BrepDifferenceData brepBoolean(const std::vector<std::shared_ptr<void>>& operands,
                               BrepOperation operation, double filletRadius)
{
  std::vector<TopoDS_Shape> shapes;
  shapes.reserve(operands.size());
  for (const auto& operand : operands) shapes.push_back(shapeFrom(operand));
  const auto booleanOperation = operation == BrepOperation::Union ? BrepBooleanOperation::Union
                                : operation == BrepOperation::Difference
                                  ? BrepBooleanOperation::Difference
                                  : BrepBooleanOperation::Intersection;
  BrepBooleanResult result = applyBrepBoolean(shapes, booleanOperation, filletRadius);
  return {std::make_shared<TopoDS_Shape>(std::move(result.shape)),
          {result.filletedEdgeCount, result.achievedFilletRadius, result.clearanceRadiusUpperBound}};
}
