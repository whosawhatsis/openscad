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
  CsgOpNode(const ModuleInstantiation *mi, OpenSCADOperator type, double filletRadius = 0.0,
            bool hasFillet = false)
    : AbstractNode(mi), type(type), filletRadius(filletRadius), hasFillet(hasFillet)
  {
  }
  std::string toString() const override;
  std::string name() const override;
};
