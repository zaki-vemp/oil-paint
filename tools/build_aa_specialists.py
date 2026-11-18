"""Headless Blender 4.2 LTS AA character sculptor for Komorebi street specialists.

Authors and exports two stylized AA cyberpunk human characters into public/street/:
  1. public/street/courier.glb   — Syndicate Hooded Data-Runner (~1,800–3,400 tris, ~1.79m)
     * Sculpted angular double-walled windbreaker hood framing the face
     * Sleek glowing cyan/magenta 'Signal' cyber-visor across the eyes
     * Aerodynamic cropped runner jacket with dorsal 'Signal' spine power-strip
     * Strapped cross-body courier cyber-deck satchel with telemetry LEDs
     * Contoured athletic hard-shell shin guards & split-sole sprint shoes
  2. public/street/executive.glb — Aethelgard Corporate Executive (~1,800–3,400 tris, ~1.82m)
     * Sharp high-collared tailored Neo-Kyoto longcoat (split hem weighted cleanly to thighs)
     * Gold/amber 'Signal' collar & cuff circuitry traces
     * Sleek gold-bridged cyber-optic glasses with glowing amber lenses
     * Pointed-toe corporate dress boots & swept-back slicked hair

Orientation & rig contract (Blender Z-up -> glTF/Three.js Y-up via export_yup=True):
  * Facing -Y in Blender = +Z in Three.js; +X = character Left, -X = Right, z = 0 = ground.
  * 21-bone humanoid skeleton (>= 18), JOINTS_0 skinning, 2 batched meshes ('Body' + 'Signal'),
    COLOR_0 FLOAT_COLOR vertex colors, finite floats, and all 6 required clips:
    'Idle', 'Walk', 'Run', 'Punch', 'HitReact', 'Death'.

Run with:
  .test/blender/blender --background --disable-autoexec --python tools/build_aa_specialists.py
"""

from __future__ import annotations

import json
import math
import struct
import sys
import traceback
from pathlib import Path
from typing import Sequence

sys.dont_write_bytecode = True

TOOLS_DIR = Path(__file__).resolve().parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

import bmesh
import bpy
from mathutils import Matrix, Vector

import blender_aa_lib as aa

ROOT = TOOLS_DIR.parent
OUT_DIR = ROOT / 'public' / 'street'


# ---------------------------------------------------------------------------
# Curated Character Palettes (Linear RGB for Komorebi Paint & Signal Shaders)
# ---------------------------------------------------------------------------

COURIER_COLORS = {
    'jacket_main': (0.088, 0.124, 0.192),      # Syndicate stealth midnight-navy
    'jacket_panel': (0.760, 0.815, 0.870),     # High-contrast aero ice-silver yoke
    'jacket_teal': (0.070, 0.395, 0.475),      # Electric Syndicate teal trim
    'hood_lining': (0.460, 0.095, 0.250),      # Deep magenta-crimson inner hood weave
    'base_suit': (0.065, 0.078, 0.108),        # Matte carbon runner compression suit
    'armor_plate': (0.520, 0.585, 0.665),      # Brushed titanium shin/forearm plates
    'armor_dark': (0.140, 0.170, 0.220),       # Gunmetal housing & buckles
    'strap_web': (0.120, 0.135, 0.170),        # Reinforced kevlar cross-body webbing
    'deck_case': (0.205, 0.250, 0.320),        # Armored cyber-deck slate shell
    'shoe_upper': (0.095, 0.125, 0.185),       # Sprint shoe tech-mesh upper
    'shoe_sole': (0.830, 0.860, 0.900),        # Crisp white aero foam midsole
    'shoe_heel': (0.660, 0.115, 0.320),        # Syndicate magenta heel stabilizer
    'skin': (0.745, 0.518, 0.378),             # Stylized warm runner skin
    'visor_cyan': (0.035, 0.945, 1.000),       # Signal: luminous cyan visor/spine
    'visor_magenta': (1.000, 0.135, 0.565),    # Signal: hot magenta telemetry accents
}

EXEC_COLORS = {
    'coat_main': (0.055, 0.068, 0.110),        # Aristocratic midnight-obsidian wool
    'coat_lining': (0.475, 0.075, 0.128),      # Imperial Neo-Kyoto crimson silk lining
    'coat_lapel': (0.800, 0.768, 0.690),       # Tailored champagne-ivory lapel trim
    'waistcoat': (0.145, 0.165, 0.220),        # Charcoal structured inner vest
    'shirt_silk': (0.855, 0.835, 0.785),       # Crisp ivory high-collar silk placket
    'trouser': (0.085, 0.100, 0.142),          # Pressed dark slate dress trousers
    'boot_leather': (0.040, 0.044, 0.060),     # Polished obsidian dress boot leather
    'gold_trim': (0.800, 0.610, 0.220),        # Brushed Aethelgard brass/gold hardware
    'hair_dark': (0.065, 0.065, 0.090),        # Slicked raven-espresso hair
    'hair_silver': (0.580, 0.615, 0.675),      # Distinguished silver temple sweep
    'skin': (0.820, 0.615, 0.475),             # Chiseled light-warm skin
    'sig_gold': (1.000, 0.660, 0.110),         # Signal: glowing Aethelgard amber-gold
    'sig_solar': (1.000, 0.845, 0.280),        # Signal: bright solar-gold optic highlight
}


# ---------------------------------------------------------------------------
# Math, Mirroring & Multi-Bone Skinning Helpers
# ---------------------------------------------------------------------------

def _lerp(a: float, b: float, t: float) -> float:
    return a + (b - a) * t


def _smoothstep(edge0: float, edge1: float, x: float) -> float:
    if abs(edge1 - edge0) < 1e-8:
        return 1.0 if x >= edge1 else 0.0
    t = max(0.0, min(1.0, (x - edge0) / (edge1 - edge0)))
    return t * t * (3.0 - 2.0 * t)


def _vlerp(a: Vector, b: Vector, t: float) -> Vector:
    return a + (b - a) * t


def tag_skin_role(objs, role: str):
    """Attach a side-agnostic '__role_<role>' vertex group preserved across combine_by_role."""
    single = isinstance(objs, bpy.types.Object)
    items = [objs] if single else list(objs)
    vg_name = f'__role_{role}'
    for obj in items:
        if obj is None or obj.type != 'MESH':
            continue
        for vg in list(obj.vertex_groups):
            if vg.name.startswith('__role_') or vg.name.startswith('__pin_'):
                obj.vertex_groups.remove(vg)
        vg = obj.vertex_groups.new(name=vg_name)
        idxs = list(range(len(obj.data.vertices)))
        if idxs:
            vg.add(idxs, 1.0, 'REPLACE')
    return objs


def mirror_clean(objs):
    """Mirror across X=0 via aa.mirror_x while cleaning inherited source vertex-group weights."""
    single = isinstance(objs, bpy.types.Object)
    src_list = [objs] if single else list(objs)
    dup_list = [aa.mirror_x(o) for o in src_list]

    def _swap_lr(text: str) -> str:
        if '_L' in text:
            return text.replace('_L', '_R')
        if '_R' in text:
            return text.replace('_R', '_L')
        return text

    for src, dup in zip(src_list, dup_list):
        if src is None or dup is None or dup.type != 'MESH':
            continue
        wanted_vgs = [_swap_lr(vg.name) for vg in src.vertex_groups]
        for vg in list(dup.vertex_groups):
            dup.vertex_groups.remove(vg)
        all_idxs = list(range(len(dup.data.vertices)))
        for vg_name in wanted_vgs:
            vg = dup.vertex_groups.new(name=vg_name)
            if all_idxs:
                vg.add(all_idxs, 1.0, 'REPLACE')
    return dup_list[0] if single else dup_list


def _box(*args, role: str | None = None, **kwargs) -> bpy.types.Object:
    obj = aa.add_box(*args, **kwargs)
    return tag_skin_role(obj, role) if role else obj


def _bevel_box(*args, role: str | None = None, **kwargs) -> bpy.types.Object:
    obj = aa.add_bevel_box(*args, **kwargs)
    return tag_skin_role(obj, role) if role else obj


def snapshot_skin_roles(meshes: Sequence[bpy.types.Object]) -> dict[str, dict[int, str]]:
    """Capture per-vertex '__role_*' assignments before build_humanoid_rig_and_clips clears groups."""
    out: dict[str, dict[int, str]] = {}
    for m_obj in meshes:
        if m_obj is None or m_obj.type != 'MESH':
            continue
        idx_to_role: dict[int, str] = {}
        for vg in m_obj.vertex_groups:
            if vg.name.startswith('__role_'):
                role_key = vg.name[len('__role_'):]
                if role_key.endswith('_Mirror'):
                    role_key = role_key[:-7]
                idx_to_role[vg.index] = role_key
        vert_roles: dict[int, str] = {}
        if idx_to_role:
            for v in m_obj.data.vertices:
                for g_elem in v.groups:
                    if g_elem.group in idx_to_role and g_elem.weight > 0.5:
                        vert_roles[v.index] = idx_to_role[g_elem.group]
        out[m_obj.name] = vert_roles
    return out


def _seg_dist(p: Vector, a: Vector, b: Vector) -> float:
    ab = b - a
    lsq = ab.length_squared
    if lsq < 1e-10:
        return (p - a).length
    t = max(0.0, min(1.0, (p - a).dot(ab) / lsq))
    return (p - (a + ab * t)).length


