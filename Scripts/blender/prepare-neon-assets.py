"""Prepare the reviewed Blender art for the street without altering its originals.
Blender merges painted surfaces, keeps wheel pivots, and skins the three people
to a small shared humanoid layout with six in-place clips. No game physics is baked.
Run: blender -b --python tools/prepare-neon-assets.py -- --source /path/to/oil-paint-asset-previews
"""
from pathlib import Path
import argparse
import hashlib
import json
import math
import sys
import bpy
from mathutils import Vector, Matrix, Quaternion

args=argparse.ArgumentParser()
args.add_argument('--source',required=True,type=Path)
options=args.parse_args(sys.argv[sys.argv.index('--')+1:])
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'public/street'
OUT.mkdir(exist_ok=True)
STREET=options.source/'street-vehicles-and-people'
PALETTE={
    '01':(.13,.20,.29),'02':(.035,.045,.065),'03':(.30,.39,.45),'04':(.77,.83,.81),
    '05':(.98,.34,.08),'06':(.025,.85,1),'07':(1,.52,.09),'08':(.025,.10,.14),
    '09':(.055,.42,.43),'10':(.055,.12,.19),'11':(1,.035,.02),'12':(.76,.32,.15),
    '13':(.85,.61,.44),'14':(.52,.31,.20),'15':(.055,.04,.05),'16':(.30,.09,.065),
    '17':(.38,.44,.47),'18':(.025,.53,.60),'19':(.13,.20,.29),'20':(.055,.42,.43),
}


def clear():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def load(path):
    before=set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=str(path))
    objects=[o for o in set(bpy.data.objects)-before if o.type=='MESH']
    for o in objects:
        transform=o.matrix_world.copy()
        o.data=o.data.copy()
        o.data.transform(transform)
        o.parent=None
        o.matrix_world=Matrix.Identity(4)
    for o in list(set(bpy.data.objects)-before):
        if o.type!='MESH': bpy.data.objects.remove(o,do_unlink=True)
    return objects


def signature(obj):
    coordinates=sorted(tuple(round(c,5) for c in v.co) for v in obj.data.vertices)
    return hashlib.sha256(repr(coordinates).encode()).hexdigest()


def colorize(obj):
    mesh=obj.data
    layer=mesh.color_attributes.new(name='Color',type='FLOAT_COLOR',domain='CORNER')
    for face in mesh.polygons:
        material=mesh.materials[face.material_index]
        key=material.name[:2]
        color=PALETTE.get(key,(.3,.35,.4))
        for loop in face.loop_indices: layer.data[loop].color=(*color,1)
    if not mesh.uv_layers: mesh.uv_layers.new(name='UVMap')
    mesh.color_attributes.active_color=layer
    return any(m.name[:2] in ['06','07','11'] for m in mesh.materials)


def vertex_material(signal):
    name='Signal' if signal else 'VertexPaint'
    material=bpy.data.materials.get(name)
    if material: return material
    material=bpy.data.materials.new(name)
    material.use_nodes=True
    material.use_backface_culling=True
    tree=material.node_tree
    vertex=tree.nodes.new('ShaderNodeVertexColor')
    vertex.layer_name='Color'
    shader=tree.nodes.get('Principled BSDF')
    tree.links.new(vertex.outputs['Color'],shader.inputs['Base Color'])
    shader.inputs['Roughness'].default_value=.8
    if signal:
        tree.links.new(vertex.outputs['Color'],shader.inputs['Emission Color'])
        shader.inputs['Emission Strength'].default_value=1
    return material


def combine(objects,name,ratio=1,signal=False,center=None):
    if not objects: return None
    bpy.ops.object.select_all(action='DESELECT')
    for obj in objects: obj.select_set(True)
    bpy.context.view_layer.objects.active=objects[0]
    bpy.ops.object.join()
    obj=bpy.context.object
    obj.name=name
    if ratio<1:
        mod=obj.modifiers.new('Reviewed game mesh','DECIMATE')
        mod.ratio=ratio
        mod.use_collapse_triangulate=True
        bpy.ops.object.modifier_apply(modifier=mod.name)
    obj.data.materials.clear()
    obj.data.materials.append(vertex_material(signal))
    for face in obj.data.polygons: face.material_index=0
    if center:
        obj.data.transform(Matrix.Translation(-Vector(center)))
        obj.location=center
    return obj


def export(name,objects):
    bpy.ops.object.select_all(action='DESELECT')
    for obj in objects:
        obj.select_set(True)
        if obj.type=='MESH':
            assert all(math.isfinite(c) for v in obj.data.vertices for c in v.co)
    bpy.context.scene.render.fps=24
    bpy.ops.export_scene.gltf(filepath=str(OUT/(name+'.glb')),export_format='GLB',
        use_selection=True,export_yup=True,export_animations=True,
        export_cameras=False,export_lights=False,export_all_vertex_colors=True,
        export_copyright='Original art authored for Komorebi; prepared from the reviewed Blender asset set.')


