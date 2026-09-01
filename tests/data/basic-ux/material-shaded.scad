// Geometry with material attributes, for the shaded-viewport GUI tests.
//
// The CSG difference matters: it forces a real OpenCSG product in the F5
// preview, which is a different draw path from the evaluated mesh the CLI
// renders. The metal is what makes the tests sensitive: a metal has no diffuse
// response, so if the material vertex attribute does not reach the shader the
// body renders as a plain dielectric and the difference is unmissable.
material("m", c = "red", roughness = 0.6, metallic = 1)
difference() {
  cube(20, center = true);
  sphere(13, $fn = 32);
}
