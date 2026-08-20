#pragma once

#include <cstdint>
#include <string>
#include <vector>

/*!
   Encoding of a rendered depth buffer into PNG-ready pixels.

   Two profiles, because the two families of depth-map consumer disagree on
   every axis except the curve:

   - metric: 16-bit big-endian grey, linear distance from the camera in
     millimetres scaled by DEPTHMAP_METRIC_SCALE, near = dark, background =
     65535 (farthest). Read directly by Kinect/OpenNI-style tooling, ROS,
     Open3D and PCL.
   - metricFine: as metric, but 10um units instead of 1mm - 655mm of range
     instead of 65.5m, and a hundred times the depth resolution. See
     DEPTHMAP_FINE_SCALE for why this is a separate profile and not the default.
   - visual: 8-bit RGB grey, normalized across the model's own depth extent,
     near = bright, background = black. This is what ControlNet depth and
     general image tooling expect, having been trained on MiDaS output.

   The input is linear distance **from the near plane** in millimetres, not from
   the eye: the eye sits at Camera::zoomValue() along -Y and --viewall moves it,
   so eye-relative values would shift with the zoom level and the same model
   would encode differently at two zooms. Pixels with no geometry are marked by
   a non-finite value (infinity), not by a sentinel number, so that no real
   distance can be mistaken for background.
 */

enum class DepthProfile : std::uint8_t {
  metric,
  metricFine,
  visual,
};

//! Units per millimetre in the metric profile: 1 => 1mm, matching the
//! prevailing Kinect/ROS convention. (TUM's 5000-per-metre variant exists but
//! is a dataset-specific correction, not a convention to follow.)
inline constexpr double DEPTHMAP_METRIC_SCALE = 1.0;

/*!
   Units per millimetre in the fine metric profile: 100 => 10um.

   16 bits buys a fixed product of range and resolution, and the millimetre
   profile spends nearly all of it on distances no OpenSCAD scene occupies.
   Depth is measured from the eye, and measured eye distances for ordinary models
   run from tens to a few hundred millimetres - so a 15mm-deep object viewed from
   43mm away spans 15 of 65535 levels, under 4 bits of depth in a 16-bit file.
   At 10um the same object spans 1560.

   The ceiling falls from 65.5m to 655.34mm, which is why this is a second
   profile rather than a new default: a scene framed from further away than that
   clamps, and `metric` remains the one that matches what Kinect/ROS-shaped
   tooling assumes when it reads a depth PNG as millimetres. 1um was considered
   and rejected - its 65.5mm ceiling clamps an ordinary 120mm part viewed from a
   normal distance.
 */
inline constexpr double DEPTHMAP_FINE_SCALE = 100.0;

/*!
   Units per millimetre for a profile, or 0 for the profiles that do not encode
   absolute distance.

   A non-zero answer also means the output is 16-bit: 8 bits of absolute depth is
   not worth offering, since 256 levels across any range wide enough to hold a
   scene is coarser than the geometry being described. Two things depend on that
   invariant - the encoder's bytes-per-pixel, and the embedded metadata, which is
   only possible on the 16-bit writer.
 */
inline constexpr double depth_units_per_mm(DepthProfile profile)
{
  switch (profile) {
  case DepthProfile::metric:     return DEPTHMAP_METRIC_SCALE;
  case DepthProfile::metricFine: return DEPTHMAP_FINE_SCALE;
  default:                       return 0.0;
  }
}

struct DepthPreviewPolarity {
  float geometry;
  float background;
};

inline constexpr DepthPreviewPolarity depth_preview_polarity(double units_per_mm)
{
  return units_per_mm > 0.0 ? DepthPreviewPolarity{0.0f, 1.0f} : DepthPreviewPolarity{1.0f, 0.0f};
}

struct DepthImage {
  //! Row-major pixel data, ready to hand to a PNG writer.
  std::vector<std::uint8_t> pixels;
  //! Bytes per pixel: 2 for metric (16-bit grey), 3 for visual (8-bit RGB).
  std::uint8_t bytesPerPixel = 0;
  //! The finite depth extent actually found, in millimetres. Both are 0 when
  //! the buffer held no geometry at all. These stay truthful even when an
  //! explicit range is in force - reporting the requested range back would hide
  //! precisely the fact an explicit range most needs to surface.
  double minDepth = 0.0;
  double maxDepth = 0.0;
  //! Set when an explicit range clamped real geometry, so the caller can warn.
  bool clipped = false;
};

/*!
   Encode linear camera-space depths (millimetres, non-finite where there is no
   geometry) into pixels for the given profile.
 */
DepthImage encode_depthmap(const std::vector<float>& depths, std::uint32_t width, std::uint32_t height,
                           DepthProfile profile);

/*!
   Convert window-space depth (as glReadPixels(GL_DEPTH_COMPONENT) returns it,
   in [0,1]) to millimetres.

   Distance is measured from the eye in both projections, so the same model
   exports the same numbers however it is projected, and matches what the
   viewport depth shading shows. Neither near plane is a usable origin: the
   orthographic one sits 100*dist behind the eye (a large offset unrelated to
   the model, which also overflows the metric profile's 65535mm ceiling for
   models over roughly 328 units), and the perspective one at 0.1*dist put the
   two projections that far apart for the same geometry. Depths behind the eye
   come back negative rather than clamped.

   Pixels at the far plane are background and come back as infinity, which is
   what encode_depthmap() expects. Orthographic depth is linear in eye distance;
   perspective depth is hyperbolic and has to be unprojected, which is where the
   precision hazard of a wide near/far ratio actually bites.
 */
