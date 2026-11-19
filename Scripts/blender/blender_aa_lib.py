"""Shared headless Blender 4.2 LTS AA asset-authoring library for Komorebi.

Provides procedural mesh builders with uniform metric chamfers/bevels, baked
vertical + cavity/ridge ambient-occlusion vertex colors ('Color' FLOAT_COLOR),
role-based mesh batching ('Body' VertexPaint, 'Signal' emissive, plus optional
centered pivots such as Wheel_FL..Wheel_RR), a 21-bone humanoid rig with smooth
distance-weighted + rigid-pinned skinning, expressive 24 fps animation clips,
and self-verifying binary GLB 2.0 export matching tools/assets.mjs + street-assets.js.

Coordinate convention (Blender Z-up -> glTF/Three.js Y-up via export_yup=True):
  * Blender +X  ->  Three.js +X  (character's LEFT / vehicle's LEFT)
  * Blender -Y  ->  Three.js +Z  (FORWARD direction in game!)
  * Blender +Z  ->  Three.js +Y  (UP / height above ground; z = 0 is ground!)
"""

from __future__ import annotations

import json
import math
import struct
from pathlib import Path
from typing import Iterable, Sequence

import bmesh
import bpy
from mathutils import Euler, Matrix, Quaternion, Vector
from mathutils.geometry import tessellate_polygon

COPYRIGHT = 'Original AA Blender art authored for Komorebi (CC0/MIT).'
FPS = 24

# Coordinate helpers in Blender space (exported with export_yup=True)
LEFT_X = 1.0
RIGHT_X = -1.0
FORWARD_Y = -1.0
BACK_Y = 1.0
UP_Z = 1.0

# PBR Material-Class alpha channels encoded into COLOR_0.a (decoded by src/figure/figure.js)
MAT_CLOTH = 0.06      # matte woven cloth / fabric
MAT_SKIN = 0.18       # realistic human skin + SSS
MAT_HAIR = 0.30       # anisotropic layered hair
MAT_LEATHER = 0.42    # leather / tire rubber / weathered wood
MAT_POLYMER = 0.54    # tactical matte polymer / painted armor / stone / concrete
MAT_METAL = 0.68      # brushed steel / titanium / alloy / chrome / brass
MAT_CARPAINT = 0.82   # glossy clearcoat automotive paint / ceramic glaze
MAT_GLASS = 0.94      # smoked mirror reflective glass / visor / optics

PALETTE_MAT_CLASS = {
    'obsidian': MAT_POLYMER,
    'asphalt_dark': MAT_POLYMER,
    'rubber_tire': MAT_LEATHER,
    'carbon': MAT_POLYMER,
    'gunmetal': MAT_METAL,
    'slate': MAT_POLYMER,
    'steel': MAT_METAL,
    'silver': MAT_METAL,
    'chrome': MAT_METAL,
    'warm_white': MAT_POLYMER,
    'glass_dark': MAT_GLASS,
    'navy_coat': MAT_CLOTH,
    'teal_tech': MAT_CARPAINT,
    'cobalt': MAT_CARPAINT,
    'crimson_coat': MAT_CLOTH,
    'rust_orange': MAT_CARPAINT,
    'hazard_yellow': MAT_POLYMER,
    'olive_tech': MAT_CLOTH,
    'leather_dark': MAT_LEATHER,
    'leather_tan': MAT_LEATHER,
    'denim': MAT_CLOTH,
    'copper': MAT_METAL,
    'brass': MAT_METAL,
    'skin_light': MAT_SKIN,
    'skin_warm': MAT_SKIN,
    'skin_tan': MAT_SKIN,
    'skin_deep': MAT_SKIN,
}


def infer_mat_class(color, name: str = '', rgb: tuple[float, float, float] | None = None) -> float:
    """Infer realistic PBR material-class alpha from palette key, object name, or linear RGB."""
    if isinstance(color, str) and color.strip() in PALETTE_MAT_CLASS:
        return PALETTE_MAT_CLASS[color.strip()]
    nm = (name or '').lower()
    if any(k in nm for k in ('glass', 'windshield', 'canopy', 'window', 'visor', 'lens', 'screen', 'mirror', 'eye')):
        return MAT_GLASS
    if any(k in nm for k in ('tire', 'tyre', 'rubber', 'saddle', 'seat', 'leather', 'boot', 'shoe', 'sole', 'wood', 'cedar', 'plank', 'beam', 'stool', 'grip', 'handle', 'strap', 'belt')):
        return MAT_LEATHER
    if any(k in nm for k in ('hair', 'mohawk', 'brow', 'topknot', 'ponytail')):
        return MAT_HAIR
    if any(k in nm for k in ('skin', 'face', 'head', 'neck', 'hand', 'arm', 'forearm')):
        if not any(m in nm for m in ('helmet', 'mask', 'armor', 'cyber', 'visor', 'metal', 'plate', 'hood', 'bracer', 'sleeve', 'gauntlet')):
            return MAT_SKIN
    if any(k in nm for k in ('rim', 'spoke', 'rotor', 'disc', 'exhaust', 'pipe', 'fork', 'chrome', 'steel', 'metal', 'blade', 'tsuba', 'brass', 'gold', 'alloy', 'piston', 'ram', 'strut', 'barrel', 'rail', 'antenna', 'mast', 'lantern', 'cage', 'ring', 'buckle', 'spike', 'hilt', 'guard', 'cleaver', 'katana')):
        return MAT_METAL
    if any(k in nm for k in ('coat', 'jacket', 'shirt', 'pant', 'trouser', 'jean', 'apron', 'noren', 'curtain', 'cloth', 'hood', 'scarf', 'hakama', 'jinbaori', 'vest', 'suit', 'sleeve', 'fabric')):
        return MAT_CLOTH
    if any(k in nm for k in ('hood_', 'fender', 'bumper', 'door', 'roof', 'fairing', 'body', 'shell', 'nose', 'tail', 'wing', 'spoiler', 'caliper', 'tank', 'car', 'coupe', 'sports', 'van', 'bike', 'scooter')):
        return MAT_CARPAINT
    if rgb is not None:
        r, g, b = rgb
        luma = 0.299 * r + 0.587 * g + 0.114 * b
        mx, mn = max(r, g, b), min(r, g, b)
        sat = (mx - mn) / max(mx, 1e-4)
        if luma < 0.04 and b > r * 1.25:
            return MAT_GLASS
        if luma < 0.06:
            return MAT_LEATHER
        if sat < 0.18 and luma > 0.25:
            return MAT_METAL
        if sat > 0.42:
            return MAT_CARPAINT
    return MAT_POLYMER

# Curated linear-RGB palette for stylized cyberpunk street props, vehicles & crews.
CYBER_PALETTE = {
    'obsidian': (0.030, 0.036, 0.052),
    'asphalt_dark': (0.055, 0.065, 0.088),
    'rubber_tire': (0.048, 0.052, 0.062),
    'carbon': (0.085, 0.098, 0.125),
    'gunmetal': (0.145, 0.175, 0.225),
    'slate': (0.235, 0.285, 0.355),
    'steel': (0.380, 0.435, 0.495),
    'silver': (0.620, 0.675, 0.720),
    'chrome': (0.800, 0.845, 0.880),
    'warm_white': (0.880, 0.865, 0.820),
    'glass_dark': (0.045, 0.085, 0.130),
    'navy_coat': (0.110, 0.185, 0.295),
    'teal_tech': (0.065, 0.380, 0.430),
    'cobalt': (0.130, 0.340, 0.680),
    'crimson_coat': (0.580, 0.115, 0.165),
    'rust_orange': (0.820, 0.290, 0.095),
    'hazard_yellow': (0.940, 0.740, 0.085),
    'olive_tech': (0.220, 0.285, 0.205),
    'leather_dark': (0.115, 0.082, 0.072),
    'leather_tan': (0.480, 0.295, 0.175),
    'denim': (0.145, 0.225, 0.365),
    'copper': (0.720, 0.360, 0.185),
    'brass': (0.760, 0.580, 0.220),
    'skin_light': (0.860, 0.640, 0.490),
    'skin_warm': (0.740, 0.500, 0.355),
    'skin_tan': (0.560, 0.365, 0.245),
    'skin_deep': (0.340, 0.205, 0.140),
    # Emissive / Signal colors
    'neon_cyan': (0.030, 0.880, 1.000),
    'neon_magenta': (1.000, 0.140, 0.520),
    'neon_amber': (1.000, 0.560, 0.080),
    'neon_crimson': (1.000, 0.065, 0.110),
    'neon_lime': (0.240, 0.960, 0.420),
    'neon_violet': (0.620, 0.220, 1.000),
    'headlight': (0.950, 0.960, 1.000),
}
PALETTE = CYBER_PALETTE


# ---------------------------------------------------------------------------
# Scene & Low-level Helpers
# ---------------------------------------------------------------------------

def reset_scene():
    """Reset Blender to a clean empty scene configured for 24 fps authoring."""
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.render.fps = FPS
    scene.render.fps_base = 1.0
    scene.frame_start = 1
    scene.frame_set(1)
    return scene


clear = reset_scene


def _clamp01(v: float) -> float:
    if not math.isfinite(v):
        return 0.0
    return 0.0 if v < 0.0 else (1.0 if v > 1.0 else v)


def _vec3(val, default=(1.0, 1.0, 1.0)) -> tuple[float, float, float]:
    if val is None:
        return default
    if isinstance(val, (int, float)):
        f = float(val)
        return (f, f, f)
    return (float(val[0]), float(val[1]), float(val[2]))


def resolve_color(color) -> tuple[float, float, float]:
    """Resolve a palette name, (r, g, b) tuple, '#rrggbb' string, or 0xRRGGBB int."""
    if isinstance(color, str):
        key = color.strip()
        if key in CYBER_PALETTE:
            return CYBER_PALETTE[key]
        if key.startswith('#'):
            h = key[1:]
            if len(h) == 3:
                h = ''.join(ch * 2 for ch in h)
            val = int(h, 16)
            return (((val >> 16) & 0xFF) / 255.0, ((val >> 8) & 0xFF) / 255.0, (val & 0xFF) / 255.0)
        raise KeyError(f'Unknown palette color: {color!r}')
    if isinstance(color, int):
        return (((color >> 16) & 0xFF) / 255.0, ((color >> 8) & 0xFF) / 255.0, (color & 0xFF) / 255.0)
    return (_clamp01(float(color[0])), _clamp01(float(color[1])), _clamp01(float(color[2])))


def _link_object(name: str, mesh: bpy.types.Mesh) -> bpy.types.Object:
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    return obj


def _apply_modifier(obj: bpy.types.Object, mod: bpy.types.Modifier) -> None:
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.modifier_apply(modifier=mod.name)


def _transform_matrix(location=(0.0, 0.0, 0.0), rotation=(0.0, 0.0, 0.0), scale=(1.0, 1.0, 1.0)) -> Matrix:
    loc = Vector(_vec3(location, (0.0, 0.0, 0.0)))
    if isinstance(rotation, Quaternion):
        rot = rotation
    elif isinstance(rotation, Euler):
        rot = rotation.to_quaternion()
    else:
        rx, ry, rz = _vec3(rotation, (0.0, 0.0, 0.0))
        rot = Euler((rx, ry, rz), 'XYZ').to_quaternion()
    sca = Vector(_vec3(scale, (1.0, 1.0, 1.0)))
    return Matrix.LocRotScale(loc, rot, sca)


