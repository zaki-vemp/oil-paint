"""Realistic AAA anatomical humanoid builder using MakeHuman (public/figure/starter-man.glb).

Imports the CC0 34k-triangle anatomical MakeHuman mesh, poses it into a natural street A-pose
(or crouched motorcycle riding pose), strips hidden body polygons under clothing, decimates
smoothly to a game-ready ~5k-7k triangle budget while preserving artist-painted skin weights,
bakes realistic facial/skin/eye/hair/clothing PBR vertex colors + COLOR_0.a material classes,
and skins the complete character + custom cyberpunk gear to our 21-bone street armature with
expressive 24 fps animation clips.
"""

from __future__ import annotations

import math
from pathlib import Path
from typing import Sequence

import bmesh
import bpy
from mathutils import Euler, Matrix, Quaternion, Vector

import blender_aa_lib as aa

ROOT_DIR = Path(__file__).resolve().parent.parent
STARTER_MAN_GLB = ROOT_DIR / 'public' / 'figure' / 'starter-man.glb'

# Direct remapping from MakeHuman 65-bone skeleton to our 21-bone street rig
MH_TO_STREET_BONE = {
    'pelvis': 'Pelvis',
    'spine_01': 'Spine',
    'spine_02': 'Spine',
    'spine_03': 'Chest',
    'neck_01': 'Neck',
    'Head': 'Head',
    'clavicle_l': 'Clavicle_L',
    'upperarm_l': 'UpperArm_L',
    'lowerarm_l': 'Forearm_L',
    'hand_l': 'Hand_L',
    'thigh_l': 'Thigh_L',
    'calf_l': 'Shin_L',
    'foot_l': 'Foot_L',
    'ball_l': 'Toe_L',
    'clavicle_r': 'Clavicle_R',
    'upperarm_r': 'UpperArm_R',
    'lowerarm_r': 'Forearm_R',
    'hand_r': 'Hand_R',
    'thigh_r': 'Thigh_R',
    'calf_r': 'Shin_R',
    'foot_r': 'Foot_R',
    'ball_r': 'Toe_R',
}
for _side, _suf in (('L', '_l'), ('R', '_r')):
    for _f in ('thumb', 'index', 'middle', 'ring', 'pinky'):
        for _n in ('01', '02', '03'):
            MH_TO_STREET_BONE[f'{_f}_{_n}{_suf}'] = f'Hand_{_side}'


def _pose_world_rot(arm: bpy.types.Object, bone_name: str, world_rot: Quaternion):
    pb = arm.pose.bones.get(bone_name)
    if pb is None:
        return
    pb.rotation_mode = 'QUATERNION'
    # Express desired world delta rotation in bone's local pose space
    b_q = (arm.matrix_world @ pb.bone.matrix_local).to_quaternion()
    pb.rotation_quaternion = b_q.inverted() @ world_rot @ b_q


def _strip_hidden_skin_under_suit(obj: bpy.types.Object, keep_arms: bool = True, keep_head: bool = True, keep_legs: bool = False):
    """Remove interior torso/pelvis/leg polygons hidden beneath male_casualsuit06 using rest-pose __bind coords."""
    b_attr = obj.data.attributes.get('__bind')
    bind_pts = [d.vector.copy() for d in b_attr.data] if b_attr is not None else [v.co.copy() for v in obj.data.vertices]
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bm.faces.ensure_lookup_table()
    to_del = []
    for f in bm.faces:
        c = sum((bind_pts[v.index] for v in f.verts), Vector()) / max(1, len(f.verts))
        if c.z > 1.505:
            if not keep_head and c.z > 1.56:
                to_del.append(f)
            continue
        # In original MakeHuman T-pose, arms extend along |x| > 0.245, z > 1.30
        is_arm = abs(c.x) > 0.245 and c.z > 1.30
        if is_arm:
            if not keep_arms:
                to_del.append(f)
            continue
        if c.z < 0.20:
            to_del.append(f)
            continue
        if not keep_legs:
            to_del.append(f)
    if to_del:
        bmesh.ops.delete(bm, geom=to_del, context='FACES')
    bm.to_mesh(obj.data)
    bm.free()
    obj.data.update()


def _remap_mh_vertex_groups(obj: bpy.types.Object):
    """Merge MakeHuman 65 vertex groups into our 21 street bone groups (preserving smooth weights)."""
    mesh = obj.data
    n_verts = len(mesh.vertices)
    if n_verts == 0:
        return
    # Accumulate weights per vertex into street bone names
    per_vert: list[dict[str, float]] = [{} for _ in range(n_verts)]
    vg_map = {vg.index: MH_TO_STREET_BONE.get(vg.name, 'Chest') for vg in obj.vertex_groups}
    for v in mesh.vertices:
        acc = per_vert[v.index]
        for g in v.groups:
            if g.weight <= 1e-5:
                continue
            target = vg_map.get(g.group, 'Chest')
            acc[target] = acc.get(target, 0.0) + g.weight

    for vg in list(obj.vertex_groups):
        obj.vertex_groups.remove(vg)

    created: dict[str, bpy.types.VertexGroup] = {}
    for vi, acc in enumerate(per_vert):
        if not acc:
            continue
        # Keep top 4 influences normalized
        top = sorted(acc.items(), key=lambda kv: kv[1], reverse=True)[:4]
        total = sum(w for _, w in top) or 1.0
        for bname, w in top:
            vg = created.get(bname)
            if vg is None:
                vg = obj.vertex_groups.new(name=f'__mh_{bname}')
                created[bname] = vg
            vg.add([vi], w / total, 'REPLACE')


def _decimate_mesh(obj: bpy.types.Object, ratio: float):
    if ratio >= 0.98 or len(obj.data.polygons) < 80:
        return
    mod = obj.modifiers.new('AAADecimate', 'DECIMATE')
    mod.ratio = max(0.08, min(1.0, float(ratio)))
    mod.use_collapse_triangulate = True
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.modifier_apply(modifier=mod.name)


