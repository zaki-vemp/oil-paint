"""Sculpt and export six sleek AA cyberpunk vehicle & rider GLBs for Komorebi.

Run headless with:
  .test/blender/blender --background --disable-autoexec --python tools/build_aa_vehicles.py

Outputs in public/street/:
  1. coupe.glb     — Kestrel Cyber-Sedan (6 meshes, 1,800..4,200 tris)
  2. sports.glb    — Kestrel GT-X Hypercar (6 meshes, 1,800..4,200 tris)
  3. van.glb       — Aethelgard Armored Transport Van (6 meshes, 1,800..4,200 tris)
  4. motorbike.glb — Razor Kaneda Superbike (4 meshes, 1,800..4,200 tris)
  5. scooter.glb   — Komorebi Neo-Scooter (4 meshes, 1,800..4,200 tris)
  6. rider.glb     — Seated Cyber-Rider (2 meshes, 900..1,800 tris, NO wheels)

Coordinate & wheel-pivot contract (Blender Z-up -> Three.js Y-up via export_yup=True):
  * Forward in Three.js is +Z, which is -Y in Blender.
  * Ground is Z = 0 in Blender (Y = 0 in Three.js).
  * Body and Signal meshes remain at world origin (0, 0, 0).
  * Each Wheel_* object has its origin (obj.location) at the exact axle center in
    Blender coordinates with local vertices centered around (0, 0, 0).
"""

from __future__ import annotations

import argparse
import json
import math
import struct
import sys
from pathlib import Path
from typing import Sequence

import bmesh
import bpy
from mathutils import Euler, Matrix, Quaternion, Vector

TOOLS_DIR = Path(__file__).resolve().parent
ROOT_DIR = TOOLS_DIR.parent
OUT_DIR = ROOT_DIR / 'public' / 'street'

if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))
sys.dont_write_bytecode = True

import blender_aa_lib as aa

# Exact wheel centers in Blender coordinates (X, Y, Z) & wheel radii (ride.js + prepare-neon-assets.py)
WHEEL_SPECS: dict[str, dict] = {
    'coupe': {
        'radius': 0.39,
        'wheels': [
            ('Wheel_FL', (-0.87, -1.28, 0.39)),
            ('Wheel_FR', (0.87, -1.28, 0.39)),
            ('Wheel_RL', (-0.87, 1.28, 0.39)),
            ('Wheel_RR', (0.87, 1.28, 0.39)),
        ],
    },
    'sports': {
        'radius': 0.39,
        'wheels': [
            ('Wheel_FL', (-0.87, -1.28, 0.39)),
            ('Wheel_FR', (0.87, -1.28, 0.39)),
            ('Wheel_RL', (-0.87, 1.28, 0.39)),
            ('Wheel_RR', (0.87, 1.28, 0.39)),
        ],
    },
    'van': {
        'radius': 0.32,
        'wheels': [
            ('Wheel_FL', (-0.76, -1.04, 0.32)),
            ('Wheel_FR', (0.76, -1.04, 0.32)),
            ('Wheel_RL', (-0.76, 1.03, 0.32)),
            ('Wheel_RR', (0.76, 1.03, 0.32)),
        ],
    },
    'motorbike': {
        'radius': 0.36,
        'wheels': [
            ('Wheel_F', (0.0, -0.91, 0.36)),
            ('Wheel_R', (0.0, 0.88, 0.36)),
        ],
    },
    'scooter': {
        'radius': 0.285,
        'wheels': [
            ('Wheel_F', (0.0, -0.80, 0.285)),
            ('Wheel_R', (0.0, 0.75, 0.285)),
        ],
    },
}

# Bespoke linear-RGB automotive & cyber-street finishes
COL_MIDNIGHT_TEAL = (0.048, 0.265, 0.335)
COL_TEAL_METALLIC = (0.075, 0.395, 0.465)
COL_SLATE_METALLIC = (0.215, 0.268, 0.340)
COL_PEARL_CRIMSON = (0.680, 0.068, 0.125)
COL_KANEDA_RED = (0.760, 0.072, 0.110)
COL_CARBON_BLACK = (0.062, 0.072, 0.094)
COL_GUNMETAL_ARMOR = (0.148, 0.178, 0.228)
COL_INDUSTRIAL_AMBER = (0.890, 0.500, 0.075)
COL_MINT_CYAN = (0.135, 0.665, 0.635)
COL_MINT_DEEP = (0.075, 0.440, 0.445)
COL_CREAM_IVORY = (0.875, 0.845, 0.755)
COL_SEAT_TAN = (0.465, 0.285, 0.175)
COL_CANOPY_GLASS = (0.032, 0.062, 0.105)
COL_GOLD_ALLOY = (0.780, 0.595, 0.215)
COL_SIGNAL_GOLD = (1.000, 0.680, 0.120)
COL_SIGNAL_CYAN = (0.040, 0.910, 1.000)
COL_SIGNAL_CRIMSON = (1.000, 0.055, 0.105)
COL_SIGNAL_WHITE = (0.940, 0.965, 1.000)


# ---------------------------------------------------------------------------
# Closed-Manifold Sculpting Primitives (FrontSide Culling Safe)
# ---------------------------------------------------------------------------

def _signed_volume_bm(bm: bmesh.types.BMesh) -> float:
    bm.normal_update()
    vol = 0.0
    for f in bm.faces:
        if len(f.verts) < 3:
            continue
        v0 = f.verts[0].co
        for i in range(1, len(f.verts) - 1):
            v1 = f.verts[i].co
            v2 = f.verts[i + 1].co
            vol += v0.dot(v1.cross(v2))
    return vol / 6.0


def _finalize_manifold(
    bm: bmesh.types.BMesh,
    name: str,
    location=(0.0, 0.0, 0.0),
    rotation=(0.0, 0.0, 0.0),
    scale=(1.0, 1.0, 1.0),
    bevel: float = 0.0,
    bevel_segments: int = 1,
    shade_smooth: bool = False,
    smooth_angle_cos: float | None = None,
    color=(0.30, 0.35, 0.42),
    signal: bool = False,
    ao_strength: float = 0.25,
    gradient_z=None,
) -> bpy.types.Object:
    """Ensure outward-pointing normals via signed volume before finalizing mesh."""
    bmesh.ops.remove_doubles(bm, verts=bm.verts[:], dist=1e-5)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    if len(bm.faces) >= 4 and _signed_volume_bm(bm) < 0.0:
        bmesh.ops.reverse_faces(bm, faces=bm.faces[:])
        bm.normal_update()

    obj = aa._finalize_primitive(
        bm,
        name=name,
        location=location,
        rotation=rotation,
        scale=scale,
        bevel=bevel,
        bevel_segments=bevel_segments,
        shade_smooth=shade_smooth,
        smooth_angle_cos=smooth_angle_cos,
        color=color,
        signal=signal,
        ao_strength=ao_strength,
    )
    if gradient_z is not None:
        aa.paint_mesh(
            obj,
            color=color,
            signal=signal,
            ao_strength=ao_strength,
            gradient_z=gradient_z,
        )
    return obj


def make_sym_ring(
    y: float,
    half_xz: Sequence[tuple[float, float]],
    keel_z: float | None = None,
    crown_z: float | None = None,
) -> list[tuple[float, float, float]]:
    """Build a closed symmetric cross-section at station Y from +X half-profile [(x, z), ...]."""
    pos = [(max(0.012, float(x)), float(z)) for x, z in half_xz]
    ring: list[tuple[float, float, float]] = []
    if keel_z is not None:
        ring.append((0.0, float(y), float(keel_z)))
    for x, z in pos:
        ring.append((x, float(y), z))
    if crown_z is not None:
        ring.append((0.0, float(y), float(crown_z)))
    for x, z in reversed(pos):
        ring.append((-x, float(y), z))
    return ring


def add_loft(
    rings: Sequence[Sequence[tuple[float, float, float]]],
    name: str = 'Loft',
    color=(0.30, 0.35, 0.42),
    signal: bool = False,
    shade_smooth: bool = False,
    ao_strength: float = 0.25,
    gradient_z=None,
    front_cap: tuple[float, float, float] | None = None,
    rear_cap: tuple[float, float, float] | None = None,
) -> bpy.types.Object:
    """Create a watertight manifold loft across closed cross-section rings."""
    if len(rings) < 2:
        raise ValueError(f'{name}: add_loft needs >= 2 rings')
    m = len(rings[0])
    if m < 3 or any(len(r) != m for r in rings):
        raise ValueError(f'{name}: rings must all have identical vertex count >= 3')

    bm = bmesh.new()
    bm_rings = [[bm.verts.new(pt) for pt in ring] for ring in rings]

    for k in range(len(bm_rings) - 1):
        ra, rb = bm_rings[k], bm_rings[k + 1]
        for i in range(m):
            j = (i + 1) % m
            bm.faces.new((ra[i], ra[j], rb[j], rb[i]))

    if front_cap is None:
        front_cap = (
            sum(p[0] for p in rings[0]) / m,
            sum(p[1] for p in rings[0]) / m,
            sum(p[2] for p in rings[0]) / m,
        )
    vf = bm.verts.new(front_cap)
    for i in range(m):
        j = (i + 1) % m
        bm.faces.new((vf, bm_rings[0][j], bm_rings[0][i]))

    if rear_cap is None:
        rear_cap = (
            sum(p[0] for p in rings[-1]) / m,
            sum(p[1] for p in rings[-1]) / m,
            sum(p[2] for p in rings[-1]) / m,
        )
    vr = bm.verts.new(rear_cap)
    for i in range(m):
        j = (i + 1) % m
        bm.faces.new((bm_rings[-1][i], bm_rings[-1][j], vr))

    return _finalize_manifold(
        bm,
        name=name,
        shade_smooth=shade_smooth,
        color=color,
        signal=signal,
        ao_strength=ao_strength,
        gradient_z=gradient_z,
    )


def add_lathe_x(
    profile_rx: Sequence[tuple[float, float]],
    segments: int = 18,
    center=(0.0, 0.0, 0.0),
    name: str = 'WheelLathe',
    shade_smooth: bool = True,
    color='rubber_tire',
    ao_strength: float = 0.22,
) -> bpy.types.Object:
    """Revolve a closed profile of (radius, local_x) around the X axle at center."""
    segs = max(6, int(segments))
    cx, cy, cz = float(center[0]), float(center[1]), float(center[2])
    pts = [(max(0.0, float(r)), float(lx)) for r, lx in profile_rx]
    if len(pts) < 3:
        raise ValueError('add_lathe_x needs >= 3 profile points')

    bm = bmesh.new()
    rings: list[list[bmesh.types.BMVert]] = []
    start_pole = None
    end_pole = None

    for idx, (rad, lx) in enumerate(pts):
        if idx == 0 and rad <= 1e-5:
            start_pole = bm.verts.new((cx + lx, cy, cz))
            continue
        if idx == len(pts) - 1 and rad <= 1e-5:
            end_pole = bm.verts.new((cx + lx, cy, cz))
            continue
        ring = []
        for i in range(segs):
            ang = (i / segs) * math.tau
            ring.append(bm.verts.new((cx + lx, cy + math.cos(ang) * rad, cz + math.sin(ang) * rad)))
        rings.append(ring)

    if start_pole is None:
        start_pole = bm.verts.new((cx + pts[0][1], cy, cz))
    if end_pole is None:
        end_pole = bm.verts.new((cx + pts[-1][1], cy, cz))

    for i in range(segs):
        j = (i + 1) % segs
        bm.faces.new((start_pole, rings[0][j], rings[0][i]))

    for k in range(len(rings) - 1):
        ra, rb = rings[k], rings[k + 1]
        for i in range(segs):
            j = (i + 1) % segs
            bm.faces.new((ra[i], ra[j], rb[j], rb[i]))

    for i in range(segs):
        j = (i + 1) % segs
        bm.faces.new((rings[-1][i], rings[-1][j], end_pole))

    obj = _finalize_manifold(
        bm,
        name=name,
        shade_smooth=shade_smooth,
        color=color,
        ao_strength=ao_strength,
    )
    # Keep flat shading on axial rim/sidewall faces and smooth shading on circular tread/barrel
    for poly in obj.data.polygons:
        poly.use_smooth = shade_smooth and abs(poly.normal.x) < 0.55
    obj.data.update()
    return obj


def add_tube_sweep(
    points: Sequence[Sequence[float]],
    radius: float = 0.025,
    sides: int = 8,
    radius_end: float | None = None,
    name: str = 'Tube',
    shade_smooth: bool = True,
    color=(0.30, 0.35, 0.42),
    signal: bool = False,
    ao_strength: float = 0.24,
) -> bpy.types.Object:
    """Sweep a closed n-gon along a 3D polyline with clean manifold end caps."""
    pts = [Vector((float(p[0]), float(p[1]), float(p[2]))) for p in points]
    if len(pts) < 2:
        raise ValueError('add_tube_sweep requires >= 2 points')
    n = max(4, int(sides))
    r0 = float(radius)
    r1 = float(radius if radius_end is None else radius_end)

    bm = bmesh.new()
    rings: list[list[bmesh.types.BMVert]] = []
    prev_up = Vector((0.0, 0.0, 1.0))

    for idx, pt in enumerate(pts):
        t_frac = idx / max(1, len(pts) - 1)
        rad = r0 + (r1 - r0) * t_frac
        if idx == 0:
            tangent = (pts[1] - pts[0]).normalized()
        elif idx == len(pts) - 1:
            tangent = (pts[-1] - pts[-2]).normalized()
        else:
            t_in = (pts[idx] - pts[idx - 1]).normalized()
            t_out = (pts[idx + 1] - pts[idx]).normalized()
            tangent = t_in + t_out
            tangent = tangent.normalized() if tangent.length > 1e-5 else t_out

        ref = prev_up if abs(tangent.dot(prev_up)) < 0.92 else Vector((1.0, 0.0, 0.0))
        side_v = tangent.cross(ref).normalized()
        up_v = side_v.cross(tangent).normalized()
        prev_up = up_v

        ring = []
        for i in range(n):
            ang = (i / n) * math.tau
            offset = (side_v * math.cos(ang) + up_v * math.sin(ang)) * rad
            ring.append(bm.verts.new(pt + offset))
        rings.append(ring)

    p_start = bm.verts.new(pts[0])
    p_end = bm.verts.new(pts[-1])

    for i in range(n):
        j = (i + 1) % n
        bm.faces.new((p_start, rings[0][j], rings[0][i]))

    for k in range(len(rings) - 1):
        ra, rb = rings[k], rings[k + 1]
        for i in range(n):
            j = (i + 1) % n
            bm.faces.new((ra[i], ra[j], rb[j], rb[i]))

    for i in range(n):
        j = (i + 1) % n
        bm.faces.new((rings[-1][i], rings[-1][j], p_end))

    return _finalize_manifold(
        bm,
        name=name,
        shade_smooth=shade_smooth,
        color=color,
        signal=signal,
        ao_strength=ao_strength,
    )


