#pragma once

#include <string>

#include "core/ModuleInstantiation.h"
#include "core/node.h"
#include "geometry/linalg.h"

enum class CgalAdvType { MINKOWSKI, HULL, FILL, RESIZE };

class CgalAdvNode : public AbstractNode
{
public:
  VISITABLE();
  CgalAdvNode(const ModuleInstantiation *mi, CgalAdvType type, double fa = 12.0, double fs = 2.0)
    : AbstractNode(mi), fa(fa), fs(fs), type(type)
  {
  }
  std::string toString() const override;
  std::string name() const override;

  // Captured for the B-Rep mesh boundary, which tessellates the exact result of a hull or a
  // Minkowski sum and so needs the settings that were in scope where it was written.
  double fa;
  double fs;
  unsigned int convexity{1};
  Vector3d newsize;
  Eigen::Matrix<bool, 3, 1> autosize;
  CgalAdvType type;
};
