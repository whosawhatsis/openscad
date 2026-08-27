# Blender 5.0.1 export template

`blender-5.0.1.blend` is a dependency-closed library file generated with the
official Blender 5.0.1 Linux build. It contains one scene, one animated frame
object, a camera, an area light, a world, and one material. The direct `.blend`
exporter clones and patches this graph without requiring Blender to be installed.

The template embeds Blender 5.0.1's SDNA and serialized Blender data structures.
Blender is licensed under GPL-2.0-or-later, which is compatible with OpenSCAD's
GPL-2.0-or-later license. Source tag: `v5.0.1`, commit
`a3db93c5b2595a79f65f304114c23aeef8c2139f`.

Generation settings:

- uncompressed blend-file format 1 (`BLENDER17-01v0500`);
- EEVEE renderer, 320×240, 24 fps;
- a quad mesh visible only at frame 1, with boolean visibility keyed at frames
  0, 1, and 2;
- one camera, one area light, one diffuse material, and a neutral world.

SHA-256: `dab1a9c5d17710060a0a73e58af320b83b5e8065fb7804dd0112f6efd7e5a7fc`