def add_fender_flare(
    cy: float,
    cz: float,
    r_inner: float = 0.425,
    arch_thick: float = 0.125,
    x_tub: float = 0.54,
    x_outer: float = 0.99,
    x_top_outer: float = 0.90,
    z_floor: float = 0.16,
    z_inner_root: float | None = None,
    segments: int = 9,
    body_color=COL_MIDNIGHT_TEAL,
    lip_color=None,
    name: str = 'FenderFlare_R',
) -> bpy.types.Object:
    """Build a sculpted +X flared wheel arch with dark obsidian inner wheel-well liner."""
    rings = []
    ang0 = -0.34
    ang1 = math.pi + 0.34
    x_root = max(0.32, x_tub - 0.10)
    for k in range(segments):
        u = k / max(1, segments - 1)
        ang = ang0 + (ang1 - ang0) * u
        ca, sa = math.cos(ang), math.sin(ang)

        yin = cy + ca * r_inner
        zin = max(z_floor, cz + sa * r_inner)

        yout = cy + ca * (r_inner + arch_thick)
        zout = max(z_floor, cz + sa * (r_inner + arch_thick * 1.12))

        ymid = cy + ca * (r_inner + arch_thick * 0.32)
        zmid = max(z_floor, cz + sa * (r_inner + arch_thick * 0.34))

        z_deck_in = zout if z_inner_root is None else min(zout, max(z_floor + 0.04, z_inner_root))
        z_well_in = zin if z_inner_root is None else min(zin, max(z_floor, z_inner_root - 0.06))

        rings.append([
            (x_root, yin * 0.98 + cy * 0.02, z_well_in),
            (x_tub, yin, zin),
            (x_outer - 0.025, yin, zin),
            (x_outer, ymid, zmid),
            (x_top_outer, yout, zout),
            (x_root, yout * 0.98 + cy * 0.02, z_deck_in),
        ])

    obj = add_loft(rings, name=name, color=body_color, shade_smooth=False, ao_strength=0.24)

    # Paint inward wheel-well ceiling obsidian so the arch opening reads deep and shadowed
    def _is_liner(_poly, center, normal):
        radial_y = center.y - cy
        radial_z = center.z - cz
        dot_in = radial_y * normal.y + radial_z * normal.z
        return dot_in < -0.15 and center.x < (x_outer - 0.015)

    aa.paint_faces(obj, _is_liner, 'obsidian', ao_strength=0.10, top_highlight=0.02)

    if lip_color is not None:
        def _is_lip(_poly, center, normal):
            dist = math.hypot(center.y - cy, center.z - cz)
            return center.x > (x_outer - 0.055) and dist < (r_inner + arch_thick * 0.52) and normal.x > 0.35

        aa.paint_faces(obj, _is_lip, lip_color, ao_strength=0.18, top_highlight=0.06)
    return obj


def add_wheelwell_caliper_and_strut(
    cx: float,
    cy: float,
    cz: float,
    radius: float,
    caliper_color=COL_TEAL_METALLIC,
    front: bool = True,
) -> list[bpy.types.Object]:
    """Add suspension strut + painted brake caliper inside wheel well on Body (non-spinning)."""
    sign_x = 1.0 if cx > 0.0 else -1.0
    parts = []
    # Coilover shock strut tucked inside the wheelhouse
    strut_x = sign_x * (abs(cx) - 0.24)
    parts.append(aa.add_cylinder(
        name='Suspension_Strut',
        radius=0.036,
        depth=radius * 0.95,
        vertices=8,
        location=(strut_x, cy, cz + radius * 0.42),
        rotation=(0.0, -sign_x * 0.14, 0.0),
        color='gunmetal',
        shade_smooth=True,
    ))
    # Fixed brake caliper hugging inner/rear edge of rotor (clear of spinning spokes)
    cal_ang = 0.45 if front else -0.45
    cal_r = radius * 0.43
    cal_y = cy + math.cos(cal_ang) * cal_r
    cal_z = cz + math.sin(cal_ang) * cal_r
    cal_x = sign_x * (abs(cx) - 0.055)
    parts.append(aa.add_box(
        name='Brake_Caliper',
        size=(0.065, radius * 0.30, radius * 0.18),
        location=(cal_x, cal_y, cal_z),
        rotation=(cal_ang, 0.0, 0.0),
        color=caliper_color,
    ))
    return parts


def build_cyber_wheel(
    center: tuple[float, float, float],
    radius: float = 0.39,
    width: float = 0.23,
    spokes: int = 6,
    two_sided: bool = False,
    rim_color='silver',
    lip_color='chrome',
    spoke_color=None,
    cap_color='obsidian',
    pinstripe_color=None,
    segments: int = 18,
    spoke_twist: float = 0.0,
) -> list[bpy.types.Object]:
    """Build a crisp, low-draw-cost multi-spoke alloy wheel assembly at center."""
    cx, cy, cz = float(center[0]), float(center[1]), float(center[2])
    hw = float(width) * 0.5
    spoke_col = spoke_color if spoke_color is not None else rim_color
    parts: list[bpy.types.Object] = []

    r_tread = float(radius)
    r_shld = r_tread * 0.955
    r_rim = r_tread * 0.675
    r_lip_in = r_tread * 0.605
    r_rotor = r_tread * 0.465
    r_hub = r_tread * 0.195

    if two_sided:
        # Symmetrical profile for 2-wheelers (both -X and +X faces visible)
        profile = [
            (0.0, -hw * 1.06),
            (r_hub, -hw * 1.06),
            (r_hub * 1.15, -hw * 0.62),
            (r_rotor, -hw * 0.58),
            (r_lip_in, -hw * 0.46),
            (r_rim, -hw * 0.84),
            (r_shld, -hw * 0.94),
            (r_tread, -hw * 0.70),
            (r_tread, hw * 0.70),
            (r_shld, hw * 0.94),
            (r_rim, hw * 0.84),
            (r_lip_in, hw * 0.46),
            (r_rotor, hw * 0.58),
            (r_hub * 1.15, hw * 0.62),
            (r_hub, hw * 1.06),
            (0.0, hw * 1.06),
        ]
        wheel_lathe = add_lathe_x(
            profile,
            segments=segments,
            center=(cx, cy, cz),
            name='Wheel_Body_2W',
            shade_smooth=True,
            color='rubber_tire',
        )
        # Paint alloy rim lips, recessed brake rotors, and hub caps by radial zone
        aa.paint_faces(
            wheel_lathe,
            lambda _p, c, _n: math.hypot(c.y - cy, c.z - cz) < r_rim * 1.01,
            lip_color,
            ao_strength=0.16,
        )
        if pinstripe_color is not None:
            aa.paint_faces(
                wheel_lathe,
                lambda _p, c, _n: r_lip_in * 0.97 <= math.hypot(c.y - cy, c.z - cz) <= r_rim * 1.02,
                pinstripe_color,
                ao_strength=0.05,
                top_highlight=0.04,
            )
        aa.paint_faces(
            wheel_lathe,
            lambda _p, c, _n: r_hub * 1.16 < math.hypot(c.y - cy, c.z - cz) < r_lip_in * 0.97,
            'gunmetal',
            ao_strength=0.22,
        )
        aa.paint_faces(
            wheel_lathe,
            lambda _p, c, _n: math.hypot(c.y - cy, c.z - cz) <= r_hub * 1.16,
            cap_color,
            ao_strength=0.18,
        )
        parts.append(wheel_lathe)

        # Full-thickness sculpted spokes visible cleanly from both left and right
        n_sp = max(3, int(spokes))
        for i in range(n_sp):
            ang = (i / n_sp) * math.tau
            mid_r = (r_hub * 0.85 + r_lip_in * 1.02) * 0.5
            len_r = r_lip_in * 1.04 - r_hub * 0.80
            sy = cy + math.cos(ang) * mid_r
            sz = cz + math.sin(ang) * mid_r
            parts.append(aa.add_box(
                name=f'Spoke2W_{i}',
                size=(width * 0.58, len_r, r_tread * 0.092),
                location=(cx, sy, sz),
                rotation=(ang + spoke_twist, 0.0, 0.0),
                taper=(0.78, 0.85),
                bevel=0.0,
                color=spoke_col,
            ))
    else:
        # 4-wheeler directional outer-dish profile (outward away from vehicle centerline X=0)
        out_sign = 1.0 if cx >= 0.0 else -1.0
        # Build canonical profile with +local_x pointing outward, flip if cx < 0
        raw_prof = [
            (0.0, -hw * 0.90),
            (r_rim * 0.88, -hw * 0.90),
            (r_shld, -hw * 0.95),
            (r_tread, -hw * 0.72),
            (r_tread, hw * 0.72),
            (r_shld, hw * 0.95),
            (r_rim, hw * 0.92),
            (r_lip_in, hw * 0.72),
            (r_rotor, hw * 0.24),
            (r_hub * 1.12, hw * 0.28),
            (r_hub * 0.85, hw * 0.96),
            (0.0, hw * 1.00),
        ]
        if out_sign < 0.0:
            profile = [(r, -lx) for r, lx in reversed(raw_prof)]
        else:
            profile = raw_prof

        wheel_lathe = add_lathe_x(
            profile,
            segments=segments,
            center=(cx, cy, cz),
            name='Wheel_Body_4W',
            shade_smooth=True,
            color='rubber_tire',
        )
        # Paint rim lip, deep dish cavity, rotor, and center lug cap
        aa.paint_faces(
            wheel_lathe,
            lambda _p, c, _n: (
                math.hypot(c.y - cy, c.z - cz) < r_rim * 1.01
                and (c.x - cx) * out_sign > hw * 0.55
            ),
            lip_color,
            ao_strength=0.14,
        )
        aa.paint_faces(
            wheel_lathe,
            lambda _p, c, _n: (
                r_hub * 1.15 < math.hypot(c.y - cy, c.z - cz) < r_lip_in * 0.99
                and (c.x - cx) * out_sign > -hw * 0.20
            ),
            'gunmetal',
            ao_strength=0.25,
        )
        aa.paint_faces(
            wheel_lathe,
            lambda _p, c, _n: (
                math.hypot(c.y - cy, c.z - cz) <= r_hub * 1.15
                and (c.x - cx) * out_sign > 0.0
            ),
            cap_color,
            ao_strength=0.16,
        )
        parts.append(wheel_lathe)

        # Outward concave alloy spokes spanning hub -> outer rim lip
        n_sp = max(3, int(spokes))
        spoke_x = cx + out_sign * (hw * 0.66)
        for i in range(n_sp):
            ang = (i / n_sp) * math.tau
            mid_r = (r_hub * 0.78 + r_lip_in * 1.02) * 0.5
            len_r = r_lip_in * 1.05 - r_hub * 0.72
            sy = cy + math.cos(ang) * mid_r
            sz = cz + math.sin(ang) * mid_r
            parts.append(aa.add_box(
                name=f'Spoke4W_{i}',
                size=(width * 0.19, len_r, r_tread * 0.092),
                location=(spoke_x, sy, sz),
                rotation=(ang + out_sign * spoke_twist, 0.0, -out_sign * 0.10),
                taper=(0.80, 0.85),
                bevel=0.0,
                color=spoke_col,
            ))

    return parts


# ---------------------------------------------------------------------------
# 1. coupe.glb — Kestrel Cyber-Sedan
# ---------------------------------------------------------------------------

