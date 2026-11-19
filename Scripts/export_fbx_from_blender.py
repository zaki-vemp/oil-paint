#!/usr/bin/env python3
"""Komorebi: Protocol Zero-Seven — Blender 4.2 LTS Asset Validator & UE5 FBX/PNG Exporter.

Usage (inside Blender 4.2 LTS):
  .test/blender/blender --background --factory-startup --disable-autoexec \\
      --python-exit-code 1 --python Scripts/export_fbx_from_blender.py -- --check

  .test/blender/blender --background --factory-startup --disable-autoexec \\
      --python-exit-code 1 --python Scripts/export_fbx_from_blender.py -- --export

Modes:
  --check   Loads all 26 RawAssets/street/*.glb, RawAssets/figure/starter-man*.glb,
            and RawAssets/characters/*.svg + portrait.png; validates finite geometry,
            COLOR_0 vertex colors, armatures/actions, Wheel_* pivots, and valid XML/PNG.
  --export  Runs full validation, then exports UE5-calibrated FBX files into
            RawAssets/ue_export/ and rasterizes all 6 SVG portraits to 256x256 PNGs.
"""

from __future__ import annotations

import argparse
import json
import math
import re
import shutil
import struct
import subprocess
import sys
import time
import xml.etree.ElementTree as ET
import zlib
from pathlib import Path
from typing import Any

# Complete roster of 26 authored street GLBs (RawAssets/street/SOURCE.md)
STREET_ASSETS: tuple[str, ...] = (
    # Mission & service props (5)
    "terminal",
    "supply-crate",
    "power-core",
    "beacon",
    "barricade",
    # Neo-Kyoto landmarks (2)
    "noodle-stall",
    "echo-shrine",
    # Hero blades & spirit lantern (3)
    "cyber-katana",
    "ren-odachi",
    "spirit-lantern",
    # Vehicles & seated rider (6)
    "coupe",
    "sports",
    "van",
    "motorbike",
    "scooter",
    "rider",
    # Civilians & street specialists (5)
    "resident",
    "mechanic",
    "vendor",
    "courier",
    "executive",
    # Enemy infantry & elites (5)
    "foe-punk",
    "foe-enforcer",
    "foe-gunner",
    "foe-blade",
    "foe-mech",
)

BASE_HUMANOID_CLIPS: tuple[str, ...] = (
    "Idle",
    "Walk",
    "Run",
    "Punch",
    "HitReact",
    "Death",
)

# Rigged street characters: asset_name -> (min_bones, required_clips)
RIGGED_STREET_ASSETS: dict[str, tuple[int, tuple[str, ...]]] = {
    "resident": (18, BASE_HUMANOID_CLIPS),
    "mechanic": (18, BASE_HUMANOID_CLIPS),
    "vendor": (18, BASE_HUMANOID_CLIPS),
    "courier": (18, BASE_HUMANOID_CLIPS),
    "executive": (18, BASE_HUMANOID_CLIPS),
    "foe-punk": (18, BASE_HUMANOID_CLIPS),
    "foe-enforcer": (18, BASE_HUMANOID_CLIPS),
    "foe-gunner": (18, BASE_HUMANOID_CLIPS + ("Idle_Shoot",)),
    "foe-blade": (18, BASE_HUMANOID_CLIPS + ("Sword_Slash",)),
    "foe-mech": (24, BASE_HUMANOID_CLIPS + ("Attack",)),
}

# Vehicles with expected Wheel_* pivot objects
VEHICLE_WHEELS: dict[str, tuple[str, ...]] = {
    "coupe": ("Wheel_FL", "Wheel_FR", "Wheel_RL", "Wheel_RR"),
    "sports": ("Wheel_FL", "Wheel_FR", "Wheel_RL", "Wheel_RR"),
    "van": ("Wheel_FL", "Wheel_FR", "Wheel_RL", "Wheel_RR"),
    "motorbike": ("Wheel_F", "Wheel_R"),
    "scooter": ("Wheel_F", "Wheel_R"),
}

