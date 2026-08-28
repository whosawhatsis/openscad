import bpy
import sys


MAX_SAMPLES = 256
output = sys.argv[sys.argv.index("--") + 1]
scene = bpy.context.scene
source = bpy.data.objects["Frame 0001"]

for obj in list(bpy.data.objects):
    if obj.name.startswith("Stable "):
        bpy.data.objects.remove(obj, do_unlink=True)

for index in range(1, MAX_SAMPLES + 1):
    name = f"Stable {index:04d}"
    obj = source.copy()
    obj.name = name
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata([(0, 0, 0), (1, 0, 0), (0, 1, 0)], [], [(0, 1, 2)])
    for material in source.data.materials:
        mesh.materials.append(material)
    obj.data = mesh
    scene.collection.objects.link(obj)
    obj.animation_data_clear()
    obj.rotation_mode = "QUATERNION"
    obj.hide_render = True
    obj.hide_viewport = True
    for frame in (1, 2):
        obj.keyframe_insert(data_path="hide_render", frame=frame)
        obj.keyframe_insert(data_path="hide_viewport", frame=frame)
        obj.keyframe_insert(data_path="location", frame=frame)
        obj.keyframe_insert(data_path="rotation_quaternion", frame=frame)
    obj.animation_data.action.name = name
    for curve in obj.animation_data.action.fcurves:
        for point in curve.keyframe_points:
            point.interpolation = "CONSTANT" if curve.data_path.startswith("hide_") else "LINEAR"

scene.frame_start = 1
bpy.context.preferences.filepaths.use_file_compression = False
bpy.ops.wm.save_as_mainfile(filepath=output, compress=False)
