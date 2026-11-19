"""Headless Blender 4.2 LTS AA sculptor for cyberpunk mission & service-area props.

Run headless with:
  .test/blender/blender --background --disable-autoexec --python tools/build_aa_mission_props.py

Sculpts and exports five crisp vertex-painted ('Body' + 'Signal' = 2 meshes, COLOR_0,
900..2,200 tris each) binary GLB 2.0 assets resting on Z = 0 and facing -Y in Blender
(+Z in Three.js via export_yup=True):
  1. public/street/terminal.glb     — Holo-Uplink Relay Terminal (~1.2m x 0.9m x 2.2m)
  2. public/street/supply-crate.glb — Syndicate Munitions Cache (~1.6m x 1.0m x 1.1m)
  3. public/street/power-core.glb   — Aethelgard Signal Jammer / Reactor Core (~1.0m x 1.0m x 1.85m)
  4. public/street/beacon.glb       — Corp Broadcast Spire (~1.3m x 1.3m x 2.9m)
  5. public/street/barricade.glb    — Hardlight Traffic & Sector Barrier (~2.3m x 0.8m x 1.25m)
"""

from __future__ import annotations

import math
import sys
from pathlib import Path
from typing import Sequence

sys.dont_write_bytecode = True

import bmesh
import bpy
from mathutils import Euler, Matrix, Vector

TOOLS_DIR = Path(__file__).resolve().parent
ROOT_DIR = TOOLS_DIR.parent
OUT_DIR = ROOT_DIR / 'public' / 'street'

if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

import blender_aa_lib as aa


# ---------------------------------------------------------------------------
# Closed-Manifold Sculpting Helpers (FrontSide Culling Safe)
# ---------------------------------------------------------------------------

def add_strut(
    p0,
    p1,
    radius0: float = 0.022,
    radius1: float | None = None,
    segments: int = 6,
    name: str = 'Strut',
    shade_smooth: bool = False,
    color=(0.30, 0.35, 0.42),
    signal: bool = False,
    ao_strength: float = 0.26,
    gradient_z=None,
) -> bpy.types.Object:
    """Create a watertight capped cylinder/frustum connecting 3D points p0 -> p1."""
    v0 = Vector((float(p0[0]), float(p0[1]), float(p0[2])))
    v1 = Vector((float(p1[0]), float(p1[1]), float(p1[2])))
    delta = v1 - v0
    length = max(1e-4, delta.length)
    r0 = float(radius0)
    r1 = float(radius0 if radius1 is None else radius1)
    segs = max(4, int(segments))

    bm = bmesh.new()
    bmesh.ops.create_cone(
        bm,
        cap_ends=True,
        cap_tris=False,
        segments=segs,
        radius1=r0,
        radius2=r1,
        depth=length,
    )
    mid = (v0 + v1) * 0.5
    rot_q = Vector((0.0, 0.0, 1.0)).rotation_difference(delta.normalized())
    obj = aa._finalize_primitive(
        bm,
        name=name,
        location=mid,
        rotation=rot_q,
        shade_smooth=shade_smooth,
        smooth_angle_cos=0.75 if shade_smooth else None,
        color=color,
        signal=signal,
        ao_strength=ao_strength,
    )
    if gradient_z is not None:
        aa.paint_mesh(obj, color=color, signal=signal, ao_strength=ao_strength, gradient_z=gradient_z)
    return obj


def add_tube_sweep(
    points: Sequence[Sequence[float]],
    radius: float = 0.020,
    sides: int = 5,
    name: str = 'TubeSweep',
    shade_smooth: bool = False,
    color=(0.30, 0.35, 0.42),
    signal: bool = False,
    ao_strength: float = 0.24,
    gradient_z=None,
) -> bpy.types.Object:
    """Sweep a closed n-sided polygon cleanly along a 3D polyline with manifold end caps."""
    pts = [Vector((float(p[0]), float(p[1]), float(p[2]))) for p in points]
    if len(pts) < 2:
        raise ValueError('add_tube_sweep requires >= 2 points')
    n = max(4, int(sides))
    rad = float(radius)

    bm = bmesh.new()
    rings: list[list[bmesh.types.BMVert]] = []
    prev_up = Vector((0.0, 0.0, 1.0))

    for idx, pt in enumerate(pts):
        if idx == 0:
            tangent = (pts[1] - pts[0]).normalized()
        elif idx == len(pts) - 1:
            tangent = (pts[-1] - pts[-2]).normalized()
        else:
            t_in = (pts[idx] - pts[idx - 1]).normalized()
            t_out = (pts[idx + 1] - pts[idx]).normalized()
            tangent = t_in + t_out
            tangent = tangent.normalized() if tangent.length > 1e-5 else t_out

        ref = prev_up if abs(tangent.dot(prev_up)) < 0.94 else Vector((1.0, 0.0, 0.0))
        side_v = tangent.cross(ref).normalized()
        up_v = side_v.cross(tangent).normalized()
        prev_up = up_v

        ring = []
        for i in range(n):
            ang = (i / n) * math.tau
            offset = (side_v * math.cos(ang) + up_v * math.sin(ang)) * rad
            ring.append(bm.verts.new(pt + offset))
        rings.append(ring)

    start_pole = bm.verts.new(pts[0])
    end_pole = bm.verts.new(pts[-1])

    for k in range(len(rings) - 1):
        ra, rb = rings[k], rings[k + 1]
        for i in range(n):
            j = (i + 1) % n
            bm.faces.new((ra[i], ra[j], rb[j], rb[i]))

    for i in range(n):
        j = (i + 1) % n
        bm.faces.new((start_pole, rings[0][j], rings[0][i]))
        bm.faces.new((end_pole, rings[-1][i], rings[-1][j]))

    obj = aa._finalize_primitive(
        bm,
        name=name,
        shade_smooth=shade_smooth,
        smooth_angle_cos=0.78 if shade_smooth else None,
        color=color,
        signal=signal,
        ao_strength=ao_strength,
    )
    if gradient_z is not None:
        aa.paint_mesh(obj, color=color, signal=signal, ao_strength=ao_strength, gradient_z=gradient_z)
    return obj


def add_lathe(
    profile_rz: Sequence[tuple[float, float]],
    segments: int = 12,
    phase: float = 0.0,
    name: str = 'Lathe',
    location=(0.0, 0.0, 0.0),
    rotation=(0.0, 0.0, 0.0),
    shade_smooth: bool = False,
    color=(0.35, 0.38, 0.44),
    signal: bool = False,
    ao_strength: float = 0.26,
    gradient_z=None,
) -> bpy.types.Object:
    """Revolve a (radius, z) profile around local +Z into a closed manifold solid."""
    segs = max(4, int(segments))
    pts = [(max(0.0, float(r)), float(z)) for r, z in profile_rz]
    if len(pts) < 2:
        raise ValueError('add_lathe requires >= 2 (r, z) points')

    bm = bmesh.new()
    rings: list[list[bmesh.types.BMVert]] = []
    bot_pole = None
    top_pole = None

    for idx, (rad, z) in enumerate(pts):
        if idx == 0 and rad <= 1e-5:
            bot_pole = bm.verts.new((0.0, 0.0, z))
            continue
        if idx == len(pts) - 1 and rad <= 1e-5:
            top_pole = bm.verts.new((0.0, 0.0, z))
            continue
        ring = []
        for i in range(segs):
            ang = phase + (i / segs) * math.tau
            ring.append(bm.verts.new((math.cos(ang) * rad, math.sin(ang) * rad, z)))
        rings.append(ring)

    if bot_pole is None:
        bot_pole = bm.verts.new((0.0, 0.0, pts[0][1]))
    if top_pole is None:
        top_pole = bm.verts.new((0.0, 0.0, pts[-1][1]))

    for k in range(len(rings) - 1):
        ra, rb = rings[k], rings[k + 1]
        for i in range(segs):
            j = (i + 1) % segs
            bm.faces.new((ra[i], ra[j], rb[j], rb[i]))

    for i in range(segs):
        j = (i + 1) % segs
        bm.faces.new((bot_pole, rings[0][j], rings[0][i]))
        bm.faces.new((top_pole, rings[-1][i], rings[-1][j]))

    obj = aa._finalize_primitive(
        bm,
        name=name,
        location=location,
        rotation=rotation,
        shade_smooth=shade_smooth,
        smooth_angle_cos=0.78 if shade_smooth else None,
        color=color,
        signal=signal,
        ao_strength=ao_strength,
    )
    if gradient_z is not None:
        aa.paint_mesh(obj, color=color, signal=signal, ao_strength=ao_strength, gradient_z=gradient_z)
    return obj


def build_lofted_solid(
    rings: Sequence[Sequence[tuple[float, float, float]]],
    name: str = 'LoftedSolid',
    location=(0.0, 0.0, 0.0),
    rotation=(0.0, 0.0, 0.0),
    color=(0.30, 0.35, 0.42),
    signal: bool = False,
    shade_smooth: bool = False,
    ao_strength: float = 0.25,
    gradient_z=None,
    base_point: tuple[float, float, float] | None = None,
    tip_point: tuple[float, float, float] | None = None,
) -> bpy.types.Object:
    """Create a watertight manifold lofted mesh across closed cross-section rings."""
    if len(rings) < 2:
        raise ValueError(f'{name}: build_lofted_solid requires >= 2 rings')
    m = len(rings[0])
    if m < 3 or any(len(r) != m for r in rings):
        raise ValueError(f'{name}: all rings must have matching vertex count >= 3')

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

    obj = aa._finalize_primitive(
        bm,
        name=name,
        location=location,
        rotation=rotation,
        shade_smooth=shade_smooth,
        color=color,
        signal=signal,
        ao_strength=ao_strength,
    )
    if gradient_z is not None:
        aa.paint_mesh(obj, color=color, signal=signal, ao_strength=ao_strength, gradient_z=gradient_z)
    return obj


def build_polygonal_ring(
    radius_outer: float,
    radius_inner: float,
    thickness: float,
    sides: int = 12,
    profile_mode: str = 'diamond',
    phase: float = 0.0,
    location=(0.0, 0.0, 0.0),
    rotation=(0.0, 0.0, 0.0),
    scale=(1.0, 1.0, 1.0),
    name: str = 'PolyRing',
    color='brass',
    signal: bool = False,
    ao_strength: float = 0.22,
    gradient_z=None,
) -> bpy.types.Object:
    """Build a closed manifold polygonal collar or holographic data ring."""
    bm = bmesh.new()
    hz = float(thickness) * 0.5
    r_in = float(radius_inner)
    r_out = float(radius_outer)
    r_mid = 0.5 * (r_in + r_out)

    if profile_mode == 'hex':
        ch = min(hz * 0.44, (r_out - r_in) * 0.24)
        profile = [
            (r_in, -hz + ch),
            (r_in + ch, -hz),
            (r_out - ch, -hz),
            (r_out, -hz + ch),
            (r_out, hz - ch),
            (r_out - ch, hz),
            (r_in + ch, hz),
            (r_in, hz - ch),
        ]
    elif profile_mode == 'box':
        profile = [
            (r_in, -hz),
            (r_out, -hz),
            (r_out, hz),
            (r_in, hz),
        ]
    else:
        profile = [
            (r_in, 0.0),
            (r_mid, -hz),
            (r_out, 0.0),
            (r_mid, hz),
        ]

    p_len = len(profile)
    n = max(3, int(sides))
    loops = []
    for i in range(n):
        ang = phase + (i / n) * math.tau
        ca, sa = math.cos(ang), math.sin(ang)
        loops.append([bm.verts.new((ca * r, sa * r, dz)) for r, dz in profile])

    for i in range(n):
        i_next = (i + 1) % n
        la, lb = loops[i], loops[i_next]
        for k in range(p_len):
            k_next = (k + 1) % p_len
            bm.faces.new((la[k], la[k_next], lb[k_next], lb[k]))

    obj = aa._finalize_primitive(
        bm,
        name=name,
        location=location,
        rotation=rotation,
        scale=scale,
        shade_smooth=False,
        color=color,
        signal=signal,
        ao_strength=ao_strength,
    )
    if gradient_z is not None:
        aa.paint_mesh(obj, color=color, signal=signal, ao_strength=ao_strength, gradient_z=gradient_z)
    return obj