CHARACTER_SVGS: tuple[str, ...] = (
    "dispatch.svg",
    "hana.svg",
    "kael.svg",
    "mira.svg",
    "ren.svg",
    "sora.svg",
)

PORTRAIT_SIZE = 256


def get_script_args() -> argparse.Namespace:
    """Parse CLI arguments passed after Blender's `--` separator."""
    argv = sys.argv
    if "--" in argv:
        user_args = argv[argv.index("--") + 1 :]
    else:
        user_args = []
    parser = argparse.ArgumentParser(
        description="Validate Komorebi RawAssets and optionally export UE5 FBX + PNGs."
    )
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument(
        "--check",
        action="store_true",
        help="Validate all GLB/SVG/PNG assets without writing exports (default).",
    )
    mode.add_argument(
        "--export",
        action="store_true",
        help="Validate all assets and export UE5 FBX files + 256x256 portrait PNGs.",
    )
    parser.add_argument(
        "--out-dir",
        type=str,
        default=None,
        help="Override export output directory (default: RawAssets/ue_export).",
    )
    args = parser.parse_args(user_args)
    if not args.check and not args.export:
        args.check = True
    return args


def parse_glb_json(path: Path) -> dict[str, Any]:
    """Validate GLB 2.0 header and return the embedded glTF JSON chunk."""
    raw = path.read_bytes()
    if len(raw) < 28:
        raise ValueError(f"{path.name}: file too small ({len(raw)} bytes)")
    magic, version, total_len = struct.unpack_from("<III", raw, 0)
    if magic != 0x46546C67:
        raise ValueError(f"{path.name}: invalid GLB magic 0x{magic:08x}")
    if version != 2:
        raise ValueError(f"{path.name}: unsupported glTF version {version}")
    if total_len != len(raw):
        raise ValueError(f"{path.name}: length header {total_len} != actual {len(raw)}")
    chunk_len, chunk_type = struct.unpack_from("<II", raw, 12)
    if chunk_type != 0x4E4F534A:  # JSON
        raise ValueError(f"{path.name}: first chunk is not JSON")
    return json.loads(raw[20 : 20 + chunk_len].decode("utf-8"))


def collect_bone_shape_objects(bpy_mod: Any) -> set[Any]:
    """Identify helper meshes created by Blender's glTF importer for bone display shapes."""
    shapes: set[Any] = set()
    for obj in bpy_mod.data.objects:
        if obj.type == "ARMATURE" and obj.pose:
            for pbone in obj.pose.bones:
                if pbone.custom_shape is not None:
                    shapes.add(pbone.custom_shape)
    return shapes


def strip_bone_shape_helpers(bpy_mod: Any) -> None:
    """Clear bone custom_shape references and remove stray helper meshes prior to FBX export."""
    shapes = collect_bone_shape_objects(bpy_mod)
    for obj in bpy_mod.data.objects:
        if obj.type == "ARMATURE" and obj.pose:
            for pbone in obj.pose.bones:
                if pbone.custom_shape in shapes:
                    pbone.custom_shape = None
    for helper in list(shapes):
        if helper.name in bpy_mod.data.objects:
            bpy_mod.data.objects.remove(helper, do_unlink=True)


def validate_mesh_geometry(mesh_objs: list[Any], label: str) -> tuple[int, int]:
    """Verify non-zero, finite vertex positions and non-degenerate bounding box."""
    if not mesh_objs:
        raise ValueError(f"{label}: no mesh objects found in scene")

    total_verts = 0
    total_polys = 0
    non_zero_verts = 0
    min_co = [math.inf, math.inf, math.inf]
    max_co = [-math.inf, -math.inf, -math.inf]

    for obj in mesh_objs:
        mesh = obj.data
        verts = mesh.vertices
        if len(verts) == 0:
            raise ValueError(f"{label}: mesh '{obj.name}' has 0 vertices")
        total_verts += len(verts)
        total_polys += len(mesh.polygons)

        mat_world = obj.matrix_world
        for v in verts:
            co = v.co
            if not (math.isfinite(co.x) and math.isfinite(co.y) and math.isfinite(co.z)):
                raise ValueError(f"{label}: non-finite vertex coordinate in mesh '{obj.name}'")
            if abs(co.x) > 1e-7 or abs(co.y) > 1e-7 or abs(co.z) > 1e-7:
                non_zero_verts += 1
            w = mat_world @ co
            if not (math.isfinite(w.x) and math.isfinite(w.y) and math.isfinite(w.z)):
                raise ValueError(f"{label}: non-finite world transform on '{obj.name}'")
            for idx in range(3):
                if w[idx] < min_co[idx]:
                    min_co[idx] = w[idx]
                if w[idx] > max_co[idx]:
                    max_co[idx] = w[idx]

    if non_zero_verts == 0:
        raise ValueError(f"{label}: all {total_verts} vertices are at the origin (0,0,0)")

    diag = math.sqrt(sum((max_co[i] - min_co[i]) ** 2 for i in range(3)))
    if not math.isfinite(diag) or diag < 1e-4:
        raise ValueError(f"{label}: degenerate bounding box diagonal ({diag})")

    return total_verts, total_polys


