difference() {
  cube([16, 16, 8]);
  translate([8, 8, -1]) cylinder(r=3, h=10, $fn=6);
}
translate([20, 0, 0]) cylinder(r1=4, r2=0, h=8, $fn=6);
translate([35, 0, 4]) sphere(r=4, $fn=6);
translate([45, 0, 0]) polyhedron(
  points=[[0,0,0], [10,0,0], [0,10,0], [0,0,10]],
  faces=[[0,1,2], [0,3,1], [0,2,3], [1,3,2]]
);
