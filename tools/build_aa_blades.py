"""Headless Blender 4.2 LTS AA sculptor for Komorebi hero blades and spirit lantern.

Builds and exports three vertex-painted ('Body' + 'Signal') GLB 2.0 assets into public/street/:
  1. public/street/cyber-katana.glb   — Kael's Komorebi Edge (~900–1,800 tris)
  2. public/street/ren-odachi.glb     — Ren's Architect Odachi (~1,000–2,000 tris)
  3. public/street/spirit-lantern.glb — Kael's Komorebi Seed Lantern (~800–1,400 tris)

Orientation convention for handheld blades (Blender Z-up -> Three.js Y-up via export_yup=True):
  * Grip runs along Blender Z (cyber-katana: -0.16..+0.07, ren-odachi: -0.18..+0.08).
  * Cyber-tsuba guard sits at Blender Z = +0.08 (katana) / +0.085 (odachi).
  * Single-edged curved blade runs along +Z up to +1.17 (katana) / +1.25 (odachi),
    curving toward +X (+0.03 on katana, +0.042 on odachi) with -X razor edge (ha)
    and +X titanium/obsidian fuller spine (mune).

Run with:
  .test/blender/blender --background --disable-autoexec --python tools/build_aa_blades.py
"""

from __future__ import annotations

import math
import sys
import traceback
from pathlib import Path
from typing import Sequence

import bmesh
import bpy
from mathutils import Euler, Matrix, Vector

TOOLS_DIR = Path(__file__).resolve().parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))
sys.dont_write_bytecode = True

import blender_aa_lib as aa

ROOT = TOOLS_DIR.parent
OUT_DIR = ROOT / 'public' / 'street'

KATANA_CYAN = (0.05, 0.95, 1.00)
ODACHI_CRIMSON = (1.00, 0.14, 0.35)
SPIRIT_AMBER = (1.00, 0.66, 0.12)
SPIRIT_CYAN = (0.06, 0.95, 1.00)


# ---------------------------------------------------------------------------
# Shared Sculpting & Loft Helpers
# ---------------------------------------------------------------------------

def _smoothstep(edge0: float, edge1: float, x: float) -> float:
    if abs(edge1 - edge0) < 1e-8:
        return 1.0 if x >= edge1 else 0.0
    t = max(0.0, min(1.0, (x - edge0) / (edge1 - edge0)))
    return t * t * (3.0 - 2.0 * t)


def _lerp(a: float, b: float, t: float) -> float:
    return a + (b - a) * t


def _lerp_rgb(c0, c1, t: float) -> tuple[float, float, float]:
    t = max(0.0, min(1.0, t))
    return (
        c0[0] + (c1[0] - c0[0]) * t,
        c0[1] + (c1[1] - c0[1]) * t,
        c0[2] + (c1[2] - c0[2]) * t,
    )


def build_lofted_solid(
    rings: Sequence[Sequence[tuple[float, float, float]]],
    name: str,
    color=(0.30, 0.35, 0.42),
    signal: bool = False,
    shade_smooth: bool = False,
    ao_strength: float = 0.24,
    top_highlight: float = 0.10,
    ridge_boost: float = 0.11,
    ground_ao: float = 0.04,
    gradient_z=None,
    base_point: tuple[float, float, float] | None = None,
    tip_point: tuple[float, float, float] | None = None,
) -> bpy.types.Object:
    """Create a watertight manifold lofted mesh across closed cross-section rings."""
    if len(rings) < 2:
        raise ValueError(f'{name}: build_lofted_solid requires >= 2 station rings')
    m = len(rings[0])
    if m < 3 or any(len(r) != m for r in rings):
        raise ValueError(f'{name}: all rings must share >= 3 vertices')

    bm = bmesh.new()
    bm_rings = [[bm.verts.new(pt) for pt in ring] for ring in rings]

    for k in range(len(bm_rings) - 1):
        ra = bm_rings[k]
        rb = bm_rings[k + 1]
        for i in range(m):
            j = (i + 1) % m
            bm.faces.new((ra[i], ra[j], rb[j], rb[i]))

    if base_point is None:
        bx = sum(p[0] for p in rings[0]) / m
        by = sum(p[1] for p in rings[0]) / m
        bz = sum(p[2] for p in rings[0]) / m
        base_point = (bx, by, bz)
    v_base = bm.verts.new(base_point)
    for i in range(m):
        j = (i + 1) % m
        bm.faces.new((v_base, bm_rings[0][j], bm_rings[0][i]))

    if tip_point is None:
        tx = sum(p[0] for p in rings[-1]) / m
        ty = sum(p[1] for p in rings[-1]) / m
        tz = sum(p[2] for p in rings[-1]) / m
        tip_point = (tx, ty, tz)
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
    return obj


def build_hex_ring_frame(
    z_center: float,
    radius_outer: float,
    radius_inner: float,
    thickness: float,
    sides: int = 6,
    profile_mode: str = 'diamond',
    scale_xy=(1.0, 1.0),
    twist_rad: float = 0.0,
    name: str = 'HexRing',
    color='brass',
    signal: bool = False,
    ground_ao: float = 0.04,
) -> bpy.types.Object:
    """Build a closed beveled polygonal collar/halo ring (no degenerate booleans)."""
    bm = bmesh.new()
    sx, sy = float(scale_xy[0]), float(scale_xy[1])
    hz = thickness * 0.5
    r_mid = 0.5 * (radius_inner + radius_outer)

    if profile_mode == 'cord':
        # 3-point peaked cord cross-section for braided tsuka-ito diamond wrap bands
        profile = [
            (radius_inner, -hz * 0.82),
            (radius_inner, +hz * 0.82),
            (radius_outer, 0.0),
        ]
    elif profile_mode == 'hex':
        chamfer = min(thickness * 0.22, (radius_outer - radius_inner) * 0.22)
        profile = [
            (radius_inner, 0.0),
            (radius_inner + chamfer, hz),
            (radius_outer - chamfer, hz),
            (radius_outer, 0.0),
            (radius_outer - chamfer, -hz),
            (radius_inner + chamfer, -hz),
        ]
    else:
        # Crisp 4-point diamond/chamfered rim cross-section
        profile = [
            (radius_inner, 0.0),
            (r_mid, hz),
            (radius_outer, 0.0),
            (r_mid, -hz),
        ]

    p_len = len(profile)
    ring_loops = []
    for i in range(sides):
        ang = twist_rad + (i / sides) * math.tau
        ca, sa = math.cos(ang), math.sin(ang)
        loop = [bm.verts.new((ca * r * sx, sa * r * sy, z_center + dz)) for r, dz in profile]
        ring_loops.append(loop)

    for i in range(sides):
        i_next = (i + 1) % sides
        la = ring_loops[i]
        lb = ring_loops[i_next]
        for k in range(p_len):
            k_next = (k + 1) % p_len
            bm.faces.new((la[k], la[k_next], lb[k_next], lb[k]))

    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()

    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    for p in mesh.polygons:
        p.use_smooth = False
    mesh.validate()
    mesh.update()
    aa.paint_mesh(obj, color=color, signal=signal, ground_ao=ground_ao, ao_strength=0.22)
    return obj


