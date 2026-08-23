#include <catch2/catch_test_macros.hpp>

#include "core/Material.h"

TEST_CASE("material names are safe and predictable in filenames")
{
  CHECK(Material::isValidName("PLA"));
  CHECK(Material::isValidName("petg-black_2.0"));
  CHECK_FALSE(Material::isValidName(""));
  CHECK_FALSE(Material::isValidName("two words"));
  CHECK_FALSE(Material::isValidName("../part"));
  CHECK_FALSE(Material::isValidName("part/one"));
  CHECK_FALSE(Material::isValidName("part:one"));
  CHECK_FALSE(Material::isValidName(".hidden"));
  CHECK_FALSE(Material::isValidName("part."));
  CHECK_FALSE(Material::isValidName("r\xC3\xA9sin"));
}

TEST_CASE("multi STL filenames preserve source order and number collisions")
{
  const std::vector<std::string> names{"", "PLA", "PLA", "", "PETG"};
  CHECK(Material::stlFilenames("model.stl", names) ==
        std::vector<std::string>{"model-1.stl", "model-PLA-1.stl", "model-PLA-2.stl",
                                 "model-2.stl", "model-PETG.stl"});
}
