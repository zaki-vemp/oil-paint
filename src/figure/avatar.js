// Seed-san's detailed anime mesh follows the existing locomotion skeleton through rest-pose-corrected
// world rotations. The original rig still owns foot IK and combat; the model keeps its authored textures.
// Rivals reuse the same mesh and animation library, with a contrasting outfit and no extra render passes.
import * as THREE from 'three';
import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';
import { clone } from 'three/addons/utils/SkeletonUtils.js';
import { paint } from './figure.js';

let asset;
const PALETTE = [[1, 1, 1], [0.8, 0.83, 0.86], [0.45, 0.51, 0.58]];
const BODY = { hips: 'pelvis', spine: 'spine_01', chest: 'spine_03', neck: 'neck_01', head: 'Head' };
for (const [side, suffix] of [['left', 'l'], ['right', 'r']]) {
  for (const [part, source] of Object.entries({ Shoulder: 'clavicle', UpperArm: 'upperarm', LowerArm: 'lowerarm', Hand: 'hand', UpperLeg: 'thigh', LowerLeg: 'calf', Foot: 'foot', Toes: 'ball' })) BODY[side + part] = `${source}_${suffix}`;
  for (const finger of ['Index', 'Middle', 'Ring', 'Little', 'Thumb']) for (const [i, segment] of ['Proximal', 'Intermediate', 'Distal'].entries()) {
    const name = finger === 'Little' ? 'pinky' : finger.toLowerCase();
    BODY[side + finger + segment] = `${name}_0${i + 1}_${suffix}`;
  }
  BODY[side + 'ThumbMetacarpal'] = `thumb_01_${suffix}`;
  BODY[side + 'ThumbProximal'] = `thumb_02_${suffix}`;
}

