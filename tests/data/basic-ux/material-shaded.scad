// Geometry with material attributes, for the shaded-viewport GUI test. The CSG
// difference matters: it forces a real OpenCSG product in the F5 preview, which
// is a different draw path from the evaluated mesh the CLI renders.
material("m", c = "red", roughness = 0.6, metallic = 1)
difference() {
  cube(20, center = true);
  sphere(13, $fn = 32);
}
