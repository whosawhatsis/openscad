#pragma once

#include <map>
#include <string>

#include <string>

#include "core/BaseVisitable.h"
#include "core/ModuleInstantiation.h"
#include "core/node.h"
#include "geometry/SurfaceFinish.h"
#include "geometry/linalg.h"

class ColorNode : public AbstractNode
{
public:
  VISITABLE();
  ColorNode(const ModuleInstantiation *mi) : AbstractNode(mi) {}
  std::string toString() const override;
  std::string name() const override;

  Color4f color;
  std::string materialName;
  bool isMaterial{false};

  // Procedural bump: implicit 3D coherent noise sampled in object-local
  // coordinates, perturbing only the lighting normal. Supra-pixel detail you can
  // see individually. Never changes geometry.
  // [scale (mm), strength, seed]; only meaningful when hasBump is true.
  Vector3d bump{0.0, 0.0, 0.0};
  bool hasBump{false};

  // Conventional scalar PBR metallic-roughness, both in [0,1]. Sub-pixel and
  // statistical: these widen the specular lobe and shift diffuse/specular
  // balance, and move no normal. Independent of bump; an object may set both.
  double pbrRoughness{0.0};
  bool hasPbrRoughness{false};
  double metallic{0.0};
  // Anisotropy of the specular lobe, in [-1, 1] - wider than the [0,1] of the
  // parameters above, which is why it is not one of them. Positive smears
  // reflections along the layer/turning direction of a printed or lathed
  // surface, negative across it. Like roughness it is tracked as set/unset, so
  // that dumps of scripts that never mention it stay byte-identical.
  double anisotropy{0.0};
  bool hasAnisotropy{false};
  // Additional POV-Ray finish parameters, kept as a map because nothing but the
  // POV exporter consumes them yet - see ColorNode.cc for the accepted names.
  std::map<std::string, double> finishParams;
  bool hasMetallic{false};

  //! The shading half of what this node declares, in the form both evaluators
  //! and the viewport want it. specular and ior collapse into one dielectric
  //! reflectance here; the exporters read them unfolded out of finishParams.
  [[nodiscard]] SurfaceFinish finish() const
  {
    const auto param = [&](const char *name, double fallback) {
      const auto it = finishParams.find(name);
      return it == finishParams.end() ? fallback : it->second;
    };
    SurfaceFinish f;
    if (hasPbrRoughness) f.roughness = static_cast<float>(pbrRoughness);
    if (hasMetallic) f.metallic = static_cast<float>(metallic);
    if (hasAnisotropy) f.anisotropy = static_cast<float>(anisotropy);
    f.reflectance = SurfaceFinish::reflectanceFor(param("ior", 1.5), param("specular", 1.0));
    f.emission = static_cast<float>(param("emission", 0.0));
    return f;
  }
};