def add_crystal_shard(
    name: str = 'CrystalShard',
    radius: float = 0.08,
    bot_len: float = 0.14,
    waist_h: float = 0.14,
    top_len: float = 0.22,
    sides: int = 6,
    twist: float = 0.14,
    location=(0.0, 0.0, 0.0),
    rotation=(0.0, 0.0, 0.0),
    color='neon_cyan',
    gradient_z=None,
    signal: bool = True,
) -> bpy.types.Object:
    """Create a crisp flat-shaded double-terminated polygonal crystal/beacon core."""
    n = max(4, int(sides))
    bm = bmesh.new()
    bot_v = bm.verts.new((0.0, 0.0, -float(bot_len)))
    top_v = bm.verts.new((0.0, 0.0, float(top_len)))

    stations = [
        (-float(waist_h) * 0.45, float(radius) * 0.84, 0.0),
        (float(waist_h) * 0.45, float(radius), float(twist) * 0.5),
        (float(top_len) * 0.62, float(radius) * 0.56, float(twist)),
    ]
    rings = []
    for z, rad, shift in stations:
        ring = []
        for i in range(n):
            ang = shift + (i / n) * math.tau
            ring.append(bm.verts.new((math.cos(ang) * rad, math.sin(ang) * rad, z)))
        rings.append(ring)

    for i in range(n):
        j = (i + 1) % n
        bm.faces.new((bot_v, rings[0][j], rings[0][i]))
        for k in range(len(rings) - 1):
            bm.faces.new((rings[k][i], rings[k][j], rings[k + 1][j], rings[k + 1][i]))
        bm.faces.new((top_v, rings[-1][i], rings[-1][j]))

    obj = aa._finalize_primitive(
        bm,
        name=name,
        location=location,
        rotation=rotation,
        shade_smooth=False,
        color=color,
        signal=signal,
        ao_strength=0.22,
    )
    if gradient_z is not None:
        aa.paint_mesh(obj, color=color, signal=signal, ao_strength=0.22, gradient_z=gradient_z)
    return obj


def octagonal_rect_2d(hx: float, hy: float, cx_cut: float, cy_cut: float) -> list[tuple[float, float]]:
    """Return an 8-point chamfered-rectangle 2D polygon centered at (0, 0)."""
    return [
        (-hx + cx_cut, -hy),
        (hx - cx_cut, -hy),
        (hx, -hy + cy_cut),
        (hx, hy - cy_cut),
        (hx - cx_cut, hy),
        (-hx + cx_cut, hy),
        (-hx, hy - cy_cut),
        (-hx, -hy + cy_cut),
    ]


def _bounds_world(objs: Sequence[bpy.types.Object]) -> tuple[Vector, Vector]:
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


def _snap_ground_z0(objs: Sequence[bpy.types.Object]) -> None:
    """Snap combined asset cleanly onto Z = 0.0000 if beveling introduced sub-millimetre drift."""
    bmin, _ = _bounds_world(objs)
    if abs(bmin.z) > 1e-7:
        if abs(bmin.z) > 0.03:
            raise AssertionError(f'Base Z drift {bmin.z:.4f}m exceeds 3cm tolerance')
        shift = Matrix.Translation((0.0, 0.0, -bmin.z))
        for o in objs:
            if o is not None and o.type == 'MESH':
                aa.bake_transform(o)
                o.data.transform(shift)
                o.data.update()


def _place_in_frame(
    local_pos: tuple[float, float, float],
    origin: Vector,
    rot_euler: Euler,
) -> Vector:
    return origin + (rot_euler.to_matrix() @ Vector(local_pos))


# ---------------------------------------------------------------------------
# 1. Holo-Uplink Relay Terminal (public/street/terminal.glb)
#    Footprint ~1.20m x 0.90m, Height 2.20m, Facing -Y (+Z in Three.js)
# ---------------------------------------------------------------------------

