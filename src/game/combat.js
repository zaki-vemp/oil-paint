// Authored sword poses shared by the traveler and the mirror rivals. A two-bone arm solve keeps the grip
// in the hand, while the shoulders and trunk carry the windup and follow-through. Trails follow the real
// blade in world space; impact rings and sparks are bounded, short-lived geometry, never post-processing.
import * as THREE from 'three';
import { paint } from '../figure/figure.js';

export const ATTACKS = {
  cut: { duration: 0.54, hit: [0.32, 0.69], reach: 2.6 },
  backhand: { duration: 0.50, hit: [0.30, 0.70], reach: 2.6 },
  thrust: { duration: 0.66, hit: [0.42, 0.72], reach: 3.1 },
  heavy: { duration: 0.88, hit: [0.42, 0.73], reach: 3.4 },
  spin: { duration: 0.62, hit: [0.16, 0.84], reach: 3.3 }, // the whirl: hunt.js turns the whole body once round
};
// Each frame is [grip x/y/z, blade x/y/z, trunk turn]. The grip is in character metres.
const POSES = {
  cut: [[0.34, 0.98, 0.12, 0.1, -0.9, 0.2, 0], [0.52, 1.5, -0.16, 0.5, 0.8, -0.5, -0.65], [-0.25, 1.13, 0.58, -0.8, 0.15, 0.6, 0.6], [-0.3, 0.85, 0.22, -0.5, -0.6, 0.5, 0.35], [0.34, 0.98, 0.12, 0.1, -0.9, 0.2, 0]],
  backhand: [[0.34, 0.98, 0.12, 0.1, -0.9, 0.2, 0], [-0.3, 1.1, 0.24, -0.8, 0.2, -0.3, 0.5], [0.47, 1.35, 0.53, 0.85, 0.15, 0.4, -0.55], [0.51, 1.4, 0.0, 0.5, 0.65, 0.1, -0.3], [0.34, 0.98, 0.12, 0.1, -0.9, 0.2, 0]],
  thrust: [[0.34, 0.98, 0.12, 0.1, -0.9, 0.2, 0], [0.3, 1.22, -0.1, 0, 0.1, 1, -0.45], [0.1, 1.18, 0.68, 0, 0.05, 1, 0.35], [0.13, 1.15, 0.58, 0, 0.05, 1, 0.2], [0.34, 0.98, 0.12, 0.1, -0.9, 0.2, 0]],
  heavy: [[0.34, 0.98, 0.12, 0.1, -0.9, 0.2, 0], [0.2, 1.76, -0.08, 0, 0.5, -0.9, -0.15], [0.1, 1.1, 0.68, 0, -0.8, 0.6, 0.2], [0.15, 0.85, 0.5, 0, -0.9, 0.4, 0.3], [0.34, 0.98, 0.12, 0.1, -0.9, 0.2, 0]],
  spin: [[0.34, 0.98, 0.12, 0.1, -0.9, 0.2, 0], [0.62, 1.15, 0.22, 1, 0.05, 0.25, 0.3], [0.64, 1.12, 0.26, 1, 0.0, 0.3, 0.2], [0.6, 1.1, 0.3, 0.9, -0.1, 0.4, 0.1], [0.34, 0.98, 0.12, 0.1, -0.9, 0.2, 0]],
};
export function sampleAttack(type, u) {
  const keys = POSES[type] ?? POSES.cut, times = [0, 0.28, 0.53, 0.76, 1];
  u = Math.max(0, Math.min(1, u));
  let i = 0; while (i < 3 && u > times[i + 1]) i++;
  const k = (u - times[i]) / (times[i + 1] - times[i]), t = k * k * (3 - 2 * k);
  return keys[i].map((v, n) => v + (keys[i + 1][n] - v) * t);
}

