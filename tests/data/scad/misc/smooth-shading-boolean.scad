// Rendered by smoothrendertest.py with --render, once per fa= value. $fn fixes the
// sphere's mesh and the cube is unaffected by $fa, so the geometry is identical
// between runs and only the smoothing tolerance differs.
//
// --render matters: the F5 preview draws the primitives unmerged, so each still
// carries the tolerance it was built with and this passes without any propagation.
// Only the evaluated F6 mesh is a new PolySet produced by the boolean, which is
// where the angle is lost unless it is carried across the operation.
fa = 12;
$fa = fa;
difference() {
  sphere(20, $fn = 24);
  translate([0, 0, 14]) cube(30, center = true);
}