def build_terminal() -> list[bpy.types.Object]:
    aa.reset_scene()
    body: list[bpy.types.Object] = []
    sig: list[bpy.types.Object] = []

    col_gold = '#ffc426'
    col_gold_deep = '#ff8c12'
    col_cyan_hi = '#8efcff'
    col_cyan_mid = '#08e8ff'
    col_cyan_deep = '#008ee6'

    # --- A. Grounded Octagonal Plinth, Operator Tread & Anchor Lugs (Z: 0.00..0.14m) ---
    plinth = aa.add_extruded_profile(
        octagonal_rect_2d(0.58, 0.44, 0.13, 0.11),
        depth=0.068,
        axis='Z',
        centered=False,
        location=(0.0, 0.0, 0.0),
        name='Terminal_Plinth',
        color='asphalt_dark',
    )
    aa.paint_faces(plinth, lambda _p, _c, n: n.z > 0.65, color='slate')
    body.append(plinth)

    # Front (-Y) operator step tread & glowing amber safety kick-strip
    body.append(aa.add_box(
        name='Operator_Step_Mat',
        size=(0.76, 0.24, 0.030),
        location=(0.0, -0.33, 0.015),
        taper=(0.96, 0.92),
        color='gunmetal',
    ))
    sig.append(aa.add_box(
        name='Operator_Step_SafetyBar',
        size=(0.62, 0.018, 0.014),
        location=(0.0, -0.444, 0.024),
        color=col_gold_deep,
        signal=True,
    ))

    # Stepped inner pedestal riser collar
    body.append(aa.add_extruded_profile(
        octagonal_rect_2d(0.42, 0.31, 0.09, 0.08),
        depth=0.072,
        axis='Z',
        centered=False,
        location=(0.0, 0.04, 0.066),
        name='Pedestal_Riser_Collar',
        color='gunmetal',
    ))

    # 4 corner heavy anchor shoes + brass bolts
    for sx in (-0.49, 0.49):
        for sy in (-0.34, 0.34):
            body.append(aa.add_box(
                name=f'Base_Anchor_{sx:+.2f}_{sy:+.2f}',
                size=(0.11, 0.11, 0.082),
                location=(sx, sy, 0.041),
                taper=(0.86, 0.86),
                color='obsidian',
            ))
            body.append(aa.add_cylinder(
                name=f'Base_Bolt_{sx:+.2f}_{sy:+.2f}',
                radius=0.022,
                depth=0.024,
                vertices=6,
                location=(sx, sy, 0.092),
                shade_smooth=False,
                color='brass',
            ))

    # --- B. Angled Cyber-Kiosk Pedestal Spine & Rear Service Bay (Z: 0.12..1.68m) ---
    spine_profile_yz = [
        (-0.17, 0.12),
        (-0.27, 0.52),
        (-0.23, 0.86),
        (-0.03, 1.12),
        (-0.07, 1.50),
        (-0.21, 1.66),
        (0.24, 1.63),
        (0.35, 1.22),
        (0.30, 0.64),
        (0.34, 0.12),
    ]
    spine_outer = aa.add_extruded_profile(
        spine_profile_yz,
        depth=0.50,
        axis='X',
        bevel=0.016,
        bevel_segments=1,
        name='Kiosk_Pedestal_Spine',
        color='navy_coat',
    )
    aa.paint_faces(spine_outer, lambda _p, c, n: abs(n.x) < 0.35 and n.y < -0.45 and c.z > 1.15, color='obsidian')
    body.append(spine_outer)

    # Dark carbon recessed chassis core protruding slightly on +/-X for layered armor seam
    core_profile_yz = [
        (-0.13, 0.15),
        (-0.22, 0.54),
        (-0.01, 1.10),
        (-0.14, 1.60),
        (0.20, 1.58),
        (0.30, 1.18),
        (0.26, 0.15),
    ]
    body.append(aa.add_extruded_profile(
        core_profile_yz,
        depth=0.56,
        axis='X',
        name='Kiosk_Inner_Core',
        color='obsidian',
    ))

    # Front lower cooling louver bay (-Y) with internal cyan status backlight
    sig.append(aa.add_box(
        name='Front_Intake_Glow',
        size=(0.30, 0.018, 0.34),
        location=(0.0, -0.248, 0.48),
        rotation=(-0.18, 0.0, 0.0),
        color=col_cyan_deep,
        signal=True,
    ))
    for li in range(5):
        lz = 0.33 + li * 0.075
        ly = -0.235 - (li if li < 3 else (4 - li)) * 0.012
        body.append(aa.add_box(
            name=f'Front_Louver_{li}',
            size=(0.36, 0.036, 0.026),
            location=(0.0, ly, lz),
            rotation=(0.25, 0.0, 0.0),
            color='steel',
        ))

    # Rear (+Y) maintenance hatch, heatsink fins & copper power conduits
    body.append(aa.add_bevel_box(
        name='Rear_Service_Hatch',
        size=(0.36, 0.055, 0.62),
        location=(0.0, 0.335, 0.88),
        bevel=0.010,
        segments=1,
        color='teal_tech',
    ))
    for hi in range(4):
        body.append(aa.add_box(
            name=f'Rear_Heatsink_Fin_{hi}',
            size=(0.28, 0.042, 0.022),
            location=(0.0, 0.365, 1.28 + hi * 0.062),
            color='silver',
        ))
    sig.append(aa.add_box(
        name='Rear_Diagnostic_Strip',
        size=(0.22, 0.016, 0.032),
        location=(0.0, 0.368, 0.66),
        color=col_cyan_mid,
        signal=True,
    ))

    for side_s in (-1.0, 1.0):
        body.append(add_tube_sweep(
            points=[
                (side_s * 0.16, 0.40, 0.065),
                (side_s * 0.18, 0.43, 0.24),
                (side_s * 0.18, 0.37, 0.44),
                (side_s * 0.15, 0.31, 0.56),
            ],
            radius=0.020,
            sides=5,
            name=f'Rear_Power_Conduit_{side_s:+.0f}',
            color='copper',
        ))

    # --- C. Cantilevered Keyboard Deck & Biometric Console (-Y Front, Z: 0.86..1.08m) ---
    deck_origin = Vector((0.0, -0.245, 0.955))
    deck_euler = Euler((0.34, 0.0, 0.0), 'XYZ')  # +Z top tilts forward toward -Y operator

    deck_tray = aa.add_bevel_box(
        name='Keyboard_Console_Tray',
        size=(0.78, 0.38, 0.078),
        location=deck_origin,
        rotation=deck_euler,
        bevel=0.014,
        segments=1,
        color='gunmetal',
    )
    aa.paint_faces(deck_tray, lambda _p, c, _n: c.y < -0.36, color='steel')
    body.append(deck_tray)

    # Left/Right console wrist-bumper cheeks
    for side_s in (-1.0, 1.0):
        body.append(aa.add_box(
            name=f'Deck_Bumper_{side_s:+.0f}',
            size=(0.052, 0.40, 0.096),
            location=_place_in_frame((side_s * 0.395, 0.0, 0.010), deck_origin, deck_euler),
            rotation=deck_euler,
            taper=(0.86, 0.96),
            color='obsidian',
        ))

    # Recessed dark bezel + glowing cyan keycap clusters & golden biometric palm scanner
    body.append(aa.add_box(
        name='Keyboard_Recess_Plate',
        size=(0.68, 0.28, 0.020),
        location=_place_in_frame((0.0, 0.01, 0.036), deck_origin, deck_euler),
        rotation=deck_euler,
        color='obsidian',
    ))
    # Main alphanumeric glowing key matrix (left-center)
    sig.append(aa.add_box(
        name='KeyMatrix_Main_Cyan',
        size=(0.38, 0.14, 0.018),
        location=_place_in_frame((-0.11, -0.02, 0.046), deck_origin, deck_euler),
        rotation=deck_euler,
        color=col_cyan_mid,
        signal=True,
    ))
    # Upper function key strip (gold)
    sig.append(aa.add_box(
        name='KeyMatrix_Fn_Gold',
        size=(0.38, 0.038, 0.018),
        location=_place_in_frame((-0.11, 0.088, 0.046), deck_origin, deck_euler),
        rotation=deck_euler,
        color=col_gold,
        signal=True,
    ))
    # Sculpted key-row divider bars so the keyboard reads as crisp physical keys
    for row_y in (-0.055, -0.005, 0.042):
        body.append(aa.add_box(
            name=f'KeyRow_Divider_{row_y:+.3f}',
            size=(0.39, 0.010, 0.022),
            location=_place_in_frame((-0.11, row_y, 0.047), deck_origin, deck_euler),
            rotation=deck_euler,
            color='carbon',
        ))
    # Right-side biometric palm scanner ring + gold glow pad + brass rotary dial
    body.append(aa.add_cylinder(
        name='Biometric_Scanner_Bezel',
        radius=0.068,
        depth=0.022,
        vertices=8,
        location=_place_in_frame((0.20, -0.02, 0.045), deck_origin, deck_euler),
        rotation=deck_euler,
        shade_smooth=False,
        color='brass',
    ))
    sig.append(aa.add_cylinder(
        name='Biometric_Scanner_Glow',
        radius=0.050,
        depth=0.024,
        vertices=8,
        location=_place_in_frame((0.20, -0.02, 0.048), deck_origin, deck_euler),
        rotation=deck_euler,
        shade_smooth=False,
        color=col_gold,
        signal=True,
    ))
    sig.append(aa.add_box(
        name='Credchip_Slot_Glow',
        size=(0.11, 0.020, 0.016),
        location=_place_in_frame((0.20, 0.092, 0.048), deck_origin, deck_euler),
        rotation=deck_euler,
        color='neon_magenta',
        signal=True,
    ))

    # --- D. Dual Architectural Side Fins (X = ±0.50..±0.60m, Z: 0.00..2.04m) ---
    fin_body_l: list[bpy.types.Object] = []
    fin_sig_l: list[bpy.types.Object] = []

    # Grounded outrigger boot anchoring left fin cleanly to X = +0.600m, Z = 0.000m
    fin_body_l.append(aa.add_box(
        name='Fin_Outrigger_Boot_L',
        size=(0.13, 0.50, 0.10),
        location=(0.535, 0.02, 0.050),
        taper=(0.88, 0.92),
        color='gunmetal',
    ))

    # Swept aerodynamic side fin plate
    fin_profile_yz = [
        (-0.20, 0.09),
        (-0.34, 0.72),
        (-0.36, 1.28),
        (-0.26, 1.78),
        (-0.08, 2.04),
        (0.18, 1.98),
        (0.24, 1.46),
        (0.20, 0.78),
        (0.24, 0.09),
    ]
    fin_plate_l = aa.add_extruded_profile(
        fin_profile_yz,
        depth=0.064,
        axis='X',
        location=(0.545, 0.0, 0.0),
        bevel=0.010,
        bevel_segments=1,
        name='Side_Fin_Plate_L',
        color='slate',
    )
    aa.paint_faces(fin_plate_l, lambda _p, c, _n: c.z > 1.56, color='warm_white')
    fin_body_l.append(fin_plate_l)

    # Outer armor accent cheek on fin
    fin_body_l.append(aa.add_box(
        name='Side_Fin_ArmorPod_L',
        size=(0.036, 0.24, 0.56),
        location=(0.582, -0.08, 1.18),
        rotation=(-0.10, 0.0, 0.0),
        taper=(0.82, 0.88),
        color='navy_coat',
    ))

    # Twin horizontal mounting struts clamping fin to central spine
    for sz, sy in ((0.64, 0.02), (1.44, 0.05)):
        fin_body_l.append(aa.add_box(
            name=f'Fin_Mount_Strut_L_{sz:.2f}',
            size=(0.28, 0.11, 0.075),
            location=(0.39, sy, sz),
            taper=(0.94, 0.84),
            color='brass' if sz > 1.0 else 'steel',
        ))

    # Glowing leading-edge light blade + wingtip amber pip along the fin
    fin_sig_l.append(add_tube_sweep(
        points=[
            (0.512, -0.21, 0.20),
            (0.512, -0.345, 0.74),
            (0.512, -0.365, 1.28),
            (0.512, -0.265, 1.77),
            (0.512, -0.095, 2.01),
        ],
        radius=0.013,
        sides=4,
        name='Fin_Neon_Edge_L',
        color=col_cyan_mid,
        gradient_z=(col_cyan_deep, col_cyan_hi),
        signal=True,
    ))
    fin_sig_l.append(aa.add_box(
        name='Fin_Tip_Pip_L',
        size=(0.072, 0.095, 0.036),
        location=(0.545, 0.03, 2.005),
        rotation=(-0.22, 0.0, 0.0),
        color=col_gold,
        signal=True,
    ))

    body.extend(fin_body_l)
    body.extend(aa.mirror_x(fin_body_l, bone_swap=True))
    sig.extend(fin_sig_l)
    sig.extend(aa.mirror_x(fin_sig_l, bone_swap=True))

    # --- E. Holo-Projector Pods & Upper Relay Transceiver Mast (Z: 1.08..2.200m) ---
    # Lower holo-emitter well behind keyboard
    body.append(aa.add_cylinder(
        name='Lower_Holo_Emitter_Bezel',
        radius=0.125,
        radius_top=0.105,
        depth=0.055,
        vertices=8,
        location=(0.0, -0.09, 1.115),
        shade_smooth=False,
        color='brass',
    ))
    sig.append(aa.add_cylinder(
        name='Lower_Holo_Emitter_Lens',
        radius=0.088,
        depth=0.026,
        vertices=8,
        location=(0.0, -0.09, 1.142),
        shade_smooth=False,
        color=col_gold,
        signal=True,
    ))

    # Upper brow downward holo-lens pod
    body.append(aa.add_cylinder(
        name='Upper_Holo_Projector_Pod',
        radius=0.092,
        radius_top=0.115,
        depth=0.075,
        vertices=8,
        location=(0.0, -0.11, 1.615),
        shade_smooth=False,
        color='gunmetal',
    ))
    sig.append(aa.add_cylinder(
        name='Upper_Holo_Projector_Lens',
        radius=0.068,
        depth=0.022,
        vertices=8,
        location=(0.0, -0.11, 1.575),
        shade_smooth=False,
        color=col_cyan_hi,
        signal=True,
    ))

    # Upper relay mast peaking cleanly at Z = 2.200m
    body.append(aa.add_bevel_box(
        name='Relay_Mast_Base_Turret',
        size=(0.30, 0.26, 0.14),
        location=(0.0, 0.11, 1.68),
        bevel=0.012,
        segments=1,
        color='gunmetal',
    ))
    body.append(aa.add_cylinder(
        name='Relay_Mast_Shaft',
        radius=0.062,
        radius_top=0.040,
        depth=0.34,
        vertices=6,
        location=(0.0, 0.11, 1.91),
        shade_smooth=False,
        color='steel',
    ))
    for ring_z in (1.82, 1.97):
        body.append(build_polygonal_ring(
            radius_outer=0.108 if ring_z < 1.9 else 0.088,
            radius_inner=0.056 if ring_z < 1.9 else 0.044,
            thickness=0.024,
            sides=6,
            location=(0.0, 0.11, ring_z),
            name=f'Relay_Mast_Collar_{ring_z:.2f}',
            color='brass',
        ))
    # Twin side sensor horns on mast
    for sx in (-1.0, 1.0):
        body.append(add_strut(
            (sx * 0.05, 0.11, 1.90),
            (sx * 0.21, 0.08, 2.04),
            radius0=0.018,
            radius1=0.010,
            segments=5,
            name=f'Relay_Horn_{sx:+.0f}',
            color='silver',
        ))

    # Apex golden/cyan uplink crown beacon topping exact Z = 2.200m
    sig.append(add_crystal_shard(
        name='Relay_Apex_Beacon',
        radius=0.052,
        bot_len=0.045,
        waist_h=0.048,
        top_len=0.115,
        sides=6,
        twist=0.12,
        location=(0.0, 0.11, 2.085),
        color=col_gold,
        gradient_z=(col_gold_deep, '#fff29c'),
        signal=True,
    ))

    # --- F. Floating Cyan/Gold Holographic Screen & Orbiting Data Rings ('Signal') ---
    holo_origin = Vector((0.0, -0.175, 1.385))
    holo_euler = Euler((-0.18, 0.0, 0.0), 'XYZ')  # -Y front face raked slightly upward toward viewer

    # Primary floating widescreen cyan holo-slate (closed solid 18mm thick)
    holo_slate = aa.add_box(
        name='Holo_Main_Slate',
        size=(0.60, 0.018, 0.34),
        location=holo_origin,
        rotation=holo_euler,
        color=col_cyan_mid,
        signal=True,
    )
    aa.paint_mesh(holo_slate, color=col_cyan_mid, signal=True, gradient_z=(col_cyan_deep, col_cyan_hi))
    sig.append(holo_slate)

    # Top & bottom golden header/footer bars + 4 corner L-brackets
    for hz_bar, bar_w in ((0.184, 0.56), (-0.184, 0.42)):
        sig.append(aa.add_box(
            name=f'Holo_Gold_Header_{hz_bar:+.3f}',
            size=(bar_w, 0.026, 0.020),
            location=_place_in_frame((0.0, 0.0, hz_bar), holo_origin, holo_euler),
            rotation=holo_euler,
            color=col_gold,
            signal=True,
        ))
    for sx in (-1.0, 1.0):
        for sz in (-1.0, 1.0):
            sig.append(aa.add_box(
                name=f'Holo_Corner_H_{sx:+.0f}_{sz:+.0f}',
                size=(0.075, 0.028, 0.016),
                location=_place_in_frame((sx * 0.285, 0.0, sz * 0.175), holo_origin, holo_euler),
                rotation=holo_euler,
                color=col_gold,
                signal=True,
            ))
            sig.append(aa.add_box(
                name=f'Holo_Corner_V_{sx:+.0f}_{sz:+.0f}',
                size=(0.016, 0.028, 0.068),
                location=_place_in_frame((sx * 0.314, 0.0, sz * 0.148), holo_origin, holo_euler),
                rotation=holo_euler,
                color=col_gold,
                signal=True,
            ))

    # Raised 3D holographic reticle ring & telemetry bars (proud on both sides of slate!)
    sig.append(build_polygonal_ring(
        radius_outer=0.088,
        radius_inner=0.066,
        thickness=0.030,
        sides=8,
        location=_place_in_frame((0.0, 0.0, 0.01), holo_origin, holo_euler),
        rotation=Euler((-0.18 + math.pi * 0.5, 0.0, 0.0), 'XYZ'),
        name='Holo_Sync_Reticle',
        color=col_gold,
        signal=True,
    ))
    for bi, bh in enumerate((0.08, 0.15, 0.11, 0.18)):
        sig.append(aa.add_box(
            name=f'Holo_Waveform_Bar_{bi}',
            size=(0.024, 0.032, bh),
            location=_place_in_frame((-0.23 + bi * 0.036, 0.0, -0.07 + bh * 0.5), holo_origin, holo_euler),
            rotation=holo_euler,
            color='headlight' if bi == 3 else col_gold,
            signal=True,
        ))
    for ri in range(3):
        sig.append(aa.add_box(
            name=f'Holo_Readout_Line_{ri}',
            size=(0.13 - ri * 0.02, 0.032, 0.018),
            location=_place_in_frame((0.19, 0.0, 0.06 - ri * 0.055), holo_origin, holo_euler),
            rotation=holo_euler,
            color=col_cyan_hi if ri != 0 else col_gold,
            signal=True,
        ))

    # Flanking wraparound angled sub-screens
    for sx in (-1.0, 1.0):
        sig.append(aa.add_box(
            name=f'Holo_Side_WingSlate_{sx:+.0f}',
            size=(0.125, 0.015, 0.25),
            location=(sx * 0.385, -0.215, 1.385),
            rotation=(-0.16, 0.0, sx * 0.36),
            color=col_gold if sx > 0 else col_cyan_mid,
            signal=True,
        ))

    # Floating Cyan & Gold Orbiting Data Rings above projector hood + lower projection ring
    ring_center = (0.0, -0.05, 1.81)
    sig.append(build_polygonal_ring(
        radius_outer=0.315,
        radius_inner=0.286,
        thickness=0.018,
        sides=12,
        location=ring_center,
        rotation=(0.30, -0.18, 0.0),
        name='Holo_Data_Ring_Outer_Cyan',
        color=col_cyan_mid,
        gradient_z=(col_cyan_deep, col_cyan_hi),
        signal=True,
    ))
    sig.append(build_polygonal_ring(
        radius_outer=0.228,
        radius_inner=0.202,
        thickness=0.016,
        sides=10,
        location=ring_center,
        rotation=(-0.26, 0.24, 0.15),
        name='Holo_Data_Ring_Inner_Gold',
        color=col_gold,
        signal=True,
    ))
    sig.append(add_crystal_shard(
        name='Holo_Floating_Core_Node',
        radius=0.058,
        bot_len=0.085,
        waist_h=0.055,
        top_len=0.095,
        sides=6,
        twist=0.20,
        location=ring_center,
        color=col_cyan_hi,
        gradient_z=(col_cyan_mid, '#ffffff'),
        signal=True,
    ))
    sig.append(build_polygonal_ring(
        radius_outer=0.175,
        radius_inner=0.152,
        thickness=0.014,
        sides=8,
        location=(0.0, -0.11, 1.168),
        rotation=(0.08, 0.0, 0.0),
        name='Holo_Base_Emitter_Ring',
        color=col_gold,
        signal=True,
    ))

    objs = aa.combine_by_role(body, sig)
    _snap_ground_z0(objs)
    return objs