def build_curved_single_edged_blade(
    *,
    name_prefix: str,
    z_start: float,
    z_tip: float,
    tip_sori_x: float,
    base_spine_x: float,
    base_width: float,
    yokote_width: float,
    yokote_s: float,
    shinogi_half_t: float,
    spine_gradient,
    plasma_color,
    stations: int = 15,
    fuller_stations: int = 10,
    fuller_s_range=(0.045, 0.82),
    spine_vent_notches: int = 0,
) -> tuple[list[bpy.types.Object], list[bpy.types.Object]]:
    """Sculpt a curved single-edged katana/odachi blade into watertight Body + Signal solids.

    Blade geometry in Blender space (exported with export_yup=True):
      * Length runs along +Z from z_start to z_tip.
      * Blade curves gracefully toward +X, placing the needle tip at (tip_sori_x, 0, z_tip).
      * +X is the dark titanium/obsidian spine (iori-mune) with recessed bo-hi fuller and
        bevelled shinogi ridge; -X is the blazing plasma cutting edge (ha) sweeping up the
        curved kissaki fukura to the tip.
    """
    length = z_tip - z_start

    linear_drift = -0.0055
    quad_gain = (tip_sori_x - base_spine_x) - linear_drift

    def spine_x(s: float) -> float:
        return base_spine_x + linear_drift * s + quad_gain * (s ** 1.90)

    def width_at(s: float) -> float:
        if s <= yokote_s:
            u = s / yokote_s
            return _lerp(base_width, yokote_width, u ** 0.88)
        u_k = (s - yokote_s) / max(1e-5, 1.0 - yokote_s)
        return yokote_width * max(0.0, 1.0 - (u_k ** 1.62))

    def thick_scale(s: float) -> float:
        if s <= yokote_s:
            return _lerp(1.0, 0.80, s / yokote_s)
        u_k = (s - yokote_s) / max(1e-5, 1.0 - yokote_s)
        return 0.80 * _lerp(1.0, 0.26, u_k)

    f_s0, f_s1 = fuller_s_range

    def fuller_factor(s: float) -> float:
        if s <= f_s0 or s >= f_s1:
            return 0.0
        ramp_in = _smoothstep(f_s0, f_s0 + 0.045, s)
        ramp_out = 1.0 - _smoothstep(f_s1 - 0.065, f_s1, s)
        return ramp_in * ramp_out

    # 1. Spine + Shinogi + Fuller Trough Loft ('Body', 14 perimeter verts)
    body_rings = []
    body_s_max = 0.968
    for idx in range(stations):
        s = (idx / (stations - 1)) * body_s_max
        z = z_start + s * length
        xs = spine_x(s)
        w = width_at(s)
        xe = xs - w
        ts = thick_scale(s)
        fd = fuller_factor(s)

        notch = 0.0
        if spine_vent_notches > 0 and 0.13 <= s <= 0.74:
            u_v = (s - 0.13) / (0.74 - 0.13) * spine_vent_notches
            cyc = math.sin(u_v * math.pi) ** 2
            notch = 0.0024 * cyc

        def x_u(u: float) -> float:
            return xe + u * w

        t_shin = shinogi_half_t * ts
        t_mune = shinogi_half_t * 0.79 * ts
        t_clamp = shinogi_half_t * 0.48 * ts

        def t_flat(u: float) -> float:
            return _lerp(t_shin, t_mune, (u - 0.54) / (0.92 - 0.54))

        t_rim_lo = t_flat(0.62)
        t_rim_hi = t_flat(0.85)
        t_fl_mid = t_flat(0.735) * (1.0 - 0.60 * fd)

        ring = [
            (x_u(1.00) - notch, 0.0, z),
            (x_u(0.92) - notch * 0.5, +t_mune, z),
            (x_u(0.85), +t_rim_hi, z),
            (x_u(0.79), +t_fl_mid, z),
            (x_u(0.67), +t_fl_mid, z),
            (x_u(0.62), +t_rim_lo, z),
            (x_u(0.54), +t_shin, z),
            (x_u(0.35), +t_clamp, z),
            (x_u(0.375), 0.0, z),
            (x_u(0.35), -t_clamp, z),
            (x_u(0.54), -t_shin, z),
            (x_u(0.62), -t_rim_lo, z),
            (x_u(0.67), -t_fl_mid, z),
            (x_u(0.79), -t_fl_mid, z),
            (x_u(0.85), -t_rim_hi, z),
            (x_u(0.92) - notch * 0.5, -t_mune, z),
        ]
        body_rings.append(ring)

    s_spine_tip = 0.985
    xs_end = spine_x(s_spine_tip)
    w_end = width_at(s_spine_tip)
    body_tip = (xs_end - w_end * 0.22, 0.0, z_start + s_spine_tip * length)
    body_base = (spine_x(0.0) - base_width * 0.32, 0.0, z_start)

    spine_obj = build_lofted_solid(
        body_rings,
        name=f'{name_prefix}_Spine',
        color=spine_gradient[0],
        gradient_z=spine_gradient,
        signal=False,
        shade_smooth=False,
        ao_strength=0.26,
        top_highlight=0.12,
        ridge_boost=0.13,
        ground_ao=0.03,
        base_point=body_base,
        tip_point=body_tip,
    )

    # 2. Razor Plasma Cutting Edge ('Signal') from z_start all the way to (tip_sori_x, 0, z_tip)
    edge_rings = []
    edge_s_max = 0.982
    for idx in range(stations):
        s = (idx / (stations - 1)) * edge_s_max
        z = z_start + s * length
        xs = spine_x(s)
        w = max(0.0045, width_at(s))
        xe = xs - w
        ts = thick_scale(s)

        tip_blend = _smoothstep(0.935, 0.982, s)
        u_back = _lerp(0.40, 0.98, tip_blend)
        u_mid = _lerp(0.34, 0.68, tip_blend)
        u_bev = _lerp(0.17, 0.32, tip_blend)

        def x_u(u: float) -> float:
            return xe + u * w

        t_mid = shinogi_half_t * 0.40 * ts * _lerp(1.0, 0.85, tip_blend)
        t_bev = shinogi_half_t * 0.26 * ts

        ring = [
            (x_u(u_back), 0.0, z),
            (x_u(u_mid), +t_mid, z),
            (x_u(u_bev), +t_bev, z),
            (x_u(0.00), 0.0, z),
            (x_u(u_bev), -t_bev, z),
            (x_u(u_mid), -t_mid, z),
        ]
        edge_rings.append(ring)

    edge_base = (spine_x(0.0) - base_width * 0.80, 0.0, z_start)
    edge_tip = (tip_sori_x, 0.0, z_tip)

    edge_obj = build_lofted_solid(
        edge_rings,
        name=f'{name_prefix}_PlasmaEdge',
        color=plasma_color,
        signal=True,
        shade_smooth=False,
        base_point=edge_base,
        tip_point=edge_tip,
    )

    # 3. Unified Bo-Hi Plasma Fuller Conduit ('Signal') glowing inside both flanks of the fuller trough
    fuller_rings = []
    fa = f_s0 + 0.012
    fb = f_s1 - 0.012
    for idx in range(fuller_stations):
        u_f = idx / (fuller_stations - 1)
        s = _lerp(fa, fb, u_f)
        z = z_start + s * length
        xs = spine_x(s)
        w = width_at(s)
        xe = xs - w
        ts = thick_scale(s)
        fd = fuller_factor(s)
        taper = math.sin(u_f * math.pi) ** 0.45

        def x_u(u: float) -> float:
            return xe + u * w

        t_shin = shinogi_half_t * ts
        t_mune = shinogi_half_t * 0.79 * ts
        t_fl_mid = _lerp(t_shin, t_mune, (0.735 - 0.54) / 0.38) * (1.0 - 0.60 * fd)
        proud = shinogi_half_t * 0.32 * ts * taper

        ring = [
            (x_u(0.80), 0.0, z),
            (x_u(0.78), +(t_fl_mid + proud * 0.90), z),
            (x_u(0.68), +(t_fl_mid + proud), z),
            (x_u(0.66), 0.0, z),
            (x_u(0.68), -(t_fl_mid + proud), z),
            (x_u(0.78), -(t_fl_mid + proud * 0.90), z),
        ]
        fuller_rings.append(ring)

    s_f_bot = fa - 0.008
    s_f_top = fb + 0.008
    fuller_base = (spine_x(s_f_bot) - width_at(s_f_bot) * 0.27, 0.0, z_start + s_f_bot * length)
    fuller_tip = (spine_x(s_f_top) - width_at(s_f_top) * 0.27, 0.0, z_start + s_f_top * length)

    fuller_obj = build_lofted_solid(
        fuller_rings,
        name=f'{name_prefix}_PlasmaFuller',
        color=plasma_color,
        signal=True,
        shade_smooth=False,
        base_point=fuller_base,
        tip_point=fuller_tip,
    )

    return [spine_obj], [edge_obj, fuller_obj]


