#!/usr/bin/env python3
"""Komorebi: Protocol Zero-Seven — Unreal Engine 5 Editor Python Importer & Map Builder.

Executed via UnrealEditor-Cmd:
  UnrealEditor-Cmd Komorebi.uproject -ExecutePythonScript=Scripts/ue_import_and_build.py \\
      -NullRHI -NoSound -Unattended -NoSplash -NoP4 -stdout -FullStdOutLogOutput

Automates five deterministic setup stages:
  1. Batch imports all 26 street models, protagonist skeletal mesh & animation clips,
     and character portrait textures into /Game/Assets/{Street,Figure,Portraits}/.
  2. Creates /Game/Materials/MPC_KomorebiShared with all shared scalars & vectors.
  3. Builds all 9 master materials using MaterialEditingLibrary + Custom HLSL nodes
     including "/Project/KomorebiMaterialFunctions.ush".
  4. Creates all 20 Enhanced Input Actions and IMC_Komorebi matching legacy-web/src/figure/input.js.
  5. Constructs and saves /Game/Maps/L_NeoKyoto and /Game/Maps/L_Snowbound.
"""

from __future__ import annotations

import os
from pathlib import Path
from typing import Any

try:
    import unreal  # type: ignore[import-not-found]
except ImportError:
    unreal = None  # Allows offline syntax checking via `python3 -m py_compile`


STREET_ASSETS: tuple[str, ...] = (
    "terminal",
    "supply-crate",
    "power-core",
    "beacon",
    "barricade",
    "noodle-stall",
    "echo-shrine",
    "cyber-katana",
    "ren-odachi",
    "spirit-lantern",
    "coupe",
    "sports",
    "van",
    "motorbike",
    "scooter",
    "rider",
    "resident",
    "mechanic",
    "vendor",
    "courier",
    "executive",
    "foe-punk",
    "foe-enforcer",
    "foe-gunner",
    "foe-blade",
    "foe-mech",
)

RIGGED_STREET_ASSETS: set[str] = {
    "resident",
    "mechanic",
    "vendor",
    "courier",
    "executive",
    "foe-punk",
    "foe-enforcer",
    "foe-gunner",
    "foe-blade",
    "foe-mech",
}

VEHICLE_STREET_ASSETS: set[str] = {
    "coupe",
    "sports",
    "van",
    "motorbike",
    "scooter",
}

PORTRAIT_NAMES: tuple[str, ...] = (
    "portrait",
    "kael",
    "sora",
    "mira",
    "ren",
    "dispatch",
    "hana",
)

SHADER_INCLUDE_PATH = "/Project/KomorebiMaterialFunctions.ush"


def _log(msg: str) -> None:
    if unreal is not None:
        unreal.log(f"[KomorebiBuild] {msg}")
    else:
        print(f"[KomorebiBuild] {msg}")


def _warn(msg: str) -> None:
    if unreal is not None:
        unreal.log_warning(f"[KomorebiBuild] {msg}")
    else:
        print(f"[KomorebiBuild][WARN] {msg}")


def _project_root() -> Path:
    if unreal is not None:
        return Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve()
    return Path(__file__).resolve().parent.parent


def _try_set_prop(obj: Any, prop_name: str, value: Any) -> bool:
    """Safely assign an editor property if it exists on the UE object."""
    try:
        obj.set_editor_property(prop_name, value)
        return True
    except Exception:
        return False


def _ensure_asset_dir(content_path: str) -> None:
    if not unreal.EditorAssetLibrary.does_directory_exist(content_path):
        unreal.EditorAssetLibrary.make_directory(content_path)


def _create_or_load_asset(asset_name: str, package_path: str, asset_class: Any, factory: Any) -> Any:
    full_path = f"{package_path}/{asset_name}"
    if unreal.EditorAssetLibrary.does_asset_exist(full_path):
        loaded = unreal.EditorAssetLibrary.load_asset(full_path)
        if loaded is not None:
            return loaded
    _ensure_asset_dir(package_path)
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    return asset_tools.create_asset(asset_name, package_path, asset_class, factory)


def _sanitize_asset_name(name: str) -> str:
    return name.replace("-", "_")


# -----------------------------------------------------------------------------
# Stage 1: Batch Asset Import (Street, Figure, Portraits)
# -----------------------------------------------------------------------------

def _build_fbx_options(
    as_skeletal: bool,
    import_animations: bool,
    combine_meshes: bool,
    target_skeleton: Any = None,
    anim_only: bool = False,
) -> Any:
    opts = unreal.FbxImportUI()
    opts.set_editor_property("import_mesh", not anim_only)
    opts.set_editor_property("import_as_skeletal", as_skeletal or anim_only)
    opts.set_editor_property("import_animations", import_animations or anim_only)
    opts.set_editor_property("import_materials", False)
    opts.set_editor_property("import_textures", False)
    opts.set_editor_property("automated_import_should_detect_type", False)

    if anim_only:
        opts.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_ANIMATION)
        if target_skeleton is not None:
            opts.set_editor_property("skeleton", target_skeleton)
    elif as_skeletal:
        opts.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH)
        sdata = opts.skeletal_mesh_import_data
        _try_set_prop(sdata, "import_content_type", unreal.FBXImportContentType.FBXICT_ALL)
        _try_set_prop(sdata, "vertex_color_import_option", unreal.VertexColorImportOption.REPLACE)
        _try_set_prop(sdata, "update_skeleton_reference_pose", False)
        _try_set_prop(sdata, "import_morph_targets", True)
        _try_set_prop(sdata, "convert_scene", True)
        if target_skeleton is not None:
            opts.set_editor_property("skeleton", target_skeleton)
    else:
        opts.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH)
        stdata = opts.static_mesh_import_data
        _try_set_prop(stdata, "combine_meshes", combine_meshes)
        _try_set_prop(stdata, "vertex_color_import_option", unreal.VertexColorImportOption.REPLACE)
        _try_set_prop(stdata, "auto_generate_collision", True)
        _try_set_prop(stdata, "convert_scene", True)

    if import_animations or anim_only:
        adata = opts.anim_sequence_import_data
        _try_set_prop(adata, "import_meshes_in_bone_hierarchy", False)
        _try_set_prop(adata, "convert_scene", True)

    return opts


def _make_import_task(
    source_path: Path,
    dest_path: str,
    dest_name: str,
    options: Any = None,
) -> Any:
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", str(source_path))
    task.set_editor_property("destination_path", dest_path)
    task.set_editor_property("destination_name", dest_name)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", True)
    if options is not None:
        task.set_editor_property("options", options)
    return task