def validate_actions_finite(bpy_mod: Any, label: str) -> int:
    """Verify every loaded Blender action has finite keyframe values."""
    actions = list(bpy_mod.data.actions)
    for act in actions:
        for fc in act.fcurves:
            for kp in fc.keyframe_points:
                if not (math.isfinite(kp.co.x) and math.isfinite(kp.co.y)):
                    raise ValueError(f"{label}: non-finite keyframe in action '{act.name}'")
    return len(actions)


def export_scene_fbx(bpy_mod: Any, out_path: Path, is_anim_only: bool = False) -> None:
    """Export the active clean Blender scene to a UE5-calibrated FBX file."""
    out_path.parent.mkdir(parents=True, exist_ok=True)
    strip_bone_shape_helpers(bpy_mod)

    object_types = {"EMPTY", "ARMATURE"} if is_anim_only else {"EMPTY", "ARMATURE", "MESH"}
    bpy_mod.ops.export_scene.fbx(
        filepath=str(out_path),
        use_selection=False,
        object_types=object_types,
        global_scale=1.0,
        apply_unit_scale=True,
        apply_scale_options="FBX_SCALE_NONE",
        axis_forward="-Y",
        axis_up="Z",
        use_mesh_modifiers=True,
        mesh_smooth_type="FACE",
        colors_type="SRGB",
        add_leaf_bones=False,
        bake_anim=True,
        bake_anim_use_all_actions=not is_anim_only,
        bake_anim_use_nla_strips=is_anim_only,
        bake_anim_simplify_factor=0.0,
    )
    if not out_path.exists() or out_path.stat().st_size == 0:
        raise RuntimeError(f"FBX export produced missing or empty file: {out_path}")


