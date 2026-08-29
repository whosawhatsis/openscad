// The depth range must be centered on the viewport's center of rotation, not on
// the model's bounding box.
//
// A cube on the rotation center, and one small cube a thousand units away so the
// bounding box is enormous and its center is nowhere near either object. The
// centered cube sits where the camera orbits, so its distance from the eye never
// changes and neither may its shade - from any angle, in either of the two views
// below. A cube rather than a sphere because its faces are flat: every pixel of a
// face is at an exactly determined depth, so a drift of even one grey level is a
// real change rather than resampling noise on a curved silhouette.
//
// Rendered against separate expected images rather than a shared one, because the
// far cube genuinely moves between these angles.
//
// It cannot be otherwise, and the reason is worth writing down. Whole-image
// identity under a rotation needs the scene to be invariant under it, which pins
// the bounding box center on the rotation axis - and then the eye-to-box-center
// distance does not change either, so the very bug this model exists to catch
// becomes invisible. Discrimination and exact identity cannot come from the same
// pair of views. depthmap-orbit-symmetry takes the identity half on a symmetric
// scene; this file takes the discriminating half. When the range was centered on
// the bounding box, the centered object here rendered blown out to pure white
// from one angle and clamped to invisible from another.
cube(60, center = true);
translate([0, 1000, 0]) cube();
