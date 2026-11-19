"""Headless Blender 4.2 LTS realistic AAA street specialist sculptor for Komorebi.

Builds two photorealistic anatomical cyberpunk specialists from the MakeHuman base
(public/figure/starter-man.glb) + bespoke smooth-beveled gear & PBR material classes:
  1. public/street/courier.glb   — Syndicate Hooded Data-Runner
  2. public/street/executive.glb — Aethelgard Corporate Executive
"""

from __future__ import annotations

import math
import sys
from pathlib import Path

sys.dont_write_bytecode = True
TOOLS_DIR = Path(__file__).resolve().parent
ROOT_DIR = TOOLS_DIR.parent
OUT_DIR = ROOT_DIR / 'public' / 'street'
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

import bpy
import blender_aa_lib as aa
import blender_aaa_humans as ah


def _v(joints, name: str, idx: int = 0):
    return joints[name][idx]


def build_courier(out_path: Path) -> dict:
    """Syndicate hooded data-runner: windbreaker hood, mirror cyber-visor, armored cyber-deck pack."""
    aa.reset_scene()
    h = 1.79
    base = ah.load_realistic_human(
        height=h,
        bulk=(0.96, 0.96),
        skin_col=(0.76, 0.55, 0.42),
        jacket_col=(0.09, 0.14, 0.22),
        yoke_col=(0.08, 0.36, 0.44),
        sleeve_col=(0.08, 0.10, 0.14),
        pants_col=(0.07, 0.085, 0.115),
        belt_col=(0.05, 0.055, 0.07),
        boot_col=(0.08, 0.10, 0.14),
        glove_col=(0.05, 0.055, 0.068),
        keep_hair=False,
        jacket_mat=aa.MAT_POLYMER,
        pants_mat=aa.MAT_CLOTH,
        sleeve_mat=aa.MAT_CLOTH,
    )
    parts = list(base['parts'])
    signals = []
    j = base['joints']

    # Sculpted curved aerodynamic windbreaker hood framing real human face & jawline
    hx, hy, hz = _v(j, 'Head')
    hood = aa.add_sphere(
        name='Runner_Hood', radius=0.128, segments=22, rings=14,
        location=(hx, hy + 0.024, hz + 0.078), scale=(1.02, 1.15, 1.06),
        color=(0.09, 0.14, 0.22), bone='Head',
    )
    aa.paint_mesh(hood, color=(0.09, 0.14, 0.22), mat_class=aa.MAT_CLOTH, ao_strength=0.28)
    parts.append(hood)

    # Smoked mirror wrap-around cyber-visor + razor cyan LED accent
    visor_glass = aa.add_bevel_box(
        name='Courier_Visor_Glass', size=(0.145, 0.052, 0.042),
        location=(hx, hy - 0.086, hz + 0.054), bevel=0.014, segments=2,
        color='glass_dark', bone='Head',
    )
    aa.paint_mesh(visor_glass, color='glass_dark', mat_class=aa.MAT_GLASS)
    parts.append(visor_glass)
    visor_sig = aa.add_bevel_box(
        name='Courier_Visor_Signal', size=(0.132, 0.012, 0.010),
        location=(hx, hy - 0.111, hz + 0.054), bevel=0.003, segments=1,
        color='neon_cyan', signal=True, bone='Head',
    )
    signals.append(visor_sig)

    # Armored dorsal cyber-deck courier case with glowing telemetry strip
    cx, cy, cz = _v(j, 'Chest')
    deck = aa.add_bevel_box(
        name='Cyber_Deck_Pack', size=(0.24, 0.105, 0.34),
        location=(cx, cy + 0.155, cz - 0.01), bevel=0.022, segments=2,
        color='gunmetal', bone='Chest',
    )
    aa.paint_mesh(deck, color='gunmetal', mat_class=aa.MAT_METAL)
    parts.append(deck)
    spine_led = aa.add_bevel_box(
        name='Deck_Telemetry_LED', size=(0.022, 0.014, 0.22),
        location=(cx, cy + 0.208, cz - 0.01), bevel=0.004, segments=1,
        color='neon_magenta', signal=True, bone='Chest',
    )
    signals.append(spine_led)

    batched = aa.combine_by_role(parts, signals)
    rig = ah.build_realistic_rig_and_clips(batched, j, rig_name='CourierRig', height=h, style='citizen')
    return aa.export_glb(out_path, [rig, *batched])