def import_all_assets(project_root: Path) -> None:
    """Import street models, protagonist figure + clips, and portrait textures."""
    _log("Stage 1/5: Importing RawAssets into /Game/Assets/...")
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

    raw_dir = project_root / "RawAssets"
    ue_export = raw_dir / "ue_export"

    # 1A. Street assets (prefer UE5-calibrated FBX when available, fallback to GLB via Interchange)
    street_dest = "/Game/Assets/Street"
    _ensure_asset_dir(street_dest)
    street_tasks = []
    for name in STREET_ASSETS:
        fbx_path = ue_export / "street" / f"{name}.fbx"
        glb_path = raw_dir / "street" / f"{name}.glb"
        ue_name = f"SM_{_sanitize_asset_name(name)}" if name not in RIGGED_STREET_ASSETS else f"SK_{_sanitize_asset_name(name)}"

        if fbx_path.exists():
            is_rigged = name in RIGGED_STREET_ASSETS
            combine = name not in VEHICLE_STREET_ASSETS
            opts = _build_fbx_options(
                as_skeletal=is_rigged,
                import_animations=is_rigged,
                combine_meshes=combine,
            )
            street_tasks.append(_make_import_task(fbx_path, street_dest, ue_name, opts))
        elif glb_path.exists():
            street_tasks.append(_make_import_task(glb_path, street_dest, ue_name, None))
        else:
            _warn(f"Missing street source asset for '{name}' (checked FBX and GLB)")

    if street_tasks:
        asset_tools.import_asset_tasks(street_tasks)
        _log(f"Imported {len(street_tasks)} street asset tasks into {street_dest}")

    # 1B. Protagonist figure & animation clips
    figure_dest = "/Game/Assets/Figure"
    _ensure_asset_dir(figure_dest)
    man_fbx = ue_export / "figure" / "starter-man.fbx"
    man_glb = raw_dir / "figure" / "starter-man.glb"
    if man_fbx.exists():
        man_opts = _build_fbx_options(as_skeletal=True, import_animations=False, combine_meshes=False)
        asset_tools.import_asset_tasks([_make_import_task(man_fbx, figure_dest, "SK_Kael", man_opts)])
    elif man_glb.exists():
        asset_tools.import_asset_tasks([_make_import_task(man_glb, figure_dest, "SK_Kael", None)])
    else:
        _warn("Missing starter-man figure asset")

    # Locate the imported protagonist skeleton for clip targeting
    kael_skeleton = None
    for asset_path in unreal.EditorAssetLibrary.list_assets(figure_dest, recursive=True):
        asset_obj = unreal.EditorAssetLibrary.load_asset(asset_path)
        if isinstance(asset_obj, unreal.Skeleton):
            kael_skeleton = asset_obj
            break

    clips_fbx = ue_export / "figure" / "starter-man-clips.fbx"
    clips_glb = raw_dir / "figure" / "starter-man-clips.glb"
    if clips_fbx.exists():
        clip_opts = _build_fbx_options(
            as_skeletal=True,
            import_animations=True,
            combine_meshes=False,
            target_skeleton=kael_skeleton,
            anim_only=True,
        )
        asset_tools.import_asset_tasks([_make_import_task(clips_fbx, figure_dest, "A_KaelClips", clip_opts)])
    elif clips_glb.exists():
        asset_tools.import_asset_tasks([_make_import_task(clips_glb, figure_dest, "A_KaelClips", None)])
    else:
        _warn("Missing starter-man-clips asset")

    # 1C. Character portrait textures
    portraits_dest = "/Game/Assets/Portraits"
    _ensure_asset_dir(portraits_dest)
    portrait_tasks = []
    for pname in PORTRAIT_NAMES:
        candidates = [
            ue_export / "portraits" / f"{pname}.png",
            raw_dir / "characters" / f"{pname}.png",
        ]
        src = next((p for p in candidates if p.exists()), None)
        if src is not None:
            portrait_tasks.append(_make_import_task(src, portraits_dest, f"T_Portrait_{pname}", None))
        else:
            _warn(f"Missing portrait PNG for '{pname}' (run export_fbx_from_blender.py --export first)")

    if portrait_tasks:
        asset_tools.import_asset_tasks(portrait_tasks)
        for asset_path in unreal.EditorAssetLibrary.list_assets(portraits_dest, recursive=False):
            tex = unreal.EditorAssetLibrary.load_asset(asset_path)
            if isinstance(tex, unreal.Texture2D):
                _try_set_prop(tex, "lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
                _try_set_prop(tex, "srgb", True)
                _try_set_prop(tex, "never_stream", True)
                unreal.EditorAssetLibrary.save_loaded_asset(tex)
        _log(f"Imported {len(portrait_tasks)} portrait textures into {portraits_dest}")


# -----------------------------------------------------------------------------
# Stage 2: Material Parameter Collection (MPC_KomorebiShared)
# -----------------------------------------------------------------------------

def build_shared_mpc() -> Any:
    """Create /Game/Materials/MPC_KomorebiShared with all shared scalars and vectors."""
    _log("Stage 2/5: Building /Game/Materials/MPC_KomorebiShared...")
    mpc = _create_or_load_asset(
        "MPC_KomorebiShared",
        "/Game/Materials",
        unreal.MaterialParameterCollection,
        unreal.MaterialParameterCollectionFactoryNew(),
    )

    scalars = [
        ("Time", 0.0),
        ("Night", 0.0),
        ("Twilight", 0.0),
        ("Aurora", 0.0),
        ("Exposure", 1.08),
        ("InkStrength", 0.0),
        ("ImpastoStrength", 0.0),
    ]
    scalar_params = []
    for name, val in scalars:
        p = unreal.CollectionScalarParameter()
        p.set_editor_property("parameter_name", name)
        p.set_editor_property("default_value", float(val))
        scalar_params.append(p)
    mpc.set_editor_property("scalar_parameters", scalar_params)

    # Normalized sun vector in UE Z-up (corresponding to Three.js (-0.44, 0.5, -0.75))
    vectors = [
        ("SunDir", (-0.4378, -0.7463, 0.4975, 0.0)),
        ("ChapterGrass", (0.60, 0.62, 0.65, 1.0)),
        ("ChapterPath", (0.34, 0.36, 0.40, 1.0)),
        ("ChapterFoliage", (0.0, 0.85, 0.95, 1.0)),
        ("ChapterLeafLight", (1.0, 0.86, 0.45, 1.0)),
        ("SkyHigh", (0.18, 0.52, 0.88, 1.0)),
        ("HazeColor", (0.70, 0.81, 0.91, 1.0)),
        ("LampPosStrength", (0.0, 0.0, 0.0, 0.0)),
        ("LampColor", (1.0, 0.62, 0.25, 1.0)),
        ("FocusPos", (0.0, 0.0, 0.0, 0.0)),
        ("ZoneSplit", (90.0, -10.0, -30.0, 0.0)),
    ]
    vector_params = []
    for name, rgba in vectors:
        p = unreal.CollectionVectorParameter()
        p.set_editor_property("parameter_name", name)
        p.set_editor_property("default_value", unreal.LinearColor(rgba[0], rgba[1], rgba[2], rgba[3]))
        vector_params.append(p)
    mpc.set_editor_property("vector_parameters", vector_params)

    unreal.EditorAssetLibrary.save_loaded_asset(mpc)
    _log(f"Saved MPC_KomorebiShared ({len(scalars)} scalars, {len(vectors)} vectors)")
    return mpc


# -----------------------------------------------------------------------------
# Stage 3: Master Materials (wiring /Project/KomorebiMaterialFunctions.ush)
# -----------------------------------------------------------------------------

def _new_clean_material(mat_name: str, pkg_path: str = "/Game/Materials") -> Any:
    mat = _create_or_load_asset(mat_name, pkg_path, unreal.Material, unreal.MaterialFactoryNew())
    unreal.MaterialEditingLibrary.delete_all_material_expressions(mat)
    return mat


def _add_expr(mat: Any, expr_cls: Any, x: int, y: int) -> Any:
    return unreal.MaterialEditingLibrary.create_material_expression(mat, expr_cls, x, y)


def _add_mpc_param(mat: Any, mpc: Any, param_name: str, x: int, y: int) -> Any:
    node = _add_expr(mat, unreal.MaterialExpressionCollectionParameter, x, y)
    node.set_editor_property("collection", mpc)
    node.set_editor_property("parameter_name", param_name)
    return node


def _add_scalar_param(mat: Any, param_name: str, default_val: float, x: int, y: int) -> Any:
    node = _add_expr(mat, unreal.MaterialExpressionScalarParameter, x, y)
    node.set_editor_property("parameter_name", param_name)
    node.set_editor_property("default_value", float(default_val))
    return node


def _add_vector_param(mat: Any, param_name: str, rgba: tuple[float, float, float, float], x: int, y: int) -> Any:
    node = _add_expr(mat, unreal.MaterialExpressionVectorParameter, x, y)
    node.set_editor_property("parameter_name", param_name)
    node.set_editor_property("default_value", unreal.LinearColor(rgba[0], rgba[1], rgba[2], rgba[3]))
    return node


def _add_custom_hlsl(
    mat: Any,
    description: str,
    code: str,
    input_names: list[str],
    output_type: Any,
    additional_outputs: list[tuple[str, Any]] | None,
    x: int,
    y: int,
) -> Any:
    """Create a Custom HLSL node including /Project/KomorebiMaterialFunctions.ush at file scope."""
    node = _add_expr(mat, unreal.MaterialExpressionCustom, x, y)
    node.set_editor_property("description", description)
    node.set_editor_property("output_type", output_type)
    node.set_editor_property("include_file_paths", [SHADER_INCLUDE_PATH])

    custom_inputs = []
    for in_name in input_names:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", in_name)
        custom_inputs.append(ci)
    node.set_editor_property("inputs", custom_inputs)

    if additional_outputs:
        extra_outs = []
        for out_name, out_ty in additional_outputs:
            co = unreal.CustomOutput()
            co.set_editor_property("output_name", out_name)
            co.set_editor_property("output_type", out_ty)
            extra_outs.append(co)
        node.set_editor_property("additional_outputs", extra_outs)

    node.set_editor_property("code", code)
    return node


def _connect(src: Any, src_pin: str, dst: Any, dst_pin: str) -> None:
    unreal.MaterialEditingLibrary.connect_material_expressions(src, src_pin, dst, dst_pin)


def _connect_prop(src: Any, src_pin: str, prop: Any) -> None:
    unreal.MaterialEditingLibrary.connect_material_property(src, src_pin, prop)


def _finalize_material(mat: Any) -> Any:
    unreal.MaterialEditingLibrary.layout_material_expressions(mat)
    unreal.MaterialEditingLibrary.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat)
    return mat


