// Photorealistic AAA human protagonist (Kael) & mirror boss (Ren) built directly on the
// 34,026-triangle anatomical MakeHuman rig (starter-man.glb) with full PBR skin/SSS, procedural
// wet-cornea iris eyes, realistic layered hair, fitted long-sleeve tactical techwear jacket,
// leather tactical gloves, cargo trousers, scuffed leather combat boots, and flush tactical bracer.
import * as THREE from 'three';
import { clone } from 'three/addons/utils/SkeletonUtils.js';
import { paint } from './figure.js';

export async function attachAvatar(man, shared, { rival = false } = {}) {
  const materials = new Map();
  const accent = rival ? [1.0, 0.18, 0.26] : [0.05, 0.88, 1.0];
  const iris = rival ? [0.85, 0.18, 0.22] : [0.46, 0.33, 0.18];
  const jacketCol = rival
    ? [[0.34, 0.12, 0.14], [0.21, 0.08, 0.10], [0.10, 0.04, 0.05]]
    : [[0.30, 0.36, 0.45], [0.20, 0.25, 0.33], [0.10, 0.13, 0.19]];
  const sleeveCol = rival
    ? [[0.20, 0.09, 0.11], [0.13, 0.06, 0.07], [0.06, 0.03, 0.04]]
    : [[0.18, 0.22, 0.28], [0.12, 0.15, 0.20], [0.06, 0.08, 0.11]];
  const pantsCol = rival
    ? [[0.14, 0.12, 0.14], [0.09, 0.08, 0.10], [0.05, 0.04, 0.06]]
    : [[0.16, 0.18, 0.21], [0.10, 0.12, 0.15], [0.05, 0.06, 0.08]];
  const skinCol = rival
    ? [[0.82, 0.67, 0.59], [0.66, 0.51, 0.45], [0.40, 0.30, 0.27]]
    : [[0.86, 0.67, 0.55], [0.70, 0.51, 0.41], [0.44, 0.31, 0.26]];
  const hairCol = rival
    ? [[0.62, 0.65, 0.72], [0.38, 0.40, 0.48], [0.20, 0.22, 0.28]]
    : [[0.12, 0.10, 0.095], [0.07, 0.06, 0.06], [0.035, 0.03, 0.035]];
  const bootCol = [[0.14, 0.13, 0.14], [0.09, 0.08, 0.09], [0.05, 0.04, 0.05]];

  const matSuit = paint(shared, jacketCol, { skinned: true, lower: pantsCol, split: 0.965, surfaceKind: 4, accent });
  const matSkin = paint(shared, skinCol, { skinned: true, lower: sleeveCol, surfaceKind: 1, roughness: 0.46, sss: 0.68, accent });
  const matEyes = paint(shared, [[0.9, 0.9, 0.9], [0.7, 0.7, 0.7], [0.5, 0.5, 0.5]], { skinned: true, surfaceKind: 2, accent: iris });
  const matHair = paint(shared, hairCol, { skinned: true, surfaceKind: 3, roughness: 0.48 });
  matHair.side = THREE.DoubleSide;
  const matBrows = paint(shared, hairCol, { skinned: true, surfaceKind: 3, roughness: 0.58 });
  matBrows.side = THREE.DoubleSide;
  const matBoots = paint(shared, bootCol, { skinned: true, surfaceKind: 5, roughness: 0.48 });

  materials.set('suit', matSuit);
  materials.set('skin', matSkin);
  materials.set('eyes', matEyes);
  materials.set('hair', matHair);
  materials.set('brows', matBrows);
  materials.set('boots', matBoots);

  for (const mesh of man.meshes) {
    const name = `${mesh.name} ${mesh.material?.name ?? ''}`;
    mesh.visible = true;
    if (/eyelash|eyebrow/.test(name)) mesh.material = matBrows;
    else if (/casualsuit/.test(name)) mesh.material = matSuit;
    else if (/shoes/.test(name)) mesh.material = matBoots;
    else if (/short01/.test(name)) mesh.material = matHair;
    else if (/high-poly/.test(name)) mesh.material = matEyes;
    else mesh.material = matSkin;
    mesh.layers.enable(1);
    mesh.frustumCulled = false;
  }

  // Sleek, anatomically flush tactical forearm bracer & matte leather belt pouch (zero floating primitives!)
  const carbonMat = paint(shared, [[0.10, 0.115, 0.14], [0.06, 0.07, 0.09], [0.03, 0.04, 0.05]], {
    roughness: 0.42, metalness: 0.38, clearcoat: 0.10,
  });
  const pouchMat = paint(shared, [[0.065, 0.062, 0.072], [0.045, 0.042, 0.05], [0.025, 0.022, 0.03]], {
    roughness: 0.62, metalness: 0.06,
  });
  const sigMat = paint(shared, [accent, accent, accent], { surfaceKind: 6 });
  materials.set('carbon', carbonMat);
  materials.set('pouch', pouchMat);
  materials.set('signal', sigMat);

  man.object.updateMatrixWorld(true);
  const attachTo = (bone, geo, mat, offset, rot = [0, 0, 0], scale = [1, 1, 1]) => {
    if (!bone) return null;
    const mesh = new THREE.Mesh(geo, mat);
    mesh.position.set(...offset);
    mesh.rotation.set(...rot);
    mesh.scale.set(...scale);
    mesh.layers.enable(1);
    bone.add(mesh);
    return mesh;
  };

  // Fitted low-profile cybernetic forearm bracer on sword wrist (lowerarm_l)
  const lowerL = man.bones.lowerarm_l;
  if (lowerL) {
    attachTo(lowerL, new THREE.CylinderGeometry(0.044, 0.052, 0.14, 18), carbonMat, [0, 0.14, 0]);
    attachTo(lowerL, new THREE.BoxGeometry(0.010, 0.095, 0.008), sigMat, [0.046, 0.14, 0.010]);
  }
  // Subtle matte leather tactical pouch on right hip belt
  const pelvis = man.bones.pelvis;
  if (pelvis) {
    attachTo(pelvis, new THREE.BoxGeometry(0.045, 0.095, 0.082), pouchMat, [0.155, -0.02, -0.02], [0, 0, -0.08]);
  }

  const bindings = Object.values(man.bones).map((b) => ({ from: b, to: b }));
  const appearance = {
    object: man.object,
    bindings,
    materials,
    update() {
      appearance.afterUpdate?.();
    },
  };
  return appearance;
}

