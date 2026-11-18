"""Headless Blender 4.2 LTS AA enemy infantry sculptor for Komorebi.

Authors and exports three bespoke AA cyberpunk enemy rigs into public/street/
(facing -Y in Blender = +Z in Three.js, 1,900..3,600 tris each):
  1. public/street/foe-punk.glb     — Neon Viper Street Punk (for 'punk' kind)
     * Spiked cyber-leather vest, neon mohawk + respirator mask ('Signal' hot-pink/crimson
       visor & mohawk tips), cybernetic right forearm, glowing plasma tanto knife in Hand_R.
     * Clips: 'Idle', 'Walk', 'Run', 'Punch' (fast vicious knife slash), 'HitReact', 'Death'.
  2. public/street/foe-enforcer.glb — Aethelgard Riot Enforcer (for 'enforcer' kind)
     * Heavy armored riot pauldrons, segmented tactical chestplate, full-face security helmet
       with glowing crimson T-visor ('Signal'), left forearm shield-bracer, heavy crackling
       shock-cleaver in Hand_R.
     * Clips: 'Idle', 'Walk', 'Run', 'Punch' (heavy cleaver smash), 'HitReact', 'Death'.
  3. public/street/foe-gunner.glb   — Hazmat Pulse Gunner (for 'gunner' kind)
     * Bright hazard-yellow & dark charcoal pressurized bio-armor, twin dorsal plasma tanks on
       back ('Signal' glowing cyan/crimson gauges), domed hazmat helm with glowing target-visor
       ('Signal'), futuristic twin-rail SMG/pulse carbine aimed forward in Hand_R.
     * Clips: 'Idle', 'Walk', 'Run', 'Punch', 'Idle_Shoot' (aimed rifle stance with crisp
       recoil kick), 'HitReact', 'Death'.

Run headless:
  .test/blender/blender --background --disable-autoexec --python tools/build_aa_infantry.py
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
ROOT_DIR = TOOLS_DIR.parent
OUT_DIR = ROOT_DIR / 'public' / 'street'

if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

import bmesh
import bpy
from mathutils import Euler, Matrix, Quaternion, Vector

import blender_aa_lib as aa


# ---------------------------------------------------------------------------
# Curated Character Palettes (Linear RGB for Komorebi Cel/Paint + Signal)
# ---------------------------------------------------------------------------

PUNK_COLORS = {
    'vest_leather': (0.072, 0.064, 0.096),   # Scarred obsidian-violet biker leather
    'vest_plum': (0.390, 0.068, 0.215),      # Neon Viper venom-plum quilted shoulder/lining
    'mesh_shirt': (0.130, 0.145, 0.190),     # Dark ripped cyber-weave undershirt
    'skin': (0.795, 0.575, 0.440),           # Muscular bare left arm & neck
    'skin_shadow': (0.595, 0.395, 0.295),    # Warm anatomical shadow
    'buzz_scalp': (0.145, 0.135, 0.185),     # Shaved cyber-temple stubble
    'mohawk_root': (0.285, 0.042, 0.145),    # Deep magenta-crimson hair base
    'cyber_chrome': (0.655, 0.715, 0.780),   # Brushed chrome cybernetic right forearm
    'cyber_dark': (0.125, 0.150, 0.200),     # Gunmetal prosthetic chassis & respirator
    'spike_chrome': (0.790, 0.830, 0.875),   # Polished razor shoulder spikes & hardware
    'jeans': (0.105, 0.130, 0.195),          # Slim acid-dark street punk denim
    'jeans_strap': (0.255, 0.080, 0.155),    # Viper crimson-plum knee & thigh straps
    'boot_leather': (0.045, 0.050, 0.068),   # Heavy stomper combat boots
    'sig_pink': (1.000, 0.115, 0.535),       # Signal: blazing hot-pink mohawk tips & plasma tanto
    'sig_crimson': (1.000, 0.065, 0.165),    # Signal: crimson respirator visor & filter glow
}

ENFORCER_COLORS = {
    'armor_slate': (0.175, 0.208, 0.272),    # Heavy Aethelgard riot steel-slate
    'armor_light': (0.495, 0.550, 0.625),    # Brushed titanium Strike-bevels & shield rim
    'under_suit': (0.068, 0.080, 0.110),     # Matte ballistic carbon trauma suit
    'crimson_trim': (0.560, 0.082, 0.128),   # Aethelgard Security crimson authority livery
    'brass_trim': (0.755, 0.570, 0.215),     # Heavy hydraulic fittings & clasps
    'cleaver_body': (0.310, 0.355, 0.420),   # Forged tungsten-slate cleaver block
    'cleaver_spine': (0.610, 0.665, 0.730),  # Machined alloy cleaver spine & coils
    'grip_rubber': (0.085, 0.090, 0.115),    # Ribbed insulated high-voltage handle
    'sig_crimson': (1.000, 0.070, 0.145),    # Signal: glowing crimson T-visor & shield stun-bar
    'sig_shock': (0.075, 0.925, 1.000),      # Signal: crackling high-voltage shock-cleaver edge
}

GUNNER_COLORS = {
    'hazmat_yellow': (0.955, 0.745, 0.075),  # High-visibility CBRN pressurized bio-armor
    'hazmat_amber': (0.855, 0.515, 0.065),   # Reinforced amber-yellow knee/pauldron trim
    'charcoal_suit': (0.072, 0.082, 0.106),  # Dark charcoal rubberized NBC pressure suit
    'charcoal_armor': (0.150, 0.172, 0.215), # Dark charcoal chest rig, harness & carbine body
    'tank_alloy': (0.285, 0.330, 0.390),     # Pressurized dorsal plasma tank cylinders
    'steel_rail': (0.625, 0.680, 0.735),     # Twin magnetic accelerator rails & collar bolts
    'dome_dark': (0.048, 0.075, 0.110),      # Polarized inner hazmat faceplate housing
    'sig_cyan': (0.035, 0.935, 1.000),       # Signal: plasma tank level gauges & twin-rail core
    'sig_crimson': (1.000, 0.080, 0.175),    # Signal: glowing target-visor HUD & warning gauge
}


# ---------------------------------------------------------------------------
# Joint Landmarks & Math Helpers
# ---------------------------------------------------------------------------

def _lerp(a: float, b: float, t: float) -> float:
    return a + (b - a) * t


def _clamp(v: float, lo: float, hi: float) -> float:
    return lo if v < lo else (hi if v > hi else v)


def _smoothstep(e0: float, e1: float, x: float) -> float:
    if abs(e1 - e0) < 1e-8:
        return 1.0 if x >= e1 else 0.0
    t = _clamp((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def _vlerp(a: Vector, b: Vector, t: float) -> Vector:
    return a + (b - a) * t


def rig_landmarks(height: float, shoulder_width: float, hip_width: float) -> dict:
    """Match exact joint coordinates created by aa.build_humanoid_rig_and_clips."""
    h = float(height)
    sh_x = float(shoulder_width)
    hp_x = float(hip_width)
    elbow_x = sh_x + 0.036 * h
    wrist_x = sh_x + 0.034 * h
    knee_x = hp_x + 0.012 * h
    ankle_x = hp_x + 0.018 * h

    return {
        'h': h,
        'sh_x': sh_x,
        'hp_x': hp_x,
        'elbow_x': elbow_x,
        'wrist_x': wrist_x,
        'knee_x': knee_x,
        'ankle_x': ankle_x,
        'pelvis_z': 0.48 * h,
        'spine_z': 0.57 * h,
        'chest_z': 0.68 * h,
        'neck_z': 0.78 * h,
        'head_z': 0.84 * h,
        'crown_z': 1.00 * h,
        'sh_z': 0.755 * h,
        'elbow_z': 0.618 * h,
        'wrist_z': 0.485 * h,
        'hand_tip_z': 0.415 * h,
        'hip_z': 0.505 * h,
        'knee_z': 0.285 * h,
        'ankle_z': 0.080 * h,
        'ball_z': 0.032 * h,
        'shoulder_L': Vector((sh_x, 0.0, 0.755 * h)),
        'elbow_L': Vector((elbow_x, 0.015 * h, 0.618 * h)),
        'wrist_L': Vector((wrist_x, -0.025 * h, 0.485 * h)),
        'hand_L': Vector((wrist_x, -0.038 * h, 0.415 * h)),
        'shoulder_R': Vector((-sh_x, 0.0, 0.755 * h)),
        'elbow_R': Vector((-elbow_x, 0.015 * h, 0.618 * h)),
        'wrist_R': Vector((-wrist_x, -0.025 * h, 0.485 * h)),
        'hand_R': Vector((-wrist_x, -0.038 * h, 0.415 * h)),
        'hip_L': Vector((hp_x, 0.0, 0.505 * h)),
        'knee_L': Vector((knee_x, -0.012 * h, 0.285 * h)),
        'ankle_L': Vector((ankle_x, 0.010 * h, 0.080 * h)),
    }


# ---------------------------------------------------------------------------
# Role Tagging, Clean Mirroring & Rigid-Safe Skinning
# ---------------------------------------------------------------------------

def tag_skin_role(objs, role: str):
    """Attach a '__role_<role>' vertex group preserved across combine_by_role."""
    if not role:
        return objs
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
    """Mirror across X=0 via aa.mirror_x while swapping _L/_R and keeping center group names intact."""
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
        if 'bone' in src:
            dup['bone'] = _swap_lr(src['bone'])
    return dup_list[0] if single else dup_list


def snapshot_skin_roles(meshes: Sequence[bpy.types.Object]) -> dict[str, dict[int, str]]:
    """Capture per-vertex '__role_*' tags before aa.build_humanoid_rig_and_clips replaces groups."""
    out: dict[str, dict[int, str]] = {}
    for m_obj in meshes:
        if m_obj is None or m_obj.type != 'MESH':
            continue
        idx_to_role: dict[int, str] = {}
        pin_indices: set[int] = set()
        for vg in m_obj.vertex_groups:
            if vg.name.startswith('__pin_'):
                pin_indices.add(vg.index)
            elif vg.name.startswith('__role_'):
                role_key = vg.name[len('__role_'):]
                if role_key.endswith('_Mirror'):
                    role_key = role_key[:-7]
                idx_to_role[vg.index] = role_key
        vert_roles: dict[int, str] = {}
        if idx_to_role:
            for v in m_obj.data.vertices:
                is_pinned = any(g.group in pin_indices and g.weight > 0.5 for g in v.groups)
                if is_pinned:
                    continue
                for g in v.groups:
                    if g.group in idx_to_role and g.weight > 0.5:
                        vert_roles[v.index] = idx_to_role[g.group]
                        break
        out[m_obj.name] = vert_roles
    return out


def _seg_dist(p: Vector, a: Vector, b: Vector) -> float:
    ab = b - a
    lsq = ab.length_squared
    if lsq < 1e-10:
        return (p - a).length
    t = _clamp((p - a).dot(ab) / lsq, 0.0, 1.0)
    return (p - (a + ab * t)).length


def apply_infantry_skinning(
    rig: bpy.types.Object,
    meshes: Sequence[bpy.types.Object],
    role_snapshot: dict[str, dict[int, str]],
    height: float,
) -> None:
    """Apply smooth X-separated multi-bone weights to '__role_*' spans without touching '__pin_*' parts."""
    bone_segs = {b.name: (b.head_local.copy(), b.tail_local.copy()) for b in rig.data.bones}
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
                    set_weights(vi, [('Pelvis', 1.0 - t), ('Spine', t)])
                elif p.z < chest_z:
                    t = _smoothstep(spine_z, chest_z, p.z)
                    set_weights(vi, [('Spine', 1.0 - t), ('Chest', t)])
                else:
                    t_neck = _smoothstep(neck_z - 0.02 * h, neck_z + 0.04 * h, p.z)
                    w_chest = 1.0 - 0.62 * t_neck
                    w_neck = 0.62 * t_neck
                    sh_share = 0.25 * _smoothstep(0.072 * h, 0.130 * h, abs(p.x)) * _smoothstep(chest_z, neck_z, p.z)
                    set_weights(vi, [
                        ('Chest', w_chest * (1.0 - sh_share)),
                        ('Neck', w_neck * (1.0 - sh_share)),
                        (f'Clavicle_{side}', sh_share),
                    ])

            elif role == 'pelvis_saddle':
                t_th = _smoothstep(0.024 * h, 0.070 * h, abs(p.x)) * _smoothstep(hip_z + 0.015 * h, hip_z - 0.048 * h, p.z)
                set_weights(vi, [
                    ('Pelvis', 1.0 - 0.65 * t_th),
                    (f'Thigh_{side}', 0.65 * t_th),
                ])

            elif role == 'arm':
                choices = [f'Clavicle_{side}', f'UpperArm_{side}', f'Forearm_{side}', f'Hand_{side}']
                near = sorted(((_seg_dist(p, *bone_segs[k]), k) for k in choices))[:2]
                soft = 0.026 * h
                set_weights(vi, [(k, 1.0 / ((d + soft) ** 4)) for d, k in near])

            elif role == 'leg':
                choices = [f'Thigh_{side}', f'Shin_{side}']
                if p.z > hip_z - 0.030 * h:
                    choices.append('Pelvis')
                near = sorted(((_seg_dist(p, *bone_segs[k]), k) for k in choices))[:2]
                soft = 0.028 * h
                set_weights(vi, [(k, 1.0 / ((d + soft) ** 4)) for d, k in near])

            elif role == 'foot':
                t_shin = _smoothstep(ankle_z + 0.016 * h, ankle_z + 0.068 * h, p.z)
                t_toe = _smoothstep(-0.072 * h, -0.120 * h, p.y) * (1.0 - t_shin)
                w_foot = max(0.0, 1.0 - t_shin - t_toe)
                set_weights(vi, [
                    (f'Shin_{side}', t_shin),
                    (f'Foot_{side}', w_foot),
                    (f'Toe_{side}', t_toe),
                ])


# ---------------------------------------------------------------------------
# Closed-Manifold AA Sculpting Primitives (100% FrontSide Culling Safe)
# ---------------------------------------------------------------------------

def _box(*args, role: str | None = None, **kwargs) -> bpy.types.Object:
    obj = aa.add_box(*args, **kwargs)
    return tag_skin_role(obj, role) if role else obj


def _bevel_box(*args, role: str | None = None, **kwargs) -> bpy.types.Object:
    obj = aa.add_bevel_box(*args, **kwargs)
    return tag_skin_role(obj, role) if role else obj


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
    ground_ao: float = 0.14,
    gradient_z=None,
    bone: str | None = None,
    role: str | None = None,
) -> bpy.types.Object:
    """Loft closed equal-count rings into a watertight manifold solid."""
    m = len(rings[0])
    bm = bmesh.new()
    bm_rings = [[bm.verts.new(pt) for pt in ring] for ring in rings]

    for k in range(len(bm_rings) - 1):
        ra, rb = bm_rings[k], bm_rings[k + 1]
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
    elif role:
        tag_skin_role(obj, role)
    return obj


def build_thick_shell(
    outer_grid: Sequence[Sequence[tuple[float, float, float]]],
    inner_grid: Sequence[Sequence[tuple[float, float, float]]],
    name: str,
    outer_color=(0.18, 0.22, 0.28),
    inner_color=None,
    rim_color=None,
    signal: bool = False,
    shade_smooth: bool = True,
    ao_strength: float = 0.26,
    ground_ao: float = 0.12,
    bone: str | None = None,
    role: str | None = None,
) -> bpy.types.Object:
    """Build a closed double-walled armor/visor/pauldron shell with outward normals."""
    rows = len(outer_grid)
    cols = len(outer_grid[0])
    bm = bmesh.new()
    o_verts = [[bm.verts.new(pt) for pt in row] for row in outer_grid]
    i_verts = [[bm.verts.new(pt) for pt in row] for row in inner_grid]
    tag_layer = bm.faces.layers.int.new('shell_zone')

    for r in range(rows - 1):
        for c in range(cols - 1):
            f = bm.faces.new((o_verts[r][c], o_verts[r][c + 1], o_verts[r + 1][c + 1], o_verts[r + 1][c]))
            f[tag_layer] = 0
            fi = bm.faces.new((i_verts[r][c], i_verts[r + 1][c], i_verts[r + 1][c + 1], i_verts[r][c + 1]))
            fi[tag_layer] = 1

    for c in range(cols - 1):
        fb = bm.faces.new((o_verts[0][c], i_verts[0][c], i_verts[0][c + 1], o_verts[0][c + 1]))
        fb[tag_layer] = 2
        ft = bm.faces.new((o_verts[rows - 1][c], o_verts[rows - 1][c + 1], i_verts[rows - 1][c + 1], i_verts[rows - 1][c]))
        ft[tag_layer] = 2

    for r in range(rows - 1):
        fl = bm.faces.new((o_verts[r][0], o_verts[r + 1][0], i_verts[r + 1][0], i_verts[r][0]))
        fl[tag_layer] = 2
        fr = bm.faces.new((o_verts[r][cols - 1], i_verts[r][cols - 1], i_verts[r + 1][cols - 1], o_verts[r + 1][cols - 1]))
        fr[tag_layer] = 2

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
    elif role:
        tag_skin_role(obj, role)
    return obj


def build_open_vest_shell(
    stations: Sequence[tuple[float, float, float, float, float, float]],
    thickness: float = 0.016,
    arc_segments: int = 11,
    name: str = 'OpenVest',
    color=(0.08, 0.07, 0.10),
    inner_color=None,
    shade_smooth: bool = False,
    ao_strength: float = 0.28,
    bone: str | None = None,
    role: str | None = None,
) -> bpy.types.Object:
    """Build a watertight thick open-front vest/jacket shell open toward -Y (FRONT)."""
    outer_grid = []
    inner_grid = []
    m_cnt = max(5, int(arc_segments))
    for z, rx, ry_f, ry_b, yc, half_open in stations:
        th0 = -math.pi * 0.5 + float(half_open)
        th1 = math.pi * 1.5 - float(half_open)
        o_row = []
        i_row = []
        for i in range(m_cnt):
            u = i / (m_cnt - 1)
            th = _lerp(th0, th1, u)
            ct, st = math.cos(th), math.sin(th)
            ry = ry_f if st < 0.0 else ry_b
            rx_i = max(0.012, rx - thickness)
            ry_i = max(0.012, ry - thickness)
            o_row.append((ct * rx, yc + st * ry, z))
            i_row.append((ct * rx_i, yc + st * ry_i, z))
        outer_grid.append(o_row)
        inner_grid.append(i_row)

    return build_thick_shell(
        outer_grid,
        inner_grid,
        name=name,
        outer_color=color,
        inner_color=inner_color,
        rim_color=inner_color,
        shade_smooth=shade_smooth,
        ao_strength=ao_strength,
        bone=bone,
        role=role,
    )


def _oriented_chain_loft(
    stations: Sequence[tuple[Vector, float, float]],
    name: str,
    segs: int = 9,
    color=(0.2, 0.25, 0.3),
    gradient_z=None,
    shade_smooth: bool = True,
    ao_strength: float = 0.25,
    bone: str | None = None,
    role: str | None = None,
) -> bpy.types.Object:
    """Loft a smooth limb/sleeve/boot segment along 3D stations of (center_vec, rx, ry)."""
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
        gradient_z=gradient_z,
        shade_smooth=shade_smooth,
        ao_strength=ao_strength,
        bone=bone,
        role=role,
    )


def build_pelvis_saddle(lm: dict, color, name: str) -> bpy.types.Object:
    pelvis_z = lm['pelvis_z']
    hip_z = lm['hip_z']
    hp_x = lm['hp_x']
    rings = [
        _ring_oval(0.0, 0.008, pelvis_z - 0.055, hp_x * 0.68, 0.076, 0.086, segs=12, squareness=0.22),
        _ring_oval(0.0, 0.005, pelvis_z - 0.005, hp_x + 0.072, 0.104, 0.114, segs=12, squareness=0.26),
        _ring_oval(0.0, 0.003, hip_z + 0.036,    hp_x + 0.068, 0.102, 0.112, segs=12, squareness=0.26),
    ]
    return build_lofted_tube(rings, name=name, color=color, ao_strength=0.28, role='pelvis_saddle')


def build_combat_hand(
    wrist_pt: Vector,
    hand_tip_pt: Vector,
    side: str,
    palm_color,
    finger_color=None,
    name_prefix: str = 'Hand',
) -> list[bpy.types.Object]:
    """Build closed combat fist/grip mitt + opposable thumb pinned cleanly to Hand_L or Hand_R."""
    sign = 1.0 if side == 'L' else -1.0
    bone_name = f'Hand_{side}'
    f_col = palm_color if finger_color is None else finger_color
    axis = hand_tip_pt - wrist_pt
    mid_palm = wrist_pt + axis * 0.42
    knuckle = wrist_pt + axis * 0.72
    fist_end = wrist_pt + axis * 0.96

    palm = _oriented_chain_loft(
        [
            (wrist_pt + axis * 0.02, 0.031, 0.026),
            (mid_palm, 0.037, 0.029),
            (knuckle, 0.036, 0.030),
            (fist_end, 0.028, 0.024),
        ],
        name=f'{name_prefix}_Palm_{side}',
        segs=8,
        color=palm_color,
        shade_smooth=False,
        ao_strength=0.24,
        bone=bone_name,
    )
    if finger_color is not None:
        aa.paint_faces(palm, lambda _p, c, _n: c.z < mid_palm.z, color=f_col, ao_strength=0.22)

    thumb = aa.add_limb(
        wrist_pt + Vector((-sign * 0.010, -0.016, -0.016)),
        wrist_pt + Vector((-sign * 0.024, -0.048, -0.062)),
        radius0=0.013,
        radius1=0.010,
        segments=6,
        name=f'{name_prefix}_Thumb_{side}',
        color=f_col,
        ao_strength=0.22,
        bone=bone_name,
    )
    return [palm, thumb]


def build_grounded_boot_L(
    ankle_x: float,
    boot_height: float,
    sole_color,
    upper_color,
    toe_cap_color=None,
    buckle_color=None,
    width_scale: float = 1.0,
    name_prefix: str = 'Boot',
) -> list[bpy.types.Object]:
    """Build a chunky grounded combat/hazmat/riot boot on +X resting flat on z = 0.000."""
    bx = ankle_x
    w = width_scale
    rings = [
        [
            (bx - 0.043 * w,  0.072, 0.000),
            (bx + 0.045 * w,  0.072, 0.000),
            (bx + 0.047 * w, -0.020, 0.000),
            (bx + 0.048 * w, -0.130, 0.000),
            (bx + 0.030 * w, -0.236, 0.000),
            (bx - 0.028 * w, -0.236, 0.000),
            (bx - 0.046 * w, -0.130, 0.000),
            (bx - 0.045 * w, -0.020, 0.000),
        ],
        [
            (bx - 0.045 * w,  0.075, 0.028),
            (bx + 0.047 * w,  0.075, 0.028),
            (bx + 0.049 * w, -0.020, 0.026),
            (bx + 0.050 * w, -0.135, 0.025),
            (bx + 0.032 * w, -0.242, 0.024),
            (bx - 0.030 * w, -0.242, 0.024),
            (bx - 0.048 * w, -0.135, 0.025),
            (bx - 0.047 * w, -0.020, 0.026),
        ],
        [
            (bx - 0.042 * w,  0.066, 0.068),
            (bx + 0.044 * w,  0.066, 0.068),
            (bx + 0.044 * w, -0.020, 0.066),
            (bx + 0.044 * w, -0.118, 0.058),
            (bx + 0.028 * w, -0.222, 0.046),
            (bx - 0.026 * w, -0.222, 0.046),
            (bx - 0.042 * w, -0.118, 0.058),
            (bx - 0.042 * w, -0.020, 0.066),
        ],
        [
            (bx - 0.041 * w,  0.058, 0.115),
            (bx + 0.043 * w,  0.058, 0.115),
            (bx + 0.043 * w,  0.002, 0.112),
            (bx + 0.040 * w, -0.046, 0.106),
            (bx + 0.028 * w, -0.062, 0.100),
            (bx - 0.028 * w, -0.062, 0.100),
            (bx - 0.040 * w, -0.046, 0.106),
            (bx - 0.041 * w,  0.002, 0.112),
        ],
        [
            (bx - 0.043 * w,  0.056, boot_height),
            (bx + 0.045 * w,  0.056, boot_height),
            (bx + 0.045 * w,  0.004, boot_height),
            (bx + 0.042 * w, -0.038, boot_height - 0.004),
            (bx + 0.028 * w, -0.050, boot_height - 0.006),
            (bx - 0.028 * w, -0.050, boot_height - 0.006),
            (bx - 0.042 * w, -0.038, boot_height - 0.004),
            (bx - 0.043 * w,  0.004, boot_height),
        ],
    ]
    boot = build_lofted_tube(
        rings,
        name=f'{name_prefix}_Shell_L',
        color=upper_color,
        shade_smooth=False,
        ao_strength=0.25,
        ground_ao=0.14,
        role='foot',
    )
    aa.paint_faces(boot, lambda _p, c, _n: c.z < 0.030, color=sole_color, ao_strength=0.18)
    if toe_cap_color is not None:
        aa.paint_faces(boot, lambda _p, c, _n: c.y < -0.148 and 0.022 < c.z < 0.062, color=toe_cap_color, ao_strength=0.18)

    out = [boot]
    if buckle_color is not None:
        for bi, bz in enumerate((0.118, boot_height - 0.032)):
            out.append(_bevel_box(
                name=f'{name_prefix}_Strap_{bi}_L',
                size=(0.092 * w, 0.092, 0.018),
                location=(bx + 0.001, -0.002, bz),
                bevel=0.003,
                segments=1,
                color=buckle_color,
                role='foot',
            ))
    return out


def apply_character_seam_ao(
    body_obj: bpy.types.Object,
    seam_zs: Sequence[tuple[float, float, float]],
    crotch_z: float,
) -> None:
    """Bake crisp belt/cuff/armor-plate seam occlusion into Body 'Color'."""
    mesh = body_obj.data
    col = mesh.color_attributes.get('Color')
    if col is None:
        return
    verts = mesh.vertices
    v_mult = [1.0] * len(verts)
    for i, v in enumerate(verts):
        p = v.co
        m = 1.0
        if crotch_z - 0.14 < p.z < crotch_z + 0.03 and abs(p.x) < 0.062:
            t_in = (1.0 - abs(p.x) / 0.062) * (1.0 - abs(p.z - (crotch_z - 0.04)) / 0.11)
            if t_in > 0.0:
                m *= 1.0 - 0.22 * t_in
        for sz, span, strength in seam_zs:
            dz = abs(p.z - sz)
            if dz < span:
                w = 1.0 - (dz / span)
                m *= 1.0 - strength * (w * w)
        v_mult[i] = max(0.54, m)

    for li, loop in enumerate(mesh.loops):
        k = v_mult[loop.vertex_index]
        if k < 0.999:
            r, g, b, a = col.data[li].color
            col.data[li].color = (r * k, g * k, b * k, a)
    mesh.update()


# ---------------------------------------------------------------------------
# Bespoke Combat Animation Authoring (Exact Parent-Chain Weapon Orientation)
# ---------------------------------------------------------------------------

def author_infantry_clips(
    rig: bpy.types.Object,
    archetype: str,
    height: float,
) -> None:
    """Author expressive class-specific 24 fps clips with exact Hand_R weapon kinematics."""
    h = float(height)
    pelvis_z = 0.48 * h
    bpy.context.scene.render.fps = aa.FPS

    if rig.animation_data is None:
        rig.animation_data_create()
    rig.animation_data.action = None
    for act in list(bpy.data.actions):
        bpy.data.actions.remove(act)

    basis = {pb.name: pb.bone.matrix_local.to_quaternion() for pb in rig.pose.bones}
    parent_name = {pb.name: (pb.parent.name if pb.parent else None) for pb in rig.pose.bones}
    world_deltas: dict[str, Quaternion] = {}

    def reset_pose():
        world_deltas.clear()
        for pb in rig.pose.bones:
            pb.rotation_mode = 'QUATERNION'
            pb.rotation_quaternion = (1.0, 0.0, 0.0, 0.0)
            pb.location = (0.0, 0.0, 0.0)
            pb.scale = (1.0, 1.0, 1.0)
            world_deltas[pb.name] = Quaternion((1.0, 0.0, 0.0, 0.0))

    def rot(name: str, x: float = 0.0, y: float = 0.0, z: float = 0.0):
        q = basis[name]
        delta = Quaternion((0.0, 0.0, 1.0), z) @ Quaternion((0.0, 1.0, 0.0), y) @ Quaternion((1.0, 0.0, 0.0), x)
        rig.pose.bones[name].rotation_quaternion = q.inverted() @ delta @ q
        world_deltas[name] = delta

    def accum_world_delta(bone_name: str) -> Quaternion:
        chain = []
        cur = bone_name
        while cur is not None:
            chain.append(cur)
            cur = parent_name.get(cur)
        acc = Quaternion((1.0, 0.0, 0.0, 0.0))
        for bn in reversed(chain):
            acc = acc @ world_deltas.get(bn, Quaternion((1.0, 0.0, 0.0, 0.0)))
        return acc

    def aim_hand_world(side: str, x: float = 0.0, y: float = 0.0, z: float = 0.0):
        """Orient Hand_L or Hand_R to exact world-space Euler angles relative to Rest Pose."""
        hand_bn = f'Hand_{side}'
        fore_bn = f'Forearm_{side}'
        w_fore = accum_world_delta(fore_bn)
        w_target = Quaternion((0.0, 0.0, 1.0), z) @ Quaternion((0.0, 1.0, 0.0), y) @ Quaternion((1.0, 0.0, 0.0), x)
        local_delta = w_fore.inverted() @ w_target
        q = basis[hand_bn]
        rig.pose.bones[hand_bn].rotation_quaternion = q.inverted() @ local_delta @ q
        world_deltas[hand_bn] = local_delta

    def move_pelvis(dx: float = 0.0, dy: float = 0.0, dz: float = 0.0):
        rig.pose.bones['Pelvis'].location = basis['Pelvis'].inverted() @ Vector((dx, dy, dz))

    def key_all(frame: int):
        for pb in rig.pose.bones:
            pb.keyframe_insert('rotation_quaternion', frame=frame)
            if pb.name == 'Pelvis':
                pb.keyframe_insert('location', frame=frame)

    if archetype == 'punk':
        clip_specs = [
            ('Idle', 2.2),
            ('Walk', 0.96),
            ('Run', 0.62),
            ('Punch', 0.54),
            ('HitReact', 0.42),
            ('Death', 1.12),
        ]
    elif archetype == 'enforcer':
        clip_specs = [
            ('Idle', 2.4),
            ('Walk', 1.08),
            ('Run', 0.70),
            ('Punch', 0.68),
            ('HitReact', 0.44),
            ('Death', 1.18),
        ]
    else:  # 'gunner'
        clip_specs = [
            ('Idle', 2.4),
            ('Walk', 1.00),
            ('Run', 0.66),
            ('Punch', 0.58),
            ('Idle_Shoot', 0.92),
            ('HitReact', 0.42),
            ('Death', 1.15),
        ]

    for clip_title, dur in clip_specs:
        action = bpy.data.actions.new(clip_title)
        action.use_fake_user = True
        rig.animation_data.action = action
        frames = max(6, round(dur * aa.FPS))

        for f in range(frames + 1):
            u = f / frames
            phase = u * math.tau
            reset_pose()

            if clip_title == 'Idle':
                breath = math.sin(phase)
                sway = math.sin(phase)
                scan = math.sin(phase + 0.7)

                if archetype == 'punk':
                    # Predatory coiled street-punk crouch-sway with plasma tanto poised forward
                    move_pelvis(dx=0.020 * h * sway, dz=-0.022 * h - 0.008 * h * abs(sway))
                    rot('Pelvis', x=0.05, y=-0.03 * sway, z=-0.18 + 0.04 * sway)
                    rot('Spine', x=0.08 - 0.018 * breath, z=0.06)
                    rot('Chest', x=0.06 - 0.022 * breath, z=0.14 + 0.03 * scan)
                    rot('Neck', x=-0.06, z=0.04 * scan)
                    rot('Head', x=-0.05 + 0.025 * math.cos(phase), y=0.03 * sway, z=0.06 + 0.09 * scan)

                    rot('Thigh_L', x=-0.16, y=-0.04)
                    rot('Shin_L', x=0.28)
                    rot('Foot_L', x=-0.10)
                    rot('Thigh_R', x=0.06, y=0.05)
                    rot('Shin_R', x=0.16)
                    rot('Foot_R', x=-0.08)

                    rot('UpperArm_L', x=-0.42 + 0.04 * sway, y=-0.16, z=-0.22)
                    rot('Forearm_L', x=-0.72)
                    rot('UpperArm_R', x=-0.34 - 0.04 * breath, y=0.22, z=0.10)
                    rot('Forearm_R', x=-0.62 - 0.04 * breath)
                    aim_hand_world('R', x=-0.22 - 0.04 * breath, y=0.18, z=0.16)

                elif archetype == 'enforcer':
                    # Planted riot-wall stance: left shield-bracer raised across chest, shock-cleaver ready
                    move_pelvis(dx=0.010 * h * sway, dz=-0.018 * h - 0.006 * h * (0.5 - 0.5 * math.cos(phase)))
                    rot('Pelvis', x=0.03, z=-0.14)
                    rot('Spine', x=0.05 - 0.015 * breath, z=0.06)
                    rot('Chest', x=0.04 - 0.020 * breath, z=0.10)
                    rot('Head', x=0.02 * math.cos(phase), z=0.06 * scan)

                    for side, sign in (('L', 1.0), ('R', -1.0)):
                        rot(f'Thigh_{side}', x=-0.10 if side == 'L' else 0.05, y=-sign * 0.04)
                        rot(f'Shin_{side}', x=0.20 if side == 'L' else 0.14)
                        rot(f'Foot_{side}', x=-0.08)

                    # Left arm presents forearm shield-bracer forward
                    rot('Clavicle_L', z=-0.12)
                    rot('UpperArm_L', x=-0.62 + 0.02 * breath, y=-0.14, z=-0.42)
                    rot('Forearm_L', x=-0.78, z=-0.18)
                    # Right arm poises heavy shock-cleaver at ready
                    rot('UpperArm_R', x=-0.28 - 0.03 * breath, y=0.20, z=0.08)
                    rot('Forearm_R', x=-0.56)
                    aim_hand_world('R', x=-0.24 - 0.03 * breath, y=0.12, z=0.06)

                else:  # gunner
                    # Tactical CBRN patrol stance: twin-rail pulse carbine cradled forward at high-hip ready
                    move_pelvis(dx=0.012 * h * sway, dz=-0.016 * h)
                    rot('Pelvis', x=0.03, z=-0.20)
                    rot('Spine', x=0.05 - 0.014 * breath, z=0.08)
                    rot('Chest', x=0.04 - 0.018 * breath, z=0.12 + 0.03 * scan)
                    rot('Head', x=0.02 * math.cos(phase), z=0.08 * scan)

                    rot('Thigh_L', x=-0.12, y=-0.03)
                    rot('Shin_L', x=0.20)
                    rot('Foot_L', x=-0.08)
                    rot('Thigh_R', x=0.08, y=0.03)
                    rot('Shin_R', x=0.14)
                    rot('Foot_R', x=-0.06)

                    rot('Clavicle_R', z=0.08)
                    rot('UpperArm_R', x=-0.42 + 0.02 * breath, y=0.14, z=0.20)
                    rot('Forearm_R', x=-0.88)
                    aim_hand_world('R', x=0.10 + 0.02 * breath, y=0.0, z=0.08 + 0.04 * scan)

                    rot('Clavicle_L', z=-0.16)
                    rot('UpperArm_L', x=-0.86 + 0.02 * breath, y=-0.06, z=-0.54)
                    rot('Forearm_L', x=-0.58)

            elif clip_title in ('Walk', 'Run'):
                run = clip_title == 'Run'
                s_base = math.sin(phase)
                c_base = math.cos(phase)
                bob = (0.034 if run else 0.017) * h * math.cos(2.0 * phase) - (0.024 if run else 0.012) * h
                lat = (0.012 if run else 0.016) * h * s_base
                move_pelvis(dx=lat, dz=bob)

                lean = (0.18 if archetype == 'punk' else 0.13) if run else (0.05 if archetype == 'punk' else 0.035)
                hip_twist = (0.11 if run else 0.065) * s_base
                hip_roll = (0.050 if run else 0.038) * s_base
                rot('Pelvis', x=lean * 0.55, y=-hip_roll, z=-hip_twist)
                rot('Spine', x=lean, y=hip_roll * 0.6, z=hip_twist * 0.75)
                rot('Chest', x=lean * 0.75, y=hip_roll * 0.35, z=hip_twist * 1.10)
                rot('Neck', x=-lean * 0.50, z=-hip_twist * 0.45)
                rot('Head', x=-lean * 0.80 + 0.02 * math.cos(2.0 * phase), z=-hip_twist * 0.65)

                for side, sign in (('L', 1.0), ('R', -1.0)):
                    sw = s_base * sign
                    cw = c_base * sign
                    thigh_amp = (0.76 if archetype != 'enforcer' else 0.64) if run else (0.44 if archetype != 'enforcer' else 0.38)
                    lift = max(0.0, cw) * (0.20 if run else 0.09)
                    knee = (0.10 if run else 0.05) + max(0.0, -sw) * (0.80 if run else 0.42) + max(0.0, cw) * (0.50 if run else 0.28)
                    ankle = (0.32 if run else 0.20) * max(0.0, -sw) - (0.18 if run else 0.15) * max(0.0, sw)
                    rot(f'Thigh_{side}', x=-thigh_amp * sw - lift, y=-sign * 0.025)
                    rot(f'Shin_{side}', x=knee)
                    rot(f'Foot_{side}', x=ankle)
                    rot(f'Toe_{side}', x=(0.26 if run else 0.16) * max(0.0, -sw))

                if archetype == 'gunner':
                    # Tactical combat carry so twin-rail carbine stays aimed safely forward while moving
                    bob_r = 0.05 * math.sin(2.0 * phase)
                    rot('UpperArm_R', x=-0.48 + 0.10 * s_base, y=0.14, z=0.22)
                    rot('Forearm_R', x=-0.88 + bob_r)
                    aim_hand_world('R', x=(0.18 if run else 0.08) + bob_r, y=0.0, z=0.14)
                    rot('Clavicle_L', z=-0.16)
                    rot('UpperArm_L', x=-0.88 - 0.10 * s_base, z=-0.52)
                    rot('Forearm_L', x=-0.56)
                elif archetype == 'enforcer':
                    # Shield-bracer carried forward on L; shock-cleaver carried menacingly out on R
                    rot('UpperArm_L', x=(-0.72 if run else -0.52) + 0.16 * s_base, y=-0.14, z=-0.36)
                    rot('Forearm_L', x=-0.78)
                    rot('UpperArm_R', x=-0.22 - (0.36 if run else 0.22) * s_base, y=0.20, z=0.06)
                    rot('Forearm_R', x=-0.58 if run else -0.44)
                    aim_hand_world('R', x=-0.15 - 0.12 * s_base, y=0.14, z=0.04)
                else:  # punk
                    rot('UpperArm_L', x=(0.62 if run else 0.32) * s_base - (0.18 if run else 0.06), y=-0.10)
                    rot('Forearm_L', x=-0.84 if run else -0.32)
                    rot('UpperArm_R', x=-0.28 - (0.38 if run else 0.24) * s_base, y=0.22)
                    rot('Forearm_R', x=-0.62 if run else -0.42)
                    aim_hand_world('R', x=-0.10 - 0.15 * s_base, y=0.22, z=0.12)

            elif clip_title == 'Idle_Shoot':
                # Hazmat Gunner: shouldered aimed twin-rail rifle stance with crisp recoil kick!
                brace = _smoothstep(0.0, 0.20, u)
                recoil = 0.0
                for shot_u in (0.48, 0.72):
                    if u >= shot_u:
                        dt_u = (u - shot_u) / 0.20
                        if dt_u < 1.0:
                            recoil += math.sin(math.pi * (dt_u ** 0.72)) * (1.0 - 0.28 * dt_u)

                move_pelvis(dy=0.022 * h * recoil, dz=-0.026 * h * brace)
                rot('Pelvis', x=0.04 * brace, z=-0.24 * brace)
                rot('Spine', x=0.07 * brace - 0.07 * recoil, z=0.09 * brace)
                rot('Chest', x=0.06 * brace - 0.10 * recoil, z=0.15 * brace)
                rot('Neck', x=0.04 * brace, z=0.02 * brace)
                rot('Head', x=0.06 * brace - 0.06 * recoil, y=-0.07 * brace, z=0.02 * brace)

                # Right arm shoulders compact buttstock & raises holographic sight to visor line
                rot('Clavicle_R', y=0.08 * recoil, z=0.10 * brace - 0.08 * recoil)
                rot('UpperArm_R', x=-0.56 * brace - 0.12 * recoil, y=0.16 * brace, z=0.24 * brace)
                rot('Forearm_R', x=-1.08 * brace - 0.14 * recoil, z=0.18 * brace)
                # Keep twin-rail barrel dead level forward (-Y) with crisp upward recoil climb!
                aim_hand_world('R', x=-0.18 * recoil, y=0.0, z=0.02 * brace)

                # Left arm reaches cleanly forward to cup the carbine forend/foregrip
                rot('Clavicle_L', y=0.06 * brace, z=-0.26 * brace)
                rot('UpperArm_L', x=-1.26 * brace - 0.10 * recoil, y=-0.04 * brace, z=-0.50 * brace)
                rot('Forearm_L', x=-0.24 * brace - 0.08 * recoil, z=-0.10 * brace)

                rot('Thigh_L', x=-0.18 * brace)
                rot('Shin_L', x=0.28 * brace)
                rot('Foot_L', x=-0.08 * brace)
                rot('Thigh_R', x=0.12 * brace)
                rot('Shin_R', x=0.18 * brace)
                rot('Foot_R', x=-0.06 * brace)

            elif clip_title == 'Punch':
                if archetype == 'punk':
                    # Fast vicious diagonal plasma-tanto knife slash!
                    wind = _smoothstep(0.0, 0.24, u) * (1.0 - _smoothstep(0.24, 0.38, u))
                    slash = _smoothstep(0.22, 0.44, u) * (1.0 - _smoothstep(0.68, 1.0, u))

                    move_pelvis(dy=-0.045 * h * slash + 0.020 * h * wind, dz=-0.042 * h * slash)
                    rot('Pelvis', x=0.14 * slash - 0.06 * wind, z=0.28 * slash - 0.24 * wind)
                    rot('Spine', x=0.18 * slash - 0.08 * wind, z=0.32 * slash - 0.28 * wind)
                    rot('Chest', x=0.16 * slash - 0.08 * wind, z=0.38 * slash - 0.32 * wind)
                    rot('Head', x=-0.10 * slash, z=-0.42 * slash + 0.32 * wind)

                    rot('Clavicle_R', y=0.18 * wind, z=0.28 * slash - 0.20 * wind)
                    rot('UpperArm_R', x=-1.15 * wind - 0.88 * slash, y=0.52 * wind - 0.18 * slash, z=-0.38 * wind + 0.78 * slash)
                    rot('Forearm_R', x=-0.78 * wind - 0.22 * slash)
                    # Cock blade high-right on windup, whip razor-flat diagonal across -Y front on slash!
                    aim_hand_world('R', x=-0.85 * wind + 0.16 * slash, y=0.65 * wind - 0.95 * slash, z=-0.52 * wind + 0.72 * slash)

                    rot('UpperArm_L', x=-0.55 * wind + 0.34 * slash, y=-0.22 - 0.18 * slash, z=-0.28 * wind)
                    rot('Forearm_L', x=-0.82 * wind - 0.56 * slash)

                    rot('Thigh_L', x=-0.34 * slash - 0.08 * wind)
                    rot('Shin_L', x=0.44 * slash + 0.14 * wind)
                    rot('Foot_L', x=-0.12 * slash)
                    rot('Thigh_R', x=0.24 * slash)
                    rot('Shin_R', x=0.22 * slash)

                elif archetype == 'enforcer':
                    # Heavy overhead riot shock-cleaver smash!
                    wind = _smoothstep(0.0, 0.36, u) * (1.0 - _smoothstep(0.36, 0.52, u))
                    smash = _smoothstep(0.34, 0.56, u) * (1.0 - _smoothstep(0.76, 1.0, u))

                    move_pelvis(dy=-0.042 * h * smash + 0.018 * h * wind, dz=0.020 * h * wind - 0.078 * h * smash)
                    rot('Pelvis', x=-0.10 * wind + 0.20 * smash, z=-0.20 * wind + 0.24 * smash)
                    rot('Spine', x=-0.18 * wind + 0.28 * smash, z=-0.22 * wind + 0.28 * smash)
                    rot('Chest', x=-0.22 * wind + 0.26 * smash, z=-0.26 * wind + 0.32 * smash)
                    rot('Head', x=0.16 * wind - 0.18 * smash, z=0.24 * wind - 0.30 * smash)

                    # Hoist shock-cleaver high overhead, then slam edge down into -Y ground arc!
                    rot('Clavicle_R', y=0.24 * wind, z=-0.16 * wind + 0.26 * smash)
                    rot('UpperArm_R', x=-2.18 * wind - 0.72 * smash, y=0.28 * wind - 0.10 * smash, z=-0.18 * wind + 0.32 * smash)
                    rot('Forearm_R', x=-0.52 * wind - 0.18 * smash)
                    aim_hand_world('R', x=-1.45 * wind + 0.42 * smash, y=0.18 * wind - 0.12 * smash, z=-0.20 * wind + 0.22 * smash)

                    # Pull shield-bracer wide on windup, brace tight on smash
                    rot('UpperArm_L', x=-0.85 * wind - 0.32 * smash, y=-0.38 * wind - 0.18 * smash, z=-0.16 * wind - 0.42 * smash)
                    rot('Forearm_L', x=-0.72 * wind - 0.85 * smash)

                    rot('Thigh_L', x=-0.38 * smash - 0.08 * wind, y=-0.05 * smash)
                    rot('Shin_L', x=0.58 * smash + 0.14 * wind)
                    rot('Foot_L', x=-0.18 * smash)
                    rot('Thigh_R', x=0.18 * smash - 0.10 * wind, y=0.05 * smash)
                    rot('Shin_R', x=0.34 * smash + 0.12 * wind)
                    rot('Foot_R', x=-0.12 * smash)

                else:  # gunner melee rifle-butt / barrel shove
                    wind = _smoothstep(0.0, 0.26, u) * (1.0 - _smoothstep(0.26, 0.40, u))
                    strike = _smoothstep(0.24, 0.46, u) * (1.0 - _smoothstep(0.68, 1.0, u))

                    move_pelvis(dy=-0.036 * h * strike + 0.016 * h * wind, dz=-0.032 * h * strike)
                    rot('Pelvis', x=0.10 * strike - 0.05 * wind, z=0.22 * strike - 0.18 * wind)
                    rot('Spine', x=0.14 * strike - 0.06 * wind, z=0.24 * strike - 0.20 * wind)
                    rot('Chest', x=0.12 * strike - 0.06 * wind, z=0.28 * strike - 0.22 * wind)
                    rot('Head', x=-0.08 * strike, z=-0.32 * strike + 0.22 * wind)

                    rot('UpperArm_R', x=0.28 * wind - 1.25 * strike, y=0.24 * wind, z=0.24 * strike)
                    rot('Forearm_R', x=-0.95 * wind - 0.28 * strike)
                    aim_hand_world('R', x=0.25 * wind - 0.18 * strike, y=0.35 * strike, z=0.45 * strike - 0.25 * wind)

                    rot('UpperArm_L', x=-0.52 * wind - 0.85 * strike, z=-0.42 * strike)
                    rot('Forearm_L', x=-0.76)

                    rot('Thigh_L', x=-0.24 * strike)
                    rot('Shin_L', x=0.32 * strike)
                    rot('Thigh_R', x=0.18 * strike)
                    rot('Shin_R', x=0.20 * strike)

            elif clip_title == 'HitReact':
                hit = math.sin(math.pi * (u ** 0.58))
                move_pelvis(dy=0.036 * h * hit, dz=-0.028 * h * hit)
                rot('Pelvis', x=-0.10 * hit, z=-0.10 * hit)
                rot('Spine', x=-0.24 * hit, y=0.06 * hit, z=-0.16 * hit)
                rot('Chest', x=-0.22 * hit, y=0.08 * hit, z=-0.20 * hit)
                rot('Neck', x=-0.14 * hit)
                rot('Head', x=-0.26 * hit, y=-0.10 * hit, z=0.18 * hit)

                for side, sign in (('L', 1.0), ('R', -1.0)):
                    rot(f'UpperArm_{side}', x=-0.28 * hit, y=-sign * 0.34 * hit)
                    rot(f'Forearm_{side}', x=-0.60 * hit)
                    rot(f'Thigh_{side}', x=-0.16 * hit)
                    rot(f'Shin_{side}', x=0.28 * hit)

            elif clip_title == 'Death':
                buckle = _smoothstep(0.0, 0.36, u) * (1.0 - _smoothstep(0.45, 0.82, u))
                fall = _smoothstep(0.30, 0.86, u)
                drop_z = -(pelvis_z - 0.145 * h) * fall - 0.18 * h * buckle
                move_pelvis(dy=-0.08 * h * fall + 0.03 * h * buckle, dz=drop_z)

                rot('Pelvis', x=-0.14 * buckle + 1.48 * fall, y=0.05 * fall, z=0.05 * fall)
                rot('Spine', x=-0.20 * buckle - 0.02 * fall, z=-0.12 * buckle)
                rot('Chest', x=-0.16 * buckle - 0.03 * fall, z=0.06 * fall)
                rot('Neck', x=-0.06 * fall)
                rot('Head', x=-0.20 * buckle - 0.10 * fall, z=0.42 * fall)

                for side, sign in (('L', 1.0), ('R', -1.0)):
                    rot(f'Thigh_{side}', x=-0.56 * buckle + 0.06 * fall, y=-sign * 0.09 * fall)
                    rot(f'Shin_{side}', x=1.08 * buckle + 0.02 * fall)
                    rot(f'Foot_{side}', x=-0.25 * buckle + 0.45 * fall)
                    rot(f'UpperArm_{side}', x=-0.38 * buckle - 0.06 * fall, y=-sign * (0.28 * buckle + 0.56 * fall))
                    rot(f'Forearm_{side}', x=-0.65 * buckle - 0.18 * fall, y=-sign * 0.18 * fall, z=-sign * 0.52 * fall)
                # Lay right-hand weapon flat alongside body so not even its tip dips below z=0 ground
                aim_hand_world('R', x=1.56 * fall, y=-1.18 * fall, z=0.55 * fall)

            key_all(f + 1)

        for fc in action.fcurves:
            for kp in fc.keyframe_points:
                kp.interpolation = 'LINEAR'

    rig.animation_data.action = None
    reset_pose()
    bpy.context.scene.frame_set(1)


# ---------------------------------------------------------------------------
# 1. Neon Viper Street Punk — public/street/foe-punk.glb
# ---------------------------------------------------------------------------

def build_foe_punk() -> Path:
    """Sculpt and rig the Neon Viper Street Punk -> public/street/foe-punk.glb."""
    aa.reset_scene()
    lm = rig_landmarks(height=1.78, shoulder_width=0.218, hip_width=0.106)
    C = PUNK_COLORS

    body_parts: list[bpy.types.Object] = []
    sig_parts: list[bpy.types.Object] = []

    hz = lm['head_z']     # 1.495
    cz = lm['crown_z']    # 1.780
    span = cz - hz        # 0.285

    # --- A. Cranium, Neon Mohawk & Viper Respirator Mask ---
    neck_rings = [
        _ring_oval(0.0, 0.008, lm['neck_z'] - 0.036, 0.064, 0.058, 0.062, segs=10),
        _ring_oval(0.0, 0.003, lm['neck_z'] + 0.022, 0.054, 0.052, 0.056, segs=10),
        _ring_oval(0.0, 0.000, hz + 0.018, 0.055, 0.053, 0.055, segs=10),
    ]
    body_parts.append(build_lofted_tube(
        neck_rings,
        name='Punk_Neck',
        color=C['skin'],
        gradient_z=(C['skin_shadow'], C['skin']),
        ao_strength=0.26,
        ground_ao=0.02,
        role='torso',
    ))

    skull_rings = [
        _ring_oval(0.0, -0.036, hz - 0.015, 0.040, 0.052, 0.042, segs=12, squareness=0.18),
        _ring_oval(0.0, -0.016, hz + 0.14 * span, 0.068, 0.088, 0.070, segs=12, squareness=0.18),
        _ring_oval(0.0, -0.006, hz + 0.34 * span, 0.078, 0.098, 0.086, segs=12, squareness=0.18),
        _ring_oval(0.0,  0.000, hz + 0.54 * span, 0.080, 0.100, 0.094, segs=12, squareness=0.16),
        _ring_oval(0.0,  0.006, hz + 0.76 * span, 0.074, 0.088, 0.090, segs=12, squareness=0.12),
        _ring_oval(0.0,  0.010, hz + 0.94 * span, 0.046, 0.055, 0.058, segs=12),
    ]
    cranium = build_lofted_tube(
        skull_rings,
        name='Punk_Cranium',
        color=C['skin'],
        ao_strength=0.22,
        ground_ao=0.02,
        bone='Head',
    )
    # Shaved cyber-temples & back-of-head buzzcut
    aa.paint_faces(
        cranium,
        lambda _p, c, _n: c.y > -0.020 or c.z > hz + 0.62 * span,
        color=C['buzz_scalp'],
        ao_strength=0.22,
    )
    body_parts.append(cranium)

    ear_L = _bevel_box(
        name='Punk_Ear_L',
        size=(0.018, 0.028, 0.042),
        location=(0.082, 0.004, hz + 0.38 * span),
        rotation=(0.08, 0.0, -0.14),
        bevel=0.004,
        segments=1,
        color=C['skin'],
        bone='Head',
    )
    body_parts.extend([ear_L, mirror_clean(ear_L)])

    # Viper Respirator Mask (faceted nose-jaw shell + twin angled cheek filter canisters)
    resp_prof_yz = [
        (-0.082, hz + 0.42 * span),
        (-0.128, hz + 0.28 * span),
        (-0.132, hz + 0.06 * span),
        (-0.096, hz - 0.08 * span),
        (-0.048, hz - 0.05 * span),
        (-0.052, hz + 0.32 * span),
    ]
    body_parts.append(aa.add_extruded_profile(
        resp_prof_yz,
        depth=0.092,
        axis='X',
        name='Punk_RespiratorCore',
        bevel=0.006,
        bevel_segments=1,
        shade_smooth=False,
        color=C['cyber_dark'],
        ao_strength=0.26,
        bone='Head',
    ))

    # Glowing crimson respirator front vox-slit
    sig_parts.append(_box(
        name='Punk_RespGrill_Signal',
        size=(0.048, 0.010, 0.016),
        location=(0.0, -0.131, hz + 0.14 * span),
        rotation=(0.14, 0.0, 0.0),
        color=C['sig_crimson'],
        signal=True,
        bone='Head',
    ))

    # Twin angled cheek filter canisters (L & R) with chrome caps + glowing Signal filter rings
    filter_rot_L = Euler((0.42, 0.58, -0.52), 'XYZ')
    filter_pos_L = Vector((0.066, -0.096, hz + 0.10 * span))
    can_L = aa.add_cylinder(
        name='Punk_RespCanister_L',
        radius=0.024,
        depth=0.036,
        vertices=10,
        location=filter_pos_L,
        rotation=filter_rot_L,
        bevel=0.003,
        color=C['cyber_dark'],
        bone='Head',
    )
    cap_L = aa.add_cylinder(
        name='Punk_RespCap_L',
        radius=0.021,
        depth=0.008,
        vertices=10,
        location=filter_pos_L + (filter_rot_L.to_matrix() @ Vector((0.0, 0.0, 0.020))),
        rotation=filter_rot_L,
        color=C['spike_chrome'],
        bone='Head',
    )
    ring_sig_L = aa.add_cylinder(
        name='Punk_RespGlowRing_L',
        radius=0.025,
        depth=0.007,
        vertices=10,
        location=filter_pos_L + (filter_rot_L.to_matrix() @ Vector((0.0, 0.0, 0.011))),
        rotation=filter_rot_L,
        color=C['sig_crimson'],
        signal=True,
        bone='Head',
    )
    body_parts.extend([can_L, cap_L, mirror_clean(can_L), mirror_clean(cap_L)])
    sig_parts.extend([ring_sig_L, mirror_clean(ring_sig_L)])

    # Hot-Pink / Crimson Wrap-Around Cyber-Visor ('Signal') + Gunmetal Brow Bar
    visor_z = hz + 0.52 * span
    v_outer, v_inner = [], []
    for r_i, (dz, dy) in enumerate(((-0.018, 0.0), (0.002, -0.006), (0.020, -0.002))):
        ro, ri = [], []
        for c_i in range(7):
            u = (c_i / 6.0) * 2.0 - 1.0
            ang = u * 0.92
            v_notch = -0.010 * (1.0 - min(1.0, abs(u) * 2.2)) if r_i == 0 else 0.0
            xo = math.sin(ang) * 0.090
            yo = -math.cos(ang) * 0.112 + dy
            zo = visor_z + dz + v_notch + 0.009 * (u * u)
            ro.append((xo, yo, zo))
            ri.append((xo * 0.91, yo + 0.011, zo))
        v_outer.append(ro)
        v_inner.append(ri)
    sig_parts.append(build_thick_shell(
        v_outer,
        v_inner,
        name='Punk_Visor_Signal',
        outer_color=C['sig_pink'],
        signal=True,
        shade_smooth=True,
        bone='Head',
    ))
    body_parts.append(_bevel_box(
        name='Punk_VisorBrow',
        size=(0.168, 0.044, 0.015),
        location=(0.0, -0.092, visor_z + 0.022),
        rotation=(-0.12, 0.0, 0.0),
        bevel=0.003,
        segments=1,
        color=C['cyber_dark'],
        bone='Head',
    ))

    # Spiked Neon Mohawk (7 fanned blades sweeping from forehead over crown to occiput)
    mohawk_center_y = 0.005
    mohawk_center_z = hz + 0.42 * span
    for m_i in range(7):
        t_m = m_i / 6.0
        fan_ang = _lerp(-0.58, 1.48, t_m)  # Sweeps from -Y forehead over +Z crown to +Y occiput
        dir_y = math.sin(fan_ang)
        dir_z = math.cos(fan_ang)
        tan_y = math.cos(fan_ang)
        tan_z = -math.sin(fan_ang)

        r_root = 0.102
        r_mid = 0.158 + 0.022 * math.sin(math.pi * t_m)
        r_tip = 0.215 + 0.036 * math.sin(math.pi * t_m)  # Peaks at ~1.86m!
        half_chord = 0.026

        # Dark crimson-plum Mohawk Base ('Body')
        base_yz = [
            (mohawk_center_y + dir_y * r_root - tan_y * half_chord, mohawk_center_z + dir_z * r_root - tan_z * half_chord),
            (mohawk_center_y + dir_y * r_root + tan_y * half_chord, mohawk_center_z + dir_z * r_root + tan_z * half_chord),
            (mohawk_center_y + dir_y * r_mid + tan_y * (half_chord * 0.72), mohawk_center_z + dir_z * r_mid + tan_z * (half_chord * 0.72)),
            (mohawk_center_y + dir_y * r_mid - tan_y * (half_chord * 0.72), mohawk_center_z + dir_z * r_mid - tan_z * (half_chord * 0.72)),
        ]
        body_parts.append(aa.add_extruded_profile(
            base_yz,
            depth=0.024,
            axis='X',
            name=f'Punk_MohawkBase_{m_i}',
            shade_smooth=False,
            color=C['mohawk_root'],
            ao_strength=0.24,
            bone='Head',
        ))
        # Blazing Hot-Pink Mohawk Spike Tip ('Signal')
        tip_yz = [
            (mohawk_center_y + dir_y * (r_mid - 0.004) - tan_y * (half_chord * 0.72), mohawk_center_z + dir_z * (r_mid - 0.004) - tan_z * (half_chord * 0.72)),
            (mohawk_center_y + dir_y * (r_mid - 0.004) + tan_y * (half_chord * 0.72), mohawk_center_z + dir_z * (r_mid - 0.004) + tan_z * (half_chord * 0.72)),
            (mohawk_center_y + dir_y * r_tip + tan_y * 0.010, mohawk_center_z + dir_z * r_tip + tan_z * 0.010),
        ]
        sig_parts.append(aa.add_extruded_profile(
            tip_yz,
            depth=0.018,
            axis='X',
            name=f'Punk_MohawkTip_Signal_{m_i}',
            shade_smooth=False,
            color=C['sig_pink'],
            signal=True,
            bone='Head',
        ))

    # --- B. Spiked Cyber-Leather Vest & Undershirt Torso ---
    torso_rings = [
        _ring_oval(0.0, 0.006, lm['pelvis_z'] - 0.010, 0.132, 0.094, 0.102, segs=12, squareness=0.20),
        _ring_oval(0.0, 0.002, lm['spine_z'],          0.128, 0.092, 0.100, segs=12, squareness=0.22),
        _ring_oval(0.0, -0.004, lm['chest_z'],         0.152, 0.112, 0.114, segs=12, squareness=0.25),
        _ring_oval(0.0, -0.002, lm['sh_z'],            0.162, 0.106, 0.110, segs=12, squareness=0.24),
        _ring_oval(0.0, 0.004, lm['neck_z'] + 0.008,   0.074, 0.068, 0.072, segs=12),
    ]
    body_parts.append(build_lofted_tube(
        torso_rings,
        name='Punk_MeshTorso',
        color=C['mesh_shirt'],
        ao_strength=0.28,
        role='torso',
    ))
    body_parts.append(build_pelvis_saddle(lm, C['jeans'], 'Punk_PelvisSaddle'))

    # Cropped Sleeveless Spiked Cyber-Leather Vest (open on -Y front)
    vest_stations = [
        (lm['spine_z'] - 0.035, 0.148, 0.110, 0.116,  0.002, 0.42),
        (lm['spine_z'] + 0.055, 0.154, 0.116, 0.120,  0.000, 0.38),
        (lm['chest_z'] + 0.015, 0.172, 0.128, 0.130, -0.004, 0.34),
        (lm['sh_z'] + 0.018,    0.180, 0.122, 0.126, -0.002, 0.38),
        (lm['neck_z'] + 0.032,  0.118, 0.098, 0.106,  0.006, 0.48),
    ]
    vest = build_open_vest_shell(
        vest_stations,
        thickness=0.018,
        arc_segments=11,
        name='Punk_CyberVest',
        color=C['vest_leather'],
        inner_color=C['vest_plum'],
        shade_smooth=False,
        ao_strength=0.28,
        role='torso',
    )
    # Paint upper shoulder yoke in venom-plum
    aa.paint_faces(
        vest,
        lambda _p, c, n: c.z > lm['chest_z'] + 0.065 and n.z > 0.15,
        color=C['vest_plum'],
        ao_strength=0.22,
    )
    body_parts.append(vest)

    # Wide asymmetric studded biker lapels (L & R) hugging the vest front
    lapel_L = _bevel_box(
        name='Punk_BikerLapel_L',
        size=(0.060, 0.018, 0.235),
        location=(0.082, -0.114, lm['chest_z'] + 0.015),
        rotation=(-0.08, -0.14, -0.22),
        taper=(1.24, 1.0),
        bevel=0.004,
        segments=1,
        color=C['vest_plum'],
        bone='Chest',
    )
    body_parts.extend([lapel_L, mirror_clean(lapel_L)])

    # Dorsal Neon Viper glowing V-chevron emblem on back of vest
    for side_s in (-1.0, 1.0):
        sig_parts.append(_box(
            name=f'Punk_BackViperChevron_{side_s:+.0f}',
            size=(0.020, 0.012, 0.145),
            location=(side_s * 0.046, 0.132, lm['chest_z'] + 0.035),
            rotation=(0.05, side_s * 0.46, 0.0),
            color=C['sig_pink'],
            signal=True,
            bone='Chest',
        ))

    # 3 Razor Chrome Shoulder Spikes per vest shoulder ridge (Clavicle_L & Clavicle_R)
    sh_spikes_L = []
    for sp_i, (sx_o, sy_o, sz_o, tilt_y) in enumerate((
        (0.118, -0.018, lm['sh_z'] + 0.048, -0.22),
        (0.152,  0.000, lm['sh_z'] + 0.042, -0.38),
        (0.182,  0.016, lm['sh_z'] + 0.030, -0.55),
    )):
        sh_spikes_L.append(aa.add_cone(
            name=f'Punk_ShoulderSpike_{sp_i}_L',
            radius1=0.016,
            radius2=0.0015,
            depth=0.062,
            vertices=6,
            location=(sx_o, sy_o, sz_o + 0.024),
            rotation=(0.0, -tilt_y, 0.0),
            shade_smooth=False,
            color=C['spike_chrome'],
            bone='Clavicle_L',
        ))
    body_parts.extend(sh_spikes_L)
    body_parts.extend(mirror_clean(sh_spikes_L))

    # Studded biker belt + chrome Viper fang buckle
    body_parts.append(build_lofted_tube(
        [
            _ring_oval(0.0, 0.004, lm['pelvis_z'] + 0.020, 0.144, 0.104, 0.112, segs=12, squareness=0.25),
            _ring_oval(0.0, 0.004, lm['pelvis_z'] + 0.064, 0.142, 0.102, 0.110, segs=12, squareness=0.25),
        ],
        name='Punk_StuddedBelt',
        color=C['vest_leather'],
        shade_smooth=False,
        bone='Pelvis',
    ))
    body_parts.append(_bevel_box(
        name='Punk_BeltBuckle',
        size=(0.066, 0.024, 0.052),
        location=(0.0, -0.108, lm['pelvis_z'] + 0.042),
        bevel=0.004,
        segments=1,
        color=C['spike_chrome'],
        bone='Pelvis',
    ))

    # --- C. Asymmetric Arms: Bare Muscular Left Arm + Cybernetic Right Forearm! ---
    # Left bare muscular arm (+X) with spiked leather wrist cuff & biker glove
    body_parts.append(_oriented_chain_loft(
        [
            (lm['shoulder_L'] + Vector((-0.018, 0.0, 0.008)), 0.052, 0.054),
            (_vlerp(lm['shoulder_L'], lm['elbow_L'], 0.48), 0.048, 0.052),
            (lm['elbow_L'], 0.041, 0.043),
            (_vlerp(lm['elbow_L'], lm['wrist_L'], 0.55), 0.039, 0.042),
            (lm['wrist_L'], 0.031, 0.033),
        ],
        name='Punk_BareArm_L',
        segs=9,
        color=C['skin'],
        gradient_z=(C['skin_shadow'], C['skin']),
        ao_strength=0.24,
        role='arm',
    ))
    fore_dir_L = (lm['wrist_L'] - lm['elbow_L']).normalized()
    fore_rot_L = Vector((0.0, 0.0, 1.0)).rotation_difference(fore_dir_L)
    body_parts.append(aa.add_cylinder(
        name='Punk_SpikedWristband_L',
        radius=0.038,
        radius_top=0.041,
        depth=0.056,
        vertices=8,
        location=_vlerp(lm['elbow_L'], lm['wrist_L'], 0.84),
        rotation=fore_rot_L,
        shade_smooth=False,
        color=C['vest_leather'],
        bone='Forearm_L',
    ))
    body_parts.extend(build_combat_hand(
        lm['wrist_L'],
        lm['hand_L'],
        side='L',
        palm_color=C['vest_leather'],
        finger_color=C['skin'],
        name_prefix='Punk',
    ))

    # Right Arm (-X): cyber-socket shoulder ring + bicep + Cybernetic Right Forearm!
    body_parts.append(_oriented_chain_loft(
        [
            (lm['shoulder_R'] + Vector((0.018, 0.0, 0.008)), 0.054, 0.056),
            (_vlerp(lm['shoulder_R'], lm['elbow_R'], 0.52), 0.049, 0.053),
            (lm['elbow_R'] + Vector((0.0, 0.0, 0.012)), 0.044, 0.046),
        ],
        name='Punk_UpperArm_R',
        segs=9,
        color=C['skin'],
        ao_strength=0.24,
        bone='UpperArm_R',
    ))
    # Segmented Brushed-Chrome Cybernetic Right Forearm ('Forearm_R')
    fore_dir_R = (lm['wrist_R'] - lm['elbow_R']).normalized()
    fore_rot_R = Vector((0.0, 0.0, 1.0)).rotation_difference(fore_dir_R)

    cyber_fore = _oriented_chain_loft(
        [
            (lm['elbow_R'] + Vector((0.0, 0.0, 0.016)), 0.050, 0.054),
            (_vlerp(lm['elbow_R'], lm['wrist_R'], 0.30), 0.053, 0.058),
            (_vlerp(lm['elbow_R'], lm['wrist_R'], 0.68), 0.046, 0.050),
            (lm['wrist_R'] + Vector((0.0, 0.0, -0.006)), 0.039, 0.043),
        ],
        name='Punk_CyberForearm_R',
        segs=8,
        color=C['cyber_chrome'],
        shade_smooth=False,
        ao_strength=0.25,
        bone='Forearm_R',
    )
    # Dark gunmetal elbow joint & wrist servo bands
    aa.paint_faces(
        cyber_fore,
        lambda _p, c, _n: c.z > lm['elbow_R'].z - 0.025 or c.z < lm['wrist_R'].z + 0.032,
        color=C['cyber_dark'],
        ao_strength=0.24,
    )
    body_parts.append(cyber_fore)

    # Cyber-elbow rotary drum + outer hydraulic piston rod + glowing Signal conduit
    body_parts.append(aa.add_cylinder(
        name='Punk_CyberElbowDrum_R',
        radius=0.034,
        depth=0.104,
        vertices=10,
        location=lm['elbow_R'],
        rotation=(0.0, math.pi * 0.5, 0.0),
        shade_smooth=False,
        color=C['cyber_dark'],
        bone='Forearm_R',
    ))
    body_parts.append(aa.add_limb(
        lm['elbow_R'] + Vector((-0.046, 0.018, -0.012)),
        lm['wrist_R'] + Vector((-0.038, 0.010, 0.024)),
        radius0=0.012,
        radius1=0.010,
        segments=6,
        name='Punk_CyberPiston_R',
        color=C['spike_chrome'],
        bone='Forearm_R',
    ))
    sig_parts.append(_box(
        name='Punk_CyberArmConduit_Signal_R',
        size=(0.012, 0.018, 0.165),
        location=_vlerp(lm['elbow_R'], lm['wrist_R'], 0.50) + Vector((-0.046, -0.016, 0.0)),
        rotation=fore_rot_R,
        color=C['sig_pink'],
        signal=True,
        bone='Forearm_R',
    ))

    body_parts.extend(build_combat_hand(
        lm['wrist_R'],
        lm['hand_R'],
        side='R',
        palm_color=C['cyber_dark'],
        finger_color=C['cyber_chrome'],
        name_prefix='PunkCyber',
    ))

    # --- D. Glowing Plasma Tanto Knife in Hand_R! ---
    # Centered inside Hand_R grip, blade extending forward (-Y) and +14 deg up (+Z)
    grip_R = lm['wrist_R'] + Vector((0.008, -0.018, -0.056))
    knife_rot = Euler((1.32, 0.0, 0.0), 'XYZ')  # Local +Z -> (0, -0.969, +0.247) forward-up!
    knife_mat = Matrix.LocRotScale(grip_R, knife_rot, Vector((1.0, 1.0, 1.0)))

    def _kpt(lx: float, ly: float, lz: float) -> Vector:
        return knife_mat @ Vector((lx, ly, lz))

    # Ribbed tsuka handle (-0.095 .. +0.075 along local Z) + pommel ring + hexagonal guard
    body_parts.append(aa.add_box(
        name='Punk_TantoGrip',
        size=(0.024, 0.034, 0.170),
        location=_kpt(0.0, 0.0, -0.010),
        rotation=knife_rot,
        bevel=0.004,
        bevel_segments=1,
        color=C['cyber_dark'],
        bone='Hand_R',
    ))
    body_parts.append(aa.add_cylinder(
        name='Punk_TantoPommel',
        radius=0.020,
        depth=0.020,
        vertices=8,
        location=_kpt(0.0, 0.0, -0.102),
        rotation=knife_rot,
        shade_smooth=False,
        color=C['spike_chrome'],
        bone='Hand_R',
    ))
    body_parts.append(aa.add_bevel_box(
        name='Punk_TantoGuard',
        size=(0.048, 0.078, 0.022),
        location=_kpt(0.0, -0.004, 0.082),
        rotation=knife_rot,
        bevel=0.004,
        segments=1,
        color=C['cyber_chrome'],
        bone='Hand_R',
    ))
    # Metallic tanto mune spine (back edge +Y in blade space)
    spine_prof_yz = [
        (0.002, 0.092),
        (0.022, 0.092),
        (0.020, 0.445),
        (0.004, 0.515),  # Chisel kissaki point!
        (-0.004, 0.435),
    ]
    body_parts.append(aa.add_extruded_profile(
        spine_prof_yz,
        depth=0.014,
        axis='X',
        name='Punk_TantoSpine',
        location=grip_R,
        rotation=knife_rot,
        shade_smooth=False,
        color=C['spike_chrome'],
        bone='Hand_R',
    ))
    # Superheated Hot-Pink Plasma Tanto Cutting Edge ('Signal', pinned to Hand_R!)
    plasma_prof_yz = [
        (-0.002, 0.094),
        (-0.026, 0.102),
        (-0.028, 0.425),
        ( 0.004, 0.532),  # Blazing razor needle tip!
        ( 0.003, 0.440),
    ]
    sig_parts.append(aa.add_extruded_profile(
        plasma_prof_yz,
        depth=0.009,
        axis='X',
        name='Punk_TantoPlasmaEdge_Signal',
        location=grip_R,
        rotation=knife_rot,
        shade_smooth=False,
        color=C['sig_pink'],
        signal=True,
        bone='Hand_R',
    ))

    # --- E. Slim Punk Jeans, Asymmetric Knee Armor & Stomper Combat Boots ---
    jean_L = _oriented_chain_loft(
        [
            (lm['hip_L'] + Vector((-0.012, 0.0, 0.022)), 0.068, 0.076),
            (_vlerp(lm['hip_L'], lm['knee_L'], 0.52), 0.058, 0.064),
            (lm['knee_L'], 0.050, 0.054),
            (_vlerp(lm['knee_L'], lm['ankle_L'], 0.56), 0.045, 0.049),
            (lm['ankle_L'] + Vector((0.0, 0.0, -0.010)), 0.038, 0.042),
        ],
        name='Punk_Jeans_L',
        segs=8,
        color=C['jeans'],
        shade_smooth=False,
        ao_strength=0.26,
        role='leg',
    )
    body_parts.extend([jean_L, mirror_clean(jean_L)])

    # Asymmetric right knee guard + left thigh anarchy strap
    body_parts.append(_bevel_box(
        name='Punk_KneePad_R',
        size=(0.094, 0.052, 0.108),
        location=(lm['knee_L'].x * -1.0, lm['knee_L'].y - 0.040, lm['knee_L'].z + 0.008),
        rotation=(-0.08, 0.0, 0.0),
        bevel=0.006,
        segments=1,
        color=C['cyber_dark'],
        bone='Shin_R',
    ))
    body_parts.append(_bevel_box(
        name='Punk_ThighStrap_L',
        size=(0.132, 0.138, 0.032),
        location=(lm['hip_L'].x + 0.006, -0.004, _lerp(lm['hip_z'], lm['knee_z'], 0.45)),
        rotation=(0.0, 0.16, 0.0),
        bevel=0.004,
        segments=1,
        color=C['jeans_strap'],
        bone='Thigh_L',
    ))

    boots_L = build_grounded_boot_L(
        ankle_x=lm['ankle_x'],
        boot_height=0.215,
        sole_color=C['cyber_dark'],
        upper_color=C['boot_leather'],
        toe_cap_color=C['spike_chrome'],
        buckle_color=C['spike_chrome'],
        width_scale=1.02,
        name_prefix='PunkBoot',
    )
    body_parts.extend(boots_L)
    body_parts.extend(mirror_clean(boots_L))

    # --- Batch, Skin, Animate & Export ---
    merged = aa.combine_by_role(body_parts, sig_parts)
    body_mesh = next(m for m in merged if m.name == 'Body')
    apply_character_seam_ao(
        body_mesh,
        seam_zs=[
            (lm['spine_z'] - 0.035, 0.025, 0.20),
            (lm['pelvis_z'] + 0.020, 0.024, 0.22),
            (0.215, 0.022, 0.20),
        ],
        crotch_z=lm['pelvis_z'],
    )
    role_snap = snapshot_skin_roles(merged)
    rig = aa.build_humanoid_rig_and_clips(
        merged,
        rig_name='PunkRig',
        height=lm['h'],
        style='punk',
        attack_clip_name='Punch',
        weapon_hand=None,
        shoulder_width=lm['sh_x'],
        hip_width=lm['hp_x'],
    )
    apply_infantry_skinning(rig, merged, role_snap, height=lm['h'])
    author_infantry_clips(rig, archetype='punk', height=lm['h'])

    out_path = OUT_DIR / 'foe-punk.glb'
    info = aa.export_glb(out_path, [rig, *merged], min_tris=1900, max_meshes=6)
    print(f"[build_aa_infantry] Exported {info['path']}: {info['triangles']} tris, {info['meshes']} meshes, {info['size_kb']} KB")
    return out_path


# ---------------------------------------------------------------------------
# 2. Aethelgard Riot Enforcer — public/street/foe-enforcer.glb
# ---------------------------------------------------------------------------

def build_foe_enforcer() -> Path:
    """Sculpt and rig the Aethelgard Riot Enforcer -> public/street/foe-enforcer.glb."""
    aa.reset_scene()
    lm = rig_landmarks(height=1.85, shoulder_width=0.256, hip_width=0.124)
    C = ENFORCER_COLORS

    body_parts: list[bpy.types.Object] = []
    sig_parts: list[bpy.types.Object] = []

    hz = lm['head_z']     # 1.554
    cz = lm['crown_z']    # 1.850
    span = cz - hz        # 0.296

    # --- A. Full-Face Security Helmet with Glowing Crimson T-Visor ---
    body_parts.append(build_lofted_tube(
        [
            _ring_oval(0.0, 0.006, lm['neck_z'] - 0.035, 0.088, 0.082, 0.088, segs=10, squareness=0.25),
            _ring_oval(0.0, 0.002, hz + 0.015,           0.076, 0.072, 0.078, segs=10, squareness=0.25),
        ],
        name='Enforcer_NeckSeal',
        color=C['under_suit'],
        shade_smooth=False,
        role='torso',
    ))

    # Faceted heavy tactical security helmet dome
    helm_rings = [
        _ring_oval(0.0, -0.012, hz - 0.025,          0.078, 0.098, 0.088, segs=12, squareness=0.35),
        _ring_oval(0.0, -0.010, hz + 0.16 * span,    0.098, 0.118, 0.106, segs=12, squareness=0.38),
        _ring_oval(0.0, -0.004, hz + 0.38 * span,    0.106, 0.124, 0.116, segs=12, squareness=0.36),
        _ring_oval(0.0,  0.000, hz + 0.62 * span,    0.105, 0.122, 0.118, segs=12, squareness=0.32),
        _ring_oval(0.0,  0.006, hz + 0.84 * span,    0.086, 0.100, 0.104, segs=12, squareness=0.26),
        _ring_oval(0.0,  0.010, hz + 0.98 * span,    0.048, 0.056, 0.060, segs=12, squareness=0.20),
    ]
    helmet = build_lofted_tube(
        helm_rings,
        name='Enforcer_RiotHelmet',
        color=C['armor_slate'],
        shade_smooth=False,
        ao_strength=0.26,
        bone='Head',
    )
    # Dark recess behind T-visor
    aa.paint_faces(
        helmet,
        lambda _p, c, n: n.y < -0.55 and hz + 0.12 * span < c.z < hz + 0.62 * span and abs(c.x) < 0.075,
        color=C['under_suit'],
        ao_strength=0.18,
    )
    body_parts.append(helmet)

    # Central helmet dorsal armor crest + crimson authority stripe + mandible cheek armor
    crest = _bevel_box(
        name='Enforcer_HelmCrest',
        size=(0.042, 0.215, 0.032),
        location=(0.0, -0.002, cz - 0.006),
        rotation=(0.08, 0.0, 0.0),
        bevel=0.005,
        segments=1,
        color=C['crimson_trim'],
        bone='Head',
    )
    mandible_L = _bevel_box(
        name='Enforcer_Mandible_L',
        size=(0.044, 0.095, 0.082),
        location=(0.068, -0.082, hz + 0.20 * span),
        rotation=(0.12, -0.14, -0.24),
        bevel=0.005,
        segments=1,
        color=C['armor_light'],
        bone='Head',
    )
    nape_skirt = _bevel_box(
        name='Enforcer_HelmNapeGuard',
        size=(0.175, 0.038, 0.065),
        location=(0.0, 0.106, hz + 0.05 * span),
        rotation=(-0.26, 0.0, 0.0),
        bevel=0.004,
        segments=1,
        color=C['armor_slate'],
        bone='Head',
    )
    body_parts.extend([crest, nape_skirt, mandible_L, mirror_clean(mandible_L)])

    # Unmistakable Glowing Crimson T-Visor ('Signal', pinned to Head!)
    t_brow_z = hz + 0.52 * span
    tb_outer, tb_inner = [], []
    for dz, dy in ((-0.015, 0.0), (0.015, -0.003)):
        ro, ri = [], []
        for c_i in range(7):
            u = (c_i / 6.0) * 2.0 - 1.0
            ang = u * 0.78
            xo = math.sin(ang) * 0.096
            yo = -math.cos(ang) * 0.130 + dy
            zo = t_brow_z + dz
            ro.append((xo, yo, zo))
            ri.append((xo * 0.92, yo + 0.014, zo))
        tb_outer.append(ro)
        tb_inner.append(ri)
    sig_parts.append(build_thick_shell(
        tb_outer,
        tb_inner,
        name='Enforcer_TVisor_Brow_Signal',
        outer_color=C['sig_crimson'],
        signal=True,
        shade_smooth=False,
        bone='Head',
    ))
    sig_parts.append(_box(
        name='Enforcer_TVisor_Stem_Signal',
        size=(0.026, 0.020, 0.112),
        location=(0.0, -0.128, hz + 0.30 * span),
        rotation=(0.06, 0.0, 0.0),
        color=C['sig_crimson'],
        signal=True,
        bone='Head',
    ))

    # --- B. Segmented Tactical Chestplate, Gorget & Heavy Utility Belt ---
    body_parts.append(build_lofted_tube(
        [
            _ring_oval(0.0, 0.006, lm['pelvis_z'] - 0.010, 0.156, 0.108, 0.118, segs=12, squareness=0.28),
            _ring_oval(0.0, 0.002, lm['spine_z'],          0.154, 0.106, 0.116, segs=12, squareness=0.28),
            _ring_oval(0.0, -0.004, lm['chest_z'],         0.184, 0.128, 0.132, segs=12, squareness=0.32),
            _ring_oval(0.0, 0.000, lm['sh_z'] + 0.010,     0.194, 0.122, 0.128, segs=12, squareness=0.30),
            _ring_oval(0.0, 0.004, lm['neck_z'] + 0.012,   0.102, 0.092, 0.098, segs=12, squareness=0.24),
        ],
        name='Enforcer_UnderTorso',
        color=C['under_suit'],
        shade_smooth=False,
        ao_strength=0.28,
        role='torso',
    ))
    body_parts.append(build_pelvis_saddle(lm, C['under_suit'], 'Enforcer_PelvisSaddle'))

    # Tier 1: Heavy Pectoral Riot Cuirass + High Throat Gorget + Dorsal Power Pack ('Chest')
    cuirass_rings = [
        _ring_oval(0.0, -0.004, lm['chest_z'] - 0.055, 0.186, 0.142, 0.144, segs=12, squareness=0.44),
        _ring_oval(0.0, -0.008, lm['chest_z'] + 0.055, 0.212, 0.162, 0.156, segs=12, squareness=0.46),
        _ring_oval(0.0, -0.004, lm['sh_z'] + 0.025,    0.206, 0.148, 0.148, segs=12, squareness=0.42),
        _ring_oval(0.0,  0.002, lm['neck_z'] + 0.024,  0.128, 0.114, 0.118, segs=12, squareness=0.34),
    ]
    cuirass = build_lofted_tube(
        cuirass_rings,
        name='Enforcer_TacticalCuirass',
        color=C['armor_slate'],
        shade_smooth=False,
        ao_strength=0.26,
        bone='Chest',
    )
    aa.paint_faces(
        cuirass,
        lambda _p, c, n: n.y < -0.45 and abs(c.x) < 0.095 and lm['chest_z'] - 0.01 < c.z < lm['sh_z'] + 0.01,
        color=C['armor_light'],
        ao_strength=0.20,
    )
    body_parts.append(cuirass)

    # Angled pectoral strike plates + crimson authority chest chevron + dorsal power-pack
    pec_L = _bevel_box(
        name='Enforcer_PecStrikePlate_L',
        size=(0.106, 0.036, 0.118),
        location=(0.072, -0.156, lm['chest_z'] + 0.048),
        rotation=(0.14, 0.16, -0.12),
        bevel=0.006,
        segments=1,
        color=C['crimson_trim'],
        bone='Chest',
    )
    body_parts.extend([pec_L, mirror_clean(pec_L)])

    body_parts.append(_bevel_box(
        name='Enforcer_DorsalPack',
        size=(0.210, 0.072, 0.220),
        location=(0.0, 0.162, lm['chest_z'] + 0.045),
        bevel=0.008,
        segments=1,
        taper=(0.88, 0.90),
        color=C['armor_light'],
        bone='Chest',
    ))
    sig_parts.append(_box(
        name='Enforcer_DorsalIFF_Signal',
        size=(0.110, 0.014, 0.020),
        location=(0.0, 0.198, lm['chest_z'] + 0.095),
        color=C['sig_crimson'],
        signal=True,
        bone='Chest',
    ))

    # Tier 2 & 3: Segmented Abdominal Armor Lames (overlapping horizontal bands)
    for lame_i, lz in enumerate((lm['spine_z'] + 0.082, lm['spine_z'] + 0.008)):
        rx_l = 0.174 - 0.008 * lame_i
        ry_l = 0.132 - 0.006 * lame_i
        body_parts.append(build_lofted_tube(
            [
                _ring_oval(0.0, -0.002, lz - 0.034, rx_l, ry_l, ry_l + 0.006, segs=12, squareness=0.42),
                _ring_oval(0.0, -0.002, lz + 0.034, rx_l + 0.006, ry_l + 0.005, ry_l + 0.010, segs=12, squareness=0.42),
            ],
            name=f'Enforcer_AbLame_{lame_i}',
            color=C['armor_slate'] if lame_i == 0 else C['armor_light'],
            shade_smooth=False,
            ao_strength=0.26,
            role='torso',
        ))

    # Heavy Riot Belt + Front Groin Blast-Tasset + Hip Tassets (L & R)
    body_parts.append(build_lofted_tube(
        [
            _ring_oval(0.0, 0.002, lm['pelvis_z'] + 0.018, 0.174, 0.128, 0.136, segs=12, squareness=0.40),
            _ring_oval(0.0, 0.002, lm['pelvis_z'] + 0.078, 0.170, 0.124, 0.132, segs=12, squareness=0.40),
        ],
        name='Enforcer_TacticalBelt',
        color=C['under_suit'],
        shade_smooth=False,
        bone='Pelvis',
    ))
    body_parts.append(_bevel_box(
        name='Enforcer_FrontTasset',
        size=(0.126, 0.032, 0.148),
        location=(0.0, -0.126, lm['pelvis_z'] - 0.032),
        rotation=(-0.08, 0.0, 0.0),
        taper=(1.18, 1.0),
        bevel=0.005,
        segments=1,
        color=C['armor_slate'],
        bone='Pelvis',
    ))
    hip_plate_L = _bevel_box(
        name='Enforcer_HipTasset_L',
        size=(0.038, 0.168, 0.155),
        location=(lm['hp_x'] + 0.068, 0.0, lm['hip_z'] - 0.045),
        rotation=(0.0, -0.16, 0.0),
        bevel=0.006,
        segments=1,
        color=C['armor_slate'],
        bone='Thigh_L',
    )
    body_parts.extend([hip_plate_L, mirror_clean(hip_plate_L)])

    # --- C. Heavy Armored Riot Pauldrons (UpperArm_L & UpperArm_R) ---
    p_outer_L, p_inner_L = [], []
    sh_L = lm['shoulder_L']
    for r_i, (dz, r_rad, x_shift) in enumerate((
        (-0.095, 0.098, 0.038),
        (-0.025, 0.114, 0.026),
        ( 0.048, 0.106, 0.004),
        ( 0.092, 0.072, -0.028),
    )):
        ro, ri = [], []
        for c_i in range(6):
            th = _lerp(-math.pi * 0.44, math.pi * 0.44, c_i / 5.0)
            xo = sh_L.x + x_shift + math.cos(th) * r_rad * 0.88
            yo = math.sin(th) * (r_rad * 1.18)
            zo = sh_L.z + dz
            ro.append((xo, yo, zo))
            ri.append((xo - 0.018 * math.cos(th), yo * 0.86, zo))
        p_outer_L.append(ro)
        p_inner_L.append(ri)

    pauldron_L = build_thick_shell(
        p_outer_L,
        p_inner_L,
        name='Enforcer_RiotPauldron_L',
        outer_color=C['armor_slate'],
        inner_color=C['under_suit'],
        rim_color=C['crimson_trim'],
        shade_smooth=False,
        ao_strength=0.26,
        bone='Clavicle_L',
    )
    pauldron_ridge_L = _bevel_box(
        name='Enforcer_PauldronRidge_L',
        size=(0.095, 0.155, 0.034),
        location=(sh_L.x + 0.036, 0.0, sh_L.z + 0.076),
        rotation=(0.0, 0.34, 0.0),
        bevel=0.005,
        segments=1,
        color=C['crimson_trim'],
        bone='Clavicle_L',
    )
    pauldron_sig_L = _box(
        name='Enforcer_PauldronStrobe_Signal_L',
        size=(0.036, 0.016, 0.016),
        location=(sh_L.x + 0.052, -0.088, sh_L.z + 0.056),
        rotation=(0.0, 0.30, -0.25),
        color=C['sig_crimson'],
        signal=True,
        bone='Clavicle_L',
    )
    body_parts.extend([pauldron_L, pauldron_ridge_L, mirror_clean(pauldron_L), mirror_clean(pauldron_ridge_L)])
    sig_parts.extend([pauldron_sig_L, mirror_clean(pauldron_sig_L)])

    # --- D. Armored Arms & Left Forearm Riot Shield-Bracer ---
    arm_sleeve_L = _oriented_chain_loft(
        [
            (lm['shoulder_L'] + Vector((-0.015, 0.0, 0.005)), 0.058, 0.060),
            (_vlerp(lm['shoulder_L'], lm['elbow_L'], 0.52), 0.054, 0.058),
            (lm['elbow_L'], 0.048, 0.052),
            (_vlerp(lm['elbow_L'], lm['wrist_L'], 0.55), 0.052, 0.056),
            (lm['wrist_L'], 0.044, 0.048),
        ],
        name='Enforcer_ArmSuit_L',
        segs=8,
        color=C['under_suit'],
        shade_smooth=False,
        role='arm',
    )
    fore_dir_L = (lm['wrist_L'] - lm['elbow_L']).normalized()
    fore_rot_L = Vector((0.0, 0.0, 1.0)).rotation_difference(fore_dir_L)
    bracer_R_base = aa.add_cylinder(
        name='Enforcer_ForearmVambrace_L',
        radius=0.048,
        radius_top=0.056,
        depth=0.175,
        vertices=8,
        location=_vlerp(lm['elbow_L'], lm['wrist_L'], 0.52),
        rotation=fore_rot_L,
        shade_smooth=False,
        color=C['armor_slate'],
        bone='Forearm_L',
    )
    body_parts.extend([arm_sleeve_L, mirror_clean(arm_sleeve_L), bracer_R_base, mirror_clean(bracer_R_base)])

    # Left Forearm Riot Shield-Bracer ('Forearm_L'): heavy octagonal ballistic buckler!
    shield_center_L = _vlerp(lm['elbow_L'], lm['wrist_L'], 0.56) + Vector((0.058, -0.022, 0.0))
    shield_prof_yz = [
        (-0.068, -0.155),
        (-0.104, -0.085),
        (-0.104,  0.095),
        (-0.062,  0.155),
        ( 0.058,  0.155),
        ( 0.096,  0.085),
        ( 0.096, -0.085),
        ( 0.058, -0.155),
    ]
    shield_core = aa.add_extruded_profile(
        shield_prof_yz,
        depth=0.028,
        axis='X',
        name='Enforcer_ShieldBracer_L',
        location=shield_center_L,
        rotation=fore_rot_L,
        bevel=0.006,
        bevel_segments=1,
        shade_smooth=False,
        color=C['armor_slate'],
        bone='Forearm_L',
    )
    aa.paint_faces(
        shield_core,
        lambda _p, c, _n: abs(c.y - shield_center_L.y) > 0.075,
        color=C['armor_light'],
        ao_strength=0.20,
    )
    body_parts.append(shield_core)
    body_parts.append(_bevel_box(
        name='Enforcer_ShieldBoss_L',
        size=(0.022, 0.110, 0.185),
        location=shield_center_L + Vector((0.016, -0.004, 0.0)),
        rotation=fore_rot_L,
        bevel=0.004,
        segments=1,
        color=C['crimson_trim'],
        bone='Forearm_L',
    ))
    sig_parts.append(_box(
        name='Enforcer_ShieldStunStrip_Signal_L',
        size=(0.016, 0.018, 0.220),
        location=shield_center_L + Vector((0.014, -0.098, 0.0)),
        rotation=fore_rot_L,
        color=C['sig_crimson'],
        signal=True,
        bone='Forearm_L',
    ))

    for side_h in ('L', 'R'):
        body_parts.extend(build_combat_hand(
            lm[f'wrist_{side_h}'],
            lm[f'hand_{side_h}'],
            side=side_h,
            palm_color=C['armor_slate'],
            finger_color=C['armor_light'],
            name_prefix='EnforcerGauntlet',
        ))

    # --- E. Heavy Crackling Shock-Cleaver in Hand_R! ---
    grip_R = lm['wrist_R'] + Vector((0.008, -0.018, -0.058))
    cleaver_rot = Euler((1.18, 0.0, 0.0), 'XYZ')  # Local +Z -> (0, -0.925, +0.381) forward-up!
    cleaver_mat = Matrix.LocRotScale(grip_R, cleaver_rot, Vector((1.0, 1.0, 1.0)))

    def _cpt(lx: float, ly: float, lz: float) -> Vector:
        return cleaver_mat @ Vector((lx, ly, lz))

    # Heavy two-fisted insulated grip + counterweight pommel + capacitor guard
    body_parts.append(aa.add_cylinder(
        name='Enforcer_CleaverGrip',
        radius=0.019,
        depth=0.255,
        vertices=8,
        location=_cpt(0.0, 0.0, -0.020),
        rotation=cleaver_rot,
        shade_smooth=False,
        color=C['grip_rubber'],
        bone='Hand_R',
    ))
    body_parts.append(_bevel_box(
        name='Enforcer_CleaverPommel',
        size=(0.044, 0.056, 0.042),
        location=_cpt(0.0, 0.0, -0.155),
        rotation=cleaver_rot,
        bevel=0.005,
        segments=1,
        color=C['brass_trim'],
        bone='Hand_R',
    ))
    body_parts.append(_bevel_box(
        name='Enforcer_CleaverGuard',
        size=(0.058, 0.118, 0.046),
        location=_cpt(0.0, -0.012, 0.112),
        rotation=cleaver_rot,
        bevel=0.006,
        segments=1,
        color=C['armor_light'],
        bone='Hand_R',
    ))

    # Massive Flared Riot Cleaver Spine & Slab ('Body', local -Y = cutting side, +Y = notched back spine)
    cleaver_body_yz = [
        (-0.044, 0.130),
        ( 0.036, 0.130),
        ( 0.048, 0.340),
        ( 0.032, 0.365),  # Armor-breaker spine notch!
        ( 0.058, 0.560),
        (-0.052, 0.605),  # Brutal flared butcher-cleaver nose!
        (-0.048, 0.350),
    ]
    cleaver_slab = aa.add_extruded_profile(
        cleaver_body_yz,
        depth=0.024,
        axis='X',
        name='Enforcer_CleaverSlab',
        location=grip_R,
        rotation=cleaver_rot,
        bevel=0.003,
        bevel_segments=1,
        shade_smooth=False,
        color=C['cleaver_body'],
        bone='Hand_R',
    )
    aa.paint_faces(cleaver_slab, lambda _p, _c, n: abs(n.x) < 0.4, color=C['cleaver_spine'], ao_strength=0.20)
    body_parts.append(cleaver_slab)

    # Twin high-voltage shock transformer coils mounted on cleaver flat
    for cz_l in (0.220, 0.380):
        body_parts.append(aa.add_cylinder(
            name=f'Enforcer_ShockCoil_{cz_l:.2f}',
            radius=0.022,
            depth=0.042,
            vertices=8,
            location=_cpt(0.0, 0.004, cz_l),
            rotation=(cleaver_rot.to_matrix() @ Euler((0.0, math.pi * 0.5, 0.0), 'XYZ').to_matrix()).to_euler('XYZ'),
            shade_smooth=False,
            color=C['brass_trim'],
            bone='Hand_R',
        ))

    # Crackling High-Voltage Serrated Shock-Edge + Arc Bars ('Signal', pinned to Hand_R!)
    shock_edge_yz = [
        (-0.040, 0.136),
        (-0.076, 0.148),
        (-0.088, 0.360),
        (-0.098, 0.592),
        (-0.048, 0.618),
        (-0.044, 0.360),
    ]
    sig_parts.append(aa.add_extruded_profile(
        shock_edge_yz,
        depth=0.012,
        axis='X',
        name='Enforcer_CleaverShockEdge_Signal',
        location=grip_R,
        rotation=cleaver_rot,
        shade_smooth=False,
        color=C['sig_shock'],
        signal=True,
        bone='Hand_R',
    ))
    for arc_z in (0.220, 0.380, 0.505):
        sig_parts.append(_box(
            name=f'Enforcer_CleaverArcBar_Signal_{arc_z:.2f}',
            size=(0.030, 0.072, 0.014),
            location=_cpt(0.0, -0.032, arc_z),
            rotation=cleaver_rot,
            color=C['sig_shock'],
            signal=True,
            bone='Hand_R',
        ))

    # --- F. Heavy Riot Greaves, Knee-Cops & Armored Boots ---
    leg_L = _oriented_chain_loft(
        [
            (lm['hip_L'] + Vector((-0.012, 0.0, 0.022)), 0.076, 0.084),
            (_vlerp(lm['hip_L'], lm['knee_L'], 0.52), 0.070, 0.076),
            (lm['knee_L'], 0.058, 0.064),
            (_vlerp(lm['knee_L'], lm['ankle_L'], 0.56), 0.054, 0.060),
            (lm['ankle_L'] + Vector((0.0, 0.0, -0.010)), 0.044, 0.048),
        ],
        name='Enforcer_ArmoredLeg_L',
        segs=8,
        color=C['under_suit'],
        shade_smooth=False,
        role='leg',
    )
    thigh_plate_L = _bevel_box(
        name='Enforcer_ThighArmor_L',
        size=(0.118, 0.115, 0.215),
        location=_vlerp(lm['hip_L'], lm['knee_L'], 0.48) + Vector((0.012, -0.024, 0.0)),
        rotation=(-0.04, 0.0, 0.0),
        bevel=0.008,
        segments=1,
        color=C['armor_slate'],
        bone='Thigh_L',
    )
    greave_L = _bevel_box(
        name='Enforcer_ShinGreave_L',
        size=(0.112, 0.088, 0.255),
        location=_vlerp(lm['knee_L'], lm['ankle_L'], 0.44) + Vector((0.004, -0.034, 0.020)),
        rotation=(0.05, 0.0, 0.0),
        taper=(1.14, 1.10),
        bevel=0.008,
        segments=1,
        color=C['armor_slate'],
        bone='Shin_L',
    )
    knee_cop_L = _bevel_box(
        name='Enforcer_KneeCop_L',
        size=(0.116, 0.056, 0.105),
        location=lm['knee_L'] + Vector((0.004, -0.056, 0.012)),
        rotation=(-0.10, 0.0, 0.0),
        bevel=0.006,
        segments=1,
        color=C['armor_light'],
        bone='Shin_L',
    )
    leg_sym = [leg_L, thigh_plate_L, greave_L, knee_cop_L]
    body_parts.extend(leg_sym)
    body_parts.extend(mirror_clean(leg_sym))

    boots_L = build_grounded_boot_L(
        ankle_x=lm['ankle_x'],
        boot_height=0.220,
        sole_color=C['under_suit'],
        upper_color=C['armor_slate'],
        toe_cap_color=C['armor_light'],
        buckle_color=C['crimson_trim'],
        width_scale=1.14,
        name_prefix='EnforcerBoot',
    )
    body_parts.extend(boots_L)
    body_parts.extend(mirror_clean(boots_L))

    # --- Batch, Skin, Animate & Export ---
    merged = aa.combine_by_role(body_parts, sig_parts)
    body_mesh = next(m for m in merged if m.name == 'Body')
    apply_character_seam_ao(
        body_mesh,
        seam_zs=[
            (lm['chest_z'] - 0.055, 0.026, 0.22),
            (lm['pelvis_z'] + 0.018, 0.026, 0.22),
            (0.220, 0.024, 0.20),
        ],
        crotch_z=lm['pelvis_z'],
    )
    role_snap = snapshot_skin_roles(merged)
    rig = aa.build_humanoid_rig_and_clips(
        merged,
        rig_name='EnforcerRig',
        height=lm['h'],
        style='enforcer',
        attack_clip_name='Punch',
        weapon_hand=None,
        shoulder_width=lm['sh_x'],
        hip_width=lm['hp_x'],
    )
    apply_infantry_skinning(rig, merged, role_snap, height=lm['h'])
    author_infantry_clips(rig, archetype='enforcer', height=lm['h'])

    out_path = OUT_DIR / 'foe-enforcer.glb'
    info = aa.export_glb(out_path, [rig, *merged], min_tris=1900, max_meshes=6)
    print(f"[build_aa_infantry] Exported {info['path']}: {info['triangles']} tris, {info['meshes']} meshes, {info['size_kb']} KB")
    return out_path


# ---------------------------------------------------------------------------
# 3. Hazmat Pulse Gunner — public/street/foe-gunner.glb
# ---------------------------------------------------------------------------

def build_foe_gunner() -> Path:
    """Sculpt and rig the Hazmat Pulse Gunner -> public/street/foe-gunner.glb."""
    aa.reset_scene()
    lm = rig_landmarks(height=1.81, shoulder_width=0.230, hip_width=0.114)
    C = GUNNER_COLORS

    body_parts: list[bpy.types.Object] = []
    sig_parts: list[bpy.types.Object] = []

    hz = lm['head_z']     # 1.520
    cz = lm['crown_z']    # 1.810
    span = cz - hz        # 0.290

    # --- A. Domed Hazmat Helm with Bolted NBC Collar & Glowing Target-Visor ---
    body_parts.append(aa.add_torus(
        name='Gunner_NBC_NeckRing',
        major_radius=0.106,
        minor_radius=0.022,
        major_segments=10,
        minor_segments=5,
        location=(0.0, -0.002, hz - 0.032),
        rotation=(0.08, 0.0, 0.0),
        shade_smooth=False,
        color=C['steel_rail'],
        bone='Chest',
    ))
    body_parts.append(build_lofted_tube(
        [
            _ring_oval(0.0, 0.002, lm['neck_z'] - 0.030, 0.086, 0.082, 0.086, segs=8),
            _ring_oval(0.0, 0.000, hz + 0.015,           0.084, 0.080, 0.084, segs=8),
        ],
        name='Gunner_NeckBellows',
        color=C['charcoal_suit'],
        role='torso',
    ))

    # Pressurized Bulbous Domed Hazmat Helmet ('Head')
    dome_rings = [
        _ring_oval(0.0, -0.008, hz - 0.020,       0.088, 0.102, 0.094, segs=12, squareness=0.14),
        _ring_oval(0.0, -0.008, hz + 0.18 * span, 0.106, 0.122, 0.114, segs=12, squareness=0.10),
        _ring_oval(0.0, -0.004, hz + 0.42 * span, 0.114, 0.130, 0.124, segs=12, squareness=0.06),
        _ring_oval(0.0,  0.000, hz + 0.66 * span, 0.108, 0.122, 0.120, segs=12, squareness=0.04),
        _ring_oval(0.0,  0.004, hz + 0.86 * span, 0.084, 0.094, 0.096, segs=12),
        _ring_oval(0.0,  0.006, hz + 0.99 * span, 0.040, 0.045, 0.046, segs=12),
    ]
    haz_dome = build_lofted_tube(
        dome_rings,
        name='Gunner_HazmatDome',
        color=C['hazmat_yellow'],
        shade_smooth=True,
        ao_strength=0.22,
        bone='Head',
    )
    # Dark charcoal lower rebreather cowling + inner visor recess
    aa.paint_faces(
        haz_dome,
        lambda _p, c, n: c.z < hz + 0.22 * span or (n.y < -0.48 and hz + 0.20 * span < c.z < hz + 0.72 * span and abs(c.x) < 0.092),
        color=C['dome_dark'],
        ao_strength=0.20,
    )
    body_parts.append(haz_dome)

    # Twin forward CBRN jaw air-scrubber canisters
    scrubber_rot_L = Euler((0.52, 0.42, -0.36), 'XYZ')
    scrubber_pos_L = Vector((0.064, -0.108, hz + 0.14 * span))
    scrub_L = aa.add_cylinder(
        name='Gunner_JawScrubber_L',
        radius=0.025,
        depth=0.042,
        vertices=10,
        location=scrubber_pos_L,
        rotation=scrubber_rot_L,
        bevel=0.003,
        color=C['charcoal_armor'],
        bone='Head',
    )
    scrub_band_L = aa.add_cylinder(
        name='Gunner_ScrubberBand_L',
        radius=0.026,
        depth=0.010,
        vertices=10,
        location=scrubber_pos_L,
        rotation=scrubber_rot_L,
        color=C['hazmat_amber'],
        bone='Head',
    )
    body_parts.extend([scrub_L, scrub_band_L, mirror_clean(scrub_L), mirror_clean(scrub_band_L)])

    # Glowing Target-Visor ('Signal'): curved crimson HUD faceplate + cyan right-eye targeting reticle!
    hv_outer, hv_inner = [], []
    for dz, dy in ((-0.036, 0.002), (0.000, -0.005), (0.036, 0.002)):
        ro, ri = [], []
        for c_i in range(7):
            u = (c_i / 6.0) * 2.0 - 1.0
            ang = u * 0.80
            xo = math.sin(ang) * 0.102
            yo = -math.cos(ang) * 0.132 + dy
            zo = hz + 0.46 * span + dz
            ro.append((xo, yo, zo))
            ri.append((xo * 0.92, yo + 0.012, zo))
        hv_outer.append(ro)
        hv_inner.append(ri)
    sig_parts.append(build_thick_shell(
        hv_outer,
        hv_inner,
        name='Gunner_TargetVisor_Signal',
        outer_color=C['sig_crimson'],
        signal=True,
        shade_smooth=True,
        bone='Head',
    ))
    # Raised Glowing Cyan Target-Optic Scope Lens over Right Eye (-X)
    sig_parts.append(aa.add_cylinder(
        name='Gunner_TargetReticle_Signal_R',
        radius=0.022,
        depth=0.018,
        vertices=10,
        location=(-0.038, -0.135, hz + 0.48 * span),
        rotation=(math.pi * 0.5, 0.0, -0.18),
        color=C['sig_cyan'],
        signal=True,
        bone='Head',
    ))
    body_parts.append(aa.add_torus(
        name='Gunner_TargetBezel_R',
        major_radius=0.024,
        minor_radius=0.005,
        major_segments=10,
        minor_segments=4,
        location=(-0.038, -0.136, hz + 0.48 * span),
        rotation=(math.pi * 0.5, 0.0, -0.18),
        shade_smooth=False,
        color=C['charcoal_armor'],
        bone='Head',
    ))

    # --- B. Pressurized Hazard-Yellow & Charcoal Bio-Armor + Twin Dorsal Plasma Tanks ---
    suit_torso = build_lofted_tube(
        [
            _ring_oval(0.0, 0.006, lm['pelvis_z'] - 0.015, 0.150, 0.108, 0.118, segs=12, squareness=0.22),
            _ring_oval(0.0, 0.002, lm['spine_z'],          0.146, 0.104, 0.114, segs=12, squareness=0.22),
            _ring_oval(0.0, -0.004, lm['chest_z'],         0.172, 0.124, 0.128, segs=12, squareness=0.26),
            _ring_oval(0.0, 0.000, lm['sh_z'] + 0.008,     0.182, 0.118, 0.124, segs=12, squareness=0.24),
            _ring_oval(0.0, 0.004, lm['neck_z'] + 0.006,   0.094, 0.088, 0.092, segs=12),
        ],
        name='Gunner_PressurizedTorso',
        color=C['charcoal_suit'],
        ao_strength=0.28,
        role='torso',
    )
    body_parts.append(suit_torso)
    body_parts.append(build_pelvis_saddle(lm, C['hazmat_yellow'], 'Gunner_BioApronSaddle'))

    # Bright Hazard-Yellow Molded Bio-Armor Chest Carapace + Charcoal Harness ('Chest')
    bio_chest = build_lofted_tube(
        [
            _ring_oval(0.0, -0.004, lm['chest_z'] - 0.075, 0.168, 0.130, 0.134, segs=12, squareness=0.34),
            _ring_oval(0.0, -0.008, lm['chest_z'] + 0.045, 0.194, 0.148, 0.146, segs=12, squareness=0.36),
            _ring_oval(0.0, -0.004, lm['sh_z'] + 0.018,    0.190, 0.136, 0.138, segs=12, squareness=0.32),
            _ring_oval(0.0,  0.002, lm['neck_z'] + 0.010,  0.116, 0.104, 0.108, segs=12, squareness=0.24),
        ],
        name='Gunner_BioCarapace',
        color=C['hazmat_yellow'],
        shade_smooth=False,
        ao_strength=0.25,
        bone='Chest',
    )
    # Dark charcoal harness straps across chest
    aa.paint_faces(
        bio_chest,
        lambda _p, c, _n: 0.048 < abs(c.x) < 0.086 and c.z > lm['chest_z'] - 0.04,
        color=C['charcoal_armor'],
        ao_strength=0.22,
    )
    body_parts.append(bio_chest)

    # Chest CBRN Dosimeter / Regulator Box + Signal telemetry pip
    body_parts.append(_bevel_box(
        name='Gunner_ChestRegulator',
        size=(0.096, 0.036, 0.088),
        location=(0.0, -0.150, lm['chest_z'] + 0.038),
        bevel=0.006,
        segments=1,
        color=C['charcoal_armor'],
        bone='Chest',
    ))
    sig_parts.append(_box(
        name='Gunner_ChestDosimeter_Signal',
        size=(0.052, 0.012, 0.018),
        location=(0.0, -0.166, lm['chest_z'] + 0.052),
        color=C['sig_cyan'],
        signal=True,
        bone='Chest',
    ))

    # Charcoal pressure waist-belt
    body_parts.append(build_lofted_tube(
        [
            _ring_oval(0.0, 0.002, lm['pelvis_z'] + 0.022, 0.162, 0.118, 0.126, segs=12, squareness=0.30),
            _ring_oval(0.0, 0.002, lm['pelvis_z'] + 0.078, 0.158, 0.114, 0.122, segs=12, squareness=0.30),
        ],
        name='Gunner_SealBelt',
        color=C['charcoal_armor'],
        shade_smooth=False,
        bone='Pelvis',
    ))

    # Twin Dorsal Plasma Tanks on Back ('Chest') with Glowing Cyan & Crimson Signal Gauges!
    body_parts.append(_bevel_box(
        name='Gunner_TankCradle',
        size=(0.245, 0.056, 0.255),
        location=(0.0, 0.148, lm['chest_z'] + 0.048),
        bevel=0.006,
        segments=1,
        color=C['charcoal_armor'],
        bone='Chest',
    ))
    for t_idx, tx in enumerate((-0.086, 0.086)):
        ty = 0.205
        tz_mid = lm['chest_z'] + 0.052
        body_parts.append(aa.add_cylinder(
            name=f'Gunner_PlasmaTank_{t_idx}',
            radius=0.062,
            depth=0.330,
            vertices=10,
            location=(tx, ty, tz_mid),
            bevel=0.008,
            bevel_segments=1,
            color=C['tank_alloy'],
            bone='Chest',
        ))
        # Hazard-yellow upper pressure dome + lower boot cap
        body_parts.append(aa.add_cone(
            name=f'Gunner_TankTopCap_{t_idx}',
            radius1=0.061,
            radius2=0.026,
            depth=0.055,
            vertices=10,
            location=(tx, ty, tz_mid + 0.192),
            color=C['hazmat_yellow'],
            bone='Chest',
        ))
        body_parts.append(aa.add_cylinder(
            name=f'Gunner_TankClampBand_{t_idx}',
            radius=0.066,
            depth=0.036,
            vertices=10,
            location=(tx, ty, tz_mid - 0.045),
            shade_smooth=False,
            color=C['charcoal_armor'],
            bone='Chest',
        ))
        # Vertical glowing cyan plasma sight-gauge on rear (+Y) of each dorsal tank
        sig_parts.append(_box(
            name=f'Gunner_TankLevelGauge_Signal_{t_idx}',
            size=(0.020, 0.016, 0.195),
            location=(tx, ty + 0.060, tz_mid + 0.012),
            color=C['sig_cyan'],
            signal=True,
            bone='Chest',
        ))
        # Circular top pressure dial ('Signal': left tank cyan, right tank crimson warning gauge!)
        sig_parts.append(aa.add_cylinder(
            name=f'Gunner_TankDial_Signal_{t_idx}',
            radius=0.022,
            depth=0.014,
            vertices=10,
            location=(tx, ty + 0.046, tz_mid + 0.150),
            rotation=(-math.pi * 0.36, 0.0, 0.0),
            color=C['sig_crimson'] if t_idx == 0 else C['sig_cyan'],
            signal=True,
            bone='Chest',
        ))

    # Central tank bridge manifold + braided plasma feeder hose over right shoulder
    body_parts.append(_bevel_box(
        name='Gunner_TankBridgeManifold',
        size=(0.195, 0.042, 0.044),
        location=(0.0, 0.205, lm['chest_z'] + 0.195),
        bevel=0.005,
        segments=1,
        color=C['steel_rail'],
        bone='Chest',
    ))
    body_parts.append(aa.add_pipe(
        [
            (-0.086, 0.195, lm['chest_z'] + 0.245),
            (-0.135, 0.120, lm['sh_z'] + 0.072),
            (-0.125, -0.055, lm['sh_z'] + 0.045),
            (-0.055, -0.140, lm['chest_z'] + 0.065),
        ],
        radius=0.014,
        bevel_resolution=1,
        name='Gunner_PlasmaFeederHose',
        color=C['charcoal_suit'],
        bone='Chest',
    ))

    # --- C. Pressurized Bio-Armor Sleeves, Pauldrons & NBC Gloves ---
    sleeve_L = _oriented_chain_loft(
        [
            (lm['shoulder_L'] + Vector((-0.016, 0.0, 0.006)), 0.058, 0.060),
            (_vlerp(lm['shoulder_L'], lm['elbow_L'], 0.50), 0.056, 0.060),
            (lm['elbow_L'], 0.048, 0.050),
            (_vlerp(lm['elbow_L'], lm['wrist_L'], 0.56), 0.050, 0.054),
            (lm['wrist_L'], 0.042, 0.045),
        ],
        name='Gunner_HazmatSleeve_L',
        segs=8,
        color=C['hazmat_yellow'],
        shade_smooth=True,
        ao_strength=0.25,
        role='arm',
    )
    # Dark charcoal elbow accordion seal
    aa.paint_faces(
        sleeve_L,
        lambda _p, c, _n: abs(c.z - lm['elbow_L'].z) < 0.052,
        color=C['charcoal_suit'],
        ao_strength=0.24,
    )
    pauldron_bio_L = _bevel_box(
        name='Gunner_BioPauldron_L',
        size=(0.112, 0.150, 0.135),
        location=lm['shoulder_L'] + Vector((0.022, 0.0, 0.012)),
        rotation=(0.0, 0.26, 0.0),
        bevel=0.016,
        segments=1,
        color=C['hazmat_amber'],
        bone='Clavicle_L',
    )
    fore_dir_L = (lm['wrist_L'] - lm['elbow_L']).normalized()
    fore_rot_L = Vector((0.0, 0.0, 1.0)).rotation_difference(fore_dir_L)
    cuff_L = aa.add_cylinder(
        name='Gunner_SealCuff_L',
        radius=0.045,
        radius_top=0.048,
        depth=0.052,
        vertices=8,
        location=_vlerp(lm['elbow_L'], lm['wrist_L'], 0.86),
        rotation=fore_rot_L,
        shade_smooth=False,
        color=C['charcoal_armor'],
        bone='Forearm_L',
    )
    arm_g_sym = [sleeve_L, pauldron_bio_L, cuff_L]
    body_parts.extend(arm_g_sym)
    body_parts.extend(mirror_clean(arm_g_sym))

    for side_h in ('L', 'R'):
        body_parts.extend(build_combat_hand(
            lm[f'wrist_{side_h}'],
            lm[f'hand_{side_h}'],
            side=side_h,
            palm_color=C['charcoal_suit'],
            finger_color=C['charcoal_armor'],
            name_prefix='GunnerGlove',
        ))

    # --- D. Futuristic Twin-Rail SMG / Pulse Carbine Aimed Forward in Hand_R! ---
    # Built aligned horizontally along -Y (Forward!) with pistol grip inside Hand_R
    gx = lm['wrist_R'].x + 0.008
    gy = lm['wrist_R'].y - 0.015
    gz = lm['wrist_R'].z - 0.045
    rz = gz + 0.072  # Horizontal carbine receiver centerline

    # Angled pistol grip inside Hand_R + compact shoulder stock + receiver + hazard shroud
    body_parts.append(_bevel_box(
        name='Gunner_CarbineGrip',
        size=(0.028, 0.042, 0.115),
        location=(gx, gy + 0.004, gz + 0.008),
        rotation=(0.24, 0.0, 0.0),
        bevel=0.004,
        segments=1,
        color=C['charcoal_suit'],
        bone='Hand_R',
    ))
    body_parts.append(_bevel_box(
        name='Gunner_CarbineStock',
        size=(0.042, 0.145, 0.096),
        location=(gx, gy + 0.105, rz - 0.012),
        taper=(0.86, 0.88),
        bevel=0.005,
        segments=1,
        color=C['charcoal_armor'],
        bone='Hand_R',
    ))
    body_parts.append(_bevel_box(
        name='Gunner_CarbineReceiver',
        size=(0.056, 0.285, 0.088),
        location=(gx, gy - 0.095, rz),
        bevel=0.006,
        segments=1,
        color=C['charcoal_armor'],
        bone='Hand_R',
    ))
    body_parts.append(_bevel_box(
        name='Gunner_CarbineHazardShroud',
        size=(0.062, 0.205, 0.042),
        location=(gx, gy - 0.115, rz + 0.032),
        bevel=0.005,
        segments=1,
        color=C['hazmat_yellow'],
        bone='Hand_R',
    ))

    # Slanted forward plasma magazine + tactical foregrip
    body_parts.append(_bevel_box(
        name='Gunner_PlasmaMag',
        size=(0.032, 0.046, 0.145),
        location=(gx, gy - 0.140, rz - 0.085),
        rotation=(-0.28, 0.0, 0.0),
        bevel=0.004,
        segments=1,
        color=C['hazmat_amber'],
        bone='Hand_R',
    ))
    body_parts.append(_bevel_box(
        name='Gunner_Foregrip',
        size=(0.030, 0.048, 0.085),
        location=(gx, gy - 0.238, rz - 0.062),
        rotation=(-0.18, 0.0, 0.0),
        bevel=0.004,
        segments=1,
        color=C['charcoal_suit'],
        bone='Hand_R',
    ))

    # Futuristic Twin Magnetic Accelerator Rails (Upper + Lower parallel rails aimed along -Y!)
    for rail_tag, dz_r in (('Upper', 0.028), ('Lower', -0.024)):
        body_parts.append(_bevel_box(
            name=f'Gunner_TwinRail_{rail_tag}',
            size=(0.038, 0.310, 0.022),
            location=(gx, gy - 0.370, rz + dz_r),
            bevel=0.003,
            segments=1,
            color=C['steel_rail'],
            bone='Hand_R',
        ))
    body_parts.append(_bevel_box(
        name='Gunner_MuzzleClamp',
        size=(0.046, 0.038, 0.078),
        location=(gx, gy - 0.515, rz + 0.002),
        bevel=0.004,
        segments=1,
        color=C['charcoal_armor'],
        bone='Hand_R',
    ))

    # Glowing Cyan Twin-Rail Plasma Core + Muzzle Aperture + Crimson Reflex Optic ('Signal', Hand_R!)
    sig_parts.append(_box(
        name='Gunner_PlasmaRailCore_Signal',
        size=(0.020, 0.325, 0.020),
        location=(gx, gy - 0.375, rz + 0.002),
        color=C['sig_cyan'],
        signal=True,
        bone='Hand_R',
    ))
    sig_parts.append(_box(
        name='Gunner_MuzzleGlow_Signal',
        size=(0.026, 0.016, 0.046),
        location=(gx, gy - 0.538, rz + 0.002),
        color=C['sig_cyan'],
        signal=True,
        bone='Hand_R',
    ))
    body_parts.append(_bevel_box(
        name='Gunner_HoloSightHousing',
        size=(0.038, 0.072, 0.036),
        location=(gx, gy - 0.085, rz + 0.068),
        bevel=0.003,
        segments=1,
        color=C['charcoal_armor'],
        bone='Hand_R',
    ))
    sig_parts.append(_box(
        name='Gunner_HoloSightWindow_Signal',
        size=(0.026, 0.012, 0.024),
        location=(gx, gy - 0.085, rz + 0.070),
        color=C['sig_crimson'],
        signal=True,
        bone='Hand_R',
    ))

    # --- E. Pressurized Bio-Armor Legs & Sealed Hazmat Boots ---
    haz_leg_L = _oriented_chain_loft(
        [
            (lm['hip_L'] + Vector((-0.016, 0.0, 0.022)), 0.072, 0.080),
            (_vlerp(lm['hip_L'], lm['knee_L'], 0.52), 0.072, 0.078),
            (lm['knee_L'], 0.060, 0.065),
            (_vlerp(lm['knee_L'], lm['ankle_L'], 0.54), 0.062, 0.068),
            (lm['ankle_L'] + Vector((0.0, 0.0, -0.010)), 0.045, 0.049),
        ],
        name='Gunner_HazmatLeg_L',
        segs=8,
        color=C['hazmat_yellow'],
        shade_smooth=True,
        ao_strength=0.25,
        role='leg',
    )
    aa.paint_faces(
        haz_leg_L,
        lambda _p, c, _n: abs(c.z - lm['knee_L'].z) < 0.055,
        color=C['charcoal_suit'],
        ao_strength=0.24,
    )
    knee_seal_L = _bevel_box(
        name='Gunner_KneeReinforce_L',
        size=(0.104, 0.048, 0.098),
        location=lm['knee_L'] + Vector((0.004, -0.050, 0.006)),
        rotation=(-0.08, 0.0, 0.0),
        bevel=0.006,
        segments=1,
        color=C['charcoal_armor'],
        bone='Shin_L',
    )
    body_parts.extend([haz_leg_L, knee_seal_L, mirror_clean(haz_leg_L), mirror_clean(knee_seal_L)])

    # Pressurized thigh decon-pouch on left leg
    body_parts.append(_bevel_box(
        name='Gunner_DeconPouch_L',
        size=(0.046, 0.112, 0.128),
        location=(lm['hip_L'].x + 0.078, -0.012, _lerp(lm['hip_z'], lm['knee_z'], 0.44)),
        bevel=0.006,
        segments=1,
        color=C['charcoal_armor'],
        bone='Thigh_L',
    ))

    boots_L = build_grounded_boot_L(
        ankle_x=lm['ankle_x'],
        boot_height=0.220,
        sole_color=C['charcoal_suit'],
        upper_color=C['charcoal_armor'],
        toe_cap_color=C['hazmat_amber'],
        buckle_color=C['hazmat_yellow'],
        width_scale=1.10,
        name_prefix='GunnerHazBoot',
    )
    body_parts.extend(boots_L)
    body_parts.extend(mirror_clean(boots_L))

    # --- Batch, Skin, Animate & Export ---
    merged = aa.combine_by_role(body_parts, sig_parts)
    body_mesh = next(m for m in merged if m.name == 'Body')
    apply_character_seam_ao(
        body_mesh,
        seam_zs=[
            (lm['chest_z'] - 0.075, 0.025, 0.20),
            (lm['pelvis_z'] + 0.022, 0.025, 0.22),
            (0.220, 0.024, 0.20),
        ],
        crotch_z=lm['pelvis_z'],
    )
    role_snap = snapshot_skin_roles(merged)
    rig = aa.build_humanoid_rig_and_clips(
        merged,
        rig_name='GunnerRig',
        height=lm['h'],
        style='gunner',
        attack_clip_name='Idle_Shoot',
        weapon_hand=None,
        shoulder_width=lm['sh_x'],
        hip_width=lm['hp_x'],
    )
    apply_infantry_skinning(rig, merged, role_snap, height=lm['h'])
    author_infantry_clips(rig, archetype='gunner', height=lm['h'])

    out_path = OUT_DIR / 'foe-gunner.glb'
    info = aa.export_glb(out_path, [rig, *merged], min_tris=1900, max_meshes=6)
    print(f"[build_aa_infantry] Exported {info['path']}: {info['triangles']} tris, {info['meshes']} meshes, {info['size_kb']} KB")
    return out_path


# ---------------------------------------------------------------------------
# Strict Self-Verification (Budget, Orientation, Rig, Hand_R Weapon Pinning)
# ---------------------------------------------------------------------------

def _read_accessor_tuples(g: dict, bin_chunk: bytes, acc_idx: int) -> list[tuple]:
    acc = g['accessors'][acc_idx]
    bv = g['bufferViews'][acc['bufferView']]
    comp_fmt = {5121: ('B', 1), 5123: ('H', 2), 5125: ('I', 4), 5126: ('f', 4)}[acc['componentType']]
    n_comp = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}[acc['type']]
    offset = bv.get('byteOffset', 0) + acc.get('byteOffset', 0)
    stride = bv.get('byteStride', n_comp * comp_fmt[1])
    fmt = f'<{n_comp}{comp_fmt[0]}'
    return [struct.unpack_from(fmt, bin_chunk, offset + i * stride) for i in range(acc['count'])]


def verify_infantry_glb(path: Path, required_clips: Sequence[str]) -> dict:
    """Verify 1,900..3,600 tris, 18+ joints, Body+Signal, +Z facing, and Hand_R weapon weights."""
    info = aa.verify_glb_file(path, expect_rig=True, max_meshes=6, min_tris=1900)
    tris = info['triangles']
    assert 1900 <= tris <= 3600, f'{path.name}: triangle count {tris} outside required 1900..3600'

    raw = path.read_bytes()
    json_len = struct.unpack_from('<I', raw, 12)[0]
    g = json.loads(raw[20:20 + json_len].decode('utf-8'))
    bin_chunk = raw[28 + json_len:]

    mesh_names = [m.get('name', '') for m in g.get('meshes', [])]
    mat_names = {m.get('name', '') for m in g.get('materials', [])}
    assert len(mesh_names) == 2, f'{path.name}: expected 2 batched meshes (Body + Signal), got {mesh_names}'
    assert 'VertexPaint' in mat_names and 'Signal' in mat_names, (
        f'{path.name}: expected VertexPaint + Signal materials, got {mat_names}'
    )

    anim_set = set(info['animations'])
    for clip in required_clips:
        assert clip in anim_set, f'{path.name}: missing required clip {clip!r} (have {sorted(anim_set)})'

    skin = g['skins'][0]
    joints = skin['joints']
    assert len(joints) >= 18, f'{path.name}: expected >= 18 skin joints, got {len(joints)}'
    joint_names = [g['nodes'][ni].get('name', '') for ni in joints]
    assert 'Hand_R' in joint_names, f'{path.name}: Hand_R missing from skin joints {joint_names}'
    hand_r_joint_idx = joint_names.index('Hand_R')

    mins = [math.inf, math.inf, math.inf]
    maxs = [-math.inf, -math.inf, -math.inf]
    weapon_verts = 0

    for m in g['meshes']:
        for p in m['primitives']:
            attrs = p['attributes']
            pos_acc = g['accessors'][attrs['POSITION']]
            for i in range(3):
                mins[i] = min(mins[i], pos_acc['min'][i])
                maxs[i] = max(maxs[i], pos_acc['max'][i])

            positions = _read_accessor_tuples(g, bin_chunk, attrs['POSITION'])
            j_tuples = _read_accessor_tuples(g, bin_chunk, attrs['JOINTS_0'])
            w_tuples = _read_accessor_tuples(g, bin_chunk, attrs['WEIGHTS_0'])
            for (px, py, pz), jt, wt in zip(positions, j_tuples, w_tuples):
                # In glTF Y-up (+Z forward), weapon vertices protrude forward beyond z > 0.28 and y > 0.60
                if pz > 0.28 and py > 0.60:
                    weapon_verts += 1
                    hand_w = sum(w for j, w in zip(jt, wt) if j == hand_r_joint_idx)
                    assert hand_w > 0.99, (
                        f'{path.name}: forward weapon vertex {(px, py, pz)} not weighted to Hand_R '
                        f'(Hand_R weight={hand_w:.3f})'
                    )

    assert weapon_verts >= 12, f'{path.name}: expected forward weapon geometry on Hand_R, found {weapon_verts} verts'

    height_m = maxs[1] - mins[1]
    assert abs(mins[1]) <= 0.006, f'{path.name}: soles must rest on y=0, got min_y={mins[1]:.4f}'
    assert 1.76 <= height_m <= 1.92, f'{path.name}: height {height_m:.4f}m outside 1.76..1.92m'
    assert maxs[2] > 0.42 and maxs[2] > abs(mins[2]), (
        f'{path.name}: character + weapon must face +Z in Three.js, got z_bounds=({mins[2]:.3f}, {maxs[2]:.3f})'
    )

    print(
        f"[verify] {path.name}: {tris} tris, height={height_m:.3f}m (y={mins[1]:.3f}..{maxs[1]:.3f}), "
        f"z=({mins[2]:.3f}..{maxs[2]:.3f}), joints={len(joints)}, weapon_verts={weapon_verts}, "
        f"clips={info['animations']}"
    )
    return info


def main() -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    punk_path = build_foe_punk()
    enforcer_path = build_foe_enforcer()
    gunner_path = build_foe_gunner()

    base_clips = ('Idle', 'Walk', 'Run', 'Punch', 'HitReact', 'Death')
    verify_infantry_glb(punk_path, base_clips)
    verify_infantry_glb(enforcer_path, base_clips)
    verify_infantry_glb(gunner_path, (*base_clips, 'Idle_Shoot'))
    print('[build_aa_infantry] All 3 AA cyberpunk enemy infantry rigs built and verified.', flush=True)


if __name__ == '__main__':
    try:
        main()
    except Exception:
        traceback.print_exc()
        sys.exit(1)