def _paint_realistic_skin_mesh(
    obj: bpy.types.Object,
    bind_coords: list[Vector],
    skin_col,
    sleeve_col=None,
    glove_col=None,
    cyber_right_arm: bool = False,
    bare_left_arm: bool = False,
    sleeve_mat: float = aa.MAT_CLOTH,
):
    """Paint MakeHuman body with facial nuance (cheeks, lips, eye sockets, brow) + optional sleeves/gloves."""
    mesh = obj.data
    aa.ensure_uvs(obj)
    existing = mesh.color_attributes.get('Color')
    if existing is not None:
        mesh.color_attributes.remove(existing)
    layer = mesh.color_attributes.new(name='Color', type='FLOAT_COLOR', domain='CORNER')
    mesh.color_attributes.active_color = layer

    sr, sg, sb = aa.resolve_color(skin_col)
    slr, slg, slb = aa.resolve_color(sleeve_col) if sleeve_col is not None else (sr, sg, sb)
    glr, glg, glb = aa.resolve_color(glove_col) if glove_col is not None else (0.045, 0.044, 0.050)
    loops = mesh.loops
    col_data = layer.data
    verts = mesh.vertices

    for poly in mesh.polygons:
        nz = poly.normal.z if math.isfinite(poly.normal.z) else 0.0
        n_mod = 1.0 + (0.06 * nz if nz >= 0.0 else 0.12 * nz)
        for li in poly.loop_indices:
            vi = loops[li].vertex_index
            b = bind_coords[vi]
            arm_x = abs(b.x)
            is_right_arm = b.x < -0.255 and b.z > 1.30
            is_left_arm = b.x > 0.255 and b.z > 1.30
            in_sleeve = (is_right_arm or is_left_arm) and 0.26 < arm_x < 0.68
            in_glove = (is_right_arm or is_left_arm) and 0.68 <= arm_x < 0.81

            if cyber_right_arm and is_right_arm and arm_x > 0.34:
                # Brushed chrome/gunmetal prosthetic arm
                seam = 0.72 if (abs(arm_x - 0.45) < 0.018 or abs(arm_x - 0.68) < 0.015) else 1.0
                r, g, bl = 0.56 * seam * n_mod, 0.61 * seam * n_mod, 0.67 * seam * n_mod
                a_cls = aa.MAT_METAL
            elif in_sleeve and sleeve_col is not None and not (bare_left_arm and is_left_arm):
                # Fitted long sleeve
                shade = (0.86 + 0.16 * max(0.0, (0.68 - arm_x) / 0.42)) * n_mod
                r, g, bl = slr * shade, slg * shade, slb * shade
                a_cls = sleeve_mat
            elif in_glove and glove_col is not None and not (bare_left_arm and is_left_arm):
                r, g, bl = glr * n_mod, glg * n_mod, glb * n_mod
                a_cls = aa.MAT_LEATHER
            else:
                # Natural human skin with realistic facial blush, lip tone & eye-socket AO
                r, g, bl = sr, sg, sb
                if b.z > 1.56 and b.y < -0.01:
                    cheeks = math.exp(-((abs(b.x) - 0.042) / 0.028) ** 2 - ((b.z - 1.705) / 0.024) ** 2)
                    nose = math.exp(-(b.x / 0.015) ** 2 - ((b.z - 1.712) / 0.022) ** 2)
                    lips = math.exp(-(b.x / 0.023) ** 2 - ((b.z - 1.674) / 0.0085) ** 2) * (1.0 if b.y < -0.06 else 0.0)
                    eye_ao = math.exp(-((abs(b.x) - 0.032) / 0.019) ** 2 - ((b.z - 1.737) / 0.014) ** 2)
                    # Subtle flush & natural lip color
                    r *= 1.0 + 0.08 * cheeks + 0.05 * nose
                    g *= 1.0 - 0.06 * cheeks - 0.04 * nose - 0.28 * lips
                    bl *= 1.0 - 0.08 * cheeks - 0.05 * nose - 0.28 * lips
                    ao = (1.0 - 0.24 * eye_ao) * (0.88 + 0.12 * min(1.0, max(0.0, (b.z - 1.54) / 0.12)))
                    r *= ao; g *= ao; bl *= ao
                else:
                    # Subtle limb & neck ambient shading
                    ao = 0.88 + 0.12 * min(1.0, max(0.0, verts[vi].co.z / 1.7))
                    r *= ao; g *= ao; bl *= ao
                r *= n_mod; g *= n_mod; bl *= n_mod
                a_cls = aa.MAT_SKIN

            col_data[li].color = (aa._clamp01(r), aa._clamp01(g), aa._clamp01(bl), a_cls)

    mat = aa.get_vertex_material(False)
    mesh.materials.clear()
    mesh.materials.append(mat)
    for poly in mesh.polygons:
        poly.material_index = 0
        poly.use_smooth = True
    obj['is_signal'] = False
    mesh.update()


def _paint_realistic_eyes(obj: bpy.types.Object, bind_coords: list[Vector], iris_col=(0.24, 0.16, 0.10)):
    mesh = obj.data
    aa.ensure_uvs(obj)
    existing = mesh.color_attributes.get('Color')
    if existing is not None:
        mesh.color_attributes.remove(existing)
    layer = mesh.color_attributes.new(name='Color', type='FLOAT_COLOR', domain='CORNER')
    mesh.color_attributes.active_color = layer
    ir, ig, ib = aa.resolve_color(iris_col)
    loops = mesh.loops
    for poly in mesh.polygons:
        poly.use_smooth = True
        for li in poly.loop_indices:
            b = bind_coords[loops[li].vertex_index]
            d = math.hypot(abs(b.x) - 0.0315, b.z - 1.7355)
            front = b.y < -0.070
            if front and d < 0.0032:
                col = (0.015, 0.015, 0.020, aa.MAT_GLASS)
            elif front and d < 0.0068:
                col = (ir, ig, ib, aa.MAT_GLASS)
            else:
                col = (0.80, 0.77, 0.75, aa.MAT_GLASS)
            layer.data[li].color = col
    mat = aa.get_vertex_material(False)
    mesh.materials.clear()
    mesh.materials.append(mat)
    for poly in mesh.polygons:
        poly.material_index = 0
    obj['is_signal'] = False
    mesh.update()


