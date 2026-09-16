#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

/*!
   Chromatic three-point lighting: the model lit by three colored directional
   lights from three known directions, one per color channel.

   This is the lighting used to produce calibrated photometric-stereo images, and
   it is here for interpretation rather than reconstruction. An ordinary
   single-key render collapses curvature and orientation into one ambiguous grey
   ramp; here each channel is an independent shading observation of the same
   surface, so orientation stays separable - while the result is still an
   ordinary-looking image rather than a synthetic false-color one. The normal
   map is exact; this is legible. They answer to different readers.

   The directions are fixed, documented and emitted alongside the image: a
   consumer that does not know which direction lit which channel cannot use it
   for anything. They are given in eye space (+Z toward the viewer), so they are
   stable relative to the camera however the model is turned.
 */

struct ChromaticLight {
  //! Unit direction toward the light, in eye space (+Z toward the viewer).
  double dir[3]{};
  //! Which color channel this light drives: 0 = red, 1 = green, 2 = blue.
  int channel = 0;
};

/*!
   The three lights. Deliberately non-coplanar as vectors - three coplanar
   directions would make the channels linearly dependent, which is exactly the
   ambiguity the mode exists to remove.
 */
std::array<ChromaticLight, 3> chromatic_lights();

struct GaugeImage {
  //! Row-major RGBA, top row first.
  std::vector<std::uint8_t> pixels;
  std::uint32_t size = 0;
};

/*!
   The calibration gauge: an ideal sphere under the same three lights, which
   gives the reader a lookup from shading color back to surface orientation.

   Computed analytically, never tessellated. A faceted sphere would bake $fn
   banding into the one image whose whole job is to be a reference - the gauge
   would then describe the tessellation rather than the lighting. For a pixel at
   (u,v) inside the unit disc the exact normal is (u, v, sqrt(1 - u^2 - v^2)).

   Outside the disc the pixels are transparent rather than black, because black
   is a real shading value - a surface facing away from every light - and an
   opaque black surround would read as part of the sphere.
 */
GaugeImage render_gauge_sphere(std::uint32_t size);

//! The light directions, as JSON, to be written beside the image.
std::string serialize_lights_json(const std::array<ChromaticLight, 3>& lights);