def _build_m_street_vertex_pbr() -> Any:
    mat = _new_clean_material("M_StreetVertexPBR")
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_CLEAR_COAT)
    _try_set_prop(mat, "used_with_skeletal_mesh", True)
    _try_set_prop(mat, "used_with_instanced_static_meshes", True)

    vcol = _add_expr(mat, unreal.MaterialExpressionVertexColor, -520, -80)
    tint = _add_vector_param(mat, "Tint", (1.0, 1.0, 1.0, 1.0), -520, 140)

    cmot = unreal.CustomMaterialOutputType
    code = (
        "// Calls DecodeStreetMaterialClass from /Project/KomorebiMaterialFunctions.ush\n"
        "float Flash = saturate((max(max(Tint.r, Tint.g), Tint.b) - 1.5f) * 0.45f);\n"
        "float3 CleanTint = min(Tint, float3(1.25f, 1.25f, 1.25f));\n"
        "float3 LinCol = max(VertexRGB * CleanTint, float3(0.0f, 0.0f, 0.0f));\n"
        "float r, m, c, s;\n"
        "int detailMode;\n"
        "DecodeStreetMaterialClass(VertexAlpha, LinCol, r, m, c, s, detailMode);\n"
        "OutRoughness = r;\n"
        "OutMetallic = m;\n"
        "OutClearCoat = c;\n"
        "OutEmissive = float3(3.2f, 2.8f, 2.4f) * Flash;\n"
        "return LinCol;"
    )
    custom = _add_custom_hlsl(
        mat,
        "DecodeStreetVertexPBR",
        code,
        ["VertexRGB", "VertexAlpha", "Tint"],
        cmot.CMOT_FLOAT3,
        [
            ("OutRoughness", cmot.CMOT_FLOAT1),
            ("OutMetallic", cmot.CMOT_FLOAT1),
            ("OutClearCoat", cmot.CMOT_FLOAT1),
            ("OutEmissive", cmot.CMOT_FLOAT3),
        ],
        -180,
        0,
    )
    _connect(vcol, "RGB", custom, "VertexRGB")
    _connect(vcol, "A", custom, "VertexAlpha")
    _connect(tint, "RGB", custom, "Tint")

    _connect_prop(custom, "return", unreal.MaterialProperty.MP_BASE_COLOR)
    _connect_prop(custom, "OutRoughness", unreal.MaterialProperty.MP_ROUGHNESS)
    _connect_prop(custom, "OutMetallic", unreal.MaterialProperty.MP_METALLIC)
    _connect_prop(custom, "OutClearCoat", unreal.MaterialProperty.MP_CUSTOM_DATA0)
    _connect_prop(custom, "OutEmissive", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    return _finalize_material(mat)


def _build_m_signal_emissive(mpc: Any) -> Any:
    mat = _new_clean_material("M_SignalEmissive")
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    _try_set_prop(mat, "used_with_skeletal_mesh", True)
    _try_set_prop(mat, "used_with_instanced_static_meshes", True)

    vcol = _add_expr(mat, unreal.MaterialExpressionVertexColor, -560, -180)
    tint = _add_vector_param(mat, "Tint", (1.0, 1.0, 1.0, 1.0), -560, -20)
    wnorm = _add_expr(mat, unreal.MaterialExpressionVertexNormalWS, -560, 140)
    cam = _add_expr(mat, unreal.MaterialExpressionCameraVectorWS, -560, 240)
    night = _add_mpc_param(mat, mpc, "Night", -560, 340)
    aurora = _add_mpc_param(mat, mpc, "Aurora", -560, 440)

    cmot = unreal.CustomMaterialOutputType
    code = (
        "// Calls EvaluateSignalEmissive from /Project/KomorebiMaterialFunctions.ush\n"
        "return EvaluateSignalEmissive(SignalRGB, Tint, WorldNormal, ViewDir, Night, Aurora);"
    )
    custom = _add_custom_hlsl(
        mat,
        "SignalEmissiveHDR",
        code,
        ["SignalRGB", "Tint", "WorldNormal", "ViewDir", "Night", "Aurora"],
        cmot.CMOT_FLOAT3,
        None,
        -180,
        40,
    )
    _connect(vcol, "RGB", custom, "SignalRGB")
    _connect(tint, "RGB", custom, "Tint")
    _connect(wnorm, "", custom, "WorldNormal")
    _connect(cam, "", custom, "ViewDir")
    _connect(night, "", custom, "Night")
    _connect(aurora, "", custom, "Aurora")

    _connect_prop(custom, "return", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    return _finalize_material(mat)


def _build_m_character_pbr(mpc: Any) -> Any:
    mat = _new_clean_material("M_CharacterPBR")
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    _try_set_prop(mat, "used_with_skeletal_mesh", True)

    skind = _add_scalar_param(mat, "SurfaceKind", 4.0, -640, -280)
    uv = _add_expr(mat, unreal.MaterialExpressionTextureCoordinate, -640, -180)
    upper = _add_vector_param(mat, "UpperColor", (0.17, 0.23, 0.30, 1.0), -640, -60)
    lower = _add_vector_param(mat, "LowerColor", (0.13, 0.14, 0.17, 1.0), -640, 120)
    accent = _add_vector_param(mat, "AccentColor", (0.0, 0.92, 1.0, 1.0), -640, 300)
    tint = _add_vector_param(mat, "Tint", (1.0, 1.0, 1.0, 1.0), -640, 480)
    night = _add_mpc_param(mat, mpc, "Night", -640, 640)

    cmot = unreal.CustomMaterialOutputType
    code = (
        "// Calls EvaluateCharacterPBR from /Project/KomorebiMaterialFunctions.ush\n"
        "float3 WorldPos = LWCToFloat(GetWorldPosition(Parameters));\n"
        "float3 ObjOrigin = LWCToFloat(GetObjectWorldPosition(Parameters));\n"
        "float3 LocalPosMeters = (WorldPos - ObjOrigin) * 0.01f;\n"
        "float3 baseCol, emissive;\n"
        "float rough, metal, ao;\n"
        "EvaluateCharacterPBR(SurfaceKind, LocalPosMeters, UV, UpperColor, LowerColor, AccentColor, Tint, Night,\n"
        "    baseCol, rough, metal, emissive, ao);\n"
        "OutRoughness = rough;\n"
        "OutMetallic = metal;\n"
        "OutEmissive = emissive;\n"
        "OutAO = ao;\n"
        "return baseCol;"
    )
    custom = _add_custom_hlsl(
        mat,
        "CharacterPBR",
        code,
        ["SurfaceKind", "UV", "UpperColor", "LowerColor", "AccentColor", "Tint", "Night"],
        cmot.CMOT_FLOAT3,
        [
            ("OutRoughness", cmot.CMOT_FLOAT1),
            ("OutMetallic", cmot.CMOT_FLOAT1),
            ("OutEmissive", cmot.CMOT_FLOAT3),
            ("OutAO", cmot.CMOT_FLOAT1),
        ],
        -220,
        40,
    )
    _connect(skind, "", custom, "SurfaceKind")
    _connect(uv, "", custom, "UV")
    _connect(upper, "RGB", custom, "UpperColor")
    _connect(lower, "RGB", custom, "LowerColor")
    _connect(accent, "RGB", custom, "AccentColor")
    _connect(tint, "RGB", custom, "Tint")
    _connect(night, "", custom, "Night")

    _connect_prop(custom, "return", unreal.MaterialProperty.MP_BASE_COLOR)
    _connect_prop(custom, "OutRoughness", unreal.MaterialProperty.MP_ROUGHNESS)
    _connect_prop(custom, "OutMetallic", unreal.MaterialProperty.MP_METALLIC)
    _connect_prop(custom, "OutEmissive", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    _connect_prop(custom, "OutAO", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
    return _finalize_material(mat)


def _build_m_building_parallax(mpc: Any) -> Any:
    mat = _new_clean_material("M_BuildingParallax")
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_CLEAR_COAT)
    _try_set_prop(mat, "used_with_instanced_static_meshes", True)

    wpos = _add_expr(mat, unreal.MaterialExpressionWorldPosition, -620, -240)
    wnorm = _add_expr(mat, unreal.MaterialExpressionVertexNormalWS, -620, -120)
    cam = _add_expr(mat, unreal.MaterialExpressionCameraVectorWS, -620, 0)
    night = _add_mpc_param(mat, mpc, "Night", -620, 120)
    neon_a = _add_mpc_param(mat, mpc, "ChapterFoliage", -620, 220)
    neon_b = _add_mpc_param(mat, mpc, "ChapterLeafLight", -620, 320)

    cmot = unreal.CustomMaterialOutputType
    code = (
        "// Calls EvaluateBuildingFacade -> EvaluateBuildingParallaxWindow from /Project/KomorebiMaterialFunctions.ush\n"
        "float3 baseCol, emissive;\n"
        "float metal, rough, coat;\n"
        "EvaluateBuildingFacade(WorldPos, WorldNormal, ViewDir, Night, NeonA, NeonB,\n"
        "    baseCol, metal, rough, coat, emissive);\n"
        "OutMetallic = metal;\n"
        "OutRoughness = rough;\n"
        "OutClearCoat = coat;\n"
        "OutEmissive = emissive;\n"
        "return baseCol;"
    )
    custom = _add_custom_hlsl(
        mat,
        "BuildingParallaxFacade",
        code,
        ["WorldPos", "WorldNormal", "ViewDir", "Night", "NeonA", "NeonB"],
        cmot.CMOT_FLOAT3,
        [
            ("OutMetallic", cmot.CMOT_FLOAT1),
            ("OutRoughness", cmot.CMOT_FLOAT1),
            ("OutClearCoat", cmot.CMOT_FLOAT1),
            ("OutEmissive", cmot.CMOT_FLOAT3),
        ],
        -220,
        0,
    )
    _connect(wpos, "", custom, "WorldPos")
    _connect(wnorm, "", custom, "WorldNormal")
    _connect(cam, "", custom, "ViewDir")
    _connect(night, "", custom, "Night")
    _connect(neon_a, "", custom, "NeonA")
    _connect(neon_b, "", custom, "NeonB")

    _connect_prop(custom, "return", unreal.MaterialProperty.MP_BASE_COLOR)
    _connect_prop(custom, "OutMetallic", unreal.MaterialProperty.MP_METALLIC)
    _connect_prop(custom, "OutRoughness", unreal.MaterialProperty.MP_ROUGHNESS)
    _connect_prop(custom, "OutClearCoat", unreal.MaterialProperty.MP_CUSTOM_DATA0)
    _connect_prop(custom, "OutEmissive", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    return _finalize_material(mat)


def _build_m_terrain_neokyoto(mpc: Any) -> Any:
    mat = _new_clean_material("M_TerrainNeoKyoto")
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_CLEAR_COAT)

    wpos = _add_expr(mat, unreal.MaterialExpressionWorldPosition, -640, -260)
    vcol = _add_expr(mat, unreal.MaterialExpressionVertexColor, -640, -140)
    t_param = _add_mpc_param(mat, mpc, "Time", -640, 40)
    night = _add_mpc_param(mat, mpc, "Night", -640, 140)
    grass = _add_mpc_param(mat, mpc, "ChapterGrass", -640, 240)
    path = _add_mpc_param(mat, mpc, "ChapterPath", -640, 340)
    neon = _add_mpc_param(mat, mpc, "ChapterFoliage", -640, 440)

    cmot = unreal.CustomMaterialOutputType
    code = (
        "// Calls EvaluateNeoKyotoTerrain from /Project/KomorebiMaterialFunctions.ush\n"
        "float3 baseCol, emissive;\n"
        "float rough, coat;\n"
        "EvaluateNeoKyotoTerrain(WorldPos, float4(LandRGB, LandA), Time, Night, ChapterGrass, ChapterPath, ChapterNeon,\n"
        "    baseCol, rough, coat, emissive);\n"
        "OutRoughness = rough;\n"
        "OutClearCoat = coat;\n"
        "OutEmissive = emissive;\n"
        "return baseCol;"
    )
    custom = _add_custom_hlsl(
        mat,
        "NeoKyotoTerrainPBR",
        code,
        ["WorldPos", "LandRGB", "LandA", "Time", "Night", "ChapterGrass", "ChapterPath", "ChapterNeon"],
        cmot.CMOT_FLOAT3,
        [
            ("OutRoughness", cmot.CMOT_FLOAT1),
            ("OutClearCoat", cmot.CMOT_FLOAT1),
            ("OutEmissive", cmot.CMOT_FLOAT3),
        ],
        -220,
        40,
    )
    _connect(wpos, "", custom, "WorldPos")
    _connect(vcol, "RGB", custom, "LandRGB")
    _connect(vcol, "A", custom, "LandA")
    _connect(t_param, "", custom, "Time")
    _connect(night, "", custom, "Night")
    _connect(grass, "", custom, "ChapterGrass")
    _connect(path, "", custom, "ChapterPath")
    _connect(neon, "", custom, "ChapterNeon")

    _connect_prop(custom, "return", unreal.MaterialProperty.MP_BASE_COLOR)
    _connect_prop(custom, "OutRoughness", unreal.MaterialProperty.MP_ROUGHNESS)
    _connect_prop(custom, "OutClearCoat", unreal.MaterialProperty.MP_CUSTOM_DATA0)
    _connect_prop(custom, "OutEmissive", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    return _finalize_material(mat)


def _build_m_terrain_snowbound(mpc: Any) -> Any:
    mat = _new_clean_material("M_TerrainSnowbound")
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_CLEAR_COAT)

    wpos = _add_expr(mat, unreal.MaterialExpressionWorldPosition, -680, -340)
    wnorm = _add_expr(mat, unreal.MaterialExpressionVertexNormalWS, -680, -240)
    cam = _add_expr(mat, unreal.MaterialExpressionCameraVectorWS, -680, -140)
    vcol = _add_expr(mat, unreal.MaterialExpressionVertexColor, -680, -40)
    t_param = _add_mpc_param(mat, mpc, "Time", -680, 140)
    night = _add_mpc_param(mat, mpc, "Night", -680, 220)
    aurora = _add_mpc_param(mat, mpc, "Aurora", -680, 300)
    sun = _add_mpc_param(mat, mpc, "SunDir", -680, 380)
    lamp = _add_mpc_param(mat, mpc, "LampPosStrength", -680, 460)
    lamp_col = _add_mpc_param(mat, mpc, "LampColor", -680, 540)

    cmot = unreal.CustomMaterialOutputType
    code = (
        "// Calls EvaluateSnowboundTerrain from /Project/KomorebiMaterialFunctions.ush\n"
        "float3 baseCol, emissive;\n"
        "float rough, coat;\n"
        "EvaluateSnowboundTerrain(WorldPos, WorldNormal, ViewDir, float4(LandRGB, LandA), Time, Night, Aurora,\n"
        "    SunDir.xyz, LampPosStrength, LampColor.rgb, baseCol, rough, coat, emissive);\n"
        "OutRoughness = rough;\n"
        "OutClearCoat = coat;\n"
        "OutEmissive = emissive;\n"
        "return baseCol;"
    )
    custom = _add_custom_hlsl(
        mat,
        "SnowboundTerrainPBR",
        code,
        [
            "WorldPos",
            "WorldNormal",
            "ViewDir",
            "LandRGB",
            "LandA",
            "Time",
            "Night",
            "Aurora",
            "SunDir",
            "LampPosStrength",
            "LampColor",
        ],
        cmot.CMOT_FLOAT3,
        [
            ("OutRoughness", cmot.CMOT_FLOAT1),
            ("OutClearCoat", cmot.CMOT_FLOAT1),
            ("OutEmissive", cmot.CMOT_FLOAT3),
        ],
        -220,
        60,
    )
    _connect(wpos, "", custom, "WorldPos")
    _connect(wnorm, "", custom, "WorldNormal")
    _connect(cam, "", custom, "ViewDir")
    _connect(vcol, "RGB", custom, "LandRGB")
    _connect(vcol, "A", custom, "LandA")
    _connect(t_param, "", custom, "Time")
    _connect(night, "", custom, "Night")
    _connect(aurora, "", custom, "Aurora")
    _connect(sun, "", custom, "SunDir")
    _connect(lamp, "", custom, "LampPosStrength")
    _connect(lamp_col, "", custom, "LampColor")

    _connect_prop(custom, "return", unreal.MaterialProperty.MP_BASE_COLOR)
    _connect_prop(custom, "OutRoughness", unreal.MaterialProperty.MP_ROUGHNESS)
    _connect_prop(custom, "OutClearCoat", unreal.MaterialProperty.MP_CUSTOM_DATA0)
    _connect_prop(custom, "OutEmissive", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    return _finalize_material(mat)


def _build_m_blade_trail() -> Any:
    mat = _new_clean_material("M_BladeTrail")
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("two_sided", True)

    uv = _add_expr(mat, unreal.MaterialExpressionTextureCoordinate, -540, -180)
    vcol = _add_expr(mat, unreal.MaterialExpressionVertexColor, -540, -60)
    fade = _add_scalar_param(mat, "Fade", 1.0, -540, 120)
    is_glow = _add_scalar_param(mat, "IsGlowLayer", 0.0, -540, 220)

    cmot = unreal.CustomMaterialOutputType
    code = (
        "// Calls EvaluateBladeTrailCore / EvaluateBladeTrailGlow from /Project/KomorebiMaterialFunctions.ush\n"
        "float3 outCol;\n"
        "float outAlpha;\n"
        "if (IsGlowLayer > 0.5f)\n"
        "{\n"
        "    EvaluateBladeTrailGlow(UV, VertexRGB, Fade, outCol, outAlpha);\n"
        "}\n"
        "else\n"
        "{\n"
        "    EvaluateBladeTrailCore(UV, VertexRGB, Fade, outCol, outAlpha);\n"
        "}\n"
        "OutOpacity = saturate(outAlpha);\n"
        "return outCol;"
    )
    custom = _add_custom_hlsl(
        mat,
        "PlasmaBladeTrail",
        code,
        ["UV", "VertexRGB", "Fade", "IsGlowLayer"],
        cmot.CMOT_FLOAT3,
        [("OutOpacity", cmot.CMOT_FLOAT1)],
        -180,
        0,
    )
    _connect(uv, "", custom, "UV")
    _connect(vcol, "RGB", custom, "VertexRGB")
    _connect(fade, "", custom, "Fade")
    _connect(is_glow, "", custom, "IsGlowLayer")

    _connect_prop(custom, "return", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    _connect_prop(custom, "OutOpacity", unreal.MaterialProperty.MP_OPACITY)
    return _finalize_material(mat)


def _build_m_aurora_sky(mpc: Any) -> Any:
    mat = _new_clean_material("M_AuroraSky")
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("two_sided", True)
    _try_set_prop(mat, "is_sky", True)

    cam = _add_expr(mat, unreal.MaterialExpressionCameraVectorWS, -580, -200)
    t_param = _add_mpc_param(mat, mpc, "Time", -580, -80)
    night = _add_mpc_param(mat, mpc, "Night", -580, 20)
    twilight = _add_mpc_param(mat, mpc, "Twilight", -580, 120)
    aurora = _add_mpc_param(mat, mpc, "Aurora", -580, 220)
    sun = _add_mpc_param(mat, mpc, "SunDir", -580, 320)

    cmot = unreal.CustomMaterialOutputType
    code = (
        "// Calls EvaluateAuroraSkyDome -> EvaluateAuroraRibbon from /Project/KomorebiMaterialFunctions.ush\n"
        "// CameraVector points from sky surface back to camera; negate for outgoing view ray.\n"
        "float3 SkyDir = normalize(-CameraVector);\n"
        "return EvaluateAuroraSkyDome(SkyDir, Time, Night, Twilight, Aurora, SunDir.xyz);"
    )
    custom = _add_custom_hlsl(
        mat,
        "AuroraSkyDome",
        code,
        ["CameraVector", "Time", "Night", "Twilight", "Aurora", "SunDir"],
        cmot.CMOT_FLOAT3,
        None,
        -200,
        20,
    )
    _connect(cam, "", custom, "CameraVector")
    _connect(t_param, "", custom, "Time")
    _connect(night, "", custom, "Night")
    _connect(twilight, "", custom, "Twilight")
    _connect(aurora, "", custom, "Aurora")
    _connect(sun, "", custom, "SunDir")

    _connect_prop(custom, "return", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    return _finalize_material(mat)


def _build_pp_komorebi_finish(mpc: Any) -> Any:
    mat = _new_clean_material("PP_KomorebiFinish")
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
    # Apply after tonemapping for crisp depth-edge ink outlines, canvas grain, vignette & micro-dither
    if hasattr(unreal.BlendableLocation, "BL_SCENE_COLOR_AFTER_TONEMAPPING"):
        mat.set_editor_property("blendable_location", unreal.BlendableLocation.BL_SCENE_COLOR_AFTER_TONEMAPPING)
    elif hasattr(unreal.BlendableLocation, "BL_AFTER_TONEMAPPING"):
        mat.set_editor_property("blendable_location", unreal.BlendableLocation.BL_AFTER_TONEMAPPING)

    scene_col = _add_expr(mat, unreal.MaterialExpressionSceneTexture, -680, -340)
    scene_col.set_editor_property("scene_texture_id", unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0)

    scene_depth = _add_expr(mat, unreal.MaterialExpressionSceneTexture, -680, -200)
    scene_depth.set_editor_property("scene_texture_id", unreal.SceneTextureId.PPI_SCENE_DEPTH)

    screen_pos = _add_expr(mat, unreal.MaterialExpressionScreenPosition, -680, -60)
    view_prop = _add_expr(mat, unreal.MaterialExpressionViewProperty, -680, 40)
    _try_set_prop(view_prop, "property", unreal.MaterialExposedViewProperty.MEVP_VIEW_SIZE)

    t_param = _add_mpc_param(mat, mpc, "Time", -680, 140)
    exposure = _add_mpc_param(mat, mpc, "Exposure", -680, 220)
    ink_str = _add_mpc_param(mat, mpc, "InkStrength", -680, 300)
    impasto = _add_mpc_param(mat, mpc, "ImpastoStrength", -680, 380)
    ink_far = _add_scalar_param(mat, "InkFarMeters", 160.0, -680, 460)
    ink_col = _add_vector_param(mat, "InkColor", (0.03, 0.04, 0.07, 1.0), -680, 560)
    vignette = _add_scalar_param(mat, "VignetteStrength", 0.22, -680, 740)

    cmot = unreal.CustomMaterialOutputType
    code = (
        "// Calls ApplyKomorebiFinishPostProcess -> EvaluateKomorebiInkEdge from /Project/KomorebiMaterialFunctions.ush\n"
        "float CenterMeters = CenterDepthCm * 0.01f;\n"
        "float4 NeighborMeters = float4(CenterMeters, CenterMeters, CenterMeters, CenterMeters);\n"
        "#if !defined(SCENE_TEXTURES_DISABLED)\n"
        "if (InkStrength > 0.001f)\n"
        "{\n"
        "    float2 Px = 1.1f / max(ViewportSize, float2(1.0f, 1.0f));\n"
        "    float dR = SceneTextureLookup(clamp(ScreenUV + float2(Px.x, 0.0f), 0.001f, 0.999f), 1, false).r * 0.01f;\n"
        "    float dL = SceneTextureLookup(clamp(ScreenUV - float2(Px.x, 0.0f), 0.001f, 0.999f), 1, false).r * 0.01f;\n"
        "    float dU = SceneTextureLookup(clamp(ScreenUV + float2(0.0f, Px.y), 0.001f, 0.999f), 1, false).r * 0.01f;\n"
        "    float dD = SceneTextureLookup(clamp(ScreenUV - float2(0.0f, Px.y), 0.001f, 0.999f), 1, false).r * 0.01f;\n"
        "    NeighborMeters = float4(dR, dL, dU, dD);\n"
        "}\n"
        "#endif\n"
        "return ApplyKomorebiFinishPostProcess(\n"
        "    SceneColor, ScreenUV, ViewportSize, CenterMeters, NeighborMeters,\n"
        "    Time, Exposure, InkStrength, InkFarMeters, InkColor, ImpastoStrength, VignetteStrength);"
    )
    custom = _add_custom_hlsl(
        mat,
        "KomorebiFinishPostProcess",
        code,
        [
            "SceneColor",
            "CenterDepthCm",
            "ScreenUV",
            "ViewportSize",
            "Time",
            "Exposure",
            "InkStrength",
            "ImpastoStrength",
            "InkFarMeters",
            "InkColor",
            "VignetteStrength",
        ],
        cmot.CMOT_FLOAT3,
        None,
        -200,
        80,
    )
    _connect(scene_col, "Color", custom, "SceneColor")
    _connect(scene_depth, "Color", custom, "CenterDepthCm")
    _connect(screen_pos, "ViewportUV", custom, "ScreenUV")
    _connect(view_prop, "", custom, "ViewportSize")
    _connect(t_param, "", custom, "Time")
    _connect(exposure, "", custom, "Exposure")
    _connect(ink_str, "", custom, "InkStrength")
    _connect(impasto, "", custom, "ImpastoStrength")
    _connect(ink_far, "", custom, "InkFarMeters")
    _connect(ink_col, "RGB", custom, "InkColor")
    _connect(vignette, "", custom, "VignetteStrength")

    _connect_prop(custom, "return", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    return _finalize_material(mat)


def assign_street_materials(m_street: Any, m_signal: Any) -> None:
    """Bind VertexPaint -> M_StreetVertexPBR and Signal -> M_SignalEmissive on imported street meshes."""
    street_dest = "/Game/Assets/Street"
    if not unreal.EditorAssetLibrary.does_directory_exist(street_dest):
        return

    updated = 0
    for asset_path in unreal.EditorAssetLibrary.list_assets(street_dest, recursive=True):
        obj = unreal.EditorAssetLibrary.load_asset(asset_path)
        if isinstance(obj, unreal.StaticMesh):
            changed = False
            slots = list(obj.get_editor_property("static_materials"))
            for slot in slots:
                slot_name = str(slot.material_slot_name).lower()
                if "signal" in slot_name:
                    slot.material_interface = m_signal
                    changed = True
                elif "vertexpaint" in slot_name or slot.material_interface is None:
                    slot.material_interface = m_street
                    changed = True
            if changed:
                obj.set_editor_property("static_materials", slots)
                unreal.EditorAssetLibrary.save_loaded_asset(obj)
                updated += 1
        elif isinstance(obj, unreal.SkeletalMesh):
            changed = False
            mats = list(obj.get_editor_property("materials"))
            for slot in mats:
                slot_name = str(slot.material_slot_name).lower()
                if "signal" in slot_name:
                    slot.material_interface = m_signal
                    changed = True
                elif "vertexpaint" in slot_name or slot.material_interface is None:
                    slot.material_interface = m_street
                    changed = True
            if changed:
                obj.set_editor_property("materials", mats)
                unreal.EditorAssetLibrary.save_loaded_asset(obj)
                updated += 1
    _log(f"Bound M_StreetVertexPBR & M_SignalEmissive across {updated} street meshes")


def build_all_materials(mpc: Any) -> dict[str, Any]:
    _log("Stage 3/5: Building 9 Komorebi master materials...")
    mats = {
        "M_StreetVertexPBR": _build_m_street_vertex_pbr(),
        "M_SignalEmissive": _build_m_signal_emissive(mpc),
        "M_CharacterPBR": _build_m_character_pbr(mpc),
        "M_BuildingParallax": _build_m_building_parallax(mpc),
        "M_TerrainNeoKyoto": _build_m_terrain_neokyoto(mpc),
        "M_TerrainSnowbound": _build_m_terrain_snowbound(mpc),
        "M_BladeTrail": _build_m_blade_trail(),
        "M_AuroraSky": _build_m_aurora_sky(mpc),
        "PP_KomorebiFinish": _build_pp_komorebi_finish(mpc),
    }
    assign_street_materials(mats["M_StreetVertexPBR"], mats["M_SignalEmissive"])
    _log(f"Compiled & saved {len(mats)} master materials in /Game/Materials/")
    return mats


# -----------------------------------------------------------------------------
# Stage 4: Enhanced Input Actions & IMC_Komorebi (from legacy-web/src/figure/input.js)
# -----------------------------------------------------------------------------

def _make_key(key_name: str) -> Any:
    k = unreal.Key()
    k.set_editor_property("key_name", key_name)
    return k


def _mod_negate(x: bool = True, y: bool = True, z: bool = True) -> Any:
    m = unreal.InputModifierNegate()
    m.set_editor_property("x", x)
    m.set_editor_property("y", y)
    m.set_editor_property("z", z)
    return m


def _mod_swizzle_yxz() -> Any:
    m = unreal.InputModifierSwizzleAxis()
    m.set_editor_property("order", unreal.InputAxisSwizzle.YXZ)
    return m


def _mod_deadzone(lower: float = 0.15, upper: float = 1.0) -> Any:
    m = unreal.InputModifierDeadZone()
    m.set_editor_property("lower_threshold", lower)
    m.set_editor_property("upper_threshold", upper)
    return m


def _map_key(imc: Any, action: Any, key_name: str, modifiers: list[Any] | None = None) -> None:
    mapping = imc.map_key(action, _make_key(key_name))
    if modifiers:
        mapping.set_editor_property("modifiers", modifiers)


def build_enhanced_input() -> tuple[dict[str, Any], Any]:
    """Create all 20 Input Actions and IMC_Komorebi with exact keyboard/mouse/gamepad bindings."""
    _log("Stage 4/5: Building Enhanced Input Actions & IMC_Komorebi...")
    input_dir = "/Game/Input"
    _ensure_asset_dir(input_dir)

    vt = unreal.InputActionValueType
    action_specs: list[tuple[str, Any]] = [
        ("IA_Move", vt.AXIS2D),
        ("IA_Look", vt.AXIS2D),
        ("IA_Zoom", vt.AXIS1D),
        ("IA_Jump", vt.BOOLEAN),
        ("IA_Sprint", vt.BOOLEAN),
        ("IA_WalkToggle", vt.BOOLEAN),
        ("IA_Attack", vt.BOOLEAN),
        ("IA_Heavy", vt.BOOLEAN),
        ("IA_Spark", vt.BOOLEAN),
        ("IA_Dash", vt.BOOLEAN),
        ("IA_Flare", vt.BOOLEAN),
        ("IA_Whirl", vt.BOOLEAN),
        ("IA_Guard", vt.BOOLEAN),
        ("IA_Evade", vt.BOOLEAN),
        ("IA_LockTarget", vt.BOOLEAN),
        ("IA_NextTarget", vt.BOOLEAN),
        ("IA_Ride", vt.BOOLEAN),
        ("IA_ToggleMode", vt.BOOLEAN),
        ("IA_Pause", vt.BOOLEAN),
        ("IA_DismissComms", vt.BOOLEAN),
    ]

    actions: dict[str, Any] = {}
    for name, value_type in action_specs:
        ia = _create_or_load_asset(name, input_dir, unreal.InputAction, unreal.InputActionFactory())
        ia.set_editor_property("value_type", value_type)
        unreal.EditorAssetLibrary.save_loaded_asset(ia)
        actions[name] = ia

    imc = _create_or_load_asset(
        "IMC_Komorebi",
        input_dir,
        unreal.InputMappingContext,
        unreal.InputMappingContextFactory(),
    )
    imc.unmap_all()

    # IA_Move: WASD + Arrows + Gamepad Left Stick (X = right, Y = forward)
    _map_key(imc, actions["IA_Move"], "D")
    _map_key(imc, actions["IA_Move"], "Right")
    _map_key(imc, actions["IA_Move"], "A", [_mod_negate(True, False, False)])
    _map_key(imc, actions["IA_Move"], "Left", [_mod_negate(True, False, False)])
    _map_key(imc, actions["IA_Move"], "W", [_mod_swizzle_yxz()])
    _map_key(imc, actions["IA_Move"], "Up", [_mod_swizzle_yxz()])
    _map_key(imc, actions["IA_Move"], "S", [_mod_swizzle_yxz(), _mod_negate(False, True, False)])
    _map_key(imc, actions["IA_Move"], "Down", [_mod_swizzle_yxz(), _mod_negate(False, True, False)])
    _map_key(imc, actions["IA_Move"], "Gamepad_Left2D", [_mod_deadzone(0.15, 1.0)])

    # IA_Look: Mouse 2D + Gamepad Right Stick
    _map_key(imc, actions["IA_Look"], "Mouse2D")
    _map_key(imc, actions["IA_Look"], "Gamepad_Right2D", [_mod_deadzone(0.15, 1.0)])

    # IA_Zoom: Mouse Wheel + Gamepad D-Pad Up/Down
    _map_key(imc, actions["IA_Zoom"], "MouseWheelAxis")
    _map_key(imc, actions["IA_Zoom"], "Gamepad_DPad_Up", [_mod_negate(True, False, False)])
    _map_key(imc, actions["IA_Zoom"], "Gamepad_DPad_Down")

    # Movement & Stance
    _map_key(imc, actions["IA_Jump"], "SpaceBar")
    _map_key(imc, actions["IA_Jump"], "Gamepad_FaceButton_Bottom")  # A / Cross

    _map_key(imc, actions["IA_Sprint"], "LeftShift")
    _map_key(imc, actions["IA_Sprint"], "RightShift")
    _map_key(imc, actions["IA_Sprint"], "Gamepad_LeftThumbstick")   # L3
    _map_key(imc, actions["IA_Sprint"], "Gamepad_RightShoulder")    # RB

    _map_key(imc, actions["IA_WalkToggle"], "CapsLock")
    _map_key(imc, actions["IA_WalkToggle"], "LeftAlt")
    _map_key(imc, actions["IA_WalkToggle"], "RightAlt")

    # Combat & Unlocked Powers (exact legacy-web/src/figure/input.js bindings)
    _map_key(imc, actions["IA_Attack"], "LeftMouseButton")
    _map_key(imc, actions["IA_Attack"], "J")
    _map_key(imc, actions["IA_Attack"], "Gamepad_FaceButton_Left")  # X / Square

    _map_key(imc, actions["IA_Heavy"], "F")
    _map_key(imc, actions["IA_Heavy"], "Gamepad_LeftShoulder")      # LB

    _map_key(imc, actions["IA_Spark"], "Q")
    _map_key(imc, actions["IA_Spark"], "Gamepad_FaceButton_Top")    # Y / Triangle

    _map_key(imc, actions["IA_Dash"], "E")
    _map_key(imc, actions["IA_Dash"], "Gamepad_FaceButton_Right")   # B / Circle

    _map_key(imc, actions["IA_Flare"], "R")
    _map_key(imc, actions["IA_Flare"], "Gamepad_Special_Right")     # Start

    _map_key(imc, actions["IA_Whirl"], "C")
    _map_key(imc, actions["IA_Whirl"], "Gamepad_RightThumbstick")   # R3

    _map_key(imc, actions["IA_Guard"], "Z")
    _map_key(imc, actions["IA_Guard"], "RightMouseButton")
    _map_key(imc, actions["IA_Guard"], "Gamepad_LeftTrigger")       # LT

    _map_key(imc, actions["IA_Evade"], "X")
    _map_key(imc, actions["IA_Evade"], "Gamepad_RightTrigger")      # RT

    _map_key(imc, actions["IA_LockTarget"], "T")
    _map_key(imc, actions["IA_LockTarget"], "Gamepad_DPad_Left")    # pad btn 14

    _map_key(imc, actions["IA_NextTarget"], "Y")
    _map_key(imc, actions["IA_NextTarget"], "Gamepad_DPad_Right")   # pad btn 15

    # Vehicle, Mode, Pause & Comms
    _map_key(imc, actions["IA_Ride"], "G")
    _map_key(imc, actions["IA_Ride"], "Gamepad_Special_Left")       # Back / Select

    _map_key(imc, actions["IA_ToggleMode"], "M")

    _map_key(imc, actions["IA_Pause"], "Escape")
    _map_key(imc, actions["IA_Pause"], "P")
    _map_key(imc, actions["IA_Pause"], "H")

    _map_key(imc, actions["IA_DismissComms"], "K")
    _map_key(imc, actions["IA_DismissComms"], "Enter")

    unreal.EditorAssetLibrary.save_loaded_asset(imc)
    _log(f"Saved {len(actions)} InputActions and IMC_Komorebi in {input_dir}")
    return actions, imc


# -----------------------------------------------------------------------------
# Stage 5: Level Construction (/Game/Maps/L_NeoKyoto & /Game/Maps/L_Snowbound)
# -----------------------------------------------------------------------------

def _spawn_cpp_actor(
    actor_subsystem: Any,
    class_short_name: str,
    label: str,
    location: tuple[float, float, float] = (0.0, 0.0, 0.0),
    rotation: tuple[float, float, float] = (0.0, 0.0, 0.0),
) -> Any:
    """Spawn a KomorebiGame C++ actor by its UClass path (/Script/KomorebiGame.<Name>)."""
    class_path = f"/Script/KomorebiGame.{class_short_name}"
    cls = unreal.load_class(None, class_path)
    if cls is None:
        _warn(f"C++ class '{class_path}' not loaded; skipping spawn of '{label}' (build KomorebiEditor first).")
        return None
    actor = actor_subsystem.spawn_actor_from_class(
        cls,
        unreal.Vector(location[0], location[1], location[2]),
        unreal.Rotator(rotation[0], rotation[1], rotation[2]),
    )
    if actor is not None:
        actor.set_actor_label(label)
    return actor


def _spawn_unbound_post_process(actor_subsystem: Any, pp_material: Any, label: str = "PP_KomorebiVolume") -> Any:
    ppv = actor_subsystem.spawn_actor_from_class(
        unreal.PostProcessVolume,
        unreal.Vector(0.0, 0.0, 0.0),
        unreal.Rotator(0.0, 0.0, 0.0),
    )
    ppv.set_actor_label(label)
    ppv.set_editor_property("unbound", True)

    settings = ppv.get_editor_property("settings")
    if pp_material is not None:
        wb = unreal.WeightedBlendable(weight=1.0, object=pp_material)
        blendables = settings.get_editor_property("weighted_blendables")
        arr = list(blendables.get_editor_property("array"))
        arr.append(wb)
        blendables.set_editor_property("array", arr)
        settings.set_editor_property("weighted_blendables", blendables)
        ppv.set_editor_property("settings", settings)
    return ppv


def build_maps(mpc: Any, materials: dict[str, Any]) -> None:
    """Programmatically construct and save /Game/Maps/L_NeoKyoto and /Game/Maps/L_Snowbound."""
    _log("Stage 5/5: Building /Game/Maps/L_NeoKyoto and /Game/Maps/L_Snowbound...")
    _ensure_asset_dir("/Game/Maps")

    level_sub = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actor_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    # --- 5A. /Game/Maps/L_NeoKyoto ---
    neokyoto_map = "/Game/Maps/L_NeoKyoto"
    if not level_sub.new_level(neokyoto_map):
        _warn(f"new_level({neokyoto_map}) returned False; attempting load_level")
        level_sub.load_level(neokyoto_map)

    terrain = _spawn_cpp_actor(actor_sub, "KomorebiTerrainActor", "KomorebiTerrain_NeoKyoto")
    if terrain is not None:
        _try_set_prop(terrain, "b_is_snowbound", False)
        _try_set_prop(terrain, "is_snowbound", False)
        _try_set_prop(terrain, "terrain_material", materials.get("M_TerrainNeoKyoto"))

    city = _spawn_cpp_actor(actor_sub, "NeoKyotoCityActor", "NeoKyotoCity")
    if city is not None:
        _try_set_prop(city, "building_material", materials.get("M_BuildingParallax"))

    _spawn_cpp_actor(actor_sub, "StreetPropsActor", "NeoKyotoStreetProps")

    atmo = _spawn_cpp_actor(actor_sub, "DayNightAtmosphereActor", "DayNightAtmosphere_NeoKyoto")
    if atmo is not None:
        _try_set_prop(atmo, "b_is_snowbound", False)
        _try_set_prop(atmo, "is_snowbound", False)
        _try_set_prop(atmo, "shared_mpc", mpc)

    _spawn_cpp_actor(actor_sub, "ShowcaseCameraDirector", "ShowcaseCameraDirector")
    _spawn_cpp_actor(actor_sub, "OnlineActivitiesActor", "OnlineActivities")
    _spawn_unbound_post_process(actor_sub, materials.get("PP_KomorebiFinish"), "PostProcess_KomorebiFinish")

    # START in legacy-web/src/world/land.js is (x=0, z=78 * SCALE=234m -> UE cm)
    ps_nk = actor_sub.spawn_actor_from_class(
        unreal.PlayerStart,
        unreal.Vector(0.0, 23400.0, 120.0),
        unreal.Rotator(0.0, -90.0, 0.0),
    )
    if ps_nk is not None:
        ps_nk.set_actor_label("PlayerStart_SunleafRow")

    level_sub.save_current_level()
    _log(f"Saved map {neokyoto_map}")

    # --- 5B. /Game/Maps/L_Snowbound ---
    snowbound_map = "/Game/Maps/L_Snowbound"
    if not level_sub.new_level(snowbound_map):
        _warn(f"new_level({snowbound_map}) returned False; attempting load_level")
        level_sub.load_level(snowbound_map)

    sb_terrain = _spawn_cpp_actor(actor_sub, "KomorebiTerrainActor", "KomorebiTerrain_Snowbound")
    if sb_terrain is not None:
        _try_set_prop(sb_terrain, "b_is_snowbound", True)
        _try_set_prop(sb_terrain, "is_snowbound", True)
        _try_set_prop(sb_terrain, "terrain_material", materials.get("M_TerrainSnowbound"))

    sb_world = _spawn_cpp_actor(actor_sub, "SnowboundWorldActor", "SnowboundWorld")
    if sb_world is not None:
        _try_set_prop(sb_world, "aurora_sky_material", materials.get("M_AuroraSky"))

    sb_atmo = _spawn_cpp_actor(actor_sub, "DayNightAtmosphereActor", "DayNightAtmosphere_Snowbound")
    if sb_atmo is not None:
        _try_set_prop(sb_atmo, "b_is_snowbound", True)
        _try_set_prop(sb_atmo, "is_snowbound", True)
        _try_set_prop(sb_atmo, "shared_mpc", mpc)

    _spawn_unbound_post_process(actor_sub, materials.get("PP_KomorebiFinish"), "PostProcess_KomorebiFinish")

    ps_sb = actor_sub.spawn_actor_from_class(
        unreal.PlayerStart,
        unreal.Vector(0.0, 0.0, 220.0),
        unreal.Rotator(0.0, -90.0, 0.0),
    )
    if ps_sb is not None:
        ps_sb.set_actor_label("PlayerStart_SnowboundRidge")

    level_sub.save_current_level()
    _log(f"Saved map {snowbound_map}")


def main() -> None:
    if unreal is None:
        raise RuntimeError("Scripts/ue_import_and_build.py must be run inside UnrealEditor-Cmd.")

    root = _project_root()
    _log(f"Starting Komorebi UE5 automated import & build (ProjectRoot={root})")

    if os.environ.get("KOMOREBI_SKIP_IMPORT", "0") != "1":
        import_all_assets(root)

    mpc = build_shared_mpc()
    materials = build_all_materials(mpc)
    build_enhanced_input()
    build_maps(mpc, materials)

    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    _log("Komorebi UE5 import, material graph compilation, input setup & map build complete!")


if __name__ == "__main__":
    main()