def apply_specialist_skinning(
    rig: bpy.types.Object,
    meshes: Sequence[bpy.types.Object],
    role_snapshot: dict[str, dict[int, str]],
    height: float,
) -> None:
    """Refine multi-bone garment & limb weights using true X-sign side separation."""
    bone_segs: dict[str, tuple[Vector, Vector]] = {}
    for b in rig.data.bones:
        bone_segs[b.name] = (b.head_local.copy(), b.tail_local.copy())

    h = float(height)
    pelvis_z = 0.48 * h
    spine_z = 0.57 * h
    chest_z = 0.68 * h
    neck_z = 0.78 * h
    hip_z = 0.505 * h
    ankle_z = 0.080 * h

    for m_obj in meshes:
        v_roles = role_snapshot.get(m_obj.name, {})
        if not v_roles:
            continue
        vgs = {vg.name: vg for vg in m_obj.vertex_groups}
        all_bone_vgs = [vgs[k] for k in bone_segs if k in vgs]

        def set_weights(vi: int, pairs: list[tuple[str, float]]) -> None:
            for bvg in all_bone_vgs:
                bvg.remove([vi])
            filtered = [(bn, max(0.0, float(w))) for bn, w in pairs if bn in vgs and w > 1e-4]
            if not filtered:
                vgs['Pelvis'].add([vi], 1.0, 'REPLACE')
                return
            filtered.sort(key=lambda item: item[1], reverse=True)
            filtered = filtered[:4]
            total = sum(w for _, w in filtered) or 1.0
            for bn, w in filtered:
                vgs[bn].add([vi], w / total, 'REPLACE')

        for vi, role in v_roles.items():
            p = m_obj.data.vertices[vi].co
            side = 'L' if p.x >= 0.0 else 'R'

            if role == 'torso':
                if p.z < spine_z:
                    t = _smoothstep(pelvis_z - 0.03 * h, spine_z, p.z)
                    w_list = [('Pelvis', 1.0 - t), ('Spine', t)]
                elif p.z < chest_z:
                    t = _smoothstep(spine_z, chest_z, p.z)
                    w_list = [('Spine', 1.0 - t), ('Chest', t)]
                else:
                    t_neck = _smoothstep(neck_z - 0.02 * h, neck_z + 0.04 * h, p.z)
                    w_chest = 1.0 - 0.65 * t_neck
                    w_neck = 0.65 * t_neck
                    sh_share = 0.28 * _smoothstep(0.068 * h, 0.125 * h, abs(p.x)) * _smoothstep(chest_z, neck_z, p.z)
                    w_list = [
                        ('Chest', w_chest * (1.0 - sh_share)),
                        ('Neck', w_neck * (1.0 - sh_share)),
                        (f'Clavicle_{side}', sh_share),
                    ]
                set_weights(vi, w_list)

            elif role == 'pelvis_saddle':
                # Smooth hip/groin bridge from Pelvis into left/right Thigh
                t_th = _smoothstep(0.025 * h, 0.068 * h, abs(p.x)) * _smoothstep(hip_z + 0.015 * h, hip_z - 0.045 * h, p.z)
                set_weights(vi, [
                    ('Pelvis', 1.0 - 0.65 * t_th),
                    (f'Thigh_{side}', 0.65 * t_th),
                ])

            elif role == 'collar':
                t_up = _smoothstep(neck_z - 0.02 * h, neck_z + 0.075 * h, p.z)
                set_weights(vi, [
                    ('Chest', 1.0 - 0.76 * t_up),
                    ('Neck', 0.56 * t_up),
                    ('Head', 0.20 * t_up),
                ])

            elif role == 'coat_skirt':
                # Split-hem longcoat panel: seamless waist bridge (Spine/Pelvis) -> 100% Thigh_L or Thigh_R
                waist_top = hip_z + 0.022 * h
                thigh_lock = hip_z - 0.075 * h
                t_thigh = _smoothstep(waist_top, thigh_lock, p.z)
                t_spine = _smoothstep(pelvis_z, waist_top + 0.02 * h, p.z)
                torso_part = 1.0 - t_thigh
                set_weights(vi, [
                    ('Spine', torso_part * 0.38 * t_spine),
                    ('Pelvis', torso_part * (1.0 - 0.38 * t_spine)),
                    (f'Thigh_{side}', t_thigh),
                ])

            elif role == 'arm':
                choices = [f'Clavicle_{side}', f'UpperArm_{side}', f'Forearm_{side}', f'Hand_{side}']
                near = sorted(((_seg_dist(p, *bone_segs[k]), k) for k in choices))[:2]
                soft = 0.026 * h
                raw = [(k, 1.0 / ((d + soft) ** 4)) for d, k in near]
                set_weights(vi, raw)

            elif role == 'leg':
                choices = [f'Thigh_{side}', f'Shin_{side}']
                if p.z > hip_z - 0.030 * h:
                    choices.append('Pelvis')
                near = sorted(((_seg_dist(p, *bone_segs[k]), k) for k in choices))[:2]
                soft = 0.028 * h
                raw = [(k, 1.0 / ((d + soft) ** 4)) for d, k in near]
                set_weights(vi, raw)

            elif role == 'foot':
                t_shin = _smoothstep(ankle_z + 0.015 * h, ankle_z + 0.068 * h, p.z)
                t_toe = _smoothstep(-0.072 * h, -0.118 * h, p.y) * (1.0 - t_shin)
                w_foot = max(0.0, 1.0 - t_shin - t_toe)
                set_weights(vi, [
                    (f'Shin_{side}', t_shin),
                    (f'Foot_{side}', w_foot),
                    (f'Toe_{side}', t_toe),
                ])


# ---------------------------------------------------------------------------
# Watertight Manifold Loft & Double-Walled Garment Shell Builders
# ---------------------------------------------------------------------------

def build_lofted_tube(
    rings: Sequence[Sequence[tuple[float, float, float]]],
    name: str,
    color=(0.30, 0.35, 0.42),
    signal: bool = False,
    cap_bottom: bool = True,
    cap_top: bool = True,
    shade_smooth: bool = True,
    ao_strength: float = 0.26,
    top_highlight: float = 0.11,
    ridge_boost: float = 0.09,
    ground_ao: float = 0.16,
    gradient_z=None,
    bone: str | None = None,
    role: str | None = None,
) -> bpy.types.Object:
    """Build a watertight manifold loft across closed rings of equal vertex count."""
    if len(rings) < 2:
        raise ValueError(f'{name}: need >= 2 rings')
    m = len(rings[0])
    if m < 3 or any(len(r) != m for r in rings):
        raise ValueError(f'{name}: every ring must share the same vertex count >= 3')

    bm = bmesh.new()
    bm_rings = [[bm.verts.new(pt) for pt in ring] for ring in rings]

    for k in range(len(bm_rings) - 1):
        ra = bm_rings[k]
        rb = bm_rings[k + 1]
        for i in range(m):
            j = (i + 1) % m
            bm.faces.new((ra[i], ra[j], rb[j], rb[i]))

    if cap_bottom:
        c0 = tuple(sum(pt[d] for pt in rings[0]) / m for d in range(3))
        vb = bm.verts.new(c0)
        for i in range(m):
            j = (i + 1) % m
            bm.faces.new((vb, bm_rings[0][j], bm_rings[0][i]))

    if cap_top:
        c1 = tuple(sum(pt[d] for pt in rings[-1]) / m for d in range(3))
        vt = bm.verts.new(c1)
        for i in range(m):
            j = (i + 1) % m
            bm.faces.new((bm_rings[-1][i], bm_rings[-1][j], vt))

    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()

    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    for poly in mesh.polygons:
        poly.use_smooth = bool(shade_smooth)
    mesh.validate()
    mesh.update()

    aa.paint_mesh(
        obj,
        color=color,
        signal=signal,
        ao_strength=ao_strength,
        top_highlight=top_highlight,
        ridge_boost=ridge_boost,
        ground_ao=ground_ao,
        gradient_z=gradient_z,
    )
    if bone:
        aa.pin_to_bone(obj, bone)
    if role:
        tag_skin_role(obj, role)
    return obj


def build_thick_shell(
    outer_grid: Sequence[Sequence[tuple[float, float, float]]],
    inner_grid: Sequence[Sequence[tuple[float, float, float]]],
    name: str,
    outer_color=(0.10, 0.13, 0.20),
    inner_color=None,
    rim_color=None,
    signal: bool = False,
    shade_smooth: bool = True,
    ao_strength: float = 0.26,
    ground_ao: float = 0.14,
    bone: str | None = None,
    role: str | None = None,
) -> bpy.types.Object:
    """Build a 100% closed manifold double-walled garment/armor shell with distinct inner & rim colors."""
    rows = len(outer_grid)
    cols = len(outer_grid[0]) if rows else 0
    if rows < 2 or cols < 2:
        raise ValueError(f'{name}: shell grids must be at least 2x2')
    if len(inner_grid) != rows or any(len(r) != cols for r in inner_grid):
        raise ValueError(f'{name}: outer_grid and inner_grid dimensions must match')

    bm = bmesh.new()
    o_verts = [[bm.verts.new(pt) for pt in row] for row in outer_grid]
    i_verts = [[bm.verts.new(pt) for pt in row] for row in inner_grid]

    tag_layer = bm.faces.layers.int.new('shell_zone')  # 0=outer, 1=inner, 2=rim

    for r in range(rows - 1):
        for c in range(cols - 1):
            f = bm.faces.new((o_verts[r][c], o_verts[r][c + 1], o_verts[r + 1][c + 1], o_verts[r + 1][c]))
            f[tag_layer] = 0

    for r in range(rows - 1):
        for c in range(cols - 1):
            f = bm.faces.new((i_verts[r][c], i_verts[r + 1][c], i_verts[r + 1][c + 1], i_verts[r][c + 1]))
            f[tag_layer] = 1

    for c in range(cols - 1):
        f_bot = bm.faces.new((o_verts[0][c], i_verts[0][c], i_verts[0][c + 1], o_verts[0][c + 1]))
        f_bot[tag_layer] = 2
        f_top = bm.faces.new((o_verts[rows - 1][c], o_verts[rows - 1][c + 1], i_verts[rows - 1][c + 1], i_verts[rows - 1][c]))
        f_top[tag_layer] = 2

    for r in range(rows - 1):
        f_c0 = bm.faces.new((o_verts[r][0], o_verts[r + 1][0], i_verts[r + 1][0], i_verts[r][0]))
        f_c0[tag_layer] = 2
        f_c1 = bm.faces.new((o_verts[r][cols - 1], i_verts[r][cols - 1], i_verts[r + 1][cols - 1], o_verts[r + 1][cols - 1]))
        f_c1[tag_layer] = 2

    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    zones = [f[tag_layer] for f in bm.faces]

    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()

    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    for idx, poly in enumerate(mesh.polygons):
        poly.use_smooth = bool(shade_smooth) and (zones[idx] != 2)
    mesh.validate()
    mesh.update()

    aa.paint_mesh(obj, color=outer_color, signal=signal, ao_strength=ao_strength, ground_ao=ground_ao)
    if not signal and inner_color is not None:
        inner_set = {idx for idx, z in enumerate(zones) if z == 1}
        aa.paint_faces(obj, lambda poly, _c, _n: poly.index in inner_set, color=inner_color, ao_strength=ao_strength * 0.85)
    if not signal and rim_color is not None:
        rim_set = {idx for idx, z in enumerate(zones) if z == 2}
        aa.paint_faces(obj, lambda poly, _c, _n: poly.index in rim_set, color=rim_color, ao_strength=ao_strength * 0.65)

    if bone:
        aa.pin_to_bone(obj, bone)
    if role:
        tag_skin_role(obj, role)
    return obj


def _ring_oval(
    cx: float,
    cy: float,
    cz: float,
    rx: float,
    ry_front: float,
    ry_back: float | None = None,
    segs: int = 12,
    squareness: float = 0.0,
) -> list[tuple[float, float, float]]:
    """Generate a horizontal closed ring with independent front (-Y) and back (+Y) radii."""
    ry_b = ry_front if ry_back is None else ry_back
    pts = []
    for i in range(segs):
        ang = (i / segs) * math.tau
        ca, sa = math.cos(ang), math.sin(ang)
        if squareness > 0.0:
            p = 1.0 - 0.42 * squareness
            ca = math.copysign(abs(ca) ** p, ca)
            sa = math.copysign(abs(sa) ** p, sa)
        ry = ry_front if sa < 0.0 else ry_b
        pts.append((cx + ca * rx, cy + sa * ry, cz))
    return pts


