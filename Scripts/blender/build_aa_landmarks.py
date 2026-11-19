"""Sculpt and export two Neo-Kyoto AA street landmark GLBs for Komorebi.

Run headless with:
  .test/blender/blender --background --disable-autoexec --python tools/build_aa_landmarks.py

Outputs:
  1. public/street/noodle-stall.glb  (Neo-Kyoto Night Ramen & Tea Stall, ~1,600-3,200 tris)
  2. public/street/echo-shrine.glb   (Old Kyoto Memory Echo Pedestal, ~1,100-2,200 tris)

Coordinate convention (Blender Z-up -> Three.js Y-up via export_yup=True):
  * X: width (-X right, +X left)
  * -Y: front / primary street-facing direction (+Z in Three.js)
  * +Z: height above ground (Z = 0.0 rests flat on terrain)
"""

from __future__ import annotations

import math
import sys
from pathlib import Path
from typing import Sequence

import bmesh
import bpy
from mathutils import Euler, Vector

TOOLS_DIR = Path(__file__).resolve().parent
ROOT_DIR = TOOLS_DIR.parent
OUT_DIR = ROOT_DIR / 'public' / 'street'

if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

import blender_aa_lib as aa


# ---------------------------------------------------------------------------
# Bespoke Closed-Manifold Sculpting Helpers (FrontSide Culling Safe)
# ---------------------------------------------------------------------------