def build_tsuka_handle(
    *,
    name_prefix: str,
    z_bottom: float,
    z_top: float,
    core_rx: float,
    core_ry: float,
    wrap_pairs: int,
    core_color,
    wrap_color,
    fittings_color,
    signal_color,
) -> tuple[list[bpy.types.Object], list[bpy.types.Object]]:
    """Build an elliptical katana/odachi tsuka with diamond ito wrap, menuki, fuchi & kashira."""
    body_parts: list[bpy.types.Object] = []
    sig_parts: list[bpy.types.Object] = []

    total_h = z_top - z_bottom
    kashira_h = 0.020
    fuchi_h = 0.017
    core_z0 = z_bottom + kashira_h * 0.85
    core_z1 = z_top - fuchi_h * 0.75
    core_h = core_z1 - core_z0
    core_zc = 0.5 * (core_z0 + core_z1)

    # Samegawa ray-skin elliptical grip core
    core = aa.add_cylinder(
        name=f'{name_prefix}_SamegawaCore',
        radius=1.0,
        depth=core_h,
        vertices=8,
        location=(0.0, 0.0, core_zc),
        scale=(core_rx, core_ry, 1.0),
        shade_smooth=True,
        color=core_color,
        ao_strength=0.22,
    )
    aa.paint_mesh(core, color=core_color, ao_strength=0.22, ground_ao=0.04)
    body_parts.append(core)

    # Subtle recessed plasma seam along +/-X edges of grip peeking between diamond wraps
    for side_sign in (-1.0, 1.0):
        seam = aa.add_box(
            name=f'{name_prefix}_GripSeam_{int(side_sign)}',
            size=(0.0032, core_ry * 0.42, core_h * 0.86),
            location=(side_sign * (core_rx * 0.96), 0.0, core_zc),
            color=signal_color,
            signal=True,
        )
        sig_parts.append(seam)

    # Crisscrossing diamond tsuka-ito cord bands (+tilt and -tilt) over the samegawa core
    band_span = core_h - 0.020
    for i in range(wrap_pairs):
        u = (i + 0.5) / wrap_pairs
        zb = (core_z0 + 0.010) + u * band_span
        for tilt_sign in (-1.0, 1.0):
            band = build_hex_ring_frame(
                z_center=0.0,
                radius_outer=1.25,
                radius_inner=0.92,
                thickness=0.0145,
                sides=8,
                profile_mode='cord',
                scale_xy=(core_rx, core_ry),
                name=f'{name_prefix}_Ito_{i}_{int(tilt_sign)}',
                color=wrap_color,
                ground_ao=0.04,
            )
            rot_m = Matrix.Translation((0.0, 0.0, zb)) @ Euler((0.0, tilt_sign * 0.72, 0.0), 'XYZ').to_matrix().to_4x4()
            band.data.transform(rot_m)
            band.data.update()
            aa.paint_mesh(band, color=wrap_color, ao_strength=0.26, ridge_boost=0.12, ground_ao=0.04)
            body_parts.append(band)

    # Sculpted golden menuki ornaments tucked under the palm wrap on +/-Y flanks
    for y_sign in (-1.0, 1.0):
        menuki = aa.add_box(
            name=f'{name_prefix}_Menuki_{int(y_sign)}',
            size=(core_rx * 0.82, 0.0045, 0.024),
            location=(0.0, y_sign * (core_ry * 1.03), core_zc + total_h * 0.04),
            rotation=(0.0, 0.0, 0.18 * y_sign),
            taper=(0.75, 0.85),
            color=fittings_color,
        )
        aa.paint_mesh(menuki, color=fittings_color, ao_strength=0.18, ground_ao=0.02)
        body_parts.append(menuki)

    # Fuchi collar at top of grip (just below tsuba)
    fuchi_zc = z_top - fuchi_h * 0.5
    fuchi = aa.add_cylinder(
        name=f'{name_prefix}_Fuchi',
        radius=1.0,
        radius_top=1.08,
        depth=fuchi_h,
        vertices=8,
        location=(0.0, 0.0, fuchi_zc),
        scale=(core_rx * 1.16, core_ry * 1.18, 1.0),
        shade_smooth=False,
        color=fittings_color,
    )
    aa.paint_mesh(fuchi, color=fittings_color, ao_strength=0.20, ground_ao=0.03)
    body_parts.append(fuchi)

    fuchi_ring = build_hex_ring_frame(
        z_center=z_top - 0.0025,
        radius_outer=1.20,
        radius_inner=0.95,
        thickness=0.0035,
        sides=6,
        scale_xy=(core_rx, core_ry),
        name=f'{name_prefix}_FuchiGlow',
        color=signal_color,
        signal=True,
    )
    sig_parts.append(fuchi_ring)

    # Kashira pommel cap at bottom of grip
    kashira_zc = z_bottom + kashira_h * 0.55
    kashira = aa.add_cylinder(
        name=f'{name_prefix}_Kashira',
        radius=0.92,
        radius_top=1.15,
        depth=kashira_h * 0.90,
        vertices=8,
        location=(0.0, 0.0, kashira_zc),
        scale=(core_rx * 1.14, core_ry * 1.16, 1.0),
        shade_smooth=False,
        color=fittings_color,
    )
    aa.paint_mesh(kashira, color=fittings_color, ao_strength=0.20, ground_ao=0.03)
    body_parts.append(kashira)

    # Pommel sarute loop + glowing end-cell right at z_bottom
    pommel_loop = build_hex_ring_frame(
        z_center=z_bottom + 0.0045,
        radius_outer=0.011,
        radius_inner=0.0055,
        thickness=0.005,
        sides=6,
        name=f'{name_prefix}_Sarute',
        color=fittings_color,
    )
    rot_s = (
        Matrix.Translation((0.0, 0.0, z_bottom + 0.0045))
        @ Euler((math.pi * 0.5, 0.0, 0.0), 'XYZ').to_matrix().to_4x4()
        @ Matrix.Translation((0.0, 0.0, -(z_bottom + 0.0045)))
    )
    pommel_loop.data.transform(rot_s)
    pommel_loop.data.update()
    aa.paint_mesh(pommel_loop, color=fittings_color, ao_strength=0.20, ground_ao=0.03)
    body_parts.append(pommel_loop)

    pommel_pip = aa.add_cylinder(
        name=f'{name_prefix}_PommelCell',
        radius=0.0065,
        depth=0.004,
        vertices=6,
        location=(0.0, 0.0, z_bottom + 0.009),
        shade_smooth=False,
        color=signal_color,
        signal=True,
    )
    sig_parts.append(pommel_pip)

    return body_parts, sig_parts


