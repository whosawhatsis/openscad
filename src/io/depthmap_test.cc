#include "io/depthmap.h"

#include <catch2/catch_all.hpp>

#include <array>
#include <cmath>
#include <cstring>
#include <sstream>
#include <string>
#include <cstdint>
#include <limits>
#include <vector>

namespace {

//! Rotation centre for the cases below: the models are centred on the origin,
//! which is also where the viewport rotates about after a View All.
const double kOrigin[3] = {0.0, 0.0, 0.0};

constexpr float BG = std::numeric_limits<float>::infinity();

std::uint16_t grey16(const DepthImage& img, size_t pixel)
{
  // PNG stores 16-bit samples big-endian.
  return static_cast<std::uint16_t>(img.pixels[pixel * 2] << 8 | img.pixels[pixel * 2 + 1]);
}

}  // namespace

TEST_CASE("metric profile encodes linear millimetres, near dark", "[Depthmap]")
{
  // A 2x2 buffer: three depths plus one background pixel.
  const std::vector<float> depths = {10.0f, 20.0f, 100.0f, BG};
  const auto img = encode_depthmap(depths, 2, 2, DepthProfile::metric);

  REQUIRE(img.bytesPerPixel == 2);
  REQUIRE(img.pixels.size() == 4 * 2);

  // Value is the distance itself, so it survives a round trip to millimetres.
  CHECK(grey16(img, 0) == 10);
  CHECK(grey16(img, 1) == 20);
  CHECK(grey16(img, 2) == 100);
  // Background is the far end, and is not confusable with a near surface.
  CHECK(grey16(img, 3) == 65535);

  CHECK(img.minDepth == Catch::Approx(10.0));
  CHECK(img.maxDepth == Catch::Approx(100.0));
}

TEST_CASE("metric profile clamps beyond the representable range", "[Depthmap]")
{
  const std::vector<float> depths = {-5.0f, 70000.0f};
  const auto img = encode_depthmap(depths, 2, 1, DepthProfile::metric);

  // Nothing wraps: a negative depth floors at 0, an overlong one saturates
  // just below the background value so it stays distinguishable from no-data.
  CHECK(grey16(img, 0) == 0);
  CHECK(grey16(img, 1) == 65534);
}

TEST_CASE("visual profile normalizes to the model extent, near bright", "[Depthmap]")
{
  const std::vector<float> depths = {10.0f, 20.0f, 30.0f, BG};
  const auto img = encode_depthmap(depths, 2, 2, DepthProfile::visual);

  // RGBA, because that is what write_png() consumes on both backends: the
  // CoreGraphics writer is kCGImageAlphaNoneSkipLast and lodepng's info_raw
  // defaults to RGBA8. Alpha is dropped on the way out to a 3-channel PNG.
  REQUIRE(img.bytesPerPixel == 4);
  REQUIRE(img.pixels.size() == 4 * 4);

  // Nearest surface saturates white, farthest goes black, and the range is
  // stretched across the model's own extent rather than the clip planes.
  CHECK(img.pixels[0] == 255);
  CHECK(img.pixels[4] == 128);
  CHECK(img.pixels[8] == 0);
  // Background is black too - indistinguishable from the far surface, which is
  // what MiDaS-trained consumers expect.
  CHECK(img.pixels[12] == 0);

  // Grey, so a consumer reading only one channel gets the same answer.
  CHECK(img.pixels[0] == img.pixels[1]);
  CHECK(img.pixels[1] == img.pixels[2]);
  CHECK(img.pixels[3] == 255);  // opaque

  CHECK(img.minDepth == Catch::Approx(10.0));
  CHECK(img.maxDepth == Catch::Approx(30.0));
}

TEST_CASE("visual profile handles a model of zero depth extent", "[Depthmap]")
{
  // A flat face perpendicular to the view: min == max, so normalizing would
  // divide by zero.
  const std::vector<float> depths = {42.0f, 42.0f, BG, BG};
  const auto img = encode_depthmap(depths, 2, 2, DepthProfile::visual);

  CHECK(img.pixels[0] == 255);
  CHECK(img.pixels[4] == 255);
  CHECK(img.pixels[8] == 0);
}

