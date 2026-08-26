// Exercise the agent lighting modes (normal / coordinate / flat / chromatic).
//
// Deliberately mixed: flat faces give each mode a constant-valued region that is
// easy to reason about, the sphere gives smoothly varying normals, and the
// off-origin placement means a coordinate map that failed to normalize against
// the bounding box would saturate rather than quietly look plausible.
translate([-5, -5, 0]) cube([20, 15, 10]);
translate([10, 10, 10]) sphere(r = 6, $fn = 32);
translate([-12, 0, 0]) rotate([0, 35, 0]) cube([6, 6, 14]);
