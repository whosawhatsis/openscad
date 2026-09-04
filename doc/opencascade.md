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
- Hulls of any operands, exact through one of two constructions and tessellated otherwise.
  Both work by reducing every operand to generators of one kind and then unioning the
  operands, the ruled patches spanning generator pairs, and a core polytope whose vertices
  are the contact points of the planes that support the whole set.
  - **Balls.** Spheres and planar-faced solids -- cubes, polyhedra, imported meshes,
    extruded polygons -- mix freely in any number, because a vertex is a ball of radius zero.
  - **Disks.** Cylinders and cones mix with planar-faced solids in any number, provided
    their axes are parallel, because a cylinder is the hull of its two rim circles and a cone
    is the hull of a rim circle and its apex; a vertex is a disk of radius zero. The
    construction runs in the frame of the shared axis, so that axis need not be Z.
  - Anything else -- a sphere mixed with a cylinder, a torus, a revolution, a spline surface
    -- has every operand reduced to the vertices of a tessellation before hulling, so
    `hull()` always produces a result rather than failing. That result is a planar-faced
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

**Some hulls are approximate, and say so.** Operand sets that fit neither the ball nor the
disk construction -- a sphere mixed with a cylinder, cylinders whose axes are not parallel,
and every smooth 2D curve that is not a circle: SVG Bezier and rational-conic profiles, DXF
splines, text outlines, and nonuniformly scaled circles, all of which extrude to Bezier or
B-spline faces -- have every operand tessellated to its vertices and hulled as a polyhedron.
The result is inscribed in the true hull, so it is slightly small, and it exports as facets
rather than analytic surfaces. A warning names the hull when this happens, because otherwise
the result is indistinguishable from exact geometry.

The tessellation follows `$fa`/`$fs`, derived the same way as at the viewport and mesh-export
boundary, so an approximate hull refines when the model asks for finer facets. It is
coarsened past what was asked for if that would produce more points than the incremental
point hull can process, so refinement saturates on complex profiles.

Mixing an exact operand with a tessellated one is deliberately not done: one sphere among a
few hundred mesh vertices puts the construction's cubic tangent-plane pass into the millions
of points, for a result no better than tessellating throughout.

Closing the remaining gaps means a third generator kind, or a bridge between the two that
exist. The ruled patch between a *disk and a ball* is the missing piece for hulling a
cylinder with a sphere; non-parallel cylinder axes need tangency between circles in
different planes, where the equal-angle correspondence that makes the disk construction work
no longer holds.

**Smooth 2D curves other than circles are the largest gap, and the hardest.** The disk
construction works because a circle's parameter angle *is* its support direction, so the
ruled patch between two disks is the loft that corresponds them angle for angle. For any
other curve those two parametrisations differ, and a loft between the curves as authored is
not the hull patch; corresponding them by support direction means rebuilding the curves,
which is exactly the exactness the construction is trying to preserve.

The tractable route is to restrict to profiles sharing their planes -- the common case, since
`hull()` of 2D shapes and of equal-height prisms both land there -- where the hull is a *2D*
convex hull extruded, with a boundary of arcs of the original curves joined by bitangent
segments. That needs a bitangent between two arbitrary curves, which OpenCASCADE offers only
as `Geom2dGcc_Lin2d2Tan`, an iterative solver requiring a seed: a missed bitangent is a
silently wrong hull rather than a failure, so it needs a seeding strategy that can be argued
to be exhaustive. That is the piece of work, and it is why this is still a tessellated case. Native profile
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
