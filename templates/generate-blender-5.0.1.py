import bpy
import sys


MAX_SAMPLES = 256
MAX_OBJECTS = 1024
MAX_MATERIALS = 1024
output = sys.argv[sys.argv.index("--") + 1]
scene = bpy.context.scene
source = bpy.data.objects["Frame 0001"]

base_material = source.data.materials[0]
for action in list(bpy.data.actions):
    if action.name.startswith("Stable ") or action.name.startswith("OpenSCAD Camera"):
        bpy.data.actions.remove(action)
for material in list(bpy.data.materials):
    if material != base_material and material.name.startswith("OpenSCAD Material "):
        bpy.data.materials.remove(material, do_unlink=True)
base_material.name = "OpenSCAD Material 0001"
materials = [base_material]
for index in range(2, MAX_MATERIALS + 1):
    material = base_material.copy()
    material.name = f"OpenSCAD Material {index:04d}"
    materials.append(material)

for obj in (obj for obj in bpy.data.objects if obj.name.startswith("Frame ")):
    obj.data.materials.clear()
    obj.data.materials.append(materials[0])
    obj.data.materials.append(materials[1])
    obj.data.polygons[0].material_index = 1

for obj in list(bpy.data.objects):
    if obj.name.startswith("Stable "):
        bpy.data.objects.remove(obj, do_unlink=True)
for mesh in list(bpy.data.meshes):
    if mesh.name.startswith("Stable "):
        bpy.data.meshes.remove(mesh)

for index in range(1, MAX_OBJECTS + 1):
    name = f"Stable {index:04d}"
    obj = source.copy()
    obj.name = name
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata([(0, 0, 0), (1, 0, 0), (0, 1, 0)], [], [(0, 1, 2)])
    mesh.materials.append(materials[0])
    mesh.materials.append(materials[1])
    mesh.polygons[0].material_index = 1
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

camera = scene.camera
camera.name = "OpenSCAD Camera"
camera.data.name = "OpenSCAD Camera"
camera.rotation_mode = "QUATERNION"
camera.animation_data_clear()
for frame in (1, 2):
    camera.keyframe_insert(data_path="location", frame=frame)
    camera.keyframe_insert(data_path="rotation_quaternion", frame=frame)
camera.animation_data.action.name = "OpenSCAD Camera"
scene.frame_start = 1
bpy.context.preferences.filepaths.use_file_compression = False
bpy.ops.wm.save_as_mainfile(filepath=output, compress=False)
