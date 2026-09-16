#include "io/coordinatemap.h"

#include <sstream>
#include <string>

CoordinateBounds coordinate_bounds(const double min[3], const double max[3])
{
  CoordinateBounds bounds;
  for (int i = 0; i < 3; ++i) {
    bounds.min[i] = min[i];
    bounds.max[i] = max[i];
    const double extent = max[i] - min[i];
    bounds.degenerate[i] = !(extent > 0.0);
    bounds.extent[i] = bounds.degenerate[i] ? 1.0 : extent;
  }
  return bounds;
}

void normalize_point(const CoordinateBounds& bounds, const double point[3], double out[3])
{
  for (int i = 0; i < 3; ++i) {
    if (bounds.degenerate[i]) {
      out[i] = 0.5;
      continue;
    }
    const double t = (point[i] - bounds.min[i]) / bounds.extent[i];
    out[i] = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
  }
}

std::string serialize_bounds_json(const CoordinateBounds& bounds)
{
  std::ostringstream json;
  json.precision(17);
  json << "{\n";
  json << "  \"min\": [" << bounds.min[0] << ", " << bounds.min[1] << ", " << bounds.min[2] << "],\n";
  json << "  \"max\": [" << bounds.max[0] << ", " << bounds.max[1] << ", " << bounds.max[2] << "],\n";
  json << "  \"encoding\": \"coordinate = min + channel * (max - min)\"\n";
  json << "}\n";
  return json.str();
}
