"""Headless Blender 4.2 LTS realistic AAA civilian sculptor for Komorebi.

Builds three photorealistic anatomical Neo-Kyoto citizens from the MakeHuman base
(public/figure/starter-man.glb) + bespoke smooth-beveled cyberpunk gear & PBR material classes:
  1. public/street/resident.glb — Neo-Kyoto Streetwear Citizen
  2. public/street/mechanic.glb — District Cyber-Artisan
  3. public/street/vendor.glb   — Night-Market Chef / Field Medic
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


def _pin(obj, bone: str):
    if obj is not None:
        aa.pin_to_bone(obj, bone)
    return obj


def _v(joints, name: str, idx: int = 0):
    return joints[name][idx]


def build_resident(out_path: Path) -> dict:
    """Neo-Kyoto streetwear citizen: tailored techwear jacket, realistic face/hair, wrist holo-band."""
    aa.reset_scene()
    h = 1.78
    base = ah.load_realistic_human(
        height=h,
        bulk=(0.98, 0.98),
        skin_col=(0.82, 0.62, 0.49),
        jacket_col=(0.16, 0.25, 0.36),
        yoke_col=(0.10, 0.14, 0.20),
        sleeve_col=(0.14, 0.20, 0.30),
        pants_col=(0.12, 0.14, 0.19),
        belt_col=(0.06, 0.05, 0.055),
        boot_col=(0.09, 0.095, 0.11),
        hair_col=(0.09, 0.08, 0.09),
        iris_col=(0.26, 0.18, 0.12),
        jacket_mat=aa.MAT_CLOTH,
        pants_mat=aa.MAT_CLOTH,
    )
    parts = list(base['parts'])
    signals = []
    j = base['joints']

    # Sleek metallic tech-collar clasp + cross-chest lanyard tag
    nx, ny, nz = _v(j, 'Neck')
    collar = aa.add_torus(
        name='Tech_Collar', major_radius=0.072, minor_radius=0.014,
        major_segments=22, minor_segments=10, location=(nx, ny - 0.005, nz - 0.03),
        color=(0.12, 0.16, 0.24), bone='Chest',
    )
    aa.paint_mesh(collar, color=(0.12, 0.16, 0.24), mat_class=aa.MAT_POLYMER)
    parts.append(collar)

    # Subtle glowing cyan holo-wristband on left wrist
    wx, wy, wz = _v(j, 'Hand_L')
    band = aa.add_cylinder(
        name='Holo_Wrist_Band', radius=0.038, height=0.036, vertices=18,
        location=(wx, wy, wz + 0.03), color='gunmetal', bone='Forearm_L',
    )
    aa.paint_mesh(band, color='gunmetal', mat_class=aa.MAT_METAL)
    parts.append(band)
    sig = aa.add_torus(
        name='Holo_Wrist_Signal', major_radius=0.041, minor_radius=0.005,
        major_segments=18, minor_segments=8, location=(wx, wy, wz + 0.03),
        color='neon_cyan', signal=True, bone='Forearm_L',
    )
    signals.append(sig)

    # Compact matte urban sling-bag on back
    cx, cy, cz = _v(j, 'Chest')
    bag = aa.add_bevel_box(
        name='Urban_Sling_Bag', size=(0.24, 0.095, 0.30),
        location=(cx, cy + 0.145, cz - 0.02), bevel=0.025, segments=2,
        color=(0.11, 0.13, 0.17), bone='Chest',
    )
    aa.paint_mesh(bag, color=(0.11, 0.13, 0.17), mat_class=aa.MAT_LEATHER)
    parts.append(bag)
    bag_led = aa.add_bevel_box(
        name='Sling_LED', size=(0.12, 0.012, 0.014),
        location=(cx, cy + 0.194, cz + 0.06), bevel=0.003, segments=1,
        color='neon_cyan', signal=True, bone='Chest',
    )
    signals.append(bag_led)

    batched = aa.combine_by_role(parts, signals)
    rig = ah.build_realistic_rig_and_clips(batched, j, rig_name='ResidentRig', height=h, style='citizen')
    return aa.export_glb(out_path, [rig, *batched])


def build_mechanic(out_path: Path) -> dict:
    """District cyber-artisan: tool vest, metallic prosthetic right arm, optical AR visor monocle."""
    aa.reset_scene()
    h = 1.81
    base = ah.load_realistic_human(
        height=h,
        bulk=(1.04, 1.03),
        skin_col=(0.74, 0.52, 0.39),
        jacket_col=(0.24, 0.22, 0.18),
        yoke_col=(0.68, 0.42, 0.10),
        sleeve_col=(0.18, 0.19, 0.21),
        pants_col=(0.14, 0.16, 0.20),
        belt_col=(0.26, 0.16, 0.09),
        boot_col=(0.12, 0.09, 0.07),
        hair_col=(0.16, 0.12, 0.09),
        iris_col=(0.22, 0.28, 0.32),
        cyber_right_arm=True,
        bare_left_arm=True,
        jacket_mat=aa.MAT_POLYMER,
        pants_mat=aa.MAT_CLOTH,
    )
    parts = list(base['parts'])
    signals = []
    j = base['joints']

    # Armored utility chest rig + brass hardware buckles
    cx, cy, cz = _v(j, 'Chest')
    rig_pack = aa.add_bevel_box(
        name='Artisan_Tool_Pack', size=(0.28, 0.11, 0.32),
        location=(cx, cy + 0.155, cz - 0.01), bevel=0.024, segments=2,
        color='gunmetal', bone='Chest',
    )
    aa.paint_mesh(rig_pack, color='gunmetal', mat_class=aa.MAT_METAL)
    parts.append(rig_pack)

    # Leather belt tool pouches on both hips
    px, py, pz = _v(j, 'Pelvis')
    for sx in (-0.175, 0.175):
        pouch = aa.add_bevel_box(
            name=f'Tool_Pouch_{"L" if sx > 0 else "R"}', size=(0.065, 0.12, 0.13),
            location=(px + sx, py - 0.01, pz - 0.02), bevel=0.014, segments=2,
            color='leather_tan', bone='Pelvis',
        )
        aa.paint_mesh(pouch, color='leather_tan', mat_class=aa.MAT_LEATHER)
        parts.append(pouch)

    # Optical AR visor / eye-piece mounted cleanly over realistic face
    hx, hy, hz = _v(j, 'Head')
    frame = aa.add_bevel_box(
        name='AR_Visor_Frame', size=(0.135, 0.045, 0.032),
        location=(hx, hy - 0.095, hz + 0.045), bevel=0.008, segments=2,
        color='steel', bone='Head',
    )
    aa.paint_mesh(frame, color='steel', mat_class=aa.MAT_METAL)
    parts.append(frame)
    lens = aa.add_bevel_box(
        name='AR_Optic_Lens', size=(0.052, 0.012, 0.024),
        location=(hx + 0.032, hy - 0.114, hz + 0.045), bevel=0.004, segments=1,
        color='neon_amber', signal=True, bone='Head',
    )
    signals.append(lens)
    pack_led = aa.add_bevel_box(
        name='Pack_Gauge', size=(0.10, 0.014, 0.024),
        location=(cx, cy + 0.212, cz + 0.06), bevel=0.004, segments=1,
        color='neon_cyan', signal=True, bone='Chest',
    )
    signals.append(pack_led)

    batched = aa.combine_by_role(parts, signals)
    rig = ah.build_realistic_rig_and_clips(batched, j, rig_name='MechanicRig', height=h, style='citizen')
    return aa.export_glb(out_path, [rig, *batched])


def build_vendor(out_path: Path) -> dict:
    """Night-market vendor / field medic: modern crimson haori coat, canvas apron, hachimaki headband."""
    aa.reset_scene()
    h = 1.76
    base = ah.load_realistic_human(
        height=h,
        bulk=(0.99, 0.99),
        skin_col=(0.84, 0.65, 0.52),
        jacket_col=(0.56, 0.13, 0.16),
        yoke_col=(0.14, 0.15, 0.20),
        sleeve_col=(0.48, 0.12, 0.15),
        pants_col=(0.15, 0.18, 0.22),
        belt_col=(0.82, 0.78, 0.72),
        boot_col=(0.14, 0.13, 0.14),
        hair_col=(0.07, 0.065, 0.075),
        iris_col=(0.20, 0.14, 0.10),
        jacket_mat=aa.MAT_CLOTH,
        pants_mat=aa.MAT_CLOTH,
    )
    parts = list(base['parts'])
    signals = []
    j = base['joints']

    # Crisp woven apron front panel hanging cleanly from waist over upper thighs
    px, py, pz = _v(j, 'Pelvis')
    apron = aa.add_bevel_box(
        name='Market_Apron', size=(0.29, 0.024, 0.42),
        location=(px, py - 0.115, pz - 0.18), bevel=0.008, segments=2,
        color=(0.76, 0.74, 0.68), bone='Pelvis',
    )
    aa.paint_mesh(apron, color=(0.76, 0.74, 0.68), mat_class=aa.MAT_CLOTH)
    parts.append(apron)

    # Cloth hachimaki headband around forehead
    hx, hy, hz = _v(j, 'Head')
    band = aa.add_torus(
        name='Hachimaki_Band', major_radius=0.098, minor_radius=0.015,
        major_segments=24, minor_segments=10, location=(hx, hy + 0.006, hz + 0.092),
        color=(0.84, 0.80, 0.72), bone='Head',
    )
    aa.paint_mesh(band, color=(0.84, 0.80, 0.72), mat_class=aa.MAT_CLOTH)
    parts.append(band)

    # Glowing amber-teal market stall order badge on chest lapel
    cx, cy, cz = _v(j, 'Chest')
    badge = aa.add_bevel_box(
        name='Vendor_Badge', size=(0.056, 0.014, 0.032),
        location=(cx + 0.088, cy - 0.132, cz + 0.06), bevel=0.004, segments=1,
        color='neon_amber', signal=True, bone='Chest',
    )
    signals.append(badge)

    batched = aa.combine_by_role(parts, signals)
    rig = ah.build_realistic_rig_and_clips(batched, j, rig_name='VendorRig', height=h, style='citizen')
    return aa.export_glb(out_path, [rig, *batched])


def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for fn, name in ((build_resident, 'resident.glb'), (build_mechanic, 'mechanic.glb'), (build_vendor, 'vendor.glb')):
        info = fn(OUT_DIR / name)
        print(f"[civilians] {name}: {info['triangles']} tris, {info['size_kb']} KB", flush=True)


if __name__ == '__main__':
    main()