def bake_transform(obj: bpy.types.Object) -> bpy.types.Object:
    """Bake object world transform into mesh vertices and reset matrix_world to Identity."""
    if obj is None or obj.type != 'MESH':
        return obj
    mat = obj.matrix_world.copy()
    if mat != Matrix.Identity(4):
        flip = mat.determinant() < 0.0
        obj.data.transform(mat)
        if flip:
            obj.data.flip_normals()
        if obj.parent is None:
            obj.matrix_world = Matrix.Identity(4)
        else:
            obj.matrix_basis = Matrix.Identity(4)
        obj.data.update()
    return obj


def count_triangles(objs) -> int:
    """Return total triangle count after polygon triangulation across one or more objects."""
    if isinstance(objs, bpy.types.Object):
        objs = [objs]
    total = 0
    for obj in objs:
        if obj is not None and obj.type == 'MESH' and obj.data is not None:
            total += sum(max(0, len(p.vertices) - 2) for p in obj.data.polygons)
    return total


def pin_to_bone(objs, bone_name: str):
    """Pin all vertices of obj(s) rigidly (weight=1.0) to a named bone across later joins."""
    if not bone_name:
        return objs
    single = isinstance(objs, bpy.types.Object)
    items = [objs] if single else list(objs)
    tag_name = f'__pin_{bone_name}'
    for obj in items:
        if obj is None or obj.type != 'MESH':
            continue
        for vg in list(obj.vertex_groups):
            if vg.name.startswith('__pin_'):
                obj.vertex_groups.remove(vg)
        vg = obj.vertex_groups.new(name=tag_name)
        indices = list(range(len(obj.data.vertices)))
        if indices:
            vg.add(indices, 1.0, 'REPLACE')
        obj['bone'] = bone_name
    return objs


# ---------------------------------------------------------------------------
# Materials, UVs & Baked Stylized Vertex Painting
# ---------------------------------------------------------------------------

def get_vertex_material(signal: bool = False) -> bpy.types.Material:
    """Return shared 'VertexPaint' or emissive 'Signal' material reading 'Color'."""
    name = 'Signal' if signal else 'VertexPaint'
    mat = bpy.data.materials.get(name)
    if mat is not None:
        return mat
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    mat.use_backface_culling = True
    tree = mat.node_tree
    vcol = tree.nodes.new('ShaderNodeVertexColor')
    vcol.layer_name = 'Color'
    bsdf = tree.nodes.get('Principled BSDF')
    if bsdf is None:
        bsdf = tree.nodes.new('ShaderNodeBsdfPrincipled')
    tree.links.new(vcol.outputs['Color'], bsdf.inputs['Base Color'])
    bsdf.inputs['Roughness'].default_value = 0.8
    if signal:
        if 'Emission Color' in bsdf.inputs:
            tree.links.new(vcol.outputs['Color'], bsdf.inputs['Emission Color'])
        elif 'Emission' in bsdf.inputs:
            tree.links.new(vcol.outputs['Color'], bsdf.inputs['Emission'])
        if 'Emission Strength' in bsdf.inputs:
            bsdf.inputs['Emission Strength'].default_value = 1.0
    return mat


vertex_material = get_vertex_material


def ensure_uvs(obj: bpy.types.Object) -> None:
    """Ensure mesh has a clean finite box-projected 'UVMap' layer."""
    mesh = obj.data
    uv_layer = mesh.uv_layers.get('UVMap')
    if uv_layer is None:
        if mesh.uv_layers:
            uv_layer = mesh.uv_layers[0]
            uv_layer.name = 'UVMap'
        else:
            uv_layer = mesh.uv_layers.new(name='UVMap')
    mesh.uv_layers.active = uv_layer
    verts = mesh.vertices
    loops = mesh.loops
    uv_data = uv_layer.data
    for poly in mesh.polygons:
        n = poly.normal
        ax, ay, az = abs(n.x), abs(n.y), abs(n.z)
        for li in poly.loop_indices:
            co = verts[loops[li].vertex_index].co
            if az >= ax and az >= ay:
                u, v = co.x * 0.5 + 0.5, co.y * 0.5 + 0.5
            elif ax >= ay:
                u, v = co.y * 0.5 + 0.5, co.z * 0.5 + 0.5
            else:
                u, v = co.x * 0.5 + 0.5, co.z * 0.5 + 0.5
            uv_data[li].uv = (u if math.isfinite(u) else 0.0, v if math.isfinite(v) else 0.0)


def _compute_vertex_ridge_factor(mesh: bpy.types.Mesh) -> list[float]:
    """Compute per-vertex convex ridge (+1) vs concave crease (-1) factor via bmesh."""
    n_verts = len(mesh.vertices)
    if n_verts == 0:
        return []
    bm = bmesh.new()
    bm.from_mesh(mesh)
    bm.verts.ensure_lookup_table()
    factors = [0.0] * len(bm.verts)
    for i, v in enumerate(bm.verts):
        if not v.link_edges:
            continue
        acc = 0.0
        cnt = 0
        for e in v.link_edges:
            if len(e.link_faces) == 2:
                ang = e.calc_face_angle_signed(0.0)
                if math.isfinite(ang):
                    acc += ang
                    cnt += 1
        if cnt > 0:
            # Normalize typical chamfer/crease angles (~0.8 rad) into [-1, 1]
            factors[i] = max(-1.0, min(1.0, (acc / cnt) / 0.85))
    bm.free()
    return factors


def paint_mesh(
    obj: bpy.types.Object,
    color=(0.30, 0.35, 0.42),
    signal: bool = False,
    ao_strength: float = 0.26,
    top_highlight: float = 0.11,
    ridge_boost: float = 0.08,
    ground_ao: float = 0.18,
    gradient_z=None,
    mat_class: float | None = None,
) -> bpy.types.Object:
    """Populate 'Color' FLOAT_COLOR corner attribute with baked AO & PBR material-class alpha."""
    if obj is None or obj.type != 'MESH':
        return obj
    mesh = obj.data
    ensure_uvs(obj)

    # Ensure FLOAT_COLOR corner attribute named 'Color'
    existing = mesh.color_attributes.get('Color')
    if existing is not None and (existing.data_type != 'FLOAT_COLOR' or existing.domain != 'CORNER'):
        mesh.color_attributes.remove(existing)
        existing = None
    layer = existing or mesh.color_attributes.new(name='Color', type='FLOAT_COLOR', domain='CORNER')
    mesh.color_attributes.active_color = layer
    for idx, attr in enumerate(mesh.color_attributes):
        if attr.name == 'Color':
            mesh.color_attributes.render_color_index = idx
            break

    base_r, base_g, base_b = resolve_color(color)
    alpha_cls = 1.0 if signal else float(mat_class if mat_class is not None else infer_mat_class(color, obj.name, (base_r, base_g, base_b)))
    grad_pair = None
    if gradient_z is not None:
        grad_pair = (resolve_color(gradient_z[0]), resolve_color(gradient_z[1]))

    verts = mesh.vertices
    if not verts:
        return obj

    world_mat = obj.matrix_world
    world_zs = [(world_mat @ v.co).z for v in verts]
    z_min = min(world_zs)
    z_max = max(world_zs)
    z_span = max(1e-4, z_max - z_min)

    ridges = [] if signal else _compute_vertex_ridge_factor(mesh)
    loops = mesh.loops
    col_data = layer.data

    for poly in mesh.polygons:
        fn = poly.normal
        nz = fn.z if math.isfinite(fn.z) else 0.0
        for li in poly.loop_indices:
            vi = loops[li].vertex_index
            wz = world_zs[vi]
            t_local = max(0.0, min(1.0, (wz - z_min) / z_span))

            if grad_pair is not None:
                (r0, g0, b0), (r1, g1, b1) = grad_pair
                cr = r0 + (r1 - r0) * t_local
                cg = g0 + (g1 - g0) * t_local
                cb = b0 + (b1 - b0) * t_local
            else:
                cr, cg, cb = base_r, base_g, base_b

            if signal:
                glow = 0.92 + 0.08 * t_local + 0.05 * max(0.0, nz)
                r = _clamp01(cr * glow)
                g = _clamp01(cg * glow)
                b = _clamp01(cb * glow)
            else:
                v_ao = (1.0 - ao_strength * 0.65) + (ao_strength * 0.80) * (t_local ** 0.8)
                if wz < 0.38 and z_min < 0.25:
                    g_t = max(0.0, min(1.0, max(0.0, wz) / 0.38))
                    g_ao = (1.0 - ground_ao) + ground_ao * (g_t * g_t * (3.0 - 2.0 * g_t))
                else:
                    g_ao = 1.0
                n_mod = 1.0 + (top_highlight * 0.65 * nz if nz >= 0.0 else 0.14 * nz)
                rf = ridges[vi] if vi < len(ridges) else 0.0
                c_mod = 1.0 + (ridge_boost * rf if rf >= 0.0 else ridge_boost * 1.45 * rf)

                shade = v_ao * g_ao * n_mod * c_mod
                r = _clamp01(cr * shade)
                g = _clamp01(cg * shade)
                b = _clamp01(cb * shade)

            col_data[li].color = (r, g, b, alpha_cls)

    mat = get_vertex_material(signal)
    mesh.materials.clear()
    mesh.materials.append(mat)
    for poly in mesh.polygons:
        poly.material_index = 0
    obj['is_signal'] = bool(signal)
    mesh.update()
    return obj


def paint_faces(
    obj: bpy.types.Object,
    predicate,
    color,
    ao_strength: float = 0.20,
    top_highlight: float = 0.08,
    mat_class: float | None = None,
) -> bpy.types.Object:
    """Repaint polygons matching predicate(poly, world_center, normal) on an already painted mesh."""
    mesh = obj.data
    layer = mesh.color_attributes.get('Color')
    if layer is None:
        paint_mesh(obj, color=color, ao_strength=ao_strength, top_highlight=top_highlight, mat_class=mat_class)
        layer = mesh.color_attributes['Color']

    cr, cg, cb = resolve_color(color)
    alpha_cls = float(mat_class if mat_class is not None else infer_mat_class(color, obj.name, (cr, cg, cb)))
    verts = mesh.vertices
    loops = mesh.loops
    world_mat = obj.matrix_world
    world_pts = [world_mat @ v.co for v in verts]
    z_min = min((p.z for p in world_pts), default=0.0)
    z_max = max((p.z for p in world_pts), default=1.0)
    z_span = max(1e-4, z_max - z_min)

    for poly in mesh.polygons:
        center = sum((world_pts[vi] for vi in poly.vertices), Vector()) / max(1, len(poly.vertices))
        if not predicate(poly, center, poly.normal):
            continue
        nz = poly.normal.z if math.isfinite(poly.normal.z) else 0.0
        for li in poly.loop_indices:
            wz = world_pts[loops[li].vertex_index].z
            t = max(0.0, min(1.0, (wz - z_min) / z_span))
            shade = ((1.0 - ao_strength * 0.65) + ao_strength * 0.85 * t) * (1.0 + top_highlight * nz)
            layer.data[li].color = (_clamp01(cr * shade), _clamp01(cg * shade), _clamp01(cb * shade), alpha_cls)
    mesh.update()
    return obj


# ---------------------------------------------------------------------------
# Procedural AA Geometry Builders
# ---------------------------------------------------------------------------