def _paint_realistic_suit_mesh(
    obj: bpy.types.Object,
    bind_coords: list[Vector],
    jacket_col,
    pants_col,
    belt_col=(0.06, 0.05, 0.05),
    yoke_col=None,
    jacket_mat: float = aa.MAT_CLOTH,
    pants_mat: float = aa.MAT_CLOTH,
    kneepad_col=(0.05, 0.055, 0.068),
):
    """Paint MakeHuman casualsuit06 with jacket, shoulder yoke, leather belt+metal buckle, trousers & kneepads."""
    mesh = obj.data
    aa.ensure_uvs(obj)
    existing = mesh.color_attributes.get('Color')
    if existing is not None:
        mesh.color_attributes.remove(existing)
    layer = mesh.color_attributes.new(name='Color', type='FLOAT_COLOR', domain='CORNER')
    mesh.color_attributes.active_color = layer

    jr, jg, jb = aa.resolve_color(jacket_col)
    pr, pg, pb = aa.resolve_color(pants_col)
    br, bg, bb = aa.resolve_color(belt_col)
    yr, yg, yb = aa.resolve_color(yoke_col) if yoke_col is not None else (jr * 0.68, jg * 0.68, jb * 0.72)
    kr, kg, kb = aa.resolve_color(kneepad_col)
    loops = mesh.loops
    col_data = layer.data

    for poly in mesh.polygons:
        poly.use_smooth = True
        nz = poly.normal.z if math.isfinite(poly.normal.z) else 0.0
        n_mod = 1.0 + (0.08 * nz if nz >= 0.0 else 0.14 * nz)
        for li in poly.loop_indices:
            b = bind_coords[loops[li].vertex_index]
            if 0.935 <= b.z < 0.985:
                if abs(b.x) < 0.042 and b.y < -0.05:
                    # Brushed metal belt buckle
                    r, g, bl, a_cls = 0.56 * n_mod, 0.60 * n_mod, 0.66 * n_mod, aa.MAT_METAL
                else:
                    r, g, bl, a_cls = br * n_mod, bg * n_mod, bb * n_mod, aa.MAT_LEATHER
            elif b.z >= 0.985:
                # Upper jacket / shirt / tactical vest
                t_up = min(1.0, max(0.0, (b.z - 0.98) / 0.58))
                ao = (0.78 + 0.24 * t_up) * n_mod
                if b.z > 1.38 or abs(b.x) > 0.155:
                    r, g, bl = yr * ao, yg * ao, yb * ao
                    a_cls = jacket_mat
                elif abs(b.x) < 0.015 and b.y < -0.04:
                    # Center metallic zipper
                    r, g, bl, a_cls = 0.42 * ao, 0.46 * ao, 0.52 * ao, aa.MAT_METAL
                else:
                    r, g, bl, a_cls = jr * ao, jg * ao, jb * ao, jacket_mat
            else:
                # Trousers + reinforced knee guards
                t_leg = min(1.0, max(0.0, (b.z - 0.10) / 0.84))
                ao = (0.72 + 0.28 * t_leg) * n_mod
                if 0.41 < b.z < 0.57 and b.y < -0.01:
                    r, g, bl, a_cls = kr * ao, kg * ao, kb * ao, aa.MAT_POLYMER
                else:
                    r, g, bl, a_cls = pr * ao, pg * ao, pb * ao, pants_mat
            col_data[li].color = (aa._clamp01(r), aa._clamp01(g), aa._clamp01(bl), a_cls)

    mat = aa.get_vertex_material(False)
    mesh.materials.clear()
    mesh.materials.append(mat)
    for poly in mesh.polygons:
        poly.material_index = 0
    obj['is_signal'] = False
    mesh.update()