export async function createRivalAppearance(source, shared) {
  const object = clone(source.template), bones = {}, meshes = [];
  object.traverse((o) => {
    if (o.isBone) bones[o.name] = o;
    if (o.isSkinnedMesh) meshes.push(o);
  });
  const man = { object, bones, meshes, clips: source.clips, raw: source.raw, hands: source.hands };
  const avatar = await attachAvatar(man, shared, { rival: true });
  man.appearance = avatar;
  const mixer = new THREE.AnimationMixer(object), actions = {};
  for (const name of ['Idle', 'Walk', 'Run', 'Jump_Land']) actions[name] = mixer.clipAction(source.clips[name]);
  let current = actions.Idle;
  current.play();
  return {
    man,
    avatar,
    update(dt, speed, defeated = false) {
      const next = actions[defeated ? 'Jump_Land' : speed > 2.5 ? 'Run' : speed > 0.15 ? 'Walk' : 'Idle'];
      if (next !== current) {
        current.fadeOut(0.15);
        next.reset().fadeIn(0.15).play();
        current = next;
      }
      current.timeScale = defeated ? 0 : speed > 0.15 ? THREE.MathUtils.clamp(speed / (speed > 2.5 ? 5.3 : 1.45), 0.7, 1.8) : 1;
      if (defeated) current.time = current.getClip().duration * 0.22;
      mixer.update(dt);
    },
  };
}
