// Anisotropic roughness on material() and color() (feature 64).
//
//   anisotropy = <number in [-1,1]>   directional bias of the specular lobe.
//                                     0 is isotropic. Positive smears
//                                     reflections ALONG the layer/turning
//                                     direction, negative ACROSS it. It is the
//                                     look of a 3D print or a lathed part,
//                                     where each layer is a rounded bead
//                                     acting as a tiny cylindrical lens.
//
// The range is [-1,1], not [0,1] like roughness and metallic: the sign carries
// a 90-degree rotation of the lobe rather than a magnitude.
//
// Geometry is unchanged: every cube below exports identically as a mesh.

// 1. positive: smeared along the layers
material(c = "red", anisotropy = 0.5) cube(10);

// 2. negative: the same lobe turned 90 degrees, smeared across them
material(c = "red", anisotropy = -0.5) cube(10);

// 3. the extremes are legal and must survive as themselves
material(c = "red", anisotropy = 1) cube(10);
material(c = "red", anisotropy = -1) cube(10);

// 4. out of range clamps, with a warning, and the CLAMPED value is what reaches
//    the dump -- the dump is the geometry cache key, so it has to describe the
//    surface that will actually be drawn, not the number that was typed
material(c = "red", anisotropy = 3) cube(10);
material(c = "red", anisotropy = -3) cube(10);

// 5. anisotropy = 0 is isotropic, but explicitly set: distinct from unset,
//    because "the author chose isotropic" and "the author said nothing" are
//    different statements even though they shade identically today
material(c = "red", anisotropy = 0) cube(10);

// 6. combines with the scalar PBR pair, which is the normal way to write it:
//    a fairly glossy surface smeared in one direction
material(c = "red", roughness = 0.3, metallic = 0, anisotropy = 0.7) cube(10);

// 7. color() carries it too, as a per-surface property
color("blue", anisotropy = 0.4) cube(10);

// --- unset: dump must be byte-identical to before the feature existed ---

material(c = "red", roughness = 0.3) cube(10);
color("blue") cube(10);
