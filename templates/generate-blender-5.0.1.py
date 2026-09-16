import bpy
import mathutils
import sys


MAX_SAMPLES = 256
MAX_OBJECTS = 1024
MAX_MATERIALS = 1024
output = sys.argv[sys.argv.index("--") + 1]
scene = bpy.context.scene
source = bpy.data.objects["Frame 0001"]
if "sharp_edge" not in source.data.attributes:
    source.data.attributes.new(name="sharp_edge", type="BOOLEAN", domain="EDGE")

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
for material in materials:
    material.use_fake_user = True

for obj in (obj for obj in bpy.data.objects if obj.name.startswith("Frame ")):
    if "sharp_edge" not in obj.data.attributes:
        obj.data.attributes.new(name="sharp_edge", type="BOOLEAN", domain="EDGE")
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
    mesh.from_pydata([(0, 0, 0), (1, 0, 0), (0, 1, 0)], [(0, 1), (1, 2), (2, 0)], [(0, 1, 2)])
    mesh.attributes.new(name="sharp_edge", type="BOOLEAN", domain="EDGE")
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
camera.data.animation_data_clear()
for frame in (1, 2):
    camera.keyframe_insert(data_path="location", frame=frame)
    camera.keyframe_insert(data_path="rotation_quaternion", frame=frame)
camera.animation_data.action.name = "OpenSCAD Camera"
for frame in (1, 2):
    camera.data.keyframe_insert(data_path="lens", frame=frame)
    camera.data.keyframe_insert(data_path="ortho_scale", frame=frame)
    camera.data.keyframe_insert(data_path="type", frame=frame)
camera.data.animation_data.action.name = "OpenSCAD Camera Data"

# Default Lighting: Dual Directional Sun Headlights parented to camera
for obj in list(bpy.data.objects):
    if obj.type == "LIGHT":
        bpy.data.objects.remove(obj, do_unlink=True)
for light in list(bpy.data.lights):
    bpy.data.lights.remove(light, do_unlink=True)

sun0_data = bpy.data.lights.new(name="OpenSCAD Headlight 0", type="SUN")
sun0_data.energy = 3.0
sun0_data.color = (1.0, 1.0, 1.0)
sun0_data.use_fake_user = True
sun0_obj = bpy.data.objects.new("OpenSCAD Headlight 0", sun0_data)
scene.collection.objects.link(sun0_obj)
sun0_obj.parent = camera

sun1_data = bpy.data.lights.new(name="OpenSCAD Headlight 1", type="SUN")
sun1_data.energy = 1.2
sun1_data.color = (0.92, 0.95, 1.0)
sun1_data.use_fake_user = True
sun1_obj = bpy.data.objects.new("OpenSCAD Headlight 1", sun1_data)
scene.collection.objects.link(sun1_obj)
sun1_obj.parent = camera

dir0 = mathutils.Vector((0.577, 0.577, -0.577)).normalized()
dir1 = mathutils.Vector((-0.577, -0.577, 0.577)).normalized()
sun0_obj.rotation_euler = dir0.to_track_quat("-Z", "Y").to_euler()
sun1_obj.rotation_euler = dir1.to_track_quat("-Z", "Y").to_euler()

if scene.world:
    scene.world.use_nodes = True
    bg = scene.world.node_tree.nodes.get("Background")
    if bg:
        bg.inputs["Color"].default_value = (0.05, 0.05, 0.06, 1.0)
        bg.inputs["Strength"].default_value = 0.4

scene.frame_start = 1
scene.render.engine = "CYCLES"
scene.cycles.device = "GPU"
scene.cycles.samples = 128
scene.cycles.use_denoising = True
scene.render.threads_mode = "AUTO"
try:
    cpref = bpy.context.preferences.addons["cycles"].preferences
    cpref.compute_device_type = "CUDA"
    cpref.get_devices()
    for d in cpref.devices:
        if d.type in ("CUDA", "OPTIX", "METAL", "HIP", "ONEAPI"):
            d.use = True
except Exception:
    pass
bpy.context.preferences.filepaths.use_file_compression = False
bpy.ops.wm.save_as_mainfile(filepath=output, compress=False)