TEST_CASE("orthographic depth is measured from the eye, not the near plane", "[Depthmap]")
{
  // Near/far here are the -100*dist/+100*dist that GLView::setupCamera uses for
  // an orthographic camera. That near plane sits 100*dist *behind* the eye, so
  // measuring from it would offset every value by a distance that has nothing to
  // do with the model - and would overflow the metric profile's 65535mm ceiling
  // for any model over roughly 328 units. Orthographic depth is therefore
  // reported from the eye, where the numbers mean something.
  const std::vector<float> window = {0.5f, 0.75f, 0.9f};
  const auto mm = linearize_depth(window, -100.0, 100.0, false);

  REQUIRE(mm.size() == 3);
  CHECK(mm[0] == Catch::Approx(0.0));   // halfway through the clip range is the eye
  CHECK(mm[1] == Catch::Approx(50.0));  // linear in eye distance, so no curve to undo
  CHECK(mm[2] == Catch::Approx(80.0));
}

TEST_CASE("orthographic geometry behind the eye reads as negative", "[Depthmap]")
{
  // The orthographic near plane is behind the eye, so the clip volume genuinely
  // contains negative eye distances. They are left negative here rather than
  // clamped, so the caller can see them; the metric profile floors them at 0.
  const auto mm = linearize_depth({0.25f}, -100.0, 100.0, false);
  CHECK(mm[0] == Catch::Approx(-50.0));
}

TEST_CASE("perspective depth unprojects through the hyperbolic curve", "[Depthmap]")
{
  // n=1, f=101. A surface at eye distance d has window depth
  // f*(d-n) / ((f-n)*d), so d=2 gives 101/200 = 0.505.
  //
  // Measured from the eye, exactly as the orthographic case is: measuring from
  // the near plane instead put the two projections 0.1*dist apart for the same
  // model, and left the viewport shading (which is eye-relative in both)
  // agreeing with only one of them.
  const std::vector<float> window = {0.0f, 0.505f};
  const auto mm = linearize_depth(window, 1.0, 101.0, true);

  REQUIRE(mm.size() == 2);
  CHECK(mm[0] == Catch::Approx(1.0));  // on the near plane, which is 1 from the eye
  // The margin is loose because window depth arrives as a float and the curve is
  // steep here - the precision hazard of a wide near/far ratio, visible even in
  // this toy case.
  CHECK(mm[1] == Catch::Approx(2.0).margin(1e-4));
}

TEST_CASE("the far plane is background, not a real distance", "[Depthmap]")
{
  const std::vector<float> window = {1.0f, 0.5f};

  const auto ortho = linearize_depth(window, -100.0, 100.0, false);
  CHECK(std::isinf(ortho[0]));
  CHECK(std::isfinite(ortho[1]));

  const auto persp = linearize_depth(window, 1.0, 101.0, true);
  CHECK(std::isinf(persp[0]));
  CHECK(std::isfinite(persp[1]));
}

TEST_CASE("the viewport depth range spans the bounding box extent", "[Depthmap]")
{
  // Taken from the model's own eye-space extent, not from what is on screen, so
  // rotating the model does not change the shading of a surface that did not move.
  const auto r = depth_range_for_bounds(90.0, 110.0);
  CHECK(r.start == Catch::Approx(90.0));
  CHECK(r.end == Catch::Approx(110.0));
}

TEST_CASE("the viewport depth range survives the camera being inside the model", "[Depthmap]")
{
  // Zoomed in past the model's near side: the near end would go negative, which
  // fog will not accept, so it floors at zero without collapsing the range.
  const auto r = depth_range_for_bounds(-5.0, 15.0);
  CHECK(r.start == Catch::Approx(0.0));
  CHECK(r.end == Catch::Approx(15.0));
  CHECK(r.end > r.start);
}