# ---------------------------------------------------------------------------
# 2. Syndicate Munitions Cache (public/street/supply-crate.glb)
#    Dimensions ~1.60m x 1.00m x 1.10m, Facing -Y (+Z in Three.js)
# ---------------------------------------------------------------------------

def build_supply_crate() -> list[bpy.types.Object]:
    aa.reset_scene()
    body: list[bpy.types.Object] = []
    sig: list[bpy.types.Object] = []

    col_olive_armor = (0.185, 0.235, 0.215)
    col_amber = '#ff9c14'
    col_amber_hi = '#ffd24c'
    col_magenta = '#ff1f80'

    # --- A. Heavy Forklift Skid Base (Z: 0.000..0.105m) ---
    for sy in (-0.34, 0.34):
        body.append(aa.add_box(
            name=f'Skid_Rail_{sy:+.2f}',
            size=(1.42, 0.13, 0.092),
            location=(0.0, sy, 0.046),
            taper=(0.98, 0.90),
            color='asphalt_dark',
        ))
    for sx in (-0.58, 0.0, 0.58):
        body.append(aa.add_box(
            name=f'Skid_CrossBearer_{sx:+.2f}',
            size=(0.16, 0.80, 0.072),
            location=(sx, 0.0, 0.036),
            color='obsidian',
        ))

    # --- B. Armored Octagonal Cargo Tub & Clamshell Lid (Z: 0.085..1.045m) ---
    # Lower octagonal hull (bottom + two 45-deg lower chines + vertical armored side walls)
    tub_profile_yz = [
        (-0.315, 0.085),
        (-0.455, 0.235),
        (-0.455, 0.620),
        (0.455, 0.620),
        (0.455, 0.235),
        (0.315, 0.085),
    ]
    tub = aa.add_extruded_profile(
        tub_profile_yz,
        depth=1.46,
        axis='X',
        bevel=0.014,
        bevel_segments=1,
        name='Crate_Octagonal_Tub',
        color=col_olive_armor,
    )
    body.append(tub)

    # Recessed dark perimeter seal gasket collar at Z = 0.615..0.658m
    body.append(aa.add_box(
        name='Crate_Seam_Gasket',
        size=(1.48, 0.87, 0.048),
        location=(0.0, 0.0, 0.635),
        color='obsidian',
    ))

    # Upper octagonal clamshell lid (vertical lip + two angled upper shoulders + flat crown deck)
    lid_profile_yz = [
        (-0.468, 0.650),
        (-0.468, 0.785),
        (-0.300, 1.042),
        (0.300, 1.042),
        (0.468, 0.785),
        (0.468, 0.650),
    ]
    lid = aa.add_extruded_profile(
        lid_profile_yz,
        depth=1.48,
        axis='X',
        bevel=0.014,
        bevel_segments=1,
        name='Crate_Octagonal_Lid',
        color='gunmetal',
    )
    aa.paint_faces(lid, lambda _p, _c, n: n.z > 0.85, color='slate')
    body.append(lid)

    # 3 wraparound octagonal structural stiffener ribs
    rib_profile_yz = [
        (-0.330, 0.078),
        (-0.478, 0.232),
        (-0.478, 0.790),
        (-0.312, 1.056),
        (0.312, 1.056),
        (0.478, 0.790),
        (0.478, 0.232),
        (0.330, 0.078),
    ]
    for rx in (-0.35, 0.0, 0.35):
        body.append(aa.add_extruded_profile(
            rib_profile_yz,
            depth=0.056,
            axis='X',
            location=(rx, 0.0, 0.0),
            name=f'Armor_Stiffener_Rib_{rx:+.2f}',
            color='carbon',
        ))

    # --- C. 8 Heavy Corner Bumpers & End-Cap Handles (X = ±0.800m, Y = ±0.500m, Z: 0.000..1.100m) ---
    end_body_l: list[bpy.types.Object] = []
    end_sig_l: list[bpy.types.Object] = []

    # Octagonal end bulkhead cap + recessed carry handle & purge valve on +X (mirrored to -X)
    end_cap_yz = [
        (-0.27, 0.16),
        (-0.39, 0.29),
        (-0.39, 0.76),
        (-0.25, 0.96),
        (0.25, 0.96),
        (0.39, 0.76),
        (0.39, 0.29),
        (0.27, 0.16),
    ]
    end_body_l.append(aa.add_extruded_profile(
        end_cap_yz,
        depth=0.042,
        axis='X',
        location=(0.745, 0.0, 0.0),
        name='End_Bulkhead_L',
        color='obsidian',
    ))
    end_body_l.append(add_tube_sweep(
        points=[
            (0.755, -0.17, 0.56),
            (0.782, -0.14, 0.53),
            (0.782, 0.14, 0.53),
            (0.755, 0.17, 0.56),
        ],
        radius=0.017,
        sides=5,
        name='End_Carry_Handle_L',
        color='steel',
    ))
    end_body_l.append(aa.add_cylinder(
        name='End_Purge_Valve_L',
        radius=0.042,
        depth=0.032,
        vertices=6,
        location=(0.768, 0.0, 0.74),
        rotation=(0.0, math.pi * 0.5, 0.0),
        shade_smooth=False,
        color='brass',
    ))
    end_sig_l.append(aa.add_box(
        name='End_Telemetry_Strip_L',
        size=(0.020, 0.24, 0.032),
        location=(0.768, 0.0, 0.38),
        color=col_magenta,
        signal=True,
    ))

    for sy_s in (-1.0, 1.0):
        # Bottom heavy shock bumper resting flat on Z = 0.000m, spanning to X=0.800, Y=±0.500
        bot_bumper = aa.add_bevel_box(
            name=f'Corner_Bumper_Bot_L_{sy_s:+.0f}',
            size=(0.17, 0.18, 0.30),
            location=(0.715, sy_s * 0.410, 0.150),
            bevel=0.018,
            segments=1,
            color='obsidian',
        )
        aa.paint_faces(bot_bumper, lambda _p, c, _n: c.z > 0.22, color='crimson_coat')
        end_body_l.append(bot_bumper)

        # Vertical corner armor column
        end_body_l.append(aa.add_box(
            name=f'Corner_Stanchion_L_{sy_s:+.0f}',
            size=(0.11, 0.11, 0.48),
            location=(0.735, sy_s * 0.435, 0.53),
            color='gunmetal',
        ))

        # Top heavy crown bumper peaking at Z = 1.100m
        end_body_l.append(aa.add_bevel_box(
            name=f'Corner_Bumper_Top_L_{sy_s:+.0f}',
            size=(0.17, 0.19, 0.30),
            location=(0.715, sy_s * 0.395, 0.925),
            taper=(0.92, 0.84),
            bevel=0.018,
            segments=1,
            color='obsidian',
        ))
        # Forged steel hoist lug peaking cleanly at Z = 1.100m
        end_body_l.append(aa.add_box(
            name=f'Corner_Hoist_Lug_L_{sy_s:+.0f}',
            size=(0.085, 0.042, 0.056),
            location=(0.710, sy_s * 0.345, 1.072),
            taper=(0.82, 0.88),
            color='steel',
        ))
        # Corner amber beacon pip
        end_sig_l.append(aa.add_box(
            name=f'Corner_Beacon_Pip_L_{sy_s:+.0f}',
            size=(0.046, 0.042, 0.030),
            location=(0.772, sy_s * 0.405, 0.985),
            color=col_amber,
            signal=True,
        ))

    body.extend(end_body_l)
    body.extend(aa.mirror_x(end_body_l, bone_swap=True))
    sig.extend(end_sig_l)
    sig.extend(aa.mirror_x(end_sig_l, bone_swap=True))

    # --- D. Mechanical Latch Clamps, Biometric Lock (-Y Front) & Top Rails ---
    for face_s in (-1.0, 1.0):  # -1.0 = primary -Y front, +1.0 = +Y rear
        for lx in (-0.51, 0.51):
            # Upper lid catch bracket
            body.append(aa.add_box(
                name=f'Latch_Upper_{face_s:+.0f}_{lx:+.2f}',
                size=(0.105, 0.044, 0.11),
                location=(lx, face_s * 0.468, 0.725),
                taper=(0.90, 0.88),
                color='steel',
            ))
            # Lower cam-lever housing
            body.append(aa.add_box(
                name=f'Latch_Lower_{face_s:+.0f}_{lx:+.2f}',
                size=(0.115, 0.048, 0.15),
                location=(lx, face_s * 0.470, 0.515),
                color='gunmetal',
            ))
            # Crimson toggle lever + twin brass tension rods
            body.append(aa.add_box(
                name=f'Latch_Lever_{face_s:+.0f}_{lx:+.2f}',
                size=(0.072, 0.022, 0.13),
                location=(lx, face_s * 0.488, 0.545),
                color='crimson_coat',
            ))
            for rod_dx in (-0.030, 0.030):
                body.append(add_strut(
                    (lx + rod_dx, face_s * 0.480, 0.56),
                    (lx + rod_dx, face_s * 0.480, 0.72),
                    radius0=0.009,
                    segments=4,
                    name=f'Latch_Rod_{face_s:+.0f}_{lx:+.2f}_{rod_dx:+.3f}',
                    color='brass',
                ))

    # Primary Front (-Y) Syndicate Biometric Lock Escutcheon
    body.append(aa.add_extruded_profile(
        octagonal_rect_2d(0.145, 0.125, 0.038, 0.038),
        depth=0.044,
        axis='Y',
        location=(0.0, -0.464, 0.630),
        name='Front_Biometric_Escutcheon',
        color='obsidian',
    ))
    body.append(build_polygonal_ring(
        radius_outer=0.076,
        radius_inner=0.050,
        thickness=0.020,
        sides=8,
        location=(0.0, -0.486, 0.630),
        rotation=(math.pi * 0.5, 0.0, 0.0),
        name='Front_Lock_Rotary_Wheel',
        color='brass',
    ))
    sig.append(aa.add_cylinder(
        name='Front_Lock_Magenta_Core',
        radius=0.044,
        depth=0.020,
        vertices=8,
        location=(0.0, -0.489, 0.630),
        rotation=(math.pi * 0.5, 0.0, 0.0),
        shade_smooth=False,
        color=col_magenta,
        signal=True,
    ))

    # Top crown tactical rails & central syndicate transponder plate
    for ry in (-0.19, 0.19):
        body.append(aa.add_box(
            name=f'Top_Tac_Rail_{ry:+.2f}',
            size=(1.12, 0.048, 0.032),
            location=(0.0, ry, 1.056),
            taper=(0.98, 0.84),
            color='steel',
        ))
    body.append(aa.add_box(
        name='Top_Transponder_Plate',
        size=(0.28, 0.24, 0.034),
        location=(0.0, 0.0, 1.055),
        taper=(0.90, 0.90),
        color='obsidian',
    ))
    sig.append(aa.add_box(
        name='Top_Transponder_AmberBar',
        size=(0.18, 0.055, 0.018),
        location=(0.0, 0.0, 1.074),
        color=col_amber_hi,
        signal=True,
    ))

    # --- E. Glowing Amber & Magenta Hazard Strips ('Signal') ---
    # Angled upper lid shoulder hazard chevrons/bars on BOTH -Y front and +Y rear shoulders
    shoulder_tilt = math.atan2(0.168, 0.257)  # matches octagonal lid upper chamfer slope
    for face_s in (-1.0, 1.0):
        rot_x = -shoulder_tilt if face_s < 0 else shoulder_tilt
        # 6 glowing slanted hazard bars across the three bays
        hazard_xs = (-0.53, -0.44, -0.18, 0.18, 0.44, 0.53)
        for idx, hx in enumerate(hazard_xs):
            is_mag = idx in (2, 3)
            sig.append(aa.add_box(
                name=f'Shoulder_Hazard_Strip_{face_s:+.0f}_{idx}',
                size=(0.072 if not is_mag else 0.22, 0.022, 0.155 if not is_mag else 0.068),
                location=(hx, face_s * 0.390, 0.912),
                rotation=(rot_x, 0.0, 0.0),
                color=col_magenta if is_mag else col_amber,
                signal=True,
            ))

        # Horizontal glowing waist hazard strips flanking the seam
        for wx in (-0.22, 0.22):
            if face_s > 0 or abs(wx) > 0.18:
                sig.append(aa.add_box(
                    name=f'Waist_Hazard_Bar_{face_s:+.0f}_{wx:+.2f}',
                    size=(0.19, 0.020, 0.034),
                    location=(wx, face_s * 0.462, 0.630 if face_s < 0 else 0.560),
                    color=col_amber if face_s < 0 else col_magenta,
                    signal=True,
                ))

    objs = aa.combine_by_role(body, sig)
    _snap_ground_z0(objs)
    return objs


