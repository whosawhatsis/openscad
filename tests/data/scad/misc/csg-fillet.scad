union(fillet = 2) {
    cube(10);
    translate([5, 5, 5]) sphere(5);
}

difference(fillet = -1) {
    cube(10);
    translate([5, 5, -1]) cylinder(h = 12, r = 2);
}

intersection(fillet = 0) {
    cube(10);
    sphere(8);
}