def build_coupe() -> list[bpy.types.Object]:
    """Build Kestrel Cyber-Sedan: midnight-teal & slate wedge, dark canopy, cyan/crimson Signal."""
    aa.reset_scene()
    body: list[bpy.types.Object] = []
    sig: list[bpy.types.Object] = []

    # 1. Central sculpted wedge fuselage + cabin greenhouse (-2.12 .. +2.08m)
    # Narrowed across wheel zones (|y|~1.28) so tires sit inside deep dark wheel wells,
    # plunging low along the hood between the front fenders!
    fuselage_stations = [
        (-2.11, [(0.42, 0.16), (0.68, 0.22), (0.72, 0.38), (0.60, 0.49), (0.34, 0.52)], 0.15, 0.53),
        (-1.92, [(0.50, 0.15), (0.86, 0.20), (0.90, 0.44), (0.76, 0.57), (0.44, 0.60)], 0.14, 0.61),
        (-1.66, [(0.52, 0.16), (0.55, 0.34), (0.58, 0.56), (0.56, 0.66), (0.38, 0.68)], 0.16, 0.69),
        (-1.28, [(0.50, 0.17), (0.53, 0.42), (0.56, 0.64), (0.55, 0.73), (0.38, 0.75)], 0.17, 0.76),
        (-0.92, [(0.54, 0.17), (0.86, 0.24), (0.93, 0.62), (0.84, 0.84), (0.58, 0.87)], 0.16, 0.88),
        (-0.36, [(0.56, 0.17), (0.92, 0.24), (0.96, 0.66), (0.86, 0.90), (0.68, 1.29)], 0.16, 1.33),
        (0.28,  [(0.56, 0.17), (0.92, 0.24), (0.96, 0.68), (0.87, 0.92), (0.67, 1.30)], 0.16, 1.34),
        (0.86,  [(0.54, 0.18), (0.86, 0.26), (0.94, 0.68), (0.86, 0.93), (0.63, 1.20)], 0.17, 1.23),
        (1.28,  [(0.50, 0.19), (0.53, 0.46), (0.56, 0.74), (0.56, 0.95), (0.54, 1.07)], 0.18, 1.09),
        (1.70,  [(0.52, 0.20), (0.84, 0.28), (0.94, 0.66), (0.88, 0.92), (0.66, 0.97)], 0.19, 0.98),
        (2.04,  [(0.48, 0.24), (0.84, 0.30), (0.90, 0.62), (0.84, 0.86), (0.60, 0.91)], 0.22, 0.92),
    ]
    rings = [make_sym_ring(y, hpts, keel, crown) for y, hpts, keel, crown in fuselage_stations]
    hull = add_loft(
        rings,
        name='Coupe_Hull',
        color=COL_MIDNIGHT_TEAL,
        ao_strength=0.26,
        front_cap=(0.0, -2.13, 0.33),
        rear_cap=(0.0, 2.08, 0.58),
    )
    # Paint dark wheelhouse inner walls, underbody & lower diffuser obsidian
    aa.paint_faces(
        hull,
        lambda _p, c, n: (
            c.z < 0.24
            or (abs(abs(c.y) - 1.28) < 0.46 and c.z < 0.78 and abs(n.x) > 0.45)
        ),
        'obsidian',
        ao_strength=0.14,
    )
    # Paint dark tinted panoramic cockpit canopy (windshield, side glass, fastback rear glass)
    aa.paint_faces(
        hull,
        lambda _p, c, n: (
            -0.90 < c.y < 1.18
            and c.z > 0.92
            and not (-0.34 < c.y < 0.82 and abs(c.x) < 0.58 and n.z > 0.82)
        ),
        COL_CANOPY_GLASS,
        ao_strength=0.16,
        top_highlight=0.14,
    )
    # Paint metallic slate center hood crease & roof crown panel
    aa.paint_faces(
        hull,
        lambda _p, c, n: (
            (-1.95 < c.y < -0.90 and abs(c.x) < 0.36 and n.z > 0.55)
            or (-0.35 < c.y < 0.85 and abs(c.x) < 0.58 and n.z > 0.82)
        ),
        COL_SLATE_METALLIC,
        ao_strength=0.20,
    )
    body.append(hull)

    # 2. Sculpted +X front & rear pontoon fenders + side aero sill, mirrored across X = 0
    side_parts: list[bpy.types.Object] = []

    side_parts.append(add_fender_flare(
        cy=-1.28,
        cz=0.39,
        r_inner=0.425,
        arch_thick=0.125,
        x_tub=0.52,
        x_outer=0.985,
        x_top_outer=0.86,
        z_floor=0.16,
        z_inner_root=0.56,
        segments=9,
        body_color=COL_MIDNIGHT_TEAL,
        lip_color=COL_SLATE_METALLIC,
        name='Coupe_FrontFender_R',
    ))
    side_parts.append(add_fender_flare(
        cy=1.28,
        cz=0.39,
        r_inner=0.425,
        arch_thick=0.135,
        x_tub=0.52,
        x_outer=0.995,
        x_top_outer=0.88,
        z_floor=0.18,
        z_inner_root=0.78,
        segments=9,
        body_color=COL_MIDNIGHT_TEAL,
        lip_color=COL_SLATE_METALLIC,
        name='Coupe_RearFender_R',
    ))

    # Front nose corner cheek bridging splitter to front fender
    nose_cheek = add_loft(
        [
            [(0.50, -2.09, 0.16), (0.82, -2.06, 0.18), (0.86, -2.05, 0.44), (0.74, -2.04, 0.56), (0.50, -2.06, 0.56)],
            [(0.50, -1.68, 0.16), (0.95, -1.68, 0.18), (0.98, -1.68, 0.52), (0.88, -1.68, 0.72), (0.50, -1.68, 0.72)],
        ],
        name='Coupe_NoseCheek_R',
        color=COL_MIDNIGHT_TEAL,
    )
    side_parts.append(nose_cheek)

    # Rear quarter bumper cheek bridging rear fender to tail
    tail_cheek = add_loft(
        [
            [(0.50, 1.68, 0.19), (0.96, 1.68, 0.22), (0.99, 1.68, 0.58), (0.89, 1.68, 0.88), (0.50, 1.68, 0.88)],
            [(0.50, 2.04, 0.23), (0.86, 2.03, 0.26), (0.90, 2.02, 0.58), (0.82, 2.01, 0.84), (0.50, 2.02, 0.86)],
        ],
        name='Coupe_TailCheek_R',
        color=COL_MIDNIGHT_TEAL,
    )
    side_parts.append(tail_cheek)

    # Sculpted side rocker skirt (slate + carbon blade)
    side_parts.append(aa.add_bevel_box(
        name='Coupe_Rocker_R',
        size=(0.14, 1.74, 0.11),
        location=(0.93, 0.0, 0.20),
        bevel=0.016,
        segments=1,
        color=COL_CARBON_BLACK,
    ))
    # Side intake scoop ahead of rear haunch
    side_parts.append(aa.add_bevel_box(
        name='Coupe_SideIntakeFrame_R',
        size=(0.10, 0.62, 0.28),
        location=(0.94, 0.48, 0.56),
        rotation=(0.0, 0.0, -0.06),
        bevel=0.015,
        segments=1,
        color=COL_SLATE_METALLIC,
    ))
    side_parts.append(aa.add_box(
        name='Coupe_SideIntakeCavity_R',
        size=(0.07, 0.50, 0.19),
        location=(0.965, 0.46, 0.56),
        rotation=(0.0, 0.0, -0.06),
        color='obsidian',
    ))
    # Crisp A-pillar & C-pillar metallic buttress trim + cyber wing mirror
    side_parts.append(add_tube_sweep(
        [(0.80, -0.88, 0.86), (0.69, -0.34, 1.29), (0.68, 0.34, 1.30), (0.76, 1.18, 1.02)],
        radius=0.028,
        sides=6,
        name='Coupe_RoofRail_R',
        color=COL_TEAL_METALLIC,
        shade_smooth=False,
    ))
    side_parts.append(aa.add_bevel_box(
        name='Coupe_Mirror_R',
        size=(0.15, 0.18, 0.065),
        location=(0.95, -0.66, 0.89),
        rotation=(0.0, 0.10, -0.22),
        bevel=0.012,
        segments=1,
        color=COL_SLATE_METALLIC,
    ))

    mirrored_side = aa.mirror_x(side_parts)
    body.extend(side_parts)
    body.extend(mirrored_side)

    # 3. Front carbon splitter, rear Venturi diffuser, lip spoiler, shark-fin antenna & exhausts
    body.append(aa.add_bevel_box(
        name='Coupe_FrontSplitter',
        size=(1.86, 0.34, 0.055),
        location=(0.0, -2.00, 0.145),
        taper=(0.92, 0.88),
        bevel=0.012,
        segments=1,
        color=COL_CARBON_BLACK,
    ))
    body.append(aa.add_box(
        name='Coupe_LowerGrille',
        size=(1.18, 0.10, 0.15),
        location=(0.0, -2.09, 0.27),
        color='obsidian',
    ))
    # Rear ducktail lip spoiler
    body.append(aa.add_bevel_box(
        name='Coupe_RearSpoiler',
        size=(1.68, 0.18, 0.065),
        location=(0.0, 2.02, 0.93),
        rotation=(0.24, 0.0, 0.0),
        bevel=0.012,
        segments=1,
        color=COL_SLATE_METALLIC,
    ))
    # Roof cyber telemetry fin
    body.append(aa.add_extruded_profile(
        [(0.58, 1.31), (0.86, 1.29), (0.80, 1.41), (0.68, 1.38)],
        depth=0.036,
        axis='X',
        name='Coupe_RoofFin',
        color=COL_CARBON_BLACK,
        bevel=0.006,
        bevel_segments=1,
    ))
    # Rear Venturi diffuser + 4 vertical strakes + twin brushed steel exhausts
    body.append(aa.add_bevel_box(
        name='Coupe_DiffuserHousing',
        size=(1.58, 0.22, 0.17),
        location=(0.0, 2.01, 0.24),
        bevel=0.014,
        segments=1,
        color=COL_CARBON_BLACK,
    ))
    for sx in (-0.42, -0.14, 0.14, 0.42):
        body.append(aa.add_box(
            name=f'Coupe_DiffuserFin_{sx:+.2f}',
            size=(0.026, 0.20, 0.15),
            location=(sx, 2.05, 0.22),
            color=COL_SLATE_METALLIC,
        ))
    for sign in (-1.0, 1.0):
        body.append(aa.add_bevel_box(
            name=f'Coupe_Exhaust_{sign:+.0f}',
            size=(0.20, 0.14, 0.085),
            location=(sign * 0.63, 2.06, 0.25),
            bevel=0.012,
            segments=1,
            color='silver',
        ))

    # Wheelhouse struts & teal calipers
    for w_name, w_pos in WHEEL_SPECS['coupe']['wheels']:
        body.extend(add_wheelwell_caliper_and_strut(
            *w_pos, radius=0.39, caliper_color=COL_TEAL_METALLIC, front=w_name.startswith('Wheel_F')
        ))

    # 4. Signal Mesh (Cyan front LED visor lightbar + corner fangs, crimson rear horizon taillight)
    sig.append(aa.add_bevel_box(
        name='Coupe_FrontLightbar',
        size=(1.66, 0.065, 0.048),
        location=(0.0, -2.09, 0.49),
        bevel=0.010,
        segments=1,
        color=COL_SIGNAL_CYAN,
        signal=True,
    ))
    for sign in (-1.0, 1.0):
        sig.append(aa.add_box(
            name=f'Coupe_FrontCornerLED_{sign:+.0f}',
            size=(0.08, 0.24, 0.045),
            location=(sign * 0.84, -1.98, 0.49),
            rotation=(0.0, 0.0, sign * 0.22),
            color=COL_SIGNAL_CYAN,
            signal=True,
        ))
        sig.append(aa.add_box(
            name=f'Coupe_FrontDRLFang_{sign:+.0f}',
            size=(0.16, 0.055, 0.095),
            location=(sign * 0.74, -2.07, 0.30),
            rotation=(0.0, sign * 0.35, 0.0),
            color=COL_SIGNAL_WHITE,
            signal=True,
        ))
        sig.append(aa.add_box(
            name=f'Coupe_SideMarker_{sign:+.0f}',
            size=(0.03, 0.18, 0.025),
            location=(sign * 0.99, -0.92, 0.52),
            color=COL_SIGNAL_CYAN,
            signal=True,
        ))

    # Crimson rear full-width taillight strip + wraparound corner vertical ticks + CHMSL
    sig.append(aa.add_bevel_box(
        name='Coupe_RearTaillight',
        size=(1.72, 0.065, 0.058),
        location=(0.0, 2.06, 0.76),
        bevel=0.010,
        segments=1,
        color=COL_SIGNAL_CRIMSON,
        signal=True,
    ))
    sig.append(aa.add_box(
        name='Coupe_HighBrakeStrip',
        size=(0.92, 0.055, 0.024),
        location=(0.0, 0.86, 1.235),
        rotation=(-0.22, 0.0, 0.0),
        color=COL_SIGNAL_CRIMSON,
        signal=True,
    ))
    for sign in (-1.0, 1.0):
        sig.append(aa.add_box(
            name=f'Coupe_RearCornerTail_{sign:+.0f}',
            size=(0.075, 0.22, 0.09),
            location=(sign * 0.87, 1.98, 0.73),
            color=COL_SIGNAL_CRIMSON,
            signal=True,
        ))

    # 5. Multi-spoke alloy wheels at exact contract centers
    extra_wheels = []
    for w_name, w_center in WHEEL_SPECS['coupe']['wheels']:
        w_parts = build_cyber_wheel(
            center=w_center,
            radius=0.39,
            width=0.23,
            spokes=6,
            two_sided=False,
            rim_color='silver',
            lip_color='chrome',
            spoke_color='silver',
            cap_color=COL_MIDNIGHT_TEAL,
            segments=18,
            spoke_twist=0.12,
        )
        extra_wheels.append((w_name, w_parts, w_center))

    return aa.combine_by_role(body, sig, extra_named=extra_wheels)


# ---------------------------------------------------------------------------
# 2. sports.glb — Kestrel GT-X Hypercar
# ---------------------------------------------------------------------------

