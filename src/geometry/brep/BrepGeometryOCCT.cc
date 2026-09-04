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
#include <BRepClass_FaceClassifier.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRep_Builder.hxx>
#include <BRepLib.hxx>
#include <BRepBuilderAPI_GTransform.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepLib_ToolTriangulatedShape.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepGProp.hxx>
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
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Splitter.hxx>
#include <BOPAlgo_GlueEnum.hxx>
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
#include <STEPControl_Writer.hxx>
#include <STEPControl_Reader.hxx>
#include <IGESControl_Writer.hxx>
#include <IGESControl_Reader.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <gp_GTrsf.hxx>
#include <gp_Circ.hxx>
#include <gp_Pln.hxx>
#include <Geom_BSplineSurface.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_BezierCurve.hxx>
#include <Geom_OffsetCurve.hxx>
#include <math_Function.hxx>
#include <math_GaussSingleIntegration.hxx>
#include <math_DirectPolynomialRoots.hxx>
#include <GProp_GProps.hxx>
#include <TColStd_Array1OfReal.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <Geom_Line.hxx>
#include <GeomConvert.hxx>
#include <GeomAPI_ExtremaCurveCurve.hxx>
#include <TColgp_Array2OfPnt.hxx>
#include <TColStd_Array2OfReal.hxx>
#include <TopTools_ListOfShape.hxx>
#include <algorithm>
#include <map>
#include <set>

