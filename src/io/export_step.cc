#include "io/export.h"

#include "geometry/brep/BrepGeometry.h"
#include "geometry/brep/BrepGeometryData.h"

bool export_step(const std::shared_ptr<const Geometry>& geom, const std::string& filename)
{
  const auto brep = std::dynamic_pointer_cast<const BrepGeometry>(geom);
  if (!brep) return false;
  return brepWriteStep(brep->opaqueShape(), filename);
}

bool export_iges(const std::shared_ptr<const Geometry>& geom, const std::string& filename)
{
  const auto brep = std::dynamic_pointer_cast<const BrepGeometry>(geom);
  return brep && brepWriteIges(brep->opaqueShape(), filename);
}