def _finalize_primitive(
    bm: bmesh.types.BMesh,
    name: str,
    location=(0.0, 0.0, 0.0),
    rotation=(0.0, 0.0, 0.0),
    scale=(1.0, 1.0, 1.0),
    bevel: float = 0.0,
    bevel_segments: int = 2,
    subdivisions: int = 0,
    shade_smooth: bool = False,
    smooth_angle_cos: float | None = None,
    color=(0.30, 0.35, 0.42),
    signal: bool = False,
    ao_strength: float = 0.26,
    bone: str | None = None,
) -> bpy.types.Object:
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])

    # Apply scale into bmesh BEFORE bevel so bevel widths are uniform in true metres
    sca = _vec3(scale, (1.0, 1.0, 1.0))
    if sca != (1.0, 1.0, 1.0):
        bmesh.ops.scale(bm, vec=sca, verts=bm.verts[:])
        if sca[0] * sca[1] * sca[2] < 0.0:
            bmesh.ops.reverse_faces(bm, faces=bm.faces[:])

    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()

    obj = _link_object(name, mesh)

    if bevel > 0.0 and len(mesh.polygons) > 0:
        xs = [v.co.x for v in mesh.vertices]
        ys = [v.co.y for v in mesh.vertices]
        zs = [v.co.z for v in mesh.vertices]
        min_dim = min(max(xs) - min(xs), max(ys) - min(ys), max(zs) - min(zs))
        safe_width = min(float(bevel), max(1e-4, min_dim * 0.42))
        if safe_width > 1e-4:
            mod = obj.modifiers.new('AABevel', 'BEVEL')
            mod.width = safe_width
            mod.segments = max(1, int(bevel_segments))
            mod.limit_method = 'ANGLE'
            mod.angle_limit = math.radians(28.0)
            if hasattr(mod, 'use_clamp_overlap'):
                mod.use_clamp_overlap = True
            _apply_modifier(obj, mod)

    if subdivisions > 0:
        mod = obj.modifiers.new('AASubdiv', 'SUBSURF')
        mod.subdivision_type = 'SIMPLE'
        mod.levels = int(subdivisions)
        mod.render_levels = int(subdivisions)
        _apply_modifier(obj, mod)

    # Set polygon smoothing before transforming into world space
    if smooth_angle_cos is not None:
        for p in obj.data.polygons:
            p.use_smooth = abs(p.normal.z) < smooth_angle_cos
    else:
        for p in obj.data.polygons:
            p.use_smooth = bool(shade_smooth)

    # Transform vertices directly into world space so matrix_world stays Identity
    mat = _transform_matrix(location=location, rotation=rotation, scale=(1.0, 1.0, 1.0))
    if mat != Matrix.Identity(4):
        obj.data.transform(mat)
    obj.data.validate()
    obj.data.update()

    paint_mesh(obj, color=color, signal=signal, ao_strength=ao_strength)
    if bone:
        pin_to_bone(obj, bone)
    return obj


def add_box(
    name: str = 'Box',
    size=(1.0, 1.0, 1.0),
    location=(0.0, 0.0, 0.0),
    rotation=(0.0, 0.0, 0.0),
    scale=(1.0, 1.0, 1.0),
    color=(0.30, 0.35, 0.42),
    signal: bool = False,
    bevel: float = 0.0,
    bevel_segments: int = 2,
    taper=None,
    shear_y: float = 0.0,
    shear_x: float = 0.0,
    subdivisions: int = 0,
    shade_smooth: bool = False,
    ao_strength: float = 0.26,
    bone: str | None = None,
) -> bpy.types.Object:
    """Create a box of full dimensions size=(sx, sy, sz) with optional taper, shear, and bevel."""
    sx, sy, sz = _vec3(size, (1.0, 1.0, 1.0))
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    bmesh.ops.scale(bm, vec=(sx, sy, sz), verts=bm.verts[:])

    if taper is not None or shear_y != 0.0 or shear_x != 0.0:
        tx, ty = (float(taper), float(taper)) if isinstance(taper, (int, float)) else (
            (float(taper[0]), float(taper[1])) if taper is not None else (1.0, 1.0)
        )
        half_z = max(1e-5, sz * 0.5)
        for v in bm.verts:
            if v.co.z > 0.0:
                v.co.x = v.co.x * tx + shear_x
                v.co.y = v.co.y * ty + shear_y
            else:
                # Keep bottom centered cleanly
                _ = half_z

    return _finalize_primitive(
        bm,
        name=name,
        location=location,
        rotation=rotation,
        scale=scale,
        bevel=bevel,
        bevel_segments=bevel_segments,
        subdivisions=subdivisions,
        shade_smooth=shade_smooth,
        color=color,
        signal=signal,
        ao_strength=ao_strength,
        bone=bone,
    )


def add_bevel_box(
    name: str = 'BevelBox',
    size=(1.0, 1.0, 1.0),
    location=(0.0, 0.0, 0.0),
    rotation=(0.0, 0.0, 0.0),
    scale=(1.0, 1.0, 1.0),
    bevel: float = 0.03,
    segments: int = 2,
    bevel_segments: int | None = None,
    color=(0.30, 0.35, 0.42),
    signal: bool = False,
    taper=None,
    shear_y: float = 0.0,
    shear_x: float = 0.0,
    subdivisions: int = 0,
    shade_smooth: bool = False,
    ao_strength: float = 0.26,
    bone: str | None = None,
) -> bpy.types.Object:
    """Create a chamfered/beveled box of full dimensions size=(sx, sy, sz)."""
    segs = bevel_segments if bevel_segments is not None else segments
    return add_box(
        name=name,
        size=size,
        location=location,
        rotation=rotation,
        scale=scale,
        color=color,
        signal=signal,
        bevel=bevel,
        bevel_segments=segs,
        taper=taper,
        shear_y=shear_y,
        shear_x=shear_x,
        subdivisions=subdivisions,
        shade_smooth=shade_smooth,
        ao_strength=ao_strength,
        bone=bone,
    )


def add_cylinder(
    name: str = 'Cylinder',
    radius: float = 0.5,
    depth: float = 1.0,
    vertices: int = 18,
    location=(0.0, 0.0, 0.0),
    rotation=(0.0, 0.0, 0.0),
    scale=(1.0, 1.0, 1.0),
    color=(0.30, 0.35, 0.42),
    signal: bool = False,
    bevel: float = 0.0,
    bevel_segments: int = 1,
    cap_ends: bool = True,
    radius_top: float | None = None,
    height: float | None = None,
    segments: int | None = None,
    shade_smooth: bool = True,
    ao_strength: float = 0.26,
    bone: str | None = None,
) -> bpy.types.Object:
    """Create a cylinder (or tapered cylinder when radius_top is given) aligned with local Z."""
    h = float(height if height is not None else depth)
    segs = max(5, int(segments if segments is not None else vertices))
    r1 = float(radius)
    r2 = float(radius_top if radius_top is not None else radius)

    bm = bmesh.new()
    bmesh.ops.create_cone(
        bm,
        cap_ends=cap_ends,
        cap_tris=False,
        segments=segs,
        radius1=r1,
        radius2=r2,
        depth=h,
    )
    return _finalize_primitive(
        bm,
        name=name,
        location=location,
        rotation=rotation,
        scale=scale,
        bevel=bevel,
        bevel_segments=bevel_segments,
        shade_smooth=shade_smooth,
        smooth_angle_cos=0.75 if shade_smooth else None,
        color=color,
        signal=signal,
        ao_strength=ao_strength,
        bone=bone,
    )


def add_cone(
    name: str = 'Cone',
    radius1: float = 0.5,
    radius2: float = 0.0,
    depth: float = 1.0,
    vertices: int = 18,
    location=(0.0, 0.0, 0.0),
    rotation=(0.0, 0.0, 0.0),
    scale=(1.0, 1.0, 1.0),
    color=(0.30, 0.35, 0.42),
    signal: bool = False,
    bevel: float = 0.0,
    bevel_segments: int = 1,
    cap_ends: bool = True,
    radius: float | None = None,
    radius_top: float | None = None,
    height: float | None = None,
    segments: int | None = None,
    shade_smooth: bool = True,
    ao_strength: float = 0.26,
    bone: str | None = None,
) -> bpy.types.Object:
    """Create a cone or frustum from bottom radius1 to top radius2 over depth along local Z."""
    r_bot = float(radius if radius is not None else radius1)
    r_top = float(radius_top if radius_top is not None else radius2)
    h = float(height if height is not None else depth)
    segs = max(5, int(segments if segments is not None else vertices))

    bm = bmesh.new()
    bmesh.ops.create_cone(
        bm,
        cap_ends=cap_ends,
        cap_tris=False,
        segments=segs,
        radius1=r_bot,
        radius2=r_top,
        depth=h,
    )
    return _finalize_primitive(
        bm,
        name=name,
        location=location,
        rotation=rotation,
        scale=scale,
        bevel=bevel,
        bevel_segments=bevel_segments,
        shade_smooth=shade_smooth,
        smooth_angle_cos=0.82 if shade_smooth else None,
        color=color,
        signal=signal,
        ao_strength=ao_strength,
        bone=bone,
    )


def add_sphere(
    name: str = 'Sphere',
    radius: float = 0.5,
    segments: int = 18,
    rings: int = 12,
    ring_count: int | None = None,
    location=(0.0, 0.0, 0.0),
    rotation=(0.0, 0.0, 0.0),
    scale=(1.0, 1.0, 1.0),
    color=(0.30, 0.35, 0.42),
    signal: bool = False,
    shade_smooth: bool = True,
    ao_strength: float = 0.26,
    bone: str | None = None,
) -> bpy.types.Object:
    """Create a UV sphere (or ellipsoid via scale) centered at location."""
    u_segs = max(6, int(segments))
    v_segs = max(4, int(ring_count if ring_count is not None else rings))
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=u_segs, v_segments=v_segs, radius=float(radius))
    return _finalize_primitive(
        bm,
        name=name,
        location=location,
        rotation=rotation,
        scale=scale,
        shade_smooth=shade_smooth,
        color=color,
        signal=signal,
        ao_strength=ao_strength,
        bone=bone,
    )


def add_torus(
    name: str = 'Torus',
    major_radius: float = 0.5,
    minor_radius: float = 0.12,
    major_segments: int = 24,
    minor_segments: int = 12,
    location=(0.0, 0.0, 0.0),
    rotation=(0.0, 0.0, 0.0),
    scale=(1.0, 1.0, 1.0),
    color=(0.30, 0.35, 0.42),
    signal: bool = False,
    shade_smooth: bool = True,
    ao_strength: float = 0.26,
    bone: str | None = None,
) -> bpy.types.Object:
    """Create a parametric torus lying in the local XY plane around Z."""
    maj_n = max(6, int(major_segments))
    min_n = max(4, int(minor_segments))
    R = float(major_radius)
    r = float(minor_radius)

    bm = bmesh.new()
    grid = []
    for i in range(maj_n):
        u = (i / maj_n) * math.tau
        cu, su = math.cos(u), math.sin(u)
        ring = []
        for j in range(min_n):
            v = (j / min_n) * math.tau
            cv, sv = math.cos(v), math.sin(v)
            rad = R + r * cv
            ring.append(bm.verts.new((rad * cu, rad * su, r * sv)))
        grid.append(ring)

    for i in range(maj_n):
        i_next = (i + 1) % maj_n
        for j in range(min_n):
            j_next = (j + 1) % min_n
            bm.faces.new((grid[i][j], grid[i_next][j], grid[i_next][j_next], grid[i][j_next]))

    return _finalize_primitive(
        bm,
        name=name,
        location=location,
        rotation=rotation,
        scale=scale,
        shade_smooth=shade_smooth,
        color=color,
        signal=signal,
        ao_strength=ao_strength,
        bone=bone,
    )


