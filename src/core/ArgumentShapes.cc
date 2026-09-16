#include "core/ArgumentShapes.h"

#include <unordered_map>

namespace {

// Deliberately small. A shape earns its place only when the seeded values are
// genuinely neutral, so that accepting one is never a silent change of meaning.
//
// Written in the project's normal style, spaces included. That is only possible
// because the popup is a user list: QScintilla trims an entry at its first space
// for autocompletion lists, but not for user lists, so this text reaches the
// document verbatim.
//
// The bar is not merely "valid": a shape earns its place when its seeded value is
// a no-op AND a single digit away from what is almost always wanted. mirror is the
// clearest case - mirror([0,0,0]) is an explicitly handled no-op (see the guard in
// builtin_mirror, which leaves the identity matrix in place for a zero vector), and
// changing one 0 to a 1 gives the mirror the user actually came for.
//
// Notable omission:
//   color - every value is a choice and none is neutral, so there is no seed that
//           is both a no-op and a step towards the common case.
//   cylinder - the historical defaults (r=1, h=1 but r1/r2 asymmetry) are odd
//              enough that curated named forms disambiguate better than one shape.
const std::unordered_map<std::string, std::vector<std::string>> shapes = {
  // Transforms: identity values, so the call is a no-op until edited.
  {"translate", {"translate([0, 0, 0])"}},
  {"rotate", {"rotate([0, 0, 0])", "rotate(0, [0, 0, 1])"}},
  {"scale", {"scale([1, 1, 1])"}},
  {"resize", {"resize([0, 0, 0])"}},
  {"mirror", {"mirror([0, 0, 0])"}},

  // Primitives: unit dimensions, the practical starting point.
  {"cube", {"cube([1, 1, 1])", "cube([1, 1, 1], center = true)"}},
  {"sphere", {"sphere(r = 1)", "sphere(d = 1)"}},
  {"cylinder", {"cylinder(h = 1, r = 1)", "cylinder(h = 1, r1 = 1, r2 = 1)", "cylinder(h = 1, d = 1)"}},
  {"square", {"square([1, 1])", "square([1, 1], center = true)"}},
  {"circle", {"circle(r = 1)", "circle(d = 1)"}},

  // Extrusions and 2D operations.
  {"linear_extrude", {"linear_extrude(height = 1)", "linear_extrude(height = 1, twist = 0)"}},
  {"rotate_extrude", {"rotate_extrude(angle = 360)"}},
  {"offset", {"offset(r = 0)", "offset(delta = 0)"}},
};

}  // namespace

const std::vector<std::string>& argumentShapesFor(const std::string& name)
{
  static const std::vector<std::string> none;
  const auto found = shapes.find(name);
  return found == shapes.end() ? none : found->second;
}