def _oriented_chain_loft(
    stations: Sequence[tuple[Vector, float, float]],
    name: str,
    segs: int = 10,
    color=(0.2, 0.25, 0.3),
    shade_smooth: bool = True,
    ao_strength: float = 0.25,
    bone: str | None = None,
    role: str | None = None,
) -> bpy.types.Object:
    """Loft a smooth limb/sleeve along 3D stations of (center_vec, radius_x, radius_y)."""
    rings = []
    n = len(stations)
    for idx, (center, rx, ry) in enumerate(stations):
        if idx == 0:
            tangent = (stations[1][0] - center).normalized()
        elif idx == n - 1:
            tangent = (center - stations[idx - 1][0]).normalized()
        else:
            tangent = (stations[idx + 1][0] - stations[idx - 1][0]).normalized()

        x_axis = Vector((1.0, 0.0, 0.0))
        y_axis = tangent.cross(x_axis)
        if y_axis.length_squared < 1e-6:
            y_axis = Vector((0.0, 1.0, 0.0))
        else:
            y_axis.normalize()
        x_ortho = y_axis.cross(tangent).normalized()

        ring = []
        for i in range(segs):
            ang = (i / segs) * math.tau
            p = center + x_ortho * (math.cos(ang) * rx) + y_axis * (math.sin(ang) * ry)
            ring.append((p.x, p.y, p.z))
        rings.append(ring)

    return build_lofted_tube(
        rings,
        name=name,
        color=color,
        shade_smooth=shade_smooth,
        ao_strength=ao_strength,
        bone=bone,
        role=role,
    )


def build_pelvis_saddle(
    pelvis_z: float,
    hip_z: float,
    hp_x: float,
    color,
    name: str,
) -> bpy.types.Object:
    """Sculpt an anatomical hip/groin saddle bridging torso into both thighs without gaps."""
    rings = [
        _ring_oval(0.0, 0.008, pelvis_z - 0.038, hp_x * 0.58, 0.068, 0.078, segs=12, squareness=0.20),
        _ring_oval(0.0, 0.006, pelvis_z + 0.005, hp_x + 0.062, 0.094, 0.106, segs=12, squareness=0.25),
        _ring_oval(0.0, 0.004, hip_z + 0.025,    hp_x + 0.056, 0.092, 0.102, segs=12, squareness=0.25),
    ]
    return build_lofted_tube(
        rings,
        name=name,
        color=color,
        ao_strength=0.28,
        role='pelvis_saddle',
    )


def build_stylized_head(
    head_z: float,
    crown_z: float,
    skin_color,
    scalp_color=None,
    include_ears: bool = True,
    name_prefix: str = 'Head',
) -> list[bpy.types.Object]:
    """Build a sculpted stylized AA cranium, chiseled jawline, nose bridge, and neck."""
    parts = []
    span = crown_z - head_z

    neck_rings = [
        _ring_oval(0.0, 0.008, head_z - 0.42 * span, 0.058, 0.054, 0.058, segs=10),
        _ring_oval(0.0, 0.004, head_z - 0.18 * span, 0.052, 0.050, 0.054, segs=10),
        _ring_oval(0.0, 0.002, head_z + 0.08 * span, 0.054, 0.052, 0.054, segs=10),
    ]
    parts.append(build_lofted_tube(
        neck_rings,
        name=f'{name_prefix}_Neck',
        color=skin_color,
        ao_strength=0.28,
        ground_ao=0.04,
        role='torso',
    ))

    skull_stations = [
        (0.000, -0.040, head_z - 0.03 * span, 0.035, 0.048, 0.036),
        (0.000, -0.018, head_z + 0.11 * span, 0.064, 0.084, 0.066),
        (0.000, -0.006, head_z + 0.28 * span, 0.075, 0.094, 0.082),
        (0.000, -0.002, head_z + 0.46 * span, 0.078, 0.097, 0.088),
        (0.000,  0.004, head_z + 0.66 * span, 0.076, 0.092, 0.090),
        (0.000,  0.008, head_z + 0.84 * span, 0.062, 0.072, 0.074),
        (0.000,  0.010, head_z + 0.95 * span, 0.032, 0.038, 0.040),
    ]
    skull_rings = [
        _ring_oval(cx, cy, cz, rx, ry_f, ry_b, segs=12, squareness=0.18)
        for (cx, cy, cz, rx, ry_f, ry_b) in skull_stations
    ]
    cranium = build_lofted_tube(
        skull_rings,
        name=f'{name_prefix}_Cranium',
        color=skin_color,
        ao_strength=0.22,
        ground_ao=0.02,
        bone='Head',
    )
    if scalp_color is not None:
        # Paint scalp/back-of-head under hair/hood so zero bare skin peeks out behind!
        aa.paint_faces(
            cranium,
            lambda _p, c, _n: (c.y > -0.015 and c.z > head_z + 0.16 * span) or c.z > head_z + 0.64 * span,
            color=scalp_color,
            ao_strength=0.20,
        )
    parts.append(cranium)

    nose_prof = [
        (-0.086, head_z + 0.24 * span),
        (-0.114, head_z + 0.27 * span),
        (-0.095, head_z + 0.45 * span),
        (-0.080, head_z + 0.45 * span),
    ]
    parts.append(aa.add_extruded_profile(
        nose_prof,
        depth=0.022,
        axis='X',
        name=f'{name_prefix}_Nose',
        bevel=0.003,
        bevel_segments=1,
        shade_smooth=False,
        color=skin_color,
        ao_strength=0.20,
        bone='Head',
    ))

    if include_ears:
        ear_L = _bevel_box(
            name=f'{name_prefix}_Ear_L',
            size=(0.018, 0.028, 0.042),
            location=(0.079, 0.004, head_z + 0.36 * span),
            rotation=(0.08, 0.0, -0.14),
            bevel=0.005,
            segments=1,
            color=skin_color,
            ao_strength=0.24,
            bone='Head',
        )
        parts.extend([ear_L, mirror_clean(ear_L)])

    return parts


def build_stylized_hand_L(
    wrist_pt: Vector,
    hand_tip_pt: Vector,
    glove_color,
    finger_color=None,
    name_prefix: str = 'Hand',
) -> list[bpy.types.Object]:
    """Build left articulated palm + finger block + angled thumb pinned to Hand_L."""
    f_col = glove_color if finger_color is None else finger_color
    axis = hand_tip_pt - wrist_pt
    mid_palm = wrist_pt + axis * 0.40
    knuckle = wrist_pt + axis * 0.66

    palm = _oriented_chain_loft(
        [
            (wrist_pt + axis * 0.02, 0.029, 0.023),
            (mid_palm, 0.035, 0.021),
            (knuckle, 0.034, 0.019),
            (hand_tip_pt, 0.026, 0.014),
        ],
        name=f'{name_prefix}_Palm_L',
        segs=8,
        color=glove_color,
        shade_smooth=False,
        ao_strength=0.24,
        bone='Hand_L',
    )
    if finger_color is not None:
        aa.paint_faces(
            palm,
            lambda _p, c, _n: c.z < knuckle.z,
            color=f_col,
            ao_strength=0.22,
        )

    thumb_start = wrist_pt + Vector((-0.010, -0.014, -0.016))
    thumb_end = wrist_pt + Vector((-0.022, -0.044, -0.066))
    thumb = aa.add_limb(
        thumb_start,
        thumb_end,
        radius0=0.012,
        radius1=0.009,
        segments=6,
        name=f'{name_prefix}_Thumb_L',
        color=f_col,
        ao_strength=0.22,
        bone='Hand_L',
    )
    return [palm, thumb]


# ---------------------------------------------------------------------------
# 1. Syndicate Hooded Data-Runner — public/street/courier.glb
# ---------------------------------------------------------------------------

