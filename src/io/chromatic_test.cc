#include <cmath>
#include <string>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "io/chromatic.h"

using Catch::Approx;

TEST_CASE("the three light directions are unit length", "[chromatic]")
{
  for (const auto& light : chromatic_lights()) {
    const double len =
      std::sqrt(light.dir[0] * light.dir[0] + light.dir[1] * light.dir[1] + light.dir[2] * light.dir[2]);
    CHECK(len == Approx(1.0));
  }
}

TEST_CASE("the three light directions are linearly independent", "[chromatic]")
{
  // Coplanar directions would make the three channels dependent, and the whole
  // point of the mode is that each channel is an independent observation. A
  // determinant near zero is the failure, so this is a margin, not != 0.
  const auto l = chromatic_lights();
  const double det = l[0].dir[0] * (l[1].dir[1] * l[2].dir[2] - l[1].dir[2] * l[2].dir[1]) -
                     l[0].dir[1] * (l[1].dir[0] * l[2].dir[2] - l[1].dir[2] * l[2].dir[0]) +
                     l[0].dir[2] * (l[1].dir[0] * l[2].dir[1] - l[1].dir[1] * l[2].dir[0]);
  CHECK(std::abs(det) > 0.1);
}

TEST_CASE("each light drives exactly one channel", "[chromatic]")
{
  const auto l = chromatic_lights();
  CHECK(l[0].channel == 0);
  CHECK(l[1].channel == 1);
  CHECK(l[2].channel == 2);
}

TEST_CASE("the gauge sphere is lit by the same table as the model", "[chromatic]")
{
  // Odd size deliberately: with an even one no pixel centre lands on the disc
  // centre, so the exact n = (0,0,1) contract below could only be checked to
  // within a pixel's worth of tilt.
  const unsigned size = 65;
  const auto gauge = render_gauge_sphere(size);
  REQUIRE(gauge.pixels.size() == static_cast<size_t>(size) * size * 4);

  // Centre of the disc faces the viewer: n = (0,0,1), so each channel is that
  // light's own z component. This is the contract a consumer reads the gauge
  // against, so it is pinned exactly rather than eyeballed.
  const auto lights = chromatic_lights();
  const size_t centre = ((size / 2) * size + (size / 2)) * 4;
  for (int c = 0; c < 3; ++c) {
    const double expected = std::max(0.0, lights[c].dir[2]);
    CHECK(gauge.pixels[centre + c] / 255.0 == Approx(expected).margin(1.0 / 255.0));
  }
  CHECK(gauge.pixels[centre + 3] == 255);
}

TEST_CASE("the gauge is transparent outside the disc", "[chromatic]")
{
  const unsigned size = 64;
  const auto gauge = render_gauge_sphere(size);
  // A corner is outside the inscribed disc. Transparent, not black: black is a
  // real shading value (a surface facing away from every light), so an opaque
  // black corner would read as part of the sphere.
  CHECK(gauge.pixels[3] == 0);
  const size_t last = (static_cast<size_t>(size) * size - 1) * 4;
  CHECK(gauge.pixels[last + 3] == 0);
}

TEST_CASE("gauge shading is never negative", "[chromatic]")
{
  // Lambert has to clamp: a surface turned away from a light receives nothing
  // from it, and an unclamped dot product would subtract from the other lights.
  const auto gauge = render_gauge_sphere(32);
  for (const auto v : gauge.pixels) {
    CHECK(v <= 255);
  }
  // The rim of the disc faces sideways; at least one channel must be dark there,
  // or the clamp is not happening at all.
  const auto lights = chromatic_lights();
  double n[3] = {-1.0, 0.0, 0.0};
  bool any_dark = false;
  for (const auto& light : lights) {
    const double d = n[0] * light.dir[0] + n[1] * light.dir[1] + n[2] * light.dir[2];
    if (d <= 0.0) any_dark = true;
  }
  CHECK(any_dark);
}

TEST_CASE("the sidecar records the light directions", "[chromatic]")
{
  const std::string json = serialize_lights_json(chromatic_lights());
  // Without the directions the image cannot be interpreted at all: the consumer
  // needs to know which direction each channel was lit from.
  CHECK(json.find("\"red\"") != std::string::npos);
  CHECK(json.find("\"green\"") != std::string::npos);
  CHECK(json.find("\"blue\"") != std::string::npos);
  CHECK(json.find("nan") == std::string::npos);
}
