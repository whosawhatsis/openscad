# Blender 5.0.1 export template

`blender-5.0.1.blend` is a normal saved scene generated with the
official Blender 5.0.1 Linux build. It contains one scene, one animated frame
object and mesh for each of 256 remesh slots, 256 hidden persistent-object
slots, a camera, an area light, a world, and one material. The direct `.blend`
exporter fills those authored slots and patches their mesh/action data without
requiring Blender to be installed.

The template embeds Blender 5.0.1's SDNA and serialized Blender data structures.
Blender is licensed under GPL-2.0-or-later, which is compatible with OpenSCAD's
GPL-2.0-or-later license. Source tag: `v5.0.1`, commit
`a3db93c5b2595a79f65f304114c23aeef8c2139f`.

Generation settings:

- uncompressed blend-file format 1 (`BLENDER17-01v0500`);
- EEVEE renderer, 320×240, 24 fps;
- 256 separately authored quad meshes and objects, each visible only at its
  corresponding frame, with boolean visibility keyed around that frame;
- one camera, one area light, one diffuse material, and a neutral world.

The companion `generate-blender-5.0.1.py` script adds 256 hidden persistent-object
slots with Blender-authored transform actions. Run it with Blender 5.0.1 against
this template and pass the replacement path after `--`.

This gives dependency-free export 256 evenly spaced remesh samples while
persistent objects can carry longer transform animations.

SHA-256: `dc3d2830d65483f7062daae72574d3b11455955a58b7b3dc896a33d94015ff49`