namespace {

const TopoDS_Shape& shapeFrom(const std::shared_ptr<void>& shape)
{
  static const TopoDS_Shape none;
  if (!shape) return none;
  return *std::static_pointer_cast<TopoDS_Shape>(shape);
}

// Standard_Failure derives from Standard_Transient, not std::exception, so an OCCT throw
// would unwind straight past every `catch (const std::exception&)` in the evaluator and the
// import/export paths. Every exported brep* entry point translates it here instead.
template <typename Body>
auto occtGuarded(Body body) -> decltype(body())
{
  try {
    return body();
  } catch (const Standard_Failure& error) {
    throw std::runtime_error(error.GetMessageString());
  }
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
  try {
    return std::make_shared<TopoDS_Shape>(BRepPrimAPI_MakeBox(x, y, z).Shape());
  } catch (const Standard_Failure& error) {
    throw std::runtime_error(error.GetMessageString());
  }
}

std::shared_ptr<void> brepMakeCylinder(double radius, double height)
{
  try {
    // Unlike the box/sphere/cone builders, MakeCylinder accepts degenerate dimensions and
    // returns a shape that only fails later, inside whatever boolean consumes it.
    if (radius <= 0.0 || height <= 0.0)
      throw std::runtime_error("B-Rep cylinder requires a positive radius and height");
    return std::make_shared<TopoDS_Shape>(BRepPrimAPI_MakeCylinder(radius, height).Shape());
  } catch (const Standard_Failure& error) {
    throw std::runtime_error(error.GetMessageString());
  }
}

std::shared_ptr<void> brepMakeSphere(double radius)
{
  try {
    return std::make_shared<TopoDS_Shape>(BRepPrimAPI_MakeSphere(radius).Shape());
  } catch (const Standard_Failure& error) {
    throw std::runtime_error(error.GetMessageString());
  }
}

std::shared_ptr<void> brepMakeCone(double radius1, double radius2, double height)
{
  try {
    return std::make_shared<TopoDS_Shape>(BRepPrimAPI_MakeCone(radius1, radius2, height).Shape());
  } catch (const Standard_Failure& error) {
    throw std::runtime_error(error.GetMessageString());
  }
}

std::shared_ptr<void> brepMakePrism(const std::vector<std::array<double, 2>>& outline, double height)
{
  try {
    if (outline.size() < 3) throw std::runtime_error("B-Rep extrusion profile needs three points");
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
  } catch (const Standard_Failure& error) {
    throw std::runtime_error(error.GetMessageString());
  }
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
  // Only now, once the shape is known to qualify, is the optimal-bounds cost worth paying.
  auto bounds = brepBounds(shape);
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

// A vertex is a ball of radius zero. That one identity is what lets hull() take any mix of
// operands without a path per combination: a sphere reduces to one ball, and any solid whose
// faces are planar -- a cube, a polyhedron, an imported mesh, an extruded polygon -- reduces
// to its vertices. Both then go through the same construction below.
//
// The hull's boundary is spherical caps, ruled patches tangent to a pair, and flat facets
// tangent to a triple, so
//
//     conv(U Bi) = U Bi  U  U(pairs) tangentEnvelope  U  conv(centres and tangent points)
//
// Bridging pairs alone is not enough: the region between three balls, near their centroid,
// lies in no pairwise envelope. Nor are triples enough -- four balls at the corners of a
// tetrahedron leave its centre outside every triple's slab, which is what the core polytope
// covers. The core's own facets are the flat parts of the hull boundary, and everything
// between it and that boundary is a cap or a pair envelope. With every radius zero the whole
// construction collapses to the convex hull of the vertices, which is the polyhedral case.
struct HullBall {
  gp_Pnt center;
  double radius{0};
};

// ponytail: the triple pass is cubic and only runs when a sphere is present; raise this or
// reduce the vertices further if real models hit it.
constexpr size_t hullGeneratorLimit = 512;
constexpr size_t hullTessellationLimit = 2000;

// Empty for an operand this construction cannot represent exactly, which today means any
// face that is neither planar nor a whole sphere, or any edge that is not a straight line.
std::optional<std::vector<HullBall>> hullBalls(const std::shared_ptr<void>& shape)
{
  std::vector<HullBall> balls;
  if (brepIsEmpty(shape)) return balls;
  if (const auto sphere = fullSphere(shapeFrom(shape)))
    return std::vector<HullBall>{{sphere->Location(), sphere->Radius()}};
  for (TopExp_Explorer faces(shapeFrom(shape), TopAbs_FACE); faces.More(); faces.Next())
    if (BRepAdaptor_Surface(TopoDS::Face(faces.Current())).GetType() != GeomAbs_Plane) return {};
  for (TopExp_Explorer edges(shapeFrom(shape), TopAbs_EDGE); edges.More(); edges.Next())
    if (BRepAdaptor_Curve(TopoDS::Edge(edges.Current())).GetType() != GeomAbs_Line) return {};
  std::set<std::array<double, 3>> unique;
  for (TopExp_Explorer vertices(shapeFrom(shape), TopAbs_VERTEX); vertices.More(); vertices.Next()) {
    const auto p = BRep_Tool::Pnt(TopoDS::Vertex(vertices.Current()));
    unique.insert({p.X(), p.Y(), p.Z()});
  }
  for (const auto& p : unique) balls.push_back({gp_Pnt(p[0], p[1], p[2]), 0.0});
  return balls;
}

// Cylinders and cones do not reduce to balls, but they do reduce to *disks*: a cylinder is
// the hull of its two rim circles, and a cone is the hull of a rim circle and its apex. A
// vertex is a disk of radius zero, so planar solids join the same model, exactly as they do
// for balls.
//
// The construction works because every disk here shares one normal. A supporting plane with
// normal m touches disk i at c_i + r_i * h, where h is m's unit component in the disks'
// common plane -- the *same* h for every disk. So the contact point depends only on one
// angle, and both the pair and triple tangency conditions collapse to
//
//     A cos(theta) + B sin(theta) + C = 0
//
// whose contact points are the vertices of every flat facet of the hull. The rest of the
// boundary is the operands' own faces and the ruled patches between disk pairs, and a ruled
// patch between two disks is always inside the hull because its rulings are chords of it.
struct HullDisk {
  gp_Pnt center;
  double radius{0};
};

struct HullSection {
  std::shared_ptr<void> shape;
  std::vector<HullDisk> disks;  // centres in the frame whose Z is the shared axis
};

// Angles where a supporting plane touches, i.e. the roots of A cos(t) + B sin(t) + C = 0.
std::vector<double> tangentAngles(double a, double b, double c)
{
  const double radius = std::hypot(a, b);
  if (radius <= Precision::Confusion()) return {};  // degenerate: every angle or none
  const double ratio = -c / radius;
  if (std::abs(ratio) > 1.0) return {};  // no plane touches both
  const double phase = std::atan2(b, a);
  const double offset = std::acos(std::clamp(ratio, -1.0, 1.0));
  return {phase + offset, phase - offset};
}

// A circle in the shared plane at the disk's height, parametrised from +X so that two of
// them correspond angle for angle, which is what makes the loft between them the hull patch.
TopoDS_Wire diskWire(const HullDisk& disk)
{
  return BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(gp_Circ(
                                   gp_Ax2(disk.center, gp_Dir(0, 0, 1), gp_Dir(1, 0, 0)), disk.radius)))
    .Wire();
}

// The ruled solid spanning two disks, analytic when they are coaxial. Empty when the pair
// spans no volume: two points, or two disks in one plane, both of which the core polytope
// covers.
std::optional<TopoDS_Shape> diskBridge(const HullDisk& first, const HullDisk& second)
{
  const double height = second.center.Z() - first.center.Z();
  if (std::abs(height) <= Precision::Confusion()) return {};
  const HullDisk& lower = height > 0 ? first : second;
  const HullDisk& upper = height > 0 ? second : first;
  if (lower.radius <= Precision::Confusion() && upper.radius <= Precision::Confusion()) return {};

  const double offset =
    std::hypot(upper.center.X() - lower.center.X(), upper.center.Y() - lower.center.Y());
  if (offset <= Precision::Confusion()) {
    // Coaxial, so OpenCASCADE has an analytic primitive for it and no loft is needed.
    const gp_Ax2 placement(lower.center, gp_Dir(0, 0, 1));
    const double span = upper.center.Z() - lower.center.Z();
    if (std::abs(lower.radius - upper.radius) <= Precision::Confusion())
      return BRepPrimAPI_MakeCylinder(placement, lower.radius, span).Shape();
    return BRepPrimAPI_MakeCone(placement, lower.radius, upper.radius, span).Shape();
  }

  BRepOffsetAPI_ThruSections loft(true, true, Precision::Confusion());
  loft.CheckCompatibility(false);
  for (const HullDisk& disk : {lower, upper}) {
    if (disk.radius <= Precision::Confusion())
      loft.AddVertex(BRepBuilderAPI_MakeVertex(disk.center).Vertex());
    else loft.AddWire(diskWire(disk));
  }
  loft.Build();
  if (!loft.IsDone()) throw std::runtime_error("B-Rep hull ruled patch failed");
  TopoDS_Shape shape = loft.Shape();
  // Lofting to a vertex can come back as a shell rather than a solid, and a shell in the
  // operand list makes the fuse fail outright rather than reporting anything useful.
  if (shape.ShapeType() == TopAbs_SHELL) shape = BRepBuilderAPI_MakeSolid(TopoDS::Shell(shape)).Solid();
  if (shape.ShapeType() != TopAbs_SOLID) throw std::runtime_error("B-Rep hull patch is not a solid");
  auto solid = TopoDS::Solid(shape);
  if (!BRepLib::OrientClosedSolid(solid)) throw std::runtime_error("B-Rep hull patch is open");
  if (!BRepCheck_Analyzer(solid).IsValid()) throw std::runtime_error("B-Rep hull patch is invalid");
  return solid;
}

// The one direction every cylinder and cone axis must share for the disk construction to
// apply. Empty when an operand has a surface it cannot represent -- a sphere, a torus, a
// spline -- or an edge that is neither a line nor a circle.
std::optional<gp_Dir> sharedDiskAxis(const std::vector<std::shared_ptr<void>>& operands)
{
  std::optional<gp_Dir> axis;
  for (const auto& operand : operands) {
    if (brepIsEmpty(operand)) continue;
    for (TopExp_Explorer faces(shapeFrom(operand), TopAbs_FACE); faces.More(); faces.Next()) {
      const BRepAdaptor_Surface surface(TopoDS::Face(faces.Current()));
      gp_Dir direction;
      if (surface.GetType() == GeomAbs_Plane) continue;
      else if (surface.GetType() == GeomAbs_Cylinder) direction = surface.Cylinder().Axis().Direction();
      else if (surface.GetType() == GeomAbs_Cone) direction = surface.Cone().Axis().Direction();
      else return {};
      if (!axis) axis = direction;
      else if (!axis->IsParallel(direction, Precision::Angular())) return {};
    }
    for (TopExp_Explorer edges(shapeFrom(operand), TopAbs_EDGE); edges.More(); edges.Next()) {
      const auto type = BRepAdaptor_Curve(TopoDS::Edge(edges.Current())).GetType();
      if (type != GeomAbs_Line && type != GeomAbs_Circle) return {};
    }
  }
  return axis;
}

// Each operand, moved into the frame whose Z is the shared axis, with its rim circles as
// disks and its remaining vertices as disks of radius zero.
std::vector<HullSection> diskSections(const std::vector<std::shared_ptr<void>>& operands,
                                      const gp_Trsf& toLocal)
{
  std::vector<HullSection> sections;
  for (const auto& operand : operands) {
    if (brepIsEmpty(operand)) continue;
    HullSection section;
    section.shape = std::make_shared<TopoDS_Shape>(
      BRepBuilderAPI_Transform(shapeFrom(operand), toLocal, true).Shape());
    std::set<std::array<double, 4>> seen;
    for (TopExp_Explorer edges(shapeFrom(section.shape), TopAbs_EDGE); edges.More(); edges.Next()) {
      const BRepAdaptor_Curve curve(TopoDS::Edge(edges.Current()));
      if (curve.GetType() != GeomAbs_Circle) continue;
      const auto circle = curve.Circle();
      const auto center = circle.Location();
      if (seen.insert({center.X(), center.Y(), center.Z(), circle.Radius()}).second)
        section.disks.push_back({center, circle.Radius()});
    }
    const size_t rims = section.disks.size();
    std::set<std::array<double, 3>> corners;
    for (TopExp_Explorer vertices(shapeFrom(section.shape), TopAbs_VERTEX); vertices.More();
         vertices.Next()) {
      const auto point = BRep_Tool::Pnt(TopoDS::Vertex(vertices.Current()));
      // A vertex sitting on a rim is already represented by that disk.
      bool onRim = false;
      for (size_t i = 0; i < rims && !onRim; ++i) {
        const auto& disk = section.disks[i];
        onRim = std::abs(point.Z() - disk.center.Z()) <= Precision::Confusion() &&
                std::abs(std::hypot(point.X() - disk.center.X(), point.Y() - disk.center.Y()) -
                         disk.radius) <= Precision::Confusion();
      }
      if (!onRim && corners.insert({point.X(), point.Y(), point.Z()}).second)
        section.disks.push_back({point, 0.0});
    }
    sections.push_back(std::move(section));
  }
  return sections;
}

std::shared_ptr<void> diskHull(const std::vector<HullSection>& sections)
{
  std::vector<HullDisk> disks;
  std::vector<size_t> owner;
  for (size_t index = 0; index < sections.size(); ++index) {
    for (const auto& disk : sections[index].disks) {
      disks.push_back(disk);
      owner.push_back(index);
    }
  }

  const auto contact = [&disks](size_t index, double angle) {
    return gp_Pnt(disks[index].center.X() + disks[index].radius * std::cos(angle),
                  disks[index].center.Y() + disks[index].radius * std::sin(angle),
                  disks[index].center.Z());
  };
  // Support of a disk for the plane normal (q cos t, q sin t, w).
  const auto support = [&disks](size_t index, double angle, double flat, double rise) {
    return flat * (disks[index].center.X() * std::cos(angle) +
                   disks[index].center.Y() * std::sin(angle) + disks[index].radius) +
           rise * disks[index].center.Z();
  };

  std::vector<gp_Pnt> core;
  // Only planes that support the whole set carry hull geometry. Keeping the rest would bury
  // the fuse in redundant tangent solids, which is both slow and a source of invalid results.
  const auto record = [&](double angle, double flat, double rise, const std::vector<size_t>& touched) {
    const double reference = support(touched.front(), angle, flat, rise);
    for (size_t index = 0; index < disks.size(); ++index)
      if (support(index, angle, flat, rise) > reference + 1e-9) return;
    for (const size_t index : touched) core.push_back(contact(index, angle));
  };

  for (size_t i = 0; i < disks.size(); ++i) {
    for (size_t j = i + 1; j < disks.size(); ++j) {
      // Planes perpendicular to the disks, touching this pair.
      for (const double angle :
           tangentAngles(disks[i].center.X() - disks[j].center.X(),
                         disks[i].center.Y() - disks[j].center.Y(), disks[i].radius - disks[j].radius))
        record(angle, 1.0, 0.0, {i, j});
      for (size_t k = j + 1; k < disks.size(); ++k) {
        // Oblique planes touching all three. Eliminating the plane's tilt between the two
        // equal-support conditions leaves the same one-angle equation.
        const double zij = disks[i].center.Z() - disks[j].center.Z();
        const double zik = disks[i].center.Z() - disks[k].center.Z();
        const double dx = disks[i].center.X() - disks[j].center.X();
        const double ex = disks[i].center.X() - disks[k].center.X();
        const double dy = disks[i].center.Y() - disks[j].center.Y();
        const double ey = disks[i].center.Y() - disks[k].center.Y();
        const double dr = disks[i].radius - disks[j].radius;
        const double er = disks[i].radius - disks[k].radius;
        for (const double angle :
             tangentAngles(dx * zik - ex * zij, dy * zik - ey * zij, dr * zik - er * zij)) {
          // Recover the tilt from the pair condition: flat * Gij + rise * Zij = 0.
          double flat = -zij;
          double rise = dx * std::cos(angle) + dy * std::sin(angle) + dr;
          const double norm = std::hypot(flat, rise);
          if (norm <= Precision::Confusion()) continue;
          flat /= norm;
          rise /= norm;
          if (flat < 0) {
            flat = -flat;
            rise = -rise;
            record(angle + M_PI, flat, rise, {i, j, k});
            continue;
          }
          record(angle, flat, rise, {i, j, k});
        }
      }
    }
  }

  // Same order rule as the ball hull: start from the core polytope so that every later fuse
  // grows a large accumulator, and add the tangent curved pieces last.
  std::vector<std::shared_ptr<void>> parts;
  if (auto polytope = pointHull(core)) parts.push_back(std::move(polytope));
  for (const auto& section : sections)
    if (!brepIsEmpty(section.shape)) parts.push_back(section.shape);
  // Every ruled patch between two disks is a set of chords of the hull, so it is always a
  // subset of the result and needs no support test. Coaxial disks in particular have only
  // *tilted* common tangent planes, which the perpendicular pair equation above cannot see,
  // so gating bridges on that equation would drop the cone joining two coaxial cylinders.
  for (size_t first = 0; first < disks.size(); ++first)
    for (size_t second = first + 1; second < disks.size(); ++second)
      if (owner[first] != owner[second])
        if (const auto bridge = diskBridge(disks[first], disks[second]))
          parts.push_back(std::make_shared<TopoDS_Shape>(*bridge));

  if (parts.empty()) return {};
  if (parts.size() == 1) return parts.front();
  auto result = brepBoolean(parts, BrepOperation::Union, 0).shape;
  if (brepIsEmpty(result) || !BRepCheck_Analyzer(shapeFrom(result)).IsValid())
    throw std::runtime_error("B-Rep hull is invalid");
  return result;
}

// Operands with no exact construction contribute the vertices of a tessellation instead, so
// hull() always produces something rather than failing. The result is a faceted solid that
// is inscribed in the true hull; doc/opencascade.md records this as an approximation.
std::vector<HullBall> tessellatedBalls(const std::shared_ptr<void>& shape, double fa, double fs)
{
  const auto bounds = brepBounds(shape);
  const double diagonal =
    std::hypot(std::hypot(bounds[3] - bounds[0], bounds[4] - bounds[1]), bounds[5] - bounds[2]);
  // Derived from $fa/$fs the same way the viewport and mesh-export boundary derives them, so
  // an approximate hull refines when the model asks for finer facets.
  const double radius = std::max(diagonal / 2.0, 0.01);
  const double halfSegment = std::min(fs / 2.0, radius);
  const double chord = std::max(radius - std::sqrt(radius * radius - halfSegment * halfSegment), 1e-4);
  const double angular = std::max(fa * M_PI / 180.0, 1e-3);
  // The point hull is incremental and rescans its faces per vertex, so it is the tessellation
  // tolerance that decides whether this finishes at all. Coarsen past what was asked for
  // rather than hand it an unbounded mesh.
  std::vector<gp_Pnt> points;
  for (double deflection = std::max(chord, Precision::Confusion());; deflection *= 2.0) {
    const auto mesh = brepMesh(shape, deflection, angular);
    points.clear();
    points.reserve(mesh.vertices.size());
    for (const auto& vertex : mesh.vertices) points.emplace_back(vertex[0], vertex[1], vertex[2]);
    if (points.size() <= hullTessellationLimit || deflection > diagonal) break;
  }

  std::vector<HullBall> balls;
  // Only extreme vertices can affect a hull, and dropping the rest keeps the triple pass small.
  if (const auto polytope = pointHull(points)) {
    std::set<std::array<double, 3>> unique;
    for (TopExp_Explorer vertices(shapeFrom(polytope), TopAbs_VERTEX); vertices.More();
         vertices.Next()) {
      const auto p = BRep_Tool::Pnt(TopoDS::Vertex(vertices.Current()));
      unique.insert({p.X(), p.Y(), p.Z()});
    }
    for (const auto& p : unique) balls.push_back({gp_Pnt(p[0], p[1], p[2]), 0.0});
    return balls;
  }
  for (const auto& point : points) balls.push_back({point, 0.0});
  return balls;
}

// The tangent envelope between two balls: a cylinder for equal radii, a cone otherwise, and
// a cone to an apex when one of them is a vertex. Empty when one ball already contains the
// other, and when both are vertices -- the core polytope spans those.
std::optional<TopoDS_Shape> tangentEnvelope(const HullBall& first, const HullBall& second)
{
  const double r1 = first.radius, r2 = second.radius;
  if (r1 <= Precision::Confusion() && r2 <= Precision::Confusion()) return {};
  gp_Vec axis(first.center, second.center);
  const double distance = axis.Magnitude();
  if (distance <= std::abs(r2 - r1) + Precision::Confusion()) return {};
  axis.Normalize();
  const double slope = (r2 - r1) / distance, radial = std::sqrt(1 - slope * slope);
  const gp_Ax2 placement(first.center.Translated(axis * (-slope * r1)), gp_Dir(axis));
  const double height = distance * radial * radial;
  if (height <= Precision::Confusion()) return {};
  return r1 == r2 ? BRepPrimAPI_MakeCylinder(placement, r1, height).Shape()
                  : BRepPrimAPI_MakeCone(placement, r1 * radial, r2 * radial, height).Shape();
}

// The six points where the two planes tangent to all three balls touch them. Empty when the
// centres are collinear, or when no plane touches all three; the pairwise envelopes then
// already cover everything the triple contributes.
std::vector<gp_Pnt> triTangentPoints(const HullBall& a, const HullBall& b, const HullBall& c)
{
  const gp_Vec u(a.center, b.center), v(a.center, c.center);
  const gp_Vec w = u.Crossed(v);
  if (w.Magnitude() <= Precision::Confusion()) return {};  // collinear centres
  // Solve n.u = r_a - r_b, n.v = r_a - r_c for the component of n in the plane of the centres.
  const double uu = u.Dot(u), vv = v.Dot(v), uv = u.Dot(v);
  const double determinant = uu * vv - uv * uv;
  if (std::abs(determinant) <= Precision::Confusion()) return {};
  const double du = a.radius - b.radius, dv = a.radius - c.radius;
  const double alpha = (du * vv - dv * uv) / determinant;
  const double beta = (dv * uu - du * uv) / determinant;
  const gp_Vec inPlane = u * alpha + v * beta;
  const double remainder = 1.0 - inPlane.Dot(inPlane);
  if (remainder <= Precision::Confusion()) return {};  // no plane touches all three
  const gp_Vec offAxis = gp_Vec(gp_Dir(w)) * std::sqrt(remainder);

  std::vector<gp_Pnt> tangents;
  for (const gp_Vec& normal : {inPlane + offAxis, inPlane - offAxis})
    for (const HullBall& ball : {a, b, c})
      tangents.emplace_back(ball.center.XYZ() + (normal * ball.radius).XYZ());
  return tangents;
}

std::shared_ptr<void> ballHull(const std::vector<HullBall>& balls)
{
  // Drop balls swallowed by another one so they contribute no degenerate pieces.
  std::vector<size_t> kept;
  for (size_t i = 0; i < balls.size(); ++i) {
    bool absorbed = false;
    for (size_t j = 0; j < balls.size() && !absorbed; ++j) {
      if (i == j) continue;
      const double distance = balls[i].center.Distance(balls[j].center);
      const bool inside = distance + balls[i].radius <= balls[j].radius + Precision::Confusion();
      // Coincident equals would otherwise absorb each other; keep the first.
      absorbed = inside && (balls[j].radius > balls[i].radius + Precision::Confusion() || j < i);
    }
    if (!absorbed) kept.push_back(i);
  }
  if (kept.empty()) return {};
  if (kept.size() == 1) {
    const auto& only = balls[kept.front()];
    if (only.radius <= Precision::Confusion()) return {};  // a single point has no volume
    return std::make_shared<TopoDS_Shape>(BRepPrimAPI_MakeSphere(only.center, only.radius).Shape());
  }
  const bool curved = std::any_of(kept.begin(), kept.end(),
                                  [&](size_t i) { return balls[i].radius > Precision::Confusion(); });
  // ponytail: the triple pass is cubic and only runs when a sphere is present; raise this or
  // pre-reduce the vertices to their own hull if real models hit it.
  if (curved && kept.size() > 512)
    throw std::runtime_error(
      "B-Rep hull of a sphere with more than 512 other hull points is "
      "not yet supported");

  std::vector<gp_Pnt> core;
  std::vector<std::shared_ptr<void>> envelopes, caps;
  for (const size_t i : kept) {
    core.push_back(balls[i].center);
    if (balls[i].radius > Precision::Confusion())
      caps.push_back(std::make_shared<TopoDS_Shape>(
        BRepPrimAPI_MakeSphere(balls[i].center, balls[i].radius).Shape()));
  }
  for (size_t i = 0; i < kept.size(); ++i) {
    for (size_t j = i + 1; j < kept.size(); ++j) {
      if (const auto envelope = tangentEnvelope(balls[kept[i]], balls[kept[j]]))
        envelopes.push_back(std::make_shared<TopoDS_Shape>(*envelope));
      if (!curved) continue;  // vertex-only triples contribute their centres, already in core
      for (size_t k = j + 1; k < kept.size(); ++k) {
        // Same reason, per triple: three vertices tangent-touch at themselves.
        if (balls[kept[i]].radius <= Precision::Confusion() &&
            balls[kept[j]].radius <= Precision::Confusion() &&
            balls[kept[k]].radius <= Precision::Confusion())
          continue;
        const auto tangents = triTangentPoints(balls[kept[i]], balls[kept[j]], balls[kept[k]]);
        core.insert(core.end(), tangents.begin(), tangents.end());
      }
    }
  }
  // Order matters, and not only for tidiness. Each envelope is tangent to the ball it springs
  // from, and fusing a tangent solid into a small accumulator makes OpenCASCADE discard one of
  // them outright -- a cube hulled with a sphere silently lost the sphere. Starting from the
  // core polytope and adding the caps last keeps every fuse a transversal overlap.
  std::vector<std::shared_ptr<void>> parts;
  // Fewer than four independent points means the pair envelopes already span everything.
  if (auto polytope = pointHull(core)) parts.push_back(std::move(polytope));
  parts.insert(parts.end(), envelopes.begin(), envelopes.end());
  parts.insert(parts.end(), caps.begin(), caps.end());
  if (parts.empty()) return {};
  if (parts.size() == 1) return parts.front();
  auto result = brepBoolean(parts, BrepOperation::Union, 0).shape;
  if (brepIsEmpty(result) || !BRepCheck_Analyzer(shapeFrom(result)).IsValid())
    throw std::runtime_error("B-Rep hull is invalid");
  return result;
}

}  // namespace