def build_courier() -> Path:
    """Sculpt and rig the Syndicate Hooded Data-Runner (~1.79m) -> public/street/courier.glb."""
    aa.reset_scene()
    H = 1.79
    sh_x = 0.214
    hp_x = 0.108

    pelvis_z = 0.48 * H      # 0.859
    spine_z = 0.57 * H       # 1.020
    chest_z = 0.68 * H       # 1.217
    neck_z = 0.78 * H        # 1.396
    head_z = 0.84 * H        # 1.504
    crown_z = 1.00 * H       # 1.790
    sh_z = 0.755 * H         # 1.351
    elbow_L = Vector((sh_x + 0.036 * H, 0.015 * H, 0.618 * H))
    wrist_L = Vector((sh_x + 0.034 * H, -0.025 * H, 0.485 * H))
    hand_L = Vector((sh_x + 0.034 * H, -0.038 * H, 0.415 * H))
    hip_L = Vector((hp_x, 0.0, 0.505 * H))
    knee_L = Vector((hp_x + 0.012 * H, -0.012 * H, 0.285 * H))
    ankle_L = Vector((hp_x + 0.018 * H, 0.010 * H, 0.080 * H))

    body_parts: list[bpy.types.Object] = []
    sig_parts: list[bpy.types.Object] = []

    # --- A. Head, Face & Sleek Cyan/Magenta Cyber-Visor ---
    body_parts.extend(build_stylized_head(
        head_z=head_z,
        crown_z=crown_z - 0.045,
        skin_color=COURIER_COLORS['skin'],
        scalp_color=COURIER_COLORS['base_suit'],  # Stealth balaclava/under-hood cap
        include_ears=False,
        name_prefix='CourierHead',
    ))

    # Chin respirator / stealth comm-guard
    body_parts.append(_bevel_box(
        name='Courier_ChinGuard',
        size=(0.084, 0.048, 0.030),
        location=(0.0, -0.075, head_z + 0.010),
        rotation=(0.18, 0.0, 0.0),
        taper=(0.82, 0.85),
        bevel=0.005,
        segments=1,
        color=COURIER_COLORS['armor_dark'],
        ao_strength=0.24,
        bone='Head',
    ))

    # Wrap-around Cyber-Visor Optics ('Signal') + Titanium Brow Frame ('Body')
    visor_z = head_z + 0.112
    visor_cols = 7
    v_outer = []
    v_inner = []
    for r_i, (dz, thick_y) in enumerate(((-0.020, 0.0), (0.002, -0.006), (0.022, -0.002))):
        row_o = []
        row_i = []
        for c_i in range(visor_cols):
            u = (c_i / (visor_cols - 1)) * 2.0 - 1.0
            ang = u * 0.90
            v_dip = -0.008 * (1.0 - min(1.0, abs(u) * 2.2)) if r_i == 0 else 0.0
            xo = math.sin(ang) * 0.088
            yo = -math.cos(ang) * 0.110 + thick_y
            zo = visor_z + dz + v_dip + 0.008 * (u * u)
            row_o.append((xo, yo, zo))
            row_i.append((xo * 0.92, yo + 0.010, zo))
        v_outer.append(row_o)
        v_inner.append(row_i)

    sig_parts.append(build_thick_shell(
        v_outer,
        v_inner,
        name='Courier_CyberVisor_Signal',
        outer_color=COURIER_COLORS['visor_cyan'],
        signal=True,
        shade_smooth=True,
        bone='Head',
    ))

    body_parts.append(_bevel_box(
        name='Courier_VisorBrow',
        size=(0.172, 0.080, 0.013),
        location=(0.0, -0.075, visor_z + 0.025),
        rotation=(0.08, 0.0, 0.0),
        bevel=0.003,
        segments=1,
        color=COURIER_COLORS['armor_dark'],
        bone='Head',
    ))

    chev_L = _box(
        name='Courier_VisorChevron_L',
        size=(0.013, 0.032, 0.025),
        location=(0.075, -0.085, visor_z + 0.005),
        rotation=(0.10, -0.22, -0.52),
        color=COURIER_COLORS['visor_magenta'],
        signal=True,
        bone='Head',
    )
    sig_parts.extend([chev_L, mirror_clean(chev_L)])

    sig_parts.append(_box(
        name='Courier_VisorReticle',
        size=(0.028, 0.010, 0.007),
        location=(0.0, -0.117, visor_z + 0.003),
        color=COURIER_COLORS['visor_magenta'],
        signal=True,
        bone='Head',
    ))

    # --- B. Sculpted Angular Windbreaker Hood (Full-Enclosure Double-Walled Shell) ---
    # Generously wraps entire skull dome from forward angular brow peak to rear aero tail!
    hood_u_steps = 9
    hood_v_steps = 6
    hood_outer = []
    hood_inner = []
    for vi in range(hood_v_steps):
        vt = vi / (hood_v_steps - 1)  # 0 = front face-framing brow, 1 = rear hood tail tip
        tail_taper = _smoothstep(0.55, 1.0, vt)
        row_o = []
        row_i = []
        for ui in range(hood_u_steps):
            un = (ui / (hood_u_steps - 1)) * 2.0 - 1.0
            theta = un * _lerp(math.pi * 0.78, math.pi * 0.97, vt ** 0.8)
            abs_u = abs(un)

            crown_peak = (1.0 - min(1.0, abs_u * 1.45)) ** 1.35
            rx = _lerp(0.118, 0.022, tail_taper) * (1.0 + 0.06 * (1.0 - abs(abs_u - 0.55)))
            cz_center = _lerp(head_z + 0.105, head_z + 0.088, tail_taper)
            rz_up = _lerp(0.168, 0.052, tail_taper)

            x = math.sin(theta) * rx
            z = cz_center + math.cos(theta) * rz_up + crown_peak * _lerp(0.026, 0.008, vt)
            front_y = -0.054 - 0.092 * (max(0.0, math.cos(theta * 0.85)) ** 1.5)
            back_y = 0.168 - 0.014 * abs_u
            y = _lerp(front_y, back_y, vt ** 0.85)

            thick = _lerp(0.015, 0.006, tail_taper)
            inward = Vector((-math.sin(theta) * thick, 0.012 if vt < 0.30 else -thick, -math.cos(theta) * thick))
            row_o.append((x, y, z))
            row_i.append((x + inward.x, y + inward.y, z + inward.z))
        hood_outer.append(row_o)
        hood_inner.append(row_i)

    hood_shell = build_thick_shell(
        hood_outer,
        hood_inner,
        name='Courier_AngularHood',
        outer_color=COURIER_COLORS['jacket_main'],
        inner_color=COURIER_COLORS['hood_lining'],
        rim_color=COURIER_COLORS['jacket_panel'],
        shade_smooth=False,
        ao_strength=0.28,
        ground_ao=0.04,
        bone='Head',
    )
    # Contrast ice-silver racing brow chevron across top-front of hood
    aa.paint_faces(
        hood_shell,
        lambda _p, c, n: c.z > head_z + 0.22 and c.y < 0.02 and n.z > 0.2,
        color=COURIER_COLORS['jacket_panel'],
        ao_strength=0.18,
    )
    body_parts.append(hood_shell)

    # Rear aerodynamic hood tail cone closing the back cowl cleanly
    body_parts.append(aa.add_cone(
        name='Courier_HoodTailCone',
        radius1=0.056,
        radius2=0.008,
        depth=0.055,
        vertices=8,
        location=(0.0, 0.158, head_z + 0.092),
        rotation=(-math.pi * 0.46, 0.0, 0.0),
        shade_smooth=False,
        color=COURIER_COLORS['jacket_main'],
        ao_strength=0.26,
        bone='Head',
    ))

    # Central aero ridge fin along top of hood (reaches exact target height ~1.795m)
    hood_fin_prof = [
        (-0.138, head_z + 0.280),
        (-0.035, crown_z + 0.004),
        ( 0.115, head_z + 0.238),
        ( 0.090, head_z + 0.215),
        (-0.115, head_z + 0.264),
    ]
    body_parts.append(aa.add_extruded_profile(
        hood_fin_prof,
        depth=0.020,
        axis='X',
        name='Courier_HoodCrest',
        bevel=0.003,
        bevel_segments=1,
        color=COURIER_COLORS['jacket_panel'],
        ao_strength=0.20,
        bone='Head',
    ))

    # --- C. Pelvis Saddle, Aerodynamic Runner Jacket & Spine Power-Strip ---
    body_parts.append(build_pelvis_saddle(
        pelvis_z=pelvis_z,
        hip_z=hip_L.z,
        hp_x=hp_x,
        color=COURIER_COLORS['base_suit'],
        name='Courier_PelvisSaddle',
    ))

    # Lower waist & runner tech belt
    waist_rings = [
        _ring_oval(0.0, 0.004, pelvis_z + 0.030, 0.146, 0.098, 0.106, segs=12, squareness=0.28),
        _ring_oval(0.0, 0.002, spine_z - 0.015,  0.138, 0.092, 0.100, segs=12, squareness=0.28),
    ]
    body_parts.append(build_lofted_tube(
        waist_rings,
        name='Courier_TechBelt',
        color=COURIER_COLORS['armor_dark'],
        shade_smooth=False,
        ao_strength=0.26,
        role='torso',
    ))
    body_parts.append(_bevel_box(
        name='Courier_BeltBuckle',
        size=(0.058, 0.020, 0.038),
        location=(0.0, -0.100, pelvis_z + 0.062),
        bevel=0.004,
        segments=1,
        color=COURIER_COLORS['armor_plate'],
        role='torso',
    ))

    # Aerodynamic cropped windbreaker jacket with crisp V-taper & storm collar
    jacket_stations = [
        (0.0,  0.003, spine_z - 0.020, 0.146, 0.100, 0.106),  # Snug elastic hem
        (0.0,  0.002, spine_z + 0.060, 0.156, 0.110, 0.114),  # Ribcage taper
        (0.0, -0.004, chest_z - 0.015, 0.180, 0.126, 0.124),  # Mid chest
        (0.0, -0.005, chest_z + 0.085, 0.204, 0.132, 0.128),  # Broad athletic chest
        (0.0,  0.002, sh_z + 0.022,    0.188, 0.114, 0.116),  # Sloped shoulder line
        (0.0,  0.004, neck_z + 0.022,  0.106, 0.094, 0.096),  # Storm collar base
        (0.0, -0.002, neck_z + 0.056,  0.102, 0.098, 0.092),  # Storm collar rim
    ]
    jacket_rings = [
        _ring_oval(cx, cy, cz, rx, ry_f, ry_b, segs=16, squareness=0.26)
        for (cx, cy, cz, rx, ry_f, ry_b) in jacket_stations
    ]
    jacket = build_lofted_tube(
        jacket_rings,
        name='Courier_AeroJacket',
        color=COURIER_COLORS['jacket_main'],
        ao_strength=0.26,
        ground_ao=0.08,
        role='torso',
    )
    aa.paint_faces(
        jacket,
        lambda _p, c, _n: c.z > chest_z + 0.042 and c.y < 0.080,
        color=COURIER_COLORS['jacket_panel'],
        ao_strength=0.20,
    )
    aa.paint_faces(
        jacket,
        lambda _p, c, _n: c.z < spine_z + 0.025,
        color=COURIER_COLORS['jacket_teal'],
        ao_strength=0.22,
    )
    body_parts.append(jacket)

    # Flush front storm-zipper seam & collar V-guard
    body_parts.append(_bevel_box(
        name='Courier_ZipperPlacket',
        size=(0.026, 0.014, 0.360),
        location=(0.018, -0.124, chest_z + 0.015),
        rotation=(0.05, 0.0, 0.0),
        bevel=0.003,
        segments=1,
        color=COURIER_COLORS['jacket_teal'],
        role='torso',
    ))

    # Sloped aerodynamic deltoid shoulder pads hugging shoulder-to-sleeve curve
    sh_cowl_L = _bevel_box(
        name='Courier_ShoulderCowl_L',
        size=(0.098, 0.146, 0.044),
        location=(sh_x - 0.004, 0.002, sh_z + 0.014),
        rotation=(0.0, 0.42, 0.0),  # Natural 24-deg downward deltoid slope!
        taper=(0.76, 0.84),
        bevel=0.007,
        segments=1,
        color=COURIER_COLORS['jacket_panel'],
        role='arm',
    )
    body_parts.extend([sh_cowl_L, mirror_clean(sh_cowl_L)])

    # Dorsal Spine Exoshell Housing ('Body') + Glowing Spine Power-Strip ('Signal')
    body_parts.append(_bevel_box(
        name='Courier_SpineHousing',
        size=(0.064, 0.024, 0.385),
        location=(0.0, 0.124, chest_z - 0.018),
        rotation=(-0.05, 0.0, 0.0),
        taper=(0.86, 0.90),
        bevel=0.005,
        segments=1,
        color=COURIER_COLORS['armor_dark'],
        role='torso',
    ))

    sig_parts.append(_box(
        name='Courier_SpinePowerRail',
        size=(0.020, 0.012, 0.340),
        location=(0.0, 0.138, chest_z - 0.018),
        rotation=(-0.05, 0.0, 0.0),
        color=COURIER_COLORS['visor_cyan'],
        signal=True,
        role='torso',
    ))

    for idx in range(5):
        frac = idx / 4.0
        nz = _lerp(spine_z + 0.030, chest_z + 0.125, frac)
        ny = _lerp(0.130, 0.145, frac)
        cell_col = COURIER_COLORS['visor_magenta'] if idx in (0, 4) else COURIER_COLORS['visor_cyan']
        sig_parts.append(_box(
            name=f'Courier_SpineCell_{idx}',
            size=(0.048, 0.012, 0.020),
            location=(0.0, ny, nz),
            rotation=(-0.05, 0.0, 0.0),
            color=cell_col,
            signal=True,
            role='torso',
        ))

    # --- D. Strapped Cross-Body Courier Cyber-Deck Satchel ---
    strap_waypoints = [
        ( 0.136, -0.018, sh_z + 0.036),
        ( 0.088, -0.122, chest_z + 0.080),
        (-0.015, -0.136, chest_z - 0.040),
        (-0.115, -0.114, spine_z + 0.038),
        (-0.160, -0.022, pelvis_z + 0.098),
        (-0.166,  0.076, pelvis_z + 0.088),
        (-0.095,  0.128, spine_z + 0.048),
        ( 0.015,  0.144, chest_z - 0.020),
        ( 0.104,  0.122, chest_z + 0.084),
    ]
    strap_rings = []
    n_sw = len(strap_waypoints)
    for k, pt in enumerate(strap_waypoints):
        c_vec = Vector(pt)
        p_prev = Vector(strap_waypoints[(k - 1) % n_sw])
        p_next = Vector(strap_waypoints[(k + 1) % n_sw])
        tan = (p_next - p_prev).normalized()
        out_dir = Vector((c_vec.x * 0.45, c_vec.y, 0.12)).normalized()
        side_dir = tan.cross(out_dir).normalized()
        out_ortho = side_dir.cross(tan).normalized()
        hw, ht = 0.022, 0.007
        strap_rings.append([
            tuple(c_vec - side_dir * hw - out_ortho * ht),
            tuple(c_vec + side_dir * hw - out_ortho * ht),
            tuple(c_vec + side_dir * hw + out_ortho * ht),
            tuple(c_vec - side_dir * hw + out_ortho * ht),
        ])
    strap_rings.append(strap_rings[0])
    body_parts.append(build_lofted_tube(
        strap_rings,
        name='Courier_SatchelStrap',
        color=COURIER_COLORS['strap_web'],
        cap_bottom=False,
        cap_top=False,
        shade_smooth=False,
        ao_strength=0.24,
        role='torso',
    ))

    body_parts.append(_bevel_box(
        name='Courier_StrapBuckle',
        size=(0.042, 0.020, 0.048),
        location=(0.036, -0.138, chest_z + 0.022),
        rotation=(0.08, 0.0, -0.58),
        bevel=0.004,
        segments=1,
        color=COURIER_COLORS['armor_plate'],
        role='torso',
    ))

    # Armored Cyber-Deck Courier Satchel resting at Right Hip / Back (-X, +Y)
    deck_loc = (-0.166, 0.084, pelvis_z + 0.095)
    deck_rot = (0.10, 0.14, 0.28)
    body_parts.append(_bevel_box(
        name='Courier_CyberDeckCase',
        size=(0.090, 0.192, 0.152),
        location=deck_loc,
        rotation=deck_rot,
        taper=(0.90, 0.92),
        bevel=0.012,
        segments=1,
        color=COURIER_COLORS['deck_case'],
        ao_strength=0.26,
        bone='Pelvis',
    ))
    body_parts.append(_bevel_box(
        name='Courier_CyberDeckFlap',
        size=(0.096, 0.178, 0.050),
        location=(-0.170, 0.086, pelvis_z + 0.154),
        rotation=deck_rot,
        bevel=0.008,
        segments=1,
        color=COURIER_COLORS['jacket_panel'],
        ao_strength=0.22,
        bone='Pelvis',
    ))
    body_parts.append(aa.add_cylinder(
        name='Courier_DeckAntenna',
        radius=0.006,
        radius_top=0.003,
        depth=0.092,
        vertices=6,
        location=(-0.174, 0.146, pelvis_z + 0.208),
        rotation=(-0.16, -0.12, 0.0),
        shade_smooth=False,
        color=COURIER_COLORS['armor_plate'],
        bone='Pelvis',
    ))
    sig_parts.append(_box(
        name='Courier_DeckDataBar',
        size=(0.012, 0.126, 0.018),
        location=(-0.212, 0.092, pelvis_z + 0.096),
        rotation=deck_rot,
        color=COURIER_COLORS['visor_cyan'],
        signal=True,
        bone='Pelvis',
    ))
    sig_parts.append(_box(
        name='Courier_DeckBeaconPip',
        size=(0.014, 0.024, 0.018),
        location=(-0.204, 0.024, pelvis_z + 0.126),
        rotation=deck_rot,
        color=COURIER_COLORS['visor_magenta'],
        signal=True,
        bone='Pelvis',
    ))

    # --- E. Tapered Runner Sleeves, Flush Forearm Gauntlets & Hands (Mirrored Cleanly) ---
    sh_pt_L = Vector((sh_x - 0.012, 0.002, sh_z + 0.005))
    sleeve_L = _oriented_chain_loft(
        [
            (sh_pt_L, 0.056, 0.058),
            (_vlerp(sh_pt_L, elbow_L, 0.52), 0.048, 0.050),
            (elbow_L, 0.044, 0.046),
            (_vlerp(elbow_L, wrist_L, 0.55), 0.042, 0.044),
            (wrist_L, 0.036, 0.037),
        ],
        name='Courier_Sleeve_L',
        segs=10,
        color=COURIER_COLORS['jacket_main'],
        ao_strength=0.25,
        role='arm',
    )
    aa.paint_faces(
        sleeve_L,
        lambda _p, c, _n: c.z < elbow_L.z - 0.075,
        color=COURIER_COLORS['armor_dark'],
        ao_strength=0.22,
    )
    body_parts.extend([sleeve_L, mirror_clean(sleeve_L)])

    # Flush-aligned forearm gauntlet plate using positive Z -> forearm bone direction!
    fore_dir_L = (wrist_L - elbow_L).normalized()
    fore_rot_L = Vector((0.0, 0.0, 1.0)).rotation_difference(fore_dir_L)
    gauntlet_L = _bevel_box(
        name='Courier_Gauntlet_L',
        size=(0.024, 0.054, 0.135),
        location=_vlerp(elbow_L, wrist_L, 0.54) + Vector((0.034, -0.004, 0.0)),
        rotation=fore_rot_L,
        taper=(0.86, 0.82),
        bevel=0.005,
        segments=1,
        color=COURIER_COLORS['armor_plate'],
        bone='Forearm_L',
    )
    body_parts.extend([gauntlet_L, mirror_clean(gauntlet_L)])

    wrist_sig_L = _box(
        name='Courier_WristSignal_L',
        size=(0.010, 0.038, 0.020),
        location=_vlerp(elbow_L, wrist_L, 0.78) + Vector((0.042, -0.005, 0.0)),
        rotation=fore_rot_L,
        color=COURIER_COLORS['visor_cyan'],
        signal=True,
        bone='Forearm_L',
    )
    sig_parts.extend([wrist_sig_L, mirror_clean(wrist_sig_L)])

    hands_L = build_stylized_hand_L(
        wrist_L,
        hand_L,
        glove_color=COURIER_COLORS['armor_dark'],
        finger_color=COURIER_COLORS['skin'],
        name_prefix='Courier',
    )
    body_parts.extend(hands_L)
    body_parts.extend(mirror_clean(hands_L))

    # --- F. Runner Legs, Sculpted Athletic Shin Guards & Sprint Shoes ---
    leg_L = _oriented_chain_loft(
        [
            (hip_L + Vector((0.0, 0.0, 0.028)), 0.075, 0.082),
            (_vlerp(hip_L, knee_L, 0.52), 0.065, 0.070),
            (knee_L, 0.052, 0.056),
            (_vlerp(knee_L, ankle_L, 0.45), 0.050, 0.054),
            (ankle_L - Vector((0.0, 0.0, 0.042)), 0.033, 0.036),
        ],
        name='Courier_Leg_L',
        segs=10,
        color=COURIER_COLORS['base_suit'],
        ao_strength=0.26,
        role='leg',
    )
    body_parts.extend([leg_L, mirror_clean(leg_L)])

    knee_cap_L = _bevel_box(
        name='Courier_KneePad_L',
        size=(0.074, 0.040, 0.084),
        location=knee_L + Vector((0.004, -0.044, 0.005)),
        rotation=(0.12, 0.0, 0.0),
        taper=(0.82, 0.78),
        bevel=0.007,
        segments=1,
        color=COURIER_COLORS['armor_dark'],
        bone='Shin_L',
    )
    body_parts.extend([knee_cap_L, mirror_clean(knee_cap_L)])

    # Contoured Athletic Hard-Shell Shin Guards
    shin_axis = ankle_L - knee_L
    sg_rows = 5
    sg_cols = 5
    sg_outer = []
    sg_inner = []
    for ri in range(sg_rows):
        rt = 0.16 + 0.64 * (ri / (sg_rows - 1))
        c_pt = knee_L + shin_axis * rt
        rad_x = _lerp(0.055, 0.043, ri / (sg_rows - 1))
        rad_y = _lerp(0.059, 0.047, ri / (sg_rows - 1))
        row_o = []
        row_i = []
        for ci in range(sg_cols):
            ang = -math.pi * 0.5 + ((ci / (sg_cols - 1)) - 0.42) * (math.pi * 0.82)
            ca, sa = math.cos(ang), math.sin(ang)
            ridge = 0.008 if ci == 2 else 0.0
            row_o.append((c_pt.x + ca * (rad_x + 0.010), c_pt.y + sa * (rad_y + 0.010 + ridge), c_pt.z))
            row_i.append((c_pt.x + ca * rad_x, c_pt.y + sa * rad_y, c_pt.z))
        sg_outer.append(row_o)
        sg_inner.append(row_i)

    shin_guard_L = build_thick_shell(
        sg_outer,
        sg_inner,
        name='Courier_ShinGuard_L',
        outer_color=COURIER_COLORS['armor_plate'],
        inner_color=COURIER_COLORS['armor_dark'],
        rim_color=COURIER_COLORS['jacket_teal'],
        shade_smooth=False,
        ao_strength=0.22,
        bone='Shin_L',
    )
    body_parts.extend([shin_guard_L, mirror_clean(shin_guard_L)])

    for s_idx, s_t in enumerate((0.30, 0.64)):
        sp = knee_L + shin_axis * s_t
        strap_calf_L = aa.add_cylinder(
            name=f'Courier_ShinStrap_{s_idx}_L',
            radius=_lerp(0.054, 0.044, s_t),
            depth=0.020,
            vertices=10,
            location=sp,
            shade_smooth=False,
            color=COURIER_COLORS['armor_dark'],
            bone='Shin_L',
        )
        body_parts.extend([strap_calf_L, mirror_clean(strap_calf_L)])

    shin_pip_L = _box(
        name='Courier_ShinSignal_L',
        size=(0.010, 0.016, 0.092),
        location=knee_L + shin_axis * 0.46 + Vector((0.052, -0.020, 0.0)),
        color=COURIER_COLORS['visor_cyan'],
        signal=True,
        bone='Shin_L',
    )
    sig_parts.extend([shin_pip_L, mirror_clean(shin_pip_L)])

    # Sleek Low-Slung Cyberpunk Sprint Shoes (flat sole on z = 0.000m, sculpted arch & toe spring)
    ax_x = ankle_L.x
    shoe_rings_L = [
        [
            (ax_x - 0.042,  0.068, 0.000),
            (ax_x + 0.044,  0.068, 0.000),
            (ax_x + 0.046, -0.025, 0.004),  # Sculpted midfoot split-sole arch
            (ax_x + 0.048, -0.115, 0.000),
            (ax_x + 0.032, -0.220, 0.008),  # Sprinter toe-spring!
            (ax_x - 0.030, -0.220, 0.008),
            (ax_x - 0.046, -0.115, 0.000),
            (ax_x - 0.044, -0.025, 0.004),
        ],
        [
            (ax_x - 0.046,  0.076, 0.028),
            (ax_x + 0.048,  0.076, 0.028),
            (ax_x + 0.048, -0.025, 0.024),
            (ax_x + 0.050, -0.118, 0.024),
            (ax_x + 0.034, -0.228, 0.024),
            (ax_x - 0.032, -0.228, 0.024),
            (ax_x - 0.048, -0.118, 0.024),
            (ax_x - 0.046, -0.025, 0.024),
        ],
        [
            (ax_x - 0.041,  0.064, 0.062),
            (ax_x + 0.043,  0.064, 0.062),
            (ax_x + 0.043, -0.022, 0.064),
            (ax_x + 0.042, -0.092, 0.054),
            (ax_x + 0.027, -0.198, 0.036),
            (ax_x - 0.025, -0.198, 0.036),
            (ax_x - 0.040, -0.092, 0.054),
            (ax_x - 0.041, -0.022, 0.064),
        ],
        [
            (ax_x - 0.037,  0.052, 0.116),
            (ax_x + 0.039,  0.052, 0.116),
            (ax_x + 0.039,  0.010, 0.110),
            (ax_x + 0.034, -0.028, 0.104),
            (ax_x + 0.022, -0.038, 0.100),
            (ax_x - 0.022, -0.038, 0.100),
            (ax_x - 0.034, -0.028, 0.104),
            (ax_x - 0.037,  0.010, 0.110),
        ],
    ]
    shoe_L = build_lofted_tube(
        shoe_rings_L,
        name='Courier_SprintShoe_L',
        color=COURIER_COLORS['shoe_upper'],
        shade_smooth=False,
        ao_strength=0.24,
        ground_ao=0.14,
        role='foot',
    )
    aa.paint_faces(
        shoe_L,
        lambda _p, c, _n: c.z < 0.030,
        color=COURIER_COLORS['shoe_sole'],
        ao_strength=0.16,
    )
    aa.paint_faces(
        shoe_L,
        lambda _p, c, _n: c.y > 0.036 and c.z > 0.028,
        color=COURIER_COLORS['shoe_heel'],
        ao_strength=0.18,
    )
    aa.paint_faces(
        shoe_L,
        lambda _p, c, n: c.y < -0.045 and c.z > 0.038 and abs(c.x - ax_x) < 0.024 and n.z > 0.25,
        color=COURIER_COLORS['jacket_panel'],  # Aero tongue/lace shield!
        ao_strength=0.18,
    )
    body_parts.extend([shoe_L, mirror_clean(shoe_L)])

    shoe_sig_L = _box(
        name='Courier_ShoeSignal_L',
        size=(0.008, 0.062, 0.011),
        location=(ax_x + 0.048, 0.022, 0.025),
        color=COURIER_COLORS['visor_cyan'],
        signal=True,
        role='foot',
    )
    sig_parts.extend([shoe_sig_L, mirror_clean(shoe_sig_L)])

    # --- Batch into 'Body' + 'Signal', Rig 21 Bones & Keyframe 6 Clips ---
    merged = aa.combine_by_role(body_parts, sig_parts)
    role_snap = snapshot_skin_roles(merged)
    rig = aa.build_humanoid_rig_and_clips(
        merged,
        rig_name='CourierRig',
        height=H,
        style='punk',
        attack_clip_name='Punch',
        shoulder_width=sh_x,
        hip_width=hp_x,
    )
    apply_specialist_skinning(rig, merged, role_snap, height=H)

    out_path = OUT_DIR / 'courier.glb'
    info = aa.export_glb(out_path, [rig, *merged], min_tris=1201, max_meshes=6)
    print(f"[build_aa_specialists] Exported {info['path']}: {info['triangles']} tris, {info['meshes']} meshes, {info['size_kb']} KB")
    return out_path