def load_realistic_human(
    height: float = 1.79,
    pose: str = 'standing',
    bulk: tuple[float, float] = (1.0, 1.0),
    skin_col='skin_warm',
    jacket_col='navy_coat',
    yoke_col=None,
    sleeve_col=None,
    pants_col='denim',
    belt_col='leather_dark',
    boot_col='leather_dark',
    hair_col=(0.08, 0.07, 0.075),
    glove_col=None,
    iris_col=(0.24, 0.16, 0.10),
    jacket_mat: float = aa.MAT_CLOTH,
    pants_mat: float = aa.MAT_CLOTH,
    sleeve_mat: float = aa.MAT_CLOTH,
    keep_hair: bool = True,
    keep_head: bool = True,
    keep_arms: bool = True,
    cyber_right_arm: bool = False,
    bare_left_arm: bool = False,
    suit_decimate: float = 0.28,
    body_decimate: float = 0.16,
    boot_decimate: float = 0.22,
    hair_decimate: float = 0.22,
) -> dict:
    """Import, pose, scale, decimate and PBR-paint starter-man.glb; return body parts + exact posed joints."""
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=str(STARTER_MAN_GLB))
    imported = [o for o in bpy.data.objects if o not in before]

    arm = next((o for o in imported if o.type == 'ARMATURE'), None)
    if arm is None:
        raise RuntimeError('starter-man.glb missing armature')

    # Pose the MakeHuman armature before baking into static mesh vertices
    bpy.context.view_layer.objects.active = arm
    for pb in arm.pose.bones:
        pb.rotation_mode = 'QUATERNION'
        pb.rotation_quaternion = (1.0, 0.0, 0.0, 0.0)
        pb.location = (0.0, 0.0, 0.0)

    if pose == 'seated':
        # Sportbike rider racing crouch: forward torso, knees bent gripping tank, hands forward on bars
        _pose_world_rot(arm, 'pelvis', Quaternion((1, 0, 0), -0.34))
        _pose_world_rot(arm, 'spine_01', Quaternion((1, 0, 0), -0.22))
        _pose_world_rot(arm, 'spine_02', Quaternion((1, 0, 0), -0.20))
        _pose_world_rot(arm, 'spine_03', Quaternion((1, 0, 0), -0.18))
        _pose_world_rot(arm, 'neck_01', Quaternion((1, 0, 0), 0.36))
        _pose_world_rot(arm, 'Head', Quaternion((1, 0, 0), 0.34))
        for suf, sgn in (('_l', 1.0), ('_r', -1.0)):
            _pose_world_rot(arm, f'upperarm{suf}', Quaternion((0, 0, 1), -sgn * 1.18) @ Quaternion((0, 1, 0), sgn * 0.92))
            _pose_world_rot(arm, f'lowerarm{suf}', Quaternion((1, 0, 0), 0.36) @ Quaternion((0, 0, 1), sgn * 0.22))
            _pose_world_rot(arm, f'thigh{suf}', Quaternion((1, 0, 0), 1.08) @ Quaternion((0, 1, 0), -sgn * 0.22))
            _pose_world_rot(arm, f'calf{suf}', Quaternion((1, 0, 0), -1.32))
            _pose_world_rot(arm, f'foot{suf}', Quaternion((1, 0, 0), 0.34))
    else:
        # Natural relaxed standing rest posture (+X left, -Y forward, +Z up) — arms hang naturally near hips
        _pose_world_rot(arm, 'upperarm_l', Quaternion((0, 1, 0), 1.40) @ Quaternion((0, 0, 1), -0.08))
        _pose_world_rot(arm, 'lowerarm_l', Quaternion((1, 0, 0), 0.20))
        _pose_world_rot(arm, 'upperarm_r', Quaternion((0, 1, 0), -1.40) @ Quaternion((0, 0, 1), 0.08))
        _pose_world_rot(arm, 'lowerarm_r', Quaternion((1, 0, 0), 0.20))
        _pose_world_rot(arm, 'thigh_l', Quaternion((0, 1, 0), -0.03))
        _pose_world_rot(arm, 'thigh_r', Quaternion((0, 1, 0), 0.03))

    bpy.context.view_layer.update()
    depsgraph = bpy.context.evaluated_depsgraph_get()

    # Collect target meshes, evaluate posed geometry, keep original bind coordinates for semantic shading
    kept_specs = []
    ref_zs: list[float] = []
    mesh_objs = [o for o in imported if o.type == 'MESH']
    for o in mesh_objs:
        nm = o.name.lower()
        if not any(k in nm for k in ('male_generic', 'casualsuit', 'shoes', 'short01', 'high-poly')):
            continue
        if 'short01' in nm and not keep_hair:
            continue
        if 'high-poly' in nm and not keep_head:
            continue

        # Save rest bind-pose vertex coordinates (aligned in Blender space) before evaluating pose
        bind_pts = [(o.matrix_world @ v.co).copy() for v in o.data.vertices]
        eval_obj = o.evaluated_get(depsgraph)
        new_mesh = bpy.data.meshes.new_from_object(eval_obj, preserve_all_data_layers=True, depsgraph=depsgraph)
        new_mesh.transform(o.matrix_world)
        # Measure full posed sole-to-crown Z BEFORE stripping head/hair so keep_head=False keeps exact anatomical scale!
        if 'male_generic' in nm or 'shoes' in nm:
            ref_zs.extend(v.co.z for v in new_mesh.vertices)
        new_obj = bpy.data.objects.new(f'AAA_{o.name}', new_mesh)
        bpy.context.collection.objects.link(new_obj)

        # Copy MakeHuman vertex groups onto the baked posed mesh
        for vg in o.vertex_groups:
            new_obj.vertex_groups.new(name=vg.name)
        for v_src, v_dst in zip(o.data.vertices, new_obj.data.vertices):
            for g in v_src.groups:
                if g.weight > 1e-5:
                    new_obj.vertex_groups[g.group].add([v_dst.index], g.weight, 'REPLACE')

        # Strip hidden interior body faces BEFORE decimating & store bind coords as a temporary attribute
        if 'male_generic' in nm:
            # Store bind coords as vector attribute so decimate preserves semantic locations!
            attr = new_obj.data.attributes.new(name='__bind', type='FLOAT_VECTOR', domain='POINT')
            for i, p in enumerate(bind_pts):
                attr.data[i].vector = p
            _strip_hidden_skin_under_suit(new_obj, keep_arms=keep_arms, keep_head=keep_head)
            _decimate_mesh(new_obj, body_decimate)
        elif 'casualsuit' in nm:
            attr = new_obj.data.attributes.new(name='__bind', type='FLOAT_VECTOR', domain='POINT')
            for i, p in enumerate(bind_pts):
                attr.data[i].vector = p
            _decimate_mesh(new_obj, suit_decimate)
        elif 'high-poly' in nm:
            attr = new_obj.data.attributes.new(name='__bind', type='FLOAT_VECTOR', domain='POINT')
            for i, p in enumerate(bind_pts):
                attr.data[i].vector = p
            _decimate_mesh(new_obj, 0.20)
        elif 'shoes' in nm:
            _decimate_mesh(new_obj, boot_decimate)
        elif 'short01' in nm:
            _decimate_mesh(new_obj, hair_decimate)

        kept_specs.append((nm, new_obj))

    # Measure posed world height from full unstripped body+shoes & exact posed joint coords before removing MakeHuman rig
    all_zs = ref_zs if ref_zs else [v.co.z for _, o in kept_specs for v in o.data.vertices]
    z_min, z_max = min(all_zs), max(all_zs)
    raw_h = max(0.1, z_max - z_min)
    s_uni = float(height) / raw_h
    bx, by = float(bulk[0]), float(bulk[1])

    def map_pt(p: Vector) -> Vector:
        return Vector((p.x * s_uni * bx, p.y * s_uni * by, (p.z - z_min) * s_uni))

    arm_eval = arm.evaluated_get(depsgraph)
    def mh_head(bname: str) -> Vector:
        pb = arm_eval.pose.bones[bname]
        return map_pt(arm_eval.matrix_world @ pb.head)
    def mh_tail(bname: str) -> Vector:
        pb = arm_eval.pose.bones[bname]
        return map_pt(arm_eval.matrix_world @ pb.tail)

    pelvis_p = mh_head('pelvis')
    spine_p = mh_head('spine_01')
    chest_p = mh_head('spine_03')
    neck_p = mh_head('neck_01')
    head_p = mh_head('Head')
    crown_p = Vector((0.0, head_p.y, float(height)))
    joints: dict[str, tuple[tuple[float, float, float], tuple[float, float, float]]] = {
        'Pelvis': (tuple(pelvis_p), tuple(spine_p)),
        'Spine': (tuple(spine_p), tuple(chest_p)),
        'Chest': (tuple(chest_p), tuple(neck_p)),
        'Neck': (tuple(neck_p), tuple(head_p)),
        'Head': (tuple(head_p), tuple(crown_p)),
    }
    for side, suf in (('L', '_l'), ('R', '_r')):
        clav = mh_head(f'clavicle{suf}')
        sh = mh_head(f'upperarm{suf}')
        el = mh_head(f'lowerarm{suf}')
        wr = mh_head(f'hand{suf}')
        hd = mh_tail(f'hand{suf}')
        th = mh_head(f'thigh{suf}')
        kn = mh_head(f'calf{suf}')
        an = mh_head(f'foot{suf}')
        bl = mh_head(f'ball{suf}')
        to = mh_tail(f'ball{suf}')
        joints.update({
            f'Clavicle_{side}': (tuple(clav), tuple(sh)),
            f'UpperArm_{side}': (tuple(sh), tuple(el)),
            f'Forearm_{side}': (tuple(el), tuple(wr)),
            f'Hand_{side}': (tuple(wr), tuple(hd)),
            f'Thigh_{side}': (tuple(th), tuple(kn)),
            f'Shin_{side}': (tuple(kn), tuple(an)),
            f'Foot_{side}': (tuple(an), tuple(bl)),
            f'Toe_{side}': (tuple(bl), tuple(to)),
        })

    # Cleanly remove temporary MakeHuman import objects
    for o in imported:
        bpy.data.objects.remove(o, do_unlink=True)

    # Scale, ground, paint realistic PBR colors + material classes, and remap skin weights
    scale_mat = Matrix.Diagonal((s_uni * bx, s_uni * by, s_uni, 1.0)) @ Matrix.Translation((0.0, 0.0, -z_min))
    parts: list[bpy.types.Object] = []
    for nm, o in kept_specs:
        # Read preserved bind-pose coords before removing temp attribute
        b_attr = o.data.attributes.get('__bind')
        bind_coords = [d.vector.copy() for d in b_attr.data] if b_attr is not None else [v.co.copy() for v in o.data.vertices]
        if b_attr is not None:
            o.data.attributes.remove(b_attr)

        o.data.transform(scale_mat)
        o.data.update()
        _remap_mh_vertex_groups(o)

        if 'male_generic' in nm:
            _paint_realistic_skin_mesh(
                o, bind_coords, skin_col=skin_col, sleeve_col=sleeve_col, glove_col=glove_col,
                cyber_right_arm=cyber_right_arm, bare_left_arm=bare_left_arm, sleeve_mat=sleeve_mat,
            )
        elif 'casualsuit' in nm:
            _paint_realistic_suit_mesh(
                o, bind_coords, jacket_col=jacket_col, pants_col=pants_col,
                belt_col=belt_col, yoke_col=yoke_col, jacket_mat=jacket_mat, pants_mat=pants_mat,
            )
        elif 'high-poly' in nm:
            _paint_realistic_eyes(o, bind_coords, iris_col=iris_col)
        elif 'shoes' in nm:
            for p in o.data.polygons:
                p.use_smooth = True
            aa.paint_mesh(o, color=boot_col, mat_class=aa.MAT_LEATHER, ao_strength=0.22)
        elif 'short01' in nm:
            for p in o.data.polygons:
                p.use_smooth = True
            aa.paint_mesh(o, color=hair_col, mat_class=aa.MAT_HAIR, ao_strength=0.26)
        parts.append(o)

    return {'parts': parts, 'joints': joints, 'height': float(height)}


