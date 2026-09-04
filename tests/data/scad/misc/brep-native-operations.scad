// Native BREP paths: smooth generated curves and intentionally faceted imported data.
linear_extrude(3) offset(r=1) difference() { circle(4); circle(2); }
translate([12,0,0]) linear_extrude(3) projection(cut=true) sphere(4);
translate([24,0,0]) linear_extrude(3) projection() translate([0,0,10]) sphere(4);
translate([36,0,0]) linear_extrude(3) text("Oo", size=5, font="Liberation Sans");
translate([0,12,0]) hull() { cube(2); translate([4,2,0]) cube(2); }
translate([12,12,0]) minkowski() { cube(2); sphere(1); }
translate([24,12,0]) import("../../off/brep-tetrahedron.off");
translate([36,12,0]) surface("../3D/features/surface-simple.dat");
translate([0,24,0]) hull() { sphere(2); translate([6,0,0]) sphere(1); }
translate([12,24,0]) linear_extrude(3) hull() { circle(1); translate([5,0]) circle(2); }
translate([24,24,0]) linear_extrude(3) minkowski() { square(4); circle(1); }
translate([36,24,0]) import("../../3mf/brep-multipart.3mf");
translate([0,36,0]) linear_extrude(3) offset(delta=1, chamfer=true) square(4);
translate([12,36,0]) linear_extrude(3) offset(delta=1, chamfer=true) circle(4);
translate([24,36,0]) linear_extrude(3) offset(delta=1, chamfer=true) circle(4, $fn=6);
translate([36,36,0]) hull() { sphere(1); translate([6,0,0]) sphere(1); translate([3,5,0]) sphere(2); }
translate([0,48,0]) hull() { cube(2); translate([8,1,1]) sphere(1); translate([1,8,1]) sphere(2); }
translate([12,48,0]) hull() { cube(2); translate([8,1,0]) cylinder(r1=2, r2=1, h=4); }
translate([24,48,0]) hull() { cylinder(r=1, h=4); translate([7,0,0]) cylinder(r1=2, r2=1, h=6); translate([3,6,0]) cube(2); }
