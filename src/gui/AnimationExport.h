#pragma once

#include <string>

#include "io/VideoEncoder.h"

namespace animation_export {

inline bool recordsGeometry(const std::string& path)
{
  const auto suffix = outputSuffix(path);
  return suffix == "usda" || suffix == "usdz" || suffix == "blend";
}

}  // namespace animation_export
