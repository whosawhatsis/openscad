#include "io/export.h"

#include "geometry/brep/BrepGeometry.h"
#include "geometry/brep/BrepGeometryData.h"
#include "utils/printutils.h"

namespace {

// Returning false without a word is how this failed silently for every command-line export:
// the writer needs the retained B-Rep, and a mesh reached it whenever the caller had already
// crossed the tessellation boundary.
std::shared_ptr<const BrepGeometry> exactGeometry(const std::shared_ptr<const Geometry>& geom,
                                                  const char *format)
{
  const auto brep = std::dynamic_pointer_cast<const BrepGeometry>(geom);
  if (!brep)
    LOG(message_group::Export_Error,
        "%1$s export needs exact geometry, but the model was already tessellated. It requires "
        "the OpenCASCADE backend and a model it can represent exactly.",
        format);
  return brep;
}

}  // namespace

bool export_step(const std::shared_ptr<const Geometry>& geom, const std::string& filename)
{
  const auto brep = exactGeometry(geom, "STEP");
  return brep && brepWriteStep(brep->opaqueShape(), filename);
}

bool export_iges(const std::shared_ptr<const Geometry>& geom, const std::string& filename)
{
  const auto brep = exactGeometry(geom, "IGES");
  return brep && brepWriteIges(brep->opaqueShape(), filename);
}