TEST_CASE("the viewport depth range never collapses to a point", "[Depthmap]")
{
  // A zero-radius bound (a single point, or a degenerate model) would make fog
  // divide by zero.
  const auto r = depth_range_for_bounds(50.0, 50.0);
  CHECK(r.end > r.start);
}

TEST_CASE("an empty view yields background everywhere", "[Depthmap]")
{
  const std::vector<float> depths = {BG, BG};

  const auto metric = encode_depthmap(depths, 2, 1, DepthProfile::metric);
  CHECK(grey16(metric, 0) == 65535);
  CHECK(grey16(metric, 1) == 65535);
  CHECK(metric.minDepth == 0.0);
  CHECK(metric.maxDepth == 0.0);

  const auto visual = encode_depthmap(depths, 2, 1, DepthProfile::visual);
  CHECK(visual.pixels[0] == 0);
  CHECK(visual.pixels[4] == 0);
}

TEST_CASE("Feature 22: camera parameters serialize to valid JSON", "[Depthmap]")
{
  CameraParameters cam;
  cam.modelview[0] = 1.0;
  cam.modelview[5] = 1.0;
  cam.modelview[10] = 1.0;
  cam.modelview[15] = 1.0;
  cam.projection[0] = 2.0;
  cam.clipNear = 10.0;
  cam.clipFar = 500.0;
  cam.fov = 45.0;
  cam.ortho = false;
  cam.viewport[0] = 800;
  cam.viewport[1] = 600;

  std::string json = serialize_camera_json(cam);
  CHECK(json.find("\"modelview\"") != std::string::npos);
  CHECK(json.find("\"projection\"") != std::string::npos);
  CHECK(json.find("\"clipNear\": 10") != std::string::npos);
  CHECK(json.find("\"clipFar\": 500") != std::string::npos);
}

TEST_CASE("Feature 23: explicit depth range overrides dynamic bounds", "[Depthmap]")
{
  const std::vector<float> depths = {5.0f, 50.0f, 150.0f, BG};
  DepthmapOptions opts;
  opts.profile = DepthProfile::metric;
  opts.has_explicit_range = true;
  opts.explicit_near = 10.0;
  opts.explicit_far = 100.0;

  const auto img = encode_depthmap(depths, 2, 2, opts);
  // 5.0 is below explicit_near (10.0) -> clamps to explicit_near (10mm)
  CHECK(grey16(img, 0) == 10);
  // 50.0 is inside range -> 50mm
  CHECK(grey16(img, 1) == 50);
  // 150.0 is above explicit_far (100.0) -> clamps to explicit_far (100mm)
  CHECK(grey16(img, 2) == 100);
}

TEST_CASE("Feature 24: float depth exports to PFM format stream", "[Depthmap]")
{
  const std::vector<float> depths = {10.5f, 20.25f, 30.0f, BG};
  std::ostringstream ss;
  bool ok = export_pfm(ss, depths, 2, 2);
  REQUIRE(ok);
  std::string str = ss.str();
  CHECK(str.substr(0, 3) == "Pf\n");
  CHECK(str.find("2 2\n") != std::string::npos);
  CHECK(str.find("-1.0\n") != std::string::npos);
}

TEST_CASE("PFM rows are written in the order the format expects", "[Depthmap]")
{
  // The depths handed to export_pfm come straight from glReadPixels, which is
  // already bottom-to-top - and bottom-to-top is exactly PFM's own row order.
  // So the payload must come out in array order, untouched. Reversing here
  // would undo a flip that was never applied and write the image upside down.
  const std::vector<float> depths = {1.0f, 2.0f, 3.0f, 4.0f};  // row 0 = {1,2} = bottom
  std::ostringstream out;
  REQUIRE(export_pfm(out, depths, 2, 2));

  const std::string s = out.str();
  const size_t payload = s.size() - 2 * 2 * sizeof(float);
  std::vector<float> got(4);
  std::memcpy(got.data(), s.data() + payload, 4 * sizeof(float));

  CHECK(got[0] == 1.0f);
  CHECK(got[1] == 2.0f);
  CHECK(got[2] == 3.0f);
  CHECK(got[3] == 4.0f);
}

