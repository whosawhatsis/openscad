# Canny edge maps (experimental)

Choose **View → Shading → Canny Edge Map**. No separate feature flag is required,
matching the other shading views.
The image contains white feature edges on black, independent of lighting. **Feature Wireframe**
shows geometric edges only; Canny also includes boundaries between raw colors, including alpha.
Show Edges uses the same geometric edge detector.

Export an 8-bit grayscale PNG from the command line:

```sh
openscad --export-format=cannymap --edge-width=1 -o edges.png model.scad
```

The default uses OpenCSG preview, including meshes produced by `render()` and `minkowski()`.
Add `--render` to export evaluated geometry. The GUI follows the current F5/F6 geometry.

**Feature edge width** in Preferences → 3D View controls Canny, Feature Wireframe, and Show Edges.
The default is 1 logical screen pixel; exports use actual image pixels. `--edge-width` accepts any
finite nonnegative value, including fractions below 1. Zero suppresses all edges. Fractional widths
are sampled at pixel centers and may disappear at low resolution. No antialiasing is applied:
PNG samples are strictly 0 or 255. To smooth the result, render at a higher resolution with a
correspondingly larger edge width and downscale externally.

Surfaces with alpha greater than 0.5 occlude edges. Alpha of exactly 0.5 or less contributes
full-intensity edges without hiding opaque edges behind it. Only the nearest transparent layer is
included; this is not full transparency depth peeling.

Tessellation seams are suppressed using each geometry's smoothing tolerance, derived from twice
its `$fa`. Coplanar seams between equal raw colors do not create edges. Lighting, specular highlights,
and other shading gradients do not create color boundaries.