# ---------------------------------------------------------------------------
# 3. Aethelgard Signal Jammer / Reactor Core (public/street/power-core.glb)
#    Footprint ~1.00m x 1.00m, Height 1.85m, Facing -Y (+Z in Three.js)
# ---------------------------------------------------------------------------

def build_power_core() -> list[bpy.types.Object]:
    aa.reset_scene()
    body: list[bpy.types.Object] = []
    sig: list[bpy.types.Object] = []

    col_cyan = '#0cf2ff'
    col_cyan_hot = '#b8ffff'
    col_mag = '#ff2288'
    col_violet = '#b23aff'
    col_amber = '#ffa21a'

    # --- A. Hexagonal Reactor Plinth, Stabilizer Claws & -Y Breaker Console (Z: 0.00..0.46m) ---
    # Rotated by pi/6 so flat faces point directly toward -Y (front) and +Y (back)
    body.append(aa.add_cylinder(
        name='Reactor_Hex_Plinth',
        radius=0.44,
        radius_top=0.41,
        depth=0.14,
        vertices=6,
        location=(0.0, 0.0, 0.070),
        rotation=(0.0, 0.0, math.pi / 6.0),
        bevel=0.014,
        bevel_segments=1,
        shade_smooth=False,
        color='asphalt_dark',
    ))

    # 6 radial stabilizer feet at 0, 60, 120, 180, 240, 300 deg (X reaches ±0.500m cleanly)
    for i in range(6):
        ang = (i / 6.0) * math.tau
        ca, sa = math.cos(ang), math.sin(ang)
        body.append(aa.add_box(
            name=f'Reactor_Claw_Foot_{i}',
            size=(0.15, 0.11, 0.11),
            location=(ca * 0.425, sa * 0.425, 0.055),
            rotation=(0.0, 0.0, ang),
            taper=(0.76, 0.84),
            color='obsidian',
        ))

    # Front (-Y) Aethelgard Breaker & Diagnostic Console + Rear (+Y) Coolant Manifold (Y reaches ±0.500m)
    body.append(aa.add_bevel_box(
        name='Front_Breaker_Console',
        size=(0.32, 0.14, 0.30),
        location=(0.0, -0.430, 0.150),
        taper=(0.88, 0.78),
        bevel=0.012,
        segments=1,
        color='navy_coat',
    ))
    sig.append(aa.add_box(
        name='Front_Console_Readout',
        size=(0.20, 0.018, 0.075),
        location=(0.0, -0.492, 0.215),
        rotation=(-0.16, 0.0, 0.0),
        color=col_cyan,
        signal=True,
    ))
    sig.append(aa.add_box(
        name='Front_Console_WarnBar',
        size=(0.16, 0.018, 0.026),
        location=(0.0, -0.496, 0.135),
        color=col_amber,
        signal=True,
    ))

    body.append(aa.add_bevel_box(
        name='Rear_Coolant_Manifold',
        size=(0.28, 0.14, 0.28),
        location=(0.0, 0.430, 0.140),
        bevel=0.012,
        segments=1,
        color='teal_tech',
    ))
    for side_s in (-1.0, 1.0):
        body.append(add_strut(
            (side_s * 0.09, 0.46, 0.27),
            (side_s * 0.09, 0.33, 0.42),
            radius0=0.022,
            segments=5,
            name=f'Rear_Coolant_Pipe_{side_s:+.0f}',
            color='copper',
        ))

    # Lower magnetic injector dish & copper superconducting coil
    body.append(aa.add_cylinder(
        name='Lower_Injector_Dish',
        radius=0.39,
        radius_top=0.31,
        depth=0.22,
        vertices=6,
        location=(0.0, 0.0, 0.25),
        rotation=(0.0, 0.0, math.pi / 6.0),
        bevel=0.012,
        bevel_segments=1,
        shade_smooth=False,
        color='gunmetal',
    ))
    body.append(build_polygonal_ring(
        radius_outer=0.385,
        radius_inner=0.335,
        thickness=0.044,
        sides=6,
        phase=math.pi / 6.0,
        location=(0.0, 0.0, 0.24),
        name='Lower_Copper_Coil',
        color='copper',
    ))
    body.append(aa.add_cone(
        name='Lower_Cathode_Spike',
        radius1=0.085,
        radius2=0.024,
        depth=0.14,
        vertices=6,
        location=(0.0, 0.0, 0.42),
        shade_smooth=False,
        color='brass',
    ))
    sig.append(build_polygonal_ring(
        radius_outer=0.255,
        radius_inner=0.185,
        thickness=0.024,
        sides=6,
        phase=math.pi / 6.0,
        location=(0.0, 0.0, 0.365),
        name='Lower_Injector_GlowRing',
        color=col_cyan,
        signal=True,
    ))

    # --- B. Armoured 6-Rib Hex Cage (Z: 0.34..1.52m, wide open -Y/+Y view windows) ---
    rib_profile_rz = [
        (0.365, 0.35),
        (0.420, 0.35),
        (0.478, 0.66),
        (0.486, 0.95),
        (0.478, 1.24),
        (0.420, 1.51),
        (0.365, 1.51),
        (0.415, 1.22),
        (0.424, 0.95),
        (0.415, 0.68),
    ]
    for i in range(6):
        ang = (i / 6.0) * math.tau  # 0, 60, 120, 180, 240, 300 -> unobstructed -Y (-90 deg) window!
        rib = aa.add_extruded_profile(
            rib_profile_rz,
            depth=0.068,
            axis='Y',
            rotation=(0.0, 0.0, ang),
            name=f'Hex_Cage_Rib_{i}',
            color='gunmetal',
        )
        aa.paint_faces(rib, lambda _p, c, _n: 0.78 < c.z < 1.12, color='slate')
        body.append(rib)

        # Brass field-pinch collar & inward glowing amber pinch electrode at Z = 0.95m
        ca, sa = math.cos(ang), math.sin(ang)
        body.append(aa.add_box(
            name=f'Cage_Pinch_Clamp_{i}',
            size=(0.072, 0.086, 0.11),
            location=(ca * 0.452, sa * 0.452, 0.95),
            rotation=(0.0, 0.0, ang),
            taper=(0.90, 0.88),
            color='brass',
        ))
        sig.append(aa.add_box(
            name=f'Cage_Pinch_Emitter_{i}',
            size=(0.044, 0.036, 0.046),
            location=(ca * 0.398, sa * 0.398, 0.95),
            rotation=(0.0, 0.0, ang),
            color=col_amber,
            signal=True,
        ))

    # Lower & Upper heavy hexagonal cage locking collars
    for cz, cname in ((0.39, 'Cage_Collar_Lower'), (1.47, 'Cage_Collar_Upper')):
        body.append(build_polygonal_ring(
            radius_outer=0.435,
            radius_inner=0.355,
            thickness=0.056,
            sides=6,
            profile_mode='box',
            location=(0.0, 0.0, cz),
            name=cname,
            color='obsidian',
        ))

    # --- C. Upper Containment Cap & Jammer Emitter Crown (Z: 1.46..1.850m) ---
    body.append(aa.add_cylinder(
        name='Upper_Containment_Cap',
        radius=0.32,
        radius_top=0.39,
        depth=0.18,
        vertices=6,
        location=(0.0, 0.0, 1.56),
        rotation=(0.0, 0.0, math.pi / 6.0),
        bevel=0.012,
        bevel_segments=1,
        shade_smooth=False,
        color='navy_coat',
    ))
    body.append(build_polygonal_ring(
        radius_outer=0.382,
        radius_inner=0.332,
        thickness=0.040,
        sides=6,
        phase=math.pi / 6.0,
        location=(0.0, 0.0, 1.58),
        name='Upper_Copper_Coil',
        color='copper',
    ))
    body.append(aa.add_cone(
        name='Upper_Anode_Spike',
        radius1=0.024,
        radius2=0.082,
        depth=0.14,
        vertices=6,
        location=(0.0, 0.0, 1.44),
        shade_smooth=False,
        color='brass',
    ))
    sig.append(build_polygonal_ring(
        radius_outer=0.250,
        radius_inner=0.182,
        thickness=0.022,
        sides=6,
        phase=math.pi / 6.0,
        location=(0.0, 0.0, 1.495),
        name='Upper_Containment_GlowRing',
        color=col_cyan,
        signal=True,
    ))

    # Crown jammer hub + 6 angled radiator prongs peaking cleanly at Z = 1.850m
    body.append(aa.add_cylinder(
        name='Crown_Jammer_Hub',
        radius=0.25,
        radius_top=0.19,
        depth=0.10,
        vertices=6,
        location=(0.0, 0.0, 1.70),
        rotation=(0.0, 0.0, math.pi / 6.0),
        bevel=0.010,
        bevel_segments=1,
        shade_smooth=False,
        color='obsidian',
    ))
    sig.append(build_polygonal_ring(
        radius_outer=0.215,
        radius_inner=0.155,
        thickness=0.024,
        sides=6,
        phase=math.pi / 6.0,
        location=(0.0, 0.0, 1.755),
        name='Crown_Jammer_MagentaRing',
        color=col_mag,
        signal=True,
    ))

    prong_rz = [
        (0.18, 1.66),
        (0.27, 1.66),
        (0.30, 1.81),
        (0.22, 1.850),
        (0.19, 1.76),
    ]
    for i in range(6):
        ang = (i / 6.0) * math.tau
        body.append(aa.add_extruded_profile(
            prong_rz,
            depth=0.036,
            axis='Y',
            rotation=(0.0, 0.0, ang),
            name=f'Crown_Jammer_Prong_{i}',
            color='silver',
        ))

    sig.append(add_crystal_shard(
        name='Crown_Jammer_EmitterTip',
        radius=0.055,
        bot_len=0.04,
        waist_h=0.04,
        top_len=0.095,
        sides=6,
        location=(0.0, 0.0, 1.755),
        color=col_cyan,
        gradient_z=(col_cyan, '#ffffff'),
        signal=True,
    ))

    # --- D. Suspended Glowing Plasma Torus & Vertical Lightning Core ('Signal', Z: 0.44..1.46m) ---
    # 1. Central blazing vertical lightning spine
    sig.append(aa.add_cylinder(
        name='Lightning_Core_Column',
        radius=0.036,
        depth=0.98,
        vertices=8,
        location=(0.0, 0.0, 0.95),
        shade_smooth=False,
        color=col_cyan_hot,
        signal=True,
    ))
    aa.paint_mesh(sig[-1], color=col_cyan_hot, signal=True, gradient_z=('#18e8ff', '#ffffff'))

    # 2. Three helical jagged 3D vertical lightning arcs spiraling around the core
    for arc_i in range(3):
        base_ang = (arc_i / 3.0) * math.tau + 0.35
        arc_pts = []
        n_steps = 6
        for k in range(n_steps + 1):
            u = k / n_steps
            z = 0.46 + u * 0.98
            envelope = math.sin(u * math.pi)
            jag = (1.0 if k % 2 == 1 else -0.55) * envelope
            rad = 0.026 + (0.088 + 0.028 * jag) * envelope
            ang = base_ang + u * (math.pi * 1.65) + (0.28 if k % 2 == 1 else -0.22) * envelope
            arc_pts.append((math.cos(ang) * rad, math.sin(ang) * rad, z))

        is_violet = arc_i == 2
        sig.append(add_tube_sweep(
            points=arc_pts,
            radius=0.013,
            sides=4,
            name=f'Vertical_Lightning_Arc_{arc_i}',
            color=col_violet if is_violet else col_cyan,
            gradient_z=(col_mag, '#ffd6ff') if is_violet else ('#00b4ff', '#d6ffff'),
            signal=True,
        ))

    # 3. Suspended Primary Glowing Plasma Torus + Counter-Rotating Flux Rings + Core Pinch Crystal
    sig.append(aa.add_torus(
        name='Suspended_Plasma_Torus',
        major_radius=0.242,
        minor_radius=0.048,
        major_segments=14,
        minor_segments=5,
        location=(0.0, 0.0, 0.95),
        shade_smooth=True,
        color=col_cyan,
        signal=True,
    ))
    sig.append(build_polygonal_ring(
        radius_outer=0.332,
        radius_inner=0.304,
        thickness=0.020,
        sides=6,
        phase=math.pi / 6.0,
        location=(0.0, 0.0, 0.95),
        name='Plasma_Outer_FluxRing',
        color=col_amber,
        signal=True,
    ))
    for gi, tilt in enumerate(((0.46, 0.22, 0.0), (-0.46, -0.22, 0.55))):
        sig.append(build_polygonal_ring(
            radius_outer=0.205,
            radius_inner=0.184,
            thickness=0.015,
            sides=10,
            location=(0.0, 0.0, 0.95),
            rotation=tilt,
            name=f'Plasma_Gyro_Ribbon_{gi}',
            color=col_mag,
            signal=True,
        ))
    sig.append(add_crystal_shard(
        name='Plasma_Heart_PinchCore',
        radius=0.088,
        bot_len=0.13,
        waist_h=0.09,
        top_len=0.13,
        sides=6,
        twist=0.24,
        location=(0.0, 0.0, 0.95),
        color=col_cyan_hot,
        gradient_z=(col_cyan, '#ffffff'),
        signal=True,
    ))

    objs = aa.combine_by_role(body, sig)
    _snap_ground_z0(objs)
    return objs