TEST_CASE("a depth range is parsed only when it is usable", "[Depthmap]")
{
  double near = 0, far = 0;
  std::string err;

  CHECK(parse_depth_range("80,90", near, far, err));
  CHECK(near == Catch::Approx(80.0));
  CHECK(far == Catch::Approx(90.0));

  // Whitespace is what a shell leaves behind; tolerate it.
  CHECK(parse_depth_range(" 80 , 90 ", near, far, err));
  CHECK(near == Catch::Approx(80.0));

  // A single number is the far value with near defaulting to 0 - "everything
  // past this distance is background" is the common case, and typing ",150"
  // isn't discoverable.
  CHECK(parse_depth_range("150", near, far, err));
  CHECK(near == Catch::Approx(0.0));
  CHECK(far == Catch::Approx(150.0));
  CHECK(parse_depth_range(" 150 ", near, far, err));
  CHECK(far == Catch::Approx(150.0));

  // Each of these used to be accepted, silently ignored, or fatal.
  CHECK_FALSE(parse_depth_range("abc,def", near, far, err));  // used to abort the process
  CHECK_FALSE(parse_depth_range("abc", near, far, err));      // single non-numeric value
  CHECK_FALSE(parse_depth_range("100,50", near, far, err));   // inverted; used to paint all white
  CHECK_FALSE(parse_depth_range("80,80", near, far, err));    // zero extent divides by zero
  CHECK_FALSE(parse_depth_range("0", near, far, err));        // far=0, implicit near=0: zero extent
  CHECK_FALSE(parse_depth_range("-5", near, far, err));       // far<0, implicit near=0: inverted
  CHECK_FALSE(parse_depth_range("", near, far, err));
  CHECK_FALSE(parse_depth_range("80,", near, far, err));
  CHECK_FALSE(parse_depth_range("80,90,100", near, far, err));

  // Every rejection explains itself, so the CLI has something to print.
  CHECK_FALSE(err.empty());
}

TEST_CASE("an explicit range reports the true extent and flags clipping", "[Depthmap]")
{
  // Depths run 70-95; the range covers only 80-90, so both ends are clipped.
  const std::vector<float> depths = {70.0f, 85.0f, 95.0f, BG};
  DepthmapOptions opts;
  opts.profile = DepthProfile::metric;
  opts.has_explicit_range = true;
  opts.explicit_near = 80.0;
  opts.explicit_far = 90.0;
  const auto img = encode_depthmap(depths, 2, 2, opts);

  // Values are clamped into the range...
  CHECK(grey16(img, 0) == 80);
  CHECK(grey16(img, 1) == 85);
  CHECK(grey16(img, 2) == 90);

  // ...but the reported extent stays truthful, so the caller can still tell what
  // was actually in the scene. Reporting the requested range back would hide
  // exactly the fact an explicit range most needs to surface.
  CHECK(img.minDepth == Catch::Approx(70.0));
  CHECK(img.maxDepth == Catch::Approx(95.0));
  CHECK(img.clipped);
}

TEST_CASE("an explicit range that covers the scene does not flag clipping", "[Depthmap]")
{
  const std::vector<float> depths = {82.0f, 88.0f, BG, BG};
  DepthmapOptions opts;
  opts.has_explicit_range = true;
  opts.explicit_near = 80.0;
  opts.explicit_far = 90.0;
  const auto img = encode_depthmap(depths, 2, 2, opts);
  CHECK_FALSE(img.clipped);
}

