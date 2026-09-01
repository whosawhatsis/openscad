// A transparent part with internal cavities, for layer-ordering.
//
// The shape matters. A convex transparent solid cannot test ordering at all:
// two layers of the same shaded color composite to the same result in either
// order, and measured, a cube and a plain tube agree on mean, p10, p90 and peak
// brightness whichever order they are drawn in. What makes order visible is
// several shells at different depths whose normals differ strongly - here a
// thin plate unioned with a taller block, bored through vertically and again
// horizontally, so a view ray crosses four or six differently-lit surfaces.
//
// Distilled from the part that exposed the bug in the wild, the extruder body
// in a user's hot-end illustration.
color([.2, .2, .2, .8]) difference() {
  union() {
    linear_extrude(2) square(42, center = true);
    linear_extrude(14) translate([-21, -21]) square([42, 22]);
  }
  cylinder(d = 22, h = 40, center = true, $fn = 48);
  translate([0, -10, 7]) rotate([0, 90, 0]) cylinder(d = 9, h = 60, center = true, $fn = 48);
  for (x = [-1, 1], y = [-1, 1]) translate([x * 15.5, y * 15.5, 0]) cylinder(d = 3, h = 40, center = true, $fn = 24);
}