# ---------------------------------------------------------------------------
# 4. Corp Broadcast Spire (public/street/beacon.glb)
#    Footprint ~1.30m x 1.30m, Height 2.90m, Facing -Y (+Z in Three.js)
# ---------------------------------------------------------------------------

def build_beacon() -> list[bpy.types.Object]:
    aa.reset_scene()
    body: list[bpy.types.Object] = []
    sig: list[bpy.types.Object] = []

    col_cyan = '#0cecff'
    col_cyan_deep = '#0088e8'
    col_crimson = '#ff1238'
    col_crimson_hi = '#ff6e88'

    # --- A. Corporate Monument Plinth & Balanced Radial Outriggers (Z: 0.00..0.42m) ---
    body.append(aa.add_cylinder(
        name='Spire_Hex_Plinth_Base',
        radius=0.56,
        radius_top=0.51,
        depth=0.095,
        vertices=6,
        location=(0.0, 0.0, 0.0475),
        bevel=0.012,
        bevel_segments=1,
        shade_smooth=False,
        color='obsidian',
    ))
    body.append(aa.add_cylinder(
        name='Spire_Hex_Plinth_Step',
        radius=0.45,
        radius_top=0.38,
        depth=0.11,
        vertices=6,
        location=(0.0, 0.0, 0.145),
        rotation=(0.0, 0.0, math.pi / 6.0),
        bevel=0.010,
        bevel_segments=1,
        shade_smooth=False,
        color='gunmetal',
    ))

    # 3 primary blade outrigger spurs (-90 deg = -Y front, +30, +150) + 3 inter-blade stabilizers (+90, -30, -150)
    # Plus lateral stabilizer cheeks at 0 and 180 deg -> crisp centered ~1.30m x 1.30m footprint!
    for i in range(6):
        ang = -math.pi * 0.5 + (i / 6.0) * math.tau
        ca, sa = math.cos(ang), math.sin(ang)
        is_primary = (i % 2 == 0)
        reach = 0.555 if is_primary else 0.570
        body.append(aa.add_box(
            name=f'Spire_Outrigger_Foot_{i}',
            size=(0.19 if is_primary else 0.16, 0.14 if is_primary else 0.12, 0.12 if is_primary else 0.095),
            location=(ca * reach, sa * reach, 0.060 if is_primary else 0.0475),
            rotation=(0.0, 0.0, ang),
            taper=(0.74, 0.84),
            color='gunmetal' if is_primary else 'obsidian',
        ))
        if is_primary:
            sig.append(aa.add_box(
                name=f'Spire_Outrigger_Pip_{i}',
                size=(0.055, 0.068, 0.022),
                location=(ca * 0.618, sa * 0.618, 0.088),
                rotation=(0.0, 0.0, ang),
                color=col_crimson if i == 0 else col_cyan,
                signal=True,
            ))

    for sx in (-1.0, 1.0):
        body.append(aa.add_box(
            name=f'Spire_Lateral_Anchor_{sx:+.0f}',
            size=(0.16, 0.18, 0.092),
            location=(sx * 0.570, 0.0, 0.046),
            taper=(0.78, 0.84),
            color='obsidian',
        ))

    # Front (-Y) Corporate Crest & Uplink Escutcheon marking primary street face
    body.append(aa.add_bevel_box(
        name='Corp_Front_Crest_Plaque',
        size=(0.24, 0.085, 0.44),
        location=(0.0, -0.455, 0.44),
        rotation=(-0.18, 0.0, 0.0),
        bevel=0.010,
        segments=1,
        color='obsidian',
    ))
    body.append(aa.add_box(
        name='Corp_Front_Brass_Trim',
        size=(0.19, 0.024, 0.36),
        location=(0.0, -0.495, 0.44),
        rotation=(-0.18, 0.0, 0.0),
        color='brass',
    ))
    sig.append(add_crystal_shard(
        name='Corp_Front_Crest_Diamond',
        radius=0.046,
        bot_len=0.11,
        waist_h=0.06,
        top_len=0.13,
        sides=4,
        twist=0.0,
        location=(0.0, -0.506, 0.45),
        rotation=(-0.18, 0.0, math.pi * 0.25),
        color=col_crimson,
        signal=True,
    ))

    # --- B. Central Dark-Carbon Waveguide Core Column (Z: 0.18..2.30m) ---
    body.append(add_lathe(
        profile_rz=[
            (0.26, 0.18),
            (0.20, 0.62),
            (0.135, 1.45),
            (0.150, 2.02),
            (0.185, 2.28),
        ],
        segments=6,
        phase=math.pi / 6.0,
        name='Spire_Core_Waveguide',
        color='obsidian',
    ))

    # 3 vertical glowing cyan plasma veins nestled in the valleys between the 3 blades (+90, -30, -150 deg)
    for vi in range(3):
        vang = math.pi * 0.5 + (vi / 3.0) * math.tau
        ca, sa = math.cos(vang), math.sin(vang)
        sig.append(add_tube_sweep(
            points=[
                (ca * 0.21, sa * 0.21, 0.32),
                (ca * 0.16, sa * 0.16, 0.95),
                (ca * 0.135, sa * 0.135, 1.55),
                (ca * 0.155, sa * 0.155, 2.18),
            ],
            radius=0.015,
            sides=4,
            name=f'Spire_Waveguide_Vein_{vi}',
            color=col_cyan,
            gradient_z=(col_cyan_deep, '#9efeff'),
            signal=True,
        ))

    # --- C. Sleek Tri-Blade Corporate Pylon (-90 deg = -Y front, +30 deg, +150 deg) ---
    # Lofted 6-vertex razor-blade cross-section along local +X, then rotated to each blade azimuth
    blade_stations = [
        # (z, r_in, r_mid, r_out, half_thick)
        (0.08, 0.20, 0.44, 0.61, 0.056),
        (0.48, 0.17, 0.33, 0.45, 0.048),
        (1.02, 0.14, 0.24, 0.32, 0.040),
        (1.52, 0.125, 0.20, 0.265, 0.036),
        (2.04, 0.145, 0.235, 0.315, 0.038),
        (2.38, 0.195, 0.255, 0.315, 0.034),
        (2.58, 0.205, 0.240, 0.275, 0.026),
        (2.72, 0.135, 0.158, 0.180, 0.014),
    ]
    blade_rings = []
    edge_glow_pts = []
    for z, r_in, r_mid, r_out, ht in blade_stations:
        blade_rings.append([
            (r_in, -ht * 0.62, z),
            (r_mid, -ht, z),
            (r_out, 0.0, z),
            (r_mid, ht, z),
            (r_in, ht * 0.62, z),
            (r_in - 0.012, 0.0, z),
        ])
        if 0.20 <= z <= 2.60:
            edge_glow_pts.append((r_out + 0.008, 0.0, z))

    for bi in range(3):
        bang = -math.pi * 0.5 + (bi / 3.0) * math.tau
        blade = build_lofted_solid(
            blade_rings,
            name=f'Corp_Pylon_Blade_{bi}',
            rotation=(0.0, 0.0, bang),
            color='warm_white',
            gradient_z=('slate', 'warm_white'),
            tip_point=(0.125, 0.0, 2.75),
        )
        body.append(blade)

        # Glowing outer razor light-spine along each blade
        rot_m = Euler((0.0, 0.0, bang), 'XYZ').to_matrix()
        world_glow_pts = [tuple(rot_m @ Vector(p)) for p in edge_glow_pts]
        sig.append(add_tube_sweep(
            points=world_glow_pts,
            radius=0.012,
            sides=4,
            name=f'Corp_Blade_LightEdge_{bi}',
            color=col_cyan,
            gradient_z=(col_cyan_deep, col_crimson_hi if bi == 0 else '#94fdff'),
            signal=True,
        ))

    # --- D. 4 Tiered Emitter Rings (Z = 0.78, 1.28, 1.76, 2.22m) ---
    tier_specs = [
        (0.78, 0.435, 0.365, col_cyan, 'gunmetal'),
        (1.28, 0.355, 0.292, col_cyan, 'gunmetal'),
        (1.76, 0.320, 0.262, '#4cf6ff', 'brass'),
        (2.22, 0.350, 0.285, col_crimson, 'brass'),
    ]
    for ti, (tz, r_out, r_in, sig_col, collar_col) in enumerate(tier_specs):
        body.append(build_polygonal_ring(
            radius_outer=r_out * 0.96,
            radius_inner=r_in * 0.94,
            thickness=0.044,
            sides=9,
            profile_mode='box',
            location=(0.0, 0.0, tz),
            name=f'Emitter_Tier_Collar_{ti}',
            color=collar_col,
        ))
        sig.append(build_polygonal_ring(
            radius_outer=r_out + 0.020,
            radius_inner=r_out - 0.018,
            thickness=0.026,
            sides=9,
            location=(0.0, 0.0, tz),
            name=f'Emitter_Tier_GlowRing_{ti}',
            color=sig_col,
            signal=True,
        ))
        # 3 inter-blade phase emitter pods per ring
        for ni in range(3):
            nang = math.pi * 0.5 + (ni / 3.0) * math.tau
            body.append(aa.add_box(
                name=f'Emitter_Tier_Node_{ti}_{ni}',
                size=(0.056, 0.048, 0.062),
                location=(math.cos(nang) * r_out, math.sin(nang) * r_out, tz),
                rotation=(0.0, 0.0, nang),
                color='obsidian',
            ))

    # --- E. Blazing Cyan/Crimson Crown Beacon & Apex Finial (Z: 2.28..2.900m) ---
    crown_z = 2.52
    # Blazing crimson double-terminated broadcast core crystal
    sig.append(add_crystal_shard(
        name='Crown_Crimson_Core',
        radius=0.125,
        bot_len=0.21,
        waist_h=0.14,
        top_len=0.22,
        sides=6,
        twist=0.18,
        location=(0.0, 0.0, crown_z),
        color=col_crimson,
        gradient_z=('#d90429', '#ff8fa3'),
        signal=True,
    ))

    # Horizontal blazing cyan halo + two intersecting crimson/cyan gimbal rings
    sig.append(build_polygonal_ring(
        radius_outer=0.258,
        radius_inner=0.228,
        thickness=0.022,
        sides=12,
        location=(0.0, 0.0, crown_z),
        name='Crown_Cyan_BroadcastHalo',
        color=col_cyan,
        signal=True,
    ))
    sig.append(build_polygonal_ring(
        radius_outer=0.205,
        radius_inner=0.182,
        thickness=0.016,
        sides=9,
        location=(0.0, 0.0, crown_z),
        rotation=(1.08, 0.25, 0.0),
        name='Crown_Crimson_MeridianRing',
        color=col_crimson,
        signal=True,
    ))
    sig.append(build_polygonal_ring(
        radius_outer=0.195,
        radius_inner=0.174,
        thickness=0.015,
        sides=9,
        location=(0.0, 0.0, crown_z),
        rotation=(-1.05, 0.28, 1.05),
        name='Crown_Cyan_MeridianRing',
        color='#8cfbff',
        signal=True,
    ))

    # Upper coupler ring + blazing crimson/white spire needle peaking cleanly at Z = 2.900m
    body.append(build_polygonal_ring(
        radius_outer=0.115,
        radius_inner=0.068,
        thickness=0.032,
        sides=6,
        location=(0.0, 0.0, 2.745),
        name='Crown_Apex_Coupler',
        color='brass',
    ))
    sig.append(add_crystal_shard(
        name='Crown_Apex_SpireNeedle',
        radius=0.044,
        bot_len=0.035,
        waist_h=0.035,
        top_len=0.145,
        sides=6,
        twist=0.08,
        location=(0.0, 0.0, 2.755),
        color=col_crimson,
        gradient_z=(col_crimson, '#ffffff'),
        signal=True,
    ))

    objs = aa.combine_by_role(body, sig)
    _snap_ground_z0(objs)
    return objs


