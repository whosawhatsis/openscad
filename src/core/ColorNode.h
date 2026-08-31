#pragma once

#include <map>
#include <string>

#include <string>

#include "core/BaseVisitable.h"
#include "core/ModuleInstantiation.h"
#include "core/node.h"
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
  // Additional POV-Ray finish parameters, kept as a map because nothing but the
  // POV exporter consumes them yet - see ColorNode.cc for the accepted names.
  std::map<std::string, double> finishParams;
  bool hasMetallic{false};
};
