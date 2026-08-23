#pragma once

#include <string>
#include <vector>

namespace Material {

bool isValidName(const std::string& name);
std::vector<std::string> stlFilenames(const std::string& filename,
                                      const std::vector<std::string>& materialNames);

}  // namespace Material
