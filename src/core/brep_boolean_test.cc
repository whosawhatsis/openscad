#ifdef ENABLE_OPENCSCADE

#include <catch2/catch_test_macros.hpp>

#include "geometry/brep/BrepBoolean.h"

#include <BRepCheck_Analyzer.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>

TEST_CASE("B-Rep difference fillets only boolean-created edges", "[brep]")
{
  const TopoDS_Shape box = BRepPrimAPI_MakeBox(20.0, 20.0, 10.0).Shape();
  const gp_Ax2 axis(gp_Pnt(10.0, 10.0, -1.0), gp_Dir(0.0, 0.0, 1.0));
  const TopoDS_Shape cylinder = BRepPrimAPI_MakeCylinder(axis, 4.0, 12.0).Shape();

  const auto result = applyBrepDifference({box, cylinder}, 1.0);

  REQUIRE(result.filletedEdgeCount == 2);
  REQUIRE(BRepCheck_Analyzer(result.shape).IsValid());
}

#endif