def validate_and_maybe_export_street(
    bpy_mod: Any,
    street_dir: Path,
    export_dir: Path | None,
) -> dict[str, int]:
    """Validate (and optionally export) all 26 authored street GLBs."""
    stats = {"assets": 0, "verts": 0, "polys": 0, "actions": 0, "wheels": 0, "exported": 0}

    for name in STREET_ASSETS:
        glb_path = street_dir / f"{name}.glb"
        if not glb_path.exists():
            raise FileNotFoundError(f"Missing street GLB: {glb_path}")

        gltf = parse_glb_json(glb_path)
        for mesh_def in gltf.get("meshes", []):
            for prim in mesh_def.get("primitives", []):
                if "COLOR_0" not in prim.get("attributes", {}):
                    raise ValueError(f"street/{name}.glb: primitive missing glTF COLOR_0 attribute")

        bpy_mod.ops.wm.read_factory_settings(use_empty=True)
        bpy_mod.ops.import_scene.gltf(filepath=str(glb_path))

        helper_shapes = collect_bone_shape_objects(bpy_mod)
        mesh_objs = [
            o
            for o in bpy_mod.context.scene.objects
            if o.type == "MESH" and o not in helper_shapes
        ]
        verts, polys = validate_mesh_geometry(mesh_objs, f"street/{name}.glb")

        # Verify COLOR_0 vertex color attribute in Blender on every authored street mesh
        for obj in mesh_objs:
            color_attrs = list(obj.data.color_attributes.keys())
            if not color_attrs:
                raise ValueError(
                    f"street/{name}.glb: mesh '{obj.name}' missing Blender color attribute (COLOR_0)"
                )

        mat_names = {m.name for m in bpy_mod.data.materials}
        if "VertexPaint" not in mat_names:
            raise ValueError(f"street/{name}.glb: missing 'VertexPaint' material role")
        if "Signal" not in mat_names:
            raise ValueError(f"street/{name}.glb: missing 'Signal' emissive material role")

        # Rigged street characters / enemies
        if name in RIGGED_STREET_ASSETS:
            min_bones, required_clips = RIGGED_STREET_ASSETS[name]
            armatures = [o for o in bpy_mod.context.scene.objects if o.type == "ARMATURE"]
            if not armatures:
                raise ValueError(f"street/{name}.glb: expected armature, found none")
            max_bones = max(len(a.data.bones) for a in armatures)
            if max_bones < min_bones:
                raise ValueError(
                    f"street/{name}.glb: armature has {max_bones} bones, expected >= {min_bones}"
                )
            if not any(len(o.vertex_groups) > 0 for o in mesh_objs):
                raise ValueError(f"street/{name}.glb: no skinned vertex groups on meshes")

            action_names = [a.name for a in bpy_mod.data.actions]
            gltf_anim_names = {a.get("name", "") for a in gltf.get("animations", [])}
            for clip in required_clips:
                in_blender = any(
                    an == clip or an.startswith(f"{clip}_") for an in action_names
                )
                in_gltf = clip in gltf_anim_names
                if not (in_blender and in_gltf):
                    raise ValueError(
                        f"street/{name}.glb: missing required animation clip '{clip}' "
                        f"(blender={in_blender}, gltf={in_gltf})"
                    )
            stats["actions"] += validate_actions_finite(bpy_mod, f"street/{name}.glb")

        # Vehicles with wheel pivots
        if name in VEHICLE_WHEELS:
            expected_wheels = VEHICLE_WHEELS[name]
            scene_by_name = {o.name: o for o in bpy_mod.context.scene.objects}
            for wname in expected_wheels:
                if wname not in scene_by_name:
                    raise ValueError(f"street/{name}.glb: missing wheel pivot '{wname}'")
                wobj = scene_by_name[wname]
                if wobj.type != "MESH" or len(wobj.data.vertices) == 0:
                    raise ValueError(f"street/{name}.glb: wheel pivot '{wname}' has no geometry")
                loc = wobj.matrix_world.translation
                if not (math.isfinite(loc.x) and math.isfinite(loc.y) and math.isfinite(loc.z)):
                    raise ValueError(f"street/{name}.glb: non-finite wheel pivot '{wname}'")
            stats["wheels"] += len(expected_wheels)

        if export_dir is not None:
            export_scene_fbx(bpy_mod, export_dir / "street" / f"{name}.fbx", is_anim_only=False)
            stats["exported"] += 1

        stats["assets"] += 1
        stats["verts"] += verts
        stats["polys"] += polys

    return stats