std::vector<float> linearize_depth(const std::vector<float>& windowDepth, double clipNear,
                                   double clipFar, bool perspective);

//! Eye-space distances that the viewport depth shading maps across.
//! How close to the eye the near end of the depth range may come, as a fraction
//! of the distance to the model centre. Deliberately close to 1: the cap breaks
//! the range's distance invariance while it binds, so it should engage only when
//! the model is nearly as large as its own viewing distance.
inline constexpr double DEPTH_NEAR_MARGIN = 0.95;

struct DepthRange {
  double start = 0.0;
  double end = 0.0;
};

/*!
   The depth range the shading normalizes across: a sphere centred on the
   **viewport's centre of rotation** and large enough to contain the model, with
   its radius capped so the near end stays in front of the eye.

   **Orientation invariant by construction**, which is the point - a sphere
   presents the same depth extent from every direction, so turning the model no
   longer rebalances the image. That invariance is not free and the cost is not
   an implementation detail: any range that is both deterministic and
   rotation-invariant must be at least the largest extent the box can present,
   which *is* the sphere diameter. On a long model (measured on a 43 x 711 x 60
   one) the side view therefore grades over ~16x more range than its own depth
   needs, leaving roughly 15 of the 255 grey levels in use. That is the accepted
   trade; `-O depthmap/range=near,far` overrides it, and the metric profile does
   not use a range at all.

   Centred on the rotation centre rather than the model's own centre, because the
   camera orbits *that* point: the eye-to-centre distance is then constant under
   rotation, and so is the radius the cap derives from it. Centred on the model
   instead, an orbit around any other point changes that distance, and while the
   cap is binding the whole image rescales as you turn the model - which is
   exactly the instability this range exists to remove. The sphere is grown to
   enclose the bounding box from that centre, so panning away from the model
   costs contrast rather than correctness.

   **The cap** (`R' = min(R, 0.95 d)`) keeps the near end off the eye: a model
   larger than its own viewing distance would otherwise put `start` behind the
   camera, where the gradient stops meaning anything. When the cap binds, geometry
   outside the range is clamped rather than wrapped - near of `start` reads pure
   white, beyond `end` pure black - so an out-of-range surface saturates instead
   of reversing.
 */
DepthRange capped_sphere_range(const double bboxMin[3], const double bboxMax[3],
                               const double rotationCentre[3], const double modelview[16]);

/*!
   The depth-to-grey mapping for the viewport, built from the eye-space depth
   extent of the model's bounding box - pinned to the model, not recomputed from
   what happens to be on screen, so the shading does not swim as the model is
   rotated.

   Fed to GL_LINEAR fog, whose distance is eye-space and linear, so the viewport
   needs none of linearize_depth()'s unprojection.
 */
DepthRange depth_range_for_bounds(double nearest, double farthest);

/*!
   The range the viewport shading should use: an explicit range if the user gave
   one, otherwise the bounding-box extent. An explicit range is a stronger
   statement than "fit the model", and it is what the export honours - so the
   viewport defers to it, or preview and file disagree exactly when the user
   asked for them to agree.
 */
DepthRange resolve_depth_range(const struct DepthmapOptions& options, double nearest, double farthest);

struct CameraParameters {
  double modelview[16]{};
  double projection[16]{};
  double clipNear = 0.0;
  double clipFar = 0.0;
  double fov = 0.0;
  bool ortho = false;
  int viewport[2]{};
};

std::string serialize_camera_json(const CameraParameters& cam);

struct DepthmapOptions {
  DepthProfile profile = DepthProfile::metric;
  //! True when the range below was derived from the model rather than typed by
  //! the user. Only affects how a clamping warning is worded - a user who did
  //! not ask for a range should not be told their request clipped something.
  bool range_from_model = false;
  std::string camera_sidecar_path;
  bool has_explicit_range = false;
  double explicit_near = 0.0;
  double explicit_far = 0.0;
};

DepthImage encode_depthmap(const std::vector<float>& depths, std::uint32_t width, std::uint32_t height,
                           const DepthmapOptions& options);

/*!
   Write depths as a PFM (greyscale float) stream.

   `depths` arrives in the orientation glReadPixels produced, bottom row first,
   which is already PFM's own row order - so the payload is written in array
   order. Do not "flip for PFM": that undoes a flip this path never applied and
   writes the image upside down.
 */
bool export_pfm(std::ostream& out, const std::vector<float>& depths, std::uint32_t width,
                std::uint32_t height);

/*!
   Parse a "near,far" depth range, or a single number as far with near
   defaulting to 0. Returns false and sets `error` for anything unusable -
   non-numeric, more than one comma, inverted, or zero extent - so the caller
   can report it rather than crash on it or silently drop it.
 */
bool parse_depth_range(const std::string& text, double& near, double& far, std::string& error);