export async function attachAvatar(man, shared, { rival = false } = {}) {
  asset ??= new GLTFLoader().loadAsync('./characters/seed-san.vrm');
  const gltf = await asset, object = clone(gltf.scene);
  object.name = rival ? 'Anime rival' : 'Anime traveler';
  // The mechanical backpack is a separate mesh. The human character's outfit and face remain intact.
  object.traverse(o => { if (/^robo_arm/.test(o.name)) o.visible = false; });
  object.updateMatrixWorld(true);
  const box = new THREE.Box3().setFromObject(object), height = box.max.y - box.min.y;
  const scale = 1.8 / height;
  object.scale.setScalar(scale);
  object.position.y = -box.min.y * scale;
  const materials = new Map();
  object.traverse(o => {
    if (!o.isMesh) return;
    const original = o.material, name = original.name ?? '';
    if (/backpack|armgear|anim_logo|robo_face|glass|green_emit/.test(name)) { o.visible = false; return; }
    const key = original.uuid;
    if (!materials.has(key)) {
      const clothes = /huku|wear_metal/.test(name);
      const tint = clothes ? rival ? [1.0, 0.32, 0.22] : [0.38, 0.77, 0.90] : [1.15, 1.15, 1.15];
      const mat = paint(shared, PALETTE, { skinned: !!o.isSkinnedMesh, map: original.map, tint });
      mat.side = original.side;
      mat.transparent = original.transparent;
      mat.depthWrite = original.depthWrite;
      mat.alphaTest = original.alphaTest;
      mat.uniforms.uCutoff = { value: original.alphaTest || (original.transparent ? 0.05 : 0) };
      materials.set(key, mat);
    }
    o.material = materials.get(key); o.frustumCulled = false; o.layers.enable(1);
  });
  for (const mesh of man.meshes) mesh.visible = false;
  man.object.updateMatrixWorld(true);
  object.updateMatrixWorld(true);
  const humanoid = gltf.parser.json.extensions.VRMC_vrm.humanoid.humanBones;
  const byName = new Map(); object.traverse(o => { if (o.isBone) byName.set(o.name, o); });
  const bindings = [];
  for (const [part, source] of Object.entries(BODY)) {
    const index = humanoid[part]?.node, node = gltf.parser.json.nodes[index];
    const from = man.bones[source], to = node && byName.get(THREE.PropertyBinding.sanitizeNodeName(node.name));
    if (!from || !to) continue;
    bindings.push({ from, to, inverse: from.getWorldQuaternion(new THREE.Quaternion()).invert(), rest: to.getWorldQuaternion(new THREE.Quaternion()) });
  }
  const depth = bone => { let n = 0; for (let p = bone.parent; p; p = p.parent) n++; return n; };
  bindings.sort((a, b) => depth(a.to) - depth(b.to));
  const bootMat = paint(shared, [[0.22, 0.29, 0.34], [0.1, 0.15, 0.2], [0.04, 0.07, 0.1]]);
  const scarfMat = paint(shared, rival ? [[0.98, 0.75, 0.42], [0.7, 0.4, 0.2], [0.3, 0.17, 0.15]] : [[1, 0.48, 0.34], [0.8, 0.25, 0.19], [0.4, 0.13, 0.15]]);
  function prop(bone, geometry, material, offset, size) {
    const mesh = new THREE.Mesh(geometry, material);
    object.add(mesh); mesh.position.copy(object.worldToLocal(bone.getWorldPosition(new THREE.Vector3()))).add(new THREE.Vector3(...offset));
    if (size) mesh.scale.set(...size);
    mesh.layers.enable(1); object.updateMatrixWorld(true); bone.attach(mesh); return mesh;
  }
  for (const foot of bindings.filter(b => b.from.name === 'foot_l' || b.from.name === 'foot_r')) {
    const toe = bindings.find(b => b.from.name === foot.from.name.replace('foot_', 'ball_')).to;
    const a = object.worldToLocal(foot.to.getWorldPosition(new THREE.Vector3()));
    const delta = object.worldToLocal(toe.getWorldPosition(new THREE.Vector3())).sub(a);
    prop(foot.to, new THREE.SphereGeometry(1, 10, 6), bootMat, [delta.x * 0.5, delta.y - 0.015, delta.z * 0.5], [0.08, 0.06, Math.abs(delta.z) * 0.65 + 0.065]);
    prop(foot.to, new THREE.CylinderGeometry(0.055, 0.067, 0.22, 8), bootMat, [0, -0.015, 0]);
  }
  const neck = bindings.find(b => b.from.name === 'neck_01').to;
  const scarf = prop(neck, new THREE.TorusGeometry(0.1, 0.029, 6, 16), scarfMat, [0, -0.02, 0]); scarf.rotation.x += Math.PI / 2;
  const hips = bindings.find(b => b.from === man.bones.pelvis);
  const sourceHip = hips.from.getWorldPosition(new THREE.Vector3());
  const targetHip = hips.to.getWorldPosition(new THREE.Vector3());
  const q = new THREE.Quaternion(), parent = new THREE.Quaternion(), p = new THREE.Vector3(), inv = new THREE.Matrix4();
  man.object.add(object);
  function update() {
    man.object.updateMatrixWorld(true);
    // Parent-before-child order is essential: every child removes its already animated parent rotation.
    for (const b of bindings) {
      b.from.getWorldQuaternion(q).multiply(b.inverse).multiply(b.rest);
      b.to.parent.getWorldQuaternion(parent).invert();
      b.to.quaternion.copy(parent.multiply(q));
      b.to.updateMatrixWorld(true);
    }
    inv.copy(man.object.matrixWorld).invert();
    p.copy(man.bones.pelvis.getWorldPosition(p)).applyMatrix4(inv).sub(sourceHip).add(targetHip);
    p.applyMatrix4(man.object.matrixWorld);
    hips.to.position.copy(hips.to.parent.worldToLocal(p));
    object.updateMatrixWorld(true);
    appearance.afterUpdate?.();
  }
  if (bindings.length < 40) throw new Error('Anime avatar is missing required humanoid bindings');
  const appearance = { object, bindings, update, materials };
  return appearance;
}

export async function createRivalAppearance(source, shared) {
  const object = clone(source.template), bones = {}, meshes = [];
  object.traverse(o => { if (o.isBone) bones[o.name] = o; if (o.isSkinnedMesh) meshes.push(o); });
  const man = { object, bones, meshes, clips: source.clips, raw: source.raw, hands: source.hands };
  const avatar = await attachAvatar(man, shared, { rival: true });
  man.appearance = avatar;
  const mixer = new THREE.AnimationMixer(object), actions = {};
  for (const name of ['Idle', 'Walk', 'Run', 'Jump_Land']) actions[name] = mixer.clipAction(source.clips[name]);
  let current = actions.Idle; current.play();
  return { man, avatar, update(dt, speed, defeated = false) {
    const next = actions[defeated ? 'Jump_Land' : speed > 2.5 ? 'Run' : speed > 0.15 ? 'Walk' : 'Idle'];
    if (next !== current) { current.fadeOut(0.15); next.reset().fadeIn(0.15).play(); current = next; }
    current.timeScale = defeated ? 0 : speed > 0.15 ? THREE.MathUtils.clamp(speed / (speed > 2.5 ? 5.3 : 1.45), 0.7, 1.8) : 1;
    if (defeated) current.time = current.getClip().duration * 0.22;
    mixer.update(dt);
  } };
}