def validate_and_maybe_export_figure(
    bpy_mod: Any,
    figure_dir: Path,
    export_dir: Path | None,
) -> dict[str, int]:
    """Validate (and optionally export) starter-man.glb and starter-man-clips.glb."""
    stats = {"assets": 0, "verts": 0, "bones": 0, "actions": 0, "exported": 0}

    # 1. starter-man.glb (protagonist MakeHuman skeletal rig + meshes)
    man_glb = figure_dir / "starter-man.glb"
    if not man_glb.exists():
        raise FileNotFoundError(f"Missing figure GLB: {man_glb}")
    parse_glb_json(man_glb)

    bpy_mod.ops.wm.read_factory_settings(use_empty=True)
    bpy_mod.ops.import_scene.gltf(filepath=str(man_glb))

    helper_shapes = collect_bone_shape_objects(bpy_mod)
    mesh_objs = [
        o
        for o in bpy_mod.context.scene.objects
        if o.type == "MESH" and o not in helper_shapes
    ]
    verts, _ = validate_mesh_geometry(mesh_objs, "figure/starter-man.glb")

    armatures = [o for o in bpy_mod.context.scene.objects if o.type == "ARMATURE"]
    if not armatures:
        raise ValueError("figure/starter-man.glb: missing armature")
    bone_count = max(len(a.data.bones) for a in armatures)
    if bone_count < 50:
        raise ValueError(f"figure/starter-man.glb: expected >= 50 bones, got {bone_count}")
    if not any(len(o.vertex_groups) > 0 for o in mesh_objs):
        raise ValueError("figure/starter-man.glb: no skinned vertex groups found")

    if export_dir is not None:
        export_scene_fbx(bpy_mod, export_dir / "figure" / "starter-man.fbx", is_anim_only=False)
        stats["exported"] += 1

    stats["assets"] += 1
    stats["verts"] += verts
    stats["bones"] = bone_count

    # 2. starter-man-clips.glb (locomotion / combat clip pack)
    clips_glb = figure_dir / "starter-man-clips.glb"
    if not clips_glb.exists():
        raise FileNotFoundError(f"Missing figure clips GLB: {clips_glb}")
    clips_gltf = parse_glb_json(clips_glb)
    if not clips_gltf.get("animations"):
        raise ValueError("figure/starter-man-clips.glb: glTF contains 0 animation clips")

    bpy_mod.ops.wm.read_factory_settings(use_empty=True)
    bpy_mod.ops.import_scene.gltf(filepath=str(clips_glb))

    if len(bpy_mod.context.scene.objects) < 20:
        raise ValueError(
            f"figure/starter-man-clips.glb: expected skeleton node hierarchy, got "
            f"{len(bpy_mod.context.scene.objects)} objects"
        )
    action_count = validate_actions_finite(bpy_mod, "figure/starter-man-clips.glb")
    if action_count == 0:
        raise ValueError("figure/starter-man-clips.glb: imported 0 Blender animation actions")

    if export_dir is not None:
        export_scene_fbx(bpy_mod, export_dir / "figure" / "starter-man-clips.fbx", is_anim_only=True)
        stats["exported"] += 1

    stats["assets"] += 1
    stats["actions"] += action_count
    return stats


def _write_png_rgba(path: Path, width: int, height: int, rgba_rows: list[bytes]) -> None:
    """Write a standards-compliant 8-bit RGBA PNG using pure Python stdlib."""
    def chunk(tag: bytes, data: bytes) -> bytes:
        crc = zlib.crc32(tag + data) & 0xFFFFFFFF
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", crc)

    raw = b"".join(b"\x00" + row for row in rgba_rows)
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    png = (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", ihdr)
        + chunk(b"IDAT", zlib.compress(raw, level=6))
        + chunk(b"IEND", b"")
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(png)


def _extract_svg_palette(svg_text: str) -> tuple[tuple[int, int, int], tuple[int, int, int], tuple[int, int, int]]:
    """Extract background, secondary, and neon accent RGB colors from SVG hex stops."""
    hexes = re.findall(r"#([0-9a-fA-F]{6})\b", svg_text)
    colors: list[tuple[int, int, int]] = []
    for h in hexes:
        rgb = (int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16))
        if rgb not in colors:
            colors.append(rgb)

    bg = colors[0] if len(colors) >= 1 else (10, 14, 22)
    mid = colors[1] if len(colors) >= 2 else (18, 28, 42)
    # Pick the most saturated/brightest color as the neon accent ring
    accent = (0, 240, 255)
    best_score = -1
    for c in colors:
        score = (max(c) - min(c)) + max(c) // 2
        if score > best_score:
            best_score = score
            accent = c
    return bg, mid, accent


