#include "io/depthmap.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <string>
#include <cstdint>
#include <limits>
#include <sstream>
#include <vector>

namespace {

//! Reserved for pixels with no geometry, so a saturating real depth must stop
//! one short of it.
constexpr std::uint16_t METRIC_BACKGROUND = 65535;

void push_be16(std::vector<std::uint8_t>& pixels, std::uint16_t value)
{
  // PNG stores 16-bit samples big-endian.
  pixels.push_back(static_cast<std::uint8_t>(value >> 8));
  pixels.push_back(static_cast<std::uint8_t>(value & 0xff));
}

}  // namespace

DepthRange depth_range_for_bounds(double nearest, double farthest)
{
  DepthRange range;
  // Geometry behind the eye would make fog run backwards, so the near end
  // floors at zero.
  range.start = std::max(0.0, nearest);
  range.end = std::max(range.start, farthest);
  // GL_LINEAR fog divides by (end - start), so the two must never coincide - a
  // degenerate or zero-radius model would otherwise produce a division by zero.
  if (!(range.end > range.start)) range.end = range.start + 1.0;
  return range;
}

DepthRange resolve_depth_range(const DepthmapOptions& options, double nearest, double farthest)
{
  if (!options.has_explicit_range) return depth_range_for_bounds(nearest, farthest);
  // Reuse the same guards, so a range that reached here degenerate (it should
  // have been rejected at parse time) still cannot make fog divide by zero.
  return depth_range_for_bounds(options.explicit_near, options.explicit_far);
}

std::vector<float> linearize_depth(const std::vector<float>& windowDepth, double clipNear,
                                   double clipFar, bool perspective)
{
  std::vector<float> mm;
  mm.reserve(windowDepth.size());

  for (const float z : windowDepth) {
    // Anything at (or past) the far plane is empty space, not a surface.
    if (!(z < 1.0f)) {
      mm.push_back(std::numeric_limits<float>::infinity());
      continue;
    }
    double eye;
    if (perspective) {
      // Undo the hyperbolic projection: z_ndc = 2z - 1, and
      // eye = 2nf / (f + n - z_ndc(f - n)).
      const double ndc = 2.0 * z - 1.0;
      const double denom = clipFar + clipNear - ndc * (clipFar - clipNear);
      if (denom == 0.0) {
        mm.push_back(std::numeric_limits<float>::infinity());
        continue;
      }
      eye = 2.0 * clipNear * clipFar / denom;
    } else {
      // Orthographic depth is already linear in eye distance.
      eye = clipNear + z * (clipFar - clipNear);
    }
    // Both projections report distance from the eye. Neither near plane is a
    // usable origin: the orthographic one sits 100*dist *behind* the eye, and
    // the perspective one at 0.1*dist put the two projections that far apart for
    // the same model. The eye is the one origin they share, and it is what the
    // viewport shading measures from too. Geometry behind the eye stays negative
    // here rather than being clamped; the metric profile floors it at 0.
    mm.push_back(static_cast<float>(eye));
  }

  return mm;
}

DepthImage encode_depthmap(const std::vector<float>& depths, std::uint32_t width, std::uint32_t height,
                           DepthProfile profile)
{
  const size_t count = std::min(depths.size(), static_cast<size_t>(width) * height);

  DepthImage img;
  // Visual is RGBA rather than RGB because that is what write_png() consumes on
  // both backends - the CoreGraphics writer is kCGImageAlphaNoneSkipLast, and
  // lodepng's info_raw defaults to RGBA8. The alpha is dropped on the way out.
  img.bytesPerPixel = depth_units_per_mm(profile) > 0.0 ? 2 : 4;
  img.pixels.reserve(count * img.bytesPerPixel);

  bool any = false;
  for (size_t i = 0; i < count; ++i) {
    if (!std::isfinite(depths[i])) continue;
    const double d = depths[i];
    if (!any) {
      img.minDepth = img.maxDepth = d;
      any = true;
    } else {
      img.minDepth = std::min(img.minDepth, d);
      img.maxDepth = std::max(img.maxDepth, d);
    }
  }
  if (!any) img.minDepth = img.maxDepth = 0.0;

  if (const double units = depth_units_per_mm(profile); units > 0.0) {
    for (size_t i = 0; i < count; ++i) {
      if (!std::isfinite(depths[i])) {
        push_be16(img.pixels, METRIC_BACKGROUND);
        continue;
      }
      const double scaled = std::lround(depths[i] * units);
      // Clamp rather than let the cast wrap: a model deeper than ~65m saturates
      // just below the background value so it stays distinguishable from no-data.
      const double clamped = std::clamp(scaled, 0.0, static_cast<double>(METRIC_BACKGROUND - 1));
      push_be16(img.pixels, static_cast<std::uint16_t>(clamped));
    }
  } else {
    // Normalizing over a zero extent would divide by zero - a face perpendicular
    // to the view is a single depth, and the whole of it is "nearest".
    const double extent = img.maxDepth - img.minDepth;
    for (size_t i = 0; i < count; ++i) {
      std::uint8_t grey = 0;  // background is black, matching MiDaS-shaped output
      if (std::isfinite(depths[i])) {
        const double t = extent > 0.0 ? (img.maxDepth - depths[i]) / extent : 1.0;
        grey = static_cast<std::uint8_t>(std::lround(t * 255.0));
      }
      img.pixels.insert(img.pixels.end(), 3, grey);
      img.pixels.push_back(255);  // opaque; write_png() discards it
    }
  }

  return img;
}

