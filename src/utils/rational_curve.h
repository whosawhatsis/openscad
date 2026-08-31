#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <vector>

using RationalCurve2d = std::vector<std::array<double, 3>>;  // x, y, weight
using RationalContour2d = std::vector<RationalCurve2d>;

// Exact rational quadratic ellipse spans, each at most a quarter turn.
inline RationalContour2d rationalEllipseArcs(const std::array<double, 2>& center,
                                             const std::array<double, 2>& u,
                                             const std::array<double, 2>& v, double start, double sweep)
{
  constexpr double pi = 3.14159265358979323846;
  if (!std::isfinite(start) || !std::isfinite(sweep) || std::abs(sweep) > 2 * pi + 1e-9)
    throw std::invalid_argument("Invalid ellipse angular range");
  RationalContour2d curves;
  if (sweep == 0) return curves;
  const int count = std::max(1, static_cast<int>(std::ceil(std::abs(sweep) / (pi / 2))));
  const double step = sweep / count;
  const auto pole = [&](double angle, double weight) {
    return std::array<double, 3>{center[0] + (u[0] * std::cos(angle) + v[0] * std::sin(angle)) / weight,
                                 center[1] + (u[1] * std::cos(angle) + v[1] * std::sin(angle)) / weight,
                                 weight};
  };
  for (int i = 0; i < count; ++i) {
    const double a = start + i * step;
    curves.push_back({pole(a, 1), pole(a + step / 2, std::cos(step / 2)), pole(a + step, 1)});
  }
  return curves;
}