std::shared_ptr<void> brepHull(const std::vector<std::shared_ptr<void>>& operands, double fa, double fs,
                               bool *approximated)
{
  if (approximated) *approximated = false;
  try {
    std::vector<HullBall> balls;
    bool reducible = !operands.empty();
    for (const auto& operand : operands) {
      const auto part = hullBalls(operand);
      if (!part) {
        reducible = false;
        break;
      }
      balls.insert(balls.end(), part->begin(), part->end());
    }
    if (reducible) return ballHull(balls);

    // Cylinders and cones, in any number and mixed with planar solids, as long as their axes
    // are parallel. This replaced six pairwise constructions that between them covered only
    // two operands at a time.
    if (const auto axis = sharedDiskAxis(operands)) {
      gp_Trsf toLocal;
      toLocal.SetTransformation(gp_Ax3(gp_Pnt(0, 0, 0), *axis));
      try {
        if (auto local = diskHull(diskSections(operands, toLocal)))
          return std::make_shared<TopoDS_Shape>(
            BRepBuilderAPI_Transform(shapeFrom(local), toLocal.Inverted(), true).Shape());
      } catch (const std::exception&) {
        // A safety net, not an expected path: no arrangement in the tests needs it. If some
        // model does defeat OpenCASCADE's fuse, degrading to the tessellated path below
        // beats failing, and the exactness that is expected to hold is pinned by tests.
      }
    }

    // Nothing exact fits the whole operand set, so every operand contributes the vertices of
    // a tessellation and the hull is computed as a polyhedron. Keeping some operands exact
    // while others are tessellated is deliberately not done: mixing one sphere with a few
    // hundred mesh vertices puts the cubic triple pass into the millions of points, for a
    // result no better than this one.
    if (approximated) *approximated = true;
    std::vector<HullBall> generators;
    for (const auto& operand : operands) {
      if (brepIsEmpty(operand)) continue;
      const auto part = tessellatedBalls(operand, fa, fs);
      generators.insert(generators.end(), part.begin(), part.end());
    }
    if (generators.empty()) return {};
    return ballHull(generators);
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
      std::shared_ptr<void> remaining;
      if (BRepLib::OrientClosedSolid(solid) && BRepCheck_Analyzer(solid).IsValid()) {
        remaining = std::make_shared<TopoDS_Shape>(solid);
      } else {
        double minX = contour.front().front()[0], maxX = minX;
        double minY = contour.front().front()[1], maxY = minY;
        for (const auto& curve : contour) {
          for (const auto& point : curve) {
            minX = std::min(minX, point[0]);
            maxX = std::max(maxX, point[0]);
            minY = std::min(minY, point[1]);
            maxY = std::max(maxY, point[1]);
          }
        }
        const double margin = std::max(maxX - minX, maxY - minY) + 1;
        minX -= margin;
        maxX += margin;
        minY -= margin;
        maxY += margin;
        const auto plane =
          BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), minX, maxX, minY, maxY)
            .Face();
        TopTools_ListOfShape arguments, tools;
        arguments.Append(plane);
        for (TopExp_Explorer edges(wire.Wire(), TopAbs_EDGE); edges.More(); edges.Next()) {
          auto edge = TopoDS::Edge(edges.Current());
          BRepLib::BuildPCurveForEdgeOnPlane(edge, plane);
          tools.Append(edge);
        }
        BRepAlgoAPI_Splitter splitter;
        splitter.SetArguments(arguments);
        splitter.SetTools(tools);
        splitter.Build();
        if (!splitter.IsDone()) throw std::runtime_error("Self-intersecting profile split failed");
        const auto windingAt = [&](const gp_Pnt& sample) {
          int winding = 0;
          for (const auto& curve : contour) {
            const size_t degree = curve.size() - 1;
            std::array<double, 4> bernstein{};
            TColgp_Array1OfPnt poles(1, curve.size());
            TColStd_Array1OfReal weights(1, curve.size());
            for (size_t i = 0; i < curve.size(); ++i) {
              double weight = 1;
              if constexpr (N == 3) weight = curve[i][2];
              bernstein[i] = weight * (curve[i][1] - sample.Y());
              poles(i + 1) = gp_Pnt(curve[i][0], curve[i][1], 0);
              weights(i + 1) = weight;
            }
            std::vector<double> roots;
            const auto appendRoots = [&](const math_DirectPolynomialRoots& solution) {
              if (!solution.IsDone() || solution.InfiniteRoots()) return;
              for (int i = 1; i <= solution.NbSolutions(); ++i) roots.push_back(solution.Value(i));
            };
            if (degree == 1) {
              appendRoots(math_DirectPolynomialRoots(bernstein[1] - bernstein[0], bernstein[0]));
            } else if (degree == 2) {
              appendRoots(math_DirectPolynomialRoots(bernstein[0] - 2 * bernstein[1] + bernstein[2],
                                                     2 * (bernstein[1] - bernstein[0]), bernstein[0]));
            } else {
              appendRoots(math_DirectPolynomialRoots(
                -bernstein[0] + 3 * bernstein[1] - 3 * bernstein[2] + bernstein[3],
                3 * (bernstein[0] - 2 * bernstein[1] + bernstein[2]), 3 * (bernstein[1] - bernstein[0]),
                bernstein[0]));
            }
            Handle(Geom_BezierCurve) geometry;
            if constexpr (N == 3) geometry = new Geom_BezierCurve(poles, weights);
            else geometry = new Geom_BezierCurve(poles);
            for (const double root : roots) {
              if (root < -Precision::PConfusion() || root >= 1 - Precision::PConfusion()) continue;
              gp_Pnt point;
              gp_Vec tangent;
              geometry->D1(std::clamp(root, 0.0, 1.0), point, tangent);
              if (point.X() > sample.X() && std::abs(tangent.Y()) > Precision::Confusion())
                winding += tangent.Y() > 0 ? 1 : -1;
            }
          }
          return winding;
        };
        std::vector<std::shared_ptr<void>> cells;
        for (TopExp_Explorer faces(splitter.Shape(), TopAbs_FACE); faces.More(); faces.Next()) {
          const auto candidate = std::make_shared<TopoDS_Shape>(faces.Current());
          const auto bounds = brepBounds(candidate);
          if (bounds[0] <= minX + Precision::Confusion() || bounds[1] <= minY + Precision::Confusion() ||
              bounds[3] >= maxX - Precision::Confusion() || bounds[4] >= maxY - Precision::Confusion())
            continue;
          GProp_GProps properties;
          const auto candidateFace = TopoDS::Face(faces.Current());
          BRepGProp::SurfaceProperties(candidateFace, properties);
          const gp_Pnt sample = properties.CentreOfMass();
          if (BRepClass_FaceClassifier(candidateFace, sample, Precision::Confusion()).State() !=
              TopAbs_IN)
            throw std::runtime_error("Self-intersecting profile cell has no interior point");
          const int winding = windingAt(sample);
          if (evenOdd ? std::abs(winding) % 2 == 0 : winding == 0) continue;
          auto cell = std::make_shared<TopoDS_Shape>(
            TopoDS::Solid(BRepPrimAPI_MakePrism(candidateFace, gp_Vec(0, 0, height)).Shape()));
          if (!BRepCheck_Analyzer(shapeFrom(cell)).IsValid())
            throw std::runtime_error("Self-intersecting profile cell is invalid");
          cells.push_back(cell);
        }
        remaining = brepBoolean(cells, BrepOperation::Union, 0).shape;
        if (brepIsEmpty(remaining))
          throw std::runtime_error("Self-intersecting profile has no bounded regions");
      }
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
  double height, int lineCap, int lineJoin, double miterLimit)
{
  if (!std::isfinite(width) || width <= 0) throw std::invalid_argument("Invalid SVG stroke width");
  if (!std::isfinite(miterLimit) || miterLimit < 1)
    throw std::invalid_argument("Invalid SVG stroke miter limit");
  if (lineCap != 2 && lineCap != 3 && lineCap != 4)
    throw std::invalid_argument("Invalid SVG stroke cap");
  if (lineJoin != 0 && lineJoin != 2 && lineJoin != 3)
    throw std::invalid_argument("Invalid SVG stroke join");
  try {
    std::vector<std::shared_ptr<void>> strokes;
    const auto polygonPrism = [&](std::initializer_list<gp_Pnt> points) {
      BRepBuilderAPI_MakePolygon boundary;
      for (const auto& point : points) boundary.Add(point);
      boundary.Close();
      BRepBuilderAPI_MakeFace face(boundary.Wire(), true);
      if (!face.IsDone()) throw std::runtime_error("SVG stroke polygon construction failed");
      return std::make_shared<TopoDS_Shape>(
        TopoDS::Solid(BRepPrimAPI_MakePrism(face.Face(), gp_Vec(0, 0, height)).Shape()));
    };
    const auto appendJoin = [&](std::vector<std::shared_ptr<void>>& parts, const gp_Vec& previous,
                                const gp_Vec& next, const gp_Pnt& center) {
      const double turn = previous.X() * next.Y() - previous.Y() * next.X();
      if (std::abs(turn) <= Precision::Angular()) return;
      if (lineJoin == 2) {
        parts.push_back(std::make_shared<TopoDS_Shape>(
          BRepPrimAPI_MakeCylinder(gp_Ax2(center, gp_Dir(0, 0, 1)), width / 2, height).Shape()));
        return;
      }
      const double side = turn > 0 ? -1 : 1;
      const gp_Vec previousNormal(-previous.Y() * width / 2 * side, previous.X() * width / 2 * side, 0);
      const gp_Vec nextNormal(-next.Y() * width / 2 * side, next.X() * width / 2 * side, 0);
      const gp_Pnt previousOuter = center.Translated(previousNormal),
                   nextOuter = center.Translated(nextNormal);
      if (lineJoin == 0) {
        parts.push_back(polygonPrism({center, previousOuter, nextOuter}));
        return;
      }
      const gp_Vec between(previousOuter, nextOuter);
      const double along = (between.X() * next.Y() - between.Y() * next.X()) / turn;
      const gp_Pnt miter = previousOuter.Translated(previous * along);
      if (miter.Distance(center) <= miterLimit * width / 2)
        parts.push_back(polygonPrism({previousOuter, miter, nextOuter}));
      else parts.push_back(polygonPrism({center, previousOuter, nextOuter}));
    };
    for (const auto& centerline : centerlines) {
      if (centerline.empty()) continue;
      const auto& firstCurve = centerline.front();
      const auto& lastCurve = centerline.back();
      const bool closed =
        !firstCurve.empty() && !lastCurve.empty() &&
        gp_Pnt(firstCurve.front()[0], firstCurve.front()[1], 0)
            .Distance(gp_Pnt(lastCurve.back()[0], lastCurve.back()[1], 0)) <= Precision::Confusion();
      std::optional<gp_Vec> previousTangent;
      std::optional<gp_Vec> firstTangent;
      gp_Pnt previousEnd;
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
        gp_Vec startTangent, endTangent;
        if (curve.size() == 2) {
          gp_Vec direction(poles(1), poles(2));
          if (direction.Magnitude() <= Precision::Confusion()) continue;
          direction.Normalize();
          startTangent = endTangent = direction;
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
          gp_Pnt endpoint;
          basis->D1(basis->FirstParameter(), endpoint, startTangent);
          basis->D1(basis->LastParameter(), endpoint, endTangent);
          startTangent.Normalize();
          endTangent.Normalize();
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
        std::vector<std::shared_ptr<void>> parts{stroke};
        if (segment == 0) firstTangent = startTangent;
        if (!closed && segment == 0 && lineCap == 4)
          parts.push_back(std::make_shared<TopoDS_Shape>(
            BRepPrimAPI_MakeCylinder(gp_Ax2(poles(1), gp_Dir(0, 0, 1)), width / 2, height).Shape()));
        if (!closed && segment + 1 == centerline.size() && lineCap == 4)
          parts.push_back(std::make_shared<TopoDS_Shape>(
            BRepPrimAPI_MakeCylinder(gp_Ax2(poles(curve.size()), gp_Dir(0, 0, 1)), width / 2, height)
              .Shape()));
        const auto squareCap = [&](const gp_Pnt& center, gp_Vec outward) {
          outward.Normalize();
          const gp_Vec normal(-outward.Y() * width / 2, outward.X() * width / 2, 0);
          const gp_Vec extension = outward * (width / 2);
          return polygonPrism({center.Translated(normal), center.Translated(normal + extension),
                               center.Translated(-normal + extension), center.Translated(-normal)});
        };
        if (!closed && segment == 0 && lineCap == 3) parts.push_back(squareCap(poles(1), -startTangent));
        if (!closed && segment + 1 == centerline.size() && lineCap == 3)
          parts.push_back(squareCap(poles(curve.size()), endTangent));
        if (previousTangent) appendJoin(parts, *previousTangent, startTangent, previousEnd);
        auto joined = brepBoolean(parts, BrepOperation::Union, 0).shape;
        if (!BRepCheck_Analyzer(shapeFrom(joined)).IsValid())
          throw std::runtime_error("SVG stroke prism is invalid");
        strokes.push_back(joined);
        previousTangent = endTangent;
        previousEnd = poles(curve.size());
      }
      if (closed && previousTangent && firstTangent) {
        std::vector<std::shared_ptr<void>> closingJoin;
        appendJoin(closingJoin, *previousTangent, *firstTangent, previousEnd);
        strokes.insert(strokes.end(), closingJoin.begin(), closingJoin.end());
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
  try {
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
        BRepClass3d_SolidClassifier classifier(solids[j], BRep_Tool::Pnt(vertex),
                                               Precision::Confusion());
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
        if (inside[j][i] && depth[j] == depth[i] + 1)
          region.Add(TopoDS::Shell(boundaries[j].Reversed()));
      }
      if (!BRepCheck_Analyzer(region.Solid()).IsValid())
        throw std::runtime_error("B-Rep mesh cavity construction failed");
      builder.Add(regions, region.Solid());
    }
    // Merge coplanar triangles, without smoothing intentionally faceted surfaces.
    ShapeUpgrade_UnifySameDomain unify(regions, true, true, false);
    unify.Build();
    return std::make_shared<TopoDS_Shape>(unify.Shape());
  } catch (const Standard_Failure& error) {
    throw std::runtime_error(error.GetMessageString());
  }
}

size_t brepSurfaceCount(const std::shared_ptr<void>& shape, BrepSurfaceType type)
{
  try {
    if (brepIsEmpty(shape)) return 0;
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
  } catch (const Standard_Failure& error) {
    throw std::runtime_error(error.GetMessageString());
  }
}

size_t brepMemsize(const std::shared_ptr<void>& shape)
{
  // OCCT owns the shape's storage behind a handle, so this approximates it from the topology
  // that dominates it. GeometryCache budgets its LRU by this; a constant makes B-Reps look free.
  if (brepIsEmpty(shape)) return 0;
  size_t entities = 0;
  for (TopExp_Explorer faces(shapeFrom(shape), TopAbs_FACE); faces.More(); faces.Next()) ++entities;
  for (TopExp_Explorer edges(shapeFrom(shape), TopAbs_EDGE); edges.More(); edges.Next()) ++entities;
  for (TopExp_Explorer vertices(shapeFrom(shape), TopAbs_VERTEX); vertices.More(); vertices.Next())
    ++entities;
  // ponytail: flat per-entity estimate; measure real OCCT allocation if cache pressure misbehaves.
  return entities * 512;
}

std::array<double, 6> brepBounds(const std::shared_ptr<void>& shape)
{
  try {
    if (brepIsEmpty(shape)) return {0, 0, 0, 0, 0, 0};
    Bnd_Box box;
    BRepBndLib::AddOptimal(shapeFrom(shape), box, false, false);
    std::array<double, 6> result;
    box.Get(result[0], result[1], result[2], result[3], result[4], result[5]);
    return result;
  } catch (const Standard_Failure& error) {
    throw std::runtime_error(error.GetMessageString());
  }
}

std::shared_ptr<void> brepTransform(const std::shared_ptr<void>& shape,
                                    const std::array<double, 12>& matrix)
{
  try {
    if (brepIsEmpty(shape)) return shape;
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
  } catch (const Standard_Failure& error) {
    throw std::runtime_error(error.GetMessageString());
  }
}

BrepMeshData brepMesh(const std::shared_ptr<void>& shape, double linearDeflection,
                      double angularDeflection)
{
  try {
    if (brepIsEmpty(shape)) return {};
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
  } catch (const Standard_Failure& error) {
    throw std::runtime_error(error.GetMessageString());
  }
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

bool brepWriteStep(const std::shared_ptr<void>& shape, const std::string& filename)
{
  try {
    STEPControl_Writer writer;
    if (writer.Transfer(shapeFrom(shape), STEPControl_AsIs) != IFSelect_RetDone) return false;
    return writer.Write(filename.c_str()) == IFSelect_RetDone;
  } catch (const Standard_Failure& error) {
    throw std::runtime_error(error.GetMessageString());
  }
}

std::shared_ptr<void> brepReadStep(const std::string& filename)
{
  try {
    STEPControl_Reader reader;
    if (reader.ReadFile(filename.c_str()) != IFSelect_RetDone || reader.TransferRoots() == 0)
      throw std::runtime_error("OpenCASCADE STEP import failed");
    auto shape = std::make_shared<TopoDS_Shape>(reader.OneShape());
    if (shape->IsNull() || !BRepCheck_Analyzer(*shape).IsValid())
      throw std::runtime_error("OpenCASCADE STEP import produced invalid geometry");
    return shape;
  } catch (const Standard_Failure& error) {
    throw std::runtime_error(error.GetMessageString());
  }
}

bool brepWriteIges(const std::shared_ptr<void>& shape, const std::string& filename)
{
  try {
    IGESControl_Writer writer;
    return writer.AddShape(shapeFrom(shape)) && writer.Write(filename.c_str());
  } catch (const Standard_Failure& error) {
    throw std::runtime_error(error.GetMessageString());
  }
}

std::shared_ptr<void> brepReadIges(const std::string& filename)
{
  try {
    IGESControl_Reader reader;
    if (reader.ReadFile(filename.c_str()) != IFSelect_RetDone || reader.TransferRoots() == 0)
      throw std::runtime_error("OpenCASCADE IGES import failed");
    auto shape = std::make_shared<TopoDS_Shape>(reader.OneShape());
    if (shape->IsNull() || !BRepCheck_Analyzer(*shape).IsValid())
      throw std::runtime_error("OpenCASCADE IGES import produced invalid geometry");
    return shape;
  } catch (const Standard_Failure& error) {
    throw std::runtime_error(error.GetMessageString());
  }
}