def build_sports() -> list[bpy.types.Object]:
    """Build Kestrel GT-X Hypercar: pearl-crimson & carbon widebody, louvers, GT wing, quad exhausts."""
    aa.reset_scene()
    body: list[bpy.types.Object] = []
    sig: list[bpy.types.Object] = []

    # 1. Ultra-low mid-engine hypercar wedge + fighter-jet teardrop canopy (-2.16 .. +2.10m)
    gt_stations = [
        (-2.15, [(0.44, 0.13), (0.72, 0.16), (0.76, 0.31), (0.58, 0.39), (0.30, 0.41)], 0.12, 0.42),
        (-1.92, [(0.52, 0.13), (0.88, 0.17), (0.92, 0.38), (0.74, 0.49), (0.40, 0.51)], 0.12, 0.52),
        (-1.66, [(0.50, 0.14), (0.53, 0.32), (0.55, 0.50), (0.52, 0.59), (0.32, 0.60)], 0.14, 0.57),
        (-1.28, [(0.48, 0.15), (0.52, 0.38), (0.54, 0.58), (0.52, 0.67), (0.34, 0.66)], 0.15, 0.63),
        (-0.88, [(0.54, 0.14), (0.88, 0.19), (0.95, 0.56), (0.82, 0.77), (0.52, 0.80)], 0.14, 0.81),
        (-0.28, [(0.56, 0.14), (0.92, 0.19), (0.96, 0.58), (0.84, 0.82), (0.58, 1.13)], 0.13, 1.16),
        (0.24,  [(0.56, 0.14), (0.90, 0.19), (0.94, 0.60), (0.84, 0.84), (0.56, 1.12)], 0.13, 1.15),
        (0.84,  [(0.54, 0.15), (0.88, 0.21), (0.96, 0.64), (0.86, 0.87), (0.54, 1.02)], 0.14, 1.04),
        (1.28,  [(0.50, 0.16), (0.53, 0.44), (0.55, 0.70), (0.55, 0.88), (0.48, 0.94)], 0.16, 0.95),
        (1.72,  [(0.52, 0.18), (0.88, 0.24), (0.97, 0.62), (0.88, 0.85), (0.58, 0.88)], 0.17, 0.89),
        (2.08,  [(0.48, 0.22), (0.86, 0.28), (0.92, 0.56), (0.84, 0.78), (0.56, 0.82)], 0.20, 0.83),
    ]
    gt_rings = [make_sym_ring(y, hpts, keel, crown) for y, hpts, keel, crown in gt_stations]
    hull = add_loft(
        gt_rings,
        name='Sports_Hull',
        color=COL_PEARL_CRIMSON,
        ao_strength=0.26,
        front_cap=(0.0, -2.18, 0.26),
        rear_cap=(0.0, 2.11, 0.52),
    )
    # Dark obsidian undertray & inner wheelhouse cavities
    aa.paint_faces(
        hull,
        lambda _p, c, n: (
            c.z < 0.21
            or (abs(abs(c.y) - 1.28) < 0.46 and c.z < 0.78 and abs(n.x) > 0.45)
        ),
        'obsidian',
        ao_strength=0.14,
    )
    # Teardrop wrap-around fighter canopy glass
    aa.paint_faces(
        hull,
        lambda _p, c, n: (
            -0.88 < c.y < 0.48
            and c.z > 0.81
            and not (-0.28 < c.y < 0.36 and abs(c.x) < 0.44 and n.z > 0.84)
        ),
        COL_CANOPY_GLASS,
        ao_strength=0.15,
        top_highlight=0.16,
    )
    # Carbon-black recessed front aero hood channel, roof spine & rear fastback engine bay deck
    aa.paint_faces(
        hull,
        lambda _p, c, n: (
            (-1.90 < c.y < -0.92 and abs(c.x) < 0.34 and n.z > 0.45)
            or (-0.30 < c.y < 1.80 and abs(c.x) < 0.45 and c.z > 0.84 and n.z > 0.55)
        ),
        COL_CARBON_BLACK,
        ao_strength=0.20,
    )
    body.append(hull)

    # 2. Widebody sculpted +X fenders, deep mid-engine side scoops, flying buttresses & side blades
    side: list[bpy.types.Object] = []

    side.append(add_fender_flare(
        cy=-1.28,
        cz=0.39,
        r_inner=0.425,
        arch_thick=0.135,
        x_tub=0.50,
        x_outer=1.025,
        x_top_outer=0.88,
        z_floor=0.13,
        z_inner_root=0.48,
        segments=9,
        body_color=COL_PEARL_CRIMSON,
        lip_color=COL_CARBON_BLACK,
        name='Sports_FrontFender_R',
    ))
    side.append(add_fender_flare(
        cy=1.28,
        cz=0.39,
        r_inner=0.425,
        arch_thick=0.150,
        x_tub=0.50,
        x_outer=1.040,
        x_top_outer=0.90,
        z_floor=0.15,
        z_inner_root=0.74,
        segments=9,
        body_color=COL_PEARL_CRIMSON,
        lip_color=COL_CARBON_BLACK,
        name='Sports_RearFender_R',
    ))

    # Nose cheek + front dive-plane canard
    side.append(add_loft(
        [
            [(0.48, -2.14, 0.13), (0.86, -2.10, 0.15), (0.90, -2.08, 0.38), (0.76, -2.06, 0.48), (0.48, -2.08, 0.48)],
            [(0.48, -1.68, 0.14), (0.99, -1.68, 0.16), (1.02, -1.68, 0.48), (0.90, -1.68, 0.68), (0.48, -1.68, 0.68)],
        ],
        name='Sports_NoseCheek_R',
        color=COL_PEARL_CRIMSON,
    ))
    side.append(aa.add_box(
        name='Sports_FrontCanard_R',
        size=(0.14, 0.28, 0.025),
        location=(0.95, -1.96, 0.24),
        rotation=(0.22, -0.18, -0.18),
        color=COL_CARBON_BLACK,
    ))

    # Rear widebody quarter cheek
    side.append(add_loft(
        [
            [(0.48, 1.68, 0.16), (1.00, 1.68, 0.20), (1.03, 1.68, 0.56), (0.91, 1.68, 0.84), (0.48, 1.68, 0.84)],
            [(0.48, 2.08, 0.21), (0.90, 2.06, 0.25), (0.93, 2.05, 0.55), (0.84, 2.04, 0.79), (0.48, 2.05, 0.80)],
        ],
        name='Sports_TailCheek_R',
        color=COL_PEARL_CRIMSON,
    ))

    # Aggressive carbon side-skirt aero blade + vertical fin ahead of rear wheel
    side.append(aa.add_bevel_box(
        name='Sports_SideBlade_R',
        size=(0.20, 1.76, 0.065),
        location=(0.96, 0.0, 0.155),
        bevel=0.010,
        segments=1,
        color=COL_CARBON_BLACK,
    ))
    side.append(aa.add_extruded_profile(
        [(0.64, 0.15), (0.84, 0.15), (0.82, 0.42), (0.72, 0.34)],
        depth=0.028,
        axis='X',
        location=(1.03, 0.0, 0.0),
        name='Sports_SkirtFin_R',
        color=COL_CARBON_BLACK,
    ))

    # Deep mid-engine triangular side air scoop
    side.append(aa.add_bevel_box(
        name='Sports_SideScoopCowling_R',
        size=(0.13, 0.78, 0.36),
        location=(0.96, 0.42, 0.54),
        taper=(0.78, 0.86),
        rotation=(0.0, 0.0, -0.08),
        bevel=0.015,
        segments=1,
        color=COL_PEARL_CRIMSON,
    ))
    side.append(aa.add_box(
        name='Sports_SideScoopMouth_R',
        size=(0.09, 0.62, 0.26),
        location=(0.985, 0.38, 0.54),
        rotation=(0.0, 0.0, -0.08),
        color='obsidian',
    ))

    # Flying C-pillar aero buttress sweeping from canopy crown to rear haunch
    side.append(add_tube_sweep(
        [(0.55, 0.22, 1.11), (0.68, 0.82, 1.00), (0.82, 1.44, 0.88)],
        radius=0.046,
        radius_end=0.062,
        sides=6,
        name='Sports_Buttress_R',
        color=COL_PEARL_CRIMSON,
        shade_smooth=False,
    ))
    # Stalk aero mirror
    side.append(aa.add_bevel_box(
        name='Sports_Mirror_R',
        size=(0.16, 0.16, 0.050),
        location=(0.96, -0.64, 0.81),
        rotation=(0.0, 0.14, -0.26),
        bevel=0.009,
        segments=1,
        color=COL_CARBON_BLACK,
    ))

    body.extend(side)
    body.extend(aa.mirror_x(side))

    # 3. Low jutting carbon front splitter, louvered fastback engine cover, raised GT wing & quad exhausts
    body.append(aa.add_bevel_box(
        name='Sports_FrontSplitter',
        size=(1.98, 0.44, 0.048),
        location=(0.0, -2.04, 0.115),
        taper=(0.90, 0.86),
        bevel=0.010,
        segments=1,
        color=COL_CARBON_BLACK,
    ))
    # Roof snorkel air-scoop
    body.append(aa.add_bevel_box(
        name='Sports_RoofScoop',
        size=(0.24, 0.46, 0.095),
        location=(0.0, 0.12, 1.18),
        taper=(0.75, 0.82),
        bevel=0.012,
        segments=1,
        color=COL_CARBON_BLACK,
    ))

    # 5 angled carbon louvers over the mid-engine fastback spine
    for idx in range(5):
        ly = 0.56 + idx * 0.21
        lz = 1.08 - idx * 0.042
        lw = 0.66 - idx * 0.032
        body.append(aa.add_box(
            name=f'Sports_EngineLouver_{idx}',
            size=(lw, 0.13, 0.028),
            location=(0.0, ly, lz),
            rotation=(-0.28, 0.0, 0.0),
            taper=(0.94, 0.85),
            color=COL_CARBON_BLACK,
        ))

    # Raised swan-neck GT rear wing (pylons + cambered airfoil + angled GT endplates)
    for sign in (-1.0, 1.0):
        body.append(aa.add_extruded_profile(
            [(1.58, 0.86), (1.82, 0.84), (1.94, 1.25), (1.76, 1.26)],
            depth=0.036,
            axis='X',
            location=(sign * 0.42, 0.0, 0.0),
            name=f'Sports_WingPylon_{sign:+.0f}',
            color=COL_CARBON_BLACK,
            bevel=0.005,
            bevel_segments=1,
        ))
        body.append(aa.add_extruded_profile(
            [(1.68, 1.12), (2.14, 1.08), (2.16, 1.31), (1.74, 1.29)],
            depth=0.028,
            axis='X',
            location=(sign * 0.96, 0.0, 0.0),
            name=f'Sports_WingEndplate_{sign:+.0f}',
            color=COL_PEARL_CRIMSON,
            bevel=0.005,
            bevel_segments=1,
        ))
    body.append(aa.add_bevel_box(
        name='Sports_GTWingBlade',
        size=(1.94, 0.30, 0.046),
        location=(0.0, 1.88, 1.24),
        rotation=(0.16, 0.0, 0.0),
        bevel=0.010,
        segments=1,
        color=COL_CARBON_BLACK,
    ))

    # Aggressive 5-strake carbon diffuser + dual quad-exhaust barrels (4 barrels total)
    body.append(aa.add_bevel_box(
        name='Sports_RearDiffuser',
        size=(1.74, 0.26, 0.20),
        location=(0.0, 2.03, 0.23),
        bevel=0.014,
        segments=1,
        color=COL_CARBON_BLACK,
    ))
    for sx in (-0.64, -0.26, 0.0, 0.26, 0.64):
        body.append(aa.add_box(
            name=f'Sports_DiffuserStrake_{sx:+.2f}',
            size=(0.026, 0.24, 0.19),
            location=(sx, 2.08, 0.21),
            color='obsidian',
        ))
    for sign in (-1.0, 1.0):
        for dx in (0.39, 0.52):
            pipe = aa.add_cylinder(
                name=f'Sports_QuadExhaust_{sign:+.0f}_{dx:.2f}',
                radius=0.048,
                radius_top=0.044,
                depth=0.16,
                vertices=10,
                location=(sign * dx, 2.09, 0.32),
                rotation=(math.pi * 0.5, 0.0, 0.0),
                color=COL_GOLD_ALLOY,
            )
            aa.paint_faces(pipe, lambda _p, c, n: n.y > 0.6 and c.y > 2.14, 'obsidian', ao_strength=0.1)
            body.append(pipe)

    # Gold/crimson performance brake calipers
    for w_name, w_pos in WHEEL_SPECS['sports']['wheels']:
        body.extend(add_wheelwell_caliper_and_strut(
            *w_pos, radius=0.39, caliper_color=COL_GOLD_ALLOY, front=w_name.startswith('Wheel_F')
        ))

    # 4. Signal Mesh (Cyan laser chevron headlights + gold aero splitter/skirt/wing strips + crimson taillights)
    # Gold front splitter lip & nose emblem
    sig.append(aa.add_box(
        name='Sports_SplitterGoldStrip',
        size=(1.68, 0.045, 0.022),
        location=(0.0, -2.23, 0.125),
        color=COL_SIGNAL_GOLD,
        signal=True,
    ))
    for sign in (-1.0, 1.0):
        # Angular cyan laser headlight slashes
        sig.append(aa.add_box(
            name=f'Sports_HeadlightOuter_{sign:+.0f}',
            size=(0.34, 0.22, 0.042),
            location=(sign * 0.72, -1.96, 0.44),
            rotation=(0.14, -sign * 0.16, sign * 0.36),
            color=COL_SIGNAL_CYAN,
            signal=True,
        ))
        sig.append(aa.add_box(
            name=f'Sports_HeadlightInner_{sign:+.0f}',
            size=(0.20, 0.08, 0.036),
            location=(sign * 0.46, -2.08, 0.39),
            rotation=(0.0, 0.0, sign * 0.18),
            color=COL_SIGNAL_WHITE,
            signal=True,
        ))
        # Gold aero side-skirt edge strip + cyan side-scoop accent + gold wing endplate slash
        sig.append(aa.add_box(
            name=f'Sports_SideGoldStrip_{sign:+.0f}',
            size=(0.028, 1.62, 0.022),
            location=(sign * 1.045, 0.0, 0.16),
            color=COL_SIGNAL_GOLD,
            signal=True,
        ))
        sig.append(aa.add_box(
            name=f'Sports_ScoopCyanSlash_{sign:+.0f}',
            size=(0.030, 0.045, 0.24),
            location=(sign * 1.01, 0.08, 0.54),
            rotation=(0.24, 0.0, 0.0),
            color=COL_SIGNAL_CYAN,
            signal=True,
        ))
        sig.append(aa.add_box(
            name=f'Sports_WingGoldTip_{sign:+.0f}',
            size=(0.034, 0.34, 0.026),
            location=(sign * 0.97, 1.94, 1.29),
            color=COL_SIGNAL_GOLD,
            signal=True,
        ))

    # Twin-tier crimson cyber taillight blades + central gold F1 rain/telemetry lamp
    sig.append(aa.add_bevel_box(
        name='Sports_RearTailUpper',
        size=(1.80, 0.055, 0.042),
        location=(0.0, 2.09, 0.74),
        bevel=0.008,
        segments=1,
        color=COL_SIGNAL_CRIMSON,
        signal=True,
    ))
    for sign in (-1.0, 1.0):
        sig.append(aa.add_box(
            name=f'Sports_RearTailLower_{sign:+.0f}',
            size=(0.48, 0.050, 0.034),
            location=(sign * 0.62, 2.09, 0.65),
            color=COL_SIGNAL_CRIMSON,
            signal=True,
        ))
    sig.append(aa.add_box(
        name='Sports_DiffuserBeacon',
        size=(0.09, 0.045, 0.08),
        location=(0.0, 2.11, 0.26),
        color=COL_SIGNAL_GOLD,
        signal=True,
    ))

    # 5. Forged carbon & gold-lip 7-spoke hypercar wheels
    extra_wheels = []
    for w_name, w_center in WHEEL_SPECS['sports']['wheels']:
        w_parts = build_cyber_wheel(
            center=w_center,
            radius=0.39,
            width=0.25,
            spokes=7,
            two_sided=False,
            rim_color=COL_CARBON_BLACK,
            lip_color=COL_GOLD_ALLOY,
            spoke_color=COL_GOLD_ALLOY,
            cap_color=COL_PEARL_CRIMSON,
            segments=16,
            spoke_twist=0.18,
        )
        extra_wheels.append((w_name, w_parts, w_center))

    return aa.combine_by_role(body, sig, extra_named=extra_wheels)


# ---------------------------------------------------------------------------
# 3. van.glb — Aethelgard Armored Transport Van
# ---------------------------------------------------------------------------