def _generate_fallback_portrait_png(svg_path: Path, png_path: Path, size: int = PORTRAIT_SIZE) -> None:
    """Pure-Python stylized cyberpunk portrait PNG fallback when no SVG rasterizer is installed."""
    svg_text = svg_path.read_text(encoding="utf-8", errors="ignore")
    bg, mid, accent = _extract_svg_palette(svg_text)

    cx = size * 0.5
    cy = size * 0.44
    rows: list[bytes] = []
    for y in range(size):
        fy = y / max(size - 1, 1)
        row = bytearray(size * 4)
        for x in range(size):
            fx = x / max(size - 1, 1)
            dx = (x - cx) / (size * 0.5)
            dy = (y - cy) / (size * 0.5)
            r = math.hypot(dx, dy)

            # Dark cyberpunk backdrop gradient + radial neon halo
            t = 0.5 * (fx + fy)
            halo = max(0.0, 1.0 - r * 1.15) ** 2.0
            cr = bg[0] * (1.0 - t) + mid[0] * t + accent[0] * halo * 0.32
            cg = bg[1] * (1.0 - t) + mid[1] * t + accent[1] * halo * 0.32
            cb = bg[2] * (1.0 - t) + mid[2] * t + accent[2] * halo * 0.32

            # Subtle character bust silhouette (head + shoulders)
            head_d = math.hypot((x - cx) / (size * 0.16), (y - size * 0.37) / (size * 0.19))
            body_d = math.hypot((x - cx) / (size * 0.34), (y - size * 0.86) / (size * 0.32))
            if head_d < 1.0 or body_d < 1.0:
                rim = max(0.0, 1.0 - abs(min(head_d, body_d) - 0.92) * 10.0)
                cr = cr * 0.38 + accent[0] * rim * 0.55
                cg = cg * 0.38 + accent[1] * rim * 0.55
                cb = cb * 0.38 + accent[2] * rim * 0.55

            # Crisp HUD corner brackets & subtle scanline
            border = (x < 6 or x >= size - 6 or y < 6 or y >= size - 6) and (
                (x < 34 or x >= size - 34) and (y < 34 or y >= size - 34)
            )
            if border:
                cr, cg, cb = accent[0], accent[1], accent[2]
            elif (y & 3) == 0:
                cr *= 0.92
                cg *= 0.92
                cb *= 0.92

            off = x * 4
            row[off] = max(0, min(255, int(cr)))
            row[off + 1] = max(0, min(255, int(cg)))
            row[off + 2] = max(0, min(255, int(cb)))
            row[off + 3] = 255
        rows.append(bytes(row))

    _write_png_rgba(png_path, size, size, rows)