def add_extruded_profile(
    profile_2d: Sequence[Sequence[float]],
    depth: float = 1.0,
    axis: str = 'X',
    name: str = 'ExtrudedProfile',
    location=(0.0, 0.0, 0.0),
    rotation=(0.0, 0.0, 0.0),
    scale=(1.0, 1.0, 1.0),
    color=(0.30, 0.35, 0.42),
    signal: bool = False,
    bevel: float = 0.0,
    bevel_segments: int = 2,
    centered: bool = True,
    shade_smooth: bool = False,
    ao_strength: float = 0.26,
    bone: str | None = None,
) -> bpy.types.Object:
    """Extrude a 2D polygon (convex or concave) along 'X', 'Y', or 'Z'.

    Axis mapping:
      * axis='X': profile (u, v) = (Y, Z) side silhouette, extruded across width X.
      * axis='Y': profile (u, v) = (X, Z) front cross-section, extruded along Y.
      * axis='Z': profile (u, v) = (X, Y) footprint, extruded vertically along Z.
    """
    pts = [(float(p[0]), float(p[1])) for p in profile_2d]
    cleaned = []
    for pt in pts:
        if not cleaned or math.hypot(pt[0] - cleaned[-1][0], pt[1] - cleaned[-1][1]) > 1e-6:
            cleaned.append(pt)
    if len(cleaned) > 2 and math.hypot(cleaned[0][0] - cleaned[-1][0], cleaned[0][1] - cleaned[-1][1]) <= 1e-6:
        cleaned.pop()
    if len(cleaned) < 3:
        raise ValueError(f'add_extruded_profile requires >= 3 distinct 2D points, got {len(cleaned)}')

    ax = axis.upper()
    d0 = -float(depth) * 0.5 if centered else 0.0
    d1 = float(depth) * 0.5 if centered else float(depth)

    def to_3d(u: float, v: float, w: float) -> Vector:
        if ax == 'X':
            return Vector((w, u, v))
        if ax == 'Y':
            return Vector((u, w, v))
        if ax == 'Z':
            return Vector((u, v, w))
        raise ValueError(f"axis must be 'X', 'Y', or 'Z', got {axis!r}")

    bm = bmesh.new()
    front_verts = [bm.verts.new(to_3d(u, v, d0)) for u, v in cleaned]
    back_verts = [bm.verts.new(to_3d(u, v, d1)) for u, v in cleaned]

    # Triangulate arbitrary concave/convex 2D profile cleanly
    loop_2d = [Vector((u, v, 0.0)) for u, v in cleaned]
    tris = tessellate_polygon([loop_2d])
    for i0, i1, i2 in tris:
        if i0 != i1 and i1 != i2 and i2 != i0:
            try:
                bm.faces.new((front_verts[i0], front_verts[i2], front_verts[i1]))
            except ValueError:
                pass
            try:
                bm.faces.new((back_verts[i0], back_verts[i1], back_verts[i2]))
            except ValueError:
                pass

    n = len(cleaned)
    for i in range(n):
        j = (i + 1) % n
        try:
            bm.faces.new((front_verts[i], front_verts[j], back_verts[j], back_verts[i]))
        except ValueError:
            pass

    return _finalize_primitive(
        bm,
        name=name,
        location=location,
        rotation=rotation,
        scale=scale,
        bevel=bevel,
        bevel_segments=bevel_segments,
        shade_smooth=shade_smooth,
        color=color,
        signal=signal,
        ao_strength=ao_strength,
        bone=bone,
    )


def add_limb(
    p0,
    p1,
    radius0: float = 0.07,
    radius1: float | None = None,
    segments: int = 14,
    cap_round: bool = True,
    scale_xy=(1.0, 1.0),
    name: str = 'Limb',
    color=(0.30, 0.35, 0.42),
    signal: bool = False,
    bevel: float = 0.0,
    ao_strength: float = 0.26,
    bone: str | None = None,
) -> bpy.types.Object:
    """Create a tapered capsule/limb segment connecting 3D points p0 -> p1."""
    v0 = Vector(_vec3(p0, (0.0, 0.0, 0.0)))
    v1 = Vector(_vec3(p1, (0.0, 0.0, 1.0)))
    delta = v1 - v0
    length = delta.length
    r0 = float(radius0)
    r1 = float(radius0 if radius1 is None else radius1)
    if length < 1e-5:
        return add_sphere(name=name, radius=r0, location=v0, color=color, signal=signal, bone=bone)

    segs = max(6, int(segments))
    sx, sy = float(scale_xy[0]), float(scale_xy[1])
    bm = bmesh.new()

    # Stations of (z, r) from bottom cap through shaft to top cap
    stations = []
    if cap_round and r0 > 1e-4:
        stations.append((-r0 * 0.32, r0 * 0.52))
        stations.append((-r0 * 0.12, r0 * 0.86))
    stations.extend([
        (0.0, r0),
        (length * 0.33, r0 + (r1 - r0) * 0.33),
        (length * 0.67, r0 + (r1 - r0) * 0.67),
        (length, r1),
    ])
    if cap_round and r1 > 1e-4:
        stations.append((length + r1 * 0.12, r1 * 0.86))
        stations.append((length + r1 * 0.32, r1 * 0.52))

    rings = []
    for z, rad in stations:
        ring = []
        for i in range(segs):
            ang = (i / segs) * math.tau
            ring.append(bm.verts.new((math.cos(ang) * rad * sx, math.sin(ang) * rad * sy, z)))
        rings.append(ring)

    for k in range(len(rings) - 1):
        r_a, r_b = rings[k], rings[k + 1]
        for i in range(segs):
            j = (i + 1) % segs
            bm.faces.new((r_a[i], r_a[j], r_b[j], r_b[i]))

    bot_pole = bm.verts.new((0.0, 0.0, (-r0 * 0.44) if (cap_round and r0 > 1e-4) else 0.0))
    top_pole = bm.verts.new((0.0, 0.0, (length + r1 * 0.44) if (cap_round and r1 > 1e-4) else length))
    for i in range(segs):
        j = (i + 1) % segs
        bm.faces.new((bot_pole, rings[0][j], rings[0][i]))
        bm.faces.new((rings[-1][i], rings[-1][j], top_pole))

    rot_q = Vector((0.0, 0.0, 1.0)).rotation_difference(delta.normalized())
    return _finalize_primitive(
        bm,
        name=name,
        location=v0,
        rotation=rot_q,
        bevel=bevel,
        shade_smooth=True,
        color=color,
        signal=signal,
        ao_strength=ao_strength,
        bone=bone,
    )


add_capsule = add_limb


def add_pipe(
    points: Sequence[Sequence[float]],
    radius: float = 0.03,
    bevel_resolution: int = 3,
    name: str = 'Pipe',
    color=(0.24, 0.28, 0.34),
    signal: bool = False,
    cyclic: bool = False,
    fill_caps: bool = True,
    ao_strength: float = 0.26,
    bone: str | None = None,
) -> bpy.types.Object:
    """Create a 3D tubular pipe/cable/handlebar mesh sweeping through points."""
    pts = [_vec3(p) for p in points]
    if len(pts) < 2:
        raise ValueError('add_pipe requires at least 2 3D points')

    curve = bpy.data.curves.new(f'{name}_Curve', type='CURVE')
    curve.dimensions = '3D'
    curve.bevel_depth = float(radius)
    curve.bevel_resolution = max(1, int(bevel_resolution))
    curve.use_fill_caps = bool(fill_caps)
    spline = curve.splines.new('POLY')
    spline.points.add(len(pts) - 1)
    for i, (x, y, z) in enumerate(pts):
        spline.points[i].co = (x, y, z, 1.0)
    spline.use_cyclic_u = bool(cyclic)

    c_obj = bpy.data.objects.new(f'{name}_CurveObj', curve)
    bpy.context.collection.objects.link(c_obj)
    depsgraph = bpy.context.evaluated_depsgraph_get()
    eval_obj = c_obj.evaluated_get(depsgraph)
    mesh = bpy.data.meshes.new_from_object(eval_obj)
    mesh.name = name
    bpy.data.objects.remove(c_obj, do_unlink=True)
    bpy.data.curves.remove(curve)

    obj = _link_object(name, mesh)
    for p in obj.data.polygons:
        p.use_smooth = True
    obj.data.validate()
    obj.data.update()
    paint_mesh(obj, color=color, signal=signal, ao_strength=ao_strength)
    if bone:
        pin_to_bone(obj, bone)
    return obj


def mirror_x(objs, bone_swap: bool = True):
    """Duplicate mesh object(s) mirrored across X = 0 with outward normals & swapped _L/_R pins."""
    single = isinstance(objs, bpy.types.Object)
    items = [objs] if single else list(objs)
    mirrored = []
    mirror_mat = Matrix.Diagonal((-1.0, 1.0, 1.0, 1.0))

    def _swap_lr(text: str) -> str:
        if '_L' in text:
            return text.replace('_L', '_R')
        if '_R' in text:
            return text.replace('_R', '_L')
        return text + '_Mirror'

    for src in items:
        if src is None or src.type != 'MESH':
            continue
        bake_transform(src)
        new_mesh = src.data.copy()
        new_mesh.transform(mirror_mat)
        new_mesh.flip_normals()
        new_mesh.update()
        new_name = _swap_lr(src.name) if bone_swap else f'{src.name}_Mirror'
        dup = _link_object(new_name, new_mesh)
        dup['is_signal'] = bool(src.get('is_signal', False))

        # Copy vertex groups, swapping _L <-> _R if requested
        idx_to_dst_vg = {}
        for vg_src in src.vertex_groups:
            vg_name = _swap_lr(vg_src.name) if bone_swap else vg_src.name
            idx_to_dst_vg[vg_src.index] = dup.vertex_groups.new(name=vg_name)
        for v in src.data.vertices:
            for g_elem in v.groups:
                if g_elem.weight > 0.0 and g_elem.group in idx_to_dst_vg:
                    idx_to_dst_vg[g_elem.group].add([v.index], g_elem.weight, 'REPLACE')
        if 'bone' in src:
            dup['bone'] = _swap_lr(src['bone']) if bone_swap else src['bone']
        mirrored.append(dup)

    return mirrored[0] if single else mirrored


