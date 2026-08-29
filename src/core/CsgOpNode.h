#pragma once

#include <string>

#include "core/ModuleInstantiation.h"
#include "core/enums.h"
#include "core/node.h"

class CsgOpNode : public AbstractNode
{
public:
  VISITABLE();
  OpenSCADOperator type;
  double filletRadius;
  bool hasFillet;
  double fa;
  double fs;
  CsgOpNode(const ModuleInstantiation *mi, OpenSCADOperator type, double filletRadius = 0.0,
            bool hasFillet = false, double fa = 12.0, double fs = 2.0)
    : AbstractNode(mi), type(type), filletRadius(filletRadius), hasFillet(hasFillet), fa(fa), fs(fs)
  {
  }
  std::string toString() const override;
  std::string name() const override;
};
