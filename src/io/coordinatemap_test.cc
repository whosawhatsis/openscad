#include <sstream>
#include <string>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "io/coordinatemap.h"

using Catch::Approx;

TEST_CASE("bounds map the model into the unit cube", "[coordinatemap]")
{
  const double min[3] = {-10.0, 0.0, 5.0};
  const double max[3] = {10.0, 40.0, 25.0};
  const auto bounds = coordinate_bounds(min, max);

  double p[3];
  normalize_point(bounds, min, p);
  CHECK(p[0] == Approx(0.0));
  CHECK(p[1] == Approx(0.0));
  CHECK(p[2] == Approx(0.0));

  normalize_point(bounds, max, p);
  CHECK(p[0] == Approx(1.0));
  CHECK(p[1] == Approx(1.0));
  CHECK(p[2] == Approx(1.0));

  const double mid[3] = {0.0, 20.0, 15.0};
  normalize_point(bounds, mid, p);
  CHECK(p[0] == Approx(0.5));
  CHECK(p[1] == Approx(0.5));
  CHECK(p[2] == Approx(0.5));
}

TEST_CASE("a flat model does not divide by zero", "[coordinatemap]")
{
  // A 2D-ish model has zero extent on Z. Saturating that axis (or producing NaN)
  // would corrupt the other two channels' meaning as well, so it pins to mid-grey.
  const double min[3] = {0.0, 0.0, 3.0};
  const double max[3] = {10.0, 10.0, 3.0};
  const auto bounds = coordinate_bounds(min, max);

  double p[3];
  normalize_point(bounds, min, p);
  CHECK(p[2] == Approx(0.5));
  normalize_point(bounds, max, p);
  CHECK(p[2] == Approx(0.5));
  // The live axes are unaffected by the degenerate one.
  CHECK(p[0] == Approx(1.0));
}

TEST_CASE("points outside the bounds are clamped, not wrapped", "[coordinatemap]")
{
  const double min[3] = {0.0, 0.0, 0.0};
  const double max[3] = {10.0, 10.0, 10.0};
  const auto bounds = coordinate_bounds(min, max);

  const double outside[3] = {-5.0, 15.0, 5.0};
  double p[3];
  normalize_point(bounds, outside, p);
  CHECK(p[0] == Approx(0.0));
  CHECK(p[1] == Approx(1.0));
  CHECK(p[2] == Approx(0.5));
}

TEST_CASE("the sidecar records the box a pixel decodes against", "[coordinatemap]")
{
  const double min[3] = {-10.0, 0.0, 5.0};
  const double max[3] = {10.0, 40.0, 25.0};
  const auto bounds = coordinate_bounds(min, max);

  const std::string json = serialize_bounds_json(bounds);
  // Both corners must be present: a consumer decodes with min + channel * extent,
  // and cannot do that from the normalization factors alone.
  CHECK(json.find("\"min\"") != std::string::npos);
  CHECK(json.find("\"max\"") != std::string::npos);
  CHECK(json.find("-10") != std::string::npos);
  CHECK(json.find("40") != std::string::npos);
  // Degenerate axes are padded internally; the sidecar must still report the
  // real box, or the decode shifts by half the padding.
  CHECK(json.find("25") != std::string::npos);
}

TEST_CASE("a degenerate axis still reports its true extent in the sidecar", "[coordinatemap]")
{
  const double min[3] = {0.0, 0.0, 3.0};
  const double max[3] = {10.0, 10.0, 3.0};
  const auto bounds = coordinate_bounds(min, max);
  const std::string json = serialize_bounds_json(bounds);
  CHECK(json.find("3") != std::string::npos);
  CHECK(json.find("nan") == std::string::npos);
  CHECK(json.find("inf") == std::string::npos);
}
