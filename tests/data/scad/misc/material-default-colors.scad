// Default colours for named materials.
//
// A material's colour is resolved from three places, nearest wins:
//   1. an explicit c/alpha argument on the material() call,
//   2. $material_colors in the model,
//   3. the per-material default table in Preferences (not exercised here - a
//      test must never depend on the user's settings).
// A named material that resolves to no colour anywhere warns, and stays
// colourless rather than inventing one.

$material_colors = [
  ["PLA", "yellow"],
  ["PETG", [0.5, 0.5, 0, 0.8]],
];

// Resolved from $material_colors, by colour name.
material("PLA") cube(10);

// Resolved from $material_colors, as a vector with alpha.
material("PETG") translate([20, 0, 0]) cube(10);

// An explicit colour wins over the table.
material("PLA", "red") translate([40, 0, 0]) cube(10);

// An explicit alpha applies on top of the table's colour.
material("PLA", alpha = 0.25) translate([60, 0, 0]) cube(10);

// Not in the table and no colour given: warns, stays colourless.
material("ABS") translate([80, 0, 0]) cube(10);

// A nested lookup still resolves - $material_colors is an ordinary special
// variable, so it follows the usual dynamic scoping rules.
translate([0, 20, 0]) material("PETG") cube(10);

// color() never consults the table; it is not a material.
color("blue") translate([20, 20, 0]) cube(10);
