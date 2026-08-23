// Surface finish on material() and color() (feature 15).
//
// Two distinct, independent attributes that are easy to confuse:
//
//   finish = scale                      procedural bump. Implicit 3D coherent
//   finish = [scale, strength]          noise sampled in object-local coords,
//   finish = [scale, strength, seed]    perturbing only the lighting normal.
//                                       Detail you can see individually.
//                                         scale     wavelength in mm, > 0
//                                         strength  normal tilt, defaults to 1
//                                         seed      optional, defaults to 0
//
//   roughness = <number in [0,1]>       conventional scalar PBR microfacet
//                                       roughness. 0 = mirror, 1 = fully rough.
//                                       Sub-pixel and statistical: widens the
//                                       specular lobe, moves no normal.
//
//   metallic  = <number in [0,1]>       PBR metalness. Ships with roughness
//                                       because metallic-roughness is a pair,
//                                       and glTF defaults metallic to 1.0 --
//                                       writing roughness alone would export
//                                       everything as metal.
//
// None of these change geometry: every cube below exports identically as a mesh.

// --- procedural bump (finish) ---

// 1. material() with scale and strength
material(c = "red", finish = [2, 0.5]) cube(10);

// 2. material() with an explicit seed
material(c = "red", finish = [2, 0.5, 7]) cube(10);

// 3. color() carries the same attribute, as a per-surface (not per-body) property
color("blue", finish = [1, 0.25]) cube(10);

// 4. a bare scalar is the scale, with strength 1 and seed 0. Unambiguous here in
//    a way it would not be under the name "roughness", where a scalar means the
//    PBR microfacet value instead.
material(c = "red", finish = 2) cube(10);
material(c = "red", finish = [2, 1, 0]) cube(10);

// 5. omitted seed is exactly equivalent to seed = 0
material(c = "green", finish = [3, 1]) cube(10);
material(c = "green", finish = [3, 1, 0]) cube(10);

// --- scalar PBR ---

// 6. roughness alone
material(c = "red", roughness = 0.4) cube(10);

// 7. metallic alone
material(c = "red", metallic = 1) cube(10);

// 8. the metallic-roughness pair, which is how PBR is normally written
material(c = "red", roughness = 0.2, metallic = 0.9) cube(10);

// 9. roughness is a scalar and finish is a vector, so both coexist on one object
//    without ambiguity: visible bumps on a semi-glossy surface
material(c = "red", finish = [2, 0.5], roughness = 0.3, metallic = 0) cube(10);

// 10. color() takes the PBR scalars too
color("blue", roughness = 0.6) cube(10);

// --- neither: dump must be byte-identical to before the feature existed ---

material(c = "red") cube(10);
color("blue") cube(10);
