#include "geometry/PolySet.h"

#include <catch2/catch_all.hpp>
#include <cmath>

#include "geometry/SurfaceFinish.h"
#include "geometry/linalg.h"

// The anisotropy axis has to move with the geometry it describes (registry row
// 64). Written before the implementation.
//
// Why it cannot be derived in the shader from a global build axis instead: a
// model showing several parts that were each printed in a different orientation
// and then assembled would get one layer direction for all of them, correct for
// at most one. The axis is therefore a property of the material, captured at
// material() evaluation and carried through every transform the geometry sees.

namespace {

PolySet makeTriangleWithFinish(const SurfaceFinish& finish)
{
  PolySet ps(3);
  ps.vertices = {Vector3d(0, 0, 0), Vector3d(1, 0, 0), Vector3d(0, 1, 0)};
  ps.indices = {{0, 1, 2}};
  ps.setFinish(finish);
  return ps;
}

SurfaceFinish anisotropicFinish()
{
  SurfaceFinish f;
  f.roughness = 0.3f;
  f.anisotropy = 1.0f;
  return f;
}

}  // namespace

TEST_CASE("a rotation carries the anisotropy axis with it", "[PolySet][anisotropy][axis]")
{
  // The case the whole design rests on: rotate([90,0,0]) turns the default +Z
  // layer direction into the Y axis. If it stayed on Z the smear would slide
  // across the surface like a projected texture instead of behaving like a
  // material.
  //
  // Parallelism, not equality: the axis is an undirected line rather than a
  // ray, because a lobe smeared along +d and one smeared along -d are the same
  // lobe. Asserting a sign here would pin down something the renderer cannot
  // observe -- and would be asserting the wrong one, since a right-handed
  // rotation about +X sends +Z to -Y.
  PolySet ps = makeTriangleWithFinish(anisotropicFinish());

  Transform3d rot(Eigen::AngleAxisd(M_PI / 2, Vector3d::UnitX()));
  ps.transform(rot);

  REQUIRE(ps.finishes.size() == 1);
  const Vector3d axis = ps.finishes[0].axis;
  CHECK(std::abs(axis.dot(Vector3d::UnitY())) == Catch::Approx(1.0).margin(1e-9));
  CHECK(std::abs(axis.dot(Vector3d::UnitZ())) == Catch::Approx(0.0).margin(1e-9));
}

TEST_CASE("a translation leaves the anisotropy axis alone", "[PolySet][anisotropy][axis]")
{
  // It is a direction, not a position. Applying the full affine transform to it
  // as if it were a point is the obvious bug here, and it would tilt the axis
  // by an amount that depends on where the part happens to sit.
  PolySet ps = makeTriangleWithFinish(anisotropicFinish());

  ps.transform(Transform3d(Eigen::Translation3d(10.0, -4.0, 2.5)));

  const Vector3d axis = ps.finishes[0].axis;
  CHECK(std::abs(axis.dot(Vector3d::UnitZ())) == Catch::Approx(1.0).margin(1e-9));
}

TEST_CASE("the anisotropy axis stays normalized under scaling", "[PolySet][anisotropy][axis]")
{
  // Anisotropy is ill-defined under non-uniform scale, so the contract is
  // modest but must hold: whatever direction comes out, it is a unit vector.
  // A non-unit axis would silently scale the tangent frame in the shader.
  PolySet ps = makeTriangleWithFinish(anisotropicFinish());

  Transform3d scale(Eigen::Scaling(3.0, 1.0, 0.5));
  ps.transform(scale);

  CHECK(ps.finishes[0].axis.norm() == Catch::Approx(1.0).epsilon(1e-9));
}

TEST_CASE("a degenerate axis falls back to isotropic rather than to NaN", "[PolySet][anisotropy][axis]")
{
  // A flattening scale can collapse the axis to zero length. Normalizing that
  // is a division by zero, and a NaN axis paints the surface black or white.
  PolySet ps = makeTriangleWithFinish(anisotropicFinish());

  Transform3d flatten(Eigen::Scaling(1.0, 1.0, 0.0));
  ps.transform(flatten);

  const SurfaceFinish& f = ps.finishes[0];
  CHECK(std::isfinite(f.axis.x()));
  CHECK(std::isfinite(f.axis.y()));
  CHECK(std::isfinite(f.axis.z()));
  CHECK(f.anisotropy == 0.0f);
}

TEST_CASE("the axis defaults to the build direction and joins the finish identity",
          "[PolySet][anisotropy][axis]")
{
  const SurfaceFinish def;
  CHECK(def.axis.x() == 0.0);
  CHECK(def.axis.y() == 0.0);
  CHECK(def.axis.z() == 1.0);
  CHECK(def.isDefault());

  SurfaceFinish turned;
  turned.axis = Vector3d(0, 1, 0);
  CHECK(turned != def);
  CHECK((turned < def || def < turned));
}
