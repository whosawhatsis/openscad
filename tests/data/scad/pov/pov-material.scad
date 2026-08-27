// Per-body finish in POV export. Only the surface parameters POV-Ray and
// Blender's Principled BSDF both express are accepted, so a value here means
// the same thing in either renderer.
material("rough", c = "red", roughness = 0.6, metallic = 1) cube(10);
material("glass", c = "blue", ior = 1.5, specular = 0.9, emission = 0.2)
  translate([20, 0, 0]) cube(10);
