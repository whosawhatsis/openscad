#include "core/ArgumentShapes.h"

#include <unordered_map>

namespace {

// Deliberately small. A shape earns its place only when the seeded values are
// genuinely neutral, so that accepting one is never a silent change of meaning.
//
// Written without spaces on purpose: Scintilla treats a space in an autocompletion
// entry as the end of the inserted word and the start of an annotation, so
// "translate([0, 0, 0])" would insert as "translate([0,". The text here is exactly
// what lands in the document.
//
// Notable omissions and why:
//   mirror   - has no identity normal; [0,0,0] is degenerate and [1,0,0] is a
//              real transformation, so neither is a safe default.
//   color    - every value is a choice, none is neutral.
//   cylinder - the historical defaults (r=1, h=1 but r1/r2 asymmetry) are odd
//              enough that curated named forms disambiguate better than one shape.
const std::unordered_map<std::string, std::vector<std::string>> shapes = {
  // Transforms: identity values, so the call is a no-op until edited.
  {"translate", {"translate([0,0,0])"}},
  {"rotate", {"rotate([0,0,0])", "rotate(0,[0,0,1])"}},
  {"scale", {"scale([1,1,1])"}},
  {"resize", {"resize([0,0,0])"}},

  // Primitives: unit dimensions, the practical starting point.
  {"cube", {"cube([1,1,1])", "cube([1,1,1],center=true)"}},
  {"sphere", {"sphere(r=1)", "sphere(d=1)"}},
  {"cylinder", {"cylinder(h=1,r=1)", "cylinder(h=1,r1=1,r2=1)", "cylinder(h=1,d=1)"}},
  {"square", {"square([1,1])", "square([1,1],center=true)"}},
  {"circle", {"circle(r=1)", "circle(d=1)"}},

  // Extrusions and 2D operations.
  {"linear_extrude", {"linear_extrude(height=1)", "linear_extrude(height=1,twist=0)"}},
  {"rotate_extrude", {"rotate_extrude(angle=360)"}},
  {"offset", {"offset(r=0)", "offset(delta=0)"}},
};

}  // namespace

const std::vector<std::string>& argumentShapesFor(const std::string& name)
{
  static const std::vector<std::string> none;
  const auto found = shapes.find(name);
  return found == shapes.end() ? none : found->second;
}
