#include "SurfaceFinish.h"

#include <catch2/catch_all.hpp>
#include <cmath>

// The anisotropic-roughness contract, written before the implementation exists
// (registry row 64). The physical claim these pin down: a 3D-printed or
// lathe-turned surface smears reflections along one surface direction and stays
// crisp across it, which in a GGX BRDF is two alphas instead of one.

namespace {

//! The reference conversion, spelled out independently of the implementation so
//! the test cannot pass by agreeing with a copy of the same mistake.
//! alpha = roughness^2, split by the glTF/Blender 0.9 factor.
double referenceK(double anisotropy)
{
  return std::sqrt(1.0 - 0.9 * std::abs(anisotropy));
}

}  // namespace

TEST_CASE("isotropic roughness is the exact anisotropy = 0 case", "[SurfaceFinish][anisotropy]")
{
  // Not approximate: the existing isotropic look must not drift by a rounding
  // error just because the anisotropic code path now exists.
  for (const float roughness : {0.0f, 0.04f, 0.3f, 1.0f}) {
    const auto alpha = SurfaceFinish::alphaFor(roughness, 0.0f);
    const float expected = roughness * roughness;
    CHECK(alpha.along == expected);
    CHECK(alpha.across == expected);
  }
}

TEST_CASE("anisotropy widens one axis and narrows the other", "[SurfaceFinish][anisotropy]")
{
  const float roughness = 0.4f;
  const double alpha = static_cast<double>(roughness) * roughness;

  const auto half = SurfaceFinish::alphaFor(roughness, 0.5f);
  CHECK(half.along > half.across);
  CHECK(half.along == Catch::Approx(alpha / referenceK(0.5)).epsilon(1e-6));
  CHECK(half.across == Catch::Approx(alpha * referenceK(0.5)).epsilon(1e-6));
}

TEST_CASE("the product of the two alphas is preserved", "[SurfaceFinish][anisotropy]")
{
  // along * across == alpha^2 for every anisotropy, because the split is a
  // reciprocal pair. This is the invariant that catches a formula that widens
  // one axis without narrowing the other, which would brighten the whole
  // surface as anisotropy rises.
  const float roughness = 0.6f;
  const double alphaSq = std::pow(static_cast<double>(roughness) * roughness, 2.0);
  for (const float anisotropy : {-1.0f, -0.3f, 0.0f, 0.3f, 1.0f}) {
    const auto a = SurfaceFinish::alphaFor(roughness, anisotropy);
    CHECK(static_cast<double>(a.along) * a.across == Catch::Approx(alphaSq).epsilon(1e-6));
  }
}

TEST_CASE("negative anisotropy swaps the two axes exactly", "[SurfaceFinish][anisotropy]")
{
  // The [-1, 1] range is this project's extension: glTF spells the same thing
  // as strength |a| plus a 90-degree rotation, so the negative half must be the
  // positive half with the axes exchanged, not a separately-derived curve.
  const float roughness = 0.25f;
  for (const float anisotropy : {0.2f, 0.75f, 1.0f}) {
    const auto pos = SurfaceFinish::alphaFor(roughness, anisotropy);
    const auto neg = SurfaceFinish::alphaFor(roughness, -anisotropy);
    CHECK(neg.along == pos.across);
    CHECK(neg.across == pos.along);
  }
}

TEST_CASE("full anisotropy stays finite", "[SurfaceFinish][anisotropy]")
{
  // The 0.9 factor exists precisely so alpha_along does not divide by zero at
  // anisotropy = 1. A NaN here is a black or white surface in the viewport.
  const auto extreme = SurfaceFinish::alphaFor(1.0f, 1.0f);
  CHECK(std::isfinite(extreme.along));
  CHECK(std::isfinite(extreme.across));
  CHECK(extreme.across > 0.0f);
}

TEST_CASE("anisotropy defaults to isotropic and joins the finish identity",
          "[SurfaceFinish][anisotropy]")
{
  SurfaceFinish def;
  CHECK(def.anisotropy == 0.0f);
  // A model that never called material() must still be "default", or every
  // PolySet starts carrying a finish channel it does not need.
  CHECK(def.isDefault());

  // Two finishes differing only in anisotropy are different materials. If this
  // fails, the value is missing from the comparison tuples, and the geometry
  // cache and the per-face finish runs will silently merge surfaces that shade
  // differently.
  SurfaceFinish brushed;
  brushed.anisotropy = 0.8f;
  CHECK(brushed != def);
  CHECK(!brushed.isDefault());
  CHECK((brushed < def || def < brushed));
}