# ---------------------------------------------------------------------------
# 5. Hardlight Traffic & Sector Barrier (public/street/barricade.glb)
#    Dimensions ~2.30m wide x 0.80m deep x 1.25m tall, Facing -Y (+Z in Three.js)
# ---------------------------------------------------------------------------

def build_barricade() -> list[bpy.types.Object]:
    aa.reset_scene()
    body: list[bpy.types.Object] = []
    sig: list[bpy.types.Object] = []

    col_concrete = (0.255, 0.290, 0.345)
    col_cyan = '#08ecff'
    col_cyan_deep = '#007ad9'
    col_amber = '#ffa014'
    col_crimson = '#ff1c54'

    # --- A. Heavy Ferro-Concrete Jersey Curb & End Bastion Anchors (Z: 0.00..0.50m) ---
    curb_profile_yz = [
        (-0.375, 0.000),
        (-0.375, 0.080),
        (-0.245, 0.265),
        (-0.205, 0.470),
        (0.055, 0.470),
        (0.265, 0.255),
        (0.345, 0.085),
        (0.345, 0.000),
    ]
    curb = aa.add_extruded_profile(
        curb_profile_yz,
        depth=1.42,
        axis='X',
        bevel=0.014,
        bevel_segments=1,
        name='Concrete_Jersey_Curb',
        color=col_concrete,
    )
    aa.paint_faces(curb, lambda _p, c, _n: c.z < 0.075, color='asphalt_dark')
    body.append(curb)

    # Central steel road-grip clamp shoes & hazard bumper blocks
    for sx in (-0.44, 0.0, 0.44):
        body.append(aa.add_bevel_box(
            name=f'Road_Anchor_Shoe_{sx:+.2f}',
            size=(0.18, 0.78, 0.072),
            location=(sx, -0.01, 0.036),
            bevel=0.008,
            segments=1,
            color='obsidian',
        ))
    for sx in (-0.44, 0.44):
        body.append(aa.add_bevel_box(
            name=f'Front_Toe_Bumper_{sx:+.2f}',
            size=(0.24, 0.065, 0.095),
            location=(sx, -0.315, 0.155),
            rotation=(-0.48, 0.0, 0.0),
            bevel=0.008,
            segments=1,
            color='hazard_yellow',
        ))

    # Left (+X) & Mirrored Right (-X) Heavy Concrete/Steel Bastion Pods + Stanchions + Rear Braces
    side_body_l: list[bpy.types.Object] = []
    side_sig_l: list[bpy.types.Object] = []

    # Heavy end bastion anchor spanning X: 0.70..1.150m, Y: -0.400..+0.400m, Z: 0.000..0.49m
    bastion_yz = [
        (-0.400, 0.000),
        (-0.400, 0.110),
        (-0.275, 0.490),
        (0.140, 0.490),
        (0.400, 0.210),
        (0.400, 0.000),
    ]
    bastion_l = aa.add_extruded_profile(
        bastion_yz,
        depth=0.45,
        axis='X',
        location=(0.925, 0.0, 0.0),
        bevel=0.016,
        bevel_segments=1,
        name='Bastion_Anchor_L',
        color='gunmetal',
    )
    aa.paint_faces(bastion_l, lambda _p, c, _n: 0.09 < c.z < 0.36 and abs(c.x) < 1.08, color=col_concrete)
    side_body_l.append(bastion_l)

    # Outer steel flank cheek & front yellow tow lug
    side_body_l.append(aa.add_bevel_box(
        name='Bastion_Outer_Armor_L',
        size=(0.065, 0.56, 0.28),
        location=(1.118, -0.02, 0.18),
        bevel=0.010,
        segments=1,
        color='obsidian',
    ))
    side_body_l.append(aa.add_bevel_box(
        name='Bastion_Tow_Lug_L',
        size=(0.13, 0.065, 0.095),
        location=(0.925, -0.375, 0.145),
        bevel=0.008,
        segments=1,
        color='hazard_yellow',
    ))

    # Raked Armored Riot-Shield Emitter Stanchion (rx = -0.30 rad -> raked back from -Y bottom to +Y top)
    shield_origin = Vector((0.0, -0.065, 0.835))
    shield_euler = Euler((-0.30, 0.0, 0.0), 'XYZ')

    side_body_l.append(aa.add_bevel_box(
        name='Shield_Stanchion_L',
        size=(0.15, 0.14, 0.78),
        location=_place_in_frame((0.975, 0.0, 0.01), shield_origin, shield_euler),
        rotation=shield_euler,
        bevel=0.012,
        segments=1,
        color='gunmetal',
    ))
    side_body_l.append(aa.add_bevel_box(
        name='Stanchion_Capacitor_Pod_L',
        size=(0.075, 0.18, 0.44),
        location=_place_in_frame((1.065, 0.02, -0.02), shield_origin, shield_euler),
        rotation=shield_euler,
        bevel=0.010,
        segments=1,
        color='teal_tech',
    ))

    # Top corner strobe hood peaking cleanly at Z = 1.250m
    side_body_l.append(aa.add_bevel_box(
        name='Strobe_Hood_L',
        size=(0.19, 0.16, 0.068),
        location=(0.975, 0.055, 1.216),
        bevel=0.010,
        segments=1,
        color='obsidian',
    ))
    side_sig_l.append(aa.add_box(
        name='Corner_Strobe_Glow_L',
        size=(0.14, 0.125, 0.042),
        location=(0.975, 0.042, 1.192),
        color=col_amber,
        signal=True,
    ))

    # Two Rear (+Y) Hydraulic A-Frame Compression Braces (outer bastion + inner curb)
    for bx, top_z_local in ((0.94, 0.22), (0.42, 0.18)):
        p_top = _place_in_frame((bx, 0.06, top_z_local), shield_origin, shield_euler)
        p_bot = Vector((bx, 0.335, 0.145))
        p_mid = p_bot.lerp(p_top, 0.52)
        side_body_l.append(add_strut(
            p_bot,
            p_mid,
            radius0=0.036,
            radius1=0.034,
            segments=6,
            name=f'Rear_Hydraulic_Cyl_L_{bx:.2f}',
            color='gunmetal',
        ))
        side_body_l.append(add_strut(
            p_mid,
            p_top,
            radius0=0.022,
            segments=6,
            name=f'Rear_Hydraulic_Rod_L_{bx:.2f}',
            color='steel',
        ))
        side_body_l.append(aa.add_cylinder(
            name=f'Rear_Hydraulic_Collar_L_{bx:.2f}',
            radius=0.042,
            depth=0.028,
            vertices=6,
            location=p_mid,
            rotation=Vector((0.0, 0.0, 1.0)).rotation_difference((p_top - p_bot).normalized()),
            shade_smooth=False,
            color='brass',
        ))

    body.extend(side_body_l)
    body.extend(aa.mirror_x(side_body_l, bone_swap=True))
    sig.extend(side_sig_l)
    sig.extend(aa.mirror_x(side_sig_l, bone_swap=True))

    # --- B. Raked Riot-Shield Emitter Rails & Ballistic Backing Frame ---
    # Lower emitter sill rail
    body.append(aa.add_bevel_box(
        name='Shield_Lower_Projector_Rail',
        size=(1.86, 0.12, 0.082),
        location=_place_in_frame((0.0, 0.0, -0.365), shield_origin, shield_euler),
        rotation=shield_euler,
        bevel=0.010,
        segments=1,
        color='obsidian',
    ))
    # Upper segmented armor brow with central tactical sightline step
    for sx in (-0.62, 0.62):
        body.append(aa.add_bevel_box(
            name=f'Shield_Upper_Brow_{sx:+.2f}',
            size=(0.62, 0.11, 0.072),
            location=_place_in_frame((sx, 0.0, 0.375), shield_origin, shield_euler),
            rotation=shield_euler,
            bevel=0.010,
            segments=1,
            color='gunmetal',
        ))
    body.append(aa.add_bevel_box(
        name='Shield_Center_Visor_Bar',
        size=(0.64, 0.095, 0.052),
        location=_place_in_frame((0.0, 0.0, 0.345), shield_origin, shield_euler),
        rotation=shield_euler,
        bevel=0.008,
        segments=1,
        color='obsidian',
    ))

    # Dark ballistic polycarbonate core backing + 2 vertical mullions (casts clean shadow, solid from both sides)
    body.append(aa.add_box(
        name='Ballistic_Shield_Core_Plate',
        size=(1.82, 0.020, 0.66),
        location=_place_in_frame((0.0, 0.0, 0.0), shield_origin, shield_euler),
        rotation=shield_euler,
        color='glass_dark',
    ))
    for mx in (-0.34, 0.34):
        body.append(aa.add_box(
            name=f'Shield_Mullion_{mx:+.2f}',
            size=(0.036, 0.052, 0.68),
            location=_place_in_frame((mx, 0.0, 0.0), shield_origin, shield_euler),
            rotation=shield_euler,
            color='obsidian',
        ))

    # --- C. Hardlight Energy Perimeter & Glowing Hazard Chevrons ('Signal') ---
    # All Signal elements span 38..46mm thickness through the 20mm dark core so they shine cleanly from BOTH front (-Y) and back (+Y)!
    for z_edge in (-0.312, 0.305):
        sig.append(aa.add_box(
            name=f'Hardlight_Edge_H_{z_edge:+.3f}',
            size=(1.80, 0.036, 0.026),
            location=_place_in_frame((0.0, 0.0, z_edge), shield_origin, shield_euler),
            rotation=shield_euler,
            color=col_cyan,
            signal=True,
        ))
    for x_edge in (-0.885, 0.885):
        v_bar = aa.add_box(
            name=f'Hardlight_Edge_V_{x_edge:+.3f}',
            size=(0.028, 0.036, 0.63),
            location=_place_in_frame((x_edge, 0.0, -0.004), shield_origin, shield_euler),
            rotation=shield_euler,
            color=col_cyan,
            signal=True,
        )
        aa.paint_mesh(v_bar, color=col_cyan, signal=True, gradient_z=(col_cyan_deep, '#9efeff'))
        sig.append(v_bar)

    # Subtle horizontal hardlight field scanlines across the 3 bays
    for bx in (-0.61, 0.0, 0.61):
        for sz_line in (-0.21, 0.21):
            sig.append(aa.add_box(
                name=f'Hardlight_Scanline_{bx:+.2f}_{sz_line:+.2f}',
                size=(0.48 if bx != 0.0 else 0.58, 0.030, 0.014),
                location=_place_in_frame((bx, 0.0, sz_line), shield_origin, shield_euler),
                rotation=shield_euler,
                color=col_cyan_deep,
                signal=True,
            ))

    # 4 Bold Directional Inward-Pointing Glowing Hazard Chevrons (> > on +X, < < on -X) + Central Crimson Stop Diamond!
    # Profile in (X, Z) extruded along Y (depth=0.046m proud on both front & rear faces of shield)
    def make_chevron_xz(dir_sign: float, half_h: float = 0.22, tip_dx: float = 0.11, bar_w: float = 0.068):
        # Points inward toward X = 0 when dir_sign = -1 on +X side, or +1 on -X side
        return [
            (-dir_sign * (tip_dx * 0.55), -half_h),
            (-dir_sign * (tip_dx * 0.55 - bar_w), -half_h),
            (dir_sign * (tip_dx * 0.65), 0.0),
            (-dir_sign * (tip_dx * 0.55 - bar_w), half_h),
            (-dir_sign * (tip_dx * 0.55), half_h),
            (dir_sign * (tip_dx * 0.65 - bar_w), 0.0),
        ]

    chevron_specs = [
        (-0.70, +1.0, col_amber),
        (-0.50, +1.0, col_crimson),
        (0.50, -1.0, col_crimson),
        (0.70, -1.0, col_amber),
    ]
    for idx, (cx, d_sign, c_col) in enumerate(chevron_specs):
        sig.append(aa.add_extruded_profile(
            make_chevron_xz(d_sign),
            depth=0.046,
            axis='Y',
            location=_place_in_frame((cx, 0.0, -0.005), shield_origin, shield_euler),
            rotation=shield_euler,
            name=f'Hazard_Chevron_{idx}',
            color=c_col,
            signal=True,
        ))

    # Central Sector-Lock Diamond & Flanking Mini-Chevrons in center bay (X = 0)
    diamond_outer_xz = [
        (0.0, -0.20),
        (0.15, 0.0),
        (0.0, 0.20),
        (-0.15, 0.0),
    ]
    sig.append(aa.add_extruded_profile(
        diamond_outer_xz,
        depth=0.044,
        axis='Y',
        location=_place_in_frame((0.0, 0.0, -0.005), shield_origin, shield_euler),
        rotation=shield_euler,
        name='Center_Hazard_Diamond',
        color=col_crimson,
        signal=True,
    ))
    diamond_inner_xz = [
        (0.0, -0.10),
        (0.075, 0.0),
        (0.0, 0.10),
        (-0.075, 0.0),
    ]
    sig.append(aa.add_extruded_profile(
        diamond_inner_xz,
        depth=0.052,
        axis='Y',
        location=_place_in_frame((0.0, 0.0, -0.005), shield_origin, shield_euler),
        rotation=shield_euler,
        name='Center_Hazard_CoreGold',
        color='#ffd238',
        signal=True,
    ))
    for cx_mid, d_sign in ((-0.21, +1.0), (0.21, -1.0)):
        sig.append(aa.add_extruded_profile(
            make_chevron_xz(d_sign, half_h=0.145, tip_dx=0.075, bar_w=0.048),
            depth=0.044,
            axis='Y',
            location=_place_in_frame((cx_mid, 0.0, -0.005), shield_origin, shield_euler),
            rotation=shield_euler,
            name=f'Inner_Hazard_Chevron_{cx_mid:+.2f}',
            color=col_amber,
            signal=True,
        ))

    objs = aa.combine_by_role(body, sig)
    _snap_ground_z0(objs)
    return objs