# ---------------------------------------------------------------------------
# 2. Aethelgard Corporate Executive — public/street/executive.glb
# ---------------------------------------------------------------------------

def build_executive() -> Path:
    """Sculpt and rig the Aethelgard Corporate Executive (~1.82m) -> public/street/executive.glb."""
    aa.reset_scene()
    H = 1.82
    sh_x = 0.224
    hp_x = 0.110

    pelvis_z = 0.48 * H      # 0.874
    spine_z = 0.57 * H       # 1.037
    chest_z = 0.68 * H       # 1.238
    neck_z = 0.78 * H        # 1.420
    head_z = 0.84 * H        # 1.529
    crown_z = 1.00 * H       # 1.820
    sh_z = 0.755 * H         # 1.374
    elbow_L = Vector((sh_x + 0.036 * H, 0.015 * H, 0.618 * H))
    wrist_L = Vector((sh_x + 0.034 * H, -0.025 * H, 0.485 * H))
    hand_L = Vector((sh_x + 0.034 * H, -0.038 * H, 0.415 * H))
    hip_L = Vector((hp_x, 0.0, 0.505 * H))
    knee_L = Vector((hp_x + 0.012 * H, -0.012 * H, 0.285 * H))
    ankle_L = Vector((hp_x + 0.018 * H, 0.010 * H, 0.080 * H))

    body_parts: list[bpy.types.Object] = []
    sig_parts: list[bpy.types.Object] = []

    # --- A. Chiseled Head, Full-Coverage Slicked Hair & Sleek Cyber-Optic Glasses ---
    body_parts.extend(build_stylized_head(
        head_z=head_z,
        crown_z=crown_z - 0.036,
        skin_color=EXEC_COLORS['skin'],
        scalp_color=EXEC_COLORS['hair_dark'],  # Full dark hairline/nape under sculpted locks!
        include_ears=True,
        name_prefix='ExecHead',
    ))

    port_L = _box(
        name='Exec_TemplePort_L',
        size=(0.008, 0.026, 0.012),
        location=(0.080, -0.042, head_z + 0.140),
        rotation=(0.0, 0.0, -0.18),
        color=EXEC_COLORS['gold_trim'],
        bone='Head',
    )
    body_parts.extend([port_L, mirror_clean(port_L)])

    # Sculpted Swept-Back Slicked Hair (hugs forehead hairline & entire upper/back cranium cleanly!)
    hair_u = 9
    hair_v = 6
    hair_outer = []
    hair_inner = []
    for vi in range(hair_v):
        vt = vi / (hair_v - 1)  # 0 = swept front hairline -> 1 = tapered nape at back of neck
        row_o = []
        row_i = []
        for ui in range(hair_u):
            un = (ui / (hair_u - 1)) * 2.0 - 1.0
            theta = un * _lerp(math.pi * 0.56, math.pi * 0.74, vt)
            abs_u = abs(un)

            # Aristocratic swept-back pompadour volume sitting flush on the hairline
            part_crest = 0.016 * math.exp(-((un + 0.22) * 2.8) ** 2) * (1.0 - 0.50 * vt)
            rx = _lerp(0.082, 0.066, vt ** 1.6)
            dome_arc = math.sin(math.pi * min(1.0, vt * 0.76 + 0.24))
            cz_base = _lerp(head_z + 0.136, head_z + 0.048, vt ** 1.35)
            rz_up = 0.092 + 0.038 * dome_arc

            x = math.sin(theta) * rx
            z = cz_base + math.cos(theta) * rz_up + part_crest
            front_y = -0.094 + 0.054 * (abs_u ** 1.25)
            back_y = 0.104 - 0.018 * abs_u
            y = _lerp(front_y, back_y, vt ** 0.78)

            inward = Vector((-math.sin(theta) * 0.016, 0.016 if vt < 0.35 else -0.012, -math.cos(theta) * 0.020))
            row_o.append((x, y, z))
            row_i.append((x + inward.x, y + inward.y, z + inward.z))
        hair_outer.append(row_o)
        hair_inner.append(row_i)

    slick_hair = build_thick_shell(
        hair_outer,
        hair_inner,
        name='Exec_SlickedHair',
        outer_color=EXEC_COLORS['hair_dark'],
        inner_color=EXEC_COLORS['hair_dark'],
        rim_color=EXEC_COLORS['hair_dark'],
        shade_smooth=True,
        ao_strength=0.22,
        ground_ao=0.02,
        bone='Head',
    )
    aa.paint_faces(
        slick_hair,
        lambda _p, c, _n: abs(c.x) > 0.064 and c.z < head_z + 0.195,
        color=EXEC_COLORS['hair_silver'],
        ao_strength=0.18,
    )
    body_parts.append(slick_hair)

    # Sleek Cyber-Optic Glasses: Gold Bridge & Temple Arms ('Body') + Glowing Amber Lenses ('Signal')
    optic_z = head_z + 0.116
    body_parts.append(_bevel_box(
        name='Exec_OpticBrowBridge',
        size=(0.154, 0.012, 0.007),
        location=(0.0, -0.103, optic_z + 0.013),
        bevel=0.002,
        segments=1,
        color=EXEC_COLORS['gold_trim'],
        bone='Head',
    ))

    temple_arm_L = _box(
        name='Exec_OpticTemple_L',
        size=(0.006, 0.104, 0.006),
        location=(0.078, -0.048, optic_z + 0.011),
        rotation=(0.04, 0.0, 0.05),
        color=EXEC_COLORS['gold_trim'],
        bone='Head',
    )
    body_parts.extend([temple_arm_L, mirror_clean(temple_arm_L)])

    lens_prof_L = [
        (0.012, optic_z + 0.011),
        (0.073, optic_z + 0.013),
        (0.077, optic_z - 0.004),
        (0.061, optic_z - 0.014),
        (0.017, optic_z - 0.011),
    ]
    lens_L = aa.add_extruded_profile(
        lens_prof_L,
        depth=0.008,
        axis='Y',
        name='Exec_CyberOpticLens_L',
        location=(0.0, -0.105, 0.0),
        rotation=(0.0, 0.0, -0.10),
        bevel=0.0015,
        bevel_segments=1,
        color=EXEC_COLORS['sig_solar'],
        signal=True,
        bone='Head',
    )
    sig_parts.extend([lens_L, mirror_clean(lens_L)])

    # --- B. Tailored Neo-Kyoto Inner Waistcoat, High Stand-Collar & Seamless Longcoat ---
    body_parts.append(build_pelvis_saddle(
        pelvis_z=pelvis_z,
        hip_z=hip_L.z,
        hp_x=hp_x,
        color=EXEC_COLORS['trouser'],
        name='Exec_TrouserSaddle',
    ))

    inner_stations = [
        (0.0,  0.004, pelvis_z + 0.010, 0.132, 0.088, 0.095),
        (0.0,  0.002, spine_z + 0.025,  0.128, 0.092, 0.094),
        (0.0, -0.004, chest_z + 0.015,  0.154, 0.110, 0.106),
        (0.0, -0.002, sh_z + 0.010,     0.140, 0.094, 0.096),
        (0.0,  0.002, neck_z + 0.025,   0.074, 0.070, 0.072),
    ]
    inner_rings = [
        _ring_oval(cx, cy, cz, rx, ry_f, ry_b, segs=10, squareness=0.22)
        for (cx, cy, cz, rx, ry_f, ry_b) in inner_stations
    ]
    waistcoat = build_lofted_tube(
        inner_rings,
        name='Exec_InnerWaistcoat',
        color=EXEC_COLORS['waistcoat'],
        ao_strength=0.26,
        role='torso',
    )
    aa.paint_faces(
        waistcoat,
        lambda _p, c, _n: c.z > chest_z - 0.020 and c.y < -0.065 and abs(c.x) < 0.068,
        color=EXEC_COLORS['shirt_silk'],
        ao_strength=0.16,
    )
    body_parts.append(waistcoat)

    # Gold sternum tie-pin & tailored Neo-Kyoto waist sash/belt unifying upper coat & split skirt!
    body_parts.append(_box(
        name='Exec_SternumPin',
        size=(0.044, 0.012, 0.012),
        location=(0.0, -0.112, chest_z + 0.082),
        color=EXEC_COLORS['gold_trim'],
        role='torso',
    ))

    waist_seam_z = pelvis_z + 0.068
    sash_rings = [
        _ring_oval(0.0, 0.002, waist_seam_z - 0.022, 0.160, 0.110, 0.116, segs=12, squareness=0.28),
        _ring_oval(0.0, 0.002, waist_seam_z + 0.022, 0.158, 0.108, 0.114, segs=12, squareness=0.28),
    ]
    waist_sash = build_lofted_tube(
        sash_rings,
        name='Exec_WaistSash',
        color=EXEC_COLORS['coat_main'],
        shade_smooth=False,
        ao_strength=0.24,
        role='torso',
    )
    body_parts.append(waist_sash)
    body_parts.append(_bevel_box(
        name='Exec_WaistBuckle',
        size=(0.058, 0.022, 0.036),
        location=(0.0, -0.108, waist_seam_z),
        bevel=0.004,
        segments=1,
        color=EXEC_COLORS['gold_trim'],
        role='torso',
    ))

    # Tailored Neo-Kyoto Upper Longcoat with sleek V-lapel opening (narrows cleanly to waist clasp!)
    coat_u = 11
    coat_z_levels = [
        # (z, rx, ry_front, ry_back, wrap_frac: 0.92 at waist clasp -> 0.72 open V at upper chest!)
        (waist_seam_z,     0.156, 0.106, 0.112, 0.92),  # Meets waist clasp & split skirt seamlessly!
        (spine_z + 0.048,  0.156, 0.108, 0.114, 0.88),
        (chest_z - 0.005,  0.180, 0.124, 0.122, 0.81),
        (chest_z + 0.095,  0.208, 0.132, 0.126, 0.74),  # Sleek V-neck chest opening
        (sh_z + 0.030,     0.194, 0.112, 0.116, 0.71),  # Structured shoulder yoke
    ]
    uc_outer = []
    uc_inner = []
    for zl, rx, ry_f, ry_b, wrap_frac in coat_z_levels:
        row_o = []
        row_i = []
        for ui in range(coat_u):
            un = (ui / (coat_u - 1)) * 2.0 - 1.0
            theta = un * (math.pi * wrap_frac)
            sx = math.sin(theta)
            cy_dir = math.cos(theta)
            ry = ry_b if cy_dir >= 0.0 else ry_f
            xo = sx * rx
            yo = cy_dir * ry
            row_o.append((xo, yo, zl))
            row_i.append((xo - sx * 0.013, yo - cy_dir * 0.013, zl))
        uc_outer.append(row_o)
        uc_inner.append(row_i)

    upper_coat = build_thick_shell(
        uc_outer,
        uc_inner,
        name='Exec_TailoredUpperCoat',
        outer_color=EXEC_COLORS['coat_main'],
        inner_color=EXEC_COLORS['coat_lining'],
        rim_color=EXEC_COLORS['coat_lapel'],
        shade_smooth=True,
        ao_strength=0.25,
        ground_ao=0.06,
        role='torso',
    )
    # Paint sleek champagne-ivory front lapel borders right on the outer coat's V-opening!
    aa.paint_faces(
        upper_coat,
        lambda _p, c, n: c.y < -0.075 and abs(c.x) < 0.135 and n.y < -0.25,
        color=EXEC_COLORS['coat_lapel'],
        ao_strength=0.18,
    )
    body_parts.append(upper_coat)

    # Sloped tailored shoulder epaulettes (follow natural deltoid slope cleanly!)
    epaulette_L = _bevel_box(
        name='Exec_Epaulette_L',
        size=(0.104, 0.126, 0.022),
        location=(sh_x - 0.008, 0.002, sh_z + 0.024),
        rotation=(0.0, 0.30, 0.0),
        taper=(0.82, 0.88),
        bevel=0.004,
        segments=1,
        color=EXEC_COLORS['coat_lapel'],
        role='arm',
    )
    body_parts.extend([epaulette_L, mirror_clean(epaulette_L)])

    # Sharp High-Collared Neo-Kyoto Origami Stand-Collar
    col_u = 9
    col_v = 4
    col_outer = []
    col_inner = []
    for vi in range(col_v):
        vt = vi / (col_v - 1)
        zl = _lerp(sh_z + 0.024, head_z + 0.038, vt)
        rx = _lerp(0.116, 0.138, vt ** 1.15)
        ry = _lerp(0.102, 0.124, vt ** 1.15)
        wrap = _lerp(0.75, 0.67, vt)
        row_o = []
        row_i = []
        for ui in range(col_u):
            un = (ui / (col_u - 1)) * 2.0 - 1.0
            theta = un * (math.pi * wrap)
            sx, cy_d = math.sin(theta), math.cos(theta)
            tip_lift = 0.018 * vt * (abs(un) ** 2.0)
            xo = sx * rx
            yo = cy_d * ry
            zo = zl + tip_lift
            row_o.append((xo, yo, zo))
            row_i.append((xo - sx * 0.011, yo - cy_d * 0.011, zo))
        col_outer.append(row_o)
        col_inner.append(row_i)

    body_parts.append(build_thick_shell(
        col_outer,
        col_inner,
        name='Exec_HighStandCollar',
        outer_color=EXEC_COLORS['coat_main'],
        inner_color=EXEC_COLORS['coat_lining'],
        rim_color=EXEC_COLORS['gold_trim'],
        shade_smooth=False,
        ao_strength=0.24,
        ground_ao=0.02,
        role='collar',
    ))

    # --- C. Gold/Amber 'Signal' Collar, Lapel & Spine Circuitry ---
    collar_trace_pts_L = [
        (0.022,  0.114, sh_z + 0.095),
        (0.102,  0.076, sh_z + 0.118),
        (0.138, -0.012, sh_z + 0.138),
        (0.114, -0.086, sh_z + 0.156),
    ]
    collar_sig_L = aa.add_pipe(
        collar_trace_pts_L,
        radius=0.005,
        bevel_resolution=1,
        name='Exec_CollarCircuit_L',
        color=EXEC_COLORS['sig_gold'],
        signal=True,
    )
    tag_skin_role(collar_sig_L, 'collar')
    sig_parts.extend([collar_sig_L, mirror_clean(collar_sig_L)])

    collar_tip_sig_L = _box(
        name='Exec_CollarTipCircuit_L',
        size=(0.008, 0.011, 0.112),
        location=(0.110, -0.086, sh_z + 0.098),
        rotation=(-0.14, 0.16, -0.18),
        color=EXEC_COLORS['sig_solar'],
        signal=True,
        role='collar',
    )
    sig_parts.extend([collar_tip_sig_L, mirror_clean(collar_tip_sig_L)])

    # Sleek angled V-lapel Signal circuit trace hugging upper coat opening
    lapel_trace_pts_L = [
        (0.108, -0.098, chest_z + 0.115),
        (0.074, -0.126, chest_z + 0.005),
        (0.036, -0.114, waist_seam_z + 0.028),
    ]
    lapel_sig_L = aa.add_pipe(
        lapel_trace_pts_L,
        radius=0.0045,
        bevel_resolution=1,
        name='Exec_LapelCircuit_L',
        color=EXEC_COLORS['sig_gold'],
        signal=True,
    )
    tag_skin_role(lapel_sig_L, 'torso')
    sig_parts.extend([lapel_sig_L, mirror_clean(lapel_sig_L)])

    sig_parts.append(_box(
        name='Exec_DorsalCrest_Signal',
        size=(0.030, 0.010, 0.150),
        location=(0.0, 0.130, chest_z + 0.065),
        color=EXEC_COLORS['sig_gold'],
        signal=True,
        role='torso',
    ))

    # --- D. Split-Hem Tailored Longcoat Panels (L & R Weighted Cleanly to Thighs!) ---
    skirt_u = 7
    skirt_z_stations = [
        # Starts right at waist_seam_z matching upper coat radius -> flares gracefully over thighs!
        (waist_seam_z,     0.156, 0.106, 0.112, 0.86 * math.pi, 0.055 * math.pi),
        (pelvis_z - 0.015, 0.168, 0.114, 0.120, 0.72 * math.pi, 0.062 * math.pi),
        (pelvis_z - 0.120, 0.178, 0.120, 0.128, 0.64 * math.pi, 0.070 * math.pi),
        (knee_L.z + 0.120, 0.188, 0.126, 0.136, 0.58 * math.pi, 0.078 * math.pi),
        (knee_L.z - 0.020, 0.198, 0.132, 0.144, 0.54 * math.pi, 0.086 * math.pi),
    ]
    sk_outer_L = []
    sk_inner_L = []
    for zl, rx, ry_f, ry_b, ang_front, ang_back in skirt_z_stations:
        row_o = []
        row_i = []
        for ui in range(skirt_u):
            ut = ui / (skirt_u - 1)
            theta = _lerp(ang_back, ang_front, ut)
            sx = math.sin(theta)
            cy_d = math.cos(theta)
            ry = ry_b if cy_d >= 0.0 else ry_f
            xo = max(0.014, sx * rx)
            yo = cy_d * ry
            row_o.append((xo, yo, zl))
            row_i.append((max(0.012, xo - sx * 0.013), yo - cy_d * 0.013, zl))
        sk_outer_L.append(row_o)
        sk_inner_L.append(row_i)

    skirt_L = build_thick_shell(
        sk_outer_L,
        sk_inner_L,
        name='Exec_LongcoatSkirt_L',
        outer_color=EXEC_COLORS['coat_main'],
        inner_color=EXEC_COLORS['coat_lining'],
        rim_color=EXEC_COLORS['coat_lapel'],
        shade_smooth=True,
        ao_strength=0.25,
        ground_ao=0.10,
        role='coat_skirt',
    )
    body_parts.extend([skirt_L, mirror_clean(skirt_L)])

    hem_gold_L = _box(
        name='Exec_SkirtHemPip_L',
        size=(0.010, 0.044, 0.016),
        location=(0.180, -0.024, knee_L.z - 0.008),
        rotation=(0.0, 0.0, -0.14),
        color=EXEC_COLORS['sig_gold'],
        signal=True,
        role='coat_skirt',
    )
    sig_parts.extend([hem_gold_L, mirror_clean(hem_gold_L)])

    # --- E. Tailored Coat Sleeves, Gold/Amber 'Signal' Cuff Circuitry & Hands ---
    sh_pt_L = Vector((sh_x - 0.012, 0.002, sh_z + 0.006))
    sleeve_L = _oriented_chain_loft(
        [
            (sh_pt_L, 0.056, 0.058),
            (_vlerp(sh_pt_L, elbow_L, 0.50), 0.048, 0.050),
            (elbow_L, 0.045, 0.046),
            (_vlerp(elbow_L, wrist_L, 0.62), 0.042, 0.044),
            (wrist_L, 0.046, 0.048),
        ],
        name='Exec_CoatSleeve_L',
        segs=8,
        color=EXEC_COLORS['coat_main'],
        ao_strength=0.24,
        role='arm',
    )
    body_parts.extend([sleeve_L, mirror_clean(sleeve_L)])

    fore_dir = (wrist_L - elbow_L).normalized()
    fore_rot_L = Vector((0.0, 0.0, 1.0)).rotation_difference(fore_dir)
    cuff_center_L = _vlerp(elbow_L, wrist_L, 0.84)

    cuff_band_L = aa.add_cylinder(
        name='Exec_TailoredCuff_L',
        radius=0.046,
        radius_top=0.050,
        depth=0.066,
        vertices=10,
        location=cuff_center_L,
        rotation=fore_rot_L,
        shade_smooth=False,
        color=EXEC_COLORS['coat_lapel'],
        bone='Forearm_L',
    )
    body_parts.extend([cuff_band_L, mirror_clean(cuff_band_L)])

    for ring_idx, r_off in enumerate((-0.020, 0.018)):
        cuff_ring_L = aa.add_torus(
            name=f'Exec_CuffCircuitRing_{ring_idx}_L',
            major_radius=0.049 + 0.002 * ring_idx,
            minor_radius=0.0045,
            major_segments=10,
            minor_segments=4,
            location=cuff_center_L + fore_dir * r_off,
            rotation=fore_rot_L,
            shade_smooth=False,
            color=EXEC_COLORS['sig_gold'] if ring_idx == 0 else EXEC_COLORS['sig_solar'],
            signal=True,
            bone='Forearm_L',
        )
        sig_parts.extend([cuff_ring_L, mirror_clean(cuff_ring_L)])

    cuff_bar_L = _box(
        name='Exec_CuffTraceBar_L',
        size=(0.009, 0.014, 0.112),
        location=_vlerp(elbow_L, wrist_L, 0.72) + Vector((0.045, 0.0, 0.0)),
        rotation=fore_rot_L,
        color=EXEC_COLORS['sig_gold'],
        signal=True,
        bone='Forearm_L',
    )
    sig_parts.extend([cuff_bar_L, mirror_clean(cuff_bar_L)])

    hands_L = build_stylized_hand_L(
        wrist_L,
        hand_L,
        glove_color=EXEC_COLORS['skin'],
        name_prefix='Exec',
    )
    body_parts.extend(hands_L)
    body_parts.extend(mirror_clean(hands_L))

    # --- F. Slim Pressed Dress Trousers & Pointed Corporate Dress Boots ---
    trouser_L = _oriented_chain_loft(
        [
            (hip_L + Vector((0.0, 0.0, 0.025)), 0.070, 0.076),
            (_vlerp(hip_L, knee_L, 0.52), 0.058, 0.064),
            (knee_L, 0.050, 0.055),
            (_vlerp(knee_L, ankle_L, 0.55), 0.045, 0.050),
            (ankle_L + Vector((0.0, 0.0, 0.035)), 0.040, 0.044),
        ],
        name='Exec_DressTrouser_L',
        segs=8,
        color=EXEC_COLORS['trouser'],
        shade_smooth=False,
        ao_strength=0.26,
        role='leg',
    )
    body_parts.extend([trouser_L, mirror_clean(trouser_L)])

    # Sleek Pointed-Toe Neo-Kyoto Executive Dress Boots (snug ankle shaft, distinct stacked heel)
    bx = ankle_L.x
    boot_rings_L = [
        [
            (bx - 0.038,  0.066, 0.000),
            (bx + 0.040,  0.066, 0.000),
            (bx + 0.040, -0.015, 0.008),  # Executive boot arch step!
            (bx + 0.042, -0.100, 0.000),
            (bx + 0.018, -0.232, 0.005),  # Sharp pointed corporate toe!
            (bx - 0.016, -0.232, 0.005),
            (bx - 0.040, -0.100, 0.000),
            (bx - 0.038, -0.015, 0.008),
        ],
        [
            (bx - 0.040,  0.068, 0.024),
            (bx + 0.042,  0.068, 0.024),
            (bx + 0.042, -0.015, 0.022),
            (bx + 0.044, -0.105, 0.018),
            (bx + 0.020, -0.238, 0.018),
            (bx - 0.018, -0.238, 0.018),
            (bx - 0.042, -0.105, 0.018),
            (bx - 0.040, -0.015, 0.022),
        ],
        [
            (bx - 0.038,  0.058, 0.058),
            (bx + 0.040,  0.058, 0.058),
            (bx + 0.040, -0.015, 0.058),
            (bx + 0.038, -0.092, 0.048),
            (bx + 0.016, -0.212, 0.032),
            (bx - 0.014, -0.212, 0.032),
            (bx - 0.036, -0.092, 0.048),
            (bx - 0.038, -0.015, 0.058),
        ],
        [
            (bx - 0.037,  0.052, 0.098),
            (bx + 0.039,  0.052, 0.098),
            (bx + 0.039,  0.002, 0.096),
            (bx + 0.037, -0.036, 0.092),
            (bx + 0.025, -0.048, 0.088),
            (bx - 0.025, -0.048, 0.088),
            (bx - 0.037, -0.036, 0.092),
            (bx - 0.037,  0.002, 0.096),
        ],
        [
            (bx - 0.038,  0.050, 0.198),
            (bx + 0.040,  0.050, 0.198),
            (bx + 0.040,  0.005, 0.198),
            (bx + 0.037, -0.030, 0.196),
            (bx + 0.025, -0.040, 0.194),
            (bx - 0.025, -0.040, 0.194),
            (bx - 0.037, -0.030, 0.196),
            (bx - 0.038,  0.005, 0.198),
        ],
    ]
    boot_L = build_lofted_tube(
        boot_rings_L,
        name='Exec_DressBoot_L',
        color=EXEC_COLORS['boot_leather'],
        shade_smooth=False,
        ao_strength=0.22,
        ground_ao=0.12,
        role='foot',
    )
    aa.paint_faces(
        boot_L,
        lambda _p, c, _n: 0.002 < c.z < 0.025,
        color=EXEC_COLORS['gold_trim'],
        ao_strength=0.16,
    )
    body_parts.extend([boot_L, mirror_clean(boot_L)])

    ankle_buckle_L = _bevel_box(
        name='Exec_BootBuckle_L',
        size=(0.084, 0.080, 0.014),
        location=(bx + 0.001, 0.008, 0.126),
        bevel=0.003,
        segments=1,
        color=EXEC_COLORS['gold_trim'],
        role='foot',
    )
    body_parts.extend([ankle_buckle_L, mirror_clean(ankle_buckle_L)])

    boot_sig_L = _box(
        name='Exec_BootHeelSignal_L',
        size=(0.050, 0.008, 0.010),
        location=(bx + 0.001, 0.069, 0.028),
        color=EXEC_COLORS['sig_gold'],
        signal=True,
        role='foot',
    )
    sig_parts.extend([boot_sig_L, mirror_clean(boot_sig_L)])

    # --- Batch into 'Body' + 'Signal', Rig 21 Bones & Keyframe 6 Clips ---
    merged = aa.combine_by_role(body_parts, sig_parts)
    role_snap = snapshot_skin_roles(merged)
    rig = aa.build_humanoid_rig_and_clips(
        merged,
        rig_name='ExecutiveRig',
        height=H,
        style='citizen',
        attack_clip_name='Punch',
        shoulder_width=sh_x,
        hip_width=hp_x,
    )
    apply_specialist_skinning(rig, merged, role_snap, height=H)

    out_path = OUT_DIR / 'executive.glb'
    info = aa.export_glb(out_path, [rig, *merged], min_tris=1201, max_meshes=6)
    print(f"[build_aa_specialists] Exported {info['path']}: {info['triangles']} tris, {info['meshes']} meshes, {info['size_kb']} KB")
    return out_path