def add_armored_neck_seal(joints: dict, collar_color=(0.08, 0.09, 0.11)) -> list[bpy.types.Object]:
    """Build a solid ribbed ballistic neck seal & high tactical gorget bridging Chest -> Head with zero gap."""
    nx, ny, nz = joints['Neck'][0]
    hx, hy, hz = joints['Head'][0]
    mid_z = 0.5 * (nz + hz) + 0.015
    span = max(0.14, (hz - nz) + 0.11)
    seal = aa.add_cylinder(
        name='Armored_Neck_Seal', radius=0.072, depth=span, vertices=18,
        location=(0.5 * (nx + hx), 0.5 * (ny + hy) + 0.005, mid_z),
        color=collar_color, bone='Neck',
    )
    aa.paint_mesh(seal, color=collar_color, mat_class=aa.MAT_POLYMER, ao_strength=0.22)
    gorget = aa.add_cylinder(
        name='Tactical_Gorget_Collar', radius=0.112, depth=0.075, vertices=20,
        location=(nx, ny + 0.004, nz + 0.012), bevel=0.016, bevel_segments=2,
        color=collar_color, bone='Chest',
    )
    aa.paint_mesh(gorget, color=collar_color, mat_class=aa.MAT_METAL, ao_strength=0.24)
    return [seal, gorget]


