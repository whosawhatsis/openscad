# Blender 5.0.1 export template

`blender-5.0.1.blend` is a normal saved scene generated with the
official Blender 5.0.1 Linux build. It contains one scene, one animated frame
object and mesh for each of 256 remesh slots, 1024 hidden persistent-object
slots, an animated camera, an area light, a world, and 1024 materials. The direct `.blend`
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
- one animated camera, one area light, 1024 Principled materials, and a neutral world.

The companion `generate-blender-5.0.1.py` script adds 1024 hidden persistent-object
slots with Blender-authored transform actions, the material pool, and camera actions for transform,
projection, lens, and orthographic scale. Run it with Blender 5.0.1 against
this template and pass the replacement path after `--`.

This gives dependency-free export up to 256 configurable, evenly spaced remesh samples while
up to 1024 persistent objects can carry longer transform animations. OpenSCAD patches camera,
per-face material, and transform animation data into the authored IDs.

SHA-256: `a11d2c7b7d84da5a3fa517324c2962c00f1dd939d7c9e971831a052d8985ac09`