# ---------------------------------------------------------------------------
# 1. Kael's Komorebi Edge — public/street/cyber-katana.glb
# ---------------------------------------------------------------------------

def build_cyber_katana() -> Path:
    """Sculpt Kael's Komorebi Edge (~900–1,800 tris) and export public/street/cyber-katana.glb."""
    aa.reset_scene()
    body_parts: list[bpy.types.Object] = []
    sig_parts: list[bpy.types.Object] = []

    # 1. Tsuka handle: Blender Z = -0.16 .. +0.07
    t_body, t_sig = build_tsuka_handle(
        name_prefix='Katana',
        z_bottom=-0.160,
        z_top=0.070,
        core_rx=0.0168,
        core_ry=0.0122,
        wrap_pairs=5,
        core_color='teal_tech',
        wrap_color='obsidian',
        fittings_color='brass',
        signal_color=KATANA_CYAN,
    )
    body_parts.extend(t_body)
    sig_parts.extend(t_sig)

    # 2. Ornate Hexagonal Cyber-Tsuba Guard at Blender Z = +0.08
    for sz, sname in ((0.0725, 'Seppa_Low'), (0.0875, 'Seppa_High')):
        seppa = aa.add_cylinder(
            name=f'Katana_{sname}',
            radius=1.0,
            depth=0.0035,
            vertices=6,
            location=(0.0, 0.0, sz),
            scale=(0.025, 0.018, 1.0),
            shade_smooth=False,
            color='brass',
        )
        aa.paint_mesh(seppa, color='brass', ao_strength=0.18, ground_ao=0.02)
        body_parts.append(seppa)

    tsuba_core = aa.add_cylinder(
        name='Katana_TsubaCore',
        radius=1.0,
        depth=0.0085,
        vertices=6,
        location=(0.0, 0.0, 0.080),
        scale=(0.046, 0.036, 1.0),
        shade_smooth=False,
        color='obsidian',
    )
    aa.paint_mesh(tsuba_core, color='obsidian', ao_strength=0.22, ground_ao=0.02)
    body_parts.append(tsuba_core)

    tsuba_rim = build_hex_ring_frame(
        z_center=0.080,
        radius_outer=1.0,
        radius_inner=0.76,
        thickness=0.0115,
        sides=6,
        scale_xy=(0.058, 0.045),
        name='Katana_TsubaRim',
        color='brass',
        ground_ao=0.02,
    )
    body_parts.append(tsuba_rim)

    for i in range(6):
        ang = (i / 6.0) * math.tau
        cx = math.cos(ang) * 0.038
        cy = math.sin(ang) * 0.030
        spoke = aa.add_box(
            name=f'Katana_TsubaSpoke_{i}',
            size=(0.024, 0.0055, 0.0105),
            location=(cx, cy, 0.080),
            rotation=(0.0, 0.0, ang),
            taper=(0.86, 0.86),
            color='gunmetal',
        )
        aa.paint_mesh(spoke, color='gunmetal', ao_strength=0.20, ground_ao=0.02)
        body_parts.append(spoke)

    tsuba_glow = build_hex_ring_frame(
        z_center=0.080,
        radius_outer=0.80,
        radius_inner=0.67,
        thickness=0.0104,
        sides=6,
        scale_xy=(0.056, 0.043),
        name='Katana_TsubaGlow',
        color=KATANA_CYAN,
        signal=True,
    )
    sig_parts.append(tsuba_glow)

    # 3. Brass & Gunmetal Cyber-Habaki Emitter Collar (Z = +0.088 .. +0.120)
    habaki_profile = [
        (-0.0235, 0.0),
        (-0.012, +0.0092),
        (+0.014, +0.0085),
        (+0.0205, 0.0),
        (+0.014, -0.0085),
        (-0.012, -0.0092),
    ]
    habaki = aa.add_extruded_profile(
        habaki_profile,
        depth=0.032,
        axis='Z',
        centered=False,
        location=(0.0, 0.0, 0.088),
        shade_smooth=False,
        name='Katana_Habaki',
        color='brass',
    )
    aa.paint_mesh(habaki, color='brass', ao_strength=0.20, ground_ao=0.02)
    body_parts.append(habaki)

    for y_sign in (-1.0, 1.0):
        emitter = aa.add_box(
            name=f'Katana_Emitter_{int(y_sign)}',
            size=(0.0065, 0.0024, 0.020),
            location=(0.0045, y_sign * 0.0088, 0.105),
            color=KATANA_CYAN,
            signal=True,
        )
        sig_parts.append(emitter)

    # 4. Curved Single-Edged Katana Blade: Z = +0.090 .. +1.170, curving +0.030 toward +X at tip
    b_body, b_sig = build_curved_single_edged_blade(
        name_prefix='Katana',
        z_start=0.090,
        z_tip=1.170,
        tip_sori_x=0.030,
        base_spine_x=0.0175,
        base_width=0.039,
        yokote_width=0.028,
        yokote_s=0.865,
        shinogi_half_t=0.0068,
        spine_gradient=((0.115, 0.145, 0.195), (0.245, 0.300, 0.375)),
        plasma_color=KATANA_CYAN,
        stations=13,
        fuller_stations=8,
        fuller_s_range=(0.042, 0.815),
    )
    body_parts.extend(b_body)
    sig_parts.extend(b_sig)

    merged = aa.combine_by_role(body_parts, sig_parts)
    out_path = OUT_DIR / 'cyber-katana.glb'
    info = aa.export_glb(out_path, merged, min_tris=551, max_meshes=6)
    assert 900 <= info['triangles'] <= 1800, (
        f"cyber-katana.glb triangle count {info['triangles']} outside target 900..1800"
    )
    print(f"[build_aa_blades] Exported {info['path']}: {info['triangles']} tris, {info['meshes']} meshes, {info['size_kb']} KB")
    return out_path