def build_wheel_assembly(
    center=(0.0, 0.0, 0.36),
    radius: float = 0.36,
    width: float = 0.22,
    tire_color='rubber_tire',
    rim_color='silver',
    rotor_color='gunmetal',
    caliper_color=(0.78, 0.09, 0.12),
    spokes: int = 10,
) -> list[bpy.types.Object]:
    """Build a realistic AAA wheel (rounded-shoulder tire, forged multi-spoke alloy rim, brake rotor & caliper)."""
    cx, cy, cz = _vec3(center)
    side_sign = -1.0 if cx < 0.0 else 1.0
    rot_wheel = (0.0, math.pi * 0.5, 0.0)
    parts = []

    # Curved-shoulder rubber tire (24 segments, smooth rounded bevel)
    tire = add_cylinder(
        name='Road_Tire',
        radius=radius,
        depth=width,
        vertices=24,
        location=(cx, cy, cz),
        rotation=rot_wheel,
        bevel=min(0.042, width * 0.22),
        bevel_segments=2,
        color=tire_color,
    )
    paint_mesh(tire, color=tire_color, mat_class=MAT_LEATHER, ao_strength=0.22)
    parts.append(tire)

    # Outer forged alloy rim lip & deep inner barrel
    rim_r = radius * 0.68
    barrel = add_cylinder(
        name='Rim_Barrel',
        radius=rim_r,
        depth=width * 1.03,
        vertices=24,
        location=(cx, cy, cz),
        rotation=rot_wheel,
        bevel=0.014,
        bevel_segments=1,
        color=rim_color,
    )
    paint_mesh(barrel, color=rim_color, mat_class=MAT_METAL, ao_strength=0.20)
    parts.append(barrel)

    # Recessed dark wheel well interior & brushed steel brake rotor
    rotor = add_cylinder(
        name='Brake_Rotor',
        radius=radius * 0.54,
        depth=width * 1.05,
        vertices=18,
        location=(cx, cy, cz),
        rotation=rot_wheel,
        color=rotor_color,
    )
    paint_mesh(rotor, color=rotor_color, mat_class=MAT_METAL, ao_strength=0.28)
    parts.append(rotor)

    # Painted performance brake caliper tucked behind upper-rear spokes
    caliper = add_bevel_box(
        name='Brake_Caliper',
        size=(width * 0.24, radius * 0.28, radius * 0.16),
        location=(cx + side_sign * width * 0.30, cy + radius * 0.34, cz + radius * 0.16),
        bevel=0.010,
        segments=1,
        color=caliper_color,
    )
    paint_mesh(caliper, color=caliper_color, mat_class=MAT_CARPAINT, ao_strength=0.18)
    parts.append(caliper)

    # Forged split-spoke alloy wheel face
    spoke_x = cx + side_sign * (width * 0.45)
    n_spokes = max(5, min(8, int(spokes)))
    for i in range(n_spokes):
        ang = (i / n_spokes) * math.tau
        sy = cy + math.cos(ang) * (rim_r * 0.50)
        sz = cz + math.sin(ang) * (rim_r * 0.50)
        sp = add_bevel_box(
            name=f'Spoke_{i}',
            size=(width * 0.15, rim_r * 0.88, radius * 0.068),
            location=(spoke_x, sy, sz),
            rotation=(ang, 0.0, 0.0),
            bevel=0.006,
            segments=1,
            color=rim_color,
        )
        paint_mesh(sp, color=rim_color, mat_class=MAT_METAL, ao_strength=0.18)
        parts.append(sp)

    # Center machined hub cap
    cap = add_cylinder(
        name='Axle_Cap',
        radius=radius * 0.19,
        depth=width * 1.13,
        vertices=14,
        location=(cx, cy, cz),
        rotation=rot_wheel,
        bevel=0.010,
        bevel_segments=2,
        color='chrome',
    )
    paint_mesh(cap, color='chrome', mat_class=MAT_METAL, ao_strength=0.18)
    parts.append(cap)
    return parts


def subdivide_to_min_tris(obj_or_objs, min_tris: int = 550):
    """Subdivide mesh(es) preserving shape, UVs, vertex groups, and 'Color' until > min_tris."""
    single = isinstance(obj_or_objs, bpy.types.Object)
    objs = [obj_or_objs] if single else [o for o in obj_or_objs if o is not None and o.type == 'MESH']
    if not objs:
        return obj_or_objs

    total = count_triangles(objs)
    if total >= min_tris:
        return obj_or_objs

    # Pick the largest mesh (normally 'Body') to absorb needed density cleanly
    target_obj = max(objs, key=lambda o: max(1, count_triangles(o)))
    other_tris = total - count_triangles(target_obj)
    goal = max(4, int(min_tris) - other_tris)

    mesh = target_obj.data
    bm = bmesh.new()
    bm.from_mesh(mesh)
    bmesh.ops.triangulate(bm, faces=bm.faces[:])

    guard = 0
    while len(bm.faces) < goal and guard < 6:
        guard += 1
        if len(bm.faces) * 4 <= goal * 1.35:
            bmesh.ops.subdivide_edges(bm, edges=bm.edges[:], cuts=1, use_grid_fill=True)
        else:
            need = goal - len(bm.faces)
            edges_sorted = sorted(bm.edges[:], key=lambda e: e.calc_length(), reverse=True)
            k = max(1, min(len(edges_sorted), math.ceil(need / 1.75)))
            bmesh.ops.subdivide_edges(bm, edges=edges_sorted[:k], cuts=1)
        bmesh.ops.triangulate(bm, faces=bm.faces[:])

    bm.to_mesh(mesh)
    bm.free()
    mesh.validate()
    mesh.update()

    # Keep 'Color' active after bmesh round-trip
    col = mesh.color_attributes.get('Color')
    if col is not None:
        mesh.color_attributes.active_color = col
    return obj_or_objs


def decimate_to_max_tris(obj_or_objs, max_tris: int = 4800):
    """Collapse-decimate mesh(es) down to max_tris if over budget while preserving 'Color'."""
    single = isinstance(obj_or_objs, bpy.types.Object)
    objs = [obj_or_objs] if single else [o for o in obj_or_objs if o is not None and o.type == 'MESH']
    total = count_triangles(objs)
    if total <= max_tris or total == 0:
        return obj_or_objs

    ratio = max(0.1, float(max_tris) / float(total))
    for obj in objs:
        tris = count_triangles(obj)
        if tris < 60:
            continue
        mod = obj.modifiers.new('AADecimate', 'DECIMATE')
        mod.ratio = ratio
        mod.use_collapse_triangulate = True
        _apply_modifier(obj, mod)
        col = obj.data.color_attributes.get('Color')
        if col is not None:
            obj.data.color_attributes.active_color = col
    return obj_or_objs


# ---------------------------------------------------------------------------
# Role-Based Batching (<= 6 Meshes Contract)
# ---------------------------------------------------------------------------

def _join_group(
    objects: Sequence[bpy.types.Object],
    name: str,
    signal: bool = False,
    center=None,
) -> bpy.types.Object | None:
    valid = [o for o in objects if o is not None and o.type == 'MESH' and len(o.data.vertices) > 0]
    if not valid:
        return None

    for o in valid:
        bake_transform(o)
        ensure_uvs(o)
        if o.data.color_attributes.get('Color') is None:
            paint_mesh(o, signal=signal)
        o.hide_viewport = False
        o.hide_render = False
        o.hide_set(False)

    bpy.ops.object.select_all(action='DESELECT')
    for o in valid:
        o.select_set(True)
    bpy.context.view_layer.objects.active = valid[0]
    if len(valid) > 1:
        bpy.ops.object.join()

    joined = bpy.context.view_layer.objects.active
    joined.name = name
    joined.data.name = name
    mat = get_vertex_material(signal)
    joined.data.materials.clear()
    joined.data.materials.append(mat)
    for poly in joined.data.polygons:
        poly.material_index = 0

    col = joined.data.color_attributes.get('Color')
    if col is not None:
        joined.data.color_attributes.active_color = col
        for idx, attr in enumerate(joined.data.color_attributes):
            if attr.name == 'Color':
                joined.data.color_attributes.render_color_index = idx
                break
    joined['is_signal'] = bool(signal)

    if center is not None:
        c = Vector(_vec3(center, (0.0, 0.0, 0.0)))
        joined.data.transform(Matrix.Translation(-c))
        joined.location = c
    joined.data.validate()
    joined.data.update()
    return joined


def _bbox_midpoint(objs: Sequence[bpy.types.Object]) -> tuple[float, float, float]:
    xs, ys, zs = [], [], []
    for o in objs:
        if o is None or o.type != 'MESH':
            continue
        mw = o.matrix_world
        for v in o.data.vertices:
            p = mw @ v.co
            xs.append(p.x)
            ys.append(p.y)
            zs.append(p.z)
    if not xs:
        return (0.0, 0.0, 0.0)
    return ((min(xs) + max(xs)) * 0.5, (min(ys) + max(ys)) * 0.5, (min(zs) + max(zs)) * 0.5)


def combine_by_role(
    parts: Iterable[bpy.types.Object],
    signal_parts: Iterable[bpy.types.Object] | None = None,
    extra_named=None,
    body_name: str = 'Body',
    signal_name: str = 'Signal',
) -> list[bpy.types.Object]:
    """Merge non-signal parts into 'Body' and emissive parts into 'Signal', plus extra_named.

    extra_named may be:
      * dict mapping node_name -> (objs, center_xyz) | {'parts': objs, 'center': xyz} | objs
      * sequence of (node_name, objs[, center_xyz]) tuples
    Wheel_* nodes automatically center their geometry around their axle pivot coordinates.
    """
    parts_list = [o for o in (parts or []) if o is not None]
    if signal_parts is None:
        body_in = [o for o in parts_list if not o.get('is_signal', False)]
        sig_in = [o for o in parts_list if o.get('is_signal', False)]
    else:
        body_in = parts_list
        sig_in = [o for o in signal_parts if o is not None]

    out: list[bpy.types.Object] = []
    body_obj = _join_group(body_in, body_name, signal=False)
    if body_obj is not None:
        out.append(body_obj)
    sig_obj = _join_group(sig_in, signal_name, signal=True)
    if sig_obj is not None:
        out.append(sig_obj)

    if extra_named:
        entries = extra_named.items() if isinstance(extra_named, dict) else extra_named
        for entry in entries:
            if isinstance(entry, bpy.types.Object):
                out.append(entry)
                continue
            if len(entry) == 2:
                e_name, spec = entry
                if isinstance(spec, dict):
                    e_objs = spec.get('parts') or spec.get('objects') or []
                    e_center = spec.get('center', spec.get('pivot'))
                    e_sig = bool(spec.get('signal', False))
                elif (
                    isinstance(spec, tuple)
                    and len(spec) == 2
                    and isinstance(spec[0], (list, tuple))
                    and (spec[1] is None or (isinstance(spec[1], (list, tuple, Vector)) and len(spec[1]) == 3 and isinstance(spec[1][0], (int, float))))
                ):
                    e_objs, e_center = spec
                    e_sig = False
                else:
                    e_objs = [spec] if isinstance(spec, bpy.types.Object) else list(spec)
                    e_center = _bbox_midpoint(e_objs) if str(e_name).startswith('Wheel_') else None
                    e_sig = False
            elif len(entry) >= 3:
                e_name, e_objs, e_center = entry[0], entry[1], entry[2]
                e_sig = bool(entry[3]) if len(entry) > 3 else False
                if isinstance(e_objs, bpy.types.Object):
                    e_objs = [e_objs]
            else:
                raise ValueError(f'Unsupported extra_named entry: {entry!r}')

            if isinstance(e_objs, bpy.types.Object):
                e_objs = [e_objs]
            merged = _join_group(list(e_objs), str(e_name), signal=e_sig, center=e_center)
            if merged is not None:
                out.append(merged)

    if len(out) > 6:
        raise ValueError(f'GLB batching contract violated: {len(out)} meshes (> 6): {[o.name for o in out]}')
    return out


# ---------------------------------------------------------------------------
# 21-Bone Humanoid Rig, Smart Skinning & Expressive 24 fps Clips
# ---------------------------------------------------------------------------

def _seg_distance(p: Vector, a: Vector, b: Vector) -> float:
    ab = b - a
    len_sq = ab.length_squared
    if len_sq < 1e-10:
        return (p - a).length
    t = max(0.0, min(1.0, (p - a).dot(ab) / len_sq))
    return (p - (a + ab * t)).length


def _smoothstep(edge0: float, edge1: float, x: float) -> float:
    if abs(edge1 - edge0) < 1e-8:
        return 1.0 if x >= edge1 else 0.0
    t = max(0.0, min(1.0, (x - edge0) / (edge1 - edge0)))
    return t * t * (3.0 - 2.0 * t)


