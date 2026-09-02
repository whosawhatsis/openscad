#pragma once

#include <cmath>
#include <tuple>

#include "geometry/linalg.h"

/*!
 * Per-surface shading parameters, the shading half of what color() and
 * material() declare. Carried alongside color everywhere color goes - through
 * Manifold's original-ID runs, into PolySet's per-face channel, and into the
 * viewport as one vec4 vertex attribute.
 *
 * Why a per-face channel rather than the per-object scalars on Geometry: the F6
 * renderer draws one resolved mesh, so a per-object value can only ever
 * describe a model made of a single material, and a multi-child root has no
 * single value to carry at all. F5 never noticed because it draws one CSG
 * product per leaf.
 *
 * `specular` and `ior` are folded into a single `reflectance` here rather than
 * carried separately: both do exactly one job in a microfacet BRDF, which is to
 * set a dielectric's F0, and the fold is exact. The exporters that want them
 * spelled out read the unfolded values from Geometry::finishParams().
 */
struct SurfaceFinish {
  //! Negative means the model set none, so that an explicit roughness = 0 (a
  //! mirror) stays distinguishable from "not set". Zero cannot be the sentinel,
  //! because zero is a meaningful roughness.
  float roughness{-1.0f};
  float metallic{0.0f};
  //! Normal-incidence reflectance of a dielectric. 0.04 is ior 1.5, which is
  //! the conventional default for everything that is not a metal.
  float reflectance{0.04f};
  float emission{0.0f};
  //! Anisotropy of the specular lobe, in [-1, 1]. 0 is isotropic. Positive
  //! smears reflections along the layer/turning direction (a 3D print, a lathed
  //! part); negative smears them across it. Deliberately signed rather than
  //! glTF's strength + rotation pair, because the only rotation this needs is
  //! the 90-degree one, and a sign is cheaper than an angle.
  float anisotropy{0.0f};
  //! The surface direction the anisotropy is measured along - the layer
  //! direction of a print, the turning direction of a lathed part. Captured as
  //! the build axis (+Z) when material() is evaluated and then transformed with
  //! the geometry, so a rotated part carries its layer direction with it.
  //!
  //! Stored rather than derived in the shader from a global +Z: an assembly of
  //! parts each printed in a different orientation would otherwise get one
  //! layer direction for all of them, correct for at most one. It costs
  //! nothing to store, because anisotropy already forces a second vec4 vertex
  //! attribute and (axis.xyz, anisotropy) fills it exactly.
  Vector3d axis{0.0, 0.0, 1.0};

  //! The two GGX alphas for this finish: `along` the anisotropy direction and
  //! `across` it. Isotropic GGX is the anisotropy = 0 case, where both equal
  //! roughness^2.
  struct MicrofacetAlpha {
    float along;
    float across;
  };

  //! Splits roughness into the two GGX alphas. The 0.9 factor is the
  //! glTF/Blender convention; it keeps the widened axis finite at anisotropy 1,
  //! where an undamped split would divide by zero and paint the surface NaN.
  //!
  //! The split is a reciprocal pair - one axis is divided by k and the other
  //! multiplied by it - so `along * across` stays alpha^2 at every anisotropy
  //! and the lobe redistributes energy instead of gaining it.
  //!
  //! The magnitude is taken before the split and the pair swapped afterwards,
  //! rather than letting a signed anisotropy flow into k. Those are not the
  //! same function: k(-x) != 1/k(x), so feeding the sign through would make
  //! negative anisotropy a differently-shaped lobe instead of the same lobe
  //! turned 90 degrees, which is what the sign is defined to mean.
  static MicrofacetAlpha alphaFor(float roughness, float anisotropy)
  {
    const float alpha = roughness * roughness;
    const float magnitude = std::fmin(std::fabs(anisotropy), 1.0f);
    const float k = std::sqrt(1.0f - 0.9f * magnitude);
    const MicrofacetAlpha split{alpha / k, alpha * k};
    return anisotropy < 0.0f ? MicrofacetAlpha{split.across, split.along} : split;
  }

  //! Moves the anisotropy axis by a transform applied to the geometry. It is a
  //! direction, so only the linear part applies - a translation must not move
  //! it, and the affine transform would tilt it by an amount depending on where
  //! the part happens to sit.
  //!
  //! No inverse-transpose: the axis lies in the surface rather than normal to
  //! it, so it transforms as a tangent. Under non-uniform scale a tangent and
  //! the surface it should lie in disagree; anisotropy is ill-defined there
  //! anyway, so renormalize and move on rather than add a correction nobody
  //! asked for. A scale that flattens the axis to nothing leaves no direction
  //! to speak of, so the finish drops to isotropic instead of normalizing a
  //! zero vector into NaN.
  void transformAxis(const Transform3d& mat)
  {
    const Vector3d moved = mat.linear() * axis;
    const double length = moved.norm();
    if (length > 1e-12) {
      axis = moved / length;
    } else {
      axis = Vector3d(0.0, 0.0, 1.0);
      anisotropy = 0.0f;
    }
  }

  //! F0 for the given index of refraction, scaled by a specular multiplier.
  //! ior 1.5 and specular 1 give the 0.04 default back.
  static float reflectanceFor(double ior, double specular)
  {
    const double f0 = (ior - 1.0) / (ior + 1.0);
    return static_cast<float>(f0 * f0 * specular);
  }

  [[nodiscard]] bool isDefault() const { return *this == SurfaceFinish{}; }

  bool operator==(const SurfaceFinish& o) const
  {
    return std::tie(roughness, metallic, reflectance, emission, anisotropy, axis.x(), axis.y(),
                    axis.z()) == std::tie(o.roughness, o.metallic, o.reflectance, o.emission,
                                          o.anisotropy, o.axis.x(), o.axis.y(), o.axis.z());
  }
  bool operator!=(const SurfaceFinish& o) const { return !(*this == o); }
  bool operator<(const SurfaceFinish& o) const
  {
    return std::tie(roughness, metallic, reflectance, emission, anisotropy, axis.x(), axis.y(),
                    axis.z()) < std::tie(o.roughness, o.metallic, o.reflectance, o.emission,
                                         o.anisotropy, o.axis.x(), o.axis.y(), o.axis.z());
  }
};