def build_van() -> list[bpy.types.Object]:
    """Build Aethelgard Armored Transport Van: gunmetal & amber, bullbar, roof beacon, cargo ribs, twin doors."""
    aa.reset_scene()
    body: list[bpy.types.Object] = []
    sig: list[bpy.types.Object] = []

    # 1. Brutalist cab-forward armored transport hull (-1.66 .. +1.68m, Z up to 1.66m)
    van_stations = [
        (-1.65, [(0.44, 0.20), (0.74, 0.24), (0.80, 0.56), (0.76, 0.88), (0.62, 0.96)], 0.19, 0.98),
        (-1.38, [(0.48, 0.18), (0.78, 0.24), (0.84, 0.68), (0.81, 1.06), (0.68, 1.38)], 0.18, 1.42),
        (-1.04, [(0.48, 0.18), (0.50, 0.42), (0.54, 0.72), (0.84, 1.12), (0.72, 1.58)], 0.18, 1.63),
        (-0.62, [(0.52, 0.18), (0.83, 0.22), (0.87, 0.76), (0.85, 1.26), (0.74, 1.62)], 0.17, 1.66),
        (0.20,  [(0.52, 0.18), (0.83, 0.22), (0.87, 0.76), (0.85, 1.28), (0.74, 1.62)], 0.17, 1.66),
        (0.62,  [(0.52, 0.18), (0.83, 0.22), (0.87, 0.76), (0.85, 1.28), (0.74, 1.62)], 0.17, 1.66),
        (1.03,  [(0.48, 0.18), (0.50, 0.42), (0.54, 0.72), (0.85, 1.24), (0.74, 1.61)], 0.18, 1.65),
        (1.44,  [(0.50, 0.19), (0.82, 0.23), (0.86, 0.76), (0.84, 1.26), (0.73, 1.60)], 0.18, 1.64),
        (1.67,  [(0.48, 0.21), (0.79, 0.25), (0.83, 0.76), (0.80, 1.24), (0.70, 1.56)], 0.20, 1.59),
    ]
    van_rings = [make_sym_ring(y, hpts, keel, crown) for y, hpts, keel, crown in van_stations]
    hull = add_loft(
        van_rings,
        name='Van_Hull',
        color=COL_GUNMETAL_ARMOR,
        ao_strength=0.27,
        front_cap=(0.0, -1.67, 0.62),
        rear_cap=(0.0, 1.69, 0.92),
    )
    # Dark obsidian wheelhouse walls & chassis belly
    aa.paint_faces(
        hull,
        lambda _p, c, n: (
            c.z < 0.23
            or ((abs(c.y + 1.04) < 0.40 or abs(c.y - 1.03) < 0.40) and c.z < 0.70 and abs(n.x) > 0.40)
        ),
        'obsidian',
        ao_strength=0.14,
    )
    # Recessed armored dark glass windshield & side cab windows
    aa.paint_faces(
        hull,
        lambda _p, c, n: (
            (c.y < -0.95 and 0.98 < c.z < 1.46 and n.y < -0.35)
            or (-1.28 < c.y < -0.62 and 1.08 < c.z < 1.48 and abs(n.x) > 0.45)
        ),
        COL_CANOPY_GLASS,
        ao_strength=0.16,
    )
    # Industrial-amber flank livery band across mid-beltline
    aa.paint_faces(
        hull,
        lambda _p, c, n: -0.65 < c.y < 1.55 and 0.70 < c.z < 1.02 and abs(n.x) > 0.55,
        COL_INDUSTRIAL_AMBER,
        ao_strength=0.22,
    )
    # Slate roof armor deck
    aa.paint_faces(
        hull,
        lambda _p, c, n: c.z > 1.54 and n.z > 0.65,
        COL_SLATE_METALLIC,
        ao_strength=0.18,
    )
    body.append(hull)

    # Recessed dark armored windshield inset + center divider mullion + sun visor brow
    body.append(aa.add_bevel_box(
        name='Van_WindshieldPane',
        size=(1.32, 0.06, 0.44),
        location=(0.0, -1.49, 1.22),
        rotation=(-0.56, 0.0, 0.0),
        bevel=0.010,
        segments=1,
        color='obsidian',
    ))
    body.append(aa.add_bevel_box(
        name='Van_WindshieldMullion',
        size=(0.070, 0.09, 0.46),
        location=(0.0, -1.51, 1.22),
        rotation=(-0.56, 0.0, 0.0),
        bevel=0.010,
        segments=1,
        color=COL_GUNMETAL_ARMOR,
    ))
    body.append(aa.add_bevel_box(
        name='Van_SunVisorBrow',
        size=(1.46, 0.20, 0.075),
        location=(0.0, -1.18, 1.50),
        rotation=(0.20, 0.0, 0.0),
        bevel=0.012,
        segments=1,
        color=COL_INDUSTRIAL_AMBER,
    ))

    # 2. Heavy armored wheel arches, side cargo ribs & armored step rails (+X mirrored)
    v_side: list[bpy.types.Object] = []

    v_side.append(add_fender_flare(
        cy=-1.04,
        cz=0.32,
        r_inner=0.365,
        arch_thick=0.115,
        x_tub=0.48,
        x_outer=0.895,
        x_top_outer=0.84,
        z_floor=0.17,
        segments=7,
        body_color=COL_CARBON_BLACK,
        lip_color=COL_INDUSTRIAL_AMBER,
        name='Van_FrontArch_R',
    ))
    v_side.append(add_fender_flare(
        cy=1.03,
        cz=0.32,
        r_inner=0.365,
        arch_thick=0.115,
        x_tub=0.48,
        x_outer=0.895,
        x_top_outer=0.84,
        z_floor=0.17,
        segments=7,
        body_color=COL_CARBON_BLACK,
        lip_color=COL_INDUSTRIAL_AMBER,
        name='Van_RearArch_R',
    ))

    # Heavy side step rail between axles
    v_side.append(aa.add_bevel_box(
        name='Van_SideStep_R',
        size=(0.12, 1.22, 0.085),
        location=(0.86, 0.0, 0.20),
        bevel=0.012,
        segments=1,
        color=COL_CARBON_BLACK,
    ))

    # 5 stamped upper armored cargo ribs above the industrial-amber beltline
    for rib_idx, ry in enumerate((-0.44, -0.06, 0.32, 0.70, 1.18)):
        v_side.append(aa.add_box(
            name=f'Van_CargoRib_R_{rib_idx}',
            size=(0.036, 0.075, 0.46),
            location=(0.855, ry, 1.28),
            taper=(0.75, 0.88),
            color=COL_SLATE_METALLIC,
        ))
    # Side cab armored viewport glass
    v_side.append(aa.add_box(
        name='Van_SideCabWindow_R',
        size=(0.035, 0.50, 0.32),
        location=(0.835, -0.92, 1.26),
        rotation=(0.0, -0.18, 0.0),
        color='obsidian',
    ))

    # Sliding door rail & industrial mirror on dual arms
    v_side.append(aa.add_box(
        name='Van_DoorTrack_R',
        size=(0.045, 1.48, 0.042),
        location=(0.87, 0.38, 1.06),
        color='obsidian',
    ))
    v_side.append(aa.add_bevel_box(
        name='Van_Mirror_R',
        size=(0.11, 0.14, 0.24),
        location=(0.93, -1.04, 1.22),
        bevel=0.012,
        segments=1,
        color=COL_CARBON_BLACK,
    ))

    body.extend(v_side)
    body.extend(aa.mirror_x(v_side))

    # 3. Heavy Bullbar Bumper, Skid Plate, Roof HVAC + Beacon Mount & Rear Twin Doors
    body.append(aa.add_bevel_box(
        name='Van_FrontBumper',
        size=(1.72, 0.24, 0.24),
        location=(0.0, -1.66, 0.30),
        bevel=0.018,
        segments=1,
        color=COL_CARBON_BLACK,
    ))
    body.append(aa.add_bevel_box(
        name='Van_SkidPlate',
        size=(1.12, 0.28, 0.14),
        location=(0.0, -1.68, 0.20),
        rotation=(0.42, 0.0, 0.0),
        bevel=0.012,
        segments=1,
        color='steel',
    ))
    # Tubular armored bullbar hoop + twin vertical uprights
    body.append(add_tube_sweep(
        [(-0.70, -1.66, 0.28), (-0.66, -1.78, 0.36), (-0.54, -1.80, 0.68), (0.54, -1.80, 0.68), (0.66, -1.78, 0.36), (0.70, -1.66, 0.28)],
        radius=0.036,
        sides=7,
        name='Van_BullbarHoop',
        color='obsidian',
    ))
    for sign in (-1.0, 1.0):
        body.append(aa.add_bevel_box(
            name=f'Van_BullbarPost_{sign:+.0f}',
            size=(0.065, 0.12, 0.48),
            location=(sign * 0.38, -1.76, 0.46),
            bevel=0.010,
            segments=1,
            color=COL_INDUSTRIAL_AMBER,
        ))

    # Roof armored HVAC pod + beacon bar pedestal
    body.append(aa.add_bevel_box(
        name='Van_RoofBeaconBase',
        size=(1.18, 0.24, 0.065),
        location=(0.0, -0.78, 1.67),
        bevel=0.012,
        segments=1,
        color=COL_CARBON_BLACK,
    ))
    body.append(aa.add_bevel_box(
        name='Van_RoofHVACPod',
        size=(0.96, 1.18, 0.11),
        location=(0.0, 0.42, 1.69),
        taper=(0.88, 0.90),
        bevel=0.018,
        segments=1,
        color=COL_SLATE_METALLIC,
    ))

    # Rear twin armored doors, central seam, heavy hinges, lock bar & rear step bumper
    for sign in (-1.0, 1.0):
        body.append(aa.add_bevel_box(
            name=f'Van_RearDoorPanel_{sign:+.0f}',
            size=(0.64, 0.065, 1.16),
            location=(sign * 0.35, 1.68, 0.92),
            bevel=0.012,
            segments=1,
            color=COL_SLATE_METALLIC,
        ))
        body.append(aa.add_bevel_box(
            name=f'Van_RearDoorWindow_{sign:+.0f}',
            size=(0.44, 0.075, 0.32),
            location=(sign * 0.35, 1.685, 1.24),
            bevel=0.008,
            segments=1,
            color=COL_CANOPY_GLASS,
        ))
        for hz in (0.54, 1.30):
            body.append(aa.add_box(
                name=f'Van_DoorHinge_{sign:+.0f}_{hz:.2f}',
                size=(0.08, 0.08, 0.06),
                location=(sign * 0.68, 1.69, hz),
                color=COL_INDUSTRIAL_AMBER,
            ))
    body.append(aa.add_box(
        name='Van_RearDoorLockBar',
        size=(0.055, 0.085, 1.10),
        location=(0.04, 1.70, 0.92),
        color='obsidian',
    ))
    body.append(aa.add_bevel_box(
        name='Van_RearStepBumper',
        size=(1.70, 0.22, 0.16),
        location=(0.0, 1.70, 0.25),
        bevel=0.014,
        segments=1,
        color=COL_CARBON_BLACK,
    ))

    # Heavy-duty wheelhouse struts & amber calipers
    for w_name, w_pos in WHEEL_SPECS['van']['wheels']:
        body.extend(add_wheelwell_caliper_and_strut(
            *w_pos, radius=0.32, caliper_color=COL_INDUSTRIAL_AMBER, front=w_name.startswith('Wheel_F')
        ))

    # 4. Signal Mesh (Roof beacon bar, twin armored headlights, perimeter markers, rear twin-door taillights)
    # Roof beacon bar (industrial amber outer pods + tactical cyan center strobe)
    for sign in (-1.0, 1.0):
        sig.append(aa.add_bevel_box(
            name=f'Van_RoofBeaconAmber_{sign:+.0f}',
            size=(0.42, 0.18, 0.085),
            location=(sign * 0.34, -0.78, 1.74),
            bevel=0.014,
            segments=1,
            color='neon_amber',
            signal=True,
        ))
    sig.append(aa.add_bevel_box(
        name='Van_RoofBeaconCenter',
        size=(0.22, 0.18, 0.085),
        location=(0.0, -0.78, 1.74),
        bevel=0.012,
        segments=1,
        color=COL_SIGNAL_CYAN,
        signal=True,
    ))

    # Front projector bars & bullbar fog lights + rear vertical taillight stacks
    for sign in (-1.0, 1.0):
        sig.append(aa.add_bevel_box(
            name=f'Van_Headlight_{sign:+.0f}',
            size=(0.34, 0.065, 0.085),
            location=(sign * 0.56, -1.65, 0.68),
            bevel=0.010,
            segments=1,
            color=COL_SIGNAL_WHITE,
            signal=True,
        ))
        sig.append(aa.add_box(
            name=f'Van_FogAmber_{sign:+.0f}',
            size=(0.20, 0.055, 0.055),
            location=(sign * 0.56, -1.75, 0.34),
            color='neon_amber',
            signal=True,
        ))
        sig.append(aa.add_box(
            name=f'Van_SideRoofMarker_{sign:+.0f}',
            size=(0.032, 0.24, 0.036),
            location=(sign * 0.78, 0.90, 1.60),
            color='neon_amber',
            signal=True,
        ))
        sig.append(aa.add_bevel_box(
            name=f'Van_RearTaillight_{sign:+.0f}',
            size=(0.095, 0.065, 0.42),
            location=(sign * 0.76, 1.68, 0.78),
            bevel=0.010,
            segments=1,
            color=COL_SIGNAL_CRIMSON,
            signal=True,
        ))
        sig.append(aa.add_box(
            name=f'Van_RearHazard_{sign:+.0f}',
            size=(0.095, 0.065, 0.12),
            location=(sign * 0.76, 1.68, 1.08),
            color='neon_amber',
            signal=True,
        ))

    # 5. Heavy-duty 6-lug armored steel/gunmetal wheels
    extra_wheels = []
    for w_name, w_center in WHEEL_SPECS['van']['wheels']:
        w_parts = build_cyber_wheel(
            center=w_center,
            radius=0.32,
            width=0.22,
            spokes=6,
            two_sided=False,
            rim_color=COL_GUNMETAL_ARMOR,
            lip_color='steel',
            spoke_color='steel',
            cap_color=COL_INDUSTRIAL_AMBER,
            segments=16,
            spoke_twist=0.0,
        )
        extra_wheels.append((w_name, w_parts, w_center))

    return aa.combine_by_role(body, sig, extra_named=extra_wheels)