def rasterize_svg_to_png(svg_path: Path, png_path: Path, size: int = PORTRAIT_SIZE) -> str:
    """Rasterize an SVG portrait to a 256x256 PNG using available tools or pure-Python fallback."""
    png_path.parent.mkdir(parents=True, exist_ok=True)

    rsvg = shutil.which("rsvg-convert")
    if rsvg:
        subprocess.run(
            [rsvg, "-w", str(size), "-h", str(size), "-o", str(png_path), str(svg_path)],
            check=True,
        )
        return "rsvg-convert"

    inkscape = shutil.which("inkscape")
    if inkscape:
        subprocess.run(
            [
                inkscape,
                str(svg_path),
                "--export-type=png",
                f"--export-filename={png_path}",
                f"--export-width={size}",
                f"--export-height={size}",
            ],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        return "inkscape"

    try:
        import cairosvg  # type: ignore[import-not-found]

        cairosvg.svg2png(
            url=str(svg_path),
            write_to=str(png_path),
            output_width=size,
            output_height=size,
        )
        return "cairosvg"
    except Exception:
        pass

    magick = shutil.which("magick") or shutil.which("convert")
    if magick:
        try:
            subprocess.run(
                [
                    magick,
                    "-background",
                    "none",
                    str(svg_path),
                    "-resize",
                    f"{size}x{size}!",
                    str(png_path),
                ],
                check=True,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            return Path(magick).name
        except Exception:
            pass

    _generate_fallback_portrait_png(svg_path, png_path, size=size)
    return "python-fallback"


def validate_and_maybe_export_portraits(
    chars_dir: Path,
    export_dir: Path | None,
) -> dict[str, Any]:
    """Validate all 6 character SVGs (valid XML) and portrait.png (valid PNG header)."""
    stats: dict[str, Any] = {"svgs": 0, "pngs": 0, "rasterizer": "none", "exported": 0}

    for svg_name in CHARACTER_SVGS:
        svg_path = chars_dir / svg_name
        if not svg_path.exists():
            raise FileNotFoundError(f"Missing character portrait SVG: {svg_path}")
        if svg_path.stat().st_size == 0:
            raise ValueError(f"Empty SVG file: {svg_path}")
        try:
            tree = ET.parse(svg_path)
        except ET.ParseError as exc:
            raise ValueError(f"Invalid XML in {svg_name}: {exc}") from exc
        root_tag = tree.getroot().tag.split("}")[-1].lower()
        if root_tag != "svg":
            raise ValueError(f"{svg_name}: root element is <{root_tag}>, expected <svg>")
        stats["svgs"] += 1

        if export_dir is not None:
            out_png = export_dir / "portraits" / f"{svg_path.stem}.png"
            stats["rasterizer"] = rasterize_svg_to_png(svg_path, out_png, PORTRAIT_SIZE)
            stats["exported"] += 1

    portrait_png = chars_dir / "portrait.png"
    if not portrait_png.exists():
        raise FileNotFoundError(f"Missing portrait PNG: {portrait_png}")
    hdr = portrait_png.read_bytes()[:24]
    if len(hdr) < 24 or hdr[:8] != b"\x89PNG\r\n\x1a\n" or hdr[12:16] != b"IHDR":
        raise ValueError(f"{portrait_png}: invalid PNG header")
    width, height = struct.unpack(">II", hdr[16:24])
    if width <= 0 or height <= 0:
        raise ValueError(f"{portrait_png}: invalid dimensions {width}x{height}")
    stats["pngs"] = 1

    if export_dir is not None:
        out_portrait = export_dir / "portraits" / "portrait.png"
        out_portrait.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(portrait_png, out_portrait)
        stats["exported"] += 1

    return stats


def main() -> int:
    try:
        import bpy  # type: ignore[import-not-found]
    except ImportError:
        print(
            "[Komorebi] ERROR: This script must be executed inside Blender 4.2 LTS "
            "(e.g. .test/blender/blender --background --factory-startup --python "
            "Scripts/export_fbx_from_blender.py -- --check).",
            file=sys.stderr,
        )
        return 1

    args = get_script_args()
    repo_root = Path(__file__).resolve().parent.parent
    raw_dir = repo_root / "RawAssets"
    street_dir = raw_dir / "street"
    figure_dir = raw_dir / "figure"
    chars_dir = raw_dir / "characters"
    export_dir = (
        Path(args.out_dir).resolve()
        if args.out_dir
        else (raw_dir / "ue_export" if args.export else None)
    )

    t0 = time.monotonic()
    mode_label = "EXPORT" if args.export else "CHECK"
    print(f"[Komorebi] Blender {bpy.app.version_string} asset pipeline ({mode_label})")

    street_stats = validate_and_maybe_export_street(bpy, street_dir, export_dir)
    figure_stats = validate_and_maybe_export_figure(bpy, figure_dir, export_dir)
    portrait_stats = validate_and_maybe_export_portraits(chars_dir, export_dir)

    elapsed = time.monotonic() - t0
    print(
        "[Komorebi] PASS: "
        f"{street_stats['assets']}/26 street GLBs "
        f"({street_stats['verts']} verts, {street_stats['polys']} polys, "
        f"{street_stats['actions']} rig actions, {street_stats['wheels']} wheel pivots), "
        f"{figure_stats['assets']}/2 figure GLBs "
        f"({figure_stats['verts']} verts, {figure_stats['bones']} bones, "
        f"{figure_stats['actions']} clip actions), "
        f"{portrait_stats['svgs']}/6 SVGs + {portrait_stats['pngs']} portrait.png "
        f"validated in {elapsed:.2f}s."
    )
    if args.export and export_dir is not None:
        print(
            f"[Komorebi] EXPORTED: {street_stats['exported']} street FBXs, "
            f"{figure_stats['exported']} figure FBXs, "
            f"{portrait_stats['exported']} portrait PNGs (rasterizer: {portrait_stats['rasterizer']}) "
            f"-> {export_dir}"
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