# ---------------------------------------------------------------------------
# Export & Strict Self-Verification Entry Point
# ---------------------------------------------------------------------------

PROP_SPECS = [
    # (filename, builder, target_w, target_d, target_h)
    ('terminal.glb', build_terminal, 1.20, 0.90, 2.20),
    ('supply-crate.glb', build_supply_crate, 1.60, 1.00, 1.10),
    ('power-core.glb', build_power_core, 1.00, 1.00, 1.85),
    ('beacon.glb', build_beacon, 1.30, 1.30, 2.90),
    ('barricade.glb', build_barricade, 2.30, 0.80, 1.25),
]


def main() -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    min_tris = 900
    max_tris = 2200

    for filename, builder, target_w, target_d, target_h in PROP_SPECS:
        objs = builder()
        names = [o.name for o in objs]
        assert names == ['Body', 'Signal'], f'{filename}: expected exact [Body, Signal] meshes, got {names}'

        body_tris = aa.count_triangles(objs[0])
        sig_tris = aa.count_triangles(objs[1])
        total_tris = body_tris + sig_tris
        assert sig_tris >= 80, f'{filename}: Signal mesh too sparse ({sig_tris} tris)'
        assert min_tris <= total_tris <= max_tris, (
            f'{filename}: total tris {total_tris} (Body={body_tris}, Signal={sig_tris}) '
            f'outside required {min_tris}..{max_tris}'
        )

        bmin, bmax = _bounds_world(objs)
        dims = bmax - bmin
        assert abs(bmin.z) <= 1e-4, f'{filename}: expected resting on Z=0, got bmin.z={bmin.z:.5f}'
        assert abs(dims.x - target_w) <= 0.12, f'{filename}: width {dims.x:.3f}m far from ~{target_w:.2f}m'
        assert abs(dims.y - target_d) <= 0.12, f'{filename}: depth {dims.y:.3f}m far from ~{target_d:.2f}m'
        assert abs(dims.z - target_h) <= 0.08, f'{filename}: height {dims.z:.3f}m far from ~{target_h:.2f}m'

        out_path = OUT_DIR / filename
        info = aa.export_glb(
            out_path,
            objs,
            min_tris=min_tris,
            max_meshes=2,
        )
        assert info['meshes'] == 2, f'{filename}: exported mesh count {info["meshes"]} != 2'
        assert min_tris <= info['triangles'] <= max_tris, (
            f'{filename}: exported tris {info["triangles"]} outside {min_tris}..{max_tris}'
        )

        print(
            f"[OK] {filename:17s} | {info['triangles']:4d} tris (Body={body_tris:4d}, Signal={sig_tris:3d}) | "
            f"{info['meshes']} meshes | {info['size_kb']:5.1f} KB | "
            f"W={dims.x:.3f}m D={dims.y:.3f}m H={dims.z:.3f}m (Z={bmin.z:.4f}..{bmax.z:.3f})"
        )


if __name__ == '__main__':
    try:
        main()
    except Exception:
        import traceback
        traceback.print_exc()
        sys.exit(1)
