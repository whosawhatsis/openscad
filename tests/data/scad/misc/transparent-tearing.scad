// A single convex transparent solid with an opaque object inside it.
//
// Convex and smooth on purpose: correctly composited, its interior has no
// abrupt color steps at all, so any high-frequency structure in the image can
// only be draw-order artifacts. Transparent geometry drawn with depth writes
// enabled occludes itself triangle by triangle and fills the interior with
// stripes, which is what this scene exists to catch.
color([.2, .4, .6, .5]) rotate([90, 0, 0]) cylinder(d = 30, h = 60, center = true, $fn = 64);
color([.3, .3, .3]) sphere(d = 25, $fn = 48);
