// The depth range must be centred on the viewport's centre of rotation, not on
// the model's bounding box.
//
// A small sphere at the rotation centre, and one small cube a thousand units
// away so the bounding box is enormous and its centre is nowhere near either
// object. The sphere sits where the camera orbits, so its distance from the eye
// never changes and neither may its shade - from any angle, in either of the two
// views below.
//
// Rendered against separate expected images rather than a shared one, because
// the far cube genuinely moves between these angles. The companion test
// depthmap-orbit-symmetry pins exact pixel identity on a symmetric scene; this
// one is the case that discriminates, because when the range was centred on the
// bounding box this model rendered the sphere blown out to pure white from two
// angles and clamped to invisible from a third.
sphere(10);
translate([0, 1000, 0]) cube();
