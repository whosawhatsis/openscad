#include "AnimationExport.h"

#include <catch2/catch_all.hpp>

TEST_CASE("USD files use geometry animation recording", "[animate][usd]")
{
  REQUIRE(animation_export::recordsGeometry("animation.usda"));
  REQUIRE(animation_export::recordsGeometry("animation.usdz"));
  REQUIRE(animation_export::recordsGeometry("animation.USDZ"));
  REQUIRE_FALSE(animation_export::recordsGeometry("animation.gif"));
  REQUIRE_FALSE(animation_export::recordsGeometry("animation.png"));
}
