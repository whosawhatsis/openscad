linear_extrude(height=8, center=true)
  difference() {
    circle(r=5);
    circle(r=2);
  }

translate([15, 0, 0]) linear_extrude(height=8) circle(r=4, $fn=6);
translate([25, 0, 0]) linear_extrude(height=8)
  polygon([[0,0], [4,0], [4,2], [2,2], [2,4], [0,4]]);
translate([35, 0, 0]) linear_extrude(height=8) square([4,4], center=true);

// Nested island, hole, and a disconnected contour in deliberately mixed order.
translate([45, 0, 0]) linear_extrude(height=8)
  polygon(points=[[0,0], [10,0], [10,10], [0,10],
                  [2,2], [8,2], [8,8], [2,8],
                  [4,4], [6,4], [6,6], [4,6],
                  [12,0], [14,0], [14,2], [12,2]],
          paths=[[8,9,10,11], [4,5,6,7], [12,13,14,15], [0,1,2,3]]);

translate([65, 0, 0]) linear_extrude(height=8, scale=0.5)
  difference() { circle(r=4); circle(r=2); }
translate([80, 0, 0]) linear_extrude(height=8, scale=[0.5, 0.75]) circle(r=4);
translate([95, 0, 0]) linear_extrude(height=8, scale=1.5) circle(r=3, $fn=6);