TEST_CASE("the camera sidecar states its conventions", "[Depthmap]")
{
  CameraParameters cam;
  cam.clipNear = 10;
  cam.clipFar = 500;
  const auto json = serialize_camera_json(cam);

  // Sixteen bare numbers are ambiguous: glGetDoublev returns column-major, while
  // JSON consumers tend to assume row-major, and guessing wrong yields a
  // plausible-looking transposed reconstruction. Say it in the file.
  CHECK(json.find("\"matrixOrder\": \"column-major\"") != std::string::npos);
  CHECK(json.find("\"handedness\": \"right\"") != std::string::npos);
  // Depth is measured from the eye in both projections, and in millimetres.
  CHECK(json.find("\"depthOrigin\": \"eye\"") != std::string::npos);
  CHECK(json.find("\"depthUnits\": \"mm\"") != std::string::npos);
}

TEST_CASE("an explicit range overrides the viewport's bounding-box range", "[Depthmap]")
{
  // The viewport pins its shading to the bounding box so it does not swim while
  // the model is rotated. An explicit range is a stronger statement than that,
  // and is what the export uses - so when one is given the viewport must defer
  // to it, or the preview and the file disagree exactly when the user has asked
  // for them to agree.
  DepthmapOptions opts;
  opts.has_explicit_range = true;
  opts.explicit_near = 80.0;
  opts.explicit_far = 90.0;

  const auto r = resolve_depth_range(opts, 10.0, 500.0);
  CHECK(r.start == Catch::Approx(80.0));
  CHECK(r.end == Catch::Approx(90.0));
}

TEST_CASE("without an explicit range the viewport still uses the bounding box", "[Depthmap]")
{
  const DepthmapOptions opts;
  const auto r = resolve_depth_range(opts, 90.0, 110.0);
  CHECK(r.start == Catch::Approx(90.0));
  CHECK(r.end == Catch::Approx(110.0));
}

TEST_CASE("a resolved range is always usable by fog", "[Depthmap]")
{
  // Whichever source it came from, fog divides by (end - start).
  DepthmapOptions opts;
  opts.has_explicit_range = true;
  opts.explicit_near = 50.0;
  opts.explicit_far = 50.0;  // rejected at parse time, but do not trust that here
  CHECK(resolve_depth_range(opts, 0.0, 0.0).end > resolve_depth_range(opts, 0.0, 0.0).start);
  CHECK(resolve_depth_range(DepthmapOptions{}, 5.0, 5.0).end >
        resolve_depth_range(DepthmapOptions{}, 5.0, 5.0).start);
}

// ---------------------------------------------------------------------------
// Depth range helpers.
//
// Reported from dogfooding 2026-08-20: on a long, thin model ("extruder
// illustration", which sticks far out in one direction) the viewport depth
// shading looks wrong when the protrusion points at the camera. It is following
// consistent rules - these tests pin what those rules are, because the effect is
// large enough that it has to be a deliberate choice rather than an accident
// nobody wrote down.
// ---------------------------------------------------------------------------

namespace {

//! Column-major GL modelview looking down -Z from `dist`, with no rotation.
std::array<double, 16> viewDownZ(double dist)
{
  std::array<double, 16> mv{};
  mv[0] = 1.0;
  mv[5] = 1.0;
  mv[10] = 1.0;
  mv[15] = 1.0;
  mv[14] = -dist;
  return mv;
}

//! Looking along the model's +X axis instead: the third row becomes (1,0,0), so
//! eye depth is measured along X.
std::array<double, 16> viewDownX(double dist)
{
  std::array<double, 16> mv{};
  mv[2] = 1.0;
  mv[4] = 1.0;
  mv[9] = 1.0;
  mv[15] = 1.0;
  mv[14] = -dist;
  return mv;
}

}  // namespace

// ---------------------------------------------------------------------------
// Capped bounding-sphere range (the user's design decision, 2026-08-20).
// ---------------------------------------------------------------------------

TEST_CASE("the sphere range spans the model's bounding sphere", "[Depthmap]")
{
  const double bmin[3] = {-1.0, -1.0, -1.0};
  const double bmax[3] = {1.0, 1.0, 1.0};
  const double R = std::sqrt(3.0);  // half the body diagonal
  const auto range = capped_sphere_range(bmin, bmax, kOrigin, viewDownZ(100.0).data());
  CHECK(range.start == Catch::Approx(100.0 - R));
  CHECK(range.end == Catch::Approx(100.0 + R));
}