# ---------------------------------------------------------------------------
# 2. Ren's Architect Odachi — public/street/ren-odachi.glb
# ---------------------------------------------------------------------------

def build_ren_odachi() -> Path:
    """Sculpt Ren's Architect Odachi (~1,000–2,000 tris) and export public/street/ren-odachi.glb."""
    aa.reset_scene()
    body_parts: list[bpy.types.Object] = []
    sig_parts: list[bpy.types.Object] = []

    # 1. Longer two-handed Odachi Tsuka grip: Blender Z = -0.18 .. +0.08
    t_body, t_sig = build_tsuka_handle(
        name_prefix='Odachi',
        z_bottom=-0.180,
        z_top=0.080,
        core_rx=0.0178,
        core_ry=0.0128,
        wrap_pairs=5,
        core_color='crimson_coat',
        wrap_color='obsidian',
        fittings_color='brass',
        signal_color=ODACHI_CRIMSON,
    )
    body_parts.extend(t_body)
    sig_parts.extend(t_sig)

    for x_sign in (-1.0, 1.0):
        fin = aa.add_box(
            name=f'Odachi_PommelFin_{int(x_sign)}',
            size=(0.008, 0.010, 0.022),
            location=(x_sign * 0.020, 0.0, -0.166),
            rotation=(0.0, x_sign * 0.28, 0.0),
            taper=(0.65, 0.80),
            color='brass',
        )
        aa.paint_mesh(fin, color='brass', ao_strength=0.18, ground_ao=0.02)
        body_parts.append(fin)

    # 2. Twin-Pronged Crimson/Gold Cyber-Tsuba at Blender Z = +0.085
    tsuba_hub = aa.add_cylinder(
        name='Odachi_TsubaHub',
        radius=1.0,
        depth=0.012,
        vertices=8,
        location=(0.0, 0.0, 0.085),
        scale=(0.048, 0.036, 1.0),
        shade_smooth=False,
        color='crimson_coat',
    )
    aa.paint_mesh(tsuba_hub, color='crimson_coat', ao_strength=0.22, ground_ao=0.02)
    body_parts.append(tsuba_hub)

    tsuba_gold_rim = build_hex_ring_frame(
        z_center=0.085,
        radius_outer=1.0,
        radius_inner=0.74,
        thickness=0.0135,
        sides=8,
        twist_rad=math.pi / 8.0,
        scale_xy=(0.056, 0.042),
        name='Odachi_TsubaGoldRim',
        color='brass',
        ground_ao=0.02,
    )
    body_parts.append(tsuba_gold_rim)

    # Swept twin prongs (-X blade-catcher fang & +X spine fang) in burnished gold & crimson
    for p_idx, (x_sign, reach_x, tip_z) in enumerate(((-1.0, 0.086, 0.138), (1.0, 0.080, 0.128))):
        prong_pts = [
            (x_sign * 0.036, 0.079),
            (x_sign * 0.068, 0.082),
            (x_sign * reach_x, tip_z - 0.012),
            (x_sign * (reach_x - 0.008), tip_z),
            (x_sign * 0.056, 0.098),
            (x_sign * 0.036, 0.092),
        ]
        prong = aa.add_extruded_profile(
            prong_pts,
            depth=0.013,
            axis='Y',
            centered=True,
            shade_smooth=False,
            name=f'Odachi_TsubaProng_{p_idx}',
            color='brass',
        )
        aa.paint_mesh(prong, color='brass', ao_strength=0.20, ground_ao=0.02)
        body_parts.append(prong)

        prong_glow = aa.add_box(
            name=f'Odachi_ProngGlow_{p_idx}',
            size=(0.030, 0.0162, 0.0042),
            location=(x_sign * 0.060, 0.0, 0.096 + (0.004 if x_sign < 0 else 0.0)),
            rotation=(0.0, x_sign * -0.58, 0.0),
            color=ODACHI_CRIMSON,
            signal=True,
        )
        sig_parts.append(prong_glow)

    tsuba_sig_ring = build_hex_ring_frame(
        z_center=0.085,
        radius_outer=0.78,
        radius_inner=0.64,
        thickness=0.0138,
        sides=8,
        twist_rad=math.pi / 8.0,
        scale_xy=(0.054, 0.040),
        name='Odachi_TsubaGlowRing',
        color=ODACHI_CRIMSON,
        signal=True,
    )
    sig_parts.append(tsuba_sig_ring)

    # 3. Armored Obsidian & Gold Habaki Shroud (Z = +0.091 .. +0.139)
    habaki_pts = [
        (-0.027, 0.0),
        (-0.014, +0.0102),
        (+0.016, +0.0094),
        (+0.024, 0.0),
        (+0.016, -0.0094),
        (-0.014, -0.0102),
    ]
    habaki = aa.add_extruded_profile(
        habaki_pts,
        depth=0.046,
        axis='Z',
        centered=False,
        location=(0.0, 0.0, 0.091),
        shade_smooth=False,
        name='Odachi_Habaki',
        color='obsidian',
    )
    aa.paint_mesh(habaki, color='obsidian', ao_strength=0.24, ground_ao=0.02)
    body_parts.append(habaki)

    habaki_collar = aa.add_extruded_profile(
        [(px * 1.09, py * 1.12) for px, py in habaki_pts],
        depth=0.010,
        axis='Z',
        centered=False,
        location=(0.0, 0.0, 0.092),
        shade_smooth=False,
        name='Odachi_HabakiTrim',
        color='brass',
    )
    aa.paint_mesh(habaki_collar, color='brass', ao_strength=0.18, ground_ao=0.02)
    body_parts.append(habaki_collar)

    # 4. Menacing Longer Odachi Blade: Z = +0.095 .. +1.250, curving +0.042 toward +X
    b_body, b_sig = build_curved_single_edged_blade(
        name_prefix='Odachi',
        z_start=0.095,
        z_tip=1.250,
        tip_sori_x=0.042,
        base_spine_x=0.0205,
        base_width=0.045,
        yokote_width=0.032,
        yokote_s=0.845,
        shinogi_half_t=0.0076,
        spine_gradient=((0.026, 0.030, 0.044), (0.088, 0.096, 0.128)),
        plasma_color=ODACHI_CRIMSON,
        stations=14,
        fuller_stations=9,
        fuller_s_range=(0.048, 0.835),
        spine_vent_notches=6,
    )
    body_parts.extend(b_body)
    sig_parts.extend(b_sig)

    # 5. Vented Heat Slots & Integrated Louver Bridges along Obsidian Fuller Spine
    blade_len = 1.250 - 0.095
    lin_d = -0.0055
    q_gain = (0.042 - 0.0205) - lin_d

    def odachi_spine_x(s: float) -> float:
        return 0.0205 + lin_d * s + q_gain * (s ** 1.90)

    def odachi_w(s: float) -> float:
        return _lerp(0.045, 0.032, (s / 0.845) ** 0.88)

    vent_count = 6
    s_v0, s_v1 = 0.13, 0.74
    for v_i in range(vent_count + 1):
        s_rib = _lerp(s_v0, s_v1, v_i / vent_count)
        z_rib = 0.095 + s_rib * blade_len
        xs = odachi_spine_x(s_rib)
        w = odachi_w(s_rib)
        ts = _lerp(1.0, 0.80, s_rib / 0.845)
        rib_col = 'brass' if v_i in (0, vent_count) else 'obsidian'
        rib = aa.add_box(
            name=f'Odachi_VentRib_{v_i}',
            size=(w * 0.22, 0.0116 * ts, 0.0085),
            location=(xs - w * 0.25, 0.0, z_rib),
            rotation=(0.0, 0.18, 0.0),
            taper=(0.86, 0.90),
            color=rib_col,
        )
        aa.paint_mesh(rib, color=rib_col, ao_strength=0.22, ground_ao=0.02)
        body_parts.append(rib)

        if v_i < vent_count:
            s_slot = _lerp(s_v0, s_v1, (v_i + 0.5) / vent_count)
            z_slot = 0.095 + s_slot * blade_len
            xs_s = odachi_spine_x(s_slot)
            w_s = odachi_w(s_slot)
            ts_s = _lerp(1.0, 0.80, s_slot / 0.845)
            slot = aa.add_box(
                name=f'Odachi_HeatSlot_{v_i}',
                size=(w_s * 0.20, 0.0086 * ts_s, 0.046),
                location=(xs_s - w_s * 0.15, 0.0, z_slot),
                color=ODACHI_CRIMSON,
                signal=True,
            )
            sig_parts.append(slot)

    merged = aa.combine_by_role(body_parts, sig_parts)
    out_path = OUT_DIR / 'ren-odachi.glb'
    info = aa.export_glb(out_path, merged, min_tris=551, max_meshes=6)
    assert 1000 <= info['triangles'] <= 2000, (
        f"ren-odachi.glb triangle count {info['triangles']} outside target 1000..2000"
    )
    print(f"[build_aa_blades] Exported {info['path']}: {info['triangles']} tris, {info['meshes']} meshes, {info['size_kb']} KB")
    return out_path


