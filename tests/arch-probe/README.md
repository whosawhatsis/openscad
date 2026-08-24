# Architecture-divergence probe

Detects OpenSCAD geometry that depends on the CPU architecture the binary runs on.

Background: openscad/openscad#6161. `path::arc_to()` feeds a `ceil()` that picks an arc's
segment count, so a sub-ulp floating-point difference flips the count and re-samples the whole
arc. On arm64 Clang contracts `a*b + c` into a single `fma` (one rounding step); the x86-64
baseline has no FMA instruction and rounds after every multiply. Same source, different output.

## Usage

Run on two machines of different architectures, from the same commit, then diff:

```sh
tests/arch-probe/run-probe.py <path-to-openscad-binary> report-$(uname -m).txt
```

Any model whose hash differs between the two reports produces architecture-dependent geometry.
`10-svg-arcs` is a **known-positive control**: with an unpatched binary it must differ, or the
harness is not measuring what it claims.

## What the hash covers, and why

The hash is canonical: facet normals are ignored, vertices are sorted, and coordinates are
quantised to 1e-6 before hashing. Each of those removes a false positive found the hard way when
this harness was first written against raw file hashes:

* **Normals** are cross products; they diverge at the 1e-16 level between architectures for free,
  and every consumer recomputes them. A raw hash flagged all ten models on that alone.
* **Order** is not stable for minkowski and hull. An order-sensitive comparison reported a
  whole-model difference for geometry that was in fact identical to 1e-15.
* **Quantisation** keeps ordinary floating-point noise out. Real divergence of this class flips an
  integer segment count and moves vertices by ~1e-1 — eight orders of magnitude clear of the
  threshold. A value sitting exactly on a quantisation boundary could still flip; that would show
  as a one-off difference with an unchanged vertex count, which is the signature to check for
  before believing a positive.

## Why hashes and not goldens

Committed golden files only detect divergence from whichever architecture generated them, and go
stale on every legitimate tessellation change. Comparing two live runs of the same commit isolates
the architecture as the only variable.

## Coverage and limits

The corpus targets code paths where a floating-point value is rounded to an integer segment or
slice count: `$fa`/`$fs` circle discretisation, cone tessellation, `rotate_extrude` angle,
`linear_extrude` twist slices (`getHelixSlices`), Clipper2 rounded offset joins, Minkowski and
hull over curved inputs, and SVG arc import.

This is a **detector, not a proof**. A model that matches shows no divergence *for those inputs* —
divergence is triggered by values that land near a rounding boundary, so a negative result is
evidence, not a guarantee. `02-sphere-near-boundary` deliberately sweeps radii in irregular steps
to raise the chance of straddling one.

ASCII STL is used because it is textual and full-precision. Vertex order must be deterministic for
the hash to be meaningful; if a future change makes export order depend on thread scheduling, this
harness will produce false positives and needs a canonicalising sort.