def build_realistic_rig_and_clips(
    body_objs,
    joints: dict[str, tuple[tuple[float, float, float], tuple[float, float, float]]],
    rig_name: str = 'StreetRig',
    height: float = 1.79,
    style: str = 'citizen',
    attack_clip_name: str = 'Punch',
    weapon_hand: str | None = None,
    extra_clips: Sequence[str] | None = None,
) -> bpy.types.Object:
    """Create the 21-bone street armature at exact anatomical MakeHuman joints, preserve native
    MakeHuman skin weights on body vertices, pin custom gear cleanly, and bake all 24 fps clips."""
    meshes = [body_objs] if isinstance(body_objs, bpy.types.Object) else [o for o in body_objs if o is not None and o.type == 'MESH']
    parents = {
        'Spine': 'Pelvis', 'Chest': 'Spine', 'Neck': 'Chest', 'Head': 'Neck',
    }
    for side in ('L', 'R'):
        parents.update({
            f'Clavicle_{side}': 'Chest',
            f'UpperArm_{side}': f'Clavicle_{side}',
            f'Forearm_{side}': f'UpperArm_{side}',
            f'Hand_{side}': f'Forearm_{side}',
            f'Thigh_{side}': 'Pelvis',
            f'Shin_{side}': f'Thigh_{side}',
            f'Foot_{side}': f'Shin_{side}',
            f'Toe_{side}': f'Foot_{side}',
        })

    arm_data = bpy.data.armatures.new(rig_name)
    rig = bpy.data.objects.new(rig_name, arm_data)
    bpy.context.collection.objects.link(rig)
    bpy.ops.object.select_all(action='DESELECT')
    rig.select_set(True)
    bpy.context.view_layer.objects.active = rig
    bpy.ops.object.mode_set(mode='EDIT')
    for b_name, (head_pt, tail_pt) in joints.items():
        eb = arm_data.edit_bones.new(b_name)
        eb.head = head_pt
        tail = Vector(tail_pt)
        if (tail - Vector(head_pt)).length < 0.01:
            tail = Vector(head_pt) + Vector((0, 0, 0.05))
        eb.tail = tail
        eb.use_deform = True
    for b_name, p_name in parents.items():
        arm_data.edit_bones[b_name].parent = arm_data.edit_bones[p_name]
    bpy.ops.object.mode_set(mode='OBJECT')

    joint_vecs = {k: (Vector(a), Vector(b)) for k, (a, b) in joints.items()}
    sh_x = abs(joints['UpperArm_L'][0][0])
    head_z = joints['Head'][0][2]
    pelvis_z = joints['Pelvis'][0][2]
    wrist_z = joints['Hand_L'][0][2]
    wh = (weapon_hand or '').upper()
    w_right = wh in ('R', 'RIGHT', 'HAND_R', 'BOTH')
    w_left = wh in ('L', 'LEFT', 'HAND_L', 'BOTH')

    for mesh_obj in meshes:
        aa.bake_transform(mesh_obj)
        # Collect preserved MakeHuman weights (__mh_*) and rigid gear pins (__pin_*)
        mh_by_idx = {}
        pin_by_idx = {}
        for vg in mesh_obj.vertex_groups:
            if vg.name.startswith('__mh_'):
                bn = vg.name[5:]
                if bn in joints:
                    mh_by_idx[vg.index] = bn
            elif vg.name.startswith('__pin_'):
                bn = vg.name[6:]
                if bn in joints:
                    pin_by_idx[vg.index] = bn

        saved_weights: dict[int, list[tuple[str, float]]] = {}
        for v in mesh_obj.data.vertices:
            pin_target = None
            mh_list = []
            for g in v.groups:
                if g.weight > 0.5 and g.group in pin_by_idx:
                    pin_target = pin_by_idx[g.group]
                    break
                if g.weight > 1e-4 and g.group in mh_by_idx:
                    mh_list.append((mh_by_idx[g.group], g.weight))
            if pin_target is not None:
                saved_weights[v.index] = [(pin_target, 1.0)]
            elif mh_list:
                tot = sum(w for _, w in mh_list) or 1.0
                saved_weights[v.index] = [(bn, w / tot) for bn, w in mh_list]

        for vg in list(mesh_obj.vertex_groups):
            mesh_obj.vertex_groups.remove(vg)

        groups = {k: mesh_obj.vertex_groups.new(name=k) for k in joints}
        for v in mesh_obj.data.vertices:
            vi = v.index
            if vi in saved_weights:
                for bn, w in saved_weights[vi]:
                    groups[bn].add([vi], w, 'REPLACE')
                continue
            # Distance-weighted fallback for unpinned custom accessories
            p = v.co
            side = 'L' if p.x >= 0.0 else 'R'
            if w_right and p.y < -0.14 * height and p.x <= 0.08 * height and 0.25 * height < p.z < 0.98 * height:
                groups['Hand_R'].add([vi], 1.0, 'REPLACE')
                continue
            if w_left and p.y < -0.14 * height and p.x >= -0.08 * height and 0.25 * height < p.z < 0.98 * height:
                groups['Hand_L'].add([vi], 1.0, 'REPLACE')
                continue
            if p.z > head_z - 0.03 * height and abs(p.x) < sh_x * 1.1:
                choices = ['Head', 'Neck']
            elif abs(p.x) > sh_x * 0.85 and p.z > wrist_z - 0.12 * height:
                choices = [f'Clavicle_{side}', f'UpperArm_{side}', f'Forearm_{side}', f'Hand_{side}']
            elif p.z < pelvis_z + 0.03 * height:
                choices = [f'Thigh_{side}', f'Shin_{side}', f'Foot_{side}', f'Toe_{side}', 'Pelvis']
            else:
                choices = ['Pelvis', 'Spine', 'Chest', 'Neck', f'Clavicle_{side}']
            near = sorted(((aa._seg_distance(p, *joint_vecs[k]), k) for k in choices))[:2]
            soft = 0.03 * height
            raw_w = [1.0 / ((d + soft) ** 4) for d, _ in near]
            w_sum = sum(raw_w) or 1.0
            for (_, k), w in zip(near, raw_w):
                groups[k].add([vi], w / w_sum, 'REPLACE')

        for mod in list(mesh_obj.modifiers):
            if mod.type == 'ARMATURE':
                mesh_obj.modifiers.remove(mod)
        arm_mod = mesh_obj.modifiers.new('StreetSkeleton', 'ARMATURE')
        arm_mod.object = rig
        mesh_obj.parent = rig

    # Reuse blender_aa_lib's expressive clip choreography directly on this exact anatomical rig!
    h = float(height)
    st = (style or 'citizen').lower()
    rig.animation_data_create()
    bpy.context.scene.render.fps = aa.FPS
    basis = {pb.name: pb.bone.matrix_local.to_quaternion() for pb in rig.pose.bones}

    def reset_pose():
        for pb in rig.pose.bones:
            pb.rotation_mode = 'QUATERNION'
            pb.rotation_quaternion = (1.0, 0.0, 0.0, 0.0)
            pb.location = (0.0, 0.0, 0.0)
            pb.scale = (1.0, 1.0, 1.0)

    def rot(name: str, x: float = 0.0, y: float = 0.0, z: float = 0.0):
        q = basis[name]
        delta = Quaternion((0.0, 0.0, 1.0), z) @ Quaternion((0.0, 1.0, 0.0), y) @ Quaternion((1.0, 0.0, 0.0), x)
        rig.pose.bones[name].rotation_quaternion = q.inverted() @ delta @ q

    def move_pelvis(dx: float = 0.0, dy: float = 0.0, dz: float = 0.0):
        rig.pose.bones['Pelvis'].location = basis['Pelvis'].inverted() @ Vector((dx, dy, dz))

    def key_all(frame: int):
        for pb in rig.pose.bones:
            pb.keyframe_insert('rotation_quaternion', frame=frame)
            if pb.name == 'Pelvis':
                pb.keyframe_insert('location', frame=frame)

    clip_durations = {
        'Idle': 2.4, 'Walk': 1.0, 'Run': 0.66, 'Punch': 0.65,
        'HitReact': 0.42, 'Death': 1.15, 'Sword_Slash': 0.68, 'Idle_Shoot': 0.85, 'Attack': 0.82,
    }
    wanted_clips = ['Idle', 'Walk', 'Run', 'Punch', 'HitReact', 'Death']
    if attack_clip_name and attack_clip_name not in wanted_clips:
        wanted_clips.append(attack_clip_name)
    for extra in (extra_clips or []):
        if extra and extra not in wanted_clips:
            wanted_clips.append(extra)

    for clip_title in wanted_clips:
        dur = clip_durations.get(clip_title, 0.70)
        action = bpy.data.actions.new(clip_title)
        action.use_fake_user = True
        rig.animation_data.action = action
        frames = max(4, round(dur * aa.FPS))

        for f in range(frames + 1):
            u = f / frames
            phase = u * math.tau
            reset_pose()

            if clip_title == 'Idle':
                breath = math.sin(phase)
                sway = math.sin(phase)
                scan = math.sin(phase + 0.65)
                move_pelvis(dx=0.012 * h * sway, dz=-0.004 * h * abs(sway))
                rot('Pelvis', x=0.01, y=-0.02 * sway, z=0.02 * sway)
                rot('Spine', x=0.014 - 0.012 * breath, y=0.018 * sway, z=-0.012 * sway)
                rot('Chest', x=-0.022 * breath, y=0.008 * sway, z=0.016 * scan)
                rot('Neck', x=0.01 * breath, z=0.03 * scan)
                rot('Head', x=0.018 * math.cos(phase), y=0.012 * sway, z=0.07 * scan)
                for side, sign in (('L', 1.0), ('R', -1.0)):
                    w_knee = max(0.0, -sway * sign)
                    rot(f'Thigh_{side}', x=-0.03 * w_knee, y=-sign * 0.015)
                    rot(f'Shin_{side}', x=0.055 * w_knee)
                    rot(f'Foot_{side}', x=-0.025 * w_knee)
                    rot(f'Clavicle_{side}', y=-sign * (0.015 + 0.012 * breath))
                if st in ('blade', 'katana'):
                    rot('Pelvis', x=0.03, z=-0.16)
                    rot('Chest', x=-0.02 * breath, z=-0.10)
                    rot('Head', z=0.22 + 0.04 * scan)
                    rot('UpperArm_R', x=-0.34 + 0.03 * breath, y=0.14, z=0.12)
                    rot('Forearm_R', x=-0.56 - 0.04 * breath)
                    rot('UpperArm_L', x=-0.22, y=-0.14)
                    rot('Forearm_L', x=-0.42)
                elif st in ('gunner', 'shooter'):
                    rot('Pelvis', x=0.03, z=-0.18)
                    rot('Chest', x=0.04 - 0.02 * breath, z=0.08)
                    rot('UpperArm_R', x=-0.62 + 0.03 * breath, y=0.10, z=0.18)
                    rot('Forearm_R', x=-0.76)
                    rot('UpperArm_L', x=-0.70 + 0.03 * breath, z=-0.36)
                    rot('Forearm_L', x=-0.68)
                elif st in ('punk', 'enforcer', 'heavy'):
                    rot('UpperArm_L', x=-0.18 + 0.03 * sway, y=-0.08)
                    rot('Forearm_L', x=-0.38)
                    rot('UpperArm_R', x=-0.22 - 0.03 * sway, y=0.08)
                    rot('Forearm_R', x=-0.44)
                else:
                    for side, sign in (('L', 1.0), ('R', -1.0)):
                        rot(f'UpperArm_{side}', x=0.03 * sway * sign, y=-sign * 0.04)
                        rot(f'Forearm_{side}', x=-0.12 - 0.02 * breath)

            elif clip_title in ('Walk', 'Run'):
                run = clip_title == 'Run'
                s_base, c_base = math.sin(phase), math.cos(phase)
                bob = (0.034 if run else 0.016) * h * math.cos(2.0 * phase) - (0.022 if run else 0.010) * h
                move_pelvis(dx=(0.011 if run else 0.016) * h * s_base, dz=bob)
                lean = 0.13 if run else 0.025
                hip_twist = (0.10 if run else 0.06) * s_base
                hip_roll = (0.05 if run else 0.038) * s_base
                rot('Pelvis', x=lean * 0.55, y=-hip_roll, z=-hip_twist)
                rot('Spine', x=lean, y=hip_roll * 0.55, z=hip_twist * 0.72)
                rot('Chest', x=lean * 0.72, y=hip_roll * 0.35, z=hip_twist * 1.1)
                rot('Neck', x=-lean * 0.52, z=-hip_twist * 0.42)
                rot('Head', x=-lean * 0.80 + 0.02 * math.cos(2.0 * phase), z=-hip_twist * 0.6)
                for side, sign in (('L', 1.0), ('R', -1.0)):
                    sw, cw = s_base * sign, c_base * sign
                    thigh_amp = 0.72 if run else 0.42
                    lift = max(0.0, cw) * (0.18 if run else 0.08)
                    knee = (0.09 if run else 0.04) + max(0.0, -sw) * (0.76 if run else 0.38) + max(0.0, cw) * (0.48 if run else 0.28)
                    ankle = (0.30 if run else 0.20) * max(0.0, -sw) - (0.18 if run else 0.14) * max(0.0, sw)
                    rot(f'Thigh_{side}', x=-thigh_amp * sw - lift, y=-sign * 0.018)
                    rot(f'Shin_{side}', x=knee)
                    rot(f'Foot_{side}', x=ankle)
                    rot(f'Toe_{side}', x=(0.24 if run else 0.16) * max(0.0, -sw))
                    arm_amp = 0.62 if run else 0.28
                    rot(f'Clavicle_{side}', z=0.05 * sw)
                    rot(f'UpperArm_{side}', x=arm_amp * sw, y=-sign * (0.075 if run else 0.045))
                    rot(f'Forearm_{side}', x=(-0.82 if run else -0.16) - (0.20 if run else 0.12) * max(0.0, -sw))

            elif clip_title == 'Sword_Slash':
                wind = aa._smoothstep(0.0, 0.30, u) * (1.0 - aa._smoothstep(0.30, 0.44, u))
                snap = aa._smoothstep(0.28, 0.50, u) * (1.0 - aa._smoothstep(0.66, 1.0, u))
                move_pelvis(dy=-0.04 * h * snap + 0.02 * h * wind, dz=-0.042 * h * snap)
                rot('Pelvis', x=0.14 * snap - 0.06 * wind, z=0.24 * snap - 0.22 * wind)
                rot('Spine', x=0.18 * snap - 0.10 * wind, z=0.28 * snap - 0.26 * wind)
                rot('Chest', x=0.14 * snap - 0.08 * wind, z=0.34 * snap - 0.30 * wind)
                rot('Head', x=-0.10 * snap, z=-0.38 * snap + 0.32 * wind)
                rot('UpperArm_R', x=-1.95 * wind - 0.65 * snap, y=0.30 * wind - 0.20 * snap, z=-0.22 * wind + 0.58 * snap)
                rot('Forearm_R', x=-0.78 * wind - 0.14 * snap)
                rot('UpperArm_L', x=-1.05 * wind + 0.26 * snap, y=-0.12 - 0.18 * snap, z=-0.38 * wind)
                rot('Forearm_L', x=-0.65 * wind - 0.48 * snap)
                rot('Thigh_L', x=-0.30 * snap - 0.08 * wind)
                rot('Shin_L', x=0.38 * snap + 0.14 * wind)
                rot('Thigh_R', x=0.22 * snap)
                rot('Shin_R', x=0.20 * snap)

            elif clip_title == 'Idle_Shoot':
                brace = aa._smoothstep(0.0, 0.16, u) * (1.0 - aa._smoothstep(0.84, 1.0, u))
                recoil = 0.0
                for shot_u in (0.24, 0.46, 0.68):
                    if u >= shot_u and (u - shot_u) / 0.16 < 1.0:
                        dt_u = (u - shot_u) / 0.16
                        recoil += math.sin(math.pi * dt_u) * (1.0 - 0.35 * dt_u)
                move_pelvis(dy=0.014 * h * recoil, dz=-0.020 * h * brace)
                rot('Pelvis', x=0.04 * brace, z=-0.22 * brace)
                rot('Spine', x=0.08 * brace - 0.06 * recoil, z=0.08 * brace)
                rot('Chest', x=0.06 * brace - 0.08 * recoil, z=0.14 * brace)
                rot('Head', x=0.08 * brace - 0.05 * recoil, z=0.06 * brace)
                rot('UpperArm_R', x=-1.15 * brace - 0.10 * recoil, y=0.08 * brace, z=0.16 * brace)
                rot('Forearm_R', x=-0.58 * brace - 0.12 * recoil)
                rot('UpperArm_L', x=-1.20 * brace - 0.10 * recoil, z=-0.44 * brace)
                rot('Forearm_L', x=-0.42 * brace - 0.10 * recoil)
                rot('Thigh_L', x=-0.16 * brace)
                rot('Shin_L', x=0.24 * brace)
                rot('Thigh_R', x=0.12 * brace)
                rot('Shin_R', x=0.18 * brace)

            elif clip_title == 'HitReact':
                hit = math.sin(math.pi * (u ** 0.58))
                move_pelvis(dy=0.032 * h * hit, dz=-0.026 * h * hit)
                rot('Pelvis', x=-0.10 * hit, z=-0.10 * hit)
                rot('Spine', x=-0.22 * hit, y=0.06 * hit, z=-0.15 * hit)
                rot('Chest', x=-0.20 * hit, y=0.08 * hit, z=-0.18 * hit)
                rot('Head', x=-0.26 * hit, z=0.16 * hit)
                for side, sign in (('L', 1.0), ('R', -1.0)):
                    rot(f'UpperArm_{side}', x=-0.24 * hit, y=-sign * 0.28 * hit)
                    rot(f'Forearm_{side}', x=-0.52 * hit)
                    rot(f'Thigh_{side}', x=-0.14 * hit)
                    rot(f'Shin_{side}', x=0.24 * hit)

            elif clip_title == 'Death':
                buckle = aa._smoothstep(0.0, 0.36, u) * (1.0 - aa._smoothstep(0.45, 0.82, u))
                fall = aa._smoothstep(0.30, 0.86, u)
                drop_z = -(pelvis_z - 0.135 * h) * fall - 0.16 * h * buckle
                move_pelvis(dy=-0.08 * h * fall + 0.03 * h * buckle, dz=drop_z)
                rot('Pelvis', x=-0.14 * buckle + 1.48 * fall, y=0.05 * fall, z=0.05 * fall)
                rot('Spine', x=-0.18 * buckle - 0.02 * fall, z=-0.10 * buckle)
                rot('Chest', x=-0.14 * buckle - 0.03 * fall, z=0.06 * fall)
                rot('Head', x=-0.18 * buckle - 0.10 * fall, z=0.42 * fall)
                for side, sign in (('L', 1.0), ('R', -1.0)):
                    rot(f'Thigh_{side}', x=-0.52 * buckle + 0.06 * fall, y=-sign * 0.08 * fall)
                    rot(f'Shin_{side}', x=1.02 * buckle + 0.02 * fall)
                    rot(f'Foot_{side}', x=-0.22 * buckle + 0.42 * fall)
                    rot(f'UpperArm_{side}', x=-0.34 * buckle - 0.05 * fall, y=-sign * (0.24 * buckle + 0.48 * fall))
                    rot(f'Forearm_{side}', x=-0.60 * buckle - 0.16 * fall, z=-sign * 0.48 * fall)

            else:
                wind = aa._smoothstep(0.0, 0.26, u) * (1.0 - aa._smoothstep(0.26, 0.40, u))
                strike = aa._smoothstep(0.24, 0.46, u) * (1.0 - aa._smoothstep(0.64, 1.0, u))
                move_pelvis(dy=-0.03 * h * strike + 0.014 * h * wind, dz=-0.028 * h * strike)
                rot('Pelvis', x=0.08 * strike - 0.04 * wind, z=0.18 * strike - 0.16 * wind)
                rot('Spine', x=0.12 * strike - 0.06 * wind, z=0.22 * strike - 0.20 * wind)
                rot('Chest', x=0.10 * strike - 0.06 * wind, z=0.28 * strike - 0.24 * wind)
                rot('Head', x=-0.08 * strike, z=-0.34 * strike + 0.26 * wind)
                rot('UpperArm_R', x=0.44 * wind - 1.44 * strike, y=0.18 * wind + 0.05 * strike, z=0.18 * strike)
                rot('Forearm_R', x=-1.10 * wind - 0.16 * strike)
                rot('UpperArm_L', x=-0.44 * wind + 0.22 * strike, y=-0.10)
                rot('Forearm_L', x=-1.05 * wind - 0.88 * strike)
                rot('Thigh_L', x=-0.20 * strike)
                rot('Shin_L', x=0.26 * strike)
                rot('Thigh_R', x=0.15 * strike)
                rot('Shin_R', x=0.16 * strike)

            key_all(f + 1)

        for fc in action.fcurves:
            for kp in fc.keyframe_points:
                kp.interpolation = 'LINEAR'

    rig.animation_data.action = None
    reset_pose()
    bpy.context.scene.frame_set(1)
    return rig
