// Parameters that material() and color() must NOT silently swallow.
//
// ambient, diffuse, brilliance, reflection and crand are POV-Ray finish keywords
// that were carried briefly and then deliberately dropped, because none of them
// has a counterpart in Blender's Principled BSDF: a model using them would have
// meant one thing in POV and nothing anywhere else. They stayed declared as named
// parameters after the reader stopped consuming them, so writing one parsed
// cleanly, warned nothing, and did nothing.
//
// Each line below must produce a "variable not specified as parameter" warning.
// The three that survived - specular, emission and ior - must not.
material("m", c = "red", ambient = 0.3) cube(1);
material("m", c = "red", diffuse = 0.7) cube(1);
material("m", c = "red", brilliance = 2) cube(1);
material("m", c = "red", reflection = 0.5) cube(1);
material("m", c = "red", crand = 0.1) cube(1);
color("red", ambient = 0.3) cube(1);

// Still honored, so still silent.
material("m", c = "red", specular = 0.5, emission = 0.2, ior = 1.5) cube(1);
material("m", c = "red", roughness = 0.4, metallic = 1) cube(1);