TEST_CASE("shading is independent of viewing distance while the cap is idle", "[Depthmap]")
{
  // The property the whole design rests on: with range = [d-R, d+R], a surface a
  // fixed offset from the centre maps to the same place in the range whatever d
  // is. A cap that binds derives R from d and destroys exactly this, which is how
  // a capped diameter of d came to rescale the image at every realistic distance
  // on a long model.
  const double bmin[3] = {-20.0, -20.0, -20.0};
  const double bmax[3] = {20.0, 20.0, 20.0};
  const double offset = 7.0;  // a surface 7mm in front of the model centre
  double shade[2];
  int i = 0;
  for (const double d : {300.0, 900.0}) {
    const auto r = capped_sphere_range(bmin, bmax, kOrigin, viewDownZ(d).data());
    shade[i++] = (r.end - (d - offset)) / (r.end - r.start);
  }
  CHECK(shade[0] == Catch::Approx(shade[1]));
}

TEST_CASE("the sphere range does not move when the model is rotated", "[Depthmap]")
{
  // The whole point of the change: a 200x8x8 model reported extent 8 side-on and
  // 200 end-on under the old per-view box measurement, so the shading rebalanced
  // as it turned. The sphere is orientation invariant, so both views agree.
  const double bmin[3] = {-100.0, -4.0, -4.0};
  const double bmax[3] = {100.0, 4.0, 4.0};
  const auto sideOn = capped_sphere_range(bmin, bmax, kOrigin, viewDownZ(5000.0).data());
  const auto endOn = capped_sphere_range(bmin, bmax, kOrigin, viewDownX(5000.0).data());
  CHECK(sideOn.start == Catch::Approx(endOn.start));
  CHECK(sideOn.end == Catch::Approx(endOn.end));
}

TEST_CASE("the sphere diameter is capped at the camera distance", "[Depthmap]")
{
  // R' = min(R, d/2), so the near end can never reach the eye however large the
  // model is relative to the viewing distance. Without the cap a model bigger
  // than its own camera distance puts the near end behind the eye, where fog
  // start goes negative and the gradient stops meaning anything.
  const double bmin[3] = {-500.0, -500.0, -500.0};
  const double bmax[3] = {500.0, 500.0, 500.0};
  const double d = 100.0;  // camera far closer than the model is big
  const auto range = capped_sphere_range(bmin, bmax, kOrigin, viewDownZ(d).data());
  CHECK(range.start == Catch::Approx(d * (1.0 - DEPTH_NEAR_MARGIN)));
  CHECK(range.end == Catch::Approx(d * (1.0 + DEPTH_NEAR_MARGIN)));
  CHECK(range.start > 0.0);
}

TEST_CASE("the sphere range never collapses or inverts", "[Depthmap]")
{
  const double p[3] = {0.0, 0.0, 0.0};
  const auto degenerate = capped_sphere_range(p, p, kOrigin, viewDownZ(10.0).data());
  CHECK(degenerate.end > degenerate.start);

  // Centre behind the eye: nothing sensible to normalize across, but it must
  // still hand back a usable, positive, non-inverted range rather than NaN.
  const auto behind = capped_sphere_range(p, p, kOrigin, viewDownZ(-10.0).data());
  CHECK(behind.end > behind.start);
  CHECK(behind.start >= 0.0);
}

TEST_CASE("the range is stable when the camera orbits its own centre", "[Depthmap]")
{
  // The reason the sphere is centred on the rotation centre rather than the
  // model: orbiting keeps the eye-to-rotation-centre distance constant, so both
  // the radius and the range stay put. Centred on a model sitting off to one
  // side, the same orbit changes that distance and rescales the image.
  const double bmin[3] = {60.0, -10.0, -10.0};  // model well off the rotation centre
  const double bmax[3] = {140.0, 10.0, 10.0};
  const auto a = capped_sphere_range(bmin, bmax, kOrigin, viewDownZ(400.0).data());
  const auto b = capped_sphere_range(bmin, bmax, kOrigin, viewDownX(400.0).data());
  CHECK(a.start == Catch::Approx(b.start));
  CHECK(a.end == Catch::Approx(b.end));
}

