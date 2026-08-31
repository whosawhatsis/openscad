// Rendered by materialrendertest.py in shaded mode, once per mode= value.
// Same geometry and color every time, so any pixel difference between renders
// comes from the material attributes alone.
//
// The plain sibling is not decoration: it makes the root of the model a union
// of two children rather than the material() node itself. F6 reads material
// attributes off the resolved geometry, and a multi-child root has no single
// set of them - so without a per-face channel the attributes are dropped and
// every mode renders identically. F5 reads them off the CSG leaf and never
// noticed. Registered twice, once per renderer, for exactly that reason.
mode = 0;

module body() sphere(20, $fn = 48);

// Kept small and off to one side so the varying body stays centred in frame:
// the highlight assertions measure a specular lobe pointed at the camera.
color("red") translate([48, 0, 0]) cube(10, center = true);

if (mode == 0) color("red") body();                                  // no material at all
else if (mode == 1) material("m", c = "red") body();                 // material, no attributes
else if (mode == 2) material("m", c = "red", roughness = 0.05) body();  // tight highlight
else if (mode == 3) material("m", c = "red", roughness = 0.9) body();   // broad highlight
else if (mode == 4) material("m", c = "red", metallic = 1) body();      // metal
else if (mode == 5) material("m", c = "red", roughness = 0) body();     // mirror, not "unset"
