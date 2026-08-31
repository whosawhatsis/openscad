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
        std::vector<std::string>{"model-1.stl", "model-PLA-1.stl", "model-PLA-2.stl", "model-2.stl",
                                 "model-PETG.stl"});
}

TEST_CASE("body labels disambiguate repeated materials in source order")
{
  // The label is what a body is called wherever a format can name it: the STL
  // filename suffix, and the object name inside AMF/3MF. A material used once
  // needs no discriminator; the unnamed default material has no name to use, so
  // it falls back to the number alone.
  const std::vector<std::string> names{"", "PLA", "PLA", "", "PETG"};
  CHECK(Material::bodyLabels(names) == std::vector<std::string>{"1", "PLA-1", "PLA-2", "2", "PETG"});

  CHECK(Material::bodyLabels({"PLA"}) == std::vector<std::string>{"PLA"});
  CHECK(Material::bodyLabels({""}) == std::vector<std::string>{""});
  CHECK(Material::bodyLabels({}).empty());
}
