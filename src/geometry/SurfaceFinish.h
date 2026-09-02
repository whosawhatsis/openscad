#pragma once

#include <cmath>
#include <tuple>

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
    return std::tie(roughness, metallic, reflectance, emission, anisotropy) ==
           std::tie(o.roughness, o.metallic, o.reflectance, o.emission, o.anisotropy);
  }
  bool operator!=(const SurfaceFinish& o) const { return !(*this == o); }
  bool operator<(const SurfaceFinish& o) const
  {
    return std::tie(roughness, metallic, reflectance, emission, anisotropy) <
           std::tie(o.roughness, o.metallic, o.reflectance, o.emission, o.anisotropy);
  }
};
