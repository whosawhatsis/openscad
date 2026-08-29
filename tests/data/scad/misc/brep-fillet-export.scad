$fa = 8;
$fs = 0.5;

difference(fillet = 2) {
    cube([20, 20, 10]);
    translate([10, 10, -1]) cylinder(h = 12, r = 4);
}
