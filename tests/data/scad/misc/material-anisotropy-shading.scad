// Anisotropic shading test model (feature 64), driven by -D mode=N.
//
// The rotation pair (modes 5/6) puts the SAME rotate() on both sides and moves
// only where material() sits relative to it:
//
//   material(...) rotate(...) sphere()   axis captured after the rotation, so
//                                        the rotation never reaches it
//   rotate(...) material(...) sphere()   axis captured inside, so the rotation
//                                        transforms it with the geometry
//
// Both render byte-identical geometry -- same tessellation, same silhouette,
// same facets facing the camera -- so any difference is the axis alone.
//
// The first draft rotated the object in one mode and not the other, on the
// assumption that a rotated sphere looks the same. It does not: at $fn = 64 a
// 90-degree turn changes which facets face the camera, so that version differed
// for reasons unrelated to anisotropy and the axis assertion passed vacuously.
// The isotropic control below is what caught it, and is why it is here.

mode = 0;

module subject() sphere(r = 20, $fn = 64);

if (mode == 0) color("red") subject();
else if (mode == 1) material(c = "red", roughness = 0.25) subject();
else if (mode == 2) material(c = "red", roughness = 0.25, anisotropy = 0) subject();
else if (mode == 3) material(c = "red", roughness = 0.25, anisotropy = 0.9) subject();
else if (mode == 4) material(c = "red", roughness = 0.25, anisotropy = -0.9) subject();
// 5 and 6: identical geometry, differing only in whether the rotation reaches
// the anisotropy axis.
else if (mode == 5) material(c = "red", roughness = 0.25, anisotropy = 0.9) rotate([90, 0, 0]) subject();
else if (mode == 6) rotate([90, 0, 0]) material(c = "red", roughness = 0.25, anisotropy = 0.9) subject();
// 7 and 8: the isotropic control for that pair. With no anisotropy there is no
// axis to move, so these two must be pixel-identical -- if they are not, the
// pair above differs for some reason other than the axis and proves nothing.
else if (mode == 7) material(c = "red", roughness = 0.25) rotate([90, 0, 0]) subject();
else if (mode == 8) rotate([90, 0, 0]) material(c = "red", roughness = 0.25) subject();
