// hull() must tessellate with the facet settings in scope, not with the built-in defaults.
$fa = 1;
$fs = 0.05;
hull() { cylinder(r = 2, h = 4); translate([10, 0, 0]) cylinder(r = 1, h = 4); }
