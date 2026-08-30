module pair() {
  cube(10);
  translate([15, 0, 0]) cube(10);
}

translate([100, 0, 0]) {
  pair();
  translate([30, 0, 0]) cube(3);
}

difference() {
  for (x = [0, 5]) translate([x, 0, 0]) cube(10);
}

%cube(500);