# ---------------------------------------------------------------------------
# 4. motorbike.glb — Razor Kaneda Superbike
# ---------------------------------------------------------------------------

def build_motorbike() -> list[bpy.types.Object]:
    """Build Razor Kaneda Superbike: crimson-and-obsidian aero fairing, cyan hub-rings & headlight slash."""
    aa.reset_scene()
    body: list[bpy.types.Object] = []
    sig: list[bpy.types.Object] = []

    # 1. Iconic sculpted Kaneda nose prow, raked bubble windscreen & upper aero cowl
    # Kept above Z=0.73 forward of Y=-0.43 so steered/scaled Wheel_F never clips the belly!
    cowl_stations = [
        (-1.28, [(0.05, 0.77), (0.12, 0.79), (0.11, 0.86), (0.06, 0.89)], 0.76, 0.90),
        (-1.08, [(0.09, 0.74), (0.22, 0.78), (0.24, 0.89), (0.15, 0.96)], 0.73, 0.98),
        (-0.80, [(0.12, 0.74), (0.27, 0.79), (0.28, 0.94), (0.19, 1.07)], 0.73, 1.10),
        (-0.50, [(0.14, 0.72), (0.28, 0.78), (0.26, 0.95), (0.17, 1.13)], 0.70, 1.16),
        (-0.34, [(0.13, 0.68), (0.23, 0.76), (0.21, 0.92), (0.13, 1.00)], 0.66, 1.02),
    ]
    cowl = add_loft(
        [make_sym_ring(y, hpts, keel, crown) for y, hpts, keel, crown in cowl_stations],
        name='Bike_FrontCowl',
        color=COL_KANEDA_RED,
        ao_strength=0.25,
        front_cap=(0.0, -1.31, 0.83),
        rear_cap=(0.0, -0.32, 0.84),
    )
    # Paint raked aero bubble windscreen dark tinted glass
    aa.paint_faces(
        cowl,
        lambda _p, c, n: -1.06 < c.y < -0.44 and c.z > 0.94 and n.z > 0.30,
        COL_CANOPY_GLASS,
        ao_strength=0.14,
        top_highlight=0.16,
    )
    body.append(cowl)

    # Front aero mudguard hugging over the top of Wheel_F
    body.append(aa.add_bevel_box(
        name='Bike_FrontFender',
        size=(0.21, 0.42, 0.085),
        location=(0.0, -0.92, 0.76),
        rotation=(0.18, 0.0, 0.0),
        taper=(0.82, 0.88),
        bevel=0.014,
        segments=1,
        color=COL_KANEDA_RED,
    ))

    # 2. Central cyber-powerplant belly core + sculpted fuel tank + bucket saddle + high tail cowl
    # Belly core strictly inside Y = [-0.42 .. +0.42] to keep full wheel clearance in ride.js
    belly_stations = [
        (-0.42, [(0.08, 0.24), (0.21, 0.30), (0.26, 0.56), (0.22, 0.78)], 0.22, 0.82),
        (-0.18, [(0.09, 0.22), (0.22, 0.28), (0.27, 0.58), (0.19, 0.86)], 0.20, 0.90),
        (0.06,  [(0.09, 0.24), (0.20, 0.30), (0.23, 0.56), (0.17, 0.70)], 0.22, 0.70),
        (0.36,  [(0.08, 0.32), (0.18, 0.40), (0.21, 0.62), (0.18, 0.72)], 0.30, 0.71),
    ]
    belly = add_loft(
        [make_sym_ring(y, hpts, keel, crown) for y, hpts, keel, crown in belly_stations],
        name='Bike_CenterChassis',
        color=COL_CARBON_BLACK,
        ao_strength=0.28,
    )
    # Paint sculpted upper tank spine & side fairing cheeks crimson
    aa.paint_faces(
        belly,
        lambda _p, c, n: (
            (c.y < 0.02 and c.z > 0.64)
            or (abs(c.x) > 0.19 and 0.34 < c.z < 0.74 and c.y < 0.05)
        ),
        COL_KANEDA_RED,
        ao_strength=0.22,
    )
    body.append(belly)

    # Sculpted rider bucket saddle (Z=0.69 matching rider.glb!) + Kaneda high backrest cowl
    body.append(aa.add_bevel_box(
        name='Bike_RiderSaddle',
        size=(0.30, 0.36, 0.085),
        location=(0.0, 0.20, 0.66),
        taper=(0.88, 0.92),
        bevel=0.018,
        segments=1,
        color='obsidian',
    ))

    tail_stations = [
        (0.36, [(0.12, 0.66), (0.22, 0.72), (0.20, 0.88), (0.13, 0.94)], 0.64, 0.95),
        (0.68, [(0.11, 0.74), (0.22, 0.79), (0.19, 0.93), (0.11, 0.98)], 0.73, 0.99),
        (1.02, [(0.08, 0.76), (0.16, 0.81), (0.14, 0.91), (0.08, 0.94)], 0.75, 0.95),
        (1.22, [(0.05, 0.79), (0.10, 0.83), (0.09, 0.88), (0.05, 0.90)], 0.78, 0.91),
    ]
    tail = add_loft(
        [make_sym_ring(y, hpts, keel, crown) for y, hpts, keel, crown in tail_stations],
        name='Bike_TailCowl',
        color=COL_KANEDA_RED,
        ao_strength=0.24,
        rear_cap=(0.0, 1.25, 0.85),
    )
    # Dark backrest lumbar pad facing rider
    aa.paint_faces(tail, lambda _p, c, n: c.y < 0.44 and n.y < -0.5, 'obsidian', ao_strength=0.18)
    body.append(tail)

    # 3. Mirrored +X Fork Shrouds, Rear Swingarm, Twin Upswept Exhausts, Clip-on Bars & Pegs
    b_side: list[bpy.types.Object] = []

    # Wide sculpted front aero fork blade (X=0.145 clears steered Wheel_F cleanly!)
    b_side.append(add_tube_sweep(
        [(0.15, -0.52, 0.86), (0.145, -0.74, 0.60), (0.14, -0.91, 0.36)],
        radius=0.044,
        radius_end=0.036,
        sides=6,
        name='Bike_FrontFork_R',
        color=COL_KANEDA_RED,
        shade_smooth=False,
    ))
    # Beefy rear swingarm beam to rear axle (0, +0.88, 0.36)
    b_side.append(add_tube_sweep(
        [(0.14, 0.24, 0.44), (0.145, 0.56, 0.40), (0.14, 0.88, 0.36)],
        radius=0.048,
        radius_end=0.038,
        sides=6,
        name='Bike_Swingarm_R',
        color=COL_GUNMETAL_ARMOR,
        shade_smooth=False,
    ))
    # Exposed sculpted copper/gunmetal engine stator cover integrated into lower fairing
    b_side.append(aa.add_cylinder(
        name='Bike_EnginePod_R',
        radius=0.095,
        depth=0.075,
        vertices=10,
        location=(0.215, -0.12, 0.41),
        rotation=(0.0, math.pi * 0.5, 0.0),
        bevel=0.010,
        color='copper',
    ))
    # Twin upswept titanium/gunmetal exhaust cannon with copper tip
    exhaust = add_tube_sweep(
        [(0.15, 0.08, 0.28), (0.17, 0.46, 0.42), (0.175, 0.86, 0.72), (0.165, 1.08, 0.81)],
        radius=0.034,
        radius_end=0.046,
        sides=8,
        name='Bike_Exhaust_R',
        color=COL_SLATE_METALLIC,
    )
    aa.paint_faces(exhaust, lambda _p, c, _n: c.y > 0.96, 'copper', ao_strength=0.16)
    b_side.append(exhaust)

    # Clip-on handlebar + grip right at (±0.25, -0.45, 0.94) matching rider.glb hands!
    b_side.append(add_tube_sweep(
        [(0.09, -0.48, 0.96), (0.18, -0.47, 0.95), (0.29, -0.44, 0.93)],
        radius=0.018,
        sides=6,
        name='Bike_Handlebar_R',
        color='obsidian',
    ))
    # Rear-set alloy footpeg at (±0.18, +0.05, 0.44) matching rider.glb boots!
    b_side.append(aa.add_cylinder(
        name='Bike_Footpeg_R',
        radius=0.018,
        depth=0.09,
        vertices=8,
        location=(0.185, 0.05, 0.44),
        rotation=(0.0, math.pi * 0.5, 0.0),
        color='silver',
    ))

    body.extend(b_side)
    body.extend(aa.mirror_x(b_side))

    # Cockpit digital instrument pod
    body.append(aa.add_bevel_box(
        name='Bike_DashPod',
        size=(0.18, 0.10, 0.06),
        location=(0.0, -0.56, 0.98),
        rotation=(-0.48, 0.0, 0.0),
        bevel=0.008,
        segments=1,
        color='obsidian',
    ))

    # 4. Signal Mesh (Twin headlight slash, glowing cyan hub-rings, HUD screen & crimson tail blade)
    for sign in (-1.0, 1.0):
        # Twin predatory headlight slash across nose prow
        sig.append(aa.add_box(
            name=f'Bike_HeadlightSlash_{sign:+.0f}',
            size=(0.13, 0.24, 0.034),
            location=(sign * 0.105, -1.19, 0.825),
            rotation=(0.22, -sign * 0.20, sign * 0.38),
            color=COL_SIGNAL_CYAN,
            signal=True,
        ))
        # Fairing cyber aero energy strip
        sig.append(aa.add_box(
            name=f'Bike_FairingStrip_{sign:+.0f}',
            size=(0.024, 0.44, 0.026),
            location=(sign * 0.275, -0.68, 0.83),
            rotation=(0.18, 0.0, 0.0),
            color=COL_SIGNAL_CYAN,
            signal=True,
        ))
        # Iconic glowing cyan stator hub-rings on front fork & rear swingarm carriers
        for tag, hy in (('Front', -0.91), ('Rear', 0.88)):
            sig.append(aa.add_torus(
                name=f'Bike_HubRing_{tag}_{sign:+.0f}',
                major_radius=0.145,
                minor_radius=0.015,
                major_segments=14,
                minor_segments=5,
                location=(sign * 0.132, hy, 0.36),
                rotation=(0.0, math.pi * 0.5, 0.0),
                color=COL_SIGNAL_CYAN,
                signal=True,
            ))

    # Digital HUD screen glow + razor crimson tail-light strip
    sig.append(aa.add_box(
        name='Bike_DashScreen',
        size=(0.14, 0.02, 0.045),
        location=(0.0, -0.54, 0.99),
        rotation=(-0.48, 0.0, 0.0),
        color=COL_SIGNAL_CYAN,
        signal=True,
    ))
    sig.append(aa.add_bevel_box(
        name='Bike_TailLight',
        size=(0.17, 0.06, 0.042),
        location=(0.0, 1.23, 0.86),
        bevel=0.008,
        segments=1,
        color=COL_SIGNAL_CRIMSON,
        signal=True,
    ))

    # 5. Kaneda aero-disc + 5-spoke wheels with painted cyan rim pinstripes
    w_front = build_cyber_wheel(
        center=(0.0, -0.91, 0.36),
        radius=0.36,
        width=0.16,
        spokes=5,
        two_sided=True,
        rim_color=COL_KANEDA_RED,
        lip_color=COL_KANEDA_RED,
        spoke_color=COL_CARBON_BLACK,
        cap_color='obsidian',
        pinstripe_color=COL_SIGNAL_CYAN,
        segments=20,
        spoke_twist=0.14,
    )
    w_rear = build_cyber_wheel(
        center=(0.0, 0.88, 0.36),
        radius=0.36,
        width=0.20,
        spokes=5,
        two_sided=True,
        rim_color=COL_KANEDA_RED,
        lip_color=COL_KANEDA_RED,
        spoke_color=COL_CARBON_BLACK,
        cap_color='obsidian',
        pinstripe_color=COL_SIGNAL_CYAN,
        segments=20,
        spoke_twist=0.14,
    )

    return aa.combine_by_role(
        body,
        sig,
        extra_named=[
            ('Wheel_F', w_front, (0.0, -0.91, 0.36)),
            ('Wheel_R', w_rear, (0.0, 0.88, 0.36)),
        ],
    )


# ---------------------------------------------------------------------------
# 5. scooter.glb — Komorebi Neo-Scooter
# ---------------------------------------------------------------------------

