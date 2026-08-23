#pragma once

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

  // Procedural bump roughness: implicit 3D coherent noise sampled in object-local
  // coordinates, perturbing only the lighting normal. Never changes geometry.
  // [scale (mm), strength, seed]; only meaningful when hasRoughness is true.
  Vector3d roughness{0.0, 0.0, 0.0};
  bool hasRoughness{false};
};
