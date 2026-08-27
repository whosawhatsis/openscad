// Per-body finish in POV export. The exporter maps our roughness and metallic
// onto POV's own finish keywords, and passes through the further POV finish
// parameters by name.
material("rough", c = "red", roughness = 0.6, metallic = 1) cube(10);
material("glass", c = "blue", ior = 1.5, ambient = 0.1, diffuse = 0.3,
         specular = 0.9, brilliance = 2, emission = 0.2, crand = 0.05,
         reflection = 0.4) translate([20, 0, 0]) cube(10);
