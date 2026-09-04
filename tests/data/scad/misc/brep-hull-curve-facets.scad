// hull() cannot yet keep Bezier profiles exact, so it tessellates them. The approximation
// must at least follow the facet settings in scope rather than a fixed internal tolerance.
$fa = 1;
$fs = 0.05;
hull() {
  linear_extrude(3) text("O", size = 10, font = "Liberation Sans");
  translate([25, 0, 0]) cube(2);
}
