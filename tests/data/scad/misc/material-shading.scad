// Rendered by materialrendertest.py in shaded mode, once per mode= value.
// Same geometry and colour every time, so any pixel difference between renders
// comes from the material attributes alone.
mode = 0;

module body() sphere(20, $fn = 48);

if (mode == 0) color("red") body();                                  // no material at all
else if (mode == 1) material("m", c = "red") body();                 // material, no attributes
else if (mode == 2) material("m", c = "red", roughness = 0.05) body();  // tight highlight
else if (mode == 3) material("m", c = "red", roughness = 0.9) body();   // broad highlight
else if (mode == 4) material("m", c = "red", metallic = 1) body();      // metal