def build_humanoid_rig_and_clips(
    body_objs,
    rig_name: str = 'StreetRig',
    height: float = 1.8,
    style: str = 'citizen',
    attack_clip_name: str = 'Punch',
    weapon_hand: str | None = None,
    shoulder_width: float | None = None,
    hip_width: float | None = None,
    extra_clips: Sequence[str] | None = None,
) -> bpy.types.Object:
    """Create a 21-bone humanoid armature, skin all body_objs, and keyframe 24 fps clips."""
    meshes = [body_objs] if isinstance(body_objs, bpy.types.Object) else [o for o in body_objs if o is not None and o.type == 'MESH']
    if not meshes:
        raise ValueError('build_humanoid_rig_and_clips requires at least one mesh object')

    h = float(height)
    st = (style or 'citizen').lower()
    is_mech = st in ('mech', 'brute')
    is_heavy = st in ('enforcer', 'heavy')

    sh_x = float(shoulder_width) if shoulder_width is not None else h * (0.195 if is_mech else 0.140 if is_heavy else 0.120)
    hp_x = float(hip_width) if hip_width is not None else h * (0.115 if is_mech else 0.072 if is_heavy else 0.062)
    elbow_x = sh_x + h * (0.045 if is_mech else 0.036)
    wrist_x = sh_x + h * (0.050 if is_mech else 0.034)
    knee_x = hp_x + h * 0.012
    ankle_x = hp_x + h * 0.018

    pelvis_z = 0.48 * h
    spine_z = 0.57 * h
    chest_z = 0.68 * h
    neck_z = 0.78 * h
    head_z = 0.84 * h
    crown_z = 1.00 * h
    sh_z = 0.755 * h
    elbow_z = 0.618 * h
    wrist_z = 0.485 * h
    hand_tip_z = 0.415 * h
    hip_z = 0.505 * h
    knee_z = 0.285 * h
    ankle_z = 0.080 * h
    ball_z = 0.032 * h

    joints: dict[str, tuple[tuple[float, float, float], tuple[float, float, float]]] = {
        'Pelvis': ((0.0, 0.0, pelvis_z), (0.0, 0.0, spine_z)),
        'Spine': ((0.0, 0.0, spine_z), (0.0, 0.0, chest_z)),
        'Chest': ((0.0, 0.0, chest_z), (0.0, 0.0, neck_z)),
        'Neck': ((0.0, 0.0, neck_z), (0.0, 0.0, head_z)),
        'Head': ((0.0, 0.0, head_z), (0.0, 0.0, crown_z)),
    }
    parents = {
        'Spine': 'Pelvis',
        'Chest': 'Spine',
        'Neck': 'Chest',
        'Head': 'Neck',
    }

    for side, sign in (('L', 1.0), ('R', -1.0)):
        clav_h = (sign * 0.032 * h, 0.0, sh_z)
        sh_p = (sign * sh_x, 0.0, sh_z)
        el_p = (sign * elbow_x, 0.015 * h, elbow_z)
        wr_p = (sign * wrist_x, -0.025 * h, wrist_z)
        hd_p = (sign * wrist_x, -0.038 * h, hand_tip_z)
        th_h = (sign * hp_x, 0.0, hip_z)
        kn_p = (sign * knee_x, -0.012 * h, knee_z)
        an_p = (sign * ankle_x, 0.010 * h, ankle_z)
        bl_p = (sign * ankle_x, -0.085 * h, ball_z)
        to_p = (sign * ankle_x, -0.145 * h, 0.020 * h)

        joints.update({
            f'Clavicle_{side}': (clav_h, sh_p),
            f'UpperArm_{side}': (sh_p, el_p),
            f'Forearm_{side}': (el_p, wr_p),
            f'Hand_{side}': (wr_p, hd_p),
            f'Thigh_{side}': (th_h, kn_p),
            f'Shin_{side}': (kn_p, an_p),
            f'Foot_{side}': (an_p, bl_p),
            f'Toe_{side}': (bl_p, to_p),
        })
        parents.update({
            f'Clavicle_{side}': 'Chest',
            f'UpperArm_{side}': f'Clavicle_{side}',
            f'Forearm_{side}': f'UpperArm_{side}',
            f'Hand_{side}': f'Forearm_{side}',
            f'Thigh_{side}': 'Pelvis',
            f'Shin_{side}': f'Thigh_{side}',
            f'Foot_{side}': f'Shin_{side}',
            f'Toe_{side}': f'Foot_{side}',
        })

    arm_data = bpy.data.armatures.new(rig_name)
    rig = bpy.data.objects.new(rig_name, arm_data)
    bpy.context.collection.objects.link(rig)
    bpy.ops.object.select_all(action='DESELECT')
    rig.select_set(True)
    bpy.context.view_layer.objects.active = rig
    bpy.ops.object.mode_set(mode='EDIT')
    for b_name, (head_pt, tail_pt) in joints.items():
        eb = arm_data.edit_bones.new(b_name)
        eb.head = head_pt
        eb.tail = tail_pt
        eb.use_deform = True
        if b_name in parents:
            eb.parent = arm_data.edit_bones[parents[b_name]]
    bpy.ops.object.mode_set(mode='OBJECT')

    # Normalize weapon_hand option ('R', 'Hand_R', 'L', 'Hand_L', 'both')
    wh = (weapon_hand or '').upper()
    w_right = wh in ('R', 'RIGHT', 'HAND_R', 'BOTH')
    w_left = wh in ('L', 'LEFT', 'HAND_L', 'BOTH')

    joint_vecs = {k: (Vector(a), Vector(b)) for k, (a, b) in joints.items()}

    # Skin every mesh in meshes (Body AND Signal!) so visors/blades/neon strips move with bones
    for mesh_obj in meshes:
        bake_transform(mesh_obj)
        pin_idx_to_bone: dict[int, str] = {}
        for pvg in mesh_obj.vertex_groups:
            if pvg.name.startswith('__pin_'):
                target_bone = pvg.name[len('__pin_'):]
                if target_bone in joints:
                    pin_idx_to_bone[pvg.index] = target_bone
        pinned_bone_by_vert: dict[int, str] = {}
        if pin_idx_to_bone:
            for v in mesh_obj.data.vertices:
                for g_elem in v.groups:
                    if g_elem.weight > 0.5 and g_elem.group in pin_idx_to_bone:
                        pinned_bone_by_vert[v.index] = pin_idx_to_bone[g_elem.group]
        for vg in list(mesh_obj.vertex_groups):
            mesh_obj.vertex_groups.remove(vg)

        groups = {k: mesh_obj.vertex_groups.new(name=k) for k in joints}
        arm_split_x = sh_x * 0.82

        for v in mesh_obj.data.vertices:
            vi = v.index
            if vi in pinned_bone_by_vert:
                groups[pinned_bone_by_vert[vi]].add([vi], 1.0, 'REPLACE')
                continue

            p = v.co
            side = 'L' if p.x >= 0.0 else 'R'

            # Held weapon corridor forward of body (-Y in Blender)
            if w_right and p.y < -0.14 * h and p.x <= 0.08 * h and 0.25 * h < p.z < 0.98 * h:
                groups['Hand_R'].add([vi], 1.0, 'REPLACE')
                continue
            if w_left and p.y < -0.14 * h and p.x >= -0.08 * h and 0.25 * h < p.z < 0.98 * h:
                groups['Hand_L'].add([vi], 1.0, 'REPLACE')
                continue

            if p.z > head_z - 0.02 * h and abs(p.x) < sh_x * 1.05:
                choices = ['Head', 'Neck']
            elif abs(p.x) > arm_split_x and p.z > wrist_z - 0.11 * h:
                choices = [f'Clavicle_{side}', f'UpperArm_{side}', f'Forearm_{side}', f'Hand_{side}']
            elif p.z < pelvis_z + 0.04 * h:
                if abs(p.x) < hp_x * 0.45 and p.z > hip_z - 0.05 * h:
                    choices = ['Pelvis', f'Thigh_{side}']
                else:
                    choices = [f'Thigh_{side}', f'Shin_{side}', f'Foot_{side}', f'Toe_{side}']
            else:
                choices = ['Pelvis', 'Spine', 'Chest', 'Neck', f'Clavicle_{side}']

            near = sorted(((_seg_distance(p, *joint_vecs[k]), k) for k in choices))[:2]
            soft = 0.032 * h
            raw_w = [1.0 / ((d + soft) ** 4) for d, _ in near]
            w_sum = sum(raw_w) or 1.0
            for (_, k), w in zip(near, raw_w):
                groups[k].add([vi], w / w_sum, 'REPLACE')

        for mod in list(mesh_obj.modifiers):
            if mod.type == 'ARMATURE':
                mesh_obj.modifiers.remove(mod)
        arm_mod = mesh_obj.modifiers.new('StreetSkeleton', 'ARMATURE')
        arm_mod.object = rig
        mesh_obj.parent = rig

    # Prepare pose rotation helpers in Armature world space:
    # +X = character Left, -Y = Forward, +Z = Up
    rig.animation_data_create()
    bpy.context.scene.render.fps = FPS
    basis = {pb.name: pb.bone.matrix_local.to_quaternion() for pb in rig.pose.bones}

    def reset_pose():
        for pb in rig.pose.bones:
            pb.rotation_mode = 'QUATERNION'
            pb.rotation_quaternion = (1.0, 0.0, 0.0, 0.0)
            pb.location = (0.0, 0.0, 0.0)
            pb.scale = (1.0, 1.0, 1.0)

    def rot(name: str, x: float = 0.0, y: float = 0.0, z: float = 0.0):
        q = basis[name]
        delta = Quaternion((0.0, 0.0, 1.0), z) @ Quaternion((0.0, 1.0, 0.0), y) @ Quaternion((1.0, 0.0, 0.0), x)
        rig.pose.bones[name].rotation_quaternion = q.inverted() @ delta @ q

    def move_pelvis(dx: float = 0.0, dy: float = 0.0, dz: float = 0.0):
        rig.pose.bones['Pelvis'].location = basis['Pelvis'].inverted() @ Vector((dx, dy, dz))

    def key_all(frame: int):
        for pb in rig.pose.bones:
            pb.keyframe_insert('rotation_quaternion', frame=frame)
            if pb.name == 'Pelvis':
                pb.keyframe_insert('location', frame=frame)

    # Clip specs: 6 mandatory + requested attack_clip_name + optional extra_clips
    clip_durations = {
        'Idle': 2.4,
        'Walk': 1.0,
        'Run': 0.66,
        'Punch': 0.65,
        'HitReact': 0.42,
        'Death': 1.15,
        'Sword_Slash': 0.68,
        'Idle_Shoot': 0.85,
        'Attack': 0.82,
    }
    wanted_clips = ['Idle', 'Walk', 'Run', 'Punch', 'HitReact', 'Death']
    if attack_clip_name and attack_clip_name not in wanted_clips:
        wanted_clips.append(attack_clip_name)
    for extra in (extra_clips or []):
        if extra and extra not in wanted_clips:
            wanted_clips.append(extra)

    for clip_title in wanted_clips:
        dur = clip_durations.get(clip_title, 0.70)
        action = bpy.data.actions.new(clip_title)
        action.use_fake_user = True
        rig.animation_data.action = action
        frames = max(4, round(dur * FPS))

        for f in range(frames + 1):
            u = f / frames
            phase = u * math.tau
            reset_pose()

            if clip_title == 'Idle':
                breath = math.sin(phase)
                sway = math.sin(phase)
                scan = math.sin(phase + 0.65)
                move_pelvis(dx=0.015 * h * sway, dz=-0.005 * h * abs(sway))
                rot('Pelvis', x=0.012, y=-0.025 * sway, z=0.024 * sway)
                rot('Spine', x=0.018 - 0.014 * breath, y=0.020 * sway, z=-0.015 * sway)
                rot('Chest', x=-0.024 * breath, y=0.008 * sway, z=0.018 * scan)
                rot('Neck', x=0.012 * breath, z=0.032 * scan)
                rot('Head', x=0.020 * math.cos(phase), y=0.015 * sway, z=0.075 * scan)

                for side, sign in (('L', 1.0), ('R', -1.0)):
                    w_knee = max(0.0, -sway * sign)
                    rot(f'Thigh_{side}', x=-0.035 * w_knee, y=-sign * 0.02)
                    rot(f'Shin_{side}', x=0.065 * w_knee)
                    rot(f'Foot_{side}', x=-0.03 * w_knee)
                    rot(f'Clavicle_{side}', y=-sign * (0.018 + 0.014 * breath))

                if st in ('blade', 'katana', 'swordsman'):
                    rot('Pelvis', x=0.03, y=-0.02 * sway, z=-0.16)
                    rot('Chest', x=-0.02 * breath, z=-0.10)
                    rot('Head', z=0.22 + 0.05 * scan)
                    rot('UpperArm_R', x=-0.36 + 0.03 * breath, y=0.14, z=0.12)
                    rot('Forearm_R', x=-0.58 - 0.04 * breath)
                    rot('UpperArm_L', x=-0.24, y=-0.16)
                    rot('Forearm_L', x=-0.46)
                elif st in ('gunner', 'shooter'):
                    rot('Pelvis', x=0.03, z=-0.18)
                    rot('Chest', x=0.04 - 0.02 * breath, z=0.08)
                    rot('UpperArm_R', x=-0.62 + 0.03 * breath, y=0.10, z=0.18)
                    rot('Forearm_R', x=-0.78)
                    rot('UpperArm_L', x=-0.72 + 0.03 * breath, z=-0.38)
                    rot('Forearm_L', x=-0.72)
                elif st in ('punk', 'enforcer', 'brawler', 'heavy'):
                    rot('UpperArm_L', x=-0.20 + 0.03 * sway, y=-0.10)
                    rot('Forearm_L', x=-0.44)
                    rot('UpperArm_R', x=-0.24 - 0.03 * sway, y=0.10)
                    rot('Forearm_R', x=-0.48)
                elif is_mech:
                    move_pelvis(dz=-0.014 * h * (0.5 - 0.5 * math.cos(phase)))
                    rot('Head', z=0.15 * math.sin(phase))
                    for side, sign in (('L', 1.0), ('R', -1.0)):
                        rot(f'UpperArm_{side}', x=-0.18, y=-sign * 0.14)
                        rot(f'Forearm_{side}', x=-0.38)
                else:
                    for side, sign in (('L', 1.0), ('R', -1.0)):
                        rot(f'UpperArm_{side}', x=0.035 * sway * sign, y=-sign * 0.055)
                        rot(f'Forearm_{side}', x=-0.15 - 0.02 * breath)

            elif clip_title in ('Walk', 'Run'):
                run = clip_title == 'Run'
                s_base = math.sin(phase)
                c_base = math.cos(phase)
                bob = (0.036 if run else 0.018) * h * math.cos(2.0 * phase) - (0.024 if run else 0.011) * h
                lat = (0.012 if run else 0.018) * h * s_base
                move_pelvis(dx=lat, dz=bob)

                lean = (0.09 if is_mech else 0.14) if run else (0.04 if is_mech else 0.025)
                hip_twist = (0.11 if run else 0.065) * s_base
                hip_roll = (0.055 if run else 0.042) * s_base
                rot('Pelvis', x=lean * 0.55, y=-hip_roll, z=-hip_twist)
                rot('Spine', x=lean, y=hip_roll * 0.6, z=hip_twist * 0.75)
                rot('Chest', x=lean * 0.75, y=hip_roll * 0.35, z=hip_twist * 1.15)
                rot('Neck', x=-lean * 0.55, z=-hip_twist * 0.45)
                rot('Head', x=-lean * 0.85 + 0.02 * math.cos(2.0 * phase), z=-hip_twist * 0.65)

                for side, sign in (('L', 1.0), ('R', -1.0)):
                    sw = s_base * sign
                    cw = c_base * sign
                    thigh_amp = (0.58 if is_mech else 0.78) if run else (0.36 if is_mech else 0.45)
                    lift = max(0.0, cw) * (0.20 if run else 0.09)
                    knee = (0.10 if run else 0.05) + max(0.0, -sw) * (0.82 if run else 0.42) + max(0.0, cw) * (0.52 if run else 0.30)
                    ankle = (0.34 if run else 0.22) * max(0.0, -sw) - (0.20 if run else 0.16) * max(0.0, sw)

                    rot(f'Thigh_{side}', x=-thigh_amp * sw - lift, y=-sign * 0.02)
                    rot(f'Shin_{side}', x=knee)
                    rot(f'Foot_{side}', x=ankle)
                    rot(f'Toe_{side}', x=(0.28 if run else 0.18) * max(0.0, -sw))

                    arm_amp = (0.48 if is_mech else 0.68) if run else (0.24 if is_mech else 0.32)
                    elbow_base = -0.88 if run else -0.18
                    elbow_pump = -0.24 * max(0.0, -sw) if run else -0.14 * max(0.0, -sw)
                    rot(f'Clavicle_{side}', z=0.06 * sw)
                    rot(f'UpperArm_{side}', x=arm_amp * sw, y=-sign * (0.085 if run else 0.055))
                    rot(f'Forearm_{side}', x=elbow_base + elbow_pump)

            elif clip_title == 'Sword_Slash':
                wind = _smoothstep(0.0, 0.30, u) * (1.0 - _smoothstep(0.30, 0.44, u))
                snap = _smoothstep(0.28, 0.50, u) * (1.0 - _smoothstep(0.66, 1.0, u))

                move_pelvis(dy=-0.04 * h * snap + 0.02 * h * wind, dz=-0.045 * h * snap)
                rot('Pelvis', x=0.14 * snap - 0.06 * wind, z=0.24 * snap - 0.22 * wind)
                rot('Spine', x=0.18 * snap - 0.10 * wind, z=0.28 * snap - 0.26 * wind)
                rot('Chest', x=0.14 * snap - 0.08 * wind, z=0.34 * snap - 0.30 * wind)
                rot('Head', x=-0.10 * snap, z=-0.38 * snap + 0.32 * wind)

                rot('Clavicle_R', y=0.18 * wind, z=0.25 * snap - 0.18 * wind)
                rot('UpperArm_R', x=-2.05 * wind - 0.68 * snap, y=0.34 * wind - 0.22 * snap, z=-0.24 * wind + 0.62 * snap)
                rot('Forearm_R', x=-0.82 * wind - 0.14 * snap)
                rot('Hand_R', x=-0.24 * wind + 0.36 * snap)

                rot('UpperArm_L', x=-1.10 * wind + 0.28 * snap, y=-0.12 - 0.20 * snap, z=-0.42 * wind)
                rot('Forearm_L', x=-0.70 * wind - 0.52 * snap)

                rot('Thigh_L', x=-0.32 * snap - 0.08 * wind)
                rot('Shin_L', x=0.40 * snap + 0.14 * wind)
                rot('Thigh_R', x=0.24 * snap)
                rot('Shin_R', x=0.22 * snap)

            elif clip_title == 'Idle_Shoot':
                brace = _smoothstep(0.0, 0.16, u) * (1.0 - _smoothstep(0.84, 1.0, u))
                recoil = 0.0
                for shot_u in (0.24, 0.46, 0.68):
                    if u >= shot_u:
                        dt_u = (u - shot_u) / 0.16
                        if dt_u < 1.0:
                            recoil += math.sin(math.pi * dt_u) * (1.0 - 0.35 * dt_u)

                move_pelvis(dy=0.015 * h * recoil, dz=-0.022 * h * brace)
                rot('Pelvis', x=0.04 * brace, z=-0.24 * brace)
                rot('Spine', x=0.08 * brace - 0.06 * recoil, z=0.08 * brace)
                rot('Chest', x=0.06 * brace - 0.09 * recoil, z=0.16 * brace)
                rot('Neck', x=0.05 * brace, z=0.04 * brace)
                rot('Head', x=0.08 * brace - 0.05 * recoil, y=-0.08 * brace, z=0.06 * brace)

                rot('Clavicle_R', z=-0.08 * recoil)
                rot('UpperArm_R', x=-1.18 * brace - 0.10 * recoil, y=0.10 * brace, z=0.18 * brace)
                rot('Forearm_R', x=-0.62 * brace - 0.14 * recoil)
                rot('UpperArm_L', x=-1.24 * brace - 0.10 * recoil, z=-0.48 * brace)
                rot('Forearm_L', x=-0.46 * brace - 0.10 * recoil)

                rot('Thigh_L', x=-0.16 * brace)
                rot('Shin_L', x=0.24 * brace)
                rot('Thigh_R', x=0.12 * brace)
                rot('Shin_R', x=0.18 * brace)

            elif clip_title == 'Attack':
                # Heavy two-arm overhead smash (mech / heavy enforcer / custom Attack)
                wind = _smoothstep(0.0, 0.36, u) * (1.0 - _smoothstep(0.36, 0.50, u))
                slam = _smoothstep(0.34, 0.56, u) * (1.0 - _smoothstep(0.72, 1.0, u))

                move_pelvis(dz=0.025 * h * wind - 0.085 * h * slam)
                rot('Pelvis', x=-0.10 * wind + 0.18 * slam)
                rot('Spine', x=-0.18 * wind + 0.28 * slam)
                rot('Chest', x=-0.22 * wind + 0.24 * slam)
                rot('Head', x=0.14 * wind - 0.18 * slam)

                for side, sign in (('L', 1.0), ('R', -1.0)):
                    rot(f'Clavicle_{side}', y=-sign * 0.20 * wind)
                    rot(f'UpperArm_{side}', x=-2.15 * wind - 0.82 * slam, y=-sign * (0.22 * wind - 0.12 * slam))
                    rot(f'Forearm_{side}', x=-0.58 * wind - 0.22 * slam)
                    rot(f'Thigh_{side}', x=-0.34 * slam - 0.08 * wind, y=-sign * 0.05 * slam)
                    rot(f'Shin_{side}', x=0.56 * slam + 0.12 * wind)
                    rot(f'Foot_{side}', x=-0.18 * slam)

            elif clip_title == 'HitReact':
                hit = math.sin(math.pi * (u ** 0.58))
                move_pelvis(dy=0.035 * h * hit, dz=-0.028 * h * hit)
                rot('Pelvis', x=-0.10 * hit, z=-0.10 * hit)
                rot('Spine', x=-0.24 * hit, y=0.06 * hit, z=-0.16 * hit)
                rot('Chest', x=-0.22 * hit, y=0.08 * hit, z=-0.20 * hit)
                rot('Neck', x=-0.16 * hit)
                rot('Head', x=-0.28 * hit, y=-0.10 * hit, z=0.18 * hit)

                for side, sign in (('L', 1.0), ('R', -1.0)):
                    rot(f'UpperArm_{side}', x=-0.26 * hit, y=-sign * 0.32 * hit)
                    rot(f'Forearm_{side}', x=-0.58 * hit)
                    rot(f'Thigh_{side}', x=-0.16 * hit)
                    rot(f'Shin_{side}', x=0.28 * hit)

            elif clip_title == 'Death':
                buckle = _smoothstep(0.0, 0.36, u) * (1.0 - _smoothstep(0.45, 0.82, u))
                fall = _smoothstep(0.30, 0.86, u)
                # Drop Pelvis head cleanly so chest/hips/limbs rest flat on z = 0 ground
                drop_z = -(pelvis_z - 0.135 * h) * fall - 0.18 * h * buckle
                move_pelvis(dy=-0.08 * h * fall + 0.03 * h * buckle, dz=drop_z)

                rot('Pelvis', x=-0.14 * buckle + 1.48 * fall, y=0.05 * fall, z=0.05 * fall)
                rot('Spine', x=-0.20 * buckle - 0.02 * fall, z=-0.12 * buckle)
                rot('Chest', x=-0.16 * buckle - 0.03 * fall, z=0.06 * fall)
                rot('Neck', x=-0.06 * fall)
                rot('Head', x=-0.20 * buckle - 0.10 * fall, z=0.45 * fall)

                for side, sign in (('L', 1.0), ('R', -1.0)):
                    rot(f'Thigh_{side}', x=-0.56 * buckle + 0.06 * fall, y=-sign * 0.09 * fall)
                    rot(f'Shin_{side}', x=1.08 * buckle + 0.02 * fall)
                    rot(f'Foot_{side}', x=-0.25 * buckle + 0.45 * fall)
                    rot(f'UpperArm_{side}', x=-0.38 * buckle - 0.05 * fall, y=-sign * (0.28 * buckle + 0.52 * fall))
                    rot(f'Forearm_{side}', x=-0.65 * buckle - 0.18 * fall, y=-sign * 0.18 * fall, z=-sign * 0.55 * fall)

            else:
                # 'Punch' (and any custom melee clip name): windup -> fast right cross snap -> recovery
                wind = _smoothstep(0.0, 0.26, u) * (1.0 - _smoothstep(0.26, 0.40, u))
                strike = _smoothstep(0.24, 0.46, u) * (1.0 - _smoothstep(0.64, 1.0, u))

                move_pelvis(dy=-0.03 * h * strike + 0.015 * h * wind, dz=-0.03 * h * strike)
                rot('Pelvis', x=0.08 * strike - 0.04 * wind, z=0.18 * strike - 0.16 * wind)
                rot('Spine', x=0.12 * strike - 0.06 * wind, z=0.22 * strike - 0.20 * wind)
                rot('Chest', x=0.10 * strike - 0.06 * wind, z=0.28 * strike - 0.24 * wind)
                rot('Head', x=-0.08 * strike, z=-0.36 * strike + 0.28 * wind)

                rot('Clavicle_R', z=0.22 * strike - 0.14 * wind)
                rot('UpperArm_R', x=0.46 * wind - 1.48 * strike, y=0.20 * wind + 0.05 * strike, z=0.20 * strike)
                rot('Forearm_R', x=-1.15 * wind - 0.16 * strike)
                rot('Hand_R', x=0.10 * strike)

                rot('UpperArm_L', x=-0.48 * wind + 0.24 * strike, y=-0.12)
                rot('Forearm_L', x=-1.10 * wind - 0.95 * strike)

                rot('Thigh_L', x=-0.22 * strike)
                rot('Shin_L', x=0.28 * strike)
                rot('Thigh_R', x=0.16 * strike)
                rot('Shin_R', x=0.18 * strike)

            key_all(f + 1)

        for fc in action.fcurves:
            for kp in fc.keyframe_points:
                kp.interpolation = 'LINEAR'

    rig.animation_data.action = None
    reset_pose()
    bpy.context.scene.frame_set(1)
    return rig