def add_strut(
    p0,
    p1,
    radius0: float = 0.02,
    radius1: float | None = None,
    segments: int = 6,
    name: str = 'Strut',
    shade_smooth: bool = True,
    color=(0.30, 0.35, 0.42),
    signal: bool = False,
    ao_strength: float = 0.26,
) -> bpy.types.Object:
    """Create a clean capped cylinder/frustum connecting 3D points p0 -> p1."""
    v0 = Vector((float(p0[0]), float(p0[1]), float(p0[2])))
    v1 = Vector((float(p1[0]), float(p1[1]), float(p1[2])))
    delta = v1 - v0
    length = delta.length
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
        depth=max(1e-4, length),
    )
    mid = (v0 + v1) * 0.5
    rot_q = Vector((0.0, 0.0, 1.0)).rotation_difference(delta.normalized())
    return aa._finalize_primitive(
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


def add_tube_sweep(
    points: Sequence[Sequence[float]],
    radius: float = 0.025,
    sides: int = 6,
    name: str = 'TubeSweep',
    shade_smooth: bool = True,
    color=(0.30, 0.35, 0.42),
    signal: bool = False,
    ao_strength: float = 0.26,
) -> bpy.types.Object:
    """Sweep a closed n-sided polygon cleanly along a 3D polyline with end caps."""
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
            tangent = (t_in + t_out)
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
        r_a, r_b = rings[k], rings[k + 1]
        for i in range(n):
            j = (i + 1) % n
            bm.faces.new((r_a[i], r_a[j], r_b[j], r_b[i]))

    for i in range(n):
        j = (i + 1) % n
        bm.faces.new((start_pole, rings[0][j], rings[0][i]))
        bm.faces.new((end_pole, rings[-1][i], rings[-1][j]))

    return aa._finalize_primitive(
        bm,
        name=name,
        shade_smooth=shade_smooth,
        smooth_angle_cos=0.78 if shade_smooth else None,
        color=color,
        signal=signal,
        ao_strength=ao_strength,
    )


def add_lathe(
    profile_rz: Sequence[tuple[float, float]],
    segments: int = 16,
    name: str = 'Lathe',
    location=(0.0, 0.0, 0.0),
    rotation=(0.0, 0.0, 0.0),
    scale=(1.0, 1.0, 1.0),
    phase: float = 0.0,
    shade_smooth: bool = True,
    smooth_angle_cos: float | None = 0.78,
    color=(0.35, 0.38, 0.44),
    gradient_z=None,
    signal: bool = False,
    ao_strength: float = 0.26,
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

    if not rings:
        bm.free()
        raise ValueError('add_lathe requires at least one ring with radius > 0')

    if bot_pole is None:
        bot_pole = bm.verts.new((0.0, 0.0, pts[0][1]))
    if top_pole is None:
        top_pole = bm.verts.new((0.0, 0.0, pts[-1][1]))

    for k in range(len(rings) - 1):
        r_a, r_b = rings[k], rings[k + 1]
        for i in range(segs):
            j = (i + 1) % segs
            bm.faces.new((r_a[i], r_a[j], r_b[j], r_b[i]))

    for i in range(segs):
        j = (i + 1) % segs
        bm.faces.new((bot_pole, rings[0][j], rings[0][i]))
        bm.faces.new((top_pole, rings[-1][i], rings[-1][j]))

    obj = aa._finalize_primitive(
        bm,
        name=name,
        location=location,
        rotation=rotation,
        scale=scale,
        shade_smooth=shade_smooth,
        smooth_angle_cos=smooth_angle_cos if shade_smooth else None,
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


def add_crystal_shard(
    name: str = 'CrystalShard',
    radius: float = 0.09,
    bot_len: float = 0.18,
    waist_h: float = 0.24,
    top_len: float = 0.34,
    sides: int = 6,
    twist: float = 0.16,
    asymmetry: float = 0.10,
    tip_offset=(0.0, 0.0),
    location=(0.0, 0.0, 0.0),
    rotation=(0.0, 0.0, 0.0),
    color='#12e8ff',
    gradient_z=None,
    signal: bool = True,
) -> bpy.types.Object:
    """Create a crisp flat-shaded double-terminated polygonal quartz/memory crystal shard."""
    n = max(4, int(sides))
    bm = bmesh.new()

    bot_v = bm.verts.new((0.0, 0.0, -float(bot_len)))
    top_v = bm.verts.new((float(tip_offset[0]), float(tip_offset[1]), float(top_len)))

    stations = [
        (-float(waist_h) * 0.48, float(radius) * 0.80, 0.0),
        (float(waist_h) * 0.42, float(radius), float(twist) * 0.55),
        (float(top_len) * 0.64, float(radius) * 0.54, float(twist)),
    ]

    rings = []
    for z, rad, ang_shift in stations:
        ring = []
        for i in range(n):
            ang = ang_shift + (i / n) * math.tau
            mod = 1.0 + asymmetry * math.cos(2.0 * ang + 0.7)
            ring.append(bm.verts.new((math.cos(ang) * rad * mod, math.sin(ang) * rad * mod, z)))
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


# ---------------------------------------------------------------------------
# 1. Neo-Kyoto Night Ramen & Tea Stall (public/street/noodle-stall.glb)
#    Footprint ~2.8m wide (X: -1.4..1.4), ~1.8m deep (Y: -0.9..0.9), ~2.7m tall (Z: 0..2.7)
#    Facing -Y in Blender (+Z in Three.js)
# ---------------------------------------------------------------------------

def build_noodle_stall() -> list[bpy.types.Object]:
    aa.reset_scene()
    body: list[bpy.types.Object] = []
    sig: list[bpy.types.Object] = []

    col_hinoki = (0.68, 0.45, 0.24)      # Warm hinoki cypress countertop & rafters
    col_cedar = (0.47, 0.28, 0.15)       # Seasoned cedar cabinet panels & slats
    col_yakisugi = (0.12, 0.085, 0.068)  # Charred dark Kyoto timber frame
    col_indigo = (0.080, 0.155, 0.320)   # Traditional藍染 noren indigo cloth
    col_ivory = (0.89, 0.86, 0.79)       # Warm cotton crest & porcelain highlight
    col_kawara = (0.088, 0.118, 0.170)   # Dark slate-blue kawara roof tile
    col_kawara_hi = (0.155, 0.195, 0.265)
    col_lacquer_red = (0.64, 0.12, 0.14) # Vermilion stool upholstery & bowl trim
    col_broth = (0.86, 0.71, 0.38)       # Rich golden tonkotsu ramen broth
    col_matcha = (0.17, 0.44, 0.24)      # Kyoto Uji tea canister green

    # --- A. Ground Plinth, Customer Duckboard & Counter Structure (Z: 0.00..0.99) ---
    body.append(aa.add_box(
        name='Stall_Plinth',
        size=(2.52, 1.22, 0.08),
        location=(0.0, 0.22, 0.04),
        color='asphalt_dark',
    ))
    body.append(aa.add_box(
        name='Customer_Duckboard_Mat',
        size=(2.28, 0.44, 0.028),
        location=(0.0, -0.64, 0.014),
        color=col_cedar,
    ))

    # Main front counter cabinet & koushi vertical wooden lattice slats
    counter_core = aa.add_box(
        name='Counter_Core',
        size=(2.42, 0.56, 0.82),
        location=(0.0, -0.06, 0.48),
        color=col_yakisugi,
    )
    aa.paint_faces(
        counter_core,
        lambda _p, _c, n: n.y < -0.7,
        color=col_cedar,
    )
    body.append(counter_core)
    for i in range(8):
        kx = -1.05 + i * 0.30
        body.append(aa.add_box(
            name=f'Koushi_Slat_{i}',
            size=(0.042, 0.032, 0.68),
            location=(kx, -0.352, 0.48),
            color=col_hinoki,
        ))
    for tz in (0.14, 0.82):
        body.append(aa.add_box(
            name=f'Counter_Trim_{tz:.2f}',
            size=(2.38, 0.048, 0.045),
            location=(0.0, -0.350, tz),
            color=col_yakisugi,
        ))
    body.append(add_strut(
        (-1.12, -0.45, 0.19),
        (1.12, -0.45, 0.19),
        radius0=0.020,
        segments=5,
        name='Footrest_Bar',
        color='brass',
    ))
    for bx in (-0.88, 0.88):
        body.append(aa.add_box(
            name=f'Footrest_Bracket_{bx:+.2f}',
            size=(0.036, 0.11, 0.036),
            location=(bx, -0.395, 0.19),
            color='gunmetal',
        ))

    for side, sx in (('L', 1.19), ('R', -1.19)):
        body.append(aa.add_box(
            name=f'Side_Wing_{side}',
            size=(0.10, 1.12, 0.82),
            location=(sx, 0.22, 0.48),
            color=col_cedar,
        ))

    # Thick warm Hinoki customer counter plank (-Y overhang) + Brushed-Steel chef prep deck
    body.append(aa.add_bevel_box(
        name='Hinoki_Customer_Counter',
        size=(2.62, 0.44, 0.075),
        location=(0.0, -0.30, 0.945),
        bevel=0.018,
        segments=1,
        color=col_hinoki,
    ))
    for side, sx in (('L', 1.22), ('R', -1.22)):
        body.append(aa.add_box(
            name=f'Hinoki_Return_{side}',
            size=(0.18, 0.56, 0.070),
            location=(sx, 0.16, 0.942),
            color=col_hinoki,
        ))
    body.append(aa.add_bevel_box(
        name='Steel_Prep_Deck',
        size=(2.28, 0.52, 0.065),
        location=(0.0, 0.17, 0.905),
        bevel=0.014,
        segments=1,
        color='steel',
    ))
    body.append(aa.add_box(
        name='Steel_Backsplash_Lip',
        size=(2.24, 0.045, 0.14),
        location=(0.0, 0.42, 0.98),
        color='silver',
    ))

    # --- B. 3 Round Stools in Front (Y = -0.71, X = -0.76, 0.00, +0.76) ---
    for idx, sx in enumerate((-0.76, 0.0, 0.76)):
        sy = -0.71
        seat = add_lathe(
            profile_rz=[
                (0.155, 0.448),
                (0.170, 0.478),
                (0.142, 0.525),
            ],
            segments=8,
            name=f'Stool_Seat_{idx}',
            location=(sx, sy, 0.0),
            color=col_lacquer_red,
        )
        aa.paint_faces(
            seat,
            lambda _p, c, n: c.z < 0.465 or n.z < -0.25,
            color=col_yakisugi,
        )
        body.append(seat)

        body.append(aa.add_torus(
            name=f'Stool_Ring_{idx}',
            major_radius=0.112,
            minor_radius=0.012,
            major_segments=8,
            minor_segments=3,
            location=(sx, sy, 0.19),
            color='chrome',
        ))
        for leg_i in range(4):
            ang = math.pi * 0.25 + leg_i * (math.pi * 0.5)
            lx = sx + math.cos(ang) * 0.135
            ly = sy + math.sin(ang) * 0.135
            body.append(add_strut(
                (lx, ly, 0.012),
                (sx + math.cos(ang) * 0.035, sy + math.sin(ang) * 0.035, 0.445),
                radius0=0.016,
                radius1=0.014,
                segments=4,
                shade_smooth=False,
                name=f'Stool_Leg_{idx}_{leg_i}',
                color='gunmetal',
            ))

    # --- C. Stall Pillars, Back Kitchen Wall, Shelving & Cyber-Utility ---
    for side, px in (('L', 1.22), ('R', -1.22)):
        for role, py in (('Front', -0.34), ('Back', 0.72)):
            body.append(aa.add_box(
                name=f'Pillar_{side}_{role}',
                size=(0.095, 0.095, 2.02),
                location=(px, py, 1.03),
                color=col_yakisugi,
            ))
        body.append(aa.add_box(
            name=f'Pillar_Shoe_{side}',
            size=(0.115, 0.115, 0.12),
            location=(px, -0.34, 0.10),
            color='gunmetal',
        ))
        sign_x = 1.0 if px > 0 else -1.0
        body.append(add_strut(
            (px - sign_x * 0.04, -0.34, 1.62),
            (px - sign_x * 0.34, -0.34, 1.96),
            radius0=0.025,
            segments=4,
            shade_smooth=False,
            name=f'Knee_Brace_{side}',
            color=col_cedar,
        ))

    body.append(aa.add_box(
        name='Rear_Cabinet_Base',
        size=(2.36, 0.34, 0.88),
        location=(0.0, 0.62, 0.48),
        color=col_yakisugi,
    ))
    body.append(aa.add_box(
        name='Rear_Timber_Backwall',
        size=(2.38, 0.09, 1.16),
        location=(0.0, 0.73, 1.48),
        color=col_cedar,
    ))
    body.append(aa.add_bevel_box(
        name='Range_Exhaust_Hood',
        size=(1.08, 0.44, 0.24),
        location=(-0.54, 0.48, 1.82),
        taper=(0.76, 0.72),
        bevel=0.015,
        segments=1,
        color='steel',
    ))
    body.append(aa.add_box(
        name='Exhaust_Riser_Duct',
        size=(0.28, 0.22, 0.18),
        location=(-0.54, 0.50, 1.96),
        color='gunmetal',
    ))

    # Neo-Kyoto rear-right utility canister + copper gas/coolant pipe manifold
    body.append(aa.add_cylinder(
        name='Coolant_Tank',
        radius=0.105,
        depth=0.58,
        vertices=6,
        location=(-1.27, 0.50, 0.33),
        color='teal_tech',
    ))
    body.append(add_tube_sweep(
        points=[(-1.27, 0.50, 0.62), (-1.27, 0.68, 0.76), (-0.98, 0.68, 0.88)],
        radius=0.018,
        sides=4,
        name='Copper_Supply_Pipe',
        color='copper',
    ))

    # Rear-center tea & bowl shelf + Uji tea canisters + stacked bowls (clear of menu board!)
    body.append(aa.add_box(
        name='Tea_Shelf_Plank',
        size=(0.56, 0.20, 0.030),
        location=(0.06, 0.60, 1.24),
        color=col_hinoki,
    ))
    for ti, (tx, tcol) in enumerate(((-0.12, col_matcha), (0.05, 'copper'))):
        canister = add_lathe(
            profile_rz=[
                (0.048, 1.255),
                (0.048, 1.385),
                (0.040, 1.390),
                (0.040, 1.416),
            ],
            segments=6,
            name=f'Tea_Canister_{ti}',
            location=(tx, 0.59, 0.0),
            color=tcol,
        )
        aa.paint_faces(canister, lambda _p, c, _n: c.z > 1.386, color='brass')
        body.append(canister)
    body.append(aa.add_cone(
        name='Shelf_Bowl_Stack',
        radius1=0.048,
        radius2=0.084,
        depth=0.098,
        vertices=7,
        location=(0.23, 0.59, 1.304),
        color=col_ivory,
    ))

    # --- D. Steaming Broth Pots, Tea Kettle & Ramen Bowls on Counter ---
    pot_specs = [
        ('Tonkotsu_Pot', -0.68, 0.16, 0.165, 0.25, 'silver', True),
        ('Dashi_Pot', -0.28, 0.20, 0.135, 0.21, 'copper', False),
    ]
    for pname, px, py, prad, ph, pcol, with_lid in pot_specs:
        sig.append(aa.add_cylinder(
            name=f'{pname}_BurnerGlow',
            radius=prad * 1.05,
            depth=0.018,
            vertices=8,
            location=(px, py, 0.942),
            color='neon_amber',
            signal=True,
        ))
        pot = add_lathe(
            profile_rz=[
                (prad * 0.95, 0.94),
                (prad, 0.94 + ph),
                (prad * 1.06, 0.94 + ph + 0.016),
                (prad * 0.86, 0.94 + ph - 0.020),
            ],
            segments=8,
            name=pname,
            location=(px, py, 0.0),
            color=pcol,
        )
        aa.paint_faces(
            pot,
            lambda _p, c, n, top_z=(0.94 + ph - 0.012): c.z >= top_z and n.z > 0.65,
            color=col_broth,
            ao_strength=0.08,
            top_highlight=0.16,
        )
        body.append(pot)

        for hs in (-1.0, 1.0):
            body.append(aa.add_box(
                name=f'{pname}_Handle_{hs:+.0f}',
                size=(0.025, 0.065, 0.022),
                location=(px + hs * (prad + 0.014), py, 0.94 + ph * 0.74),
                color='gunmetal',
            ))

        if with_lid:
            body.append(aa.add_cylinder(
                name=f'{pname}_WoodLid',
                radius=prad * 0.92,
                depth=0.024,
                vertices=8,
                location=(px - 0.05, py + 0.03, 0.94 + ph + 0.035),
                rotation=(0.14, -0.18, 0.25),
                color=col_hinoki,
            ))
            body.append(aa.add_box(
                name=f'{pname}_LidHandle',
                size=(0.18, 0.032, 0.032),
                location=(px - 0.05, py + 0.03, 0.94 + ph + 0.058),
                rotation=(0.14, -0.18, 0.25),
                color=col_cedar,
            ))

    # Floating tapered steam wisps gently curling above both simmering pots
    for si, (sx, sy, sz, rot_z) in enumerate((
        (-0.66, 0.15, 1.34, 0.18),
        (-0.74, 0.19, 1.46, -0.24),
        (-0.28, 0.19, 1.31, -0.15),
    )):
        body.append(add_crystal_shard(
            name=f'Broth_Steam_Wisp_{si}',
            radius=0.022,
            bot_len=0.065,
            waist_h=0.045,
            top_len=0.095,
            sides=4,
            twist=0.35,
            asymmetry=0.15,
            tip_offset=(0.018 if si % 2 == 0 else -0.016, -0.012),
            location=(sx, sy, sz),
            rotation=(0.10, 0.14 if si % 2 == 0 else -0.12, rot_z),
            color='warm_white',
            signal=False,
        ))

    # Cast-iron Tetsubin tea kettle on right side of prep deck
    body.append(add_lathe(
        profile_rz=[
            (0.068, 0.94),
            (0.098, 0.995),
            (0.060, 1.072),
        ],
        segments=6,
        name='Tea_Kettle',
        location=(0.18, 0.14, 0.0),
        color='obsidian',
    ))
    body.append(add_tube_sweep(
        points=[(0.18, 0.08, 1.05), (0.18, 0.14, 1.13), (0.18, 0.20, 1.05)],
        radius=0.009,
        sides=4,
        name='Tea_Kettle_Handle',
        color='brass',
    ))

    # 3 Ceramic Ramen Bowls & Tea Cups on the Front Hinoki Customer Counter
    for bi, (bx, by) in enumerate(((-0.74, -0.31), (-0.02, -0.29), (0.74, -0.32))):
        bowl = add_lathe(
            profile_rz=[
                (0.052, 0.982),
                (0.104, 1.045),
                (0.112, 1.068),
                (0.088, 1.048),
            ],
            segments=7,
            name=f'Ramen_Bowl_{bi}',
            location=(bx, by, 0.0),
            color=col_yakisugi if bi != 1 else col_lacquer_red,
        )
        aa.paint_faces(
            bowl,
            lambda _p, c, n: c.z > 1.040 and n.z > 0.55,
            color=col_broth,
            ao_strength=0.06,
            top_highlight=0.15,
        )
        body.append(bowl)

        body.append(aa.add_cylinder(
            name=f'Ramen_Topping_{bi}',
            radius=0.036,
            radius_top=0.022,
            depth=0.020,
            vertices=5,
            location=(bx + 0.015, by, 1.055),
            color=col_ivory if bi % 2 == 0 else col_matcha,
        ))
        body.append(aa.add_box(
            name=f'Chopsticks_{bi}',
            size=(0.20, 0.018, 0.010),
            location=(bx + 0.02, by - 0.065, 1.074),
            rotation=(0.0, 0.0, 0.12 if bi == 0 else -0.08),
            color=col_hinoki,
        ))
        if bi in (0, 2):
            body.append(aa.add_cylinder(
                name=f'Yunomi_Cup_{bi}',
                radius=0.030,
                radius_top=0.034,
                depth=0.068,
                vertices=6,
                location=(bx + 0.17, by + 0.03, 1.016),
                color=col_matcha if bi == 0 else col_ivory,
            ))

    # --- E. Noren Split Curtain Panels & Warm Neon Menu Boards ---
    body.append(add_strut(
        (-1.18, -0.47, 1.955),
        (1.18, -0.47, 1.955),
        radius0=0.018,
        segments=5,
        name='Noren_Bamboo_Rod',
        color=col_hinoki,
    ))
    for ni, nx in enumerate((-0.84, -0.28, 0.28, 0.84)):
        sway_rx = 0.045 if ni % 2 == 0 else -0.035
        body.append(aa.add_box(
            name=f'Noren_Panel_{ni}',
            size=(0.48, 0.024, 0.40),
            location=(nx, -0.47, 1.745),
            rotation=(sway_rx, 0.0, 0.0),
            color=col_indigo,
        ))
        body.append(aa.add_box(
            name=f'Noren_Crest_{ni}',
            size=(0.28, 0.028, 0.055),
            location=(nx, -0.47, 1.65),
            rotation=(sway_rx, 0.0, 0.0),
            color=col_ivory,
        ))

    # Main Warm Neon Menu Board (mounted on back wall clearly visible below noren!)
    menu_x, menu_y, menu_z = 0.72, 0.65, 1.34
    body.append(aa.add_box(
        name='Menu_Board_Frame',
        size=(0.86, 0.065, 0.36),
        location=(menu_x, menu_y, menu_z),
        color=col_yakisugi,
    ))
    sig.append(aa.add_box(
        name='Menu_Board_WarmGlow',
        size=(0.78, 0.024, 0.29),
        location=(menu_x, menu_y - 0.028, menu_z),
        color='#ff8c1a',
        signal=True,
    ))
    for mi in range(5):
        mx = menu_x - 0.28 + mi * 0.14
        mcol = '#ffe6a3' if mi != 0 else '#ff2468'
        sig.append(aa.add_box(
            name=f'Menu_Strip_{mi}',
            size=(0.055, 0.018, 0.22),
            location=(mx, menu_y - 0.038, menu_z),
            color=mcol,
            signal=True,
        ))

    # Secondary street-facing vertical neon menu plaque on front-left pillar
    body.append(aa.add_box(
        name='Side_Menu_Frame',
        size=(0.20, 0.048, 0.42),
        location=(1.22, -0.40, 1.30),
        color='gunmetal',
    ))
    sig.append(aa.add_box(
        name='Side_Menu_Neon',
        size=(0.14, 0.020, 0.34),
        location=(1.22, -0.422, 1.30),
        color='#ff8c1a',
        signal=True,
    ))
    sig.append(aa.add_box(
        name='Side_Menu_Accent',
        size=(0.10, 0.018, 0.045),
        location=(1.22, -0.428, 1.42),
        color='neon_magenta',
        signal=True,
    ))

    # --- F. 3 Hanging Chochin Paper Lanterns (Signal Emissive Globes + Lacquer Caps) ---
    for li, lx in enumerate((-0.96, 0.0, 0.96)):
        ly = -0.74
        lz = 1.75
        body.append(add_strut(
            (lx, ly, 1.89),
            (lx, ly, 2.02),
            radius0=0.008,
            segments=4,
            shade_smooth=False,
            name=f'Chochin_Cord_{li}',
            color='obsidian',
        ))
        for cap_name, cz in (('TopCap', lz + 0.145), ('BotCap', lz - 0.145)):
            body.append(aa.add_cylinder(
                name=f'Chochin_{cap_name}_{li}',
                radius=0.062,
                depth=0.028,
                vertices=6,
                location=(lx, ly, cz),
                color='obsidian',
            ))
        lantern_grad = ('#ff4214', '#ffc43d') if li != 1 else ('#ff2e1f', '#ffd86b')
        sig.append(add_lathe(
            profile_rz=[
                (0.056, lz - 0.14),
                (0.125, lz - 0.080),
                (0.138, lz),
                (0.125, lz + 0.080),
                (0.056, lz + 0.14),
            ],
            segments=8,
            name=f'Chochin_Glow_{li}',
            location=(lx, ly, 0.0),
            color='neon_amber',
            gradient_z=lantern_grad,
            signal=True,
        ))

    # --- G. Tiled Pitched Yatai Canopy Overhang (X: -1.40..1.40, Y: -0.90..0.90) ---
    roof_profile = [
        (-0.895, 2.00),
        (-0.875, 2.055),
        (0.02, 2.365),
        (0.875, 2.065),
        (0.895, 2.01),
        (0.855, 1.965),
        (0.02, 2.275),
        (-0.855, 1.955),
    ]
    roof_deck = aa.add_extruded_profile(
        profile_2d=roof_profile,
        depth=2.76,
        axis='X',
        name='Yatai_Roof_Deck',
        color=col_kawara,
    )
    aa.paint_faces(
        roof_deck,
        lambda _p, _c, n: n.z < -0.35,
        color=col_cedar,
    )
    body.append(roof_deck)

    # Front & rear eave fascia trim boards spanning exact 2.80m width (-1.40..+1.40) and <= ±0.90m Y
    body.append(aa.add_box(
        name='Front_Eave_Fascia',
        size=(2.80, 0.032, 0.058),
        location=(0.0, -0.873, 2.005),
        rotation=(0.35, 0.0, 0.0),
        color='teal_tech',
    ))
    body.append(aa.add_box(
        name='Rear_Eave_Fascia',
        size=(2.80, 0.032, 0.056),
        location=(0.0, 0.873, 2.015),
        rotation=(-0.35, 0.0, 0.0),
        color=col_yakisugi,
    ))

    # 5 exposed warm wood taruki rafters underneath front slope
    for ri in range(5):
        rx = -1.16 + ri * 0.58
        body.append(add_strut(
            (rx, 0.02, 2.255),
            (rx, -0.85, 1.945),
            radius0=0.021,
            radius1=0.018,
            segments=4,
            shade_smooth=False,
            name=f'Front_Rafter_{ri}',
            color=col_hinoki,
        ))

    # Raised stepped kawara round tile ribs running down front & rear roof slopes
    front_pitch = math.atan2(2.365 - 2.055, 0.02 - (-0.875))
    rear_pitch = math.atan2(2.365 - 2.065, 0.875 - 0.02)
    for ti in range(6):
        tx = -1.20 + ti * 0.48
        body.append(aa.add_box(
            name=f'Kawara_Rib_F_{ti}',
            size=(0.055, 0.93, 0.034),
            location=(tx, -0.43, 2.222),
            rotation=(front_pitch, 0.0, 0.0),
            color=col_kawara_hi,
        ))
        body.append(aa.add_box(
            name=f'Kawara_Rib_R_{ti}',
            size=(0.055, 0.89, 0.032),
            location=(tx, 0.45, 2.226),
            rotation=(-rear_pitch, 0.0, 0.0),
            color=col_kawara_hi,
        ))

    # Ornamental mune ridge beam & brass-accented onigawara end caps
    body.append(aa.add_box(
        name='Roof_Mune_Ridge',
        size=(2.78, 0.11, 0.075),
        location=(0.0, 0.02, 2.385),
        color=col_kawara_hi,
    ))
    for side, ox in (('L', 1.36), ('R', -1.36)):
        body.append(aa.add_box(
            name=f'Onigawara_Cap_{side}',
            size=(0.07, 0.15, 0.12),
            location=(ox, 0.02, 2.41),
            taper=(0.82, 0.78),
            color='brass',
        ))

    # --- H. Rooftop Neon Sign Header (Z: 2.38..2.70, facing -Y) ---
    sign_y = -0.10
    for sx in (-0.68, 0.68):
        body.append(aa.add_box(
            name=f'Sign_Strut_{sx:+.2f}',
            size=(0.055, 0.16, 0.16),
            location=(sx, sign_y + 0.06, 2.38),
            color='gunmetal',
        ))
    body.append(aa.add_bevel_box(
        name='Roof_Sign_Housing',
        size=(1.76, 0.09, 0.28),
        location=(0.0, sign_y, 2.54),
        bevel=0.016,
        segments=1,
        color='obsidian',
    ))
    for side, fx in (('L', 0.91), ('R', -0.91)):
        body.append(aa.add_box(
            name=f'Roof_Sign_Fin_{side}',
            size=(0.04, 0.11, 0.22),
            location=(fx, sign_y, 2.54),
            color='brass',
        ))

    # Emissive Neon Sign Tubes & 'ラーメン' (RAMEN) Katakana Header in 'Signal' (Z <= 2.70)
    y_neon = sign_y - 0.052
    sig.append(aa.add_box(
        name='Neon_Header_TopBar',
        size=(1.70, 0.026, 0.024),
        location=(0.0, sign_y - 0.048, 2.688),
        color='neon_magenta',
        signal=True,
    ))
    sig.append(aa.add_box(
        name='Neon_Header_BotBar',
        size=(1.68, 0.026, 0.022),
        location=(0.0, sign_y - 0.048, 2.405),
        color='neon_cyan',
        signal=True,
    ))
    # Steaming ramen bowl neon icon at left (X = -0.60)
    sig.append(aa.add_extruded_profile(
        profile_2d=[
            (-0.69, 2.535),
            (-0.51, 2.535),
            (-0.55, 2.465),
            (-0.65, 2.465),
        ],
        depth=0.022,
        axis='Y',
        location=(0.0, y_neon, 0.0),
        name='Neon_Bowl_Icon',
        color='neon_amber',
        signal=True,
    ))
    for st_i, st_x in enumerate((-0.635, -0.565)):
        sig.append(aa.add_box(
            name=f'Neon_Bowl_Steam_{st_i}',
            size=(0.020, 0.020, 0.062),
            location=(st_x, y_neon, 2.592),
            rotation=(0.0, 0.16 if st_i == 0 else -0.16, 0.0),
            color='neon_cyan',
            signal=True,
        ))
    # Katakana 'ラ' (Ra, X ~ -0.25)
    sig.append(aa.add_box(
        name='Neon_Ra_Top',
        size=(0.13, 0.022, 0.024),
        location=(-0.26, y_neon, 2.615),
        color='neon_amber',
        signal=True,
    ))
    sig.append(aa.add_box(
        name='Neon_Ra_Mid',
        size=(0.19, 0.022, 0.024),
        location=(-0.25, y_neon, 2.562),
        color='neon_amber',
        signal=True,
    ))
    sig.append(aa.add_box(
        name='Neon_Ra_Leg',
        size=(0.026, 0.022, 0.125),
        location=(-0.21, y_neon, 2.502),
        rotation=(0.0, 0.56, 0.0),
        color='neon_amber',
        signal=True,
    ))
    # Katakana 'ー' (Choon, X ~ +0.05)
    sig.append(aa.add_box(
        name='Neon_Choon',
        size=(0.17, 0.022, 0.026),
        location=(0.05, y_neon, 2.548),
        color='neon_magenta',
        signal=True,
    ))
    # Katakana 'メ' (Me, X ~ +0.35)
    sig.append(aa.add_box(
        name='Neon_Me_Main',
        size=(0.026, 0.022, 0.18),
        location=(0.35, y_neon, 2.545),
        rotation=(0.0, 0.56, 0.0),
        color='neon_amber',
        signal=True,
    ))
    sig.append(aa.add_box(
        name='Neon_Me_Cross',
        size=(0.026, 0.022, 0.105),
        location=(0.34, y_neon - 0.004, 2.552),
        rotation=(0.0, -0.72, 0.0),
        color='neon_amber',
        signal=True,
    ))
    # Katakana 'ン' (N, X ~ +0.64)
    sig.append(aa.add_box(
        name='Neon_N_Tick',
        size=(0.026, 0.022, 0.072),
        location=(0.57, y_neon, 2.596),
        rotation=(0.0, -0.82, 0.0),
        color='neon_magenta',
        signal=True,
    ))
    sig.append(aa.add_box(
        name='Neon_N_Sweep',
        size=(0.026, 0.022, 0.185),
        location=(0.65, y_neon, 2.528),
        rotation=(0.0, 0.68, 0.0),
        color='neon_magenta',
        signal=True,
    ))

    return aa.combine_by_role(body, sig)


# ---------------------------------------------------------------------------
# 2. Old Kyoto Memory Echo Pedestal (public/street/echo-shrine.glb)
#    Footprint 1.1m x 1.1m (X: -0.55..0.55, Y: -0.55..0.55), 2.3m tall (Z: 0..2.3)
#    Stepped carved dark stone Toro pedestal + sleek gold/cyber-conduit ribs +
#    floating multi-shard cyan/gold Komorebi Memory Crystal & halo ring in 'Signal'
# ---------------------------------------------------------------------------

def build_echo_shrine() -> list[bpy.types.Object]:
    aa.reset_scene()
    body: list[bpy.types.Object] = []
    sig: list[bpy.types.Object] = []

    col_basalt = (0.068, 0.078, 0.102)     # Deep Kyoto weathered basalt foundation
    col_granite = (0.135, 0.152, 0.188)    # Carved dark Toro stone
    col_stone_hi = (0.215, 0.240, 0.285)   # Carved lotus collar & bevel stone
    col_gold = (0.82, 0.62, 0.21)          # Sleek Komorebi ceremonial gold-brass
    col_gold_deep = (0.58, 0.40, 0.14)     # Recessed bronze-gold trim

    # --- A. Stepped Carved Dark Stone Toro Pedestal (Z: 0.00..1.34, 1.10m x 1.10m) ---
    body.append(aa.add_bevel_box(
        name='Toro_Base_Plinth',
        size=(1.04, 1.04, 0.13),
        location=(0.0, 0.0, 0.065),
        bevel=0.020,
        segments=1,
        color=col_basalt,
    ))
    for cx in (-0.45, 0.45):
        for cy in (-0.45, 0.45):
            body.append(aa.add_box(
                name=f'Toro_Corner_Pad_{cx:+.2f}_{cy:+.2f}',
                size=(0.20, 0.20, 0.10),
                location=(cx, cy, 0.05),
                taper=(0.86, 0.86),
                color=col_granite,
            ))

    body.append(aa.add_bevel_box(
        name='Toro_Second_Step',
        size=(0.84, 0.84, 0.14),
        location=(0.0, 0.0, 0.20),
        bevel=0.022,
        segments=1,
        color=col_granite,
    ))

    # Octagonal carved Kaeribana (inverted lotus collar), Sao shaft & Chudai platform
    oct_phase = math.pi / 8.0
    toro_core = add_lathe(
        profile_rz=[
            (0.37, 0.26),   # Kaeribana base skirt
            (0.39, 0.31),
            (0.33, 0.39),   # Carved lotus waist
            (0.24, 0.46),   # Collar transition
            (0.205, 0.50),  # Sao pillar lower
            (0.188, 0.78),  # Sao slight entasis waist
            (0.202, 1.04),  # Sao pillar upper
            (0.27, 1.11),   # Ukebana lower lotus cup
            (0.36, 1.19),
            (0.42, 1.26),   # Chudai broad octagonal altar rim
            (0.41, 1.33),
            (0.30, 1.33),   # Recessed emitter basin rim
            (0.24, 1.29),   # Emitter well floor
        ],
        segments=8,
        phase=oct_phase,
        shade_smooth=False,
        name='Toro_Carved_Column',
        color=col_granite,
        ao_strength=0.30,
    )
    aa.paint_faces(
        toro_core,
        lambda _p, c, _n: (0.29 < c.z < 0.43) or (1.16 < c.z < 1.335),
        color=col_stone_hi,
        ao_strength=0.22,
        top_highlight=0.12,
    )
    body.append(toro_core)

    # 8 carved lotus-petal bosses around the Kaeribana collar
    for li in range(8):
        ang = (li / 8.0) * math.tau
        lx = math.cos(ang) * 0.34
        ly = math.sin(ang) * 0.34
        body.append(aa.add_box(
            name=f'Lotus_Boss_{li}',
            size=(0.11, 0.08, 0.085),
            location=(lx, ly, 0.33),
            rotation=(0.24, 0.0, ang + math.pi * 0.5),
            taper=(0.68, 0.58),
            color=col_stone_hi,
        ))

    # Mid-pillar carved stone & gold fushi ring + Front/Back ceremonial cyber-ofuda plaques
    body.append(add_lathe(
        profile_rz=[
            (0.205, 0.73),
            (0.235, 0.76),
            (0.235, 0.80),
            (0.205, 0.83),
        ],
        segments=8,
        phase=oct_phase,
        shade_smooth=False,
        name='Sao_Fushi_Collar',
        color=col_gold,
    ))
    for face_tag, fy in (('Front', -0.205), ('Back', 0.205)):
        body.append(aa.add_box(
            name=f'Ofuda_Frame_{face_tag}',
            size=(0.17, 0.036, 0.26),
            location=(0.0, fy, 0.78),
            color='obsidian',
        ))
        sig.append(aa.add_box(
            name=f'Ofuda_Glyph_{face_tag}',
            size=(0.095, 0.018, 0.19),
            location=(0.0, fy + (-0.016 if fy < 0 else 0.016), 0.78),
            color='neon_cyan' if face_tag == 'Front' else 'neon_amber',
            signal=True,
        ))

    # 4 upturned Kyoto warabite stone scroll brackets on the Chudai corners
    for wi in range(4):
        ang = math.pi * 0.25 + wi * (math.pi * 0.5)
        wx = math.cos(ang) * 0.37
        wy = math.sin(ang) * 0.37
        body.append(aa.add_box(
            name=f'Warabite_Scroll_{wi}',
            size=(0.09, 0.14, 0.09),
            location=(wx, wy, 1.32),
            rotation=(-0.32, 0.0, ang + math.pi * 0.5),
            taper=(0.76, 0.65),
            color=col_stone_hi,
        ))

    # Glowing emitter core disk inside the Chudai cradle basin
    body.append(aa.add_torus(
        name='Emitter_Basin_GoldRing',
        major_radius=0.22,
        minor_radius=0.022,
        major_segments=10,
        minor_segments=4,
        location=(0.0, 0.0, 1.315),
        color=col_gold,
    ))
    sig.append(aa.add_cylinder(
        name='Emitter_Basin_CoreGlow',
        radius=0.19,
        depth=0.024,
        vertices=10,
        location=(0.0, 0.0, 1.305),
        color='#18f0ff',
        signal=True,
    ))

    # --- B. Sleek Gold / Cyber-Conduit Ribs Fused into the Pedestal & Cradling Crystal ---
    for ri in range(4):
        ang = math.pi * 0.25 + ri * (math.pi * 0.5)
        ca, sa = math.cos(ang), math.sin(ang)

        body.append(aa.add_box(
            name=f'Conduit_Base_Socket_{ri}',
            size=(0.12, 0.15, 0.14),
            location=(ca * 0.42, sa * 0.42, 0.22),
            rotation=(-0.22, 0.0, ang + math.pi * 0.5),
            taper=(0.82, 0.78),
            color='obsidian',
        ))

        rib_stations = [
            (ca * 0.42, sa * 0.42, 0.24),
            (ca * 0.24, sa * 0.24, 0.52),
            (ca * 0.22, sa * 0.22, 0.98),
            (ca * 0.43, sa * 0.43, 1.24),
            (ca * 0.45, sa * 0.45, 1.56),
            (ca * 0.38, sa * 0.38, 1.84),
            (ca * 0.24, sa * 0.24, 2.02),
        ]
        body.append(add_tube_sweep(
            points=rib_stations,
            radius=0.028,
            sides=5,
            name=f'Gold_Conduit_Rib_{ri}',
            color=col_gold,
        ))
        body.append(add_tube_sweep(
            points=[
                (ca * 0.455, sa * 0.455, 1.26),
                (ca * 0.475, sa * 0.475, 1.56),
                (ca * 0.402, sa * 0.402, 1.84),
            ],
            radius=0.014,
            sides=4,
            shade_smooth=False,
            name=f'Conduit_Carbon_Spine_{ri}',
            color='obsidian',
        ))
        body.append(aa.add_box(
            name=f'Conduit_Emitter_Tip_{ri}',
            size=(0.055, 0.085, 0.095),
            location=(ca * 0.23, sa * 0.23, 2.02),
            rotation=(0.45, 0.0, ang + math.pi * 0.5),
            taper=(0.72, 0.72),
            color=col_gold,
        ))
        sig.append(add_crystal_shard(
            name=f'Conduit_Tip_Node_{ri}',
            radius=0.024,
            bot_len=0.028,
            waist_h=0.018,
            top_len=0.028,
            sides=4,
            twist=0.0,
            asymmetry=0.0,
            location=(ca * 0.195, sa * 0.195, 2.005),
            color='neon_cyan' if ri % 2 == 0 else 'neon_amber',
            signal=True,
        ))

        sig.append(add_tube_sweep(
            points=[
                (ca * 0.37, sa * 0.37, 0.28),
                (ca * 0.205, sa * 0.205, 0.54),
                (ca * 0.205, sa * 0.205, 1.02),
            ],
            radius=0.009,
            sides=4,
            shade_smooth=False,
            name=f'Cyber_Vein_{ri}',
            color='neon_cyan',
            signal=True,
        ))

    # --- C. Floating Multi-Shard Cyan/Gold Komorebi Memory Crystal + Halo Ring ('Signal') ---
    sig.append(add_crystal_shard(
        name='Memory_Crystal_Core',
        radius=0.116,
        bot_len=0.30,
        waist_h=0.20,
        top_len=0.36,
        sides=6,
        twist=0.18,
        asymmetry=0.08,
        tip_offset=(0.006, -0.006),
        location=(0.0, 0.0, 1.72),
        rotation=(0.04, -0.03, 0.22),
        color='neon_cyan',
        gradient_z=('#008ef0', '#6af4ff'),
        signal=True,
    ))

    # Floating Sacred Hoju Apex Shard crowning exact Z = 2.300m
    body.append(aa.add_torus(
        name='Apex_Gold_Crownlet',
        major_radius=0.068,
        minor_radius=0.010,
        major_segments=8,
        minor_segments=4,
        location=(0.0, 0.0, 2.155),
        color=col_gold,
    ))
    sig.append(add_crystal_shard(
        name='Memory_Apex_Hoju_Shard',
        radius=0.052,
        bot_len=0.048,
        waist_h=0.042,
        top_len=0.110,
        sides=6,
        twist=0.10,
        asymmetry=0.04,
        location=(0.0, 0.0, 2.190),
        color='neon_amber',
        gradient_z=('#ff7b08', '#ffd85c'),
        signal=True,
    ))

    # 6 Orbiting Multi-Shard Cyan & Gold Memory Fragments in ascending spiral
    shard_specs = [
        (-65.0, 0.188, 1.60, 0.048, 0.11, 0.16, 0.34, True),
        (-5.0,  0.198, 1.72, 0.054, 0.13, 0.19, 0.30, False),
        (55.0,  0.184, 1.84, 0.046, 0.10, 0.16, 0.32, True),
        (115.0, 0.192, 1.66, 0.052, 0.12, 0.18, 0.28, False),
        (175.0, 0.180, 1.88, 0.045, 0.10, 0.15, 0.35, True),
        (235.0, 0.194, 1.76, 0.055, 0.13, 0.20, 0.30, False),
    ]
    for si, (deg, orb_r, sz, srad, sbot, stop, stilt, is_gold) in enumerate(shard_specs):
        ang = math.radians(deg)
        sx = math.cos(ang) * orb_r
        sy = math.sin(ang) * orb_r
        grad = ('#ff7b08', '#ffd452') if is_gold else ('#00a8ff', '#74f8ff')
        base_c = 'neon_amber' if is_gold else 'neon_cyan'
        sig.append(add_crystal_shard(
            name=f'Memory_Satellite_Shard_{si}',
            radius=srad,
            bot_len=sbot,
            waist_h=0.08,
            top_len=stop,
            sides=5,
            twist=0.20,
            asymmetry=0.12,
            location=(sx, sy, sz),
            rotation=(stilt, 0.0, ang - math.pi * 0.5),
            color=base_c,
            gradient_z=grad,
            signal=True,
        ))

    # Luminous Dual Halo Ring in 'Signal'
    halo_z = 1.76
    halo_tilt_x = math.radians(17.0)
    sig.append(aa.add_torus(
        name='Memory_Halo_Outer_Cyan',
        major_radius=0.365,
        minor_radius=0.019,
        major_segments=18,
        minor_segments=4,
        location=(0.0, 0.0, halo_z),
        rotation=(halo_tilt_x, math.radians(-6.0), 0.0),
        color='neon_cyan',
        signal=True,
    ))
    sig.append(aa.add_torus(
        name='Memory_Halo_Inner_Gold',
        major_radius=0.285,
        minor_radius=0.013,
        major_segments=14,
        minor_segments=4,
        location=(0.0, 0.0, halo_z),
        rotation=(math.radians(-16.0), math.radians(14.0), 0.0),
        color='neon_amber',
        signal=True,
    ))
    halo_rot = Euler((halo_tilt_x, math.radians(-6.0), 0.0), 'XYZ').to_matrix()
    for hi in range(4):
        hang = (hi / 4.0) * math.tau
        local_pt = Vector((math.cos(hang) * 0.365, math.sin(hang) * 0.365, 0.0))
        w_pt = Vector((0.0, 0.0, halo_z)) + halo_rot @ local_pt
        body.append(aa.add_box(
            name=f'Halo_Gold_Clasp_{hi}',
            size=(0.048, 0.048, 0.048),
            location=w_pt,
            rotation=(0.4, 0.4, hang),
            color=col_gold_deep,
        ))

    return aa.combine_by_role(body, sig)


# ---------------------------------------------------------------------------
# Export & Self-Verification Entry Point
# ---------------------------------------------------------------------------

def main() -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    specs = [
        ('noodle-stall.glb', build_noodle_stall, 1600, 3200),
        ('echo-shrine.glb', build_echo_shrine, 1100, 2200),
    ]

    for filename, builder, min_budget, max_budget in specs:
        objs = builder()
        bmin, bmax = _bounds_world(objs)
        dims = bmax - bmin
        out_path = OUT_DIR / filename
        info = aa.export_glb(
            out_path,
            objs,
            min_tris=max(701, min_budget),
            max_meshes=6,
        )
        tris = info['triangles']
        assert tris > 700, f'{filename}: expected > 700 tris, got {tris}'
        assert min_budget <= tris <= max_budget, (
            f'{filename}: triangle count {tris} outside target budget {min_budget}..{max_budget}'
        )
        assert info['meshes'] <= 6, f'{filename}: expected <= 6 meshes, got {info["meshes"]}'
        print(
            f"[OK] {filename}: {tris} tris, {info['meshes']} meshes, {info['size_kb']} KB | "
            f"Blender bounds X[{bmin.x:.3f}..{bmax.x:.3f}] ({dims.x:.3f}m) "
            f"Y[{bmin.y:.3f}..{bmax.y:.3f}] ({dims.y:.3f}m) "
            f"Z[{bmin.z:.3f}..{bmax.z:.3f}] ({dims.z:.3f}m)"
        )


if __name__ == '__main__':
    main()