def static(name,path,ratio=.6):
    clear()
    objects=load(path)
    buckets={False:[],True:[]}
    for obj in objects: buckets[colorize(obj)].append(obj)
    out=[combine(parts,'Signal' if signal else 'Body',ratio,signal) for signal,parts in buckets.items() if parts]
    export(name,out)


WHEELS={
    'coupe':[(-.87,-1.28,.39),(.87,-1.28,.39),(-.87,1.28,.39),(.87,1.28,.39)],
    'van':[(-.76,-1.04,.32),(.76,-1.04,.32),(-.76,1.03,.32),(.76,1.03,.32)],
    'motorbike':[(0,-.91,.36),(0,.88,.36)],
    'scooter':[(0,-.80,.285),(0,.75,.285)],
}


def vehicle(name,path):
    clear()
    parts=load(path)
    wheels=[[] for _ in WHEELS[name]]
    body={False:[],True:[]}
    for obj in parts:
        signal=colorize(obj)
        if obj.name.startswith(('Road tyre','Brake rotor','Rim lip','Alloy spoke','Axle cap')):
            center=sum((v.co for v in obj.data.vertices),Vector())/len(obj.data.vertices)
            index=min(range(len(wheels)),key=lambda i:(center-Vector(WHEELS[name][i])).length)
            wheels[index].append(obj)
        else: body[signal].append(obj)
    out=[combine(p,'Signal' if s else 'Body',.68,s) for s,p in body.items() if p]
    for i,parts in enumerate(wheels):
        assert parts,name+' missing a wheel'
        front=i<(2 if len(wheels)==4 else 1)
        label=('F' if front else 'R')+('L' if i%2==0 else 'R') if len(wheels)==4 else ('F' if front else 'R')
        out.append(combine(parts,'Wheel_'+label,.58,center=WHEELS[name][i]))
    export(name,out)


def rider():
    clear()
    original=load(STREET/'models/03-razor-street-bike.glb')
    shapes={signature(o) for o in original}
    for obj in original: bpy.data.objects.remove(obj,do_unlink=True)
    people=load(STREET/'models/05-bike-with-rider.glb')
    pieces=[]
    for obj in people:
        if signature(obj) in shapes: bpy.data.objects.remove(obj,do_unlink=True)
        else:
            colorize(obj)
            pieces.append(obj)
    assert pieces and not any(o.name.startswith('Road tyre') for o in pieces)
    export('rider',[combine(pieces,'SeatedRider',.36)])


