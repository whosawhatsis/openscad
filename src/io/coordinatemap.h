#pragma once

#include <string>

/*!
   Encoding of model coordinates into the RGB channels of a rendered image.

   The coordinate map answers "which point of the model is this pixel?", the
   companion question to the depth map's "how far away is it?". Raw object
   coordinates cannot be written to a color channel directly: GL clamps color
   to [0,1], so every model larger than one unit - which is nearly all of them -
   saturates into flat blocks. So the model's own bounding box is normalized
   into the unit cube, and the box is written alongside the image, exactly as
   the depth map writes its camera sidecar. Without the box a pixel cannot be
   decoded back to a coordinate, and the image is a picture rather than data.

   Decode: `coordinate = min + channel * (max - min)`, per axis.
 */
struct CoordinateBounds {
  //! The real bounding box, as reported - what a consumer decodes against.
  double min[3]{};
  double max[3]{};
  //! Per-axis divisor used for normalization. Never zero: a degenerate axis
  //! (a flat, 2D-ish model) gets 1 so the division is safe, and that axis is
  //! pinned to 0.5 rather than being allowed to produce NaN or saturate.
  double extent[3]{};
  bool degenerate[3]{};
};

/*!
   Build the normalization for a bounding box given as two corners.
   `min`/`max` may be equal on any axis; that axis becomes degenerate.
 */
CoordinateBounds coordinate_bounds(const double min[3], const double max[3]);

/*!
   Map a model-space point into [0,1] per axis, clamping outside the box.

   Clamping rather than wrapping matters: a wrapped coordinate is
   indistinguishable from a real one on the far side of the model, so an out-of-
   box point would decode to a confidently wrong position.
 */
void normalize_point(const CoordinateBounds& bounds, const double point[3], double out[3]);

//! The sidecar contents: the box a pixel decodes against, as JSON.
std::string serialize_bounds_json(const CoordinateBounds& bounds);