# ---------------------------------------------------------------------------
# Validated Binary GLB 2.0 Export (tools/assets.mjs Contract)
# ---------------------------------------------------------------------------

def _flatten_export_objects(objects) -> list[bpy.types.Object]:
    raw: list[bpy.types.Object] = []

    def _collect(item):
        if item is None:
            return
        if isinstance(item, bpy.types.Object):
            raw.append(item)
        elif isinstance(item, (list, tuple, set)):
            for sub in item:
                _collect(sub)

    _collect(objects)

    expanded: list[bpy.types.Object] = []
    seen = set()

    def _add(o: bpy.types.Object):
        if o is not None and o.name not in seen:
            seen.add(o.name)
            expanded.append(o)

    for o in raw:
        _add(o)
        if o.parent is not None:
            _add(o.parent)
        if o.type == 'MESH':
            for mod in o.modifiers:
                if mod.type == 'ARMATURE' and mod.object is not None:
                    _add(mod.object)
        if o.type in ('ARMATURE', 'EMPTY'):
            for child in o.children_recursive:
                if child.type == 'MESH':
                    _add(child)
    return expanded


def verify_glb_file(path: Path, expect_rig: bool = False, max_meshes: int = 6, min_tris: int = 501) -> dict:
    """Verify exported binary GLB against every rule in tools/assets.mjs."""
    data = Path(path).read_bytes()
    magic, version, total_len = struct.unpack_from('<3I', data, 0)
    assert magic == 0x46546C67, f'{path}: invalid GLB magic'
    assert version == 2, f'{path}: expected glTF 2.0'
    assert total_len == len(data), f'{path}: header length mismatch'

    json_len, json_type = struct.unpack_from('<2I', data, 12)
    assert json_type == 0x4E4F534A, f'{path}: expected JSON chunk'
    g = json.loads(data[20:20 + json_len].decode('utf-8'))
    bin_chunk = data[28 + json_len:]

    assert g.get('asset', {}).get('copyright'), f'{path}: missing copyright metadata'
    assert all(not b.get('uri') for b in g.get('buffers', [])), f'{path}: buffers must be embedded'

    meshes = g.get('meshes', [])
    assert 1 <= len(meshes) <= max_meshes, f'{path}: mesh count {len(meshes)} outside 1..{max_meshes}'

    triangles = 0
    for m in meshes:
        for p in m.get('primitives', []):
            attrs = p.get('attributes', {})
            assert 'COLOR_0' in attrs, f'{path}: primitive missing COLOR_0 vertex colors'
            pos_acc = g['accessors'][attrs['POSITION']]
            assert pos_acc['type'] == 'VEC3', f'{path}: POSITION must be VEC3'
            assert all(math.isfinite(v) for v in pos_acc['min']) and all(math.isfinite(v) for v in pos_acc['max']), (
                f'{path}: non-finite POSITION bounds'
            )
            triangles += g['accessors'][p['indices']]['count'] // 3

    assert triangles >= min_tris, f'{path}: triangle count {triangles} < {min_tris} (> 500 required)'

    comp_counts = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}
    for acc in g.get('accessors', []):
        if acc.get('componentType') != 5126 or 'bufferView' not in acc:
            continue
        bv = g['bufferViews'][acc['bufferView']]
        n_comp = comp_counts[acc['type']]
        offset = bv.get('byteOffset', 0) + acc.get('byteOffset', 0)
        stride = bv.get('byteStride', n_comp * 4)
        for i in range(acc['count']):
            vals = struct.unpack_from(f'<{n_comp}f', bin_chunk, offset + i * stride)
            if not all(math.isfinite(x) for x in vals):
                raise AssertionError(f'{path}: non-finite float in accessor')

    anim_names = sorted(a['name'] for a in g.get('animations', []))
    if expect_rig:
        skins = g.get('skins', [])
        assert any(len(s.get('joints', [])) >= 18 for s in skins), f'{path}: expected >= 18 skin joints'
        for req in ('Idle', 'Walk', 'Run', 'Punch', 'HitReact', 'Death'):
            assert req in anim_names, f'{path}: missing required clip {req!r} (have {anim_names})'
        assert all(
            'JOINTS_0' in p.get('attributes', {})
            for m in meshes
            for p in m.get('primitives', [])
        ), f'{path}: all character primitives must be skinned (JOINTS_0)'

    return {
        'path': str(path),
        'triangles': triangles,
        'meshes': len(meshes),
        'size_kb': round(len(data) / 1024.0, 1),
        'animations': anim_names,
    }


