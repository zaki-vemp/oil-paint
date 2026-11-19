"""Headless Blender 4.2 LTS realistic AAA enemy infantry sculptor for Komorebi.

Builds three photorealistic anatomical cyberpunk enemy classes from the MakeHuman base
(public/figure/starter-man.glb) + smooth-beveled tactical armor, prosthetics, helmets & weapons:
  1. public/street/foe-punk.glb     — Neon Viper Street Punk (punk kind)
  2. public/street/foe-enforcer.glb — Aethelgard Riot Enforcer (enforcer kind)
  3. public/street/foe-gunner.glb   — Hazmat Pulse Gunner (gunner kind, adds Idle_Shoot)
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


def build_punk(out_path: Path) -> dict:
    """Neon Viper street punk: realistic anatomy, biker leather vest, chrome cyber-arm, respirator, plasma tanto."""
    aa.reset_scene()
    h = 1.79
    base = ah.load_realistic_human(
        height=h,
        bulk=(1.01, 1.00),
        skin_col=(0.80, 0.59, 0.45),
        jacket_col=(0.08, 0.065, 0.095),
        yoke_col=(0.36, 0.065, 0.19),
        sleeve_col=None,
        pants_col=(0.10, 0.12, 0.18),
        belt_col=(0.06, 0.05, 0.06),
        boot_col=(0.05, 0.05, 0.065),
        keep_hair=False,
        cyber_right_arm=True,
        bare_left_arm=True,
        jacket_mat=aa.MAT_LEATHER,
        pants_mat=aa.MAT_CLOTH,
    )
    parts = list(base['parts'])
    signals = []
    j = base['joints']

    # Spiked neon-tipped mohawk crest along crown of realistic human head
    hx, hy, hz = _v(j, 'Head')
    for i in range(6):
        t = i / 5.0
        ang = -0.35 + t * 1.15
        mx = hx
        my = hy - 0.055 + t * 0.145
        mz = hz + 0.165 - abs(t - 0.35) * 0.035
        root = aa.add_cone(
            name=f'Mohawk_Root_{i}', radius1=0.022, radius2=0.008, height=0.065, vertices=10,
            location=(mx, my, mz), rotation=(ang, 0.0, 0.0), color=(0.24, 0.04, 0.14), bone='Head',
        )
        aa.paint_mesh(root, color=(0.24, 0.04, 0.14), mat_class=aa.MAT_HAIR)
        parts.append(root)
        tip = aa.add_cone(
            name=f'Mohawk_Tip_{i}', radius1=0.011, radius2=0.002, height=0.042, vertices=8,
            location=(mx, my + math.sin(ang) * 0.035, mz + math.cos(ang) * 0.035),
            rotation=(ang, 0.0, 0.0), color='neon_magenta', signal=True, bone='Head',
        )
        signals.append(tip)

    # Half-face metallic street respirator + glowing crimson visor
    resp = aa.add_bevel_box(
        name='Punk_Respirator', size=(0.095, 0.065, 0.058),
        location=(hx, hy - 0.088, hz - 0.005), bevel=0.014, segments=2,
        color='gunmetal', bone='Head',
    )
    aa.paint_mesh(resp, color='gunmetal', mat_class=aa.MAT_METAL)
    parts.append(resp)
    visor = aa.add_bevel_box(
        name='Punk_Visor_Sig', size=(0.132, 0.022, 0.024),
        location=(hx, hy - 0.098, hz + 0.052), bevel=0.006, segments=1,
        color='neon_magenta', signal=True, bone='Head',
    )
    signals.append(visor)

    # Polished chrome shoulder spikes on leather biker vest
    for side, sx in (('L', 1.0), ('R', -1.0)):
        shx, shy, shz = _v(j, f'UpperArm_{side}')
        for k in range(3):
            sp = aa.add_cone(
                name=f'Shoulder_Spike_{side}_{k}', radius1=0.014, radius2=0.002, height=0.055, vertices=10,
                location=(shx + sx * 0.015, shy - 0.03 + k * 0.03, shz + 0.055),
                rotation=(0.0, -sx * 0.35, 0.0), color='chrome', bone=f'UpperArm_{side}',
            )
            aa.paint_mesh(sp, color='chrome', mat_class=aa.MAT_METAL)
            parts.append(sp)

    # Forged metallic plasma tanto held in right hand
    rx, ry, rz = _v(j, 'Hand_R')
    grip = aa.add_cylinder(
        name='Tanto_Grip', radius=0.016, height=0.14, vertices=12,
        location=(rx, ry - 0.04, rz - 0.02), rotation=(math.pi * 0.5, 0.0, 0.0),
        color='leather_dark', bone='Hand_R',
    )
    aa.paint_mesh(grip, color='leather_dark', mat_class=aa.MAT_LEATHER)
    parts.append(grip)
    spine = aa.add_bevel_box(
        name='Tanto_Spine', size=(0.012, 0.34, 0.028),
        location=(rx, ry - 0.26, rz - 0.015), bevel=0.004, segments=1,
        color='steel', bone='Hand_R',
    )
    aa.paint_mesh(spine, color='steel', mat_class=aa.MAT_METAL)
    parts.append(spine)
    edge = aa.add_bevel_box(
        name='Tanto_Plasma_Edge', size=(0.008, 0.35, 0.012),
        location=(rx, ry - 0.265, rz - 0.032), bevel=0.002, segments=1,
        color='neon_magenta', signal=True, bone='Hand_R',
    )
    signals.append(edge)

    batched = aa.combine_by_role(parts, signals)
    rig = ah.build_realistic_rig_and_clips(batched, j, rig_name='PunkRig', height=h, style='punk', weapon_hand='Hand_R')
    return aa.export_glb(out_path, [rig, *batched])


def build_enforcer(out_path: Path) -> dict:
    """Aethelgard riot enforcer: broad anatomical build in ballistic titanium armor, riot helm & shock-cleaver."""
    aa.reset_scene()
    h = 1.86
    base = ah.load_realistic_human(
        height=h,
        bulk=(1.12, 1.09),
        skin_col=(0.74, 0.56, 0.44),
        jacket_col=(0.14, 0.17, 0.23),
        yoke_col=(0.08, 0.09, 0.12),
        sleeve_col=(0.08, 0.09, 0.12),
        pants_col=(0.09, 0.10, 0.14),
        belt_col=(0.06, 0.06, 0.08),
        boot_col=(0.07, 0.08, 0.10),
        glove_col=(0.06, 0.06, 0.08),
        keep_hair=False,
        keep_head=False,
        jacket_mat=aa.MAT_POLYMER,
        pants_mat=aa.MAT_POLYMER,
        sleeve_mat=aa.MAT_POLYMER,
    )
    parts = list(base['parts'])
    signals = []
    j = base['joints']

    # Solid armored neck gorget + seated full-face tactical riot helmet + crimson T-optic
    parts.extend(ah.add_armored_neck_seal(j, collar_color=(0.10, 0.12, 0.16)))
    hx, hy, hz = _v(j, 'Head')
    helm = aa.add_sphere(
        name='Riot_Helmet', radius=0.132, segments=24, rings=16,
        location=(hx, hy + 0.006, hz + 0.044), scale=(1.02, 1.14, 1.06),
        color=(0.16, 0.19, 0.25), bone='Head',
    )
    aa.paint_mesh(helm, color=(0.16, 0.19, 0.25), mat_class=aa.MAT_METAL, ao_strength=0.26)
    parts.append(helm)
    brow = aa.add_bevel_box(
        name='Riot_Brow_Shield', size=(0.22, 0.13, 0.055),
        location=(hx, hy - 0.068, hz + 0.092), rotation=(0.16, 0.0, 0.0), bevel=0.014, segments=2,
        color=(0.14, 0.16, 0.22), bone='Head',
    )
    aa.paint_mesh(brow, color=(0.14, 0.16, 0.22), mat_class=aa.MAT_METAL)
    parts.append(brow)
    jaw = aa.add_bevel_box(
        name='Riot_Chin_Guard', size=(0.185, 0.135, 0.095),
        location=(hx, hy - 0.062, hz - 0.015), bevel=0.024, segments=2,
        color='carbon', bone='Head',
    )
    aa.paint_mesh(jaw, color='carbon', mat_class=aa.MAT_POLYMER)
    parts.append(jaw)
    t_horiz = aa.add_bevel_box(
        name='Riot_Visor_Bar', size=(0.145, 0.025, 0.022),
        location=(hx, hy - 0.128, hz + 0.044), bevel=0.006, segments=1,
        color='neon_crimson', signal=True, bone='Head',
    )
    t_vert = aa.add_bevel_box(
        name='Riot_Visor_Stem', size=(0.026, 0.025, 0.082),
        location=(hx, hy - 0.128, hz + 0.008), bevel=0.006, segments=1,
        color='neon_crimson', signal=True, bone='Head',
    )
    signals.extend([t_horiz, t_vert])

    # Segmented beveled titanium ballistic chestplate & back power pack
    cx, cy, cz = _v(j, 'Chest')
    chest_plate = aa.add_bevel_box(
        name='Ballistic_Chest_Plate', size=(0.34, 0.12, 0.30),
        location=(cx, cy - 0.105, cz + 0.01), bevel=0.028, segments=2,
        color=(0.18, 0.21, 0.27), bone='Chest',
    )
    aa.paint_mesh(chest_plate, color=(0.18, 0.21, 0.27), mat_class=aa.MAT_METAL)
    parts.append(chest_plate)
    chest_trim = aa.add_bevel_box(
        name='Crimson_Authority_Plate', size=(0.20, 0.022, 0.095),
        location=(cx, cy - 0.165, cz + 0.04), bevel=0.008, segments=2,
        color=(0.56, 0.08, 0.12), bone='Chest',
    )
    aa.paint_mesh(chest_trim, color=(0.56, 0.08, 0.12), mat_class=aa.MAT_CARPAINT)
    parts.append(chest_trim)
    back_pack = aa.add_bevel_box(
        name='Enforcer_Back_Rig', size=(0.28, 0.11, 0.30),
        location=(cx, cy + 0.15, cz + 0.01), bevel=0.024, segments=2,
        color='gunmetal', bone='Chest',
    )
    aa.paint_mesh(back_pack, color='gunmetal', mat_class=aa.MAT_METAL)
    parts.append(back_pack)

    # Heavy beveled armored shoulder pauldrons
    for side, sx in (('L', 1.0), ('R', -1.0)):
        shx, shy, shz = _v(j, f'UpperArm_{side}')
        pauldron = aa.add_bevel_box(
            name=f'Riot_Pauldron_{side}', size=(0.16, 0.21, 0.14),
            location=(shx + sx * 0.04, shy, shz + 0.03), bevel=0.032, segments=2,
            color=(0.20, 0.24, 0.30), bone=f'UpperArm_{side}',
        )
        aa.paint_mesh(pauldron, color=(0.20, 0.24, 0.30), mat_class=aa.MAT_METAL)
        parts.append(pauldron)
        p_sig = aa.add_bevel_box(
            name=f'Pauldron_Sig_{side}', size=(0.022, 0.12, 0.018),
            location=(shx + sx * 0.122, shy, shz + 0.045), bevel=0.004, segments=1,
            color='neon_crimson', signal=True, bone=f'UpperArm_{side}',
        )
        signals.append(p_sig)

    # Heavy forearm riot shield on left forearm
    flx, fly, flz = _v(j, 'Forearm_L')
    shield = aa.add_bevel_box(
        name='Forearm_Riot_Shield', size=(0.045, 0.22, 0.38),
        location=(flx + 0.09, fly - 0.03, flz - 0.06), bevel=0.014, segments=2,
        color='steel', bone='Forearm_L',
    )
    aa.paint_mesh(shield, color='steel', mat_class=aa.MAT_METAL)
    parts.append(shield)

    # Forged tungsten shock-cleaver with blazing cyan arc edge in right hand
    rx, ry, rz = _v(j, 'Hand_R')
    hilt = aa.add_cylinder(
        name='Cleaver_Hilt', radius=0.020, height=0.22, vertices=12,
        location=(rx, ry - 0.04, rz - 0.02), rotation=(math.pi * 0.5, 0.0, 0.0),
        color='leather_dark', bone='Hand_R',
    )
    aa.paint_mesh(hilt, color='leather_dark', mat_class=aa.MAT_LEATHER)
    parts.append(hilt)
    blade = aa.add_bevel_box(
        name='Cleaver_Body', size=(0.024, 0.58, 0.105),
        location=(rx, ry - 0.40, rz - 0.02), bevel=0.008, segments=2,
        color='steel', bone='Hand_R',
    )
    aa.paint_mesh(blade, color='steel', mat_class=aa.MAT_METAL)
    parts.append(blade)
    arc = aa.add_bevel_box(
        name='Cleaver_Shock_Edge', size=(0.012, 0.56, 0.022),
        location=(rx, ry - 0.40, rz - 0.078), bevel=0.004, segments=1,
        color='neon_cyan', signal=True, bone='Hand_R',
    )
    signals.append(arc)

    batched = aa.combine_by_role(parts, signals)
    rig = ah.build_realistic_rig_and_clips(batched, j, rig_name='EnforcerRig', height=h, style='enforcer', weapon_hand='Hand_R')
    return aa.export_glb(out_path, [rig, *batched])


def build_gunner(out_path: Path) -> dict:
    """Hazmat pulse gunner: realistic proportions in NBC yellow/charcoal armor, glass dome helm, twin tanks & pulse rifle."""
    aa.reset_scene()
    h = 1.81
    base = ah.load_realistic_human(
        height=h,
        bulk=(1.04, 1.04),
        skin_col=(0.75, 0.55, 0.42),
        jacket_col=(0.86, 0.66, 0.09),
        yoke_col=(0.11, 0.12, 0.15),
        sleeve_col=(0.82, 0.62, 0.08),
        pants_col=(0.80, 0.60, 0.08),
        belt_col=(0.09, 0.10, 0.12),
        boot_col=(0.08, 0.09, 0.11),
        glove_col=(0.08, 0.09, 0.11),
        keep_hair=False,
        keep_head=False,
        jacket_mat=aa.MAT_POLYMER,
        pants_mat=aa.MAT_POLYMER,
        sleeve_mat=aa.MAT_POLYMER,
    )
    parts = list(base['parts'])
    signals = []
    j = base['joints']

    # Pressurized NBC collar ring + seated hazmat helmet shell + curved smoked mirror faceplate + HUD optics
    parts.extend(ah.add_armored_neck_seal(j, collar_color=(0.12, 0.13, 0.16)))
    hx, hy, hz = _v(j, 'Head')
    dome = aa.add_sphere(
        name='Hazmat_Helm_Shell', radius=0.136, segments=24, rings=16,
        location=(hx, hy + 0.008, hz + 0.044), scale=(1.04, 1.12, 1.05),
        color=(0.88, 0.68, 0.09), bone='Head',
    )
    aa.paint_mesh(dome, color=(0.88, 0.68, 0.09), mat_class=aa.MAT_CARPAINT)
    parts.append(dome)
    rebreather = aa.add_bevel_box(
        name='Hazmat_Rebreather', size=(0.175, 0.11, 0.080),
        location=(hx, hy - 0.068, hz - 0.018), bevel=0.020, segments=2,
        color='gunmetal', bone='Head',
    )
    aa.paint_mesh(rebreather, color='gunmetal', mat_class=aa.MAT_METAL)
    parts.append(rebreather)
    face_glass = aa.add_sphere(
        name='Hazmat_Glass_Faceplate', radius=0.118, segments=22, rings=14,
        location=(hx, hy - 0.038, hz + 0.038), scale=(0.98, 0.94, 0.88),
        color='glass_dark', bone='Head',
    )
    aa.paint_mesh(face_glass, color='glass_dark', mat_class=aa.MAT_GLASS)
    parts.append(face_glass)
    optic_r = aa.add_cylinder(
        name='Target_Optic_Eye', radius=0.022, height=0.020, vertices=16,
        location=(hx - 0.036, hy - 0.134, hz + 0.042), rotation=(math.pi * 0.5, 0.0, 0.0),
        color='neon_crimson', signal=True, bone='Head',
    )
    optic_l = aa.add_cylinder(
        name='Hud_Scan_Eye', radius=0.015, height=0.020, vertices=14,
        location=(hx + 0.034, hy - 0.132, hz + 0.042), rotation=(math.pi * 0.5, 0.0, 0.0),
        color='neon_cyan', signal=True, bone='Head',
    )
    signals.extend([optic_r, optic_l])

    # Twin brushed-steel dorsal plasma canisters + glowing cyan gauge bars
    cx, cy, cz = _v(j, 'Chest')
    for sx in (-0.088, 0.088):
        tank = aa.add_cylinder(
            name=f'Plasma_Tank_{"L" if sx > 0 else "R"}', radius=0.068, height=0.38, vertices=20,
            location=(cx + sx, cy + 0.175, cz + 0.01), bevel=0.018, bevel_segments=2,
            color='steel', bone='Chest',
        )
        aa.paint_mesh(tank, color='steel', mat_class=aa.MAT_METAL)
        parts.append(tank)
        gauge = aa.add_bevel_box(
            name=f'Tank_Gauge_{"L" if sx > 0 else "R"}', size=(0.018, 0.016, 0.19),
            location=(cx + sx, cy + 0.244, cz + 0.01), bevel=0.004, segments=1,
            color='neon_cyan', signal=True, bone='Chest',
        )
        signals.append(gauge)

    # Detailed twin-rail futuristic pulse carbine aimed forward in Hand_R
    rx, ry, rz = _v(j, 'Hand_R')
    rec = aa.add_bevel_box(
        name='Pulse_Rifle_Receiver', size=(0.062, 0.44, 0.105),
        location=(rx, ry - 0.24, rz + 0.02), bevel=0.012, segments=2,
        color='gunmetal', bone='Hand_R',
    )
    aa.paint_mesh(rec, color='gunmetal', mat_class=aa.MAT_METAL)
    parts.append(rec)
    barrel = aa.add_cylinder(
        name='Pulse_Rifle_Barrel', radius=0.020, height=0.30, vertices=16,
        location=(rx, ry - 0.54, rz + 0.035), rotation=(math.pi * 0.5, 0.0, 0.0),
        color='chrome', bone='Hand_R',
    )
    aa.paint_mesh(barrel, color='chrome', mat_class=aa.MAT_METAL)
    parts.append(barrel)
    rail_glow = aa.add_bevel_box(
        name='Pulse_Coil_Glow', size=(0.068, 0.24, 0.018),
        location=(rx, ry - 0.42, rz + 0.04), bevel=0.004, segments=1,
        color='neon_cyan', signal=True, bone='Hand_R',
    )
    signals.append(rail_glow)

    batched = aa.combine_by_role(parts, signals)
    rig = ah.build_realistic_rig_and_clips(
        batched, j, rig_name='GunnerRig', height=h, style='gunner',
        attack_clip_name='Idle_Shoot', weapon_hand='Hand_R',
    )
    return aa.export_glb(out_path, [rig, *batched])


def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for fn, name in ((build_punk, 'foe-punk.glb'), (build_enforcer, 'foe-enforcer.glb'), (build_gunner, 'foe-gunner.glb')):
        info = fn(OUT_DIR / name)
        print(f"[infantry] {name}: {info['triangles']} tris, {info['size_kb']} KB", flush=True)


if __name__ == '__main__':
    main()
