"""Convert the original OGA people and VitaVehicle car into small, textured-free game assets.

Run with Blender 4.5: blender --background --disable-autoexec --python tools/import-street-assets.py.
Download the three source files to .test/import-source as described in public/people/SOURCE.md.
The original meshes/weights stay intact; clothing materials, headgear and in-place clips are ours.
Godot scripts are never run: the car's packed vertex/index arrays are decoded as data only.
"""
import math
import re
import struct
from pathlib import Path

import bpy
from mathutils import Matrix, Quaternion, Vector

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / '.test/import-source'


def material(name, rgb):
  m = bpy.data.materials.new(name)
  m.diffuse_color = (*rgb, 1)
  return m


def export(path, objects, copyright):
  bpy.ops.object.select_all(action='DESELECT')
  for o in objects:
    o.hide_render = False
    o.hide_viewport = False
    o.hide_set(False)
    o.select_set(True)
  path.parent.mkdir(parents=True, exist_ok=True)
  bpy.ops.export_scene.gltf(filepath=str(path), export_format='GLB', use_selection=True,
    export_animations=True, export_animation_mode='ACTIONS', export_force_sampling=True,
    export_frame_range=False, export_extras=True, export_copyright=copyright)


def person(source, mesh_name, rig_name, output, author, runner=False):
  try:
    bpy.ops.wm.open_mainfile(filepath=str(source), load_ui=False, use_scripts=False)
  except RuntimeError as error:
    # Blender 4.5 discards one broken legacy shape-key pointer in the 2019 original, after loading its mesh/rig.
    if 'invalid' not in str(error) or 'Shapekey' not in str(error) or mesh_name not in bpy.data.objects:
      raise
  mesh, rig = bpy.data.objects[mesh_name], bpy.data.objects[rig_name]
  for c in bpy.data.collections:
    c.hide_render = c.hide_viewport = False
  mesh.hide_render = mesh.hide_viewport = rig.hide_render = rig.hide_viewport = False
  mesh.hide_set(False)
  rig.hide_set(False)
  world = mesh.matrix_world.copy()
  mesh.parent = rig
  mesh.matrix_world = world
  rig.animation_data_clear()
  for a in list(bpy.data.actions):
    bpy.data.actions.remove(a)
  for m in list(mesh.modifiers):
    if m.type == 'SUBSURF':
      mesh.modifiers.remove(m)  # the author's low-poly cage is the intended runtime mesh
  if mesh.data.shape_keys:
    mesh.shape_key_clear()
  mesh.data.validate()
  mesh.data.update()
  mesh.data.materials.clear()
  names = ['Shirt', 'Pants', 'Skin', 'Hair', 'Face']
  colors = [(0.12, 0.36, 0.45) if runner else (0.32, 0.55, 0.65), (0.08, 0.1, 0.16),
    (0.7, 0.47, 0.32), (0.035, 0.045, 0.07), (0.015, 0.02, 0.03)]
  mats = [material(n, c) for n, c in zip(names, colors)]
  for m in mats:
    mesh.data.materials.append(m)
  points = [mesh.matrix_world @ v.co for v in mesh.data.vertices]
  center_x = (min(v.x for v in points) + max(v.x for v in points)) / 2
  center_y = (min(v.y for v in points) + max(v.y for v in points)) / 2
  bottom, top = min(v.z for v in points), max(v.z for v in points)
  height = top - bottom
  for p in mesh.data.polygons:
    c = sum((points[i] for i in p.vertices), Vector()) / len(p.vertices)
    z = (c.z - bottom) / height
    # A complete outfit, including sleeves: only the head and hands retain skin.
    p.material_index = 2 if z > 0.84 or abs(c.x - center_x) > height * 0.27 else 3 if z < 0.09 else 1 if z < 0.51 else 0
    p.use_smooth = False
  objects = [mesh, rig]
  bones = rig.pose.bones
  old = 'Upperarm.Left' in bones
  key = (lambda n, side='': {'root': 'Back', 'spine': 'Back', 'head': 'Head', 'upper': 'Upperarm', 'fore': 'Forearm',
    'thigh': 'Thigh', 'shin': 'Shin'}[n] + ('.' + ('Left' if side == 'L' else 'Right') if side else '')) if old else (
    lambda n, side='': {'root': 'hips', 'spine': 'chest', 'head': 'head', 'upper': 'upper_arm', 'fore': 'forearm', 'thigh': 'thigh', 'shin': 'shin'}[n] + ('.' + side if side else ''))
  for p in bones:
    p.rotation_mode = 'QUATERNION'
    p.rotation_quaternion = Quaternion()
    p.location = (0, 0, 0)
  bpy.context.view_layer.update()
  # Both source rigs have different rest poses. Bring limbs to a common relaxed stance before keying motion.
  for role in ['upper', 'fore', 'thigh', 'shin']:
    for side in ['L', 'R']:
      p = bones[key(role, side)]
      world = rig.matrix_world @ p.matrix
      current = world.to_quaternion() @ Vector((0, 1, 0))
      wanted = Vector(((0.1 if role == 'upper' else 0.02 if role == 'thigh' else 0) * (1 if side == 'L' else -1), -0.04 if role == 'fore' else 0, -1)).normalized()
      turn = current.rotation_difference(wanted)
      p.matrix = rig.matrix_world.inverted() @ Matrix.LocRotScale(world.translation, turn @ world.to_quaternion(), world.to_scale())
      bpy.context.view_layer.update()
  base = {p.name: p.rotation_quaternion.copy() for p in bones}
  basis = {p.name: (rig.matrix_world @ p.matrix).to_quaternion() for p in bones}

  def rotate(name, x=0, y=0, z=0):
    q = basis[name]
    delta = Quaternion((0, 0, 1), z) @ Quaternion((0, 1, 0), y) @ Quaternion((1, 0, 0), x)
    bones[name].rotation_quaternion = base[name] @ q.inverted() @ delta @ q

  # Simple rigid accessories share the head's skinning, rather than making per-frame attachment work.
  head = bones[key('head')]
  head_world = rig.matrix_world @ head.head
  def accessory(name, location, scale, mat, sphere=False):
    if sphere:
      bpy.ops.mesh.primitive_uv_sphere_add(segments=12, ring_count=6, location=location)
    else:
      bpy.ops.mesh.primitive_cube_add(size=2, location=location)
    o = bpy.context.object
    o.name = name
    o.scale = scale
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    o.data.materials.append(mat)
    # Bake world placement into vertices, so the same armature modifier works on every source rig.
    transform = o.matrix_world.copy()
    for v in o.data.vertices:
      v.co = transform @ v.co
    o.matrix_world = Matrix.Identity(4)
    group = o.vertex_groups.new(name=key('head'))
    group.add(list(range(len(o.data.vertices))), 1, 'REPLACE')
    mod = o.modifiers.new('Head attachment', 'ARMATURE')
    mod.object = rig
    o.parent = rig
    o.matrix_world = Matrix.Identity(4)
    objects.append(o)

  face_z = bottom + height * 0.925
  face_y = min(v.y for v in points if v.z > bottom + height * 0.89)
  for sign in [-1, 1]:
    accessory('Eye', (head_world.x + sign * height * 0.024, face_y - 0.008, face_z), (0.015, 0.01, 0.018), mats[4])
  accessory('Cap', (head_world.x, head_world.y + 0.015, top - height * 0.035), (height * 0.077, height * 0.082, height * 0.047), mats[3], True)
  if runner:
    accessory('Visor', (0, face_y - 0.014, face_z), (height * 0.06, 0.012, height * 0.022), mats[3])

  rig['author'] = author
  rig['adaptation'] = 'Komorebi: outfit materials, headgear, in-place animation clips; original mesh and weights'
  bpy.context.scene.render.fps = 24
  rig.animation_data_create()
  for name, duration in [('Idle', 2), ('Walk', 1), ('Run', 0.65), ('Punch', 0.65), ('HitReact', 0.4), ('Death', 1.1)]:
    action = bpy.data.actions.new(name)
    action.use_fake_user = True
    rig.animation_data.action = action
    frames = max(2, round(duration * 24))
    for f in range(frames + 1):
      u = f / frames
      phase = u * math.tau
      for p in bones:
        p.rotation_quaternion = base[p.name]
        p.location = (0, 0, 0)
      if name in ['Walk', 'Run']:
        running = name == 'Run'
        for side, sign in [('L', 1), ('R', -1)]:
          swing = math.sin(phase) * sign
          rotate(key('thigh', side), x=swing * (0.8 if running else 0.48))
          rotate(key('shin', side), x=max(0, -swing) * (1.25 if running else 0.7))
          rotate(key('upper', side), x=-swing * (0.65 if running else 0.35))
          rotate(key('fore', side), x=-0.9 if running else -0.12)
        rotate(key('spine'), x=0.12 if running else 0.025, z=0.045 * math.sin(phase))
      elif name == 'Punch':
        hit = math.sin(math.pi * u) ** 2
        rotate(key('upper', 'R'), x=-1.45 * hit)
        rotate(key('fore', 'R'), x=-0.6 * (1 - hit))
        rotate(key('spine'), z=0.2 * hit)
      elif name == 'HitReact':
        rotate(key('spine'), x=-0.35 * math.sin(math.pi * u))
      elif name == 'Death':
        fall = min(1, u * 1.4)
        rotate(key('root'), x=1.5 * fall)
        root = bones[key('root')]
        local_up = (rig.matrix_world @ root.bone.matrix_local).to_quaternion().inverted() @ Vector((0, 0, -0.85 * fall))
        root.location = local_up
      else:
        rotate(key('spine'), x=0.012 * math.sin(phase))
        rotate(key('head'), z=0.025 * math.sin(phase))
      for p in bones:
        if p.name in [key(r, side) for r in ['upper', 'fore', 'thigh', 'shin'] for side in ['L', 'R']] + [key('root'), key('spine'), key('head')]:
          p.keyframe_insert('rotation_quaternion', frame=f + 1)
          p.keyframe_insert('location', frame=f + 1)
    for fc in action.fcurves:
      for p in fc.keyframe_points:
        p.interpolation = 'LINEAR'
  rig.animation_data.action = None
  for p in bones:
    p.rotation_quaternion = base[p.name]
    p.location = (0, 0, 0)
  bpy.context.scene.frame_set(1)
  origin = bpy.data.objects.new('StreetPerson', None)
  bpy.context.collection.objects.link(origin)
  world = rig.matrix_world.copy()
  rig.parent = origin
  rig.matrix_world = world
  origin.location = (-center_x, -center_y, -bottom)
  objects.append(origin)
  export(ROOT / 'public/people' / output, objects, author)