# ---------------------------------------------------------------------------
# 3. Kael's Komorebi Seed Lantern — public/street/spirit-lantern.glb
# ---------------------------------------------------------------------------

def _paint_spirit_crystal(obj: bpy.types.Object) -> bpy.types.Object:
    """Paint faceted spirit crystal with luminous amber heart transitioning to komorebi cyan tips."""
    aa.paint_mesh(obj, color=SPIRIT_CYAN, signal=True)
    mesh = obj.data
    col_layer = mesh.color_attributes['Color']
    verts = mesh.vertices
    zs = [v.co.z for v in verts]
    z_mid = 0.5 * (min(zs) + max(zs))
    half_span = max(1e-4, 0.5 * (max(zs) - min(zs)))

    for poly in mesh.polygons:
        f_boost = 0.94 + 0.06 * abs(poly.normal.x * 0.6 + poly.normal.y * 0.8)
        for li in poly.loop_indices:
            vi = mesh.loops[li].vertex_index
            co = verts[vi].co
            dist_z = min(1.0, abs(co.z - z_mid) / half_span)
            blend = _smoothstep(0.12, 0.88, dist_z)
            r, g, b = _lerp_rgb(SPIRIT_AMBER, SPIRIT_CYAN, blend)
            col_layer.data[li].color = (
                min(1.0, r * f_boost),
                min(1.0, g * f_boost),
                min(1.0, b * f_boost),
                1.0,
            )
    mesh.update()
    return obj