std::string serialize_camera_json(const CameraParameters& cam)
{
  std::ostringstream ss;
  ss << "{\n";
  ss << "  \"modelview\": [";
  for (int i = 0; i < 16; ++i) {
    ss << cam.modelview[i] << (i < 15 ? ", " : "");
  }
  ss << "],\n";
  ss << "  \"projection\": [";
  for (int i = 0; i < 16; ++i) {
    ss << cam.projection[i] << (i < 15 ? ", " : "");
  }
  ss << "],\n";
  ss << "  \"clipNear\": " << cam.clipNear << ",\n";
  ss << "  \"clipFar\": " << cam.clipFar << ",\n";
  ss << "  \"fov\": " << cam.fov << ",\n";
  ss << "  \"ortho\": " << (cam.ortho ? "true" : "false") << ",\n";
  ss << "  \"viewport\": [" << cam.viewport[0] << ", " << cam.viewport[1] << "],\n";
  // Sixteen bare numbers are ambiguous, and every wrong guess produces a
  // plausible-looking reconstruction rather than an obvious failure: glGetDoublev
  // returns column-major while JSON consumers tend to assume row-major, and the
  // depth origin moved to the eye for both projections. State all of it.
  ss << "  \"matrixOrder\": \"column-major\",\n";
  ss << "  \"handedness\": \"right\",\n";
  ss << "  \"depthOrigin\": \"eye\",\n";
  ss << "  \"depthUnits\": \"mm\"\n";
  ss << "}\n";
  return ss.str();
}

bool parse_depth_range(const std::string& text, double& near, double& far, std::string& error)
{
  // stod throws on junk, which used to abort the process; check before parsing.
  auto to_double = [](std::string t, double& out) {
    const auto first = t.find_first_not_of(" \t");
    if (first == std::string::npos) return false;
    t = t.substr(first, t.find_last_not_of(" \t") - first + 1);
    try {
      size_t used = 0;
      out = std::stod(t, &used);
      return used == t.size();
    } catch (const std::exception&) {
      return false;
    }
  };

  const auto comma = text.find(',');
  if (comma == std::string::npos) {
    // A single number is the far value with near defaulting to 0 - "everything
    // past this distance is background" is the common case, and it is what
    // most users will type first before discovering the near,far form.
    double f = 0;
    if (!to_double(text, f)) {
      error = "expected a number, or 'near,far'";
      return false;
    }
    if (!(0.0 < f)) {
      error = "near must be less than far";
      return false;
    }
    near = 0.0;
    far = f;
    error.clear();
    return true;
  }
  if (text.find(',', comma + 1) != std::string::npos) {
    error = "expected exactly two values";
    return false;
  }

  double n = 0, f = 0;
  if (!to_double(text.substr(0, comma), n) || !to_double(text.substr(comma + 1), f)) {
    error = "both values must be numbers";
    return false;
  }
  if (!(n < f)) {
    // An inverted or empty range silently produced an all-white visual image.
    error = "near must be less than far";
    return false;
  }

  near = n;
  far = f;
  error.clear();
  return true;
}