def export_glb(
    out_path: str | Path,
    objects,
    copyright: str = COPYRIGHT,
    min_tris: int = 501,
    max_meshes: int = 6,
) -> dict:
    """Validate finite geometry, triangle budget (>500), mesh count (<=6), and export GLB 2.0."""
    target = Path(out_path)
    target.parent.mkdir(parents=True, exist_ok=True)

    export_objs = _flatten_export_objects(objects)
    mesh_objs = [o for o in export_objs if o.type == 'MESH']
    rig_objs = [o for o in export_objs if o.type == 'ARMATURE']

    if not (1 <= len(mesh_objs) <= max_meshes):
        raise ValueError(f'{target.name}: expected 1..{max_meshes} mesh objects, got {len(mesh_objs)}')

    for m_obj in mesh_objs:
        mesh = m_obj.data
        ensure_uvs(m_obj)
        col = mesh.color_attributes.get('Color')
        if col is None:
            paint_mesh(m_obj, signal=bool(m_obj.get('is_signal', False)))
            col = mesh.color_attributes['Color']
        mesh.color_attributes.active_color = col
        for idx, attr in enumerate(mesh.color_attributes):
            if attr.name == 'Color':
                mesh.color_attributes.render_color_index = idx
                break
        if not mesh.materials:
            mesh.materials.append(get_vertex_material(bool(m_obj.get('is_signal', False))))

        for v in mesh.vertices:
            if not all(math.isfinite(c) for c in v.co):
                raise ValueError(f'{target.name}: non-finite vertex coordinate in {m_obj.name}')
        for d in col.data:
            if not all(math.isfinite(c) for c in d.color):
                raise ValueError(f'{target.name}: non-finite vertex color in {m_obj.name}')

    tris = count_triangles(mesh_objs)
    if tris < min_tris:
        raise ValueError(f'{target.name}: triangle count {tris} < {min_tris} (> 500 required by tools/assets.mjs)')

    for r_obj in rig_objs:
        if r_obj.animation_data:
            r_obj.animation_data.action = None
        for pb in r_obj.pose.bones:
            pb.rotation_mode = 'QUATERNION'
            pb.rotation_quaternion = (1.0, 0.0, 0.0, 0.0)
            pb.location = (0.0, 0.0, 0.0)
            pb.scale = (1.0, 1.0, 1.0)

    bpy.context.scene.render.fps = FPS
    bpy.context.scene.frame_set(1)

    bpy.ops.object.select_all(action='DESELECT')
    for o in export_objs:
        o.hide_viewport = False
        o.hide_render = False
        o.hide_set(False)
        o.select_set(True)
    bpy.context.view_layer.objects.active = export_objs[0]

    bpy.ops.export_scene.gltf(
        filepath=str(target),
        export_format='GLB',
        use_selection=True,
        export_yup=True,
        export_animations=True,
        export_animation_mode='ACTIONS',
        export_frame_range=False,
        export_force_sampling=True,
        export_cameras=False,
        export_lights=False,
        export_all_vertex_colors=True,
        export_copyright=copyright,
    )

    return verify_glb_file(target, expect_rig=bool(rig_objs), max_meshes=max_meshes, min_tris=min_tris)