TEST_CASE("the sphere grows to contain a model it is not centred on", "[Depthmap]")
{
  // Panning away from the model must cost contrast, not correctness: the far
  // corner has to stay inside the range or it would clamp to black.
  const double bmin[3] = {90.0, -5.0, -5.0};
  const double bmax[3] = {110.0, 5.0, 5.0};
  const auto range = capped_sphere_range(bmin, bmax, kOrigin, viewDownZ(10000.0).data());
  const double farthestCorner = std::sqrt(110.0 * 110.0 + 25.0 + 25.0);
  CHECK(range.end - 10000.0 >= Catch::Approx(farthestCorner).margin(1e-9));
}

TEST_CASE("the fine metric profile encodes 10um units", "[Depthmap]")
{
  // 1mm units leave a small model with almost no depth resolution: a 15mm-deep
  // object viewed from 43mm spans 15 of 65535 levels. At 10um the same object
  // spans 1560. The ceiling drops from 65.5m to 655mm, which still covers the
  // eye distances OpenSCAD models are actually viewed from.
  const std::vector<float> depths = {1.0f, 10.0f, 100.0f};
  const auto img = encode_depthmap(depths, 3, 1, DepthProfile::metricFine);
  REQUIRE(img.bytesPerPixel == 2);
  auto value = [&](size_t i) {
    return static_cast<int>(img.pixels[i * 2]) << 8 | img.pixels[i * 2 + 1];
  };
  CHECK(value(0) == 100);    // 1mm  -> 100 units of 10um
  CHECK(value(1) == 1000);   // 10mm
  CHECK(value(2) == 10000);  // 100mm
}

TEST_CASE("the fine metric profile saturates below the background value", "[Depthmap]")
{
  // Beyond 655.34mm the range is exhausted. Clamping must stop just short of the
  // background sentinel so "too far to represent" stays distinguishable from
  // "no geometry here" - the same rule the millimetre profile follows.
  const std::vector<float> depths = {5000.0f};
  const auto img = encode_depthmap(depths, 1, 1, DepthProfile::metricFine);
  const int value = static_cast<int>(img.pixels[0]) << 8 | img.pixels[1];
  CHECK(value == 65534);
}

TEST_CASE("background is the same sentinel in both metric profiles", "[Depthmap]")
{
  const std::vector<float> depths = {std::numeric_limits<float>::infinity()};
  for (const auto profile : {DepthProfile::metric, DepthProfile::metricFine}) {
    const auto img = encode_depthmap(depths, 1, 1, profile);
    const int value = static_cast<int>(img.pixels[0]) << 8 | img.pixels[1];
    CHECK(value == 65535);
  }
}

TEST_CASE("every absolute-scale profile is 16-bit", "[Depthmap]")
{
  // 8 bits of absolute depth is not worth offering: 256 levels over any range
  // wide enough to hold a scene is coarser than the geometry it describes. So a
  // profile that encodes real distance is always 16-bit, and the metadata design
  // leans on it - the 16-bit writer is lodepng everywhere, while the 8-bit path
  // is CoreGraphics on macOS and cannot carry arbitrary text chunks.
  const std::vector<float> depths = {10.0f};
  for (const auto profile : {DepthProfile::metric, DepthProfile::metricFine, DepthProfile::visual}) {
    const auto img = encode_depthmap(depths, 1, 1, profile);
    if (depth_units_per_mm(profile) > 0.0) {
      CHECK(img.bytesPerPixel == 2);
    } else {
      CHECK(img.bytesPerPixel > 2);  // normalized profiles are RGBA, never absolute
    }
  }
}