def build_scooter() -> list[bpy.types.Object]:
    """Build Komorebi Neo-Scooter: mint-cyan & cream Neo-Tokyo urban scooter, halo headlamp, parcel case."""
    aa.reset_scene()
    body: list[bpy.types.Object] = []
    sig: list[bpy.types.Object] = []

    # 1. Sculpted Mint-Cyan & Cream Front Leg Shield & Upper Apron
    # Positioned above Z=0.58 forward of Y=-0.30 so compressed Wheel_F in ride.js clears cleanly!
    shield_stations = [
        (-0.64, [(0.08, 0.62), (0.19, 0.66), (0.21, 0.86), (0.15, 0.99)], 0.60, 1.01),
        (-0.52, [(0.12, 0.58), (0.24, 0.62), (0.25, 0.84), (0.19, 1.03)], 0.56, 1.05),
        (-0.36, [(0.14, 0.32), (0.23, 0.36), (0.24, 0.78), (0.17, 0.98)], 0.30, 1.00),
        (-0.26, [(0.14, 0.28), (0.21, 0.31), (0.18, 0.62), (0.11, 0.86)], 0.26, 0.88),
    ]
    shield = add_loft(
        [make_sym_ring(y, hpts, keel, crown) for y, hpts, keel, crown in shield_stations],
        name='Scooter_LegShield',
        color=COL_MINT_CYAN,
        ao_strength=0.24,
        front_cap=(0.0, -0.66, 0.82),
        rear_cap=(0.0, -0.24, 0.52),
    )
    # Paint inner leg apron & center front necktie trim cream-ivory
    aa.paint_faces(
        shield,
        lambda _p, c, n: (
            (n.y > 0.35 and c.y > -0.42)
            or (abs(c.x) < 0.085 and n.y < -0.35)
        ),
        COL_CREAM_IVORY,
        ao_strength=0.18,
    )
    body.append(shield)

    # Rounded mint-cyan front mudguard over Wheel_F
    body.append(aa.add_bevel_box(
        name='Scooter_FrontFender',
        size=(0.20, 0.38, 0.09),
        location=(0.0, -0.80, 0.61),
        rotation=(0.14, 0.0, 0.0),
        taper=(0.84, 0.88),
        bevel=0.020,
        segments=2,
        color=COL_MINT_CYAN,
    ))

    # 2. Step-Through Floorboard + Sculpted Neo-Tokyo Rear Pod Bodywork
    # Floorboard kept in Y=[-0.29 .. +0.26] so front & rear wheels never intersect it!
    deck = aa.add_bevel_box(
        name='Scooter_StepDeck',
        size=(0.42, 0.56, 0.075),
        location=(0.0, -0.02, 0.29),
        bevel=0.018,
        segments=1,
        color=COL_MINT_DEEP,
    )
    aa.paint_faces(deck, lambda _p, _c, n: n.z > 0.7, COL_CARBON_BLACK, ao_strength=0.16)
    body.append(deck)

    rear_stations = [
        (0.04, [(0.13, 0.30), (0.21, 0.36), (0.22, 0.58), (0.17, 0.66)], 0.28, 0.67),
        (0.26, [(0.14, 0.34), (0.25, 0.44), (0.26, 0.62), (0.19, 0.68)], 0.32, 0.69),
        (0.54, [(0.12, 0.58), (0.24, 0.61), (0.25, 0.68), (0.18, 0.73)], 0.58, 0.74),
        (0.84, [(0.10, 0.59), (0.20, 0.62), (0.21, 0.69), (0.15, 0.74)], 0.59, 0.75),
        (1.02, [(0.07, 0.61), (0.14, 0.64), (0.14, 0.70), (0.09, 0.73)], 0.61, 0.74),
    ]
    rear_pod = add_loft(
        [make_sym_ring(y, hpts, keel, crown) for y, hpts, keel, crown in rear_stations],
        name='Scooter_RearBody',
        color=COL_MINT_CYAN,
        ao_strength=0.24,
        rear_cap=(0.0, 1.05, 0.67),
    )
    # Cream side flashes along rear cowls
    aa.paint_faces(
        rear_pod,
        lambda _p, c, n: c.y > 0.22 and 0.58 < c.z < 0.69 and abs(n.x) > 0.55,
        COL_CREAM_IVORY,
        ao_strength=0.18,
    )
    body.append(rear_pod)

    # Plush two-tone cream/tan cushioned saddle (Z top ~ 0.70m matching rider.glb!)
    saddle = aa.add_bevel_box(
        name='Scooter_Saddle',
        size=(0.31, 0.48, 0.095),
        location=(0.0, 0.24, 0.67),
        taper=(0.90, 0.94),
        bevel=0.024,
        segments=2,
        color=COL_SEAT_TAN,
    )
    aa.paint_faces(saddle, lambda _p, c, _n: c.z < 0.66, COL_CREAM_IVORY, ao_strength=0.16)
    body.append(saddle)

    # 3. Handlebar Cowl, Smoked Flyscreen, Mirrors, Forks, Foot Pads & Rear Parcel Case
    body.append(aa.add_bevel_box(
        name='Scooter_HeadsetCowl',
        size=(0.28, 0.18, 0.14),
        location=(0.0, -0.52, 0.96),
        bevel=0.022,
        segments=2,
        color=COL_CREAM_IVORY,
    ))
    body.append(aa.add_bevel_box(
        name='Scooter_Flyscreen',
        size=(0.24, 0.06, 0.14),
        location=(0.0, -0.58, 1.07),
        rotation=(-0.36, 0.0, 0.0),
        bevel=0.010,
        segments=1,
        color=COL_CANOPY_GLASS,
    ))

    # Rear tubular luggage rack + stylish chamfered Cream & Mint Parcel Case (Y = 0.54..0.92, clear of rider back!)
    body.append(add_tube_sweep(
        [(-0.14, 0.48, 0.71), (-0.14, 0.88, 0.74), (0.14, 0.88, 0.74), (0.14, 0.48, 0.71)],
        radius=0.016,
        sides=6,
        name='Scooter_LuggageRack',
        color='silver',
    ))
    parcel = aa.add_bevel_box(
        name='Scooter_ParcelCase',
        size=(0.38, 0.36, 0.28),
        location=(0.0, 0.72, 0.89),
        taper=(0.92, 0.92),
        bevel=0.026,
        segments=2,
        color=COL_CREAM_IVORY,
    )
    aa.paint_faces(parcel, lambda _p, c, _n: c.z > 0.92, COL_MINT_CYAN, ao_strength=0.20)
    aa.paint_faces(parcel, lambda _p, c, _n: 0.88 < c.z < 0.92, COL_CARBON_BLACK, ao_strength=0.15)
    body.append(parcel)

    s_side: list[bpy.types.Object] = []
    # Front fork legs & rear drivetrain/swingarm case
    s_side.append(add_tube_sweep(
        [(0.10, -0.60, 0.64), (0.105, -0.80, 0.285)],
        radius=0.026,
        sides=6,
        name='Scooter_Fork_R',
        color='silver',
    ))
    s_side.append(add_tube_sweep(
        [(0.11, 0.24, 0.34), (0.115, 0.75, 0.285)],
        radius=0.038,
        radius_end=0.030,
        sides=6,
        name='Scooter_CVTCase_R',
        color=COL_GUNMETAL_ARMOR,
    ))
    # Grips at (±0.25, -0.45, 0.95) + round retro-cyber stalk mirror + raised side foot pad at (±0.17, 0.05, 0.43)
    s_side.append(add_tube_sweep(
        [(0.12, -0.50, 0.96), (0.20, -0.47, 0.96), (0.28, -0.45, 0.95)],
        radius=0.017,
        sides=6,
        name='Scooter_Grip_R',
        color=COL_CARBON_BLACK,
    ))
    s_side.append(add_tube_sweep(
        [(0.18, -0.48, 0.98), (0.25, -0.49, 1.11)],
        radius=0.009,
        sides=5,
        name='Scooter_MirrorStalk_R',
        color='chrome',
    ))
    s_side.append(aa.add_cylinder(
        name='Scooter_MirrorHead_R',
        radius=0.048,
        depth=0.026,
        vertices=12,
        location=(0.26, -0.49, 1.13),
        rotation=(math.pi * 0.5, 0.0, -0.20),
        color=COL_CREAM_IVORY,
    ))
    s_side.append(aa.add_bevel_box(
        name='Scooter_FootPad_R',
        size=(0.08, 0.14, 0.035),
        location=(0.175, 0.05, 0.43),
        bevel=0.008,
        segments=1,
        color=COL_CARBON_BLACK,
    ))

    body.extend(s_side)
    body.extend(aa.mirror_x(s_side))

    # 4. Signal Mesh (Round Halo LED Headlamp in forward bezel, amber apron blinkers, parcel status & taillight)
    body.append(aa.add_cylinder(
        name='Scooter_HeadlampBezel',
        radius=0.108,
        depth=0.065,
        vertices=16,
        location=(0.0, -0.65, 0.90),
        rotation=(math.pi * 0.5, 0.0, 0.0),
        bevel=0.010,
        color=COL_CREAM_IVORY,
    ))
    sig.append(aa.add_torus(
        name='Scooter_HaloHeadlampRing',
        major_radius=0.088,
        minor_radius=0.018,
        major_segments=18,
        minor_segments=6,
        location=(0.0, -0.688, 0.90),
        rotation=(math.pi * 0.5, 0.0, 0.0),
        color=COL_SIGNAL_CYAN,
        signal=True,
    ))
    sig.append(aa.add_cylinder(
        name='Scooter_HaloCore',
        radius=0.054,
        depth=0.025,
        vertices=14,
        location=(0.0, -0.682, 0.90),
        rotation=(math.pi * 0.5, 0.0, 0.0),
        color=COL_SIGNAL_WHITE,
        signal=True,
    ))
    for sign in (-1.0, 1.0):
        sig.append(aa.add_bevel_box(
            name=f'Scooter_FrontBlinker_{sign:+.0f}',
            size=(0.060, 0.045, 0.095),
            location=(sign * 0.175, -0.625, 0.76),
            rotation=(0.0, 0.0, sign * 0.28),
            bevel=0.010,
            segments=1,
            color='neon_amber',
            signal=True,
        ))
    sig.append(aa.add_bevel_box(
        name='Scooter_RearTaillight',
        size=(0.18, 0.05, 0.055),
        location=(0.0, 1.04, 0.68),
        bevel=0.010,
        segments=1,
        color=COL_SIGNAL_CRIMSON,
        signal=True,
    ))
    sig.append(aa.add_box(
        name='Scooter_ParcelBeaconStrip',
        size=(0.26, 0.03, 0.026),
        location=(0.0, 0.905, 0.90),
        color=COL_SIGNAL_CYAN,
        signal=True,
    ))

    # 5. Cream & silver 5-spoke urban scooter wheels
    w_front = build_cyber_wheel(
        center=(0.0, -0.80, 0.285),
        radius=0.285,
        width=0.14,
        spokes=5,
        two_sided=True,
        rim_color=COL_CREAM_IVORY,
        lip_color='silver',
        spoke_color=COL_CREAM_IVORY,
        cap_color=COL_MINT_DEEP,
        segments=18,
    )
    w_rear = build_cyber_wheel(
        center=(0.0, 0.75, 0.285),
        radius=0.285,
        width=0.15,
        spokes=5,
        two_sided=True,
        rim_color=COL_CREAM_IVORY,
        lip_color='silver',
        spoke_color=COL_CREAM_IVORY,
        cap_color=COL_MINT_DEEP,
        segments=18,
    )

    return aa.combine_by_role(
        body,
        sig,
        extra_named=[
            ('Wheel_F', w_front, (0.0, -0.80, 0.285)),
            ('Wheel_R', w_rear, (0.0, 0.75, 0.285)),
        ],
    )


# ---------------------------------------------------------------------------
# 6. rider.glb — Seated Cyber-Rider
# ---------------------------------------------------------------------------

