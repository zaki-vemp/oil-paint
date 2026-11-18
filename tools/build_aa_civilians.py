"""Headless Blender 4.2 LTS AA cyberpunk civilian sculptor for Komorebi.

Authors and exports three stylized AA human characters into public/street/
(~1,800-3,400 tris each, height 1.75..1.82 m, facing -Y in Blender = +Z in Three.js):
  1. public/street/resident.glb — Neo-Kyoto Streetwear Citizen
  2. public/street/mechanic.glb — District Cyber-Artisan
  3. public/street/vendor.glb   — Night-Market Vendor / Field Medic

Run headless:
  .test/blender/blender --background --disable-autoexec --python tools/build_aa_civilians.py
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
# Joint Layout Mirror (Matches aa.build_humanoid_rig_and_clips 1:1)
# ---------------------------------------------------------------------------

def rig_landmarks(height: float, shoulder_width: float | None = None, hip_width: float | None = None) -> dict:
    """Compute exact joint world positions used by aa.build_humanoid_rig_and_clips."""
    h = float(height)
    sh_x = float(shoulder_width) if shoulder_width is not None else h * 0.120
    hp_x = float(hip_width) if hip_width is not None else h * 0.062
    elbow_x = sh_x + h * 0.036
    wrist_x = sh_x + h * 0.034
    knee_x = hp_x + h * 0.012
    ankle_x = hp_x + h * 0.018

    return {
        'h': h,
        'sh_x': sh_x,
        'hp_x': hp_x,
        'arm_split_x': sh_x * 0.82,
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
        # Left-side (+X) joint 3D vectors
        'shoulder_L': (sh_x, 0.0, 0.755 * h),
        'elbow_L': (elbow_x, 0.015 * h, 0.618 * h),
        'wrist_L': (wrist_x, -0.025 * h, 0.485 * h),
        'hand_tip_L': (wrist_x, -0.038 * h, 0.415 * h),
        'hip_L': (hp_x, 0.0, 0.505 * h),
        'knee_L': (knee_x, -0.012 * h, 0.285 * h),
        'ankle_L': (ankle_x, 0.010 * h, 0.080 * h),
        'ball_L': (ankle_x, -0.085 * h, 0.032 * h),
        'toe_L': (ankle_x, -0.145 * h, 0.020 * h),
    }


# ---------------------------------------------------------------------------
# Bespoke Closed-Manifold AA Sculpting Helpers (FrontSide Culling Safe)
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


def build_lofted_ring_solid(
    rings: Sequence[Sequence[tuple[float, float, float]]],
    name: str = 'LoftedSolid',
    color=(0.35, 0.40, 0.48),
    gradient_z=None,
    signal: bool = False,
    shade_smooth: bool = True,
    smooth_angle_cos: float | None = None,
    ao_strength: float = 0.26,
    top_highlight: float = 0.11,
    ridge_boost: float = 0.10,
    ground_ao: float = 0.16,
    base_point: tuple[float, float, float] | None = None,
    tip_point: tuple[float, float, float] | None = None,
    bone: str | None = None,
) -> bpy.types.Object:
    """Loft closed rings into a watertight manifold solid with baked cavity/ridge AO."""
    if len(rings) < 2:
        raise ValueError(f'{name}: requires >= 2 rings')
    m = len(rings[0])
    if m < 3 or any(len(r) != m for r in rings):
        raise ValueError(f'{name}: every ring must share >= 3 vertices')

    bm = bmesh.new()
    bm_rings = [[bm.verts.new(pt) for pt in ring] for ring in rings]

    for k in range(len(bm_rings) - 1):
        ra, rb = bm_rings[k], bm_rings[k + 1]
        for i in range(m):
            j = (i + 1) % m
            bm.faces.new((ra[i], ra[j], rb[j], rb[i]))

    if base_point is None:
        base_point = (
            sum(p[0] for p in rings[0]) / m,
            sum(p[1] for p in rings[0]) / m,
            sum(p[2] for p in rings[0]) / m,
        )
    v_base = bm.verts.new(base_point)
    for i in range(m):
        j = (i + 1) % m
        bm.faces.new((v_base, bm_rings[0][j], bm_rings[0][i]))

    if tip_point is None:
        tip_point = (
            sum(p[0] for p in rings[-1]) / m,
            sum(p[1] for p in rings[-1]) / m,
            sum(p[2] for p in rings[-1]) / m,
        )
    v_tip = bm.verts.new(tip_point)
    for i in range(m):
        j = (i + 1) % m
        bm.faces.new((bm_rings[-1][i], bm_rings[-1][j], v_tip))

    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()

    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    if smooth_angle_cos is not None:
        for p in mesh.polygons:
            p.use_smooth = abs(p.normal.z) < smooth_angle_cos
    else:
        for p in mesh.polygons:
            p.use_smooth = bool(shade_smooth)
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
    return obj


def build_torso_column(
    stations: Sequence[tuple[float, float, float, float, float]],
    segments: int = 12,
    fold_freq: int = 0,
    fold_amp: float = 0.0,
    name: str = 'TorsoColumn',
    color=(0.35, 0.40, 0.48),
    gradient_z=None,
    shade_smooth: bool = True,
    ao_strength: float = 0.26,
    ridge_boost: float = 0.10,
    ground_ao: float = 0.10,
    bone: str | None = None,
) -> bpy.types.Object:
    """Loft a closed torso/skirt/neck column from (z, rx, ry_front, ry_back, y_center) stations.

    Remember: -Y is FRONT (sin(ang) < 0), +Y is BACK (sin(ang) > 0).
    """
    rings = []
    for st_idx, (z, rx, ry_front, ry_back, yc) in enumerate(stations):
        ring = []
        t_st = st_idx / max(1, len(stations) - 1)
        envelope = math.sin(math.pi * t_st)
        for i in range(segments):
            ang = (i / segments) * math.tau
            ca, sa = math.cos(ang), math.sin(ang)
            ry = ry_front if sa < 0.0 else ry_back
            ripple = fold_amp * envelope * math.cos(fold_freq * ang) if (fold_freq > 0 and fold_amp != 0.0) else 0.0
            ring.append((ca * (rx + ripple), yc + sa * (ry + ripple), z))
        rings.append(ring)

    return build_lofted_ring_solid(
        rings,
        name=name,
        color=color,
        gradient_z=gradient_z,
        shade_smooth=shade_smooth,
        ao_strength=ao_strength,
        ridge_boost=ridge_boost,
        ground_ao=ground_ao,
        bone=bone,
    )


def build_open_garment_shell(
    stations: Sequence[tuple[float, float, float, float, float, float]],
    thickness: float = 0.016,
    arc_segments: int = 13,
    fold_freq: int = 6,
    fold_amp: float = 0.004,
    name: str = 'OpenGarment',
    color=(0.14, 0.44, 0.56),
    inner_color=None,
    gradient_z=None,
    signal: bool = False,
    shade_smooth: bool = False,
    ao_strength: float = 0.28,
    ridge_boost: float = 0.12,
    ground_ao: float = 0.10,
    bone: str | None = None,
) -> bpy.types.Object:
    """Build a watertight thick open-front coat/jacket/vest/collar shell open toward -Y (FRONT).

    Each station is (z, rx, ry_front, ry_back, y_center, open_half_angle_rad).
    Sweeps symmetrically from Left-Front (-pi/2 + open_half) around +X, +Y (Back), -X to Right-Front.
    """
    k_cnt = len(stations)
    m_cnt = max(5, int(arc_segments))
    if k_cnt < 2:
        raise ValueError(f'{name}: requires >= 2 vertical stations')

    bm = bmesh.new()
    outer_grid: list[list[bmesh.types.BMVert]] = []
    inner_grid: list[list[bmesh.types.BMVert]] = []

    for k, (z, rx, ry_f, ry_b, yc, half_open) in enumerate(stations):
        t_v = k / max(1, k_cnt - 1)
        fold_env = 0.35 + 0.65 * (1.0 - t_v)
        th0 = -math.pi * 0.5 + float(half_open)
        th1 = math.pi * 1.5 - float(half_open)
        o_row = []
        i_row = []
        for i in range(m_cnt):
            u = i / (m_cnt - 1)
            th = _lerp(th0, th1, u)
            ct, st = math.cos(th), math.sin(th)
            ry = ry_f if st < 0.0 else ry_b
            fold = fold_amp * fold_env * math.cos(fold_freq * (u * math.pi * 2.0)) if fold_freq > 0 else 0.0
            rx_o = max(0.015, rx + fold)
            ry_o = max(0.015, ry + fold)
            rx_i = max(0.008, rx_o - thickness)
            ry_i = max(0.008, ry_o - thickness)
            o_row.append(bm.verts.new((ct * rx_o, yc + st * ry_o, z)))
            i_row.append(bm.verts.new((ct * rx_i, yc + st * ry_i, z)))
        outer_grid.append(o_row)
        inner_grid.append(i_row)

    inner_faces = set()
    for k in range(k_cnt - 1):
        for i in range(m_cnt - 1):
            bm.faces.new((outer_grid[k][i], outer_grid[k][i + 1], outer_grid[k + 1][i + 1], outer_grid[k + 1][i]))
            f_in = bm.faces.new((inner_grid[k][i + 1], inner_grid[k][i], inner_grid[k + 1][i], inner_grid[k + 1][i + 1]))
            inner_faces.add(f_in)

    # Bottom hem & top collar rim
    for i in range(m_cnt - 1):
        bm.faces.new((inner_grid[0][i], outer_grid[0][i], outer_grid[0][i + 1], inner_grid[0][i + 1]))
        f_top = bm.faces.new((outer_grid[-1][i], inner_grid[-1][i], inner_grid[-1][i + 1], outer_grid[-1][i + 1]))
        inner_faces.add(f_top)

    # Left-front (i = 0) and Right-front (i = m_cnt - 1) lapel thickness strips
    for k in range(k_cnt - 1):
        f_l = bm.faces.new((inner_grid[k][0], inner_grid[k + 1][0], outer_grid[k + 1][0], outer_grid[k][0]))
        f_r = bm.faces.new((outer_grid[k][-1], outer_grid[k + 1][-1], inner_grid[k + 1][-1], inner_grid[k][-1]))
        inner_faces.add(f_l)
        inner_faces.add(f_r)

    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    tagged = [idx for idx, f in enumerate(bm.faces) if f in inner_faces]

    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()

    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    for p in mesh.polygons:
        p.use_smooth = bool(shade_smooth)
    mesh.validate()
    mesh.update()

    aa.paint_mesh(
        obj,
        color=color,
        signal=signal,
        ao_strength=ao_strength,
        ridge_boost=ridge_boost,
        ground_ao=ground_ao,
        gradient_z=gradient_z,
    )
    if inner_color is not None and not signal and tagged:
        tag_set = set(tagged)
        aa.paint_faces(
            obj,
            predicate=lambda poly, _c, _n: poly.index in tag_set,
            color=inner_color,
            ao_strength=ao_strength * 0.9,
            top_highlight=0.06,
        )
    if bone:
        aa.pin_to_bone(obj, bone)
    return obj


def build_limb_loft(
    p0: Sequence[float],
    p1: Sequence[float],
    stations: Sequence[tuple[float, float, float, float, float]],
    segments: int = 10,
    fold_freq: int = 0,
    fold_amp: float = 0.0,
    name: str = 'LimbLoft',
    color=(0.30, 0.35, 0.42),
    gradient_z=None,
    shade_smooth: bool = True,
    ao_strength: float = 0.25,
    ridge_boost: float = 0.10,
    ground_ao: float = 0.14,
    bone: str | None = None,
) -> bpy.types.Object:
    """Loft closed cross-sections along p0 -> p1.

    Each station is (t, rx, ry, x_off, fwd_off) where:
      * +x_off shifts along +X (Left / Lateral)
      * +fwd_off shifts along -Y (Forward / Anterior, e.g. kneecap / shin / bicep)
    """
    v0 = Vector((float(p0[0]), float(p0[1]), float(p0[2])))
    v1 = Vector((float(p1[0]), float(p1[1]), float(p1[2])))
    delta = v1 - v0
    dir_vec = delta.normalized() if delta.length > 1e-6 else Vector((0.0, 0.0, -1.0))

    fwd_ref = Vector((0.0, -1.0, 0.0))
    if abs(dir_vec.dot(fwd_ref)) < 0.92:
        lat_x = fwd_ref.cross(dir_vec).normalized()
        fwd_y = dir_vec.cross(lat_x).normalized()
    else:
        up_ref = Vector((0.0, 0.0, 1.0))
        lat_x = up_ref.cross(dir_vec).normalized()
        fwd_y = dir_vec.cross(lat_x).normalized()

    rings = []
    for t, rx, ry, x_off, fwd_off in stations:
        center = v0 + delta * float(t) + lat_x * float(x_off) + fwd_y * float(fwd_off)
        env = math.sin(math.pi * _clamp(float(t), 0.0, 1.0))
        ring = []
        for i in range(segments):
            ang = (i / segments) * math.tau
            ca, sa = math.cos(ang), math.sin(ang)
            rip = fold_amp * env * math.cos(fold_freq * ang) if (fold_freq > 0 and fold_amp != 0.0) else 0.0
            pt = center + lat_x * (ca * (rx + rip)) + fwd_y * (sa * (ry + rip))
            ring.append((pt.x, pt.y, pt.z))
        rings.append(ring)

    # Reverse ring vertex order if needed? build_lofted_ring_solid calls recalc_face_normals!
    return build_lofted_ring_solid(
        rings,
        name=name,
        color=color,
        gradient_z=gradient_z,
        shade_smooth=shade_smooth,
        ao_strength=ao_strength,
        ridge_boost=ridge_boost,
        ground_ao=ground_ao,
        bone=bone,
    )


# ---------------------------------------------------------------------------
# Shared Anatomical Sculpting: Head, Facial Features, Ears & Articulated Hands
# ---------------------------------------------------------------------------

def build_sculpted_head(
    lm: dict,
    skin_color=(0.84, 0.61, 0.47),
    skin_shadow=(0.66, 0.44, 0.34),
    eye_color=(0.06, 0.07, 0.11),
    brow_color=(0.14, 0.14, 0.24),
    lip_color=(0.68, 0.42, 0.36),
) -> list[bpy.types.Object]:
    """Build a sculpted stylized AA human head (jawline, cheekbones, brow, nose bridge, ears, neck)."""
    parts: list[bpy.types.Object] = []
    neck_z = lm['neck_z']
    head_z = lm['head_z']
    crown_z = lm['crown_z']

    # 1. Anatomical neck column with trapezius flare & warm under-jaw shadow gradient
    parts.append(build_torso_column(
        stations=[
            (neck_z - 0.040, 0.078, 0.070, 0.076, 0.004),
            (neck_z + 0.002, 0.058, 0.057, 0.060, 0.001),
            (head_z - 0.018, 0.055, 0.055, 0.058, -0.003),
            (head_z + 0.024, 0.058, 0.058, 0.062, -0.005),
        ],
        segments=8,
        name='Anatomical_Neck',
        color=skin_color,
        gradient_z=(skin_shadow, skin_color),
        ao_strength=0.22,
        ground_ao=0.0,
    ))

    # 2. Sculpted Cranium + Jawline + Cheekbones + Brow Plane (pinned to 'Head')
    z0 = head_z - 0.026
    hh = crown_z - z0
    head_specs = [
        # (u, rx, y_front_mag, y_back_mag, yc, jaw_sharpen)
        (0.00, 0.046, 0.086, 0.048, -0.018, 0.16),  # Balanced chin base
        (0.13, 0.072, 0.098, 0.072, -0.012, 0.14),  # Defined jawline
        (0.27, 0.083, 0.102, 0.088, -0.006, 0.10),  # Jaw corners & mouth plane
        (0.41, 0.088, 0.104, 0.100, -0.002, 0.06),  # Cheekbones
        (0.53, 0.088, 0.099, 0.106,  0.002, 0.04),  # Eye socket plane (recessed slightly)
        (0.64, 0.090, 0.105, 0.108,  0.002, 0.05),  # Brow ridge
        (0.80, 0.085, 0.096, 0.104,  0.004, 0.02),  # Forehead dome
        (0.93, 0.064, 0.068, 0.078,  0.006, 0.00),  # Upper cranium
    ]
    segs = 10
    head_rings = []
    for u, rx, yf, yb, yc, jaw_s in head_specs:
        z = z0 + u * hh
        ring = []
        for i in range(segs):
            ang = (i / segs) * math.tau
            ca, sa = math.cos(ang), math.sin(ang)
            if sa < 0.0:
                # Taper front cheeks smoothly toward centerline chin/nose bridge
                front_taper = 1.0 - jaw_s * (ca * ca)
                x = ca * rx * front_taper
                y = yc + sa * yf * (1.0 + 0.08 * jaw_s * (1.0 - ca * ca))
            else:
                x = ca * rx
                y = yc + sa * yb
            ring.append((x, y, z))
        head_rings.append(ring)

    parts.append(build_lofted_ring_solid(
        head_rings,
        name='Sculpted_Head',
        color=skin_color,
        gradient_z=(skin_shadow, skin_color),
        shade_smooth=True,
        ao_strength=0.20,
        top_highlight=0.09,
        ridge_boost=0.08,
        ground_ao=0.0,
        base_point=(0.0, -0.048, z0 - 0.004),
        tip_point=(0.0, 0.008, z0 + hh),
        bone='Head',
    ))

    # 3. Sculpted Nose Bridge & Tip protruding cleanly toward -Y (Forward)
    nose_profile_yz = [
        (-0.092, z0 + 0.57 * hh),  # Glabella root at brow
        (-0.111, z0 + 0.47 * hh),  # Mid nasal bridge
        (-0.123, z0 + 0.36 * hh),  # Nose tip (-Y forward!)
        (-0.104, z0 + 0.31 * hh),  # Columella / nostril base
        (-0.088, z0 + 0.33 * hh),  # Inner anchor
    ]
    nose = aa.add_extruded_profile(
        nose_profile_yz,
        depth=0.022,
        axis='X',
        name='Nose_Bridge',
        bevel=0.004,
        bevel_segments=1,
        shade_smooth=True,
        color=skin_color,
        ao_strength=0.18,
        bone='Head',
    )
    # Taper top of nose bridge narrower than nostril base
    for v in nose.data.vertices:
        t_n = _clamp((v.co.z - (z0 + 0.31 * hh)) / (0.26 * hh), 0.0, 1.0)
        v.co.x *= _lerp(1.0, 0.56, t_n)
    nose.data.update()
    aa.paint_mesh(nose, color=skin_color, ao_strength=0.18, ground_ao=0.0)
    aa.pin_to_bone(nose, 'Head')
    parts.append(nose)

    # Subtle lower lip / mouth shadow accent
    parts.append(aa.add_box(
        name='Mouth_Line',
        size=(0.030, 0.012, 0.007),
        location=(0.0, -0.099, z0 + 0.235 * hh),
        color=lip_color,
        ao_strength=0.15,
        bone='Head',
    ))

    # 4. Stylized Almond Eyes, Catchlight Rim & Expressive Brows (L mirrored to R)
    eye_l = aa.add_box(
        name='Eye_L',
        size=(0.026, 0.014, 0.013),
        location=(0.034, -0.096, z0 + 0.495 * hh),
        rotation=(0.04, 0.06, -0.22),
        color=eye_color,
        ao_strength=0.10,
        bone='Head',
    )
    brow_l = aa.add_box(
        name='Eyebrow_L',
        size=(0.032, 0.014, 0.008),
        location=(0.035, -0.101, z0 + 0.575 * hh),
        rotation=(0.0, -0.10, -0.22),
        color=brow_color,
        ao_strength=0.14,
        bone='Head',
    )

    # 5. Sculpted Anatomical Ears (L mirrored to R)
    ear_profile_yz = [
        (0.008,  0.028),
        (-0.012, 0.024),
        (-0.014, -0.006),
        (-0.004, -0.026),
        (0.014, -0.020),
        (0.022,  0.008),
    ]
    ear_l = aa.add_extruded_profile(
        ear_profile_yz,
        depth=0.014,
        axis='X',
        name='Ear_L',
        location=(0.088, 0.006, z0 + 0.455 * hh),
        rotation=(-0.10, 0.14, 0.26),
        shade_smooth=True,
        color=skin_shadow,
        ao_strength=0.22,
        bone='Head',
    )

    sym_head = [eye_l, brow_l, ear_l]
    parts.extend(sym_head)
    parts.extend(aa.mirror_x(sym_head))
    return parts


def _build_bent_digit(
    points_and_radii: Sequence[tuple[tuple[float, float, float], float]],
    sides: int = 5,
    name: str = 'Digit',
    color=(0.84, 0.61, 0.47),
    palm_color=None,
    bone: str = 'Hand_L',
) -> bpy.types.Object:
    """Build a clean 3-station bent finger/thumb loft (~30 tris, zero internal caps)."""
    pts = [Vector(pr[0]) for pr in points_and_radii]
    rads = [float(pr[1]) for pr in points_and_radii]
    rings = []
    for idx, (pt, rad) in enumerate(zip(pts, rads)):
        if idx == 0:
            tan = (pts[1] - pts[0]).normalized()
        elif idx == len(pts) - 1:
            tan = (pts[-1] - pts[-2]).normalized()
        else:
            tan = ((pts[idx] - pts[idx - 1]).normalized() + (pts[idx + 1] - pts[idx]).normalized()).normalized()
        ref = Vector((0.0, -1.0, 0.0)) if abs(tan.dot(Vector((0.0, -1.0, 0.0)))) < 0.9 else Vector((1.0, 0.0, 0.0))
        u_ax = ref.cross(tan).normalized()
        v_ax = tan.cross(u_ax).normalized()
        ring = []
        for i in range(sides):
            ang = (i / sides) * math.tau
            p = pt + u_ax * (math.cos(ang) * rad) + v_ax * (math.sin(ang) * rad)
            ring.append((p.x, p.y, p.z))
        rings.append(ring)

    grad = (color, palm_color) if palm_color is not None else None
    return build_lofted_ring_solid(
        rings,
        name=name,
        color=color,
        gradient_z=grad,
        shade_smooth=True,
        ao_strength=0.22,
        ground_ao=0.0,
        bone=bone,
    )


def build_articulated_hand_pair(
    lm: dict,
    skin_color=(0.84, 0.61, 0.47),
    glove_color=None,
    cuff_color=None,
) -> list[bpy.types.Object]:
    """Build articulated L + R hands (palm, bent opposable thumb, 4 distinct curled fingers)."""
    wx, wy, wz = lm['wrist_L']
    palm_col = glove_color if glove_color is not None else skin_color
    left_parts: list[bpy.types.Object] = []

    if cuff_color is not None:
        left_parts.append(build_limb_loft(
            (wx - 0.002, wy + 0.004, wz + 0.022),
            (wx, wy - 0.002, wz - 0.010),
            stations=[
                (0.0, 0.036, 0.040, 0.0, 0.0),
                (1.0, 0.030, 0.035, 0.0, 0.0),
            ],
            segments=7,
            name='WristCuff_L',
            color=cuff_color,
            ao_strength=0.24,
            ground_ao=0.0,
            bone='Hand_L',
        ))

    # Sculpted tapered palm mitt (outer back of hand at +X, inner palm at -X, thumb side at -Y)
    left_parts.append(aa.add_bevel_box(
        name='Palm_L',
        size=(0.027, 0.068, 0.064),
        location=(wx - 0.002, wy - 0.005, wz - 0.032),
        rotation=(0.14, 0.05, 0.0),
        bevel=0.005,
        segments=1,
        taper=(0.92, 0.88),
        shade_smooth=True,
        color=palm_col,
        ao_strength=0.22,
        bone='Hand_L',
    ))

    # 3-station bent opposable thumb on forward (-Y) inner (-X) edge of palm
    left_parts.append(_build_bent_digit(
        [
            ((wx - 0.006, wy - 0.028, wz - 0.018), 0.0110),
            ((wx - 0.016, wy - 0.049, wz - 0.046), 0.0094),
            ((wx - 0.012, wy - 0.056, wz - 0.072), 0.0072),
        ],
        sides=5,
        name='Thumb_L',
        color=skin_color,
        palm_color=palm_col,
        bone='Hand_L',
    ))

    # 4 individual curled fingers (index -> pinky from -Y front to +Y back, curled gently -X)
    finger_specs = [
        ('Index',  -0.024, 0.056, 0.0084),
        ('Middle', -0.008, 0.062, 0.0088),
        ('Ring',    0.008, 0.057, 0.0082),
        ('Pinky',   0.023, 0.046, 0.0073),
    ]
    knuckle_z = wz - 0.058
    for fname, dy, flen, frad in finger_specs:
        left_parts.append(_build_bent_digit(
            [
                ((wx - 0.003, wy - 0.006 + dy, knuckle_z), 0.98 * frad),
                ((wx - 0.009, wy - 0.010 + dy, knuckle_z - flen * 0.52), 0.86 * frad),
                ((wx - 0.020, wy - 0.009 + dy, knuckle_z - flen * 0.96), 0.66 * frad),
            ],
            sides=5,
            name=f'Finger_{fname}_L',
            color=skin_color,
            bone='Hand_L',
        ))

    right_parts = aa.mirror_x(left_parts)
    return left_parts + right_parts


def apply_character_seam_ao(
    body_obj: bpy.types.Object,
    seam_zs: Sequence[tuple[float, float, float]],
    chin_z: float,
    crotch_z: float,
) -> None:
    """Accentuate clothing hem/belt/collar seams, under-chin shadow, and inner-leg AO in 'Color'."""
    mesh = body_obj.data
    col = mesh.color_attributes.get('Color')
    if col is None:
        return
    verts = mesh.vertices
    loops = mesh.loops

    v_mult = [1.0] * len(verts)
    for i, v in enumerate(verts):
        p = v.co
        m = 1.0
        # Subtle under-chin / jaw-neck crease occlusion
        if chin_z - 0.055 < p.z < chin_z + 0.010 and p.y < 0.01 and abs(p.x) < 0.072:
            t_c = 1.0 - abs(p.z - (chin_z - 0.018)) / 0.040
            if t_c > 0.0:
                m *= 1.0 - 0.22 * t_c
        # Inner thigh / crotch crease occlusion
        if crotch_z - 0.14 < p.z < crotch_z + 0.03 and abs(p.x) < 0.058:
            t_in = (1.0 - abs(p.x) / 0.058) * (1.0 - abs(p.z - (crotch_z - 0.04)) / 0.11)
            if t_in > 0.0:
                m *= 1.0 - 0.24 * t_in
        # Garment hem / belt / cuff shadow bands: (z_center, half_span, strength)
        for sz, span, strength in seam_zs:
            dz = abs(p.z - sz)
            if dz < span:
                w = 1.0 - (dz / span)
                m *= 1.0 - strength * (w * w)
        v_mult[i] = max(0.52, m)

    for li, loop in enumerate(loops):
        k = v_mult[loop.vertex_index]
        if k < 0.999:
            r, g, b, a = col.data[li].color
            col.data[li].color = (r * k, g * k, b * k, a)
    mesh.update()


# ---------------------------------------------------------------------------
# 1. Neo-Kyoto Streetwear Citizen — public/street/resident.glb
# ---------------------------------------------------------------------------

def build_resident() -> dict:
    """Author public/street/resident.glb (~1.78 m Neo-Kyoto Streetwear Citizen)."""
    aa.reset_scene()
    lm = rig_landmarks(height=1.752, shoulder_width=0.218, hip_width=0.106)
    parts: list[bpy.types.Object] = []

    # Distinct vivid base colors for cel-shader readability
    c_skin = (0.84, 0.61, 0.47)
    c_skin_sh = (0.66, 0.44, 0.34)
    c_hair_base = (0.13, 0.16, 0.36)
    c_hair_hi = (0.27, 0.33, 0.60)
    c_shirt = (0.82, 0.84, 0.87)
    c_jacket = (0.09, 0.48, 0.60)
    c_jacket_hi = (0.16, 0.60, 0.72)
    c_jacket_yoke = (0.12, 0.19, 0.31)
    c_lining = (0.88, 0.33, 0.22)
    c_rib = (0.09, 0.11, 0.16)
    c_pants = (0.23, 0.27, 0.21)
    c_pants_panel = (0.14, 0.17, 0.15)
    c_strap = (0.11, 0.13, 0.18)
    c_shoe_sole = (0.87, 0.86, 0.83)
    c_shoe_red = (0.78, 0.17, 0.20)
    c_shoe_dark = (0.10, 0.12, 0.17)
    c_metal = (0.66, 0.72, 0.78)
    c_sig_cyan = (0.04, 0.94, 1.00)

    # --- A. Sculpted Head & Layered Neo-Tokyo Asymmetric Hair ---
    parts.extend(build_sculpted_head(
        lm,
        skin_color=c_skin,
        skin_shadow=c_skin_sh,
        eye_color=(0.06, 0.08, 0.13),
        brow_color=c_hair_base,
        lip_color=(0.70, 0.43, 0.37),
    ))

    hz = lm['head_z']
    cz = lm['crown_z']
    # Layered hair cap wrapping back & crown of skull (total peak ~1.785 m)
    hair_cap_stations = [
        (hz + 0.035, 0.082, 0.055, 0.106, 0.018, 1.08),
        (hz + 0.105, 0.096, 0.088, 0.118, 0.010, 0.86),
        (hz + 0.175, 0.099, 0.106, 0.120, 0.004, 0.48),
        (cz - 0.030, 0.088, 0.102, 0.110, 0.002, 0.18),
        (cz + 0.022, 0.048, 0.062, 0.068, 0.004, 0.12),
    ]
    parts.append(build_open_garment_shell(
        hair_cap_stations,
        thickness=0.018,
        arc_segments=9,
        fold_freq=8,
        fold_amp=0.004,
        name='Hair_Cap',
        color=c_hair_base,
        gradient_z=(c_hair_base, c_hair_hi),
        shade_smooth=False,
        ao_strength=0.28,
        ridge_boost=0.14,
        ground_ao=0.0,
        bone='Head',
    ))

    # Crown layered hair spikes / swept bangs (-Y front & side locks)
    bang_specs = [
        # (loc, size, rot, shear_y)
        ((-0.022, -0.092, cz - 0.045), (0.046, 0.068, 0.072), (0.36, -0.18, 0.22), -0.018),
        (( 0.028, -0.096, cz - 0.052), (0.048, 0.066, 0.078), (0.42,  0.16, -0.18), -0.020),
        (( 0.064, -0.070, cz - 0.072), (0.034, 0.056, 0.088), (0.24,  0.22, -0.35), -0.012),
        ((-0.066, -0.066, cz - 0.076), (0.032, 0.052, 0.082), (0.22, -0.20,  0.34), -0.012),
        (( 0.005, -0.018, cz + 0.006), (0.068, 0.112, 0.048), (-0.16, 0.10, -0.08), -0.014),
        ((-0.026,  0.032, cz - 0.004), (0.058, 0.098, 0.044), (-0.28, -0.14, 0.12),  0.010),
    ]
    for idx, (loc, sz, rot, sy) in enumerate(bang_specs):
        parts.append(aa.add_bevel_box(
            name=f'Hair_Lock_{idx}',
            size=sz,
            location=loc,
            rotation=rot,
            bevel=0.004,
            segments=1,
            taper=(0.55, 0.65),
            shear_y=sy,
            color=c_hair_hi if idx >= 4 else c_hair_base,
            ao_strength=0.26,
            bone='Head',
        ))

    # --- B. Layered Mock-Neck Inner Tech Tee + Cropped Bomber Jacket ---
    # Inner pearl tech tee extending slightly below cropped bomber hem (0.94..1.38 m)
    parts.append(build_torso_column(
        stations=[
            (0.925, 0.136, 0.096, 0.100, -0.004),
            (1.010, 0.130, 0.092, 0.098, -0.004),
            (1.140, 0.142, 0.104, 0.106, -0.006),
            (1.275, 0.152, 0.110, 0.110, -0.006),
            (1.365, 0.084, 0.076, 0.078, -0.002),
            (1.395, 0.068, 0.066, 0.068,  0.000),
        ],
        segments=9,
        fold_freq=5,
        fold_amp=0.0025,
        name='Inner_Tech_Tee',
        color=c_shirt,
        ao_strength=0.24,
        ground_ao=0.06,
    ))

    # Dark ribbed elastic bomber waistband (0.99..1.045 m, open front, rx < arm_split_x)
    parts.append(build_open_garment_shell(
        stations=[
            (0.990, 0.146, 0.106, 0.110, -0.004, 0.25),
            (1.045, 0.156, 0.114, 0.118, -0.004, 0.26),
        ],
        thickness=0.015,
        arc_segments=9,
        fold_freq=10,
        fold_amp=0.002,
        name='Bomber_Rib_Hem',
        color=c_rib,
        ao_strength=0.28,
    ))

    # Cropped puffy Neo-Tokyo techwear bomber body (1.04..1.36 m, rx strictly < arm_split_x=0.1788)
    parts.append(build_open_garment_shell(
        stations=[
            (1.040, 0.156, 0.116, 0.120, -0.005, 0.26),
            (1.115, 0.165, 0.126, 0.130, -0.006, 0.28),
            (1.225, 0.168, 0.130, 0.132, -0.006, 0.31),
            (1.315, 0.166, 0.122, 0.124, -0.004, 0.35),
            (1.365, 0.120, 0.096, 0.100,  0.000, 0.40),
        ],
        thickness=0.018,
        arc_segments=10,
        fold_freq=6,
        fold_amp=0.0040,
        name='Bomber_Jacket_Body',
        color=c_jacket,
        inner_color=c_lining,
        gradient_z=(c_jacket, c_jacket_hi),
        shade_smooth=False,
        ao_strength=0.28,
        ridge_boost=0.13,
    ))

    # Contrasting navy-slate shoulder yoke cap (pinned to Chest so shoulders stay crisp)
    parts.append(build_open_garment_shell(
        stations=[
            (1.285, 0.170, 0.126, 0.128, -0.004, 0.34),
            (1.345, 0.162, 0.114, 0.118, -0.002, 0.38),
            (1.372, 0.108, 0.090, 0.094,  0.000, 0.42),
        ],
        thickness=0.014,
        arc_segments=8,
        fold_freq=0,
        fold_amp=0.0,
        name='Bomber_Shoulder_Yoke',
        color=c_jacket_yoke,
        ao_strength=0.24,
        bone='Chest',
    ))

    # Tall structured popped bomber collar + coral inner lining + 'Signal' neon cyan trim
    parts.append(build_open_garment_shell(
        stations=[
            (1.355, 0.094, 0.088, 0.092, 0.002, 0.42),
            (1.425, 0.102, 0.094, 0.098, 0.005, 0.46),
            (1.482, 0.110, 0.100, 0.104, 0.008, 0.50),
        ],
        thickness=0.014,
        arc_segments=8,
        fold_freq=0,
        fold_amp=0.0,
        name='Bomber_Popped_Collar',
        color=c_jacket_yoke,
        inner_color=c_lining,
        ao_strength=0.24,
        bone='Chest',
    ))

    # Luminous 'Signal' cyber-trim along popped collar top rim (pinned to Chest!)
    parts.append(build_open_garment_shell(
        stations=[
            (1.474, 0.112, 0.102, 0.106, 0.008, 0.50),
            (1.490, 0.114, 0.104, 0.108, 0.009, 0.50),
        ],
        thickness=0.012,
        arc_segments=8,
        fold_freq=0,
        fold_amp=0.0,
        name='Signal_Collar_Trim',
        color=c_sig_cyan,
        signal=True,
        bone='Chest',
    ))

    # Front zipper track (metal) + Luminous 'Signal' chest cyber-chevron strips (L mirrored to R)
    zip_l = aa.add_box(
        name='Zipper_Track_L',
        size=(0.010, 0.012, 0.31),
        location=(0.044, -0.118, 1.195),
        rotation=(0.04, 0.04, -0.06),
        color=c_metal,
        bone='Chest',
    )
    sig_chest_l = aa.add_box(
        name='Signal_Chest_Strip_L',
        size=(0.014, 0.012, 0.185),
        location=(0.064, -0.123, 1.235),
        rotation=(0.05, 0.08, -0.10),
        color=c_sig_cyan,
        signal=True,
        bone='Chest',
    )
    sig_clav_l = aa.add_box(
        name='Signal_Chevron_L',
        size=(0.068, 0.012, 0.013),
        location=(0.106, -0.112, 1.308),
        rotation=(0.12, -0.22, -0.26),
        color=c_sig_cyan,
        signal=True,
        bone='Chest',
    )
    chest_sym = [zip_l, sig_chest_l, sig_clav_l]
    parts.extend(chest_sym)
    parts.extend(aa.mirror_x(chest_sym))

    # --- C. Bomber Sleeves (L mirrored to R, tucked seamlessly into shoulder yoke) ---
    sh_in_l = (lm['shoulder_L'][0] - 0.036, 0.0, lm['sh_z'] + 0.014)
    upper_sl_l = build_limb_loft(
        sh_in_l,
        lm['elbow_L'],
        stations=[
            (0.00, 0.068, 0.072, 0.000,  0.000),
            (0.35, 0.065, 0.067, 0.004,  0.002),
            (0.75, 0.058, 0.060, 0.002,  0.000),
            (1.06, 0.053, 0.055, 0.000, -0.002),
        ],
        segments=8,
        fold_freq=4,
        fold_amp=0.003,
        name='Bomber_UpperSleeve_L',
        color=c_jacket,
        gradient_z=(c_jacket, c_jacket_hi),
        shade_smooth=False,
        ao_strength=0.26,
        ridge_boost=0.12,
        bone='UpperArm_L',
    )
    fore_sl_l = build_limb_loft(
        lm['elbow_L'],
        lm['wrist_L'],
        stations=[
            (-0.06, 0.053, 0.055, 0.0, 0.0),
            ( 0.28, 0.055, 0.057, 0.002, 0.002),
            ( 0.68, 0.050, 0.052, 0.001, 0.002),
            ( 0.96, 0.039, 0.041, 0.0, 0.0),
        ],
        segments=8,
        fold_freq=5,
        fold_amp=0.0035,
        name='Bomber_ForeSleeve_L',
        color=c_jacket,
        shade_smooth=False,
        ao_strength=0.26,
        ridge_boost=0.12,
        bone='Forearm_L',
    )
    arm_pkt_l = aa.add_bevel_box(
        name='Sleeve_TechPocket_L',
        size=(0.022, 0.052, 0.068),
        location=(lm['shoulder_L'][0] + 0.056, 0.004, lm['sh_z'] - 0.088),
        rotation=(0.05, 0.16, 0.0),
        bevel=0.004,
        segments=1,
        color=c_jacket_yoke,
        bone='UpperArm_L',
    )
    parts.extend([upper_sl_l, fore_sl_l, arm_pkt_l])
    parts.extend(aa.mirror_x([upper_sl_l, fore_sl_l, arm_pkt_l]))

    parts.extend(build_articulated_hand_pair(
        lm,
        skin_color=c_skin,
        glove_color=c_rib,
        cuff_color=c_rib,
    ))

    # --- D. Pelvis Block, Tactical Belt & Slim Tapered Cargo Trousers ---
    parts.append(build_torso_column(
        stations=[
            (0.805, 0.142, 0.098, 0.106,  0.002),
            (0.875, 0.148, 0.102, 0.110,  0.000),
            (0.945, 0.140, 0.098, 0.104, -0.002),
        ],
        segments=9,
        name='Cargo_Pelvis',
        color=c_pants,
        ao_strength=0.28,
        bone='Pelvis',
    ))
    parts.append(build_torso_column(
        stations=[
            (0.910, 0.146, 0.103, 0.108, -0.002),
            (0.942, 0.146, 0.103, 0.108, -0.002),
        ],
        segments=9,
        name='Tactical_Belt',
        color=c_strap,
        ao_strength=0.22,
        bone='Pelvis',
    ))

    thigh_l = build_limb_loft(
        lm['hip_L'],
        lm['knee_L'],
        stations=[
            (-0.06, 0.076, 0.082, 0.000, 0.000),
            ( 0.30, 0.074, 0.078, 0.003, 0.003),
            ( 0.68, 0.064, 0.068, 0.002, 0.004),
            ( 1.05, 0.056, 0.060, 0.000, 0.004),
        ],
        segments=8,
        fold_freq=4,
        fold_amp=0.0025,
        name='Cargo_Thigh_L',
        color=c_pants,
        shade_smooth=False,
        ao_strength=0.26,
        ridge_boost=0.11,
        bone='Thigh_L',
    )
    cargo_pkt_l = aa.add_bevel_box(
        name='Cargo_Pocket_L',
        size=(0.028, 0.086, 0.125),
        location=(lm['hip_L'][0] + 0.068, -0.006, 0.690),
        rotation=(0.04, 0.07, -0.08),
        bevel=0.005,
        segments=1,
        color=c_pants_panel,
        ao_strength=0.28,
        bone='Thigh_L',
    )
    knee_pad_l = aa.add_bevel_box(
        name='Cargo_KneePanel_L',
        size=(0.074, 0.028, 0.090),
        location=(lm['knee_L'][0], lm['knee_L'][1] - 0.052, lm['knee_z'] + 0.008),
        rotation=(-0.06, 0.0, 0.0),
        bevel=0.005,
        segments=1,
        color=c_pants_panel,
        ao_strength=0.24,
        bone='Shin_L',
    )
    shin_l = build_limb_loft(
        lm['knee_L'],
        lm['ankle_L'],
        stations=[
            (-0.06, 0.055, 0.058, 0.0,  0.002),
            ( 0.32, 0.056, 0.062, 0.0, -0.006),  # Calf taper
            ( 0.72, 0.046, 0.050, 0.0, -0.002),
            ( 1.06, 0.041, 0.044, 0.0,  0.000),  # Tucked cleanly into high-top sneaker
        ],
        segments=8,
        fold_freq=5,
        fold_amp=0.002,
        name='Cargo_Shin_L',
        color=c_pants,
        shade_smooth=False,
        ao_strength=0.26,
        ridge_boost=0.10,
    )
    leg_sym = [thigh_l, cargo_pkt_l, knee_pad_l, shin_l]
    parts.extend(leg_sym)
    parts.extend(aa.mirror_x(leg_sym))

    # --- E. High-Top Cyber Sneakers (Grounded flat at z = 0.000, toes toward -Y) ---
    ax = lm['ankle_x']
    sole_profile_yz = [
        ( 0.072, 0.000),
        (-0.228, 0.000),
        (-0.252, 0.012),
        (-0.244, 0.032),
        (-0.095, 0.034),
        ( 0.078, 0.038),
    ]
    sole_l = aa.add_extruded_profile(
        sole_profile_yz,
        depth=0.096,
        axis='X',
        name='Sneaker_Midsole_L',
        location=(ax, 0.0, 0.0),
        color=c_shoe_sole,
        ao_strength=0.22,
    )
    upper_profile_yz = [
        ( 0.068, 0.032),
        (-0.232, 0.028),
        (-0.215, 0.058),
        (-0.125, 0.092),
        (-0.035, 0.148),
        ( 0.058, 0.142),
    ]
    upper_l = aa.add_extruded_profile(
        upper_profile_yz,
        depth=0.088,
        axis='X',
        name='Sneaker_Upper_L',
        location=(ax, 0.0, 0.0),
        bevel=0.005,
        bevel_segments=1,
        color=c_shoe_red,
        ao_strength=0.26,
    )
    collar_l = aa.add_bevel_box(
        name='Sneaker_AnkleStrap_L',
        size=(0.094, 0.102, 0.034),
        location=(ax, 0.006, 0.132),
        rotation=(0.12, 0.0, 0.0),
        bevel=0.004,
        segments=1,
        color=c_shoe_dark,
        ao_strength=0.24,
    )
    tongue_l = aa.add_box(
        name='Sneaker_Tongue_L',
        size=(0.064, 0.024, 0.068),
        location=(ax, -0.056, 0.132),
        rotation=(-0.34, 0.0, 0.0),
        color=c_shoe_dark,
    )
    heel_sig_l = aa.add_box(
        name='Signal_SneakerHeel_L',
        size=(0.048, 0.014, 0.012),
        location=(ax, 0.074, 0.052),
        color=c_sig_cyan,
        signal=True,
    )
    shoe_sym = [sole_l, upper_l, collar_l, tongue_l, heel_sig_l]
    parts.extend(shoe_sym)
    parts.extend(aa.mirror_x(shoe_sym))

    # --- Batch, Bake Seam AO, Rig & Export ---
    meshes = aa.combine_by_role(parts)
    body_mesh = next(m for m in meshes if m.name == 'Body')
    apply_character_seam_ao(
        body_mesh,
        seam_zs=[
            (0.992, 0.028, 0.22),  # Under cropped bomber rib hem
            (0.922, 0.024, 0.18),  # Under layered inner tee hem
            (1.360, 0.026, 0.18),  # Collar base seam
            (0.145, 0.024, 0.20),  # Trouser cuff into high-top sneaker
        ],
        chin_z=lm['head_z'] - 0.012,
        crotch_z=lm['pelvis_z'],
    )

    rig = aa.build_humanoid_rig_and_clips(
        meshes,
        rig_name='StreetRig',
        height=lm['h'],
        style='citizen',
        attack_clip_name='Punch',
        shoulder_width=lm['sh_x'],
        hip_width=lm['hp_x'],
    )
    return aa.export_glb(OUT_DIR / 'resident.glb', [rig] + meshes, min_tris=1800, max_meshes=6)


# ---------------------------------------------------------------------------
# 2. District Cyber-Artisan — public/street/mechanic.glb
# ---------------------------------------------------------------------------

def build_mechanic() -> dict:
    """Author public/street/mechanic.glb (~1.80 m District Cyber-Artisan)."""
    aa.reset_scene()
    lm = rig_landmarks(height=1.778, shoulder_width=0.236, hip_width=0.112)
    parts: list[bpy.types.Object] = []

    # Distinct industrial cyber-artisan palette
    c_skin = (0.68, 0.45, 0.31)
    c_skin_sh = (0.52, 0.32, 0.21)
    c_hair = (0.27, 0.15, 0.10)
    c_hair_hi = (0.40, 0.23, 0.15)
    c_shirt = (0.24, 0.39, 0.56)
    c_shirt_cuff = (0.31, 0.47, 0.65)
    c_vest = (0.85, 0.43, 0.11)
    c_vest_hi = (0.94, 0.56, 0.16)
    c_vest_dark = (0.15, 0.18, 0.24)
    c_leather = (0.34, 0.20, 0.11)
    c_pouch = (0.53, 0.33, 0.18)
    c_glove = (0.20, 0.14, 0.11)
    c_pants = (0.15, 0.22, 0.34)
    c_knee = (0.23, 0.29, 0.39)
    c_boot = (0.24, 0.14, 0.09)
    c_sole = (0.08, 0.09, 0.11)
    c_steel = (0.62, 0.68, 0.74)
    c_brass = (0.78, 0.59, 0.22)
    c_sig_amber = (1.00, 0.62, 0.08)
    c_sig_cyan = (0.06, 0.92, 1.00)

    # --- A. Sculpted Head, Short Textured Hair & Flip-Up Cyber-Loupe Visor ('Signal') ---
    parts.extend(build_sculpted_head(
        lm,
        skin_color=c_skin,
        skin_shadow=c_skin_sh,
        eye_color=(0.07, 0.07, 0.09),
        brow_color=c_hair,
        lip_color=(0.55, 0.34, 0.25),
    ))

    hz = lm['head_z']
    cz = lm['crown_z']
    # Short textured artisan crop hair cap (peak ~1.802 m)
    parts.append(build_open_garment_shell(
        stations=[
            (hz + 0.065, 0.084, 0.048, 0.104, 0.016, 1.25),
            (hz + 0.135, 0.092, 0.080, 0.114, 0.008, 1.02),
            (cz - 0.040, 0.088, 0.098, 0.108, 0.004, 0.55),
            (cz + 0.012, 0.058, 0.068, 0.074, 0.004, 0.20),
        ],
        thickness=0.015,
        arc_segments=9,
        fold_freq=9,
        fold_amp=0.003,
        name='Short_Textured_Hair',
        color=c_hair,
        gradient_z=(c_hair, c_hair_hi),
        shade_smooth=False,
        ao_strength=0.28,
        ridge_boost=0.14,
        ground_ao=0.0,
        bone='Head',
    ))
    for i, (dx, dy, dz, rz) in enumerate([
        (-0.028, -0.046, cz + 0.006,  0.18),
        ( 0.024, -0.052, cz + 0.008, -0.16),
        ( 0.000,  0.005, cz + 0.014,  0.04),
    ]):
        parts.append(aa.add_bevel_box(
            name=f'Hair_CropTuft_{i}',
            size=(0.048, 0.074, 0.032),
            location=(dx, dy, dz),
            rotation=(-0.12, 0.0, rz),
            bevel=0.004,
            segments=1,
            taper=(0.65, 0.72),
            color=c_hair_hi,
            bone='Head',
        ))

    # Flip-up Cyber-Loupe Visor mounted on forehead (Body frame + Signal glowing lenses, pinned to Head)
    visor_z = hz + 0.182
    parts.append(aa.add_torus(
        name='Loupe_Headband',
        major_radius=0.094,
        minor_radius=0.010,
        major_segments=12,
        minor_segments=5,
        location=(0.0, 0.002, visor_z - 0.012),
        rotation=(0.12, 0.0, 0.0),
        scale=(0.98, 1.12, 1.0),
        color=c_vest_dark,
        bone='Head',
    ))
    hinge_l = aa.add_cylinder(
        name='Loupe_Hinge_L',
        radius=0.018,
        depth=0.022,
        vertices=8,
        location=(0.092, -0.015, visor_z - 0.008),
        rotation=(0.0, math.pi * 0.5, 0.0),
        color=c_brass,
        bone='Head',
    )
    parts.extend([hinge_l, aa.mirror_x(hinge_l)])

    # Flipped-up twin precision loupe visor housing perched above brow (-Y front)
    parts.append(aa.add_bevel_box(
        name='Loupe_Visor_Bridge',
        size=(0.146, 0.040, 0.038),
        location=(0.0, -0.098, visor_z + 0.012),
        rotation=(-0.36, 0.0, 0.0),
        bevel=0.005,
        segments=1,
        color=c_vest_dark,
        bone='Head',
    ))
    # Asymmetric precision barrels: wide amber macro loupe on right, high-mag cyan micro-loupe on left
    for side_nm, lx, rad, sig_col in (
        ('RightMacro', -0.038, 0.023, c_sig_amber),
        ('LeftMicro',   0.040, 0.019, c_sig_cyan),
    ):
        parts.append(aa.add_cylinder(
            name=f'Loupe_Barrel_{side_nm}',
            radius=rad + 0.004,
            depth=0.034,
            vertices=9,
            location=(lx, -0.112, visor_z + 0.018),
            rotation=(math.pi * 0.5 - 0.36, 0.0, 0.0),
            color=c_brass,
            bone='Head',
        ))
        parts.append(aa.add_cylinder(
            name=f'Signal_LoupeLens_{side_nm}',
            radius=rad,
            depth=0.012,
            vertices=9,
            location=(lx, -0.126, visor_z + 0.023),
            rotation=(math.pi * 0.5 - 0.36, 0.0, 0.0),
            color=sig_col,
            signal=True,
            bone='Head',
        ))
    # Central luminous forehead visor strip ('Signal')
    parts.append(aa.add_box(
        name='Signal_VisorBar',
        size=(0.128, 0.012, 0.012),
        location=(0.0, -0.115, visor_z + 0.004),
        rotation=(-0.36, 0.0, 0.0),
        color=c_sig_amber,
        signal=True,
        bone='Head',
    ))

    # --- B. Chambray Work Shirt + Broad-Shouldered Utility Vest + Cross-Chest Tool Rig ---
    parts.append(build_torso_column(
        stations=[
            (0.900, 0.152, 0.108, 0.112, -0.002),
            (1.030, 0.150, 0.106, 0.112, -0.004),
            (1.180, 0.164, 0.116, 0.120, -0.005),
            (1.320, 0.174, 0.120, 0.124, -0.004),
            (1.390, 0.088, 0.080, 0.084,  0.000),
        ],
        segments=9,
        fold_freq=5,
        fold_amp=0.0025,
        name='Work_Shirt_Torso',
        color=c_shirt,
        ao_strength=0.25,
    ))

    # Broad-shouldered padded utility vest (0.95..1.38 m, rx strictly < arm_split_x=0.1935)
    parts.append(build_open_garment_shell(
        stations=[
            (0.950, 0.164, 0.122, 0.126, -0.004, 0.28),
            (1.060, 0.172, 0.128, 0.132, -0.005, 0.29),
            (1.210, 0.180, 0.136, 0.138, -0.005, 0.31),
            (1.335, 0.183, 0.132, 0.136, -0.003, 0.35),
            (1.385, 0.122, 0.098, 0.104,  0.002, 0.42),
        ],
        thickness=0.020,
        arc_segments=10,
        fold_freq=4,
        fold_amp=0.003,
        name='Utility_Vest',
        color=c_vest,
        inner_color=c_vest_dark,
        gradient_z=(c_vest, c_vest_hi),
        shade_smooth=False,
        ao_strength=0.28,
        ridge_boost=0.13,
    ))

    # Standing utility vest work collar
    parts.append(build_open_garment_shell(
        stations=[
            (1.375, 0.102, 0.090, 0.096, 0.002, 0.44),
            (1.438, 0.108, 0.094, 0.100, 0.004, 0.48),
        ],
        thickness=0.015,
        arc_segments=8,
        fold_freq=0,
        fold_amp=0.0,
        name='Vest_Collar',
        color=c_vest_dark,
        bone='Chest',
    ))

    # Broad reinforced shoulder armor/utility pads (L mirrored to R, pinned to Clavicle_L/R)
    sh_pad_l = aa.add_bevel_box(
        name='Shoulder_Pad_L',
        size=(0.118, 0.168, 0.048),
        location=(lm['sh_x'] - 0.020, -0.004, lm['sh_z'] + 0.030),
        rotation=(0.04, 0.18, 0.0),
        bevel=0.008,
        segments=1,
        color=c_vest_dark,
        ao_strength=0.24,
        bone='Clavicle_L',
    )
    parts.extend([sh_pad_l, aa.mirror_x(sh_pad_l)])

    # Glowing chest diagnostic badge ('Signal', pinned to Chest)
    parts.append(aa.add_box(
        name='Chest_Badge_Frame',
        size=(0.048, 0.016, 0.030),
        location=(0.094, -0.134, 1.255),
        rotation=(0.05, 0.0, -0.18),
        color=c_vest_dark,
        bone='Chest',
    ))
    parts.append(aa.add_box(
        name='Signal_Chest_Readout',
        size=(0.036, 0.012, 0.016),
        location=(0.095, -0.141, 1.255),
        rotation=(0.05, 0.0, -0.18),
        color=c_sig_amber,
        signal=True,
        bone='Chest',
    ))

    # Cross-chest leather tool bandolier rig (closed cyclic loop riding proud over vest front & back)
    parts.append(aa.add_pipe(
        points=[
            (-0.122, -0.102, 1.388),
            (-0.042, -0.146, 1.235),
            ( 0.058, -0.142, 1.095),
            ( 0.164, -0.070, 0.962),
            ( 0.156,  0.105, 0.978),
            ( 0.030,  0.152, 1.155),
            (-0.092,  0.148, 1.335),
        ],
        radius=0.014,
        bevel_resolution=1,
        cyclic=True,
        fill_caps=False,
        name='CrossChest_ToolRig',
        color=c_leather,
        ao_strength=0.26,
        bone='Chest',
    ))
    parts.append(aa.add_box(
        name='Rig_Center_Buckle',
        size=(0.044, 0.022, 0.042),
        location=(-0.010, -0.146, 1.180),
        rotation=(0.0, 0.48, 0.0),
        color=c_brass,
        bone='Chest',
    ))
    # Chest tool holsters / torque driver cylinders clipped to bandolier
    for idx, (tx, ty, tz) in enumerate([(-0.070, -0.140, 1.265), (-0.042, -0.145, 1.230)]):
        parts.append(aa.add_cylinder(
            name=f'Chest_ToolCell_{idx}',
            radius=0.012,
            depth=0.068,
            vertices=6,
            location=(tx, ty, tz),
            rotation=(0.08, 0.36, 0.0),
            color=c_steel if idx == 0 else c_brass,
            bone='Chest',
        ))

    # Heavy Waist Utility Belt & Sculpted Tool Pouches (pinned to Pelvis so they never stick to hands!)
    parts.append(build_torso_column(
        stations=[
            (0.892, 0.162, 0.116, 0.120, -0.002),
            (0.942, 0.164, 0.118, 0.122, -0.002),
        ],
        segments=9,
        name='Artisan_Tool_Belt',
        color=c_leather,
        ao_strength=0.24,
        bone='Pelvis',
    ))
    parts.append(aa.add_box(
        name='Belt_Brass_Buckle',
        size=(0.056, 0.022, 0.048),
        location=(0.0, -0.122, 0.918),
        color=c_brass,
        bone='Pelvis',
    ))
    pouch_specs = [
        ('Pouch_Hip_L',   ( 0.158, -0.028, 0.895), (0.046, 0.092, 0.105), (0.0,  0.06, -0.18)),
        ('Pouch_Hip_R',   (-0.158, -0.022, 0.895), (0.046, 0.088, 0.102), (0.0, -0.06,  0.16)),
        ('Pouch_Rear_R',  (-0.092,  0.118, 0.902), (0.078, 0.044, 0.088), (0.0,  0.00, -0.25)),
        ('Pouch_Front_L', ( 0.098, -0.106, 0.900), (0.058, 0.038, 0.078), (0.0,  0.00, -0.45)),
    ]
    for pname, ploc, psz, prot in pouch_specs:
        parts.append(aa.add_bevel_box(
            name=pname,
            size=psz,
            location=ploc,
            rotation=prot,
            bevel=0.005,
            segments=1,
            color=c_pouch,
            ao_strength=0.28,
            bone='Pelvis',
        ))
    # Holstered steel wrench at right hip + glowing battery cell on left hip
    parts.append(aa.add_box(
        name='Hip_Spanner_Wrench',
        size=(0.016, 0.032, 0.175),
        location=(-0.176, 0.032, 0.855),
        rotation=(0.18, -0.08, 0.0),
        color=c_steel,
        bone='Pelvis',
    ))
    parts.append(aa.add_box(
        name='Signal_Belt_Cell',
        size=(0.012, 0.022, 0.048),
        location=(0.182, -0.028, 0.900),
        color=c_sig_cyan,
        signal=True,
        bone='Pelvis',
    ))

    # --- C. Rolled-Sleeve Shirt Arms + Bare Forearms + Gloved Articulated Hands ---
    sh_in_l = (lm['shoulder_L'][0] - 0.038, 0.0, lm['sh_z'] + 0.012)
    sl_upper_l = build_limb_loft(
        sh_in_l,
        lm['elbow_L'],
        stations=[
            (0.00, 0.068, 0.070, 0.000, 0.0),
            (0.45, 0.062, 0.064, 0.002, 0.002),
            (0.88, 0.056, 0.058, 0.000, 0.0),
            (1.02, 0.055, 0.057, 0.000, 0.0),
        ],
        segments=8,
        fold_freq=4,
        fold_amp=0.0025,
        name='Rolled_ShirtSleeve_L',
        color=c_shirt,
        shade_smooth=False,
        ao_strength=0.26,
        bone='UpperArm_L',
    )
    # Thick rolled-up cuff donut right at elbow
    roll_cuff_l = build_limb_loft(
        lm['elbow_L'],
        lm['wrist_L'],
        stations=[
            (-0.06, 0.062, 0.064, 0.0, 0.0),
            ( 0.08, 0.065, 0.067, 0.0, 0.0),
            ( 0.20, 0.058, 0.060, 0.0, 0.0),
        ],
        segments=8,
        name='Rolled_Cuff_L',
        color=c_shirt_cuff,
        ao_strength=0.24,
        bone='Forearm_L',
    )
    bare_fore_l = build_limb_loft(
        lm['elbow_L'],
        lm['wrist_L'],
        stations=[
            (0.16, 0.050, 0.052, 0.0, 0.002),
            (0.42, 0.048, 0.050, 0.0, 0.003),
            (0.78, 0.039, 0.041, 0.0, 0.001),
            (1.04, 0.035, 0.038, 0.0, 0.000),
        ],
        segments=8,
        name='Bare_Forearm_L',
        color=c_skin,
        gradient_z=(c_skin_sh, c_skin),
        shade_smooth=True,
        ao_strength=0.22,
        bone='Forearm_L',
    )
    arm_sym = [sl_upper_l, roll_cuff_l, bare_fore_l]
    parts.extend(arm_sym)
    parts.extend(aa.mirror_x(arm_sym))

    parts.extend(build_articulated_hand_pair(
        lm,
        skin_color=c_skin,
        glove_color=c_glove,
        cuff_color=c_glove,
    ))

    # --- D. Heavy Work Trousers & Riveted Knee Reinforcements ---
    parts.append(build_torso_column(
        stations=[
            (0.800, 0.154, 0.106, 0.114, 0.002),
            (0.898, 0.158, 0.112, 0.118, 0.000),
        ],
        segments=9,
        name='Work_Pelvis',
        color=c_pants,
        ao_strength=0.28,
        bone='Pelvis',
    ))
    w_thigh_l = build_limb_loft(
        lm['hip_L'],
        lm['knee_L'],
        stations=[
            (-0.06, 0.082, 0.086, 0.000, 0.000),
            ( 0.35, 0.079, 0.082, 0.002, 0.002),
            ( 0.75, 0.072, 0.074, 0.001, 0.003),
            ( 1.05, 0.066, 0.068, 0.000, 0.003),
        ],
        segments=8,
        fold_freq=4,
        fold_amp=0.0025,
        name='Work_Thigh_L',
        color=c_pants,
        shade_smooth=False,
        ao_strength=0.26,
        bone='Thigh_L',
    )
    w_knee_l = aa.add_bevel_box(
        name='Work_KneePad_L',
        size=(0.086, 0.032, 0.106),
        location=(lm['knee_L'][0], lm['knee_L'][1] - 0.058, lm['knee_z'] + 0.006),
        rotation=(-0.05, 0.0, 0.0),
        bevel=0.006,
        segments=1,
        color=c_knee,
        ao_strength=0.25,
        bone='Shin_L',
    )
    w_shin_l = build_limb_loft(
        lm['knee_L'],
        lm['ankle_L'],
        stations=[
            (-0.06, 0.065, 0.068, 0.0,  0.002),
            ( 0.38, 0.066, 0.070, 0.0, -0.004),
            ( 0.82, 0.062, 0.065, 0.0,  0.000),
            ( 1.02, 0.060, 0.063, 0.0,  0.002),  # Tucked cleanly over steel-toe boot
        ],
        segments=8,
        fold_freq=5,
        fold_amp=0.0025,
        name='Work_Shin_L',
        color=c_pants,
        shade_smooth=False,
        ao_strength=0.26,
    )
    w_legs = [w_thigh_l, w_knee_l, w_shin_l]
    parts.extend(w_legs)
    parts.extend(aa.mirror_x(w_legs))

    # --- E. Rugged Steel-Toe Work Boots (Grounded flat at z = 0.000, toes -Y) ---
    ax = lm['ankle_x']
    b_sole_l = aa.add_extruded_profile(
        [
            ( 0.078, 0.000),
            (-0.236, 0.000),
            (-0.254, 0.014),
            (-0.248, 0.034),
            (-0.080, 0.032),
            ( 0.082, 0.038),
        ],
        depth=0.104,
        axis='X',
        name='WorkBoot_LugSole_L',
        location=(ax, 0.0, 0.0),
        color=c_sole,
        ao_strength=0.24,
    )
    b_upper_l = aa.add_extruded_profile(
        [
            ( 0.074, 0.032),
            (-0.228, 0.028),
            (-0.212, 0.066),
            (-0.120, 0.096),
            (-0.042, 0.168),
            ( 0.066, 0.164),
        ],
        depth=0.094,
        axis='X',
        name='WorkBoot_Leather_L',
        location=(ax, 0.0, 0.0),
        bevel=0.005,
        bevel_segments=1,
        color=c_boot,
        ao_strength=0.28,
    )
    # Brushed steel-toe cap at -Y front tip!
    steel_toe_l = aa.add_extruded_profile(
        [
            (-0.152, 0.028),
            (-0.246, 0.026),
            (-0.248, 0.054),
            (-0.214, 0.074),
            (-0.150, 0.068),
        ],
        depth=0.098,
        axis='X',
        name='Steel_ToeCap_L',
        location=(ax, 0.0, 0.0),
        bevel=0.004,
        bevel_segments=1,
        shade_smooth=True,
        color=c_steel,
        ao_strength=0.22,
    )
    boot_sym = [b_sole_l, b_upper_l, steel_toe_l]
    parts.extend(boot_sym)
    parts.extend(aa.mirror_x(boot_sym))

    # --- Batch, Bake Seam AO, Rig & Export ---
    meshes = aa.combine_by_role(parts)
    body_mesh = next(m for m in meshes if m.name == 'Body')
    apply_character_seam_ao(
        body_mesh,
        seam_zs=[
            (0.948, 0.028, 0.22),  # Under utility vest hem
            (0.892, 0.026, 0.22),  # Under heavy tool belt
            (1.095, 0.025, 0.16),  # Rolled sleeve cuff crease
            (0.168, 0.024, 0.20),  # Trouser hem over steel-toe boot
        ],
        chin_z=lm['head_z'] - 0.012,
        crotch_z=lm['pelvis_z'],
    )

    rig = aa.build_humanoid_rig_and_clips(
        meshes,
        rig_name='StreetRig',
        height=lm['h'],
        style='citizen',
        attack_clip_name='Punch',
        shoulder_width=lm['sh_x'],
        hip_width=lm['hp_x'],
    )
    return aa.export_glb(OUT_DIR / 'mechanic.glb', [rig] + meshes, min_tris=1800, max_meshes=6)


# ---------------------------------------------------------------------------
# 3. Night-Market Vendor / Field Medic — public/street/vendor.glb
# ---------------------------------------------------------------------------

def build_vendor() -> dict:
    """Author public/street/vendor.glb (~1.77 m Night-Market Vendor / Field Medic)."""
    aa.reset_scene()
    lm = rig_landmarks(height=1.732, shoulder_width=0.218, hip_width=0.106)
    parts: list[bpy.types.Object] = []

    # Distinct Neo-Kyoto night-market vendor / field medic palette
    c_skin = (0.80, 0.57, 0.43)
    c_skin_sh = (0.62, 0.41, 0.30)
    c_hair = (0.09, 0.13, 0.20)
    c_hair_hi = (0.17, 0.24, 0.35)
    c_band = (0.88, 0.85, 0.78)
    c_kosode = (0.14, 0.21, 0.34)
    c_apron = (0.80, 0.77, 0.69)
    c_obi = (0.72, 0.15, 0.19)
    c_haori = (0.62, 0.12, 0.20)
    c_haori_hi = (0.76, 0.19, 0.26)
    c_haori_in = (0.24, 0.16, 0.26)
    c_lapel = (0.19, 0.16, 0.24)
    c_gold = (0.82, 0.62, 0.22)
    c_hakama = (0.10, 0.32, 0.32)
    c_hakama_hi = (0.15, 0.43, 0.42)
    c_wrap = (0.14, 0.17, 0.23)
    c_tabi = (0.85, 0.84, 0.80)
    c_wood = (0.18, 0.11, 0.09)
    c_sig_lantern = (1.00, 0.64, 0.12)
    c_sig_medic = (0.08, 0.95, 0.88)

    # --- A. Sculpted Head, Hachimaki Headband & Twisted Topknot Hair ---
    parts.extend(build_sculpted_head(
        lm,
        skin_color=c_skin,
        skin_shadow=c_skin_sh,
        eye_color=(0.06, 0.08, 0.11),
        brow_color=c_hair,
        lip_color=(0.68, 0.40, 0.35),
    ))

    hz = lm['head_z']
    cz = lm['crown_z']
    # Swept Neo-Kyoto hair cap + side face-framing strands
    parts.append(build_open_garment_shell(
        stations=[
            (hz + 0.040, 0.084, 0.052, 0.106, 0.018, 1.10),
            (hz + 0.120, 0.094, 0.084, 0.116, 0.010, 0.88),
            (cz - 0.035, 0.090, 0.098, 0.110, 0.006, 0.44),
            (cz + 0.008, 0.056, 0.064, 0.072, 0.008, 0.16),
        ],
        thickness=0.016,
        arc_segments=9,
        fold_freq=7,
        fold_amp=0.003,
        name='Vendor_Hair_Cap',
        color=c_hair,
        gradient_z=(c_hair, c_hair_hi),
        shade_smooth=False,
        ao_strength=0.28,
        ground_ao=0.0,
        bone='Head',
    ))

    side_lock_l = aa.add_box(
        name='Side_HairLock_L',
        size=(0.022, 0.036, 0.105),
        location=(0.078, -0.064, hz + 0.105),
        rotation=(0.14, 0.08, -0.22),
        taper=(0.65, 0.70),
        color=c_hair,
        bone='Head',
    )
    parts.extend([side_lock_l, aa.mirror_x(side_lock_l)])

    # Twisted topknot bun + crossed brass kanzashi hairpins (peak ~1.775 m, inside 1.75..1.82 m!)
    parts.append(aa.add_torus(
        name='Twist_Hair_Coil',
        major_radius=0.034,
        minor_radius=0.016,
        major_segments=10,
        minor_segments=5,
        location=(0.0, 0.048, cz + 0.012),
        rotation=(-0.48, 0.0, 0.0),
        color=c_hair_hi,
        bone='Head',
    ))
    parts.append(aa.add_sphere(
        name='Twist_Topknot_Core',
        radius=0.034,
        segments=8,
        rings=5,
        location=(0.0, 0.056, cz + 0.022),
        scale=(0.88, 1.05, 0.72),
        color=c_hair,
        bone='Head',
    ))
    for sign, rz in ((1.0, 0.45), (-1.0, -0.45)):
        parts.append(aa.add_cylinder(
            name=f'Kanzashi_Pin_{"L" if sign > 0 else "R"}',
            radius=0.0045,
            depth=0.125,
            vertices=5,
            location=(sign * 0.008, 0.056, cz + 0.020),
            rotation=(0.15, math.pi * 0.36 * sign, rz),
            color=c_gold,
            bone='Head',
        ))

    # Knotted Hachimaki Headband around forehead + crimson medic crest + back tails
    band_z = hz + 0.166
    parts.append(aa.add_torus(
        name='Hachimaki_Band',
        major_radius=0.094,
        minor_radius=0.013,
        major_segments=12,
        minor_segments=5,
        location=(0.0, 0.004, band_z),
        rotation=(0.10, 0.0, 0.0),
        scale=(0.98, 1.12, 1.0),
        color=c_band,
        bone='Head',
    ))
    parts.append(aa.add_box(
        name='Hachimaki_MedicPlaque',
        size=(0.046, 0.014, 0.024),
        location=(0.0, -0.106, band_z + 0.009),
        rotation=(0.10, 0.0, 0.0),
        color=c_obi,
        bone='Head',
    ))
    for sign in (-1.0, 1.0):
        parts.append(aa.add_box(
            name=f'Hachimaki_Tail_{"L" if sign > 0 else "R"}',
            size=(0.018, 0.012, 0.072),
            location=(sign * 0.022, 0.118, band_z - 0.032),
            rotation=(-0.28, sign * 0.28, 0.0),
            taper=(0.55, 0.70),
            color=c_band,
            bone='Head',
        ))

    # --- B. Indigo Kosode Tunic + Field-Medic Apron + Crimson Obi + Neo-Kyoto Haori Coat ---
    parts.append(build_torso_column(
        stations=[
            (0.880, 0.138, 0.098, 0.102, -0.002),
            (1.020, 0.136, 0.096, 0.100, -0.003),
            (1.180, 0.146, 0.104, 0.108, -0.004),
            (1.320, 0.148, 0.104, 0.108, -0.002),
            (1.375, 0.080, 0.074, 0.078,  0.000),
        ],
        segments=9,
        name='Indigo_Kosode',
        color=c_kosode,
        ao_strength=0.25,
    ))

    # Herbal linen bib & lower market/medic apron on front (-Y) + crimson obi sash
    parts.append(aa.add_bevel_box(
        name='Apron_Chest_Bib',
        size=(0.175, 0.022, 0.245),
        location=(0.0, -0.106, 1.125),
        rotation=(0.04, 0.0, 0.0),
        bevel=0.004,
        segments=1,
        taper=(0.86, 1.0),
        color=c_apron,
        ao_strength=0.24,
    ))
    parts.append(build_open_garment_shell(
        stations=[
            (0.930, 0.154, 0.114, 0.116, -0.003, 0.08),
            (1.005, 0.152, 0.112, 0.114, -0.003, 0.08),
        ],
        thickness=0.016,
        arc_segments=9,
        fold_freq=8,
        fold_amp=0.002,
        name='Crimson_Obi_Sash',
        color=c_obi,
        ao_strength=0.24,
    ))
    # Sculpted curved front apron skirt panel (0.68..0.94 m on -Y front, pinned to Pelvis)
    apron_skirt = aa.add_bevel_box(
        name='Apron_Front_Skirt',
        size=(0.236, 0.024, 0.255),
        location=(0.0, -0.118, 0.810),
        rotation=(-0.08, 0.0, 0.0),
        bevel=0.005,
        segments=1,
        taper=(0.86, 1.0),
        color=c_apron,
        ao_strength=0.24,
        bone='Pelvis',
    )
    parts.append(apron_skirt)

    # Divided field-medic pocket + glowing 'Signal' bio-ampoules tucked into front apron (pinned to Pelvis)
    parts.append(aa.add_bevel_box(
        name='Apron_Medic_Pocket',
        size=(0.155, 0.024, 0.085),
        location=(0.0, -0.130, 0.835),
        rotation=(-0.07, 0.0, 0.0),
        bevel=0.003,
        segments=1,
        color=c_band,
        bone='Pelvis',
    ))
    for vx in (-0.036, 0.0, 0.036):
        parts.append(aa.add_cylinder(
            name=f'Signal_Medic_Vial_{vx:+.2f}',
            radius=0.0085,
            depth=0.044,
            vertices=6,
            location=(vx, -0.134, 0.882),
            rotation=(-0.07, 0.0, 0.0),
            color=c_sig_medic,
            signal=True,
            bone='Pelvis',
        ))

    # Stylized Neo-Kyoto Haori Coat (0.73..1.37 m, rx strictly < arm_split_x=0.1788 so 0% hits Hand_L/R!)
    parts.append(build_open_garment_shell(
        stations=[
            (0.730, 0.169, 0.138, 0.152,  0.004, 0.44),
            (0.860, 0.166, 0.130, 0.140,  0.002, 0.36),
            (1.010, 0.162, 0.122, 0.130, -0.002, 0.30),
            (1.185, 0.168, 0.126, 0.132, -0.004, 0.33),
            (1.315, 0.168, 0.120, 0.126, -0.002, 0.38),
            (1.372, 0.110, 0.090, 0.096,  0.002, 0.44),
        ],
        thickness=0.017,
        arc_segments=10,
        fold_freq=7,
        fold_amp=0.0040,
        name='NeoKyoto_Haori_Coat',
        color=c_haori,
        inner_color=c_haori_in,
        gradient_z=(c_haori, c_haori_hi),
        shade_smooth=False,
        ao_strength=0.28,
        ridge_boost=0.13,
    ))

    # Contrasting dark-plum & gold Haori neckband lapels (eri) along front edges (L mirrored to R)
    lapel_l = aa.add_box(
        name='Haori_Lapel_L',
        size=(0.028, 0.022, 0.37),
        location=(0.068, -0.125, 1.175),
        rotation=(0.03, -0.05, -0.08),
        color=c_lapel,
        bone='Chest',
    )
    himotie_l = aa.add_box(
        name='Haori_HimoCord_L',
        size=(0.044, 0.016, 0.014),
        location=(0.036, -0.130, 1.135),
        color=c_gold,
        bone='Chest',
    )
    parts.extend([lapel_l, himotie_l])
    parts.extend(aa.mirror_x([lapel_l, himotie_l]))

    # --- C. Warm Glowing Shoulder Lantern Badge ('Signal', pinned to Clavicle_L!) ---
    lx, ly, lz = (lm['sh_x'] - 0.016, -0.082, lm['sh_z'] + 0.006)
    # Ornate brass shoulder bracket + top cap + bottom tassel ring
    parts.append(aa.add_box(
        name='Lantern_Shoulder_Mount',
        size=(0.072, 0.068, 0.020),
        location=(lx - 0.012, ly + 0.034, lz + 0.042),
        rotation=(0.15, 0.18, -0.20),
        color=c_gold,
        bone='Clavicle_L',
    ))
    parts.append(aa.add_cylinder(
        name='Lantern_TopCap',
        radius=0.035,
        radius_top=0.016,
        depth=0.018,
        vertices=6,
        location=(lx, ly, lz + 0.032),
        color=c_wood,
        bone='Clavicle_L',
    ))
    parts.append(aa.add_cylinder(
        name='Lantern_BotCap',
        radius=0.018,
        radius_top=0.032,
        depth=0.016,
        vertices=6,
        location=(lx, ly, lz - 0.034),
        color=c_wood,
        bone='Clavicle_L',
    ))
    # Warm glowing hexagonal chochin lantern globe ('Signal', pinned to Clavicle_L!)
    parts.append(aa.add_cylinder(
        name='Signal_Shoulder_Lantern',
        radius=0.032,
        radius_top=0.035,
        depth=0.054,
        vertices=6,
        location=(lx, ly, lz - 0.002),
        color=c_sig_lantern,
        signal=True,
        bone='Clavicle_L',
    ))
    parts.append(aa.add_box(
        name='Signal_Lantern_CrestPip',
        size=(0.016, 0.010, 0.022),
        location=(lx, ly - 0.032, lz - 0.002),
        color=c_sig_medic,
        signal=True,
        bone='Clavicle_L',
    ))

    # --- D. Wide Haori Sleeves (sode) + Tapered Bound Forearms + Articulated Hands ---
    sh_in_l = (lm['shoulder_L'][0] - 0.038, 0.0, lm['sh_z'] + 0.014)
    haori_sode_l = build_limb_loft(
        sh_in_l,
        lm['elbow_L'],
        stations=[
            (0.00, 0.068, 0.072, 0.000,  0.000),
            (0.38, 0.068, 0.094, 0.006, -0.018),
            (0.88, 0.072, 0.114, 0.008, -0.028),  # Wide draped haori sleeve drop
            (1.10, 0.066, 0.106, 0.006, -0.026),
        ],
        segments=8,
        fold_freq=4,
        fold_amp=0.004,
        name='Haori_WideSleeve_L',
        color=c_haori,
        gradient_z=(c_haori, c_haori_hi),
        shade_smooth=False,
        ao_strength=0.28,
        ridge_boost=0.13,
        bone='UpperArm_L',
    )
    tekko_fore_l = build_limb_loft(
        lm['elbow_L'],
        lm['wrist_L'],
        stations=[
            (-0.04, 0.048, 0.050, 0.0, 0.0),
            ( 0.45, 0.045, 0.047, 0.0, 0.002),
            ( 0.85, 0.036, 0.038, 0.0, 0.001),
            ( 1.04, 0.034, 0.036, 0.0, 0.0),
        ],
        segments=7,
        fold_freq=6,
        fold_amp=0.002,
        name='Tekko_Forearm_L',
        color=c_wrap,
        shade_smooth=False,
        ao_strength=0.26,
        bone='Forearm_L',
    )
    arm_v_sym = [haori_sode_l, tekko_fore_l]
    parts.extend(arm_v_sym)
    parts.extend(aa.mirror_x(arm_v_sym))

    parts.extend(build_articulated_hand_pair(
        lm,
        skin_color=c_skin,
        glove_color=None,
        cuff_color=c_gold,
    ))

    # --- E. Pleated Wide Hakama-Street Trousers + Bound Shin Wraps (kyahan) ---
    parts.append(build_torso_column(
        stations=[
            (0.790, 0.152, 0.108, 0.116, 0.002),
            (0.935, 0.148, 0.106, 0.112, 0.000),
        ],
        segments=9,
        fold_freq=6,
        fold_amp=0.004,
        name='Hakama_Pelvis',
        color=c_hakama,
        shade_smooth=False,
        ao_strength=0.28,
        bone='Pelvis',
    ))
    h_thigh_l = build_limb_loft(
        lm['hip_L'],
        lm['knee_L'],
        stations=[
            (-0.06, 0.082, 0.090, 0.000, 0.000),
            ( 0.35, 0.096, 0.104, 0.006, 0.002),
            ( 0.78, 0.104, 0.112, 0.008, 0.002),
            ( 1.06, 0.098, 0.106, 0.006, 0.002),
        ],
        segments=10,
        fold_freq=5,
        fold_amp=0.0085,
        name='Hakama_PleatedThigh_L',
        color=c_hakama,
        gradient_z=(c_hakama, c_hakama_hi),
        shade_smooth=False,
        ao_strength=0.28,
        ridge_boost=0.15,
        bone='Thigh_L',
    )
    h_shin_l = build_limb_loft(
        lm['knee_L'],
        lm['ankle_L'],
        stations=[
            (-0.04, 0.096, 0.104, 0.006, 0.002),
            ( 0.36, 0.092, 0.098, 0.004, 0.000),
            ( 0.56, 0.052, 0.056, 0.000, 0.000),  # Gathers crisply into kyahan shin wrap!
        ],
        segments=10,
        fold_freq=5,
        fold_amp=0.007,
        name='Hakama_LowerGather_L',
        color=c_hakama,
        shade_smooth=False,
        ao_strength=0.28,
        ridge_boost=0.14,
        bone='Shin_L',
    )
    kyahan_l = build_limb_loft(
        lm['knee_L'],
        lm['ankle_L'],
        stations=[
            (0.50, 0.050, 0.054, 0.0,  0.000),
            (0.75, 0.046, 0.050, 0.0, -0.002),
            (1.04, 0.042, 0.045, 0.0,  0.000),
        ],
        segments=8,
        fold_freq=6,
        fold_amp=0.002,
        name='Kyahan_ShinWrap_L',
        color=c_wrap,
        shade_smooth=False,
        ao_strength=0.25,
    )
    h_legs = [h_thigh_l, h_shin_l, kyahan_l]
    parts.extend(h_legs)
    parts.extend(aa.mirror_x(h_legs))

    # --- F. Neo-Kyoto Tabi Boots & Elevated Geta Clog Soles (Grounded flat at z = 0.000) ---
    ax = lm['ankle_x']
    # Lacquered wooden geta deck + two traditional grounded cleats (ha) resting on z = 0.000
    geta_profile_yz = [
        ( 0.072, 0.022),
        ( 0.055, 0.022),
        ( 0.052, 0.000),  # Rear geta tooth grounded at z = 0.000
        ( 0.018, 0.000),
        ( 0.014, 0.022),
        (-0.105, 0.022),
        (-0.110, 0.000),  # Front geta tooth grounded at z = 0.000
        (-0.148, 0.000),
        (-0.154, 0.022),
        (-0.238, 0.022),
        (-0.244, 0.042),
        ( 0.074, 0.042),
    ]
    geta_l = aa.add_extruded_profile(
        geta_profile_yz,
        depth=0.098,
        axis='X',
        name='Geta_Platform_L',
        location=(ax, 0.0, 0.0),
        color=c_wood,
        ao_strength=0.26,
    )
    tabi_l = aa.add_extruded_profile(
        [
            ( 0.066, 0.040),
            (-0.228, 0.040),
            (-0.222, 0.066),
            (-0.125, 0.094),
            (-0.040, 0.148),
            ( 0.058, 0.145),
        ],
        depth=0.086,
        axis='X',
        name='Tabi_Boot_L',
        location=(ax, 0.0, 0.0),
        bevel=0.004,
        bevel_segments=1,
        color=c_tabi,
        ao_strength=0.24,
    )
    # Crimson hanao V-strap over tabi instep
    hanao_l = aa.add_box(
        name='Hanao_Strap_L',
        size=(0.092, 0.075, 0.022),
        location=(ax, -0.128, 0.068),
        rotation=(-0.28, 0.0, 0.0),
        color=c_obi,
        ao_strength=0.22,
    )
    clog_sym = [geta_l, tabi_l, hanao_l]
    parts.extend(clog_sym)
    parts.extend(aa.mirror_x(clog_sym))

    # --- Batch, Bake Seam AO, Rig & Export ---
    meshes = aa.combine_by_role(parts)
    body_mesh = next(m for m in meshes if m.name == 'Body')
    apply_character_seam_ao(
        body_mesh,
        seam_zs=[
            (0.930, 0.028, 0.22),  # Under crimson obi sash
            (0.722, 0.028, 0.20),  # Haori coat hem drop shadow
            (0.668, 0.024, 0.18),  # Apron hem shadow onto pleated hakama
            (0.285, 0.026, 0.22),  # Hakama gather into kyahan shin wrap
            (0.145, 0.022, 0.18),  # Kyahan into tabi boot
        ],
        chin_z=lm['head_z'] - 0.012,
        crotch_z=lm['pelvis_z'],
    )

    rig = aa.build_humanoid_rig_and_clips(
        meshes,
        rig_name='StreetRig',
        height=lm['h'],
        style='citizen',
        attack_clip_name='Punch',
        shoulder_width=lm['sh_x'],
        hip_width=lm['hp_x'],
    )
    return aa.export_glb(OUT_DIR / 'vendor.glb', [rig] + meshes, min_tris=1800, max_meshes=6)


# ---------------------------------------------------------------------------
# Strict Self-Verification (Contract + Budget + Height + Palette Diversity)
# ---------------------------------------------------------------------------

def verify_civilian_glb(path: Path) -> dict:
    info = aa.verify_glb_file(path, expect_rig=True, max_meshes=6, min_tris=1800)
    tris = info['triangles']
    assert 1800 <= tris <= 3400, f'{path.name}: triangle count {tris} outside target 1800..3400'

    raw = path.read_bytes()
    json_len = struct.unpack_from('<I', raw, 12)[0]
    g = json.loads(raw[20:20 + json_len].decode('utf-8'))

    mat_names = {m.get('name') for m in g.get('materials', [])}
    assert 'VertexPaint' in mat_names and 'Signal' in mat_names, (
        f'{path.name}: expected both VertexPaint and Signal materials, got {mat_names}'
    )

    min_xyz = [math.inf, math.inf, math.inf]
    max_xyz = [-math.inf, -math.inf, -math.inf]
    for m in g['meshes']:
        for p in m['primitives']:
            acc = g['accessors'][p['attributes']['POSITION']]
            for k in range(3):
                min_xyz[k] = min(min_xyz[k], acc['min'][k])
                max_xyz[k] = max(max_xyz[k], acc['max'][k])

    height_m = max_xyz[1] - min_xyz[1]
    assert abs(min_xyz[1]) <= 0.006, f'{path.name}: soles not grounded at y=0 (min_y={min_xyz[1]:.4f})'
    assert 1.75 <= height_m <= 1.82, f'{path.name}: height {height_m:.4f}m outside 1.75..1.82m'
    assert max_xyz[2] > abs(min_xyz[2]), (
        f'{path.name}: expected forward toes/face toward +Z in Three.js (-Y in Blender), '
        f'got z=[{min_xyz[2]:.3f}, {max_xyz[2]:.3f}]'
    )

    joints = max(len(s.get('joints', [])) for s in g.get('skins', []))
    assert joints >= 18, f'{path.name}: expected >= 18 joints, got {joints}'

    info.update({
        'height_m': round(height_m, 4),
        'min_y': round(min_xyz[1], 4),
        'z_span': (round(min_xyz[2], 3), round(max_xyz[2], 3)),
        'joints': joints,
    })
    return info


def main() -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    builders = [
        ('resident.glb', build_resident),
        ('mechanic.glb', build_mechanic),
        ('vendor.glb', build_vendor),
    ]
    for label, fn in builders:
        fn()
    for label, _fn in builders:
        verified = verify_civilian_glb(OUT_DIR / label)
        print(
            f"[AA-CIVILIAN] {label}: {verified['triangles']} tris, "
            f"height={verified['height_m']}m, meshes={verified['meshes']}, "
            f"joints={verified['joints']}, {verified['size_kb']} KB, "
            f"clips={verified['animations']}",
            flush=True,
        )
    print('ALL_3_AA_CIVILIANS_VERIFIED', flush=True)


if __name__ == '__main__':
    try:
        main()
    except Exception:
        traceback.print_exc()
        sys.exit(1)