# ---------------------------------------------------------------------------
# Strict Specialist GLB Contract Verification
# ---------------------------------------------------------------------------

def verify_specialist_glb(path: Path) -> dict:
    """Assert height (1.76..1.84m), tri budget (~1,800..3,400), +Z facing, 18+ bones, 6 clips, Body+Signal."""
    info = aa.verify_glb_file(path, expect_rig=True, max_meshes=6, min_tris=1201)
    tris = info['triangles']
    assert 1800 <= tris <= 3400, f'{path.name}: triangle count {tris} outside target 1800..3400'

    data = path.read_bytes()
    json_len, _ = struct.unpack_from('<2I', data, 12)
    g = json.loads(data[20:20 + json_len].decode('utf-8'))

    mesh_names = [m.get('name', '') for m in g.get('meshes', [])]
    mat_names = {m.get('name', '') for m in g.get('materials', [])}
    assert len(mesh_names) == 2, f'{path.name}: expected 2 batched meshes (Body + Signal), got {mesh_names}'
    assert 'Signal' in mat_names and 'VertexPaint' in mat_names, (
        f'{path.name}: expected VertexPaint + Signal materials, got {mat_names}'
    )

    mins = [float('inf')] * 3
    maxs = [float('-inf')] * 3
    for m in g['meshes']:
        for p in m['primitives']:
            acc = g['accessors'][p['attributes']['POSITION']]
            for i in range(3):
                mins[i] = min(mins[i], acc['min'][i])
                maxs[i] = max(maxs[i], acc['max'][i])

    height_m = maxs[1] - mins[1]
    assert abs(mins[1]) <= 0.008, f'{path.name}: soles must sit on ground y=0, got min_y={mins[1]:.4f}'
    assert 1.76 <= height_m <= 1.84, f'{path.name}: height {height_m:.4f}m outside 1.76..1.84m'
    assert maxs[2] > abs(mins[2]) * 0.90 and maxs[2] > 0.20, (
        f'{path.name}: toes/front must face +Z in Three.js, got z_bounds=({mins[2]:.3f}, {maxs[2]:.3f})'
    )

    n_joints = max(len(s.get('joints', [])) for s in g['skins'])
    print(
        f"[verify] {path.name}: {tris} tris, height={height_m:.3f}m (y={mins[1]:.3f}..{maxs[1]:.3f}), "
        f"z=({mins[2]:.3f}..{maxs[2]:.3f}), joints={n_joints}, clips={info['animations']}"
    )
    return info


def main() -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    courier_path = build_courier()
    exec_path = build_executive()

    for glb_path in (courier_path, exec_path):
        verify_specialist_glb(glb_path)
    print('[build_aa_specialists] Both AA cyberpunk specialist characters built and verified.')


if __name__ == '__main__':
    try:
        main()
    except Exception:
        traceback.print_exc()
        sys.exit(1)
