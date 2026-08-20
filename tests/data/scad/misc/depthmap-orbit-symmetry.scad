// Depth shading must not change as the viewport rotates.
//
// A large cube on the centre of rotation, surrounded by four small markers a
// thousand units out. The whole scene has four-fold symmetry about Z, so a 90
// degree orbit maps it exactly onto itself: the depth map from either angle must
// be the same image, not merely a similar one. Both tests therefore compare
// against a single expected image.
//
// Note what this does NOT test. The scene's symmetry puts the bounding box centre
// on the rotation centre, so a range derived from the box agrees with one derived
// from the rotation centre and this model passes either way. depthmap-offcentre
// is the asymmetric companion that tells them apart; this one pins exactness,
// that one pins correctness, and neither substitutes for the other.
//
// Deliberately low contrast: the centre cube spans a few percent of a range
// stretched to reach the markers. Stability is the property under test.
cube(60, center = true);
for (a = [0:90:270]) {
  rotate([0, 0, a]) translate([1000, 0, 0]) cube(20, center = true);
}