def build_executive(out_path: Path) -> dict:
    """Aethelgard corporate executive: tailored dark suit, gold-rimmed amber optics, brushed metal trim."""
    aa.reset_scene()
    h = 1.83
    base = ah.load_realistic_human(
        height=h,
        bulk=(1.01, 1.00),
        skin_col=(0.85, 0.66, 0.54),
        jacket_col=(0.075, 0.085, 0.125),
        yoke_col=(0.12, 0.13, 0.18),
        sleeve_col=(0.075, 0.085, 0.125),
        pants_col=(0.07, 0.08, 0.115),
        belt_col=(0.05, 0.045, 0.05),
        boot_col=(0.06, 0.055, 0.06),
        hair_col=(0.14, 0.14, 0.16),
        iris_col=(0.30, 0.24, 0.16),
        jacket_mat=aa.MAT_CLOTH,
        pants_mat=aa.MAT_CLOTH,
    )
    parts = list(base['parts'])
    signals = []
    j = base['joints']

    # Structured high executive collar with brushed brass trim
    nx, ny, nz = _v(j, 'Neck')
    collar = aa.add_cylinder(
        name='Executive_Collar', radius=0.082, radius_top=0.088, height=0.075, vertices=22,
        location=(nx, ny + 0.004, nz - 0.015), color=(0.075, 0.085, 0.125), bone='Chest',
    )
    aa.paint_mesh(collar, color=(0.075, 0.085, 0.125), mat_class=aa.MAT_CLOTH)
    parts.append(collar)

    # Gold-bridged smoked cyber-optic glasses over realistic human eyes
    hx, hy, hz = _v(j, 'Head')
    bridge = aa.add_bevel_box(
        name='Optic_Gold_Bridge', size=(0.128, 0.022, 0.008),
        location=(hx, hy - 0.098, hz + 0.062), bevel=0.002, segments=1,
        color='brass', bone='Head',
    )
    aa.paint_mesh(bridge, color='brass', mat_class=aa.MAT_METAL)
    parts.append(bridge)
    for sx in (-0.032, 0.032):
        lens = aa.add_bevel_box(
            name=f'Optic_Lens_{"L" if sx > 0 else "R"}', size=(0.044, 0.012, 0.024),
            location=(hx + sx, hy - 0.104, hz + 0.054), bevel=0.004, segments=1,
            color='neon_amber', signal=True, bone='Head',
        )
        signals.append(lens)

    # Gold corporate lapel data-pin
    cx, cy, cz = _v(j, 'Chest')
    pin_sig = aa.add_bevel_box(
        name='Corp_Lapel_Pin', size=(0.042, 0.012, 0.012),
        location=(cx + 0.085, cy - 0.132, cz + 0.075), bevel=0.003, segments=1,
        color='neon_amber', signal=True, bone='Chest',
    )
    signals.append(pin_sig)

    batched = aa.combine_by_role(parts, signals)
    rig = ah.build_realistic_rig_and_clips(batched, j, rig_name='ExecutiveRig', height=h, style='citizen')
    return aa.export_glb(out_path, [rig, *batched])


def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for fn, name in ((build_courier, 'courier.glb'), (build_executive, 'executive.glb')):
        info = fn(OUT_DIR / name)
        print(f"[specialists] {name}: {info['triangles']} tris, {info['size_kb']} KB", flush=True)


if __name__ == '__main__':
    main()