def build_rider() -> list[bpy.types.Object]:
    """Build Seated Cyber-Rider (900..1,800 tris, Body + Signal = 2 meshes, NO wheels).

    Positioned in tucked riding posture (Z ~ 0.45..1.38m, hands at Y ~ -0.45, feet on pegs Y ~ 0.05)
    matching both motorbike.glb and scooter.glb.
    """
    aa.reset_scene()
    body: list[bpy.types.Object] = []
    sig: list[bpy.types.Object] = []

    # 1. Seated Pelvis & Aerodynamic Forward-Tucked Armored Jacket Torso
    torso_stations = [
        (0.22,  0.69, [(0.08, -0.05), (0.145, -0.01), (0.150, 0.06), (0.09, 0.09)]),
        (0.14,  0.80, [(0.08, -0.07), (0.140, -0.02), (0.145, 0.07), (0.09, 0.10)]),
        (0.02,  0.94, [(0.09, -0.09), (0.175, -0.03), (0.180, 0.08), (0.11, 0.13)]),
        (-0.09, 1.06, [(0.09, -0.08), (0.185, -0.02), (0.175, 0.07), (0.10, 0.11)]),
        (-0.14, 1.13, [(0.06, -0.04), (0.095, -0.01), (0.095, 0.04), (0.06, 0.06)]),
    ]
    # Pitch cross-sections perpendicular to the ~38 deg forward torso lean
    lean_rot = Euler((-0.66, 0.0, 0.0), 'XYZ').to_matrix()
    torso_rings = []
    for cy, cz, hpts in torso_stations:
        origin = Vector((0.0, cy, cz))
        ring = []
        # Keel (chest sternum)
        p_keel = origin + lean_rot @ Vector((0.0, hpts[0][1] - 0.015, 0.0))
        ring.append((p_keel.x, p_keel.y, p_keel.z))
        for x, dy_local in hpts:
            p = origin + lean_rot @ Vector((x, dy_local, 0.0))
            ring.append((p.x, p.y, p.z))
        # Crown (spine speed-hump)
        p_crown = origin + lean_rot @ Vector((0.0, hpts[-1][1] + 0.022, 0.0))
        ring.append((p_crown.x, p_crown.y, p_crown.z))
        for x, dy_local in reversed(hpts):
            p = origin + lean_rot @ Vector((-x, dy_local, 0.0))
            ring.append((p.x, p.y, p.z))
        torso_rings.append(ring)

    jacket = add_loft(
        torso_rings,
        name='Rider_TorsoJacket',
        color='navy_coat',
        ao_strength=0.26,
    )
    # Paint pelvis/riding pants dark obsidian, jacket upper shoulders & speed-hump crimson/teal accents
    aa.paint_faces(jacket, lambda _p, c, _n: c.z < 0.78, 'obsidian', ao_strength=0.20)
    aa.paint_faces(
        jacket,
        lambda _p, c, n: c.z > 0.90 and (n.z > 0.45 or abs(c.x) > 0.14),
        COL_PEARL_CRIMSON,
        ao_strength=0.22,
    )
    body.append(jacket)

    # Popped cyber-jacket collar + dorsal aero spine protector hump
    body.append(aa.add_bevel_box(
        name='Rider_CyberCollar',
        size=(0.23, 0.18, 0.085),
        location=(0.0, -0.11, 1.12),
        rotation=(-0.45, 0.0, 0.0),
        bevel=0.015,
        segments=1,
        color=COL_CARBON_BLACK,
    ))
    body.append(aa.add_bevel_box(
        name='Rider_AeroSpineHump',
        size=(0.15, 0.28, 0.075),
        location=(0.0, 0.05, 1.05),
        rotation=(-0.68, 0.0, 0.0),
        taper=(0.72, 0.85),
        bevel=0.016,
        segments=1,
        color=COL_SLATE_METALLIC,
    ))

    # 2. Sculpted Full-Face Aerodynamic Cyber-Helmet (looking forward along -Y, crown at Z ~ 1.375m)
    helmet_stations = [
        (-0.33, [(0.045, 1.19), (0.085, 1.21), (0.085, 1.25), (0.050, 1.27)], 1.18, 1.28),
        (-0.26, [(0.075, 1.16), (0.125, 1.21), (0.130, 1.29), (0.090, 1.35)], 1.15, 1.365),
        (-0.17, [(0.080, 1.16), (0.130, 1.22), (0.132, 1.30), (0.092, 1.36)], 1.15, 1.375),
        (-0.07, [(0.065, 1.19), (0.112, 1.24), (0.110, 1.31), (0.070, 1.35)], 1.18, 1.36),
        (-0.01, [(0.035, 1.23), (0.065, 1.26), (0.062, 1.30), (0.038, 1.32)], 1.22, 1.33),
    ]
    helmet = add_loft(
        [make_sym_ring(y, hpts, keel, crown) for y, hpts, keel, crown in helmet_stations],
        name='Rider_Helmet',
        color='obsidian',
        shade_smooth=True,
        ao_strength=0.22,
        front_cap=(0.0, -0.345, 1.23),
        rear_cap=(0.0, 0.01, 1.28),
    )
    aa.paint_faces(
        helmet,
        lambda _p, c, n: abs(c.x) < 0.048 and c.z > 1.29 and n.z > 0.4,
        COL_PEARL_CRIMSON,
        ao_strength=0.16,
    )
    body.append(helmet)

    # 3. Mirrored +X Arms (Hands at Y ~ -0.45) & Tucked Riding Legs (Boots on pegs Y ~ +0.05, Z_min ~ 0.45)
    r_side: list[bpy.types.Object] = []

    # Shoulder armor pauldron -> upper arm -> forearm -> gloved hand gripping handlebar
    r_side.append(aa.add_bevel_box(
        name='Rider_Pauldron_L',
        size=(0.095, 0.14, 0.095),
        location=(0.20, -0.09, 1.07),
        rotation=(-0.35, 0.25, -0.15),
        bevel=0.016,
        segments=1,
        color=COL_SLATE_METALLIC,
    ))
    r_side.append(aa.add_limb(
        (0.195, -0.10, 1.05),
        (0.265, -0.27, 0.94),
        radius0=0.056,
        radius1=0.046,
        segments=8,
        name='Rider_UpperArm_L',
        color='navy_coat',
    ))
    r_side.append(aa.add_limb(
        (0.265, -0.27, 0.94),
        (0.250, -0.43, 0.95),
        radius0=0.045,
        radius1=0.036,
        segments=8,
        name='Rider_Forearm_L',
        color=COL_PEARL_CRIMSON,
    ))
    r_side.append(aa.add_bevel_box(
        name='Rider_Glove_L',
        size=(0.068, 0.085, 0.058),
        location=(0.250, -0.455, 0.95),
        bevel=0.012,
        segments=1,
        color='obsidian',
    ))

    # Thigh -> armored knee slider -> shin -> cyber riding boot resting on peg (Z bottom = 0.45m!)
    r_side.append(aa.add_limb(
        (0.115, 0.18, 0.71),
        (0.185, -0.15, 0.63),
        radius0=0.074,
        radius1=0.058,
        segments=8,
        name='Rider_Thigh_L',
        color=COL_CARBON_BLACK,
    ))
    r_side.append(aa.add_bevel_box(
        name='Rider_KneeSlider_L',
        size=(0.065, 0.11, 0.095),
        location=(0.195, -0.16, 0.63),
        rotation=(0.35, 0.15, -0.20),
        bevel=0.014,
        segments=1,
        color=COL_SLATE_METALLIC,
    ))
    r_side.append(aa.add_limb(
        (0.185, -0.15, 0.62),
        (0.175, 0.07, 0.50),
        radius0=0.054,
        radius1=0.042,
        segments=8,
        name='Rider_Shin_L',
        color='obsidian',
    ))
    # Armored riding boot planted on footpeg at Y ~ +0.05, sole at Z = 0.45m
    boot = aa.add_bevel_box(
        name='Rider_Boot_L',
        size=(0.086, 0.19, 0.072),
        location=(0.175, 0.03, 0.486),
        rotation=(0.12, 0.0, 0.0),
        taper=(0.86, 0.88),
        bevel=0.014,
        segments=1,
        color=COL_SLATE_METALLIC,
    )
    aa.paint_faces(boot, lambda _p, c, _n: c.z < 0.475, 'obsidian', ao_strength=0.14)
    r_side.append(boot)

    body.extend(r_side)
    body.extend(aa.mirror_x(r_side))

    # 4. Signal Mesh (Wrap-around glowing V-visor shield + jacket spine & collar telemetry strips)
    visor_stations = [
        (-0.348, [(0.042, 1.235), (0.078, 1.245), (0.074, 1.278), (0.038, 1.285)], 1.230, 1.288),
        (-0.275, [(0.088, 1.240), (0.132, 1.255), (0.128, 1.295), (0.084, 1.305)], 1.238, 1.308),
        (-0.220, [(0.110, 1.250), (0.136, 1.265), (0.132, 1.298), (0.105, 1.305)], 1.252, 1.306),
    ]
    sig.append(add_loft(
        [make_sym_ring(y, hpts, keel, crown) for y, hpts, keel, crown in visor_stations],
        name='Rider_GlowingVisor',
        color=COL_SIGNAL_CYAN,
        signal=True,
    ))
    # Glowing cyber spine strip on dorsal aero hump + collar rim ticks
    sig.append(aa.add_box(
        name='Rider_SpineTelemetry',
        size=(0.032, 0.22, 0.024),
        location=(0.0, 0.05, 1.095),
        rotation=(-0.68, 0.0, 0.0),
        color=COL_SIGNAL_CYAN,
        signal=True,
    ))
    for sign in (-1.0, 1.0):
        sig.append(aa.add_box(
            name=f'Rider_CollarLED_{sign:+.0f}',
            size=(0.022, 0.11, 0.022),
            location=(sign * 0.115, -0.10, 1.145),
            rotation=(-0.45, 0.0, 0.0),
            color=COL_SIGNAL_CYAN,
            signal=True,
        ))

    return aa.combine_by_role(body, sig)


# ---------------------------------------------------------------------------
# Strict Post-Export Contract Verification & Optional Beauty Preview Renderer
# ---------------------------------------------------------------------------

def _bounds_world(objs: Sequence[bpy.types.Object]) -> tuple[Vector, Vector]:
    bpy.context.view_layer.update()
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
    return Vector((min(xs), min(ys), min(zs))), Vector((max(xs), max(ys), max(zs)))


def verify_vehicle_contract(
    glb_path: Path,
    key: str,
    min_tris: int,
    max_tris: int,
    expected_meshes: int,
) -> dict:
    """Verify wheel pivot coordinates, node names, mesh/primitive counts & triangle budgets."""
    raw = glb_path.read_bytes()
    json_len, _ = struct.unpack_from('<2I', raw, 12)
    g = json.loads(raw[20:20 + json_len].decode('utf-8'))

    meshes = g.get('meshes', [])
    assert len(meshes) == expected_meshes, (
        f'{glb_path.name}: expected {expected_meshes} meshes, got {len(meshes)}'
    )
    for m in meshes:
        prims = m.get('primitives', [])
        assert len(prims) == 1, f'{glb_path.name}: mesh {m.get("name")} has {len(prims)} primitives (must be 1)'

    nodes_by_name = {n['name']: n for n in g.get('nodes', []) if 'name' in n}
    assert 'Body' in nodes_by_name and 'Signal' in nodes_by_name, (
        f'{glb_path.name}: missing Body or Signal node ({list(nodes_by_name)})'
    )
    for static_name in ('Body', 'Signal'):
        tr = nodes_by_name[static_name].get('translation', [0.0, 0.0, 0.0])
        assert all(abs(v) < 1e-4 for v in tr), f'{glb_path.name}: {static_name} not at origin: {tr}'

    wheel_nodes = [n for n in g.get('nodes', []) if n.get('name', '').startswith('Wheel_')]
    if key in WHEEL_SPECS:
        spec = WHEEL_SPECS[key]
        expected_wheels = spec['wheels']
        rad = spec['radius']
        assert len(wheel_nodes) == len(expected_wheels), (
            f'{glb_path.name}: expected {len(expected_wheels)} Wheel_* nodes, got {len(wheel_nodes)}'
        )
        for w_name, (bx, by, bz) in expected_wheels:
            assert w_name in nodes_by_name, f'{glb_path.name}: missing {w_name}'
            wn = nodes_by_name[w_name]
            tr = wn.get('translation', [0.0, 0.0, 0.0])
            # Blender (bx, by, bz) -> glTF export_yup=True translation [bx, bz, -by]
            exp_tr = (bx, bz, -by)
            for idx in range(3):
                assert abs(tr[idx] - exp_tr[idx]) < 1e-3, (
                    f'{glb_path.name} {w_name}: translation {tr} != expected {exp_tr}'
                )
            w_mesh = meshes[wn['mesh']]
            pos_acc = g['accessors'][w_mesh['primitives'][0]['attributes']['POSITION']]
            pmin, pmax = pos_acc['min'], pos_acc['max']
            # Centered locally around (0, 0, 0) with Y/Z bounds matching +-radius
            for axis_i in (1, 2):
                assert abs(pmin[axis_i] + rad) < 0.012 and abs(pmax[axis_i] - rad) < 0.012, (
                    f'{glb_path.name} {w_name}: local bounds {pmin}..{pmax} do not match radius {rad}'
                )
    else:
        assert len(wheel_nodes) == 0, f'{glb_path.name}: rider must have NO Wheel_* nodes'

    total_tris = sum(
        g['accessors'][p['indices']]['count'] // 3
        for m in meshes
        for p in m['primitives']
    )
    assert min_tris <= total_tris <= max_tris, (
        f'{glb_path.name}: {total_tris} tris outside {min_tris}..{max_tris}'
    )
    return {'nodes': sorted(nodes_by_name.keys()), 'triangles': total_tris}


def _render_preview_sheet(preview_dir: Path) -> None:
    """Optional scratch preview renderer (invoked only when --render-previews is passed)."""
    preview_dir.mkdir(parents=True, exist_ok=True)
    combos = [
        ('coupe', ['coupe']),
        ('sports', ['sports']),
        ('van', ['van']),
        ('motorbike_with_rider', ['motorbike', 'rider']),
        ('scooter_with_rider', ['scooter', 'rider']),
        ('rider', ['rider']),
    ]
    for label, asset_names in combos:
        aa.reset_scene()
        scene = bpy.context.scene
        for a_name in asset_names:
            bpy.ops.import_scene.gltf(filepath=str(OUT_DIR / f'{a_name}.glb'))

        world = bpy.data.worlds.new('PreviewWorld')
        world.use_nodes = True
        bg = world.node_tree.nodes.get('Background')
        if bg:
            bg.inputs[0].default_value = (0.045, 0.055, 0.085, 1.0)
            bg.inputs[1].default_value = 1.15
        scene.world = world

        # Key + rim sun lights for crisp form inspection
        for lname, lrot, lenergy, lcol in (
            ('KeySun', (0.75, 0.25, -0.55), 3.6, (1.0, 0.98, 0.95)),
            ('RimSun', (-0.85, -0.35, 2.35), 2.0, (0.65, 0.85, 1.0)),
        ):
            ldata = bpy.data.lights.new(lname, type='SUN')
            ldata.energy = lenergy
            ldata.color = lcol
            lobj = bpy.data.objects.new(lname, ldata)
            lobj.rotation_euler = lrot
            bpy.context.collection.objects.link(lobj)

        # Ground reference pad
        aa.add_box('GroundPad', size=(7.0, 7.0, 0.04), location=(0.0, 0.0, -0.02), color=(0.08, 0.09, 0.12))

        scene.render.engine = 'CYCLES'
        scene.cycles.device = 'CPU'
        scene.cycles.samples = 8
        scene.cycles.use_denoising = False
        scene.render.resolution_x = 800
        scene.render.resolution_y = 520

        cam_data = bpy.data.cameras.new('PreviewCam')
        cam_data.lens = 46.0
        cam_obj = bpy.data.objects.new('PreviewCam', cam_data)
        bpy.context.collection.objects.link(cam_obj)
        scene.camera = cam_obj

        dist = 4.5 if label in ('coupe', 'sports', 'van') else 2.7
        z_aim = 0.68 if label != 'rider' else 0.88
        for view_tag, (vx, vy) in (('front', (-0.78, -0.88)), ('rear', (0.80, 0.88))):
            cam_pos = Vector((dist * vx, dist * vy, z_aim + dist * 0.34))
            cam_obj.location = cam_pos
            direction = (Vector((0.0, 0.0, z_aim)) - cam_pos).normalized()
            cam_obj.rotation_mode = 'QUATERNION'
            cam_obj.rotation_quaternion = direction.to_track_quat('-Z', 'Y')
            scene.render.filepath = str(preview_dir / f'{label}_{view_tag}.png')
            bpy.ops.render.render(write_still=True)


def main() -> None:
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument('--render-previews', type=Path, default=None)
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    opts, _ = parser.parse_known_args(argv)

    OUT_DIR.mkdir(parents=True, exist_ok=True)

    manifest = [
        ('coupe', 'coupe.glb', build_coupe, 1800, 4200, 6),
        ('sports', 'sports.glb', build_sports, 1800, 4200, 6),
        ('van', 'van.glb', build_van, 1800, 4200, 6),
        ('motorbike', 'motorbike.glb', build_motorbike, 1800, 4200, 4),
        ('scooter', 'scooter.glb', build_scooter, 1800, 4200, 4),
        ('rider', 'rider.glb', build_rider, 900, 1800, 2),
    ]

    for key, filename, builder, min_budget, max_budget, expected_meshes in manifest:
        objs = builder()
        bmin, bmax = _bounds_world(objs)
        dims = bmax - bmin
        out_path = OUT_DIR / filename
        info = aa.export_glb(
            out_path,
            objs,
            min_tris=min_budget,
            max_meshes=expected_meshes,
        )
        check = verify_vehicle_contract(
            out_path,
            key=key,
            min_tris=min_budget,
            max_tris=max_budget,
            expected_meshes=expected_meshes,
        )
        print(
            f"[OK] {filename:14s}: {info['triangles']:4d} tris, {info['meshes']} meshes, "
            f"{info['size_kb']:5.1f} KB | nodes={check['nodes']} | "
            f"X[{bmin.x:+.3f}..{bmax.x:+.3f}] ({dims.x:.2f}m) "
            f"Y[{bmin.y:+.3f}..{bmax.y:+.3f}] ({dims.y:.2f}m) "
            f"Z[{bmin.z:+.3f}..{bmax.z:+.3f}] ({dims.z:.2f}m)"
        )

    if opts.render_previews is not None:
        _render_preview_sheet(opts.render_previews)


if __name__ == '__main__':
    try:
        main()
    except Exception:
        import traceback
        traceback.print_exc()
        sys.exit(1)
