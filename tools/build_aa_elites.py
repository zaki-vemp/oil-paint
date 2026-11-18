"""Headless Blender 4.2 LTS AA elite enemy sculptor for Komorebi.

Authors and exports two elite AA cyberpunk enemy rigs into public/street/
(facing -Y in Blender = +Z in Three.js, grounded at z = 0):
  1. public/street/foe-blade.glb — Razor Ronin Cyber-Assassin (blade kind, 2,200..3,800 tris)
     * Horned Oni faceplate (menpo) with blazing magenta 'Signal' eye-slits & third-eye sigil
     * Stealth kabuto cowl with tiered shikoro nape armor & swept cyber-chonmage topknot
     * 3-tier layered O-sode shoulder armor lames with brass odoshi & magenta edge trim
     * Sleeveless armored jinbaori surcoat over cyber-do breastplate + left-hip lacquered saya
     * Fluted suneate shin greaves, winged knee poleyns & split-toe cyber-tabi war boots
     * Long high-frequency glowing magenta katana ('Signal' razor edge) in right hand
     * Clips: 'Idle', 'Walk', 'Run', 'Punch', 'Sword_Slash', 'HitReact', 'Death'
  2. public/street/foe-mech.glb  — Aethelgard Titan Siege Mech (brute kind, 2,800..4,800 tris)
     * Towering heavy war-mech silhouette (~1.82m normalized rig height; 2.2x in hunt.js)
     * Wide sloped armored torso chassis with caged glowing 'Signal' fusion reactor core
     * Recessed turret cockpit head with menacing wrap-around 'Signal' Cyclops optic strip
     * Twin shoulder-mounted micro-missile pods ('Pod_L'/'Pod_R' bones) with 12 live warheads
     * Reverse-style 3-link heavy hydraulic walker legs with chrome rams & broad splayed feet
     * Massive Piston Siege Gauntlets with dual hydraulic rams & blazing forearm plasma blades
     * 25-bone armature + clips: 'Idle', 'Walk', 'Run', 'Punch', 'Attack', 'HitReact', 'Death'

Run headless:
  .test/blender/blender --background --disable-autoexec --python tools/build_aa_elites.py
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
from mathutils import Euler, Matrix, Quaternion, Vector

import blender_aa_lib as aa

ROOT = TOOLS_DIR.parent
OUT_DIR = ROOT / 'public' / 'street'


# ---------------------------------------------------------------------------
# Curated Linear-RGB Palettes (Tuned for Komorebi Cel/Paint + Signal Shaders)
# ---------------------------------------------------------------------------

RONIN_COLORS = {
    'armor_obsidian': (0.062, 0.070, 0.105),    # Lacquered Neo-Kyoto midnight armor
    'armor_titanium': (0.520, 0.575, 0.655),    # Brushed steel-titanium bevel lames
    'armor_slate': (0.225, 0.268, 0.340),       # Gunmetal under-chassis & couters
    'jinbaori_outer': (0.135, 0.088, 0.205),    # Imperial dark plum-indigo jinbaori coat
    'jinbaori_lining': (0.595, 0.085, 0.235),   # Crimson-magenta silk inner lining
    'jinbaori_lapel': (0.815, 0.780, 0.705),    # Champagne-ivory brocade collar & lapel trim
    'brass_odoshi': (0.835, 0.625, 0.215),      # Gilded samurai brass crest, tsuba & clasps
    'crimson_cord': (0.690, 0.115, 0.250),      # Braided tsuka-ito & obi sash cord
    'oni_mask': (0.095, 0.102, 0.148),          # Sculpted dark gunmetal Oni menpo faceplate
    'oni_ivory': (0.855, 0.825, 0.775),         # Ceramic-bone Oni fangs & horn tips
    'cyber_weave': (0.082, 0.098, 0.138),       # Synthetic muscle fiber weave on arms
    'hakama_dark': (0.088, 0.104, 0.152),       # Pleated dark indigo-carbon hakama
    'sig_magenta': (1.000, 0.115, 0.565),       # Signal: blazing high-frequency magenta plasma
    'sig_hot_pink': (1.000, 0.440, 0.800),      # Signal: white-hot magenta core & kissaki tip
}

MECH_COLORS = {
    'hull_primary': (0.265, 0.310, 0.385),      # Heavy Aethelgard gunmetal-slate armor
    'hull_light': (0.555, 0.610, 0.680),        # Chamfered titanium glacis & gauntlet plates
    'hull_dark': (0.072, 0.085, 0.118),         # Internal obsidian servo drums & housings
    'frame_steel': (0.385, 0.435, 0.500),       # Structural hydraulic leg & chassis struts
    'chrome_ram': (0.810, 0.850, 0.895),        # Polished hydraulic piston rods
    'aethel_gold': (0.860, 0.625, 0.165),       # Aethelgard hazard-amber/brass trim & bezels
    'siege_crimson': (0.640, 0.120, 0.165),     # Siege-division crimson squadron markings
    'warhead_cone': (0.790, 0.810, 0.845),      # Micro-missile nose-cones
    'sig_reactor': (1.000, 0.390, 0.085),       # Signal: blazing fusion-orange reactor core
    'sig_core_hot': (1.000, 0.790, 0.240),      # Signal: solar-gold inner reactor turbine
    'sig_optic': (1.000, 0.095, 0.280),         # Signal: Cyclops optic strip & warhead seekers
    'sig_plasma': (1.000, 0.155, 0.480),        # Signal: high-energy forearm plasma blades
}


# ---------------------------------------------------------------------------
# Shared Math, Closed-Manifold Lofting & Skin-Tagging Helpers
# ---------------------------------------------------------------------------

def _lerp(a: float, b: float, t: float) -> float:
    return a + (b - a) * t


def _vlerp(a: Vector, b: Vector, t: float) -> Vector:
    return a + (b - a) * t


def _smoothstep(edge0: float, edge1: float, x: float) -> float:
    if abs(edge1 - edge0) < 1e-8:
        return 1.0 if x >= edge1 else 0.0
    t = max(0.0, min(1.0, (x - edge0) / (edge1 - edge0)))
    return t * t * (3.0 - 2.0 * t)


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
    """Mirror across X=0 via aa.mirror_x with outward normals & swapped _L/_R vertex groups."""
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


def build_lofted_tube(
    rings: Sequence[Sequence[tuple[float, float, float]]],
    name: str,
    color=(0.30, 0.35, 0.42),
    signal: bool = False,
    cap_bottom: bool = True,
    cap_top: bool = True,
    base_point: tuple[float, float, float] | None = None,
    tip_point: tuple[float, float, float] | None = None,
    shade_smooth: bool = True,
    ao_strength: float = 0.26,
    top_highlight: float = 0.11,
    ridge_boost: float = 0.09,
    ground_ao: float = 0.14,
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
        c0 = base_point or tuple(sum(pt[d] for pt in rings[0]) / m for d in range(3))
        vb = bm.verts.new(c0)
        for i in range(m):
            j = (i + 1) % m
            bm.faces.new((vb, bm_rings[0][j], bm_rings[0][i]))

    if cap_top:
        c1 = tip_point or tuple(sum(pt[d] for pt in rings[-1]) / m for d in range(3))
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
    outer_color=(0.12, 0.09, 0.20),
    inner_color=None,
    rim_color=None,
    signal: bool = False,
    shade_smooth: bool = False,
    ao_strength: float = 0.24,
    ground_ao: float = 0.10,
    bone: str | None = None,
    role: str | None = None,
) -> bpy.types.Object:
    """Build a closed double-walled manifold shell (FrontSide culling safe from every angle)."""
    rows = len(outer_grid)
    cols = len(outer_grid[0])
    bm = bmesh.new()
    vo = [[bm.verts.new(pt) for pt in row] for row in outer_grid]
    vi = [[bm.verts.new(pt) for pt in row] for row in inner_grid]

    outer_faces = []
    inner_faces = []
    rim_faces = []

    for r in range(rows - 1):
        for c in range(cols - 1):
            outer_faces.append(bm.faces.new((vo[r][c], vo[r][c + 1], vo[r + 1][c + 1], vo[r + 1][c])))
            inner_faces.append(bm.faces.new((vi[r][c], vi[r + 1][c], vi[r + 1][c + 1], vi[r][c + 1])))

    for c in range(cols - 1):
        rim_faces.append(bm.faces.new((vo[0][c + 1], vo[0][c], vi[0][c], vi[0][c + 1])))
        rim_faces.append(bm.faces.new((vo[-1][c], vo[-1][c + 1], vi[-1][c + 1], vi[-1][c])))

    for r in range(rows - 1):
        rim_faces.append(bm.faces.new((vo[r][0], vo[r + 1][0], vi[r + 1][0], vi[r][0])))
        rim_faces.append(bm.faces.new((vo[r + 1][-1], vo[r][-1], vi[r][-1], vi[r + 1][-1])))

    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    inner_tag = set(range(len(outer_faces), len(outer_faces) + len(inner_faces)))
    rim_tag = set(range(len(outer_faces) + len(inner_faces), len(bm.faces)))

    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()

    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    for poly in mesh.polygons:
        poly.use_smooth = bool(shade_smooth)
    mesh.validate()
    mesh.update()

    aa.paint_mesh(obj, color=outer_color, signal=signal, ao_strength=ao_strength, ground_ao=ground_ao)
    if inner_color is not None and not signal:
        aa.paint_faces(obj, lambda p, _c, _n: p.index in inner_tag, color=inner_color, ao_strength=ao_strength * 0.85)
    if rim_color is not None and not signal:
        aa.paint_faces(obj, lambda p, _c, _n: p.index in rim_tag, color=rim_color, ao_strength=ao_strength * 0.70)

    if bone:
        aa.pin_to_bone(obj, bone)
    if role:
        tag_skin_role(obj, role)
    return obj


def build_oriented_chain_loft(
    stations: Sequence[tuple[Vector, float, float]],
    name: str,
    segs: int = 8,
    color=(0.20, 0.24, 0.30),
    shade_smooth: bool = True,
    ao_strength: float = 0.25,
    bone: str | None = None,
    role: str | None = None,
) -> bpy.types.Object:
    """Loft an elliptical cross-section along 3D stations [(center, rx, ry), ...]."""
    pts = [Vector(s[0]) for s in stations]
    rings = []
    up_ref = Vector((0.0, 1.0, 0.0))
    for k, (center, rx, ry) in enumerate(stations):
        if k == 0:
            tang = (pts[1] - pts[0]).normalized()
        elif k == len(stations) - 1:
            tang = (pts[-1] - pts[-2]).normalized()
        else:
            tang = (pts[k + 1] - pts[k - 1]).normalized()
        if abs(tang.dot(up_ref)) > 0.92:
            side_u = Vector((1.0, 0.0, 0.0))
        else:
            side_u = up_ref.cross(tang).normalized()
        fwd_v = tang.cross(side_u).normalized()
        ring = []
        for i in range(segs):
            ang = (i / segs) * math.tau
            p = Vector(center) + side_u * (math.cos(ang) * rx) + fwd_v * (math.sin(ang) * ry)
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


def snapshot_skin_tags(meshes: Sequence[bpy.types.Object]) -> tuple[dict[str, dict[int, str]], dict[str, dict[int, str]]]:
    """Snapshot per-vertex '__pin_<Bone>' and '__role_<role>' tags prior to rigging."""
    pins: dict[str, dict[int, str]] = {}
    roles: dict[str, dict[int, str]] = {}
    for m_obj in meshes:
        if m_obj is None or m_obj.type != 'MESH':
            continue
        idx_pin: dict[int, str] = {}
        idx_role: dict[int, str] = {}
        for vg in m_obj.vertex_groups:
            if vg.name.startswith('__pin_'):
                idx_pin[vg.index] = vg.name[len('__pin_'):]
            elif vg.name.startswith('__role_'):
                rk = vg.name[len('__role_'):]
                if rk.endswith('_Mirror'):
                    rk = rk[:-7]
                idx_role[vg.index] = rk
        v_pin: dict[int, str] = {}
        v_role: dict[int, str] = {}
        for v in m_obj.data.vertices:
            for g_elem in v.groups:
                if g_elem.weight > 0.5:
                    if g_elem.group in idx_pin:
                        v_pin[v.index] = idx_pin[g_elem.group]
                    elif g_elem.group in idx_role:
                        v_role[v.index] = idx_role[g_elem.group]
        pins[m_obj.name] = v_pin
        roles[m_obj.name] = v_role
    return pins, roles


def _seg_dist(p: Vector, a: Vector, b: Vector) -> float:
    ab = b - a
    lsq = ab.length_squared
    if lsq < 1e-10:
        return (p - a).length
    t = max(0.0, min(1.0, (p - a).dot(ab) / lsq))
    return (p - (a + ab * t)).length


# ===========================================================================
# 1. RAZOR RONIN CYBER-ASSASSIN — public/street/foe-blade.glb
# ===========================================================================

def _build_ronin_katana(grip_origin: Vector) -> tuple[list[bpy.types.Object], list[bpy.types.Object]]:
    """Build a high-frequency glowing magenta Neo-Kyoto katana pinned to Hand_R."""
    body_out: list[bpy.types.Object] = []
    sig_out: list[bpy.types.Object] = []

    # Orthonormal right-handed weapon frame at Hand_R grip:
    #   +z_w = blade length direction (forward -Y and ~24 deg up)
    #   +x_w = dark titanium spine (mune), so -x_w = blazing magenta edge (ha) facing down-forward!
    #   +y_w = blade lateral flat
    z_axis = Vector((0.055, -0.910, 0.410)).normalized()
    x_raw = Vector((-0.020, 0.410, 0.912)).normalized()
    y_axis = z_axis.cross(x_raw).normalized()
    x_axis = y_axis.cross(z_axis).normalized()

    rot_mat = Matrix((
        (x_axis.x, y_axis.x, z_axis.x, grip_origin.x),
        (x_axis.y, y_axis.y, z_axis.y, grip_origin.y),
        (x_axis.z, y_axis.z, z_axis.z, grip_origin.z),
        (0.0, 0.0, 0.0, 1.0),
    ))

    def _place_on_hand(obj: bpy.types.Object) -> bpy.types.Object:
        obj.data.transform(rot_mat)
        obj.data.validate()
        obj.data.update()
        aa.paint_mesh(
            obj,
            color=obj.get('base_col', RONIN_COLORS['armor_obsidian']),
            signal=bool(obj.get('is_signal', False)),
            ao_strength=0.22,
            ground_ao=0.04,
            gradient_z=obj.get('grad_col', None),
        )
        aa.pin_to_bone(obj, 'Hand_R')
        return obj

    # 1. Kashira pommel cap + two-handed tsuka hilt (-0.21 .. +0.065)
    pommel = aa.add_bevel_box(
        name='Ronin_Kashira',
        size=(0.034, 0.022, 0.028),
        location=(0.0, 0.0, -0.212),
        bevel=0.005,
        segments=1,
        color=RONIN_COLORS['brass_odoshi'],
    )
    pommel['base_col'] = RONIN_COLORS['brass_odoshi']
    body_out.append(_place_on_hand(pommel))

    tsuka = aa.add_cylinder(
        name='Ronin_TsukaCore',
        radius=0.0155,
        depth=0.265,
        vertices=8,
        location=(0.0, 0.0, -0.072),
        scale=(1.15, 0.76, 1.0),
        shade_smooth=False,
        color=RONIN_COLORS['armor_obsidian'],
    )
    tsuka['base_col'] = RONIN_COLORS['armor_obsidian']
    body_out.append(_place_on_hand(tsuka))

    for idx, zc in enumerate((-0.170, -0.110, -0.050, 0.015)):
        band = aa.add_torus(
            name=f'Ronin_TsukaIto_{idx}',
            major_radius=0.0165,
            minor_radius=0.0042,
            major_segments=6,
            minor_segments=3,
            location=(0.0, 0.0, zc),
            scale=(1.16, 0.80, 1.0),
            shade_smooth=False,
            color=RONIN_COLORS['crimson_cord'],
        )
        band['base_col'] = RONIN_COLORS['crimson_cord']
        body_out.append(_place_on_hand(band))

    # 2. Hexagonal cyber-tsuba guard + habaki + high-frequency Signal oscillator collar
    tsuba = aa.add_cylinder(
        name='Ronin_CyberTsuba',
        radius=0.052,
        depth=0.014,
        vertices=8,
        location=(-0.004, 0.0, 0.068),
        scale=(1.18, 0.86, 1.0),
        shade_smooth=False,
        color=RONIN_COLORS['brass_odoshi'],
    )
    tsuba['base_col'] = RONIN_COLORS['brass_odoshi']
    body_out.append(_place_on_hand(tsuba))

    habaki = aa.add_box(
        name='Ronin_Habaki',
        size=(0.036, 0.018, 0.034),
        location=(0.001, 0.0, 0.090),
        color=RONIN_COLORS['armor_titanium'],
    )
    habaki['base_col'] = RONIN_COLORS['armor_titanium']
    body_out.append(_place_on_hand(habaki))

    osc_ring = aa.add_box(
        name='Ronin_OscillatorGlow',
        size=(0.042, 0.022, 0.014),
        location=(-0.002, 0.0, 0.080),
        color=RONIN_COLORS['sig_magenta'],
        signal=True,
    )
    osc_ring['base_col'] = RONIN_COLORS['sig_magenta']
    sig_out.append(_place_on_hand(osc_ring))

    # 3. Curved single-edged blade (0.105 .. 1.045, ~0.94m blade!)
    #    Spine (Body) on +x_local, blazing magenta HF razor edge (Signal) on -x_local!
    z0, z1 = 0.102, 1.045
    blade_len = z1 - z0
    n_st = 9

    def spine_x(s: float) -> float:
        return 0.015 - 0.004 * s + 0.040 * (s ** 1.92)

    def blade_w(s: float) -> float:
        if s <= 0.86:
            return _lerp(0.034, 0.026, (s / 0.86) ** 0.88)
        uk = (s - 0.86) / 0.14
        return 0.026 * max(0.06, 1.0 - (uk ** 1.55))

    spine_rings = []
    edge_rings = []
    for i in range(n_st):
        s = i / (n_st - 1)
        zl = z0 + s * blade_len
        xs = spine_x(s)
        w = blade_w(s)
        x_join = xs - w * 0.56
        x_ha = xs - w
        th = _lerp(0.0058, 0.0022, s)
        th_join = th * 0.68

        # 6-vert closed titanium spine + shinogi ridge cross-section
        spine_rings.append([
            (xs + 0.002,  0.0,      zl),
            (xs - w * 0.22, +th,    zl),
            (x_join,     +th_join,  zl),
            (x_join,     -th_join,  zl),
            (xs - w * 0.22, -th,    zl),
            (xs,          0.0,      zl),
        ])

        # 4-vert diamond high-frequency magenta Signal plasma edge cross-section
        edge_rings.append([
            (x_join + 0.0015, +th_join * 0.92, zl),
            (x_ha,             0.0,            zl),
            (x_join + 0.0015, -th_join * 0.92, zl),
            (x_join + 0.0035,  0.0,            zl),
        ])

    tip_xs = spine_x(1.0)
    blade_body = build_lofted_tube(
        spine_rings,
        name='Ronin_KatanaSpine',
        color=RONIN_COLORS['armor_obsidian'],
        tip_point=(tip_xs - 0.003, 0.0, z1 + 0.012),
        shade_smooth=False,
    )
    blade_body['base_col'] = RONIN_COLORS['armor_titanium']
    blade_body['grad_col'] = (RONIN_COLORS['armor_obsidian'], RONIN_COLORS['armor_titanium'])
    body_out.append(_place_on_hand(blade_body))

    blade_sig = build_lofted_tube(
        edge_rings,
        name='Ronin_KatanaPlasmaEdge',
        color=RONIN_COLORS['sig_magenta'],
        signal=True,
        tip_point=(tip_xs - 0.004, 0.0, z1 + 0.024),
        shade_smooth=False,
    )
    blade_sig['base_col'] = RONIN_COLORS['sig_magenta']
    blade_sig['grad_col'] = (RONIN_COLORS['sig_magenta'], RONIN_COLORS['sig_hot_pink'])
    sig_out.append(_place_on_hand(blade_sig))

    return body_out, sig_out


def apply_ronin_skinning(
    rig: bpy.types.Object,
    meshes: Sequence[bpy.types.Object],
    pin_snap: dict[str, dict[int, str]],
    role_snap: dict[str, dict[int, str]],
    height: float,
) -> None:
    """Apply crisp rigid armor/weapon pins + smooth multi-bone garment weights on RoninRig."""
    bone_segs = {b.name: (b.head_local.copy(), b.tail_local.copy()) for b in rig.data.bones}
    h = float(height)
    pelvis_z = 0.48 * h
    spine_z = 0.57 * h
    chest_z = 0.68 * h
    neck_z = 0.78 * h
    hip_z = 0.505 * h
    ankle_z = 0.080 * h

    for m_obj in meshes:
        v_pins = pin_snap.get(m_obj.name, {})
        v_roles = role_snap.get(m_obj.name, {})
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

        for vi, bn in v_pins.items():
            if bn in vgs:
                set_weights(vi, [(bn, 1.0)])

        for vi, role in v_roles.items():
            if vi in v_pins:
                continue
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
                    sh_share = 0.26 * _smoothstep(0.072 * h, 0.130 * h, abs(p.x)) * _smoothstep(chest_z, neck_z, p.z)
                    set_weights(vi, [
                        ('Chest', (1.0 - 0.60 * t_neck) * (1.0 - sh_share)),
                        ('Neck', (0.60 * t_neck) * (1.0 - sh_share)),
                        (f'Clavicle_{side}', sh_share),
                    ])
            elif role == 'collar':
                t_up = _smoothstep(neck_z - 0.02 * h, neck_z + 0.07 * h, p.z)
                set_weights(vi, [('Chest', 1.0 - 0.74 * t_up), ('Neck', 0.54 * t_up), ('Head', 0.20 * t_up)])
            elif role == 'pelvis_saddle':
                t_th = _smoothstep(0.025 * h, 0.070 * h, abs(p.x)) * _smoothstep(hip_z + 0.015 * h, hip_z - 0.045 * h, p.z)
                set_weights(vi, [('Pelvis', 1.0 - 0.65 * t_th), (f'Thigh_{side}', 0.65 * t_th)])
            elif role == 'coat_skirt':
                waist_top = hip_z + 0.025 * h
                thigh_lock = hip_z - 0.080 * h
                t_thigh = _smoothstep(waist_top, thigh_lock, p.z)
                t_spine = _smoothstep(pelvis_z, waist_top + 0.02 * h, p.z)
                torso_part = 1.0 - t_thigh
                set_weights(vi, [
                    ('Spine', torso_part * 0.36 * t_spine),
                    ('Pelvis', torso_part * (1.0 - 0.36 * t_spine)),
                    (f'Thigh_{side}', t_thigh),
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
                t_shin = _smoothstep(ankle_z + 0.015 * h, ankle_z + 0.068 * h, p.z)
                t_toe = _smoothstep(-0.072 * h, -0.118 * h, p.y) * (1.0 - t_shin)
                w_foot = max(0.0, 1.0 - t_shin - t_toe)
                set_weights(vi, [(f'Shin_{side}', t_shin), (f'Foot_{side}', w_foot), (f'Toe_{side}', t_toe)])


def author_ronin_clips(rig: bpy.types.Object, height: float) -> None:
    """Replace actions with bespoke 24 fps Razor Ronin samurai martial-arts clips."""
    for act in list(bpy.data.actions):
        bpy.data.actions.remove(act)

    h = float(height)
    pelvis_z = 0.48 * h
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

    specs = [
        ('Idle', 2.40),
        ('Walk', 0.96),
        ('Run', 0.64),
        ('Punch', 0.64),
        ('Sword_Slash', 0.72),
        ('HitReact', 0.44),
        ('Death', 1.16),
    ]

    for clip_name, dur in specs:
        act = bpy.data.actions.new(clip_name)
        act.use_fake_user = True
        rig.animation_data.action = act
        frames = max(6, round(dur * aa.FPS))

        for f in range(frames + 1):
            u = f / frames
            phase = u * math.tau
            reset_pose()

            if clip_name == 'Idle':
                # Poised Neo-Kyoto chudan/waki kamae guard, steady breathing & predatory Oni scan
                breath = math.sin(phase)
                sway = math.sin(phase + 0.5)
                move_pelvis(dx=0.012 * h * sway, dz=-0.012 * h - 0.006 * h * abs(breath))
                rot('Pelvis', x=0.04, y=-0.02 * sway, z=-0.22 + 0.02 * sway)
                rot('Spine', x=0.03 - 0.015 * breath, z=-0.08)
                rot('Chest', x=-0.02 * breath, z=-0.08 + 0.02 * sway)
                rot('Neck', z=0.14)
                rot('Head', x=0.04 + 0.02 * breath, z=0.24 + 0.05 * math.sin(phase + 0.9))

                rot('Thigh_L', x=-0.14, y=-0.05)
                rot('Shin_L', x=0.22)
                rot('Foot_L', x=-0.08)
                rot('Thigh_R', x=0.08, y=0.06)
                rot('Shin_R', x=0.14)
                rot('Foot_R', x=-0.18)

                rot('Clavicle_R', z=0.08)
                rot('UpperArm_R', x=-0.42 + 0.03 * breath, y=0.12, z=0.18)
                rot('Forearm_R', x=-0.56 - 0.04 * breath)
                rot('Hand_R', x=0.16)
                rot('UpperArm_L', x=-0.28 - 0.02 * breath, y=-0.14, z=-0.18)
                rot('Forearm_L', x=-0.52)

            elif clip_name in ('Walk', 'Run'):
                run = clip_name == 'Run'
                sb = math.sin(phase)
                cb = math.cos(phase)
                bob = (0.034 if run else 0.016) * h * math.cos(2.0 * phase) - (0.032 if run else 0.014) * h
                move_pelvis(dx=0.014 * h * sb, dz=bob)

                lean = 0.24 if run else 0.06
                twist = (0.12 if run else 0.07) * sb
                rot('Pelvis', x=lean * 0.55, y=-0.04 * sb, z=-twist - (0.12 if run else 0.08))
                rot('Spine', x=lean * 0.85, z=twist * 0.7)
                rot('Chest', x=lean * 0.75, z=twist * 1.1)
                rot('Neck', x=-lean * 0.50, z=0.08 - twist * 0.4)
                rot('Head', x=-lean * 0.75, z=0.06 - twist * 0.5)

                for side, sign in (('L', 1.0), ('R', -1.0)):
                    sw = sb * sign
                    cw = cb * sign
                    t_amp = 0.82 if run else 0.44
                    lift = max(0.0, cw) * (0.22 if run else 0.10)
                    knee = (0.14 if run else 0.06) + max(0.0, -sw) * (0.86 if run else 0.42) + max(0.0, cw) * (0.48 if run else 0.28)
                    ank = (0.32 if run else 0.20) * max(0.0, -sw) - (0.18 if run else 0.14) * max(0.0, sw)
                    rot(f'Thigh_{side}', x=-t_amp * sw - lift, y=-sign * 0.02)
                    rot(f'Shin_{side}', x=knee)
                    rot(f'Foot_{side}', x=ank)
                    rot(f'Toe_{side}', x=(0.26 if run else 0.16) * max(0.0, -sw))

                if run:
                    # Low-slung assassin sprint: katana swept back-and-low on right flank, left arm pumping
                    rot('UpperArm_R', x=0.42 + 0.10 * sb, y=0.22, z=-0.16)
                    rot('Forearm_R', x=-0.38)
                    rot('Hand_R', x=-0.18)
                    rot('UpperArm_L', x=0.65 * sb - 0.18, y=-0.12)
                    rot('Forearm_L', x=-0.88 - 0.20 * max(0.0, -sb))
                else:
                    # Predatory Ronin stalk: katana poised forward-side, left hand guarding saya hip
                    rot('UpperArm_R', x=-0.28 - 0.12 * sb, y=0.14, z=0.10)
                    rot('Forearm_R', x=-0.48)
                    rot('Hand_R', x=0.12)
                    rot('UpperArm_L', x=0.22 * sb - 0.12, y=-0.14, z=-0.12)
                    rot('Forearm_L', x=-0.46)

            elif clip_name == 'Sword_Slash':
                # Dramatic sweeping diagonal iai / kesa-giri katana cleave!
                # Phase 1 (0..0.34): Coiled high-right jodan/iai chamber
                # Phase 2 (0.32..0.58): Explosive forward lunge + diagonal cleave across midline to low-left
                # Phase 3 (0.58..1.0): Poised samurai follow-through & return
                wind = _smoothstep(0.0, 0.32, u) * (1.0 - _smoothstep(0.32, 0.46, u))
                cleave = _smoothstep(0.30, 0.52, u) * (1.0 - _smoothstep(0.72, 1.0, u))
                snap = _smoothstep(0.34, 0.48, u) * (1.0 - _smoothstep(0.52, 0.68, u))

                move_pelvis(
                    dx=0.025 * h * cleave - 0.015 * h * wind,
                    dy=-0.085 * h * cleave + 0.030 * h * wind,
                    dz=-0.068 * h * cleave - 0.020 * h * wind,
                )
                rot('Pelvis', x=0.18 * cleave - 0.06 * wind, y=0.06 * cleave, z=0.38 * cleave - 0.34 * wind)
                rot('Spine', x=0.22 * cleave - 0.12 * wind, y=-0.08 * cleave, z=0.42 * cleave - 0.36 * wind)
                rot('Chest', x=0.18 * cleave - 0.10 * wind, y=-0.12 * cleave + 0.08 * wind, z=0.48 * cleave - 0.42 * wind)
                rot('Neck', x=-0.08 * cleave, z=-0.28 * cleave + 0.24 * wind)
                rot('Head', x=-0.12 * cleave + 0.06 * wind, z=-0.46 * cleave + 0.42 * wind)

                # Right sword arm sweeps from high-right overhead diagonally down-left across the target
                rot('Clavicle_R', y=0.20 * wind - 0.10 * cleave, z=0.34 * cleave - 0.22 * wind)
                rot(
                    'UpperArm_R',
                    x=-1.82 * wind - 0.88 * snap + 0.08 * cleave,
                    y=-0.34 * wind + 0.44 * cleave,
                    z=-0.32 * wind + 0.85 * cleave,
                )
                rot('Forearm_R', x=-0.78 * wind - 0.22 * snap - 0.06 * cleave)
                rot('Hand_R', x=-0.22 * wind + 0.35 * snap + 0.88 * cleave, y=-0.18 * cleave)

                # Left hand guides saya / counter-balances wide samurai cut
                rot('UpperArm_L', x=-0.95 * wind + 0.38 * cleave, y=-0.18 - 0.24 * cleave, z=-0.36 * wind + 0.18 * cleave)
                rot('Forearm_L', x=-0.72 * wind - 0.44 * cleave)

                # Deep martial lunge on lead left leg
                rot('Thigh_L', x=-0.46 * cleave - 0.14 * wind, y=-0.08 * cleave)
                rot('Shin_L', x=0.62 * cleave + 0.20 * wind)
                rot('Foot_L', x=-0.16 * cleave)
                rot('Thigh_R', x=0.34 * cleave + 0.08 * wind, y=0.08 * cleave)
                rot('Shin_R', x=0.26 * cleave + 0.14 * wind)
                rot('Foot_R', x=-0.24 * cleave)

            elif clip_name == 'Punch':
                # Fast close-quarters left tekko gauntlet palm-strike + tsuba blitz
                wind = _smoothstep(0.0, 0.24, u) * (1.0 - _smoothstep(0.24, 0.38, u))
                strike = _smoothstep(0.22, 0.44, u) * (1.0 - _smoothstep(0.66, 1.0, u))
                move_pelvis(dy=-0.045 * h * strike + 0.015 * h * wind, dz=-0.035 * h * strike)
                rot('Pelvis', x=0.10 * strike, z=-0.24 * strike + 0.18 * wind)
                rot('Spine', x=0.12 * strike, z=-0.28 * strike + 0.20 * wind)
                rot('Chest', x=0.10 * strike, z=-0.34 * strike + 0.24 * wind)
                rot('Head', x=-0.08 * strike, z=0.38 * strike - 0.26 * wind)

                rot('Clavicle_L', z=-0.22 * strike + 0.12 * wind)
                rot('UpperArm_L', x=0.40 * wind - 1.52 * strike, y=-0.18 * wind - 0.06 * strike, z=-0.20 * strike)
                rot('Forearm_L', x=-1.10 * wind - 0.14 * strike)
                rot('UpperArm_R', x=-0.38 - 0.20 * strike, y=0.18, z=0.14)
                rot('Forearm_R', x=-0.68)

                rot('Thigh_R', x=-0.26 * strike)
                rot('Shin_R', x=0.34 * strike)
                rot('Thigh_L', x=0.18 * strike)
                rot('Shin_L', x=0.18 * strike)

            elif clip_name == 'HitReact':
                hit = math.sin(math.pi * (u ** 0.58))
                move_pelvis(dy=0.038 * h * hit, dz=-0.030 * h * hit)
                rot('Pelvis', x=-0.10 * hit, z=-0.14 * hit)
                rot('Spine', x=-0.24 * hit, y=0.06 * hit, z=-0.18 * hit)
                rot('Chest', x=-0.22 * hit, y=0.08 * hit, z=-0.22 * hit)
                rot('Head', x=-0.26 * hit, z=0.22 * hit)
                # Raises katana across chest in reflex guard
                rot('UpperArm_R', x=-0.72 * hit, y=0.18 * hit, z=0.42 * hit)
                rot('Forearm_R', x=-0.82 * hit)
                rot('UpperArm_L', x=-0.42 * hit, y=-0.28 * hit)
                rot('Forearm_L', x=-0.64 * hit)
                rot('Thigh_L', x=-0.18 * hit)
                rot('Shin_L', x=0.30 * hit)
                rot('Thigh_R', x=-0.14 * hit)
                rot('Shin_R', x=0.26 * hit)

            elif clip_name == 'Death':
                buckle = _smoothstep(0.0, 0.36, u) * (1.0 - _smoothstep(0.45, 0.84, u))
                fall = _smoothstep(0.30, 0.88, u)
                drop_z = -(pelvis_z - 0.140 * h) * fall - 0.18 * h * buckle
                move_pelvis(dy=-0.08 * h * fall + 0.03 * h * buckle, dz=drop_z)

                rot('Pelvis', x=-0.14 * buckle + 1.46 * fall, y=0.06 * fall, z=0.08 * fall)
                rot('Spine', x=-0.18 * buckle - 0.03 * fall, z=-0.12 * buckle)
                rot('Chest', x=-0.14 * buckle - 0.04 * fall, z=0.06 * fall)
                rot('Head', x=-0.18 * buckle - 0.08 * fall, z=0.42 * fall)

                for side, sign in (('L', 1.0), ('R', -1.0)):
                    rot(f'Thigh_{side}', x=-0.56 * buckle + 0.06 * fall, y=-sign * 0.09 * fall)
                    rot(f'Shin_{side}', x=1.06 * buckle + 0.04 * fall)
                    rot(f'Foot_{side}', x=-0.24 * buckle + 0.42 * fall)

                rot('UpperArm_L', x=-0.36 * buckle - 0.06 * fall, y=-0.28 * buckle - 0.56 * fall)
                rot('Forearm_L', x=-0.62 * buckle - 0.18 * fall, z=-0.48 * fall)
                # Flare sword arm outward and roll wrist so katana lays flat along ground
                rot('UpperArm_R', x=-0.32 * buckle + 0.12 * fall, y=0.32 * buckle + 0.72 * fall)
                rot('Forearm_R', x=-0.50 * buckle - 0.12 * fall)
                rot('Hand_R', x=-0.35 * fall, y=0.65 * fall)

            key_all(f + 1)

        for fc in act.fcurves:
            for kp in fc.keyframe_points:
                kp.interpolation = 'LINEAR'

    rig.animation_data.action = None
    reset_pose()
    bpy.context.scene.frame_set(1)


def build_foe_blade() -> Path:
    """Sculpt, rig, animate, and export public/street/foe-blade.glb (Razor Ronin)."""
    aa.reset_scene()
    H = 1.80
    sh_x = 0.228
    hp_x = 0.114
    elbow_L = Vector((sh_x + H * 0.036, 0.015 * H, 0.618 * H))
    wrist_L = Vector((sh_x + H * 0.034, -0.025 * H, 0.485 * H))
    hand_L = Vector((sh_x + H * 0.034, -0.038 * H, 0.415 * H))
    hip_L = Vector((hp_x, 0.0, 0.505 * H))
    knee_L = Vector((hp_x + H * 0.012, -0.012 * H, 0.285 * H))
    ankle_L = Vector((hp_x + H * 0.018, 0.010 * H, 0.080 * H))

    body_parts: list[bpy.types.Object] = []
    sig_parts: list[bpy.types.Object] = []

    # --- A. Horned Oni Faceplate, Kabuto Cowl, Shikoro & Blazing Magenta Eye-Slits ---
    neck_col = aa.add_cylinder(
        name='Ronin_NodowaNeck',
        radius=0.060,
        radius_top=0.054,
        depth=0.195,
        vertices=10,
        location=(0.0, -0.008, 1.455),
        shade_smooth=True,
        color=RONIN_COLORS['cyber_weave'],
        bone='Neck',
    )
    body_parts.append(neck_col)

    # Stealth kabuto cranium dome
    kabuto_rings = [
        [(math.cos(a) * 0.098, math.sin(a) * 0.112, 1.525) for a in (i * math.tau / 8 for i in range(8))],
        [(math.cos(a) * 0.114, math.sin(a) * 0.128, 1.605) for a in (i * math.tau / 8 for i in range(8))],
        [(math.cos(a) * 0.112, math.sin(a) * 0.125, 1.695) for a in (i * math.tau / 8 for i in range(8))],
        [(math.cos(a) * 0.084, math.sin(a) * 0.098, 1.768) for a in (i * math.tau / 8 for i in range(8))],
        [(math.cos(a) * 0.042, math.sin(a) * 0.052, 1.800) for a in (i * math.tau / 8 for i in range(8))],
    ]
    body_parts.append(build_lofted_tube(
        kabuto_rings,
        name='Ronin_KabutoDome',
        color=RONIN_COLORS['armor_obsidian'],
        shade_smooth=False,
        bone='Head',
    ))

    # Swept cyber-chonmage topknot + brass band at rear crown
    body_parts.append(aa.add_cylinder(
        name='Ronin_TopknotTie',
        radius=0.022,
        depth=0.036,
        vertices=6,
        location=(0.0, 0.048, 1.792),
        rotation=(-0.55, 0.0, 0.0),
        shade_smooth=False,
        color=RONIN_COLORS['brass_odoshi'],
        bone='Head',
    ))
    body_parts.append(aa.add_cone(
        name='Ronin_ChonmagePlume',
        radius1=0.030,
        radius2=0.010,
        depth=0.135,
        vertices=6,
        location=(0.0, 0.102, 1.812),
        rotation=(-1.12, 0.0, 0.0),
        shade_smooth=False,
        color=RONIN_COLORS['armor_obsidian'],
        bone='Head',
    ))

    # 3-tier shikoro armored nape-guard plates wrapping back & sides of helmet
    for t_i in range(3):
        zt = 1.615 - t_i * 0.042
        rad_x = 0.124 + t_i * 0.014
        rad_y = 0.126 + t_i * 0.012
        sh_outer = []
        sh_inner = []
        for r_z in (zt - 0.022, zt + 0.018):
            ro = []
            ri = []
            for k in range(5):
                # Arc around rear/sides (-0.12*pi .. +1.12*pi in +Y back hemisphere)
                ang = -0.12 * math.pi + (k / 4.0) * (1.24 * math.pi)
                ca, sa = math.cos(ang), math.sin(ang)
                flare = 1.06 if r_z < zt else 0.96
                ro.append((ca * rad_x * flare, sa * rad_y * flare, r_z))
                ri.append((ca * (rad_x * flare - 0.014), sa * (rad_y * flare - 0.014), r_z))
            sh_outer.append(ro)
            sh_inner.append(ri)
        body_parts.append(build_thick_shell(
            sh_outer,
            sh_inner,
            name=f'Ronin_Shikoro_{t_i}',
            outer_color=RONIN_COLORS['armor_obsidian'],
            inner_color=RONIN_COLORS['crimson_cord'],
            rim_color=RONIN_COLORS['armor_titanium'] if t_i == 2 else None,
            shade_smooth=False,
            bone='Head',
        ))

    # Sculpted Oni faceplate (menpo) on -Y front face
    menpo_rings = [
        [(0.0, -0.095, 1.500), (0.056, -0.088, 1.508), (0.072, -0.042, 1.522), (-0.072, -0.042, 1.522), (-0.056, -0.088, 1.508)],
        [(0.0, -0.132, 1.548), (0.078, -0.108, 1.552), (0.094, -0.045, 1.565), (-0.094, -0.045, 1.565), (-0.078, -0.108, 1.552)],
        [(0.0, -0.138, 1.608), (0.084, -0.112, 1.606), (0.098, -0.048, 1.615), (-0.098, -0.048, 1.615), (-0.084, -0.112, 1.606)],
        [(0.0, -0.124, 1.668), (0.076, -0.105, 1.666), (0.090, -0.050, 1.668), (-0.090, -0.050, 1.668), (-0.076, -0.105, 1.666)],
    ]
    body_parts.append(build_lofted_tube(
        menpo_rings,
        name='Ronin_OniFaceplate',
        color=RONIN_COLORS['oni_mask'],
        shade_smooth=False,
        ridge_boost=0.14,
        bone='Head',
    ))

    # Furrowed Oni brow crest + brass v-horn mount (maedate)
    brow_L = aa.add_bevel_box(
        name='Ronin_OniBrow_L',
        size=(0.088, 0.036, 0.022),
        location=(0.044, -0.124, 1.656),
        rotation=(0.12, -0.24, 0.16),
        bevel=0.004,
        segments=1,
        color=RONIN_COLORS['brass_odoshi'],
        bone='Head',
    )
    body_parts.extend([brow_L, mirror_clean(brow_L)])

    # Upward-curving lower Oni fangs on menpo jaw
    fang_L = aa.add_cone(
        name='Ronin_OniFang_L',
        radius1=0.013,
        radius2=0.002,
        depth=0.052,
        vertices=6,
        location=(0.044, -0.134, 1.555),
        rotation=(0.35, 0.16, 0.0),
        shade_smooth=False,
        color=RONIN_COLORS['oni_ivory'],
        bone='Head',
    )
    body_parts.extend([fang_L, mirror_clean(fang_L)])

    # Twin swept Oni horns erupting from brow temples up to z = 1.855m
    horn_stations_L = [
        (Vector((0.056, -0.098, 1.672)), 0.021),
        (Vector((0.074, -0.126, 1.716)), 0.017),
        (Vector((0.088, -0.144, 1.766)), 0.012),
        (Vector((0.095, -0.148, 1.816)), 0.007),
    ]
    horn_rings_L = []
    for c_pt, r_h in horn_stations_L:
        horn_rings_L.append([
            (c_pt.x + math.cos(a) * r_h, c_pt.y + math.sin(a) * r_h, c_pt.z)
            for a in (i * math.tau / 6 for i in range(6))
        ])
    horn_L = build_lofted_tube(
        horn_rings_L,
        name='Ronin_OniHorn_L',
        color=RONIN_COLORS['oni_ivory'],
        gradient_z=(RONIN_COLORS['armor_obsidian'], RONIN_COLORS['oni_ivory']),
        tip_point=(0.098, -0.142, 1.856),
        shade_smooth=False,
        bone='Head',
    )
    body_parts.extend([horn_L, mirror_clean(horn_L)])

    # Blazing Magenta Signal Eye-Slits + Oni Forehead Third-Eye Sigil + Cheek Vents
    eye_slit_L = aa.add_box(
        name='Ronin_OniEyeSlit_L',
        size=(0.058, 0.022, 0.015),
        location=(0.040, -0.130, 1.632),
        rotation=(0.08, -0.26, 0.22),
        taper=(0.75, 0.85),
        color=RONIN_COLORS['sig_magenta'],
        signal=True,
        bone='Head',
    )
    cheek_sig_L = aa.add_box(
        name='Ronin_OniCheekVent_L',
        size=(0.032, 0.014, 0.007),
        location=(0.058, -0.118, 1.584),
        rotation=(0.0, -0.22, 0.35),
        color=RONIN_COLORS['sig_magenta'],
        signal=True,
        bone='Head',
    )
    third_eye = aa.add_box(
        name='Ronin_OniThirdEye',
        size=(0.016, 0.018, 0.034),
        location=(0.0, -0.128, 1.682),
        rotation=(0.14, 0.0, 0.0),
        taper=(0.55, 0.85),
        color=RONIN_COLORS['sig_hot_pink'],
        signal=True,
        bone='Head',
    )
    sig_parts.extend([eye_slit_L, mirror_clean(eye_slit_L), cheek_sig_L, mirror_clean(cheek_sig_L), third_eye])

    # --- B. Cyber-Do Cuirass + Sleeveless Armored Jinbaori Coat + Saya Scabbard ---
    torso_stations = [
        (0.90, 0.142, 0.104),
        (1.02, 0.136, 0.102),
        (1.16, 0.162, 0.118),
        (1.31, 0.192, 0.126),
        (1.41, 0.156, 0.104),
    ]
    torso_rings = []
    for zt, rx, ry in torso_stations:
        ring = []
        for i in range(10):
            ang = (i / 10) * math.tau
            # Broader heroic V-taper chest & sculpted jinbaori shoulders
            x_pt = math.cos(ang) * rx
            y_pt = math.sin(ang) * ry * (1.06 if math.sin(ang) < 0 else 0.96)
            ring.append((x_pt, y_pt, zt))
        torso_rings.append(ring)
    jinbaori_vest = build_lofted_tube(
        torso_rings,
        name='Ronin_JinbaoriTorso',
        color=RONIN_COLORS['jinbaori_outer'],
        shade_smooth=False,
        ao_strength=0.26,
        role='torso',
    )
    body_parts.append(jinbaori_vest)

    # Armored Cyber-Do front breastplate inset + brass lapels
    chest_plate = aa.add_bevel_box(
        name='Ronin_CyberDoPlate',
        size=(0.220, 0.056, 0.255),
        location=(0.0, -0.112, 1.245),
        taper=(1.14, 0.92),
        bevel=0.012,
        segments=1,
        color=RONIN_COLORS['armor_obsidian'],
        bone='Chest',
    )
    body_parts.append(chest_plate)

    lapel_L = aa.add_bevel_box(
        name='Ronin_JinbaoriLapel_L',
        size=(0.044, 0.034, 0.340),
        location=(0.098, -0.122, 1.225),
        rotation=(0.05, -0.14, 0.08),
        bevel=0.005,
        segments=1,
        color=RONIN_COLORS['jinbaori_lapel'],
    )
    tag_skin_role(lapel_L, 'torso')
    lapel_R = mirror_clean(lapel_L)
    body_parts.extend([lapel_L, lapel_R])

    # Standing samurai jinbaori high collar
    col_outer = []
    col_inner = []
    for zc, flare in ((1.385, 1.0), (1.475, 1.12)):
        ro, ri = [], []
        for k in range(8):
            ang = -0.22 * math.pi + (k / 7.0) * (1.44 * math.pi)
            ca, sa = math.cos(ang), math.sin(ang)
            ro.append((ca * 0.118 * flare, sa * 0.108 * flare, zc))
            ri.append((ca * 0.102 * flare, sa * 0.092 * flare, zc))
        col_outer.append(ro)
        col_inner.append(ri)
    body_parts.append(build_thick_shell(
        col_outer,
        col_inner,
        name='Ronin_HighCollar',
        outer_color=RONIN_COLORS['jinbaori_outer'],
        inner_color=RONIN_COLORS['jinbaori_lining'],
        rim_color=RONIN_COLORS['jinbaori_lapel'],
        shade_smooth=False,
        role='collar',
    ))

    # Glowing Magenta Signal Neo-Kyoto Clan Mon on Chest + Dorsal Spine Conduit
    sig_parts.append(aa.add_torus(
        name='Ronin_ChestClanMon',
        major_radius=0.036,
        minor_radius=0.0065,
        major_segments=8,
        minor_segments=4,
        location=(0.0, -0.141, 1.275),
        rotation=(math.pi * 0.5, 0.0, 0.0),
        shade_smooth=False,
        color=RONIN_COLORS['sig_magenta'],
        signal=True,
        bone='Chest',
    ))
    for k_sp, z_sp in enumerate((1.12, 1.20, 1.28, 1.36)):
        sig_parts.append(aa.add_box(
            name=f'Ronin_DorsalSpineNode_{k_sp}',
            size=(0.030, 0.022, 0.044),
            location=(0.0, 0.122, z_sp),
            taper=(0.82, 0.85),
            color=RONIN_COLORS['sig_magenta'],
            signal=True,
            bone='Chest' if z_sp > 1.18 else 'Spine',
        ))

    # Cinched Obi Waist Sash + Brass Clasp + Pelvis Saddle
    obi = aa.add_cylinder(
        name='Ronin_ObiSash',
        radius=0.148,
        radius_top=0.144,
        depth=0.095,
        vertices=12,
        location=(0.0, 0.0, 0.945),
        scale=(1.04, 0.78, 1.0),
        shade_smooth=False,
        color=RONIN_COLORS['crimson_cord'],
    )
    tag_skin_role(obi, 'torso')
    body_parts.append(obi)

    body_parts.append(aa.add_bevel_box(
        name='Ronin_ObiClasp',
        size=(0.076, 0.034, 0.056),
        location=(0.0, -0.118, 0.948),
        bevel=0.005,
        segments=1,
        color=RONIN_COLORS['brass_odoshi'],
        bone='Pelvis',
    ))

    pelvis_blk = aa.add_bevel_box(
        name='Ronin_PelvisSaddle',
        size=(0.255, 0.195, 0.150),
        location=(0.0, 0.0, 0.865),
        taper=(1.08, 1.04),
        bevel=0.018,
        segments=1,
        color=RONIN_COLORS['hakama_dark'],
    )
    tag_skin_role(pelvis_blk, 'pelvis_saddle')
    body_parts.append(pelvis_blk)

    # Front kusazuri armored groin tasset
    body_parts.append(aa.add_bevel_box(
        name='Ronin_FrontKusazuri',
        size=(0.135, 0.026, 0.175),
        location=(0.0, -0.116, 0.825),
        rotation=(0.10, 0.0, 0.0),
        taper=(0.82, 1.0),
        bevel=0.005,
        segments=1,
        color=RONIN_COLORS['armor_obsidian'],
        bone='Pelvis',
    ))

    # Split Sleeveless Jinbaori Coat Tails (Left + Mirrored Right double-walled manifold shells)
    sk_outer_L = []
    sk_inner_L = []
    z_levels = (0.555, 0.675, 0.795, 0.915)
    for r_i, zl in enumerate(z_levels):
        u_up = r_i / (len(z_levels) - 1)
        flare_x = _lerp(0.225, 0.152, u_up)
        flare_y = _lerp(0.158, 0.118, u_up)
        ro, ri = [], []
        # Sweep from front-side (-0.36*pi) around outer hip (0) to rear-center (+0.44*pi)
        for c_i in range(6):
            ang = -0.34 * math.pi + (c_i / 5.0) * (0.78 * math.pi)
            xo = max(0.024, math.cos(ang) * flare_x + 0.025 * (1.0 - u_up))
            yo = math.sin(ang) * flare_y
            ro.append((xo, yo, zl))
            ri.append((max(0.018, xo - 0.015 * math.cos(ang)), yo - 0.015 * math.sin(ang), zl))
        sk_outer_L.append(ro)
        sk_inner_L.append(ri)

    coat_tail_L = build_thick_shell(
        sk_outer_L,
        sk_inner_L,
        name='Ronin_JinbaoriTail_L',
        outer_color=RONIN_COLORS['jinbaori_outer'],
        inner_color=RONIN_COLORS['jinbaori_lining'],
        rim_color=RONIN_COLORS['jinbaori_lapel'],
        shade_smooth=False,
        role='coat_skirt',
    )
    body_parts.extend([coat_tail_L, mirror_clean(coat_tail_L)])

    tail_pip_L = aa.add_box(
        name='Ronin_JinbaoriHemPip_L',
        size=(0.014, 0.052, 0.018),
        location=(0.208, -0.048, 0.575),
        rotation=(0.0, 0.0, -0.22),
        color=RONIN_COLORS['sig_magenta'],
        signal=True,
    )
    tag_skin_role(tail_pip_L, 'coat_skirt')
    sig_parts.extend([tail_pip_L, mirror_clean(tail_pip_L)])

    # Left-Hip Lacquered Saya Scabbard (pinned cleanly to Pelvis at left obi)
    saya_p0 = Vector((0.152, -0.145, 1.005))
    saya_p1 = Vector((0.188, 0.495, 0.645))
    saya_body = build_oriented_chain_loft(
        [
            (saya_p0, 0.016, 0.024),
            (_vlerp(saya_p0, saya_p1, 0.45), 0.015, 0.023),
            (_vlerp(saya_p0, saya_p1, 0.85), 0.013, 0.020),
            (saya_p1, 0.010, 0.015),
        ],
        name='Ronin_SayaScabbard',
        segs=7,
        color=RONIN_COLORS['armor_obsidian'],
        shade_smooth=False,
        bone='Pelvis',
    )
    body_parts.append(saya_body)
    body_parts.append(aa.add_box(
        name='Ronin_SayaObiBracket',
        size=(0.036, 0.052, 0.048),
        location=(0.148, -0.025, 0.945),
        color=RONIN_COLORS['crimson_cord'],
        bone='Pelvis',
    ))

    saya_rot = Vector((0.0, 0.0, 1.0)).rotation_difference((saya_p1 - saya_p0).normalized())
    body_parts.append(aa.add_cylinder(
        name='Ronin_SayaKoiguchi',
        radius=0.024,
        depth=0.032,
        vertices=8,
        location=saya_p0,
        rotation=saya_rot,
        scale=(0.75, 1.08, 1.0),
        shade_smooth=False,
        color=RONIN_COLORS['brass_odoshi'],
        bone='Pelvis',
    ))
    sig_parts.append(aa.add_torus(
        name='Ronin_SayaChargeRing',
        major_radius=0.023,
        minor_radius=0.0045,
        major_segments=8,
        minor_segments=4,
        location=_vlerp(saya_p0, saya_p1, 0.08),
        rotation=saya_rot,
        shade_smooth=False,
        color=RONIN_COLORS['sig_magenta'],
        signal=True,
        bone='Pelvis',
    ))

    # --- C. Layered O-Sode Shoulder Armor, Cyber-Weave Arms, Kote Vambraces & Gauntlets ---
    # 3 overlapping samurai sode armor lames per shoulder
    for tier in range(3):
        z_top = 1.415 - tier * 0.068
        z_bot = z_top - 0.078
        x_in = 0.215 + tier * 0.016
        x_out = 0.295 + tier * 0.018
        s_outer = []
        s_inner = []
        for z_row, x_cur in ((z_bot, x_out), (z_top, x_in)):
            ro, ri = [], []
            for y_off, dx_c in ((-0.088, -0.026), (-0.032, 0.006), (0.032, 0.006), (0.088, -0.026)):
                ro.append((x_cur + dx_c, y_off, z_row))
                ri.append((x_cur + dx_c - 0.015, y_off * 0.90, z_row))
            s_outer.append(ro)
            s_inner.append(ri)
        sode_L = build_thick_shell(
            s_outer,
            s_inner,
            name=f'Ronin_SodeLame_{tier}_L',
            outer_color=RONIN_COLORS['armor_obsidian'] if tier != 1 else RONIN_COLORS['armor_titanium'],
            inner_color=RONIN_COLORS['crimson_cord'],
            rim_color=RONIN_COLORS['brass_odoshi'] if tier == 0 else None,
            shade_smooth=False,
            bone='Clavicle_L' if tier == 0 else 'UpperArm_L',
        )
        body_parts.extend([sode_L, mirror_clean(sode_L)])

    sode_sig_L = aa.add_bevel_box(
        name='Ronin_SodeSignalTrim_L',
        size=(0.018, 0.145, 0.014),
        location=(0.242, 0.0, 1.422),
        rotation=(0.0, 0.24, 0.0),
        bevel=0.003,
        segments=1,
        color=RONIN_COLORS['sig_magenta'],
        signal=True,
        bone='Clavicle_L',
    )
    sig_parts.extend([sode_sig_L, mirror_clean(sode_sig_L)])

    # Sleeveless cyber-weave upper arm + forearm sleeve (tucked cleanly into Tekko gauntlet)
    sh_pt_L = Vector((sh_x - 0.018, 0.0, 0.755 * H))
    arm_L = build_oriented_chain_loft(
        [
            (sh_pt_L, 0.054, 0.056),
            (_vlerp(sh_pt_L, elbow_L, 0.55), 0.047, 0.049),
            (elbow_L, 0.042, 0.044),
            (_vlerp(wrist_L, hand_L, 0.28), 0.034, 0.036),
        ],
        name='Ronin_CyberArm_L',
        segs=7,
        color=RONIN_COLORS['cyber_weave'],
        shade_smooth=True,
        role='arm',
    )
    body_parts.extend([arm_L, mirror_clean(arm_L)])

    # Contoured Kote forearm vambrace + elbow couter + Signal strip
    fore_dir = (wrist_L - elbow_L).normalized()
    fore_rot_L = Vector((0.0, 0.0, 1.0)).rotation_difference(fore_dir)
    kote_L = aa.add_cylinder(
        name='Ronin_KoteVambrace_L',
        radius=0.045,
        radius_top=0.052,
        depth=0.205,
        vertices=7,
        location=_vlerp(wrist_L, elbow_L, 0.48),
        rotation=fore_rot_L,
        shade_smooth=False,
        color=RONIN_COLORS['armor_obsidian'],
        bone='Forearm_L',
    )
    kote_fin_L = aa.add_box(
        name='Ronin_KoteDeflector_L',
        size=(0.018, 0.034, 0.195),
        location=_vlerp(wrist_L, elbow_L, 0.52) + Vector((0.048, 0.012, 0.0)),
        rotation=fore_rot_L,
        taper=(0.75, 0.85),
        color=RONIN_COLORS['armor_titanium'],
        bone='Forearm_L',
    )
    kote_sig_L = aa.add_box(
        name='Ronin_KoteSignal_L',
        size=(0.008, 0.014, 0.155),
        location=_vlerp(wrist_L, elbow_L, 0.48) + Vector((0.056, 0.012, 0.0)),
        rotation=fore_rot_L,
        color=RONIN_COLORS['sig_magenta'],
        signal=True,
        bone='Forearm_L',
    )
    body_parts.extend([kote_L, mirror_clean(kote_L), kote_fin_L, mirror_clean(kote_fin_L)])
    sig_parts.extend([kote_sig_L, mirror_clean(kote_sig_L)])

    # Tekko armored gauntlets + gripped fist
    hand_rot_L = Vector((0.0, 0.0, 1.0)).rotation_difference((hand_L - wrist_L).normalized())
    fist_L = aa.add_bevel_box(
        name='Ronin_TekkoGauntlet_L',
        size=(0.058, 0.072, 0.105),
        location=_vlerp(wrist_L, hand_L, 0.46),
        rotation=hand_rot_L,
        bevel=0.008,
        segments=1,
        color=RONIN_COLORS['armor_obsidian'],
        bone='Hand_L',
    )
    knuckle_L = aa.add_box(
        name='Ronin_TekkoPlate_L',
        size=(0.016, 0.068, 0.078),
        location=_vlerp(wrist_L, hand_L, 0.42) + Vector((0.028, 0.0, 0.0)),
        rotation=hand_rot_L,
        color=RONIN_COLORS['brass_odoshi'],
        bone='Hand_L',
    )
    body_parts.extend([fist_L, mirror_clean(fist_L), knuckle_L, mirror_clean(knuckle_L)])

    # --- D. Hakama Breeches, Haidate Thigh Plates, Suneate Greaves & Cyber-Tabi ---
    leg_L = build_oriented_chain_loft(
        [
            (hip_L + Vector((0.0, 0.0, 0.025)), 0.090, 0.096),
            (_vlerp(hip_L, knee_L, 0.52), 0.082, 0.088),
            (knee_L, 0.056, 0.060),
            (ankle_L - Vector((0.0, 0.0, 0.030)), 0.038, 0.042),
        ],
        name='Ronin_HakamaLeg_L',
        segs=7,
        color=RONIN_COLORS['hakama_dark'],
        shade_smooth=False,
        role='leg',
    )
    body_parts.extend([leg_L, mirror_clean(leg_L)])

    # Winged Knee Poleyn (Tateage) + Fluted Suneate Shin Greave + Signal Conduit
    poleyn_L = aa.add_bevel_box(
        name='Ronin_KneePoleyn_L',
        size=(0.096, 0.048, 0.115),
        location=(knee_L.x + 0.004, knee_L.y - 0.052, knee_L.z + 0.018),
        rotation=(-0.18, 0.0, 0.0),
        taper=(1.18, 0.72),
        bevel=0.006,
        segments=1,
        color=RONIN_COLORS['armor_titanium'],
        bone='Shin_L',
    )
    shin_dir = (knee_L - ankle_L).normalized()
    shin_rot_L = Vector((0.0, 0.0, 1.0)).rotation_difference(shin_dir)
    suneate_L = aa.add_cylinder(
        name='Ronin_SuneateGreave_L',
        radius=0.052,
        radius_top=0.062,
        depth=0.295,
        vertices=7,
        location=_vlerp(ankle_L, knee_L, 0.48) + Vector((0.0, -0.010, 0.0)),
        rotation=shin_rot_L,
        scale=(0.96, 1.08, 1.0),
        shade_smooth=False,
        color=RONIN_COLORS['armor_obsidian'],
        bone='Shin_L',
    )
    shin_sig_L = aa.add_box(
        name='Ronin_SuneateSignal_L',
        size=(0.012, 0.014, 0.225),
        location=_vlerp(ankle_L, knee_L, 0.46) + Vector((0.004, -0.062, 0.0)),
        rotation=shin_rot_L,
        color=RONIN_COLORS['sig_magenta'],
        signal=True,
        bone='Shin_L',
    )
    body_parts.extend([poleyn_L, mirror_clean(poleyn_L), suneate_L, mirror_clean(suneate_L)])
    sig_parts.extend([shin_sig_L, mirror_clean(shin_sig_L)])

    # Split-Toe Armored Cyber-Tabi War Boots grounded flat on z = 0.0
    bx = ankle_L.x
    tabi_rings_L = [
        [
            (bx - 0.042,  0.068, 0.000),
            (bx + 0.044,  0.068, 0.000),
            (bx + 0.046, -0.105, 0.000),
            (bx + 0.044, -0.225, 0.002),
            (bx + 0.004, -0.195, 0.004),  # Authentic samurai split-toe tabi cleft!
            (bx - 0.010, -0.228, 0.002),
            (bx - 0.042, -0.215, 0.002),
            (bx - 0.044, -0.105, 0.000),
        ],
        [
            (bx - 0.044,  0.070, 0.028),
            (bx + 0.046,  0.070, 0.028),
            (bx + 0.048, -0.108, 0.024),
            (bx + 0.044, -0.228, 0.022),
            (bx + 0.004, -0.196, 0.022),
            (bx - 0.010, -0.230, 0.022),
            (bx - 0.042, -0.218, 0.022),
            (bx - 0.046, -0.108, 0.024),
        ],
        [
            (bx - 0.040,  0.058, 0.072),
            (bx + 0.042,  0.058, 0.072),
            (bx + 0.042, -0.088, 0.058),
            (bx + 0.032, -0.185, 0.036),
            (bx + 0.004, -0.172, 0.036),
            (bx - 0.010, -0.188, 0.036),
            (bx - 0.032, -0.180, 0.036),
            (bx - 0.040, -0.088, 0.058),
        ],
        [
            (bx - 0.040,  0.054, 0.172),
            (bx + 0.042,  0.054, 0.172),
            (bx + 0.042, -0.025, 0.168),
            (bx + 0.030, -0.048, 0.164),
            (bx + 0.002, -0.050, 0.164),
            (bx - 0.012, -0.050, 0.164),
            (bx - 0.030, -0.048, 0.164),
            (bx - 0.040, -0.025, 0.168),
        ],
    ]
    tabi_L = build_lofted_tube(
        tabi_rings_L,
        name='Ronin_CyberTabi_L',
        color=RONIN_COLORS['armor_obsidian'],
        shade_smooth=False,
        ground_ao=0.12,
        role='foot',
    )
    aa.paint_faces(tabi_L, lambda _p, c, _n: c.z < 0.026, color=RONIN_COLORS['armor_titanium'], ao_strength=0.18)
    body_parts.extend([tabi_L, mirror_clean(tabi_L)])

    # --- E. Long High-Frequency Glowing Magenta Katana in Right Hand ---
    grip_R = Vector((-(sh_x + H * 0.034), -0.056, 0.806))
    kat_body, kat_sig = _build_ronin_katana(grip_R)
    body_parts.extend(kat_body)
    sig_parts.extend(kat_sig)

    # --- Batch into 'Body' + 'Signal', Build 21-Bone Rig & Author 7 Clips ---
    merged = aa.combine_by_role(body_parts, sig_parts)
    pin_snap, role_snap = snapshot_skin_tags(merged)
    rig = aa.build_humanoid_rig_and_clips(
        merged,
        rig_name='RoninRig',
        height=H,
        style='blade',
        attack_clip_name='Sword_Slash',
        weapon_hand='R',
        shoulder_width=sh_x,
        hip_width=hp_x,
    )
    apply_ronin_skinning(rig, merged, pin_snap, role_snap, height=H)
    author_ronin_clips(rig, height=H)

    out_path = OUT_DIR / 'foe-blade.glb'
    info = aa.export_glb(out_path, [rig, *merged], min_tris=2200, max_meshes=6)
    print(f"[build_aa_elites] Exported {info['path']}: {info['triangles']} tris, {info['meshes']} meshes, {info['size_kb']} KB")
    return out_path


# ===========================================================================
# 2. AETHELGARD TITAN SIEGE MECH — public/street/foe-mech.glb
# ===========================================================================

def build_mech_rig_and_clips(
    meshes: Sequence[bpy.types.Object],
    pin_snap: dict[str, dict[int, str]],
    rig_name: str = 'TitanMechRig',
) -> bpy.types.Object:
    """Create a 25-bone heavy reverse-joint war-mech armature, skin Body+Signal, and animate 7 clips."""
    joints: dict[str, tuple[tuple[float, float, float], tuple[float, float, float]]] = {
        'Pelvis': ((0.0, 0.0, 0.86), (0.0, 0.0, 0.98)),
        'Spine': ((0.0, 0.0, 0.98), (0.0, 0.0, 1.14)),
        'Chest': ((0.0, 0.0, 1.14), (0.0, 0.0, 1.48)),
        'Neck': ((0.0, -0.06, 1.44), (0.0, -0.10, 1.52)),
        'Head': ((0.0, -0.10, 1.52), (0.0, -0.24, 1.60)),
    }
    parents = {
        'Spine': 'Pelvis',
        'Chest': 'Spine',
        'Neck': 'Chest',
        'Head': 'Neck',
    }

    for side, sgn in (('L', 1.0), ('R', -1.0)):
        joints.update({
            f'Clavicle_{side}': ((sgn * 0.16, 0.0, 1.43), (sgn * 0.40, 0.0, 1.45)),
            f'Pod_{side}': ((sgn * 0.30, 0.02, 1.55), (sgn * 0.30, -0.18, 1.68)),
            f'UpperArm_{side}': ((sgn * 0.42, 0.0, 1.41), (sgn * 0.46, 0.03, 1.09)),
            f'Forearm_{side}': ((sgn * 0.46, 0.03, 1.09), (sgn * 0.47, -0.10, 0.77)),
            f'Hand_{side}': ((sgn * 0.47, -0.10, 0.77), (sgn * 0.47, -0.16, 0.61)),
            # Reverse-style 3-link digitigrade hydraulic walker leg:
            # Hip -> Forward Knee (Thigh) -> Rear Reverse-Hock (Shin) -> Ankle (Tarsus) -> Foot -> Toe
            f'Thigh_{side}': ((sgn * 0.23, 0.02, 0.86), (sgn * 0.25, -0.15, 0.56)),
            f'Shin_{side}': ((sgn * 0.25, -0.15, 0.56), (sgn * 0.26, 0.12, 0.34)),
            f'Tarsus_{side}': ((sgn * 0.26, 0.12, 0.34), (sgn * 0.26, -0.02, 0.11)),
            f'Foot_{side}': ((sgn * 0.26, -0.02, 0.11), (sgn * 0.26, -0.15, 0.04)),
            f'Toe_{side}': ((sgn * 0.26, -0.15, 0.04), (sgn * 0.26, -0.28, 0.02)),
        })
        parents.update({
            f'Clavicle_{side}': 'Chest',
            f'Pod_{side}': 'Chest',
            f'UpperArm_{side}': f'Clavicle_{side}',
            f'Forearm_{side}': f'UpperArm_{side}',
            f'Hand_{side}': f'Forearm_{side}',
            f'Thigh_{side}': 'Pelvis',
            f'Shin_{side}': f'Thigh_{side}',
            f'Tarsus_{side}': f'Shin_{side}',
            f'Foot_{side}': f'Tarsus_{side}',
            f'Toe_{side}': f'Foot_{side}',
        })

    arm_data = bpy.data.armatures.new(rig_name)
    rig = bpy.data.objects.new(rig_name, arm_data)
    bpy.context.collection.objects.link(rig)
    bpy.ops.object.select_all(action='DESELECT')
    rig.select_set(True)
    bpy.context.view_layer.objects.active = rig
    bpy.ops.object.mode_set(mode='EDIT')
    for b_name, (hd, tl) in joints.items():
        eb = arm_data.edit_bones.new(b_name)
        eb.head = hd
        eb.tail = tl
        eb.use_deform = True
        if b_name in parents:
            eb.parent = arm_data.edit_bones[parents[b_name]]
    bpy.ops.object.mode_set(mode='OBJECT')

    joint_vecs = {k: (Vector(a), Vector(b)) for k, (a, b) in joints.items()}

    # Skin Body & Signal meshes rigidly to mechanical bones (zero rubbery plate distortion)
    for m_obj in meshes:
        aa.bake_transform(m_obj)
        v_pins = pin_snap.get(m_obj.name, {})
        for vg in list(m_obj.vertex_groups):
            m_obj.vertex_groups.remove(vg)
        groups = {k: m_obj.vertex_groups.new(name=k) for k in joints}

        for v in m_obj.data.vertices:
            vi = v.index
            pinned = v_pins.get(vi)
            if pinned and pinned in groups:
                groups[pinned].add([vi], 1.0, 'REPLACE')
            else:
                p = v.co
                best_k = min(joints.keys(), key=lambda k: _seg_dist(p, *joint_vecs[k]))
                groups[best_k].add([vi], 1.0, 'REPLACE')

        for mod in list(m_obj.modifiers):
            if mod.type == 'ARMATURE':
                m_obj.modifiers.remove(mod)
        arm_mod = m_obj.modifiers.new('MechSkeleton', 'ARMATURE')
        arm_mod.object = rig
        m_obj.parent = rig

    rig.animation_data_create()
    bpy.context.scene.render.fps = aa.FPS
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

    specs = [
        ('Idle', 2.40),
        ('Walk', 1.12),
        ('Run', 0.76),
        ('Punch', 0.72),
        ('Attack', 0.92),
        ('HitReact', 0.48),
        ('Death', 1.25),
    ]

    for clip_name, dur in specs:
        act = bpy.data.actions.new(clip_name)
        act.use_fake_user = True
        rig.animation_data.action = act
        frames = max(6, round(dur * aa.FPS))

        for f in range(frames + 1):
            u = f / frames
            phase = u * math.tau
            reset_pose()

            if clip_name == 'Idle':
                # Heavy fusion reactor hum + hydraulic settle + menacing Cyclops & missile-pod scan
                hum = math.sin(4.0 * phase) * 0.004
                heave = 0.5 - 0.5 * math.cos(phase)
                scan = math.sin(phase)
                move_pelvis(dz=-0.022 * heave + hum)
                rot('Pelvis', x=0.02 * heave)
                rot('Spine', x=0.03 * heave + hum * 1.5, z=0.06 * scan)
                rot('Chest', x=-0.025 * heave, z=0.09 * scan)
                rot('Head', x=0.03 * math.cos(phase), z=0.22 * scan)

                for side, sgn in (('L', 1.0), ('R', -1.0)):
                    rot(f'Pod_{side}', x=-0.06 * heave, z=0.08 * scan)
                    rot(f'Thigh_{side}', x=-0.04 * heave)
                    rot(f'Shin_{side}', x=0.08 * heave)
                    rot(f'Tarsus_{side}', x=-0.04 * heave)
                    rot(f'UpperArm_{side}', x=-0.22 - 0.03 * heave, y=-sgn * 0.14)
                    rot(f'Forearm_{side}', x=-0.42 - 0.04 * heave)
                    rot(f'Hand_{side}', x=0.12 * heave)

            elif clip_name in ('Walk', 'Run'):
                # Heavy mechanical reverse-joint stomp with sharp hydraulic compression at each footfall
                run = clip_name == 'Run'
                sb = math.sin(phase)
                cb = math.cos(phase)
                # Two footfalls per cycle -> sharp hydraulic shock dip on impact
                impact = (math.sin(phase) ** 2)
                stomp_dip = -(0.065 if run else 0.042) * (1.0 - impact) - (0.020 if run else 0.010)
                lat = (0.028 if run else 0.038) * sb
                move_pelvis(dx=lat, dz=stomp_dip)

                lean = 0.16 if run else 0.07
                yaw_sw = (0.14 if run else 0.09) * sb
                roll_sw = (0.06 if run else 0.05) * sb
                rot('Pelvis', x=lean * 0.45, y=-roll_sw, z=-yaw_sw * 0.6)
                rot('Spine', x=lean * 0.55, y=roll_sw * 0.5, z=yaw_sw * 0.9)
                rot('Chest', x=lean * 0.65 + 0.03 * (1.0 - impact), y=roll_sw * 0.4, z=yaw_sw * 1.1)
                rot('Head', x=-lean * 0.65, z=-yaw_sw * 0.7)

                for side, sgn in (('L', 1.0), ('R', -1.0)):
                    sw = sb * sgn
                    cw = cb * sgn
                    lift = max(0.0, cw)
                    plant = max(0.0, -cw)
                    t_amp = 0.56 if run else 0.38

                    # 3-link reverse-joint hydraulic articulation
                    rot(f'Thigh_{side}', x=-t_amp * sw - 0.24 * lift)
                    rot(f'Shin_{side}', x=0.46 * lift + 0.18 * plant + 0.16 * max(0.0, -sw))
                    rot(f'Tarsus_{side}', x=-0.26 * lift - 0.14 * plant)
                    rot(f'Foot_{side}', x=0.22 * max(0.0, -sw) - 0.10 * max(0.0, sw))
                    rot(f'Toe_{side}', x=0.24 * max(0.0, -sw))

                    arm_amp = 0.50 if run else 0.28
                    rot(f'Pod_{side}', x=-0.10 if run else -0.03, y=-sgn * 0.03 * lift)
                    rot(f'UpperArm_{side}', x=arm_amp * sw - (0.28 if run else 0.16), y=-sgn * 0.12)
                    rot(f'Forearm_{side}', x=(-0.64 if run else -0.44) - 0.18 * max(0.0, -sw))

            elif clip_name == 'Attack':
                # Devastating Two-Arm Ground-Crush!
                # Phase 1 (0..0.38): Rear up on hydraulics, missile pods flare, BOTH Siege Gauntlets raise high overhead
                # Phase 2 (0.36..0.56): Full-tonnage hydraulic crouch + violent two-arm ground smash + chassis tremor
                # Phase 3 (0.70..1.0): Heavy hydraulic rise back to stance
                wind = _smoothstep(0.0, 0.36, u) * (1.0 - _smoothstep(0.36, 0.50, u))
                slam = _smoothstep(0.35, 0.53, u) * (1.0 - _smoothstep(0.74, 1.0, u))
                tremor = math.sin(u * 56.0) * 0.018 * _smoothstep(0.50, 0.55, u) * (1.0 - _smoothstep(0.55, 0.74, u))

                move_pelvis(
                    dy=-0.08 * slam + 0.03 * wind,
                    dz=0.045 * wind - 0.235 * slam + tremor,
                )
                rot('Pelvis', x=-0.10 * wind + 0.18 * slam)
                rot('Spine', x=-0.20 * wind + 0.34 * slam + tremor * 1.5)
                rot('Chest', x=-0.24 * wind + 0.36 * slam - tremor * 1.5)
                rot('Head', x=0.18 * wind - 0.28 * slam)

                for side, sgn in (('L', 1.0), ('R', -1.0)):
                    rot(f'Pod_{side}', x=-0.28 * wind + 0.16 * slam, y=-sgn * 0.10 * wind)
                    rot(f'Clavicle_{side}', y=-sgn * 0.18 * wind + sgn * 0.12 * slam)
                    rot(
                        f'UpperArm_{side}',
                        x=-2.22 * wind - 0.92 * slam,
                        y=-sgn * (0.24 * wind - 0.22 * slam),
                        z=-sgn * 0.18 * slam,
                    )
                    rot(f'Forearm_{side}', x=-0.46 * wind - 0.18 * slam)
                    rot(f'Hand_{side}', x=-0.22 * wind + 0.32 * slam)

                    # Reverse-joint legs compress deeply under ground-crush impact
                    rot(f'Thigh_{side}', x=-0.42 * slam + 0.08 * wind, y=-sgn * 0.06 * slam)
                    rot(f'Shin_{side}', x=0.72 * slam - 0.10 * wind)
                    rot(f'Tarsus_{side}', x=-0.48 * slam + 0.04 * wind)
                    rot(f'Foot_{side}', x=0.0)

            elif clip_name == 'Punch':
                # Heavy Right Piston-Gauntlet & Forearm Plasma-Blade Smash
                wind = _smoothstep(0.0, 0.28, u) * (1.0 - _smoothstep(0.28, 0.44, u))
                strike = _smoothstep(0.26, 0.48, u) * (1.0 - _smoothstep(0.68, 1.0, u))
                move_pelvis(dy=-0.06 * strike + 0.02 * wind, dz=-0.065 * strike)
                rot('Pelvis', x=0.08 * strike, z=0.22 * strike - 0.18 * wind)
                rot('Spine', x=0.14 * strike - 0.08 * wind, z=0.32 * strike - 0.28 * wind)
                rot('Chest', x=0.14 * strike - 0.08 * wind, z=0.36 * strike - 0.30 * wind)
                rot('Head', z=-0.42 * strike + 0.32 * wind)

                rot('Clavicle_R', z=0.24 * strike - 0.16 * wind)
                rot('UpperArm_R', x=0.45 * wind - 1.56 * strike, y=0.26 * wind - 0.12 * strike, z=0.28 * strike)
                rot('Forearm_R', x=-0.95 * wind - 0.18 * strike)
                rot('UpperArm_L', x=-0.38 * wind + 0.22 * strike, y=-0.16)
                rot('Forearm_L', x=-0.72)

                rot('Thigh_L', x=-0.22 * strike)
                rot('Shin_L', x=0.36 * strike)
                rot('Tarsus_L', x=-0.14 * strike)

            elif clip_name == 'HitReact':
                hit = math.sin(math.pi * (u ** 0.56))
                move_pelvis(dy=0.055 * hit, dz=-0.045 * hit)
                rot('Pelvis', x=-0.08 * hit, z=-0.10 * hit)
                rot('Spine', x=-0.22 * hit, z=-0.18 * hit)
                rot('Chest', x=-0.24 * hit, y=0.08 * hit, z=-0.20 * hit)
                rot('Head', x=-0.26 * hit, z=0.22 * hit)
                for side, sgn in (('L', 1.0), ('R', -1.0)):
                    rot(f'Pod_{side}', x=-0.20 * hit)
                    rot(f'UpperArm_{side}', x=-0.36 * hit, y=-sgn * 0.28 * hit)
                    rot(f'Forearm_{side}', x=-0.62 * hit)
                    rot(f'Thigh_{side}', x=-0.16 * hit)
                    rot(f'Shin_{side}', x=0.28 * hit)
                    rot(f'Tarsus_{side}', x=-0.12 * hit)

            elif clip_name == 'Death':
                # Reactor critical failure: reverse-joint knees buckle, hull crashes forward onto z = 0
                buckle = _smoothstep(0.0, 0.38, u) * (1.0 - _smoothstep(0.46, 0.84, u))
                fall = _smoothstep(0.32, 0.88, u)
                drop_z = -0.56 * fall - 0.22 * buckle
                move_pelvis(dy=-0.12 * fall + 0.04 * buckle, dz=drop_z)

                rot('Pelvis', x=-0.12 * buckle + 1.32 * fall, z=0.06 * fall)
                rot('Spine', x=-0.16 * buckle + 0.08 * fall, z=-0.10 * buckle)
                rot('Chest', x=-0.14 * buckle + 0.06 * fall)
                rot('Head', x=0.28 * fall, z=0.32 * fall)

                for side, sgn in (('L', 1.0), ('R', -1.0)):
                    rot(f'Pod_{side}', x=0.26 * fall, y=-sgn * 0.14 * fall)
                    rot(f'Thigh_{side}', x=-0.52 * buckle - 0.18 * fall, y=-sgn * 0.12 * fall)
                    rot(f'Shin_{side}', x=0.92 * buckle + 0.36 * fall)
                    rot(f'Tarsus_{side}', x=-0.42 * buckle - 0.18 * fall)
                    rot(f'Foot_{side}', x=0.35 * fall)
                    rot(f'UpperArm_{side}', x=-0.42 * buckle - 0.22 * fall, y=-sgn * (0.24 * buckle + 0.48 * fall))
                    rot(f'Forearm_{side}', x=-0.58 * buckle - 0.24 * fall, z=-sgn * 0.35 * fall)

            key_all(f + 1)

        for fc in act.fcurves:
            for kp in fc.keyframe_points:
                kp.interpolation = 'LINEAR'

    rig.animation_data.action = None
    reset_pose()
    bpy.context.scene.frame_set(1)
    return rig


def build_foe_mech() -> Path:
    """Sculpt, rig, animate, and export public/street/foe-mech.glb (Aethelgard Titan Siege Mech)."""
    aa.reset_scene()
    body_parts: list[bpy.types.Object] = []
    sig_parts: list[bpy.types.Object] = []

    # --- A. Pelvis Cradle, Hip Servo Drums & Waist Turret ('Pelvis', 'Spine') ---
    body_parts.append(aa.add_bevel_box(
        name='Mech_PelvisCore',
        size=(0.380, 0.300, 0.165),
        location=(0.0, 0.0, 0.875),
        bevel=0.020,
        segments=1,
        color=MECH_COLORS['hull_primary'],
        bone='Pelvis',
    ))
    body_parts.append(aa.add_bevel_box(
        name='Mech_GroinProw',
        size=(0.165, 0.145, 0.205),
        location=(0.0, -0.145, 0.825),
        rotation=(0.22, 0.0, 0.0),
        taper=(1.18, 0.82),
        bevel=0.014,
        segments=1,
        color=MECH_COLORS['aethel_gold'],
        bone='Pelvis',
    ))
    hip_drum_L = aa.add_cylinder(
        name='Mech_HipServoDrum_L',
        radius=0.088,
        depth=0.145,
        vertices=10,
        location=(0.215, 0.015, 0.860),
        rotation=(0.0, math.pi * 0.5, 0.0),
        shade_smooth=False,
        color=MECH_COLORS['hull_dark'],
        bone='Pelvis',
    )
    body_parts.extend([hip_drum_L, mirror_clean(hip_drum_L)])

    # Octagonal Armored Waist Gimbal ('Spine')
    body_parts.append(aa.add_cylinder(
        name='Mech_WaistTurntable',
        radius=0.185,
        radius_top=0.205,
        depth=0.165,
        vertices=8,
        location=(0.0, 0.0, 1.045),
        shade_smooth=False,
        color=MECH_COLORS['hull_dark'],
        bone='Spine',
    ))
    waist_piston_L = aa.add_limb(
        (0.145, -0.085, 0.945),
        (0.175, -0.095, 1.135),
        radius0=0.024,
        radius1=0.020,
        segments=6,
        cap_round=False,
        name='Mech_WaistPiston_L',
        color=MECH_COLORS['chrome_ram'],
        bone='Spine',
    )
    body_parts.extend([waist_piston_L, mirror_clean(waist_piston_L)])

    # --- B. Wide Armored Torso Chassis, Reactor Core & Dorsal Exhausts ('Chest') ---
    hull_rings = [
        [(-0.23, -0.18, 1.11), (0.23, -0.18, 1.11), (0.26, 0.16, 1.11), (-0.26, 0.16, 1.11)],
        [(-0.35, -0.25, 1.24), (0.35, -0.25, 1.24), (0.36, 0.22, 1.24), (-0.36, 0.22, 1.24)],
        [(-0.37, -0.24, 1.42), (0.37, -0.24, 1.42), (0.35, 0.23, 1.42), (-0.35, 0.23, 1.42)],
        [(-0.29, -0.17, 1.52), (0.29, -0.17, 1.52), (0.28, 0.18, 1.52), (-0.28, 0.18, 1.52)],
    ]
    body_parts.append(build_lofted_tube(
        hull_rings,
        name='Mech_MainHullChassis',
        color=MECH_COLORS['hull_primary'],
        shade_smooth=False,
        ridge_boost=0.12,
        bone='Chest',
    ))

    # Sloped heavy cheek glacis armor plates flanking the central reactor
    glacis_L = aa.add_bevel_box(
        name='Mech_ChestGlacis_L',
        size=(0.165, 0.085, 0.245),
        location=(0.245, -0.228, 1.315),
        rotation=(0.14, 0.22, -0.16),
        bevel=0.012,
        segments=1,
        color=MECH_COLORS['hull_light'],
        bone='Chest',
    )
    stripe_L = aa.add_box(
        name='Mech_SiegeStripe_L',
        size=(0.125, 0.024, 0.055),
        location=(0.258, -0.268, 1.365),
        rotation=(0.14, 0.22, -0.16),
        color=MECH_COLORS['siege_crimson'],
        bone='Chest',
    )
    body_parts.extend([glacis_L, mirror_clean(glacis_L), stripe_L, mirror_clean(stripe_L)])

    # Central Glowing Fusion Reactor Core + Armored Bezel + Protective Containment Ribs
    body_parts.append(aa.add_torus(
        name='Mech_ReactorBezel',
        major_radius=0.112,
        minor_radius=0.024,
        major_segments=10,
        minor_segments=4,
        location=(0.0, -0.248, 1.305),
        rotation=(math.pi * 0.5, 0.0, 0.0),
        shade_smooth=False,
        color=MECH_COLORS['aethel_gold'],
        bone='Chest',
    ))
    sig_parts.append(aa.add_cylinder(
        name='Mech_ReactorOuterGlow',
        radius=0.096,
        depth=0.032,
        vertices=10,
        location=(0.0, -0.246, 1.305),
        rotation=(math.pi * 0.5, 0.0, 0.0),
        shade_smooth=False,
        color=MECH_COLORS['sig_reactor'],
        signal=True,
        bone='Chest',
    ))
    sig_parts.append(aa.add_sphere(
        name='Mech_ReactorCoreSun',
        radius=0.056,
        segments=8,
        rings=4,
        location=(0.0, -0.255, 1.305),
        scale=(1.0, 0.55, 1.0),
        shade_smooth=False,
        color=MECH_COLORS['sig_core_hot'],
        signal=True,
        bone='Chest',
    ))
    for rib_ang in (-0.52, 0.52, math.pi * 0.5):
        body_parts.append(aa.add_box(
            name=f'Mech_ReactorCageRib_{rib_ang:.2f}',
            size=(0.215, 0.018, 0.018),
            location=(0.0, -0.272, 1.305),
            rotation=(0.0, rib_ang, 0.0),
            color=MECH_COLORS['hull_dark'],
            bone='Chest',
        ))

    # Dorsal Twin Fusion Exhaust Stacks & Glowing Thermal Louvers
    stack_L = aa.add_bevel_box(
        name='Mech_DorsalExhaust_L',
        size=(0.155, 0.125, 0.275),
        location=(0.148, 0.215, 1.335),
        rotation=(-0.14, 0.08, 0.0),
        taper=(0.86, 0.84),
        bevel=0.014,
        segments=1,
        color=MECH_COLORS['hull_dark'],
        bone='Chest',
    )
    vent_sig_L = aa.add_box(
        name='Mech_ExhaustThermalGlow_L',
        size=(0.098, 0.022, 0.165),
        location=(0.150, 0.272, 1.325),
        rotation=(-0.14, 0.08, 0.0),
        color=MECH_COLORS['sig_reactor'],
        signal=True,
        bone='Chest',
    )
    body_parts.extend([stack_L, mirror_clean(stack_L)])
    sig_parts.extend([vent_sig_L, mirror_clean(vent_sig_L)])

    # Armored Gorget Collar around Cockpit Turret
    body_parts.append(aa.add_torus(
        name='Mech_GorgetCollar',
        major_radius=0.145,
        minor_radius=0.028,
        major_segments=8,
        minor_segments=4,
        location=(0.0, -0.075, 1.495),
        scale=(1.15, 0.95, 1.0),
        shade_smooth=False,
        color=MECH_COLORS['hull_light'],
        bone='Chest',
    ))

    # --- C. Recessed Turret Head + Cyclops Optic Strip ('Neck', 'Head') ---
    body_parts.append(aa.add_cylinder(
        name='Mech_TurretNeckRing',
        radius=0.085,
        depth=0.095,
        vertices=10,
        location=(0.0, -0.085, 1.495),
        shade_smooth=False,
        color=MECH_COLORS['hull_dark'],
        bone='Neck',
    ))
    body_parts.append(aa.add_bevel_box(
        name='Mech_CockpitTurretHead',
        size=(0.235, 0.245, 0.145),
        location=(0.0, -0.125, 1.565),
        taper=(0.84, 0.88),
        shear_y=-0.025,
        bevel=0.016,
        segments=1,
        color=MECH_COLORS['hull_primary'],
        bone='Head',
    ))
    body_parts.append(aa.add_bevel_box(
        name='Mech_BrowVisorHood',
        size=(0.250, 0.145, 0.042),
        location=(0.0, -0.175, 1.622),
        rotation=(0.14, 0.0, 0.0),
        taper=(0.86, 0.90),
        bevel=0.008,
        segments=1,
        color=MECH_COLORS['hull_light'],
        bone='Head',
    ))
    # Twin tactical comms antennae on right side of turret head
    for a_i, (ax, az, h_ant) in enumerate(((-0.118, 1.665, 0.135), (-0.132, 1.645, 0.095))):
        body_parts.append(aa.add_cylinder(
            name=f'Mech_CommsAntenna_{a_i}',
            radius=0.007,
            radius_top=0.003,
            depth=h_ant,
            vertices=6,
            location=(ax, -0.065 + a_i * 0.030, az),
            rotation=(-0.18, 0.0, 0.0),
            shade_smooth=False,
            color=MECH_COLORS['aethel_gold'],
            bone='Head',
        ))

    # Wrap-Around Glowing Cyclops Optic Strip + Central Cyclops Eye Lens ('Signal')
    sig_parts.append(aa.add_bevel_box(
        name='Mech_CyclopsOpticStrip',
        size=(0.218, 0.065, 0.028),
        location=(0.0, -0.225, 1.572),
        bevel=0.006,
        segments=1,
        color=MECH_COLORS['sig_optic'],
        signal=True,
        bone='Head',
    ))
    sig_parts.append(aa.add_cylinder(
        name='Mech_CyclopsCentralEye',
        radius=0.026,
        depth=0.024,
        vertices=10,
        location=(0.0, -0.254, 1.572),
        rotation=(math.pi * 0.5, 0.0, 0.0),
        shade_smooth=False,
        color=MECH_COLORS['sig_core_hot'],
        signal=True,
        bone='Head',
    ))

    # --- D. Twin Shoulder-Mounted Micro-Missile Pods ('Pod_L', 'Pod_R') ---
    # Top of missile pod shroud sits cleanly at z = 1.825m (normalized ~1.8m rig height!)
    pod_trunnion_L = aa.add_box(
        name='Mech_PodPylon_L',
        size=(0.075, 0.125, 0.115),
        location=(0.295, -0.015, 1.545),
        taper=(0.88, 0.90),
        color=MECH_COLORS['hull_dark'],
        bone='Pod_L',
    )
    pod_box_L = aa.add_bevel_box(
        name='Mech_MissilePodHull_L',
        size=(0.215, 0.255, 0.195),
        location=(0.305, -0.045, 1.710),
        rotation=(0.08, -0.06, 0.0),
        bevel=0.016,
        segments=1,
        color=MECH_COLORS['hull_primary'],
        bone='Pod_L',
    )
    pod_rim_L = aa.add_box(
        name='Mech_MissilePodRim_L',
        size=(0.224, 0.042, 0.204),
        location=(0.302, -0.158, 1.720),
        rotation=(0.08, -0.06, 0.0),
        taper=(0.96, 0.96),
        color=MECH_COLORS['aethel_gold'],
        bone='Pod_L',
    )
    pod_bay_L = aa.add_box(
        name='Mech_MissileBayFace_L',
        size=(0.184, 0.022, 0.162),
        location=(0.302, -0.168, 1.720),
        rotation=(0.08, -0.06, 0.0),
        color=MECH_COLORS['hull_dark'],
        bone='Pod_L',
    )
    pod_laser_L = aa.add_box(
        name='Mech_TargetPod_L',
        size=(0.052, 0.145, 0.068),
        location=(0.428, -0.065, 1.705),
        rotation=(0.08, -0.06, 0.0),
        taper=(0.85, 0.90),
        color=MECH_COLORS['siege_crimson'],
        bone='Pod_L',
    )
    body_parts.extend([
        pod_trunnion_L, mirror_clean(pod_trunnion_L),
        pod_box_L, mirror_clean(pod_box_L),
        pod_rim_L, mirror_clean(pod_rim_L),
        pod_bay_L, mirror_clean(pod_bay_L),
        pod_laser_L, mirror_clean(pod_laser_L),
    ])

    # 6 micro-missile warheads (3 cols x 2 rows) per pod with glowing Signal seeker tips!
    for r_m, dz_m in enumerate((-0.038, 0.038)):
        for c_m, dx_m in enumerate((-0.054, 0.0, 0.054)):
            mx = 0.302 + dx_m
            mz = 1.720 + dz_m
            wh_L = aa.add_cone(
                name=f'Mech_MissileCone_{r_m}_{c_m}_L',
                radius1=0.021,
                radius2=0.007,
                depth=0.038,
                vertices=6,
                location=(mx, -0.184, mz),
                rotation=(math.pi * 0.5 + 0.08, 0.0, 0.0),
                shade_smooth=False,
                color=MECH_COLORS['warhead_cone'],
                bone='Pod_L',
            )
            tip_L = aa.add_cone(
                name=f'Mech_MissileSeeker_{r_m}_{c_m}_L',
                radius1=0.009,
                radius2=0.0,
                depth=0.018,
                vertices=5,
                location=(mx, -0.206, mz + 0.002),
                rotation=(math.pi * 0.5 + 0.08, 0.0, 0.0),
                shade_smooth=False,
                color=MECH_COLORS['sig_optic'],
                signal=True,
                bone='Pod_L',
            )
            body_parts.extend([wh_L, mirror_clean(wh_L)])
            sig_parts.extend([tip_L, mirror_clean(tip_L)])

    # --- E. Heavy Shoulder Pauldrons, Piston Siege Gauntlets & Forearm Plasma Blades ---
    sh_drum_L = aa.add_cylinder(
        name='Mech_ShoulderAxle_L',
        radius=0.078,
        depth=0.155,
        vertices=10,
        location=(0.405, 0.0, 1.425),
        rotation=(0.0, math.pi * 0.5, 0.0),
        shade_smooth=False,
        color=MECH_COLORS['hull_dark'],
        bone='Clavicle_L',
    )
    pauldron_L = aa.add_bevel_box(
        name='Mech_ShoulderPauldron_L',
        size=(0.175, 0.245, 0.215),
        location=(0.455, 0.0, 1.385),
        rotation=(0.0, 0.22, 0.0),
        taper=(0.82, 0.88),
        bevel=0.016,
        segments=1,
        color=MECH_COLORS['hull_light'],
        bone='UpperArm_L',
    )
    upper_arm_L = aa.add_bevel_box(
        name='Mech_UpperArmStrut_L',
        size=(0.118, 0.135, 0.255),
        location=(0.445, 0.015, 1.225),
        bevel=0.012,
        segments=1,
        color=MECH_COLORS['frame_steel'],
        bone='UpperArm_L',
    )
    elbow_drum_L = aa.add_cylinder(
        name='Mech_ElbowDrum_L',
        radius=0.068,
        depth=0.145,
        vertices=10,
        location=(0.460, 0.030, 1.090),
        rotation=(0.0, math.pi * 0.5, 0.0),
        shade_smooth=False,
        color=MECH_COLORS['hull_dark'],
        bone='UpperArm_L',
    )
    body_parts.extend([
        sh_drum_L, mirror_clean(sh_drum_L),
        pauldron_L, mirror_clean(pauldron_L),
        upper_arm_L, mirror_clean(upper_arm_L),
        elbow_drum_L, mirror_clean(elbow_drum_L),
    ])

    # Massive Piston Siege Gauntlets ('Forearm_L/R' + 'Hand_L/R')
    fore_p0 = Vector((0.460, 0.030, 1.090))
    fore_p1 = Vector((0.470, -0.100, 0.770))
    fore_mid = _vlerp(fore_p0, fore_p1, 0.54)
    fore_dir = (fore_p1 - fore_p0).normalized()
    fore_rot = Vector((0.0, 0.0, 1.0)).rotation_difference(fore_dir)

    gauntlet_L = aa.add_bevel_box(
        name='Mech_SiegeGauntlet_L',
        size=(0.195, 0.225, 0.345),
        location=fore_mid,
        rotation=fore_rot,
        taper=(0.90, 0.90),
        bevel=0.018,
        segments=1,
        color=MECH_COLORS['hull_primary'],
        bone='Forearm_L',
    )
    gauntlet_shield_L = aa.add_bevel_box(
        name='Mech_GauntletFaceplate_L',
        size=(0.048, 0.195, 0.295),
        location=fore_mid + Vector((0.092, -0.010, 0.0)),
        rotation=fore_rot,
        bevel=0.010,
        segments=1,
        color=MECH_COLORS['hull_light'],
        bone='Forearm_L',
    )
    body_parts.extend([gauntlet_L, mirror_clean(gauntlet_L), gauntlet_shield_L, mirror_clean(gauntlet_shield_L)])

    # Twin External Heavy Hydraulic Ram Pistons along outer Siege Gauntlet!
    for p_i, y_off in enumerate((-0.058, 0.058)):
        ram_top = fore_p0 + Vector((0.105, y_off, -0.02))
        ram_bot = fore_p1 + Vector((0.112, y_off, 0.03))
        cyl_L = aa.add_limb(
            ram_top,
            _vlerp(ram_top, ram_bot, 0.56),
            radius0=0.026,
            radius1=0.026,
            segments=6,
            cap_round=False,
            name=f'Mech_GauntletBarrel_{p_i}_L',
            color=MECH_COLORS['aethel_gold'],
            bone='Forearm_L',
        )
        rod_L = aa.add_limb(
            _vlerp(ram_top, ram_bot, 0.50),
            ram_bot,
            radius0=0.016,
            radius1=0.016,
            segments=6,
            cap_round=False,
            name=f'Mech_GauntletRod_{p_i}_L',
            color=MECH_COLORS['chrome_ram'],
            bone='Forearm_L',
        )
        body_parts.extend([cyl_L, mirror_clean(cyl_L), rod_L, mirror_clean(rod_L)])

    # Forearm Plasma Blades (emitter rail + blazing swept Signal plasma shear blade!)
    rail_prof = [
        (-0.04, 1.04),
        (-0.11, 1.02),
        (-0.22, 0.69),
        (-0.15, 0.67),
    ]
    rail_L = aa.add_extruded_profile(
        rail_prof,
        depth=0.036,
        axis='X',
        name='Mech_PlasmaEmitterRail_L',
        location=(0.564, 0.0, 0.0),
        color=MECH_COLORS['hull_dark'],
        bone='Forearm_L',
    )
    # World-space (Y, Z) swept plasma shear blade along outer-front Siege Gauntlet
    plasma_prof = [
        (-0.09, 1.01),
        (-0.19, 0.96),
        (-0.30, 0.74),
        (-0.37, 0.50),  # Wicked forward-down plasma tip extending boldly past crusher fist!
        (-0.21, 0.64),
        (-0.17, 0.74),
    ]
    plasma_L = aa.add_extruded_profile(
        plasma_prof,
        depth=0.018,
        axis='X',
        name='Mech_ForearmPlasmaBlade_L',
        location=(0.568, 0.0, 0.0),
        color=MECH_COLORS['sig_plasma'],
        signal=True,
        bone='Forearm_L',
    )
    body_parts.extend([rail_L, mirror_clean(rail_L)])
    sig_parts.extend([plasma_L, mirror_clean(plasma_L)])

    # Articulated Heavy Siege Crusher Claws ('Hand_L/R')
    fist_blk_L = aa.add_bevel_box(
        name='Mech_SiegeFistBlock_L',
        size=(0.165, 0.175, 0.125),
        location=(0.472, -0.115, 0.715),
        rotation=fore_rot,
        bevel=0.014,
        segments=1,
        color=MECH_COLORS['aethel_gold'],
        bone='Hand_L',
    )
    body_parts.extend([fist_blk_L, mirror_clean(fist_blk_L)])
    for f_i, fy in enumerate((-0.175, -0.118, -0.062)):
        claw_L = aa.add_box(
            name=f'Mech_CrusherFinger_{f_i}_L',
            size=(0.042, 0.044, 0.115),
            location=(0.515, fy, 0.635),
            rotation=(0.15, 0.28, 0.0),
            taper=(0.70, 0.85),
            color=MECH_COLORS['hull_dark'],
            bone='Hand_L',
        )
        body_parts.extend([claw_L, mirror_clean(claw_L)])
    thumb_L = aa.add_box(
        name='Mech_CrusherThumb_L',
        size=(0.045, 0.052, 0.098),
        location=(0.415, -0.168, 0.650),
        rotation=(0.22, -0.32, 0.0),
        taper=(0.75, 0.85),
        color=MECH_COLORS['hull_dark'],
        bone='Hand_L',
    )
    body_parts.extend([thumb_L, mirror_clean(thumb_L)])

    # --- F. Reverse-Style 3-Link Heavy Hydraulic Legs & Broad Armored Feet ---
    hip_pt = Vector((0.230, 0.020, 0.860))
    knee_pt = Vector((0.250, -0.150, 0.560))
    hock_pt = Vector((0.260, 0.120, 0.340))
    ankle_pt = Vector((0.260, -0.020, 0.110))

    # 1. Forward-sloped Armored Thigh ('Thigh_L/R') + Forward Knee Shield
    thigh_rot = Vector((0.0, 0.0, 1.0)).rotation_difference((hip_pt - knee_pt).normalized())
    thigh_hull_L = aa.add_bevel_box(
        name='Mech_ThighCarapace_L',
        size=(0.185, 0.225, 0.345),
        location=_vlerp(knee_pt, hip_pt, 0.52),
        rotation=thigh_rot,
        taper=(1.12, 1.10),
        bevel=0.018,
        segments=1,
        color=MECH_COLORS['hull_primary'],
        bone='Thigh_L',
    )
    knee_drum_L = aa.add_cylinder(
        name='Mech_FrontKneeDrum_L',
        radius=0.072,
        depth=0.195,
        vertices=10,
        location=knee_pt,
        rotation=(0.0, math.pi * 0.5, 0.0),
        shade_smooth=False,
        color=MECH_COLORS['hull_dark'],
        bone='Thigh_L',
    )
    knee_shield_L = aa.add_bevel_box(
        name='Mech_KneeRamShield_L',
        size=(0.165, 0.068, 0.165),
        location=knee_pt + Vector((0.005, -0.095, 0.020)),
        rotation=(-0.28, 0.0, 0.0),
        taper=(0.85, 0.75),
        bevel=0.010,
        segments=1,
        color=MECH_COLORS['hull_light'],
        bone='Thigh_L',
    )
    body_parts.extend([
        thigh_hull_L, mirror_clean(thigh_hull_L),
        knee_drum_L, mirror_clean(knee_drum_L),
        knee_shield_L, mirror_clean(knee_shield_L),
    ])

    # 2. Reverse-Cantilevered Mid-Leg ('Shin_L/R') + Rear Hock Drum + Chrome Hydraulic Ram
    shin_rot = Vector((0.0, 0.0, 1.0)).rotation_difference((knee_pt - hock_pt).normalized())
    shin_beam_L = aa.add_bevel_box(
        name='Mech_ReverseShinBeam_L',
        size=(0.138, 0.145, 0.365),
        location=_vlerp(hock_pt, knee_pt, 0.50),
        rotation=shin_rot,
        bevel=0.014,
        segments=1,
        color=MECH_COLORS['frame_steel'],
        bone='Shin_L',
    )
    hock_drum_L = aa.add_cylinder(
        name='Mech_ReverseHockDrum_L',
        radius=0.076,
        depth=0.185,
        vertices=10,
        location=hock_pt,
        rotation=(0.0, math.pi * 0.5, 0.0),
        shade_smooth=False,
        color=MECH_COLORS['aethel_gold'],
        bone='Shin_L',
    )
    hock_ram_L = aa.add_limb(
        knee_pt + Vector((0.0, 0.075, 0.085)),
        hock_pt + Vector((0.0, 0.055, 0.045)),
        radius0=0.028,
        radius1=0.022,
        segments=6,
        cap_round=False,
        name='Mech_ReverseHydraulicRam_L',
        color=MECH_COLORS['chrome_ram'],
        bone='Shin_L',
    )
    body_parts.extend([
        shin_beam_L, mirror_clean(shin_beam_L),
        hock_drum_L, mirror_clean(hock_drum_L),
        hock_ram_L, mirror_clean(hock_ram_L),
    ])

    # 3. Lower Hydraulic Shock-Struts ('Tarsus_L/R') from Rear Hock down to Ankle
    tars_rot = Vector((0.0, 0.0, 1.0)).rotation_difference((hock_pt - ankle_pt).normalized())
    tars_housing_L = aa.add_bevel_box(
        name='Mech_TarsusStrut_L',
        size=(0.150, 0.135, 0.285),
        location=_vlerp(ankle_pt, hock_pt, 0.50),
        rotation=tars_rot,
        bevel=0.012,
        segments=1,
        color=MECH_COLORS['hull_primary'],
        bone='Tarsus_L',
    )
    tars_piston_L = aa.add_limb(
        hock_pt + Vector((0.0, -0.065, -0.015)),
        ankle_pt + Vector((0.0, -0.068, 0.035)),
        radius0=0.026,
        radius1=0.020,
        segments=6,
        cap_round=False,
        name='Mech_AnkleShockPiston_L',
        color=MECH_COLORS['chrome_ram'],
        bone='Tarsus_L',
    )
    ankle_guard_L = aa.add_box(
        name='Mech_AnkleShield_L',
        size=(0.185, 0.115, 0.105),
        location=ankle_pt + Vector((0.0, -0.045, 0.035)),
        taper=(0.90, 0.88),
        color=MECH_COLORS['hull_light'],
        bone='Tarsus_L',
    )
    body_parts.extend([
        tars_housing_L, mirror_clean(tars_housing_L),
        tars_piston_L, mirror_clean(tars_piston_L),
        ankle_guard_L, mirror_clean(ankle_guard_L),
    ])

    # 4. Broad Armored Feet & Splayed Siege Talons Sitting Flat on z = 0.0 ('Foot_L/R', 'Toe_L/R')
    fx = ankle_pt.x
    # Central foot hub + rear hydraulic anchor heel spur (bottom dead-flat at z = 0.000!)
    foot_base_L = aa.add_bevel_box(
        name='Mech_FootChassis_L',
        size=(0.205, 0.210, 0.088),
        location=(fx, -0.035, 0.044),
        taper=(0.86, 0.84),
        bevel=0.008,
        segments=1,
        color=MECH_COLORS['hull_dark'],
        bone='Foot_L',
    )
    heel_spur_L = aa.add_box(
        name='Mech_HeelAnchorSpur_L',
        size=(0.135, 0.155, 0.064),
        location=(fx, 0.110, 0.032),
        taper=(0.76, 0.72),
        color=MECH_COLORS['hull_primary'],
        bone='Foot_L',
    )
    body_parts.extend([foot_base_L, mirror_clean(foot_base_L), heel_spur_L, mirror_clean(heel_spur_L)])

    # Twin broad splayed armored front talons ('Toe_L/R', bottom dead-flat at z = 0.000!)
    for t_idx, (tx_off, rz_t) in enumerate(((-0.062, 0.14), (0.062, -0.14))):
        talon_L = aa.add_bevel_box(
            name=f'Mech_FrontTalon_{t_idx}_L',
            size=(0.094, 0.215, 0.068),
            location=(fx + tx_off, -0.205, 0.034),
            rotation=(0.0, 0.0, rz_t),
            taper=(0.78, 0.74),
            shear_y=0.022,
            bevel=0.007,
            segments=1,
            color=MECH_COLORS['hull_light'],
            bone='Toe_L',
        )
        claw_tip_L = aa.add_box(
            name=f'Mech_TalonCleat_{t_idx}_L',
            size=(0.068, 0.065, 0.038),
            location=(fx + tx_off * 1.18, -0.315, 0.019),
            rotation=(0.0, 0.0, rz_t),
            taper=(0.68, 0.70),
            color=MECH_COLORS['aethel_gold'],
            bone='Toe_L',
        )
        body_parts.extend([talon_L, mirror_clean(talon_L), claw_tip_L, mirror_clean(claw_tip_L)])

    # --- Batch into 'Body' + 'Signal', Build 25-Bone Mech Rig & Keyframe 7 Clips ---
    merged = aa.combine_by_role(body_parts, sig_parts)
    pin_snap, _ = snapshot_skin_tags(merged)
    rig = build_mech_rig_and_clips(merged, pin_snap, rig_name='TitanMechRig')

    out_path = OUT_DIR / 'foe-mech.glb'
    info = aa.export_glb(out_path, [rig, *merged], min_tris=2800, max_meshes=6)
    print(f"[build_aa_elites] Exported {info['path']}: {info['triangles']} tris, {info['meshes']} meshes, {info['size_kb']} KB")
    return out_path


# ===========================================================================
# Strict Elite GLB Verification Suite
# ===========================================================================

def verify_elite_glb(
    path: Path,
    *,
    tri_range: tuple[int, int],
    required_clips: Sequence[str],
    required_bones: Sequence[str],
) -> dict:
    """Assert tri budget, Body+Signal meshes, COLOR_0, JOINTS_0, finite floats, bones, clips & +Z facing."""
    info = aa.verify_glb_file(path, expect_rig=True, max_meshes=6, min_tris=tri_range[0])
    tris = info['triangles']
    assert tri_range[0] <= tris <= tri_range[1], (
        f'{path.name}: triangle count {tris} outside target {tri_range[0]}..{tri_range[1]}'
    )

    data = path.read_bytes()
    json_len, _ = struct.unpack_from('<2I', data, 12)
    g = json.loads(data[20:20 + json_len].decode('utf-8'))

    mesh_names = [m.get('name', '') for m in g.get('meshes', [])]
    mat_names = {m.get('name', '') for m in g.get('materials', [])}
    assert len(mesh_names) == 2, f'{path.name}: expected 2 batched meshes (Body + Signal), got {mesh_names}'
    assert 'VertexPaint' in mat_names and 'Signal' in mat_names, (
        f'{path.name}: expected VertexPaint + Signal materials, got {mat_names}'
    )

    node_names = {n.get('name', '') for n in g.get('nodes', [])}
    for b_req in required_bones:
        assert b_req in node_names, f'{path.name}: missing required bone {b_req!r}'

    anim_set = set(info['animations'])
    for c_req in required_clips:
        assert c_req in anim_set, f'{path.name}: missing required clip {c_req!r} (have {sorted(anim_set)})'

    mins = [float('inf')] * 3
    maxs = [float('-inf')] * 3
    for m in g['meshes']:
        for p in m['primitives']:
            attrs = p['attributes']
            assert 'COLOR_0' in attrs, f'{path.name}: missing COLOR_0'
            assert 'JOINTS_0' in attrs and 'WEIGHTS_0' in attrs, f'{path.name}: missing skin attributes'
            acc = g['accessors'][attrs['POSITION']]
            for i in range(3):
                mins[i] = min(mins[i], acc['min'][i])
                maxs[i] = max(maxs[i], acc['max'][i])

    height_m = maxs[1] - mins[1]
    width_m = maxs[0] - mins[0]
    assert abs(mins[1]) <= 0.008, f'{path.name}: soles must sit on ground y=0, got min_y={mins[1]:.4f}'
    assert 1.76 <= height_m <= 1.92, f'{path.name}: normalized height {height_m:.4f}m outside 1.76..1.92m'
    assert maxs[2] > 0.24 and maxs[2] > abs(mins[2]) * 0.90, (
        f'{path.name}: must face +Z in Three.js, got z_bounds=({mins[2]:.3f}, {maxs[2]:.3f})'
    )

    n_joints = max(len(s.get('joints', [])) for s in g['skins'])
    print(
        f"[verify] {path.name}: {tris} tris, size={info['size_kb']} KB, "
        f"height={height_m:.3f}m, width={width_m:.3f}m, y=({mins[1]:.3f}..{maxs[1]:.3f}), "
        f"z=({mins[2]:.3f}..{maxs[2]:.3f}), joints={n_joints}, clips={info['animations']}"
    )
    return info


def main() -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    blade_path = build_foe_blade()
    mech_path = build_foe_mech()

    core_bones = [
        'Pelvis', 'Spine', 'Chest', 'Neck', 'Head',
        'UpperArm_L', 'UpperArm_R', 'Forearm_L', 'Forearm_R', 'Hand_L', 'Hand_R',
        'Thigh_L', 'Thigh_R', 'Shin_L', 'Shin_R', 'Foot_L', 'Foot_R',
    ]
    verify_elite_glb(
        blade_path,
        tri_range=(2200, 3800),
        required_clips=('Idle', 'Walk', 'Run', 'Punch', 'Sword_Slash', 'HitReact', 'Death'),
        required_bones=core_bones,
    )
    verify_elite_glb(
        mech_path,
        tri_range=(2800, 4800),
        required_clips=('Idle', 'Walk', 'Run', 'Punch', 'Attack', 'HitReact', 'Death'),
        required_bones=[*core_bones, 'Pod_L', 'Pod_R'],
    )
    print('[build_aa_elites] Both elite AA enemy rigs built and verified.')


if __name__ == '__main__':
    try:
        main()
    except Exception:
        traceback.print_exc()
        sys.exit(1)
