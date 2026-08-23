#include "core/Material.h"

#include <algorithm>
#include <filesystem>
#include <unordered_map>

namespace Material {

bool isValidName(const std::string& name)
{
  const auto isAsciiAlnum = [](unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
  };
  return !name.empty() && isAsciiAlnum(name.front()) && isAsciiAlnum(name.back()) &&
         std::all_of(name.begin(), name.end(), [&](unsigned char c) {
           return isAsciiAlnum(c) || c == '-' || c == '_' || c == '.';
         });
}

std::vector<std::string> bodyLabels(const std::vector<std::string>& materialNames)
{
  std::unordered_map<std::string, size_t> totals;
  std::unordered_map<std::string, size_t> indexes;
  for (const auto& name : materialNames) ++totals[name];

  std::vector<std::string> result;
  result.reserve(materialNames.size());
  for (const auto& name : materialNames) {
    std::string label = name;
    if (totals[name] > 1) {
      if (!label.empty()) label += "-";
      label += std::to_string(++indexes[name]);
    }
    result.push_back(std::move(label));
  }
  return result;
}

std::vector<std::string> stlFilenames(const std::string& filename,
                                      const std::vector<std::string>& materialNames)
{
  const std::filesystem::path path(filename);
  const auto stem = path.stem().string();
  const auto extension = path.extension().string();

  std::vector<std::string> result;
  result.reserve(materialNames.size());
  for (const auto& label : bodyLabels(materialNames)) {
    const std::string suffix = label.empty() ? "" : "-" + label;
    result.push_back((path.parent_path() / (stem + suffix + extension)).string());
  }
  return result;
}

}  // namespace Material
