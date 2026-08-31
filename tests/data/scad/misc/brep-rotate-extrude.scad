rotate_extrude() translate([5, 0]) circle(r=1);
translate([15, 0, 0]) rotate_extrude(angle=180)
  translate([5, 0]) difference() { circle(r=1); circle(r=0.5); }
translate([30, 0, 0]) rotate_extrude(angle=-180, start=90)
  translate([5, 0]) square([2, 2], center=true);
translate([45, 0, 0]) rotate_extrude() square([2, 3]);
translate([55, 0, 0]) rotate_extrude($fn=6) square([4, 2]);
translate([70, 0, 0]) rotate_extrude(angle=-180, start=90, $fn=6)
  translate([5, 0]) circle(r=1, $fn=0);
translate([85, 0, 0]) rotate_extrude($fn=6)
  polygon(points=[[2,0], [5,0], [5,4], [2,4], [3,1], [4,1], [4,3], [3,3]],
          paths=[[0,1,2,3], [4,5,6,7]]);
