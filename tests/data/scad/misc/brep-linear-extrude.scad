linear_extrude(height=8, center=true)
  difference() {
    circle(r=5);
    circle(r=2);
  }

translate([15, 0, 0]) linear_extrude(height=8) circle(r=4, $fn=6);
translate([25, 0, 0]) linear_extrude(height=8)
  polygon([[0,0], [4,0], [4,2], [2,2], [2,4], [0,4]]);
translate([35, 0, 0]) linear_extrude(height=8) square([4,4], center=true);