def person(name,path):
    clear()
    objects=load(path)
    for obj in objects: colorize(obj)
    body=combine(objects,'StreetPerson',.26)
    # One small rig shared by the authored clips, with rest joints fitted to each preview pose.
    joints={
        'Pelvis':((0,0,.86),(0,0,1.02)), 'Spine':((0,0,1.02),(0,0,1.23)),
        'Chest':((0,0,1.23),(0,0,1.40)), 'Neck':((0,0,1.40),(0,0,1.51)),
        'Head':((0,0,1.51),(0,0,1.83)),
    }
    parents={'Spine':'Pelvis','Chest':'Spine','Neck':'Chest','Head':'Neck'}
    for side,sign in [('L',1),('R',-1)]:
        shoulder=(sign*.215,0,1.355)
        elbow=(sign*.29,-.015,1.13)
        wrist=(sign*.28,-.065,.943)
        if name=='courier' and side=='L': elbow=(.31,-.10,1.11); wrist=(.235,-.285,1.245)
        joints.update({
            'UpperArm_'+side:(shoulder,elbow),'Forearm_'+side:(elbow,wrist),
            'Hand_'+side:(wrist,(wrist[0],wrist[1]-.01,wrist[2]-.105)),
            'Thigh_'+side:((sign*.11,0,.92),(sign*.14,.02,.53)),
            'Shin_'+side:((sign*.14,.02,.53),(sign*.16,.02,.145)),
            'Foot_'+side:((sign*.16,.02,.145),(sign*.16,-.19,.075)),
        })
        parents.update({'UpperArm_'+side:'Chest','Forearm_'+side:'UpperArm_'+side,'Hand_'+side:'Forearm_'+side,
            'Thigh_'+side:'Pelvis','Shin_'+side:'Thigh_'+side,'Foot_'+side:'Shin_'+side})
    data=bpy.data.armatures.new('StreetRig')
    rig=bpy.data.objects.new('StreetRig',data)
    bpy.context.collection.objects.link(rig)
    bpy.context.view_layer.objects.active=rig
    rig.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT')
    for key,(a,b) in joints.items():
        bone=data.edit_bones.new(key); bone.head=a; bone.tail=b
        if key in parents: bone.parent=data.edit_bones[parents[key]]
    bpy.ops.object.mode_set(mode='OBJECT')
    groups={key:body.vertex_groups.new(name=key) for key in joints}
    def distance(p,a,b):
        a,b=Vector(a),Vector(b); delta=b-a
        return (p-(a+delta*max(0,min(1,(p-a).dot(delta)/delta.length_squared)))).length
    for vertex in body.data.vertices:
        p=vertex.co
        side='L' if p.x>=0 else 'R'
        if p.z>1.48:
            choices=['Head']
        elif name=='courier' and p.x>.17 and p.y<-.24 and 1.16<p.z<1.42:
            choices=['Hand_L']
        elif abs(p.x)>.235 and p.z>.77:
            choices=[r+'_'+side for r in ['UpperArm','Forearm','Hand']]
        elif p.z<.96:
            choices=['Pelvis'] if abs(p.x)<.07 and p.z>.83 else [r+'_'+side for r in ['Thigh','Shin','Foot']]
        else:
            choices=['Pelvis','Spine','Chest','Neck']
        near=sorted(((distance(p,*joints[k]),k) for k in choices))[:2]
        weights=[1/(d+.035)**4 for d,k in near]
        total=sum(weights)
        for (d,k),weight in zip(near,weights): groups[k].add([vertex.index],weight/total,'REPLACE')
    modifier=body.modifiers.new('Street skeleton','ARMATURE'); modifier.object=rig
    body.parent=rig
    rig.animation_data_create()
    basis={p.name:p.bone.matrix_local.to_quaternion() for p in rig.pose.bones}
    def rotate(key,x=0,z=0):
        q=basis[key]
        rig.pose.bones[key].rotation_quaternion=q.inverted()@Quaternion((0,0,1),z)@Quaternion((1,0,0),x)@q
    for title,duration in [('Idle',2),('Walk',1),('Run',.65),('Punch',.65),('HitReact',.4),('Death',1.1)]:
        action=bpy.data.actions.new(title)
        action.use_fake_user=True
        rig.animation_data.action=action
        frames=max(2,round(duration*24))
        for f in range(frames+1):
            u=f/frames; phase=u*math.tau
            for p in rig.pose.bones:
                p.rotation_mode='QUATERNION'; p.rotation_quaternion=(1,0,0,0); p.location=(0,0,0)
            if title in ['Walk','Run']:
                run=title=='Run'
                for side,sign in [('L',1),('R',-1)]:
                    swing=math.sin(phase)*sign
                    rotate('Thigh_'+side,x=swing*(.70 if run else .40))
                    rotate('Shin_'+side,x=max(0,-swing)*(1.0 if run else .58))
                    rotate('UpperArm_'+side,x=-swing*(.55 if run else .26))
                    rotate('Forearm_'+side,x=-.35 if run else -.08)
                rotate('Spine',x=.09 if run else .015,z=.025*math.sin(phase))
            elif title=='Punch':
                strike=math.sin(math.pi*u)**2
                rotate('UpperArm_R',x=-1.3*strike); rotate('Forearm_R',x=-.5*(1-strike)); rotate('Chest',z=.16*strike)
            elif title=='HitReact': rotate('Spine',x=-.3*math.sin(math.pi*u))
            elif title=='Death':
                fall=min(1,u*1.4)
                rotate('Pelvis',x=1.48*fall)
                rig.pose.bones['Pelvis'].location=basis['Pelvis'].inverted()@Vector((0,0,-.68*fall))
            else:
                rotate('Spine',x=.009*math.sin(phase)); rotate('Head',z=.018*math.sin(phase))
            for p in rig.pose.bones:
                p.keyframe_insert('rotation_quaternion',frame=f+1); p.keyframe_insert('location',frame=f+1)
        for fc in action.fcurves:
            for key in fc.keyframe_points: key.interpolation='LINEAR'
    rig.animation_data.action=None
    for p in rig.pose.bones: p.rotation_quaternion=(1,0,0,0); p.location=(0,0,0)
    bpy.context.scene.frame_set(1)
    export(name,[body,rig])


for name,source in [('terminal','01-contract-terminal'),('supply-crate','02-supply-crate'),
    ('power-core','03-power-core'),('beacon','04-uplink-beacon'),('barricade','05-road-barricade')]:
    static(name,options.source/'models'/(source+'.glb'),.52)
for name,source in [('coupe','01-kestrel-coupe'),('van','02-parcel-ev'),
    ('motorbike','03-razor-street-bike'),('scooter','04-courier-scooter')]:
    vehicle(name,STREET/'models'/(source+'.glb'))
rider()
for name,source in [('resident','06-market-resident'),('mechanic','07-street-mechanic'),('courier','08-hooded-courier')]:
    person(name,STREET/'models'/(source+'.glb'))
print('NEON_ASSETS_PREPARED',flush=True)