DepthImage encode_depthmap(const std::vector<float>& depths, std::uint32_t width, std::uint32_t height,
                           const DepthmapOptions& options)
{
  if (!options.has_explicit_range) {
    return encode_depthmap(depths, width, height, options.profile);
  }

  const size_t count = std::min(depths.size(), static_cast<size_t>(width) * height);
  DepthImage img;
  img.bytesPerPixel = depth_units_per_mm(options.profile) > 0.0 ? 2 : 4;
  img.pixels.reserve(count * img.bytesPerPixel);

  // Report what was actually in the scene, not what was asked for, and say
  // whether the range cut anything off.
  bool any = false;
  for (size_t i = 0; i < count; ++i) {
    if (!std::isfinite(depths[i])) continue;
    const double d = depths[i];
    if (!any) {
      img.minDepth = img.maxDepth = d;
      any = true;
    } else {
      img.minDepth = std::min(img.minDepth, d);
      img.maxDepth = std::max(img.maxDepth, d);
    }
    if (d < options.explicit_near || d > options.explicit_far) img.clipped = true;
  }
  if (!any) img.minDepth = img.maxDepth = 0.0;

  if (const double units = depth_units_per_mm(options.profile); units > 0.0) {
    for (size_t i = 0; i < count; ++i) {
      if (!std::isfinite(depths[i])) {
        push_be16(img.pixels, METRIC_BACKGROUND);
        continue;
      }
      double d = std::clamp(static_cast<double>(depths[i]), options.explicit_near, options.explicit_far);
      const double scaled = std::lround(d * units);
      const double clamped = std::clamp(scaled, 0.0, static_cast<double>(METRIC_BACKGROUND - 1));
      push_be16(img.pixels, static_cast<std::uint16_t>(clamped));
    }
  } else {
    const double extent = options.explicit_far - options.explicit_near;
    for (size_t i = 0; i < count; ++i) {
      std::uint8_t grey = 0;
      if (std::isfinite(depths[i])) {
        double d =
          std::clamp(static_cast<double>(depths[i]), options.explicit_near, options.explicit_far);
        const double t = extent > 0.0 ? (options.explicit_far - d) / extent : 1.0;
        grey = static_cast<std::uint8_t>(std::lround(t * 255.0));
      }
      img.pixels.insert(img.pixels.end(), 3, grey);
      img.pixels.push_back(255);
    }
  }

  return img;
}

bool export_pfm(std::ostream& out, const std::vector<float>& depths, std::uint32_t width,
                std::uint32_t height)
{
  if (width == 0 || height == 0) return false;
  // Negative scale means little-endian samples. Derived rather than hardcoded so
  // the header cannot quietly disagree with the bytes that follow it.
  const bool little_endian = []() {
    const std::uint32_t probe = 1;
    unsigned char first;
    std::memcpy(&first, &probe, 1);
    return first == 1;
  }();
  out << "Pf\n" << width << " " << height << "\n" << (little_endian ? "-1.0" : "1.0") << "\n";

  // Array order *is* PFM order: both are bottom row first. See the header.
  const float bg = std::numeric_limits<float>::infinity();
  const size_t count = static_cast<size_t>(width) * height;
  for (size_t idx = 0; idx < count; ++idx) {
    const float val = (idx < depths.size() && std::isfinite(depths[idx])) ? depths[idx] : bg;
    out.write(reinterpret_cast<const char *>(&val), sizeof(float));
  }
  return out.good();
}

DepthRange capped_sphere_range(const double bboxMin[3], const double bboxMax[3],
                               const double rotationCentre[3], const double modelview[16])
{
  // The sphere sits on the rotation centre and is grown until it contains every
  // corner of the box, rather than being the box's own bounding sphere.
  double radius2 = 0.0;
  for (int c = 0; c < 8; ++c) {
    double d2 = 0.0;
    for (int i = 0; i < 3; ++i) {
      const double corner = (c & (1 << i)) ? bboxMax[i] : bboxMin[i];
      const double delta = corner - rotationCentre[i];
      d2 += delta * delta;
    }
    radius2 = std::max(radius2, d2);
  }
  const double radius = std::sqrt(radius2);
  const double *centre = rotationCentre;

  // Distance from the eye to the sphere centre; -z is in front of the eye.
  const double dist =
    -(modelview[2] * centre[0] + modelview[6] * centre[1] + modelview[10] * centre[2] + modelview[14]);

  // Cap the radius so the near end stays in front of the eye however large the
  // model is relative to the camera.
  //
  // The cap must bind as rarely as possible, because while it binds it destroys
  // the invariance this range exists for: [d-R, d+R] makes a surface's shade
  // independent of d (the d cancels), but a radius derived from d does not, so
  // every camera move rescales the image. Measured on a 43x711x60 model, a
  // diameter capped at d rescaled the shading at every realistic viewing
  // distance; capping the radius just short of d leaves it invariant there and
  // still keeps the near end off the eye.
  const double capped = std::min(radius, DEPTH_NEAR_MARGIN * std::max(0.0, dist));
  return depth_range_for_bounds(dist - capped, dist + capped);
}
