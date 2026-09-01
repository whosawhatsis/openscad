# Experimental OpenCASCADE backend

Build with `ENABLE_OPENCSCADE=ON`, `EXPERIMENTAL=1`, and `SNAPSHOT=1` to enable
the optional backend. Select OpenCASCADE as the 3D rendering backend in
Preferences, or use `--backend=opencascade` on the command line. Builds without
OpenCASCADE do not offer the option. The View menu's CAD Shaded option uses
retained BREP geometry for preview and rendered views.

Smooth geometry stays in BREP; viewport triangulation is separate from mesh
export tessellation. Explicit polygonal inputs remain polygonal. STEP/IGES I/O
is not implemented yet.

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
  and custom `stroke-miterlimit` values. Self-intersecting even-odd contours are split
  into exact bounded planar regions before extrusion.
- DXF profiles retain circles, circular arcs, ellipses, LINE/LWPOLYLINE edges,
  bulged polyline arcs, and resolved block INSERT transforms (including base points, rotation,
  and nonuniform scale). Layers, import origin/scale, centering, explicit `$fn`,
  and even-odd holes are supported. Endpoints join within 1e-7 model units; open
  paths close with a straight segment, as in the existing DXF importer.
- Round, miter, and chamfer offsets of supported profiles, and both cut and shadow
  projections used as extrusion profiles. Shadow projection uses OCCT silhouettes
  and native region classification, not a rendered image or triangle mesh.
- Polyhedral hulls; smooth hulls of two spheres or two parallel vertical cylinders
  with matching cap heights (including extruded two-circle hulls).
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

This is not yet a replacement for every CGAL/Manifold operation. General curved
hulls, general curved-operand Minkowski sums, DXF spline entities and INSERT
arrays/forward block references, nonuniformly transformed strokes, and negative extrusion scales
remain unsupported. Native profile
operations are currently consumed through extrusion; standalone 2D evaluation
still uses the existing polygon pipeline. Complex offsets/projections can fail
OCCT construction or validity checks. Self-intersecting nonzero-winding contours remain unsupported.
Unsupported paths report errors instead
of deliberately tessellating the input and handing the operation to a mesh kernel.

There is no arbitrary face/edge selection interface for fillets.

## Focused verification

Run `OpenSCADUnitTests '[brep]'` and
`ctest -R '(preview-png|render-png|export-stl)-brep-' --output-on-failure` in the
build directory. The native-operations fixture exercises text, offsets, both
projection modes, hulls, Minkowski sums, mesh imports, and height fields together.
