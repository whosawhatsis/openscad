#include "io/chromatic.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace {

constexpr double normalize_component(double v, double len)
{
  return v / len;
}

ChromaticLight make_light(double x, double y, double z, int channel)
{
  const double len = std::sqrt(x * x + y * y + z * z);
  return ChromaticLight{
    {normalize_component(x, len), normalize_component(y, len), normalize_component(z, len)}, channel};
}

}  // namespace

std::array<ChromaticLight, 3> chromatic_lights()
{
  // Two low lights left and right and one from above: a spread wide enough that
  // the three channels disagree about most surfaces (which is what carries the
  // shape information), while all three keep a positive Z so that surfaces
  // facing the camera are lit by all of them and the image still reads as a
  // photograph rather than as three disjoint silhouettes.
  return {
    make_light(-0.6, -0.3, 1.0, 0),
    make_light(+0.6, -0.3, 1.0, 1),
    make_light(0.0, +0.7, 1.0, 2),
  };
}

GaugeImage render_gauge_sphere(std::uint32_t size)
{
  GaugeImage gauge;
  gauge.size = size;
  gauge.pixels.assign(static_cast<size_t>(size) * size * 4, 0);
  if (size == 0) return gauge;

  const auto lights = chromatic_lights();
  for (std::uint32_t y = 0; y < size; ++y) {
    for (std::uint32_t x = 0; x < size; ++x) {
      // Pixel centres, so the disc is symmetric about the image centre.
      const double u = (2.0 * (x + 0.5) / size) - 1.0;
      // Image rows run top-first while +Y is up, so v is flipped.
      const double v = 1.0 - (2.0 * (y + 0.5) / size);
      const double r2 = u * u + v * v;
      const size_t p = (static_cast<size_t>(y) * size + x) * 4;
      if (r2 > 1.0) continue;  // outside the disc: left transparent

      const double n[3] = {u, v, std::sqrt(1.0 - r2)};
      for (const auto& light : lights) {
        const double d = n[0] * light.dir[0] + n[1] * light.dir[1] + n[2] * light.dir[2];
        // Lambert, clamped: a surface turned away from a light receives nothing
        // from it and must not subtract from the others.
        const double shade = std::max(0.0, d);
        gauge.pixels[p + light.channel] =
          static_cast<std::uint8_t>(std::lround(std::min(1.0, shade) * 255.0));
      }
      gauge.pixels[p + 3] = 255;
    }
  }
  return gauge;
}

std::string serialize_lights_json(const std::array<ChromaticLight, 3>& lights)
{
  static const char *names[3] = {"red", "green", "blue"};
  std::ostringstream json;
  json.precision(17);
  json << "{\n";
  json << "  \"space\": \"eye\",\n";
  json << "  \"note\": \"unit directions toward each light, +Z toward the viewer\",\n";
  for (const auto& light : lights) {
    json << "  \"" << names[light.channel] << "\": [" << light.dir[0] << ", " << light.dir[1] << ", "
         << light.dir[2] << "],\n";
  }
  json << "  \"shading\": \"channel = max(0, dot(normal, direction))\"\n";
  json << "}\n";
  return json.str();
}
