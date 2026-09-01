// Self-shadowing: a tall wall standing on a flat plate, so the wall throws a
// shadow across the plate.
//
// Both lights are eye-space directional and parented to the camera - light 0
// arrives from up-left-toward-the-viewer - so the geometry is arranged for that
// rig rather than for a world-fixed sun. There is deliberately no ground plane:
// the shadow receiver is part of the model, which is the whole scope of this
// feature. A user who wants a shadow on the floor adds a floor.
color([.8, .8, .8]) {
  translate([0, 0, -2]) cube([80, 80, 4], center = true);
  translate([-14, 0, 15]) cube([6, 60, 30], center = true);
}
