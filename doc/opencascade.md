# Experimental OpenCASCADE backend

Build with `ENABLE_OPENCASCADE=ON`, `EXPERIMENTAL=1`, and `SNAPSHOT=1` to enable
the optional backend. Select OpenCASCADE as the 3D rendering backend in
Preferences, or use `--backend=opencascade` on the command line. Builds without
OpenCASCADE do not offer the option. The View menu's CAD Shaded option uses
retained BREP geometry for preview and rendered views.

Smooth geometry stays in BREP; viewport triangulation is separate from mesh
export tessellation. Explicit polygonal inputs remain polygonal. STEP export writes retained BREP
geometry directly without tessellation. STEP import retains BREP with the OpenCASCADE backend; with
CGAL or Manifold it tessellates through OpenCASCADE using the active `$fn` or `$fa`/`$fs` settings.
IGES import/export follows the same retained-BREP and backend-aware tessellation rules.

## Native operation coverage

- Primitives, transforms, groups, and recursive union/difference/intersection.
- Boolean-local fillets on newly created edges.
- Linear extrusion, including taper, twist, apex and one-axis collapse; rotational
  extrusion. Twist uses tolerance-controlled smooth splines.
- Text extrusion from the font's original quadratic/cubic Bezier curves, retaining
  shaping, spacing, alignment, holes, and nonzero-winding contour overlaps.
- SVG extrusion retains line, quadratic/cubic Bezier, and elliptical-arc paths,
  circles, ellipses, polygons, and plain/rounded rectangles. Conics use exact
  rational curves, including after SVG transforms. ID/layer selection, page
  scaling, and centering are retained. Explicit `$fn` makes imported circles
  polygonal. Native import supports nonzero and even-odd fill rules, including
  inherited rules and local overrides. Fill rules apply within each shape;
  separate shapes are unioned. Open line, Bezier, and conic strokes support butt,
  round, and square caps; multi-segment and closed paths support round, bevel, and miter joins.
  Stroke width, cap, join, and miter-limit properties inherit from SVG groups. Stroke ribbons
  use retained OCCT offset curves rather than sampled polygons. Similarity transforms
  and uniform page scaling are supported. Miter joins honor SVG's default limit of 4
  and custom `stroke-miterlimit` values. Self-intersecting contours are split into exact
  bounded planar regions and classified with their even-odd or nonzero winding rule before extrusion.
- DXF profiles retain circles, circular arcs, ellipses, LINE/LWPOLYLINE edges,
  bulged polyline arcs, and resolved block INSERT transforms (including base points, rotation,
  and nonuniform scale). Layers, import origin/scale, centering, explicit `$fn`,
  and even-odd holes are supported. Endpoints join within 1e-7 model units; open
  paths close with a straight segment, as in the existing DXF importer.
- Round, miter, and chamfer offsets of supported profiles, and both cut and shadow
  projections used as extrusion profiles. Shadow projection uses OCCT silhouettes
  and native region classification, not a rendered image or triangle mesh.
- Hulls of any operands. Spheres and planar-faced solids -- cubes, polyhedra, imported
  meshes, extruded polygons -- mix freely in any number and are exact: a vertex is treated
  as a sphere of radius zero, so both reduce to the same construction, which unions the
  balls, their pairwise tangent envelopes, and the convex hull of the centres together with
  every tri-tangent point. Two vertical cylinders are exact through separate constructions:
  parallel with matching cap heights, separated coaxial, equal radii translated in 3D with
  unequal heights, or equal heights translated with unequal radii (including extruded
  two-circle hulls). **Any other operand -- a cone, a torus, a revolution, a spline surface,
  or three or more cylinders -- is reduced to the vertices of a tessellation before hulling**,
  so `hull()` always produces a result rather than failing. That result is a planar-faced
  solid inscribed in the true hull, not exact geometry; see *Current limitations*.
- Minkowski sums of polyhedra, including nonconvex operands; polyhedron/sphere rounding;
  sphere/sphere sums; vertical constant-section prisms with a vertical cylinder
  (including planar circle offsets). Operands must fit a supported path at each
  step of a multi-operand sum.
  Nonconvex polyhedra are split into convex cells using native face planes, then
  their component sums are unioned. This preserves recesses without a mesh kernel.
  Complexity limits are 256 splitting planes, 4096 cells/component pairs, and
  one million vertex pairs per convex sum; complicated inputs can be expensive.
- Height fields and mesh imports become intentionally faceted BREP solids.
  Multi-object AMF/3MF imports are combined with native booleans.
- Polyhedra with disconnected shells, cavities, and nested islands. Shells must
  be closed, manifold, and mutually nonintersecting; nesting determines cavities.
  Independently imported objects are unioned instead, not interpreted as cavities.

## Current limitations

This is not yet a replacement for every CGAL/Manifold operation. General
curved-operand Minkowski sums, DXF spline entities and INSERT
arrays/forward block references, nonuniformly transformed strokes, and negative extrusion scales
remain unsupported.

**Hulls of curved operands other than spheres are approximate.** Cones, tori, revolutions,
spline surfaces, and any set of three or more cylinders are tessellated to their vertices
and hulled as a polyhedron, at a chord tolerance of a thousandth of the operand's bounding
diagonal. The result is inscribed in the true hull, so it is slightly small, and it exports
as facets rather than analytic surfaces. This is a deliberate stopgap: it keeps `hull()`
working on any model instead of failing on unsupported combinations.

The way out is a second generator kind. The exact path today reduces every operand to
*balls*, which is why spheres and vertices need no per-combination code. A cylinder is the
hull of two disks and a cone is the hull of a disk and a point, so adding a **disk
generator** alongside the ball generator would make cylinders and cones exact for any number
of operands and in any mix with spheres and meshes, and would delete the pairwise cylinder
constructions listed above -- the last combination-specific paths in the hull. The piece
that does not yet exist is the ruled patch between a disk and a ball or another disk, which
is what `BRepOffsetAPI_ThruSections` already builds for the existing unequal-radius case.
Surfaces that are neither planar, spherical, nor circular-sectioned would still tessellate. Native profile
operations are currently consumed through extrusion; standalone 2D evaluation
still uses the existing polygon pipeline. Complex offsets/projections can fail
OCCT construction or validity checks can still reject degenerate imported contours.
Outside `hull()`, unsupported paths report errors instead
of deliberately tessellating the input and handing the operation to a mesh kernel.

There is no arbitrary face/edge selection interface for fillets.

## Focused verification

Run `OpenSCADUnitTests '[brep]'` and
`ctest -R '(preview-png|render-png|export-stl)-brep-' --output-on-failure` in the
build directory. The native-operations fixture exercises text, offsets, both
projection modes, hulls, Minkowski sums, mesh imports, and height fields together.
