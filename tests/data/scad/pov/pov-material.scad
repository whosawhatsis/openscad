// Per-body finish in POV export. The exporter maps our roughness and metallic
// straight onto POV's own finish keywords, which mean the same things.
material("rough", c = "red", roughness = 0.6, metallic = 1) cube(10);
