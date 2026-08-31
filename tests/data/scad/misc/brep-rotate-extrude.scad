rotate_extrude() translate([5, 0]) circle(r=1);
translate([15, 0, 0]) rotate_extrude(angle=180)
  translate([5, 0]) difference() { circle(r=1); circle(r=0.5); }
translate([30, 0, 0]) rotate_extrude(angle=-180, start=90)
  translate([5, 0]) square([2, 2], center=true);
translate([45, 0, 0]) rotate_extrude() square([2, 3]);