def car():
  bpy.ops.wm.read_factory_settings(use_empty=True)
  text = (SOURCE / 'car.tscn').read_text().split('[sub_resource type="ArrayMesh" id=11]')[0]
  objects = []
  for section in re.finditer(r'surfaces/(\d+) = \{(.*?)\n\}', text, re.S):
    data = section[2]
    name = re.search(r'"name": "([^"]+)"', data)[1]
    count = int(re.search(r'"vertex_count": (\d+)', data)[1])
    packed = bytes(int(n) for n in re.search(r'"array_data": PoolByteArray\( ([^)]*)\)', data)[1].split(','))
    indices = bytes(int(n) for n in re.search(r'"array_index_data": PoolByteArray\( ([^)]*)\)', data)[1].split(','))
    stride = len(packed) // count
    assert stride == 16 and len(packed) == count * stride, 'unexpected Godot vertex format'
    vertices = []
    for i in range(count):
      x, y, z = struct.unpack_from('<3f', packed, i * stride)
      vertices.append((-x * 1.9 / 0.0693243, -(y - 0.0011) * 4.2 / 0.123746, z * 34 + 0.42))
    values = struct.unpack('<' + 'H' * (len(indices) // 2), indices)
    faces = [values[i:i + 3][::-1] for i in range(0, len(values), 3)] # Godot's front winding is clockwise
    g = bpy.data.meshes.new(name)
    g.from_pydata(vertices, [], faces)
    g.validate()
    g.update()
    o = bpy.data.objects.new(name, g)
    bpy.context.collection.objects.link(o)
    g.materials.append(material(name.split('.')[0], (0.2, 0.6, 0.7)))
    objects.append(o)
  groups = {}
  for o in objects:
    n = o.name.split('.')[0]
    role = 'Body' if n == 'Color' else 'Glass' if n in ['Window', 'Mirror'] else 'Taillight' if n in ['RearLight', 'BrakeLight'] else 'Headlight' if n in ['Light', 'ReverseLight'] else 'Accent' if n == 'Signal' else 'Dark'
    groups.setdefault(role, []).append(o)
  merged = []
  for role, parts in groups.items():
    mat = material(role, (0.2, 0.6, 0.7))
    bpy.ops.object.select_all(action='DESELECT')
    for o in parts:
      o.data.materials.clear()
      o.data.materials.append(mat)
      o.select_set(True)
    bpy.context.view_layer.objects.active = parts[0]
    bpy.ops.object.join()
    parts[0].name = role
    merged.append(parts[0])
  export(ROOT / 'public/vehicles/eclipse.glb', merged, 'Eclipse SRC: shotman_16 / VitaVehicle: Jreo; CC BY 4.0')


person(SOURCE / 'pistachio/Human Basemeshes.blend', 'basemesh_female', 'basemesh_female_rig', 'citizen-female.glb', 'pistachio, Human Basemeshes, CC0')
person(SOURCE / 'bellanger/human.blend', 'HumanMale', 'MaleArm', 'citizen-male.glb', 'Clint Bellanger, Base Human Models (Low Poly), CC BY 3.0')
person(SOURCE / 'pistachio/Human Basemeshes.blend', 'basemesh_male', 'basemesh_male_rig', 'runner.glb', 'pistachio, Human Basemeshes, CC0', True)
car()
