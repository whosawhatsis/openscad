#pragma once

#include <string>
#include <vector>

namespace Material {

bool isValidName(const std::string& name);

// One label per body, in source order: the material name, with a numeric
// discriminator appended when that name is used by more than one body. Bodies
// using the unnamed default material get the number alone. These are the names
// bodies carry wherever a format can name them - the STL filename suffix and
// the object name inside AMF and 3MF.
std::vector<std::string> bodyLabels(const std::vector<std::string>& materialNames);
std::vector<std::string> stlFilenames(const std::string& filename,
                                      const std::vector<std::string>& materialNames);

}  // namespace Material
