// Two distinct colours with different alphas, so the exported stage exercises material
// interning, per-colour mesh splitting and opacity.
color([1, 0.5, 0.25, 0.125]) translate([-10, 0, 0]) cube(10);
color("green") translate([10, 0, 0]) cube(10);