export function createCombatPose(man, shared, scene, { rival = false, lantern = false } = {}) {
  const sword = new THREE.Group();
  sword.name = rival ? 'Rival katana' : 'Traveler katana';
  const steel = paint(shared, [[0.9, 0.98, 1], [0.53, 0.75, 0.8], [0.18, 0.3, 0.38]]);
  const gold = paint(shared, [[0.98, 0.77, 0.35], [0.65, 0.43, 0.14], [0.28, 0.19, 0.12]]);
  const dark = paint(shared, [[0.19, 0.27, 0.31], [0.09, 0.14, 0.18], [0.04, 0.07, 0.1]]);
  const blade = new THREE.Shape();
  blade.moveTo(-0.025, 0.1); blade.lineTo(0.025, 0.1); blade.lineTo(0.055, 0.98); blade.lineTo(0.025, 1.17); blade.lineTo(-0.02, 1.02); blade.closePath();
  const add = (g, m, y = 0) => { const o = new THREE.Mesh(g, m); o.position.y = y; o.layers.enable(1); sword.add(o); return o; };
  add(new THREE.ExtrudeGeometry(blade, { depth: 0.012, bevelEnabled: true, bevelSize: 0.003, bevelThickness: 0.003, bevelSegments: 1, steps: 1 }), steel);
  const guard = add(new THREE.TorusGeometry(0.065, 0.014, 5, 12), gold, 0.08); guard.rotation.x = Math.PI / 2;
  add(new THREE.CylinderGeometry(0.026, 0.028, 0.21, 8), dark, -0.035);
  for (let i = 0; i < 6; i++) { const wrap = add(new THREE.TorusGeometry(0.028, 0.005, 4, 8), gold, -0.12 + i * 0.031); wrap.rotation.x = Math.PI / 2; }
  const visualHand = man.appearance?.bindings.find(b => b.from === man.bones.hand_l)?.to ?? man.bones.hand_l;
  visualHand.add(sword);
  const geometry = new THREE.BufferGeometry(), positions = new Float32Array(14 * 18), colors = new Float32Array(positions.length);
  geometry.setAttribute('position', new THREE.BufferAttribute(positions, 3).setUsage(THREE.DynamicDrawUsage));
  geometry.setAttribute('color', new THREE.BufferAttribute(colors, 3));
  geometry.setDrawRange(0, 0);
  const trailMaterial = new THREE.MeshBasicMaterial({ vertexColors: true, side: THREE.DoubleSide, transparent: true, opacity: 0.65, depthWrite: false });
  const trail = new THREE.Mesh(geometry, trailMaterial); trail.frustumCulled = false; scene.add(trail);
  trail.name = 'Blade trail';
  const points = [], base = new THREE.Vector3(), tip = new THREE.Vector3(), up = new THREE.Vector3(0, 1, 0);
  const q = new THREE.Quaternion(), parentQ = new THREE.Quaternion(), before = new THREE.Vector3(), after = new THREE.Vector3();
  const target = new THREE.Vector3(), shoulder = new THREE.Vector3(), elbow = new THREE.Vector3(), wrist = new THREE.Vector3();
  const dir = new THREE.Vector3(), bend = new THREE.Vector3(), wantedElbow = new THREE.Vector3();
  const turnQ = new THREE.Quaternion(), yAxis = new THREE.Vector3(0, 1, 0), bladeDir = new THREE.Vector3();
  let active = false, attackType = 'cut', trailFade = 0, lastDt = 1 / 60;
  const swordRotation = new THREE.Quaternion();
  function aim(bone, child, goal) {
    bone.getWorldPosition(before); child.getWorldPosition(after).sub(before).normalize();
    dir.copy(goal).sub(before).normalize();
    q.setFromUnitVectors(after, dir);
    bone.getWorldQuaternion(parentQ); q.multiply(parentQ);
    bone.parent.getWorldQuaternion(parentQ).invert();
    bone.quaternion.copy(parentQ.multiply(q)); bone.updateMatrixWorld(true);
  }
  function arm(side, local) {
    const upper = man.bones[`upperarm_${side}`], lower = man.bones[`lowerarm_${side}`], hand = man.bones[`hand_${side}`];
    upper.getWorldPosition(shoulder); lower.getWorldPosition(elbow); hand.getWorldPosition(wrist);
    const a = shoulder.distanceTo(elbow), b = elbow.distanceTo(wrist);
    target.copy(local).applyMatrix4(man.object.matrixWorld);
    dir.subVectors(target, shoulder); const d = THREE.MathUtils.clamp(dir.length(), Math.abs(a - b) + 0.01, a + b - 0.008); dir.normalize();
    target.copy(shoulder).addScaledVector(dir, d);
    bend.set(side === 'l' ? 1 : -1, -0.4, -0.7).applyQuaternion(man.object.quaternion);
    bend.addScaledVector(dir, -bend.dot(dir)).normalize();
    const x = (a * a - b * b + d * d) / (2 * d), h = Math.sqrt(Math.max(0, a * a - x * x));
    wantedElbow.copy(shoulder).addScaledVector(dir, x).addScaledVector(bend, h);
    aim(upper, lower, wantedElbow); aim(lower, hand, target);
  }
  function finishWeapon() {
    visualHand.getWorldQuaternion(parentQ).invert();
    sword.quaternion.copy(parentQ.multiply(q.copy(man.object.quaternion).multiply(swordRotation)));
    sword.updateMatrixWorld(true);
    if (active) {
      base.set(0, 0.73, 0).applyMatrix4(sword.matrixWorld); tip.set(0.025, 1.14, 0).applyMatrix4(sword.matrixWorld);
      points.unshift([base.clone(), tip.clone()]); if (points.length > (attackType === 'spin' ? 13 : 10)) points.pop(); trailFade = 0.14;
    } else trailFade = Math.max(0, trailFade - lastDt);
    if (trailFade <= 0) points.length = 0;
    const color = new THREE.Color(attackType === 'heavy' ? 0xffd18a : attackType === 'spin' ? 0xc9a8ff : rival ? 0xf49f88 : 0x92edeb);
    let out = 0;
    for (let i = 0; i + 1 < points.length; i++) for (const [j, k] of [[i, 0], [i, 1], [i + 1, 0], [i + 1, 0], [i, 1], [i + 1, 1]]) {
      positions.set(points[j][k].toArray(), out); const c = color.clone().multiplyScalar(0.4 + 0.6 * (1 - j / points.length)); colors.set(c.toArray(), out); out += 3;
    }
    geometry.attributes.position.needsUpdate = geometry.attributes.color.needsUpdate = true;
    geometry.setDrawRange(0, out / 3); trailMaterial.opacity = trailFade > 0 ? 0.6 * trailFade / 0.14 : 0;
  }
  if (man.appearance) man.appearance.afterUpdate = finishWeapon;
  return { sword, trail, apply(dt, { u = -1, type = 'cut', hurt = 0, cast = 0 } = {}) {
    lastDt = dt; attackType = type;
    const frame = sampleAttack(type, Math.max(0, u));
    const weight = u >= 0 ? Math.sin(Math.PI * Math.min(1, u) * 0.9) : 0;
    for (const [name, amount] of [['spine_01', 0.25], ['spine_02', 0.35], ['spine_03', 0.4]]) {
      turnQ.setFromAxisAngle(yAxis, frame[6] * amount * (u >= 0 ? 1 : 0)); man.bones[name].quaternion.multiply(turnQ);
    }
    man.bones.spine_02.rotation.x += (type === 'heavy' ? 0.18 * weight : 0.04 * weight) - hurt * 0.22;
    man.object.updateMatrixWorld(true);
    arm('l', target.set(...frame.slice(0, 3)));
    if (!lantern && (u >= 0 || cast > 0)) arm('r', target.set(-0.4, 1.2 + cast * 0.22, cast > 0 ? 0.55 : -0.12));
    for (const [name, pose] of Object.entries(man.hands)) if (name.endsWith('_l') && man.bones[name]) man.bones[name].quaternion.fromArray(pose.fist);
    bladeDir.set(...frame.slice(3, 6)).normalize(); swordRotation.setFromUnitVectors(up, bladeDir);
    const hit = ATTACKS[type]?.hit ?? ATTACKS.cut.hit;
    active = u >= hit[0] && u <= hit[1];
    if (!man.appearance) finishWeapon();
  } };
}

export function demoCombat() {
  for (const [type, a] of Object.entries(ATTACKS)) {
    if (!(a.hit[0] < a.hit[1] && a.hit[1] < 1 && a.duration > 0.4)) throw new Error('attack timing ' + type);
    for (let i = 0; i <= 100; i++) if (sampleAttack(type, i / 100).some(v => !Number.isFinite(v))) throw new Error('pose ' + type);
  }
}
if (globalThis.process?.argv?.[1]?.endsWith('combat.js')) demoCombat();