def build_spirit_lantern() -> Path:
    """Sculpt Kael's Komorebi Seed Lantern (~800–1,400 tris, ~0.28m tall) -> public/street/spirit-lantern.glb."""
    aa.reset_scene()
    body_parts: list[bpy.types.Object] = []
    sig_parts: list[bpy.types.Object] = []

    # Total vertical span: Z = -0.140 .. +0.140 (0.280 m tall, centered at origin)

    # 1. Bottom Finial & Hexagonal Neo-Kyoto Pedestal (Z = -0.140 .. -0.066)
    finial_tip = aa.add_cone(
        name='Lantern_FinialTip',
        radius1=0.0025,
        radius2=0.017,
        depth=0.022,
        vertices=6,
        location=(0.0, 0.0, -0.129),
        shade_smooth=False,
        color='brass',
    )
    aa.paint_mesh(finial_tip, color='brass', ao_strength=0.18, ground_ao=0.02)
    body_parts.append(finial_tip)

    finial_collar = aa.add_cylinder(
        name='Lantern_FinialCollar',
        radius=0.023,
        radius_top=0.018,
        depth=0.012,
        vertices=6,
        location=(0.0, 0.0, -0.112),
        shade_smooth=False,
        color='obsidian',
    )
    aa.paint_mesh(finial_collar, color='obsidian', ao_strength=0.22, ground_ao=0.02)
    body_parts.append(finial_collar)

    base_foot = aa.add_cylinder(
        name='Lantern_BaseFoot',
        radius=0.063,
        radius_top=0.052,
        depth=0.018,
        vertices=6,
        location=(0.0, 0.0, -0.097),
        bevel=0.0022,
        bevel_segments=1,
        shade_smooth=False,
        color='obsidian',
    )
    aa.paint_mesh(base_foot, color='obsidian', ao_strength=0.24, ground_ao=0.03)
    body_parts.append(base_foot)

    for i in range(6):
        ang = (i / 6.0) * math.tau
        fx = math.cos(ang) * 0.059
        fy = math.sin(ang) * 0.059
        foot = aa.add_box(
            name=f'Lantern_CornerFoot_{i}',
            size=(0.013, 0.010, 0.012),
            location=(fx, fy, -0.100),
            rotation=(0.0, 0.0, ang),
            taper=(0.82, 0.85),
            color='brass',
        )
        aa.paint_mesh(foot, color='brass', ao_strength=0.18, ground_ao=0.02)
        body_parts.append(foot)

    base_deck = aa.add_cylinder(
        name='Lantern_GalleryDeck',
        radius=0.056,
        radius_top=0.054,
        depth=0.014,
        vertices=6,
        location=(0.0, 0.0, -0.081),
        shade_smooth=False,
        color='brass',
    )
    aa.paint_mesh(base_deck, color='brass', ao_strength=0.18, ground_ao=0.02)
    body_parts.append(base_deck)

    base_socket = build_hex_ring_frame(
        z_center=-0.070,
        radius_outer=0.055,
        radius_inner=0.042,
        thickness=0.008,
        sides=6,
        name='Lantern_LowerCollar',
        color='asphalt_dark',
        ground_ao=0.02,
    )
    body_parts.append(base_socket)

    rune_ring = build_hex_ring_frame(
        z_center=-0.068,
        radius_outer=0.036,
        radius_inner=0.025,
        thickness=0.004,
        sides=6,
        name='Lantern_FloorSeal',
        color=SPIRIT_AMBER,
        signal=True,
    )
    sig_parts.append(rune_ring)

    # 2. Hexagonal Brass & Dark-Iron Cage (Z = -0.068 .. +0.054)
    r_bot_cage = 0.051
    r_top_cage = 0.058
    z_bot_cage = -0.068
    z_top_cage = 0.054
    z_mid_cage = 0.5 * (z_bot_cage + z_top_cage)
    r_mid_cage = 0.5 * (r_bot_cage + r_top_cage)
    strut_vec = Vector((r_top_cage - r_bot_cage, 0.0, z_top_cage - z_bot_cage))
    strut_len = strut_vec.length
    strut_tilt = -math.atan2(r_top_cage - r_bot_cage, z_top_cage - z_bot_cage)

    for i in range(6):
        ang = (i / 6.0) * math.tau
        ang_next = ((i + 1) / 6.0) * math.tau
        ca, sa = math.cos(ang), math.sin(ang)
        cb, sb = math.cos(ang_next), math.sin(ang_next)

        strut = aa.add_cylinder(
            name=f'Lantern_Strut_{i}',
            radius=0.0046,
            radius_top=0.0044,
            depth=strut_len,
            vertices=6,
            location=(ca * r_mid_cage, sa * r_mid_cage, z_mid_cage),
            rotation=(0.0, -strut_tilt, ang),
            shade_smooth=False,
            color='obsidian',
        )
        aa.paint_mesh(strut, color='obsidian', ao_strength=0.24, ground_ao=0.02)
        body_parts.append(strut)

        sleeve = aa.add_cylinder(
            name=f'Lantern_StrutSleeve_{i}',
            radius=0.0066,
            depth=0.015,
            vertices=6,
            location=(ca * r_mid_cage, sa * r_mid_cage, z_mid_cage),
            shade_smooth=False,
            color='brass',
        )
        aa.paint_mesh(sleeve, color='brass', ao_strength=0.18, ground_ao=0.02)
        body_parts.append(sleeve)

        edge_yaw = ang + math.pi * (2.0 / 3.0)
        for z_bar, r_bar, col_bar, tag in (
            (z_mid_cage, r_mid_cage, 'brass', 'WaistBar'),
            (z_top_cage - 0.013, _lerp(r_bot_cage, r_top_cage, 0.89), 'asphalt_dark', 'TransomBar'),
        ):
            mx = 0.5 * (ca + cb) * r_bar
            my = 0.5 * (sa + sb) * r_bar
            bar = aa.add_box(
                name=f'Lantern_{tag}_{i}',
                size=(r_bar * 0.96, 0.0044, 0.0052),
                location=(mx, my, z_bar),
                rotation=(0.0, 0.0, edge_yaw),
                taper=(0.94, 0.86),
                color=col_bar,
            )
            aa.paint_mesh(bar, color=col_bar, ao_strength=0.20, ground_ao=0.02)
            body_parts.append(bar)

    upper_collar = build_hex_ring_frame(
        z_center=0.053,
        radius_outer=0.062,
        radius_inner=0.048,
        thickness=0.0085,
        sides=6,
        name='Lantern_UpperCollar',
        color='brass',
        ground_ao=0.02,
    )
    body_parts.append(upper_collar)

    # 3. Faceted Floating Amber-Cyan Spirit Crystal ('Signal') centered inside cage
    crystal_stations = [
        (-0.040, 0.012, 0.0),
        (-0.022, 0.024, 0.08),
        (-0.004, 0.027, 0.22),
        (+0.014, 0.022, 0.34),
        (+0.029, 0.011, 0.44),
    ]
    crystal_rings = []
    for zc, rc, twist in crystal_stations:
        ring = []
        for i in range(6):
            ang = twist + (i / 6.0) * math.tau
            ring.append((math.cos(ang) * rc, math.sin(ang) * rc, zc))
        crystal_rings.append(ring)

    main_crystal = build_lofted_solid(
        crystal_rings,
        name='Lantern_SpiritCrystal',
        color=SPIRIT_CYAN,
        signal=True,
        shade_smooth=False,
        base_point=(0.0, 0.0, -0.052),
        tip_point=(0.0, 0.0, +0.041),
    )
    _paint_spirit_crystal(main_crystal)
    sig_parts.append(main_crystal)

    mote_specs = [
        ((+0.030, +0.012, -0.024), 0.0065, 0.017, SPIRIT_AMBER, (0.22, -0.18, 0.4)),
        ((-0.028, +0.015, +0.014), 0.0060, 0.016, SPIRIT_CYAN, (-0.25, 0.20, 1.1)),
        ((+0.004, -0.031, +0.002), 0.0055, 0.015, SPIRIT_AMBER, (0.18, 0.28, 2.3)),
    ]
    for m_idx, (m_loc, m_r, m_h, m_col, m_rot) in enumerate(mote_specs):
        mote_rings = [
            [(math.cos(k * math.tau / 5) * m_r * 0.75, math.sin(k * math.tau / 5) * m_r * 0.75, -m_h * 0.22) for k in range(5)],
            [(math.cos(k * math.tau / 5 + 0.3) * m_r, math.sin(k * math.tau / 5 + 0.3) * m_r, +m_h * 0.18) for k in range(5)],
        ]
        mote = build_lofted_solid(
            mote_rings,
            name=f'Lantern_SpiritMote_{m_idx}',
            color=m_col,
            signal=True,
            shade_smooth=False,
            base_point=(0.0, 0.0, -m_h * 0.55),
            tip_point=(0.0, 0.0, +m_h * 0.55),
        )
        xform = Matrix.Translation(m_loc) @ Euler(m_rot, 'XYZ').to_matrix().to_4x4()
        mote.data.transform(xform)
        mote.data.update()
        aa.paint_mesh(mote, color=m_col, signal=True)
        sig_parts.append(mote)

    # 4. Flared Hexagonal Neo-Kyoto Pagoda Roof & Smoke Cupola (Z = +0.056 .. +0.106)
    roof_stations = [
        (0.056, 0.058),
        (0.061, 0.084),
        (0.072, 0.064),
        (0.085, 0.038),
    ]
    roof_rings = []
    for zr, rr in roof_stations:
        roof_rings.append([(math.cos(i * math.tau / 6) * rr, math.sin(i * math.tau / 6) * rr, zr) for i in range(6)])

    pagoda_roof = build_lofted_solid(
        roof_rings,
        name='Lantern_PagodaRoof',
        color='obsidian',
        shade_smooth=False,
        ao_strength=0.25,
        top_highlight=0.13,
        ridge_boost=0.12,
        ground_ao=0.02,
        base_point=(0.0, 0.0, 0.055),
        tip_point=(0.0, 0.0, 0.088),
    )
    body_parts.append(pagoda_roof)

    for i in range(6):
        ang = (i / 6.0) * math.tau
        ca, sa = math.cos(ang), math.sin(ang)
        rafter = aa.add_box(
            name=f'Lantern_RoofRafter_{i}',
            size=(0.052, 0.0058, 0.0055),
            location=(ca * 0.062, sa * 0.062, 0.072),
            rotation=(0.0, 0.44, ang),
            taper=(0.78, 0.85),
            color='brass',
        )
        aa.paint_mesh(rafter, color='brass', ao_strength=0.18, ground_ao=0.02)
        body_parts.append(rafter)

        eave_tip = aa.add_cone(
            name=f'Lantern_EaveTip_{i}',
            radius1=0.0045,
            radius2=0.0012,
            depth=0.012,
            vertices=5,
            location=(ca * 0.085, sa * 0.085, 0.065),
            rotation=(0.0, -0.55, ang),
            shade_smooth=False,
            color='brass',
        )
        aa.paint_mesh(eave_tip, color='brass', ao_strength=0.18, ground_ao=0.02)
        body_parts.append(eave_tip)

    cupola = aa.add_cylinder(
        name='Lantern_Cupola',
        radius=0.034,
        radius_top=0.028,
        depth=0.014,
        vertices=6,
        location=(0.0, 0.0, 0.091),
        shade_smooth=False,
        color='brass',
    )
    aa.paint_mesh(cupola, color='brass', ao_strength=0.18, ground_ao=0.02)
    body_parts.append(cupola)

    cupola_glow = aa.add_cylinder(
        name='Lantern_CupolaVentGlow',
        radius=0.0305,
        depth=0.0045,
        vertices=6,
        location=(0.0, 0.0, 0.092),
        shade_smooth=False,
        color=SPIRIT_CYAN,
        signal=True,
    )
    sig_parts.append(cupola_glow)

    crown_cap = aa.add_cone(
        name='Lantern_CrownCap',
        radius1=0.031,
        radius2=0.006,
        depth=0.012,
        vertices=6,
        location=(0.0, 0.0, 0.102),
        shade_smooth=False,
        color='obsidian',
    )
    aa.paint_mesh(crown_cap, color='obsidian', ao_strength=0.22, ground_ao=0.02)
    body_parts.append(crown_cap)

    # 5. Arched Dark-Iron Bail Handle & Top Brass Ring reaching Z = +0.140
    for x_sign in (-1.0, 1.0):
        lug = aa.add_box(
            name=f'Lantern_BailLug_{int(x_sign)}',
            size=(0.009, 0.011, 0.018),
            location=(x_sign * 0.048, 0.0, 0.084),
            taper=(0.82, 0.85),
            color='brass',
        )
        aa.paint_mesh(lug, color='brass', ao_strength=0.18, ground_ao=0.02)
        body_parts.append(lug)

    arch_pts = []
    arch_steps = 7
    for k in range(arch_steps + 1):
        theta = math.pi * (1.0 - k / arch_steps)
        ax = math.cos(theta) * 0.048
        az = 0.086 + math.sin(theta) * 0.040
        arch_pts.append((ax, 0.0, az))

    bail = aa.add_pipe(
        arch_pts,
        radius=0.0044,
        bevel_resolution=1,
        name='Lantern_BailArch',
        color='obsidian',
        ao_strength=0.22,
    )
    aa.paint_mesh(bail, color='obsidian', ao_strength=0.22, ground_ao=0.02)
    body_parts.append(bail)

    top_ring = aa.add_torus(
        name='Lantern_HangingRing',
        major_radius=0.0080,
        minor_radius=0.0025,
        major_segments=8,
        minor_segments=5,
        location=(0.0, 0.0, 0.1295),
        rotation=(math.pi * 0.5, 0.0, 0.0),
        shade_smooth=False,
        color='brass',
    )
    aa.paint_mesh(top_ring, color='brass', ao_strength=0.18, ground_ao=0.02)
    body_parts.append(top_ring)

    merged = aa.combine_by_role(body_parts, sig_parts)
    out_path = OUT_DIR / 'spirit-lantern.glb'
    info = aa.export_glb(out_path, merged, min_tris=551, max_meshes=6)
    assert 800 <= info['triangles'] <= 1400, (
        f"spirit-lantern.glb triangle count {info['triangles']} outside target 800..1400"
    )
    print(f"[build_aa_blades] Exported {info['path']}: {info['triangles']} tris, {info['meshes']} meshes, {info['size_kb']} KB")
    return out_path


def main() -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    katana_path = build_cyber_katana()
    odachi_path = build_ren_odachi()
    lantern_path = build_spirit_lantern()

    for glb_path in (katana_path, odachi_path, lantern_path):
        aa.verify_glb_file(glb_path, expect_rig=False, max_meshes=6, min_tris=551)
    print('[build_aa_blades] All 3 hero weapon/gear GLBs built and verified.')


if __name__ == '__main__':
    try:
        main()
    except Exception:
        traceback.print_exc()
        sys.exit(1)
