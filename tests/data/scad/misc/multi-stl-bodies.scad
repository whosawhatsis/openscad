// Body-identity semantics for color()/material() aware multi-file STL export.
// Each top-level statement below is one line of the specification; the expected
// output lists the bodies it must produce, in source order.

// Two overlapping bodies stay separate - a body is declared, not guessed from
// connectivity.
color("red") cube(2);
color("blue") translate([1, 0, 0]) cube(2);

// An enclosing wrapper replaces every nested body boundary with one new body.
material("shell", "grey") {
  color("red") translate([10, 0, 0]) cube(2);
  color("blue") translate([12, 0, 0]) cube(2);
}

// One transform enclosing several attributed bodies preserves all of them and
// applies the transform to each.
translate([0, 10, 0]) {
  color("red") cube(2);
  material(c = "blue", name = "PETG") translate([4, 0, 0]) cube(2);
}

// A material needs no color: the name is its first and most fundamental
// argument, so material("name") alone is a complete declaration and must not
// paint the body with an unset color.
material("brass") translate([50, 0, 0]) cube(2);

// A body-combining operation produces one body carrying the first operand's
// attributes.
difference() {
  material("PLA", "grey") translate([20, 0, 0]) cube(3);
  translate([21, 1, 1]) cube(1);
}

// ... and the default material when that first operand is unwrapped. The second
// operand's material must not be inherited.
difference() {
  translate([30, 0, 0]) cube(3);
  material(c = "red", name = "notinherited") translate([31, 1, 1]) cube(1);
}

// A nested body-combining operation resolves its own product first; that
// product is then the parent's first operand.
union() {
  difference() {
    material(c = "green", name = "ABS") translate([40, 0, 0]) cube(3);
    translate([41, 1, 1]) cube(1);
  }
  material(c = "red", name = "notinherited") translate([43, 0, 0]) cube(1);
}
