// A cutout face (the inside of the hole) plus plain default faces, and one explicitly
// colored child, so a render exercises all three color paths at once.
difference() {
  cube(20, center = true);
  cylinder(h = 40, r = 6, center = true, $fn = 32);
}
translate([25, 0, 0]) color("red") cube(10, center = true);
