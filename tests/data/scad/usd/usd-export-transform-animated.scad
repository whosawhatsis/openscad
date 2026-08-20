// Geometry is cache-stable across frames; only the transforms depend on $t.
color("red") translate([100 * $t, 0, 0]) rotate([0, 0, 360 * $t])
  translate([15, 0, 0]) cube([30, 10, 10], center = true);
