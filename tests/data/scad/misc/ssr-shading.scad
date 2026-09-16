// Screen-space reflections: a polished metal floor with a saturated red object
// standing on it, viewed at a shallow angle so the object's reflection falls in
// frame below it.
//
// The assertion this scene exists to support is a hue count, not a pixel diff.
// The stand-in environment the shader falls back to is a grey ground/sky
// gradient with no red in it anywhere, and the floor is metallic, so it has no
// diffuse response of its own to tint. Red below the object can therefore only
// have come from reflecting the object itself.
material("floor", c = [0.85, 0.85, 0.88], metallic = 1, roughness = 0.05)
  translate([0, 0, -1]) cube([160, 160, 2], center = true);

material("ball", c = [1, 0, 0]) translate([0, 0, 16]) sphere(15, $fn = 48);
