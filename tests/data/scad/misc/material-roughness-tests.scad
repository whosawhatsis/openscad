// Procedural bump roughness on material() and color() (feature 15).
//
// roughness is a VECTOR, never a bare scalar: a bare number is reserved for a
// future conventional scalar PBR microfacet roughness and must not be silently
// read as a procedural bump. Form is [scale, strength] or [scale, strength, seed],
// extensible by appending elements, mirroring color()'s [r,g,b] / [r,g,b,a].
//
//   scale     noise wavelength in object-local units (mm). > 0.
//   strength  how far the lighting normal is tilted. 0 = no visible effect.
//   seed      optional, defaults to 0. Deterministic across runs and machines.
//
// roughness never changes geometry, so every cube below exports identically.

// 1. material() with scale and strength
material("red", roughness = [2, 0.5]) cube(10);

// 2. material() with an explicit seed
material("red", roughness = [2, 0.5, 7]) cube(10);

// 3. color() carries the same attribute, as a per-surface (not per-body) property
color("blue", roughness = [1, 0.25]) cube(10);

// 4. no roughness: dump must be byte-identical to before the feature existed
material("red") cube(10);
color("blue") cube(10);

// 5. omitted seed is exactly equivalent to seed = 0
material("green", roughness = [3, 1]) cube(10);
material("green", roughness = [3, 1, 0]) cube(10);
