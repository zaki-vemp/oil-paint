// Authored sword poses shared by the traveler and the mirror rivals. A two-bone arm solve keeps the grip
// in the hand, while the shoulders and trunk carry the windup and follow-through. Hero blades and Kael's belt
// lantern stream from public/street over an instant painted fallback; dual-layer crescent trails follow the blade.
// Guard and quickstep poses layer over the locomotion so the feet still follow the swept capsule.
import * as THREE from 'three';
import { paint } from '../figure/figure.js';
import { loadStreetAsset, streetClone } from '../street-assets.js';
import { EVADE_TIME } from '../figure/player.js';

export const ATTACKS = {
  cut: { duration: 0.54, hit: [0.32, 0.69], reach: 2.6 },
  backhand: { duration: 0.50, hit: [0.30, 0.70], reach: 2.6 },
  thrust: { duration: 0.66, hit: [0.42, 0.72], reach: 3.1 },
  heavy: { duration: 0.88, hit: [0.42, 0.73], reach: 3.4 },
  spin: { duration: 0.62, hit: [0.16, 0.84], reach: 3.3 }, // the whirl: hunt.js turns the whole body once round
  counter: { duration: 0.47, hit: [0.24, 0.65], reach: 3.15 },
  advance: { duration: 0.64, hit: [0.36, 0.66], reach: 3.35 },
};
const GUARD = [0.22, 1.36, 0.45, -0.84, 0.52, 0.15, -0.28];
// Each frame is [grip x/y/z, blade x/y/z, trunk turn]. The grip is in character metres.
const POSES = {
  cut: [[0.34, 0.98, 0.12, 0.1, -0.9, 0.2, 0], [0.52, 1.5, -0.16, 0.5, 0.8, -0.5, -0.65], [-0.25, 1.13, 0.58, -0.8, 0.15, 0.6, 0.6], [-0.3, 0.85, 0.22, -0.5, -0.6, 0.5, 0.35], [0.34, 0.98, 0.12, 0.1, -0.9, 0.2, 0]],
  backhand: [[0.34, 0.98, 0.12, 0.1, -0.9, 0.2, 0], [-0.3, 1.1, 0.24, -0.8, 0.2, -0.3, 0.5], [0.47, 1.35, 0.53, 0.85, 0.15, 0.4, -0.55], [0.51, 1.4, 0.0, 0.5, 0.65, 0.1, -0.3], [0.34, 0.98, 0.12, 0.1, -0.9, 0.2, 0]],
  thrust: [[0.34, 0.98, 0.12, 0.1, -0.9, 0.2, 0], [0.3, 1.22, -0.1, 0, 0.1, 1, -0.45], [0.1, 1.18, 0.68, 0, 0.05, 1, 0.35], [0.13, 1.15, 0.58, 0, 0.05, 1, 0.2], [0.34, 0.98, 0.12, 0.1, -0.9, 0.2, 0]],
  heavy: [[0.34, 0.98, 0.12, 0.1, -0.9, 0.2, 0], [0.2, 1.76, -0.08, 0, 0.5, -0.9, -0.15], [0.1, 1.1, 0.68, 0, -0.8, 0.6, 0.2], [0.15, 0.85, 0.5, 0, -0.9, 0.4, 0.3], [0.34, 0.98, 0.12, 0.1, -0.9, 0.2, 0]],
  spin: [[0.34, 0.98, 0.12, 0.1, -0.9, 0.2, 0], [0.62, 1.15, 0.22, 1, 0.05, 0.25, 0.3], [0.64, 1.12, 0.26, 1, 0.0, 0.3, 0.2], [0.6, 1.1, 0.3, 0.9, -0.1, 0.4, 0.1], [0.34, 0.98, 0.12, 0.1, -0.9, 0.2, 0]],
  counter: [GUARD, [0.19, 1.27, 0.62, -0.35, 0.18, 0.92, -0.15], [0.14, 1.11, 0.70, 0.54, -0.22, 0.81, 0.44], [0.47, 1.08, 0.34, 0.84, -0.37, 0.38, 0.32], [0.34, 0.98, 0.12, 0.1, -0.9, 0.2, 0]],
  advance: [[0.34, 0.98, 0.12, 0.1, -0.9, 0.2, 0], [0.47, 0.79, -0.10, 0.65, -0.35, -0.68, -0.55], [0.10, 1.24, 0.68, -0.25, 0.44, 0.86, 0.28], [-0.25, 1.53, 0.45, -0.4, 0.89, 0.21, 0.40], [0.34, 0.98, 0.12, 0.1, -0.9, 0.2, 0]],
};
export function sampleAttack(type, u) {
  const keys = POSES[type] ?? POSES.cut, times = [0, 0.28, 0.53, 0.76, 1];
  u = Math.max(0, Math.min(1, u));
  let i = 0; while (i < 3 && u > times[i + 1]) i++;
  const k = (u - times[i]) / (times[i + 1] - times[i]), t = k * k * (3 - 2 * k);
  return keys[i].map((v, n) => v + (keys[i + 1][n] - v) * t);
}

const TRAIL_SAMPLES = 20;
const TRAIL_FADE = 0.18;
const TRAIL_NEON = {
  cyan: [0x00 / 255, 0xf0 / 255, 0xff / 255],
  gold: [0xfc / 255, 0xee / 255, 0x0a / 255],
  violet: [0xd5 / 255, 0x80 / 255, 0xff / 255],
  crimson: [0xff / 255, 0x2a / 255, 0x6d / 255],
};
const TRAIL_VERT = /* glsl */ `
  in vec3 color;
  out vec3 vColor;
  out vec2 vUv;
  void main() {
    vColor = color;
    vUv = uv;
    gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
  }
`;
const TRAIL_FRAG = /* glsl */ `
  uniform float uFade;
  in vec3 vColor;
  in vec2 vUv;
  layout(location = 0) out vec4 outC;
  void main() {
    float age = clamp(vUv.x, 0.0, 1.0);
    float rad = clamp(vUv.y, 0.0, 1.0);
    float lead = exp(-age * 8.5);
    float outerArc = smoothstep(0.66, 0.94, rad);
    float innerWake = smoothstep(0.0, 0.24, rad) * (1.0 - 0.28 * smoothstep(0.44, 0.68, rad));
    float streak = 0.9 + 0.1 * sin(rad * 26.0 - age * 7.0);
    vec3 col = mix(vColor * 1.15, vec3(1.75, 1.95, 2.25), clamp(lead * 0.85 + outerArc * (1.0 - age * 0.65) * 0.52, 0.0, 1.0));
    float tail = pow(1.0 - age, 1.55);
    float rimCut = smoothstep(1.0, 0.96, rad);
    float alpha = uFade * tail * streak * rimCut * clamp(innerWake * 0.30 + outerArc * 0.72 + lead * 0.38, 0.0, 0.88);
    outC = vec4(col, alpha);
  }
`;
const TRAIL_GLOW_FRAG = /* glsl */ `
  uniform float uFade;
  in vec3 vColor;
  in vec2 vUv;
  layout(location = 0) out vec4 outC;
  void main() {
    float age = clamp(vUv.x, 0.0, 1.0);
    float rad = clamp(vUv.y, 0.0, 1.0);
    float halo = uFade * pow(1.0 - age, 1.85) * smoothstep(0.0, 0.38, rad) * (0.12 + 0.18 * smoothstep(0.62, 0.98, rad));
    outC = vec4(vColor, halo);
  }
`;

export function createCombatPose(man, shared, scene, { rival = false, lantern = false } = {}) {
  const sword = new THREE.Group();
  sword.name = rival ? 'Rival katana' : 'Traveler katana';
  const steel = paint(shared, [[0.9, 0.98, 1], [0.53, 0.75, 0.8], [0.18, 0.3, 0.38]]);
  const gold = paint(shared, [[0.98, 0.77, 0.35], [0.65, 0.43, 0.14], [0.28, 0.19, 0.12]]);
  const dark = paint(shared, [[0.19, 0.27, 0.31], [0.09, 0.14, 0.18], [0.04, 0.07, 0.1]]);
  const blade = new THREE.Shape();
  blade.moveTo(-0.025, 0.1); blade.lineTo(0.025, 0.1); blade.lineTo(0.055, 0.98); blade.lineTo(0.025, 1.17); blade.lineTo(-0.02, 1.02); blade.closePath();
  const fallback = [];
  const add = (g, m, y = 0) => { const o = new THREE.Mesh(g, m); o.position.y = y; o.layers.enable(1); sword.add(o); fallback.push(o); return o; };
  add(new THREE.ExtrudeGeometry(blade, { depth: 0.012, bevelEnabled: true, bevelSize: 0.003, bevelThickness: 0.003, bevelSegments: 1, steps: 1 }), steel);
  const guard = add(new THREE.TorusGeometry(0.065, 0.014, 5, 12), gold, 0.08); guard.rotation.x = Math.PI / 2;
  add(new THREE.CylinderGeometry(0.026, 0.028, 0.21, 8), dark, -0.035);
  for (let i = 0; i < 6; i++) { const wrap = add(new THREE.TorusGeometry(0.028, 0.005, 4, 8), gold, -0.12 + i * 0.031); wrap.rotation.x = Math.PI / 2; }
  loadStreetAsset(rival ? 'ren-odachi' : 'cyber-katana').then((asset) => {
    for (const m of fallback) { sword.remove(m); m.geometry.dispose(); }
    fallback.length = 0;
    steel.dispose(); gold.dispose(); dark.dispose();
    const heroBlade = streetClone(asset, shared);
    heroBlade.traverse((o) => { if (o.isMesh) o.layers.enable(1); });
    sword.add(heroBlade);
  }).catch(() => {});
  const visualHand = man.appearance?.bindings.find(b => b.from === man.bones.hand_l)?.to ?? man.bones.hand_l;
  visualHand.add(sword);
  const hipBone = man.bones?.pelvis ?? man.bones?.spine_01;
  let hipLantern = null;
  if (!rival && !lantern && hipBone) {
    hipLantern = new THREE.Group();
    hipLantern.name = 'Komorebi Seed lantern';
    hipLantern.position.set(-0.155, -0.055, -0.055);
    hipLantern.rotation.set(0.06, 0.22, -0.10);
    hipLantern.scale.setScalar(0.55);
    hipBone.add(hipLantern);
    loadStreetAsset('spirit-lantern').then((asset) => {
      const seed = streetClone(asset, shared);
      seed.traverse((o) => { if (o.isMesh) o.layers.enable(1); });
      hipLantern.add(seed);
    }).catch(() => {});
  }
  const geometry = new THREE.BufferGeometry();
  const positions = new Float32Array(TRAIL_SAMPLES * 18), colors = new Float32Array(positions.length), uvs = new Float32Array(TRAIL_SAMPLES * 12);
  geometry.setAttribute('position', new THREE.BufferAttribute(positions, 3).setUsage(THREE.DynamicDrawUsage));
  geometry.setAttribute('color', new THREE.BufferAttribute(colors, 3).setUsage(THREE.DynamicDrawUsage));
  geometry.setAttribute('uv', new THREE.BufferAttribute(uvs, 2).setUsage(THREE.DynamicDrawUsage));
  geometry.setDrawRange(0, 0);
  const uFade = { value: 0 };
  const trailMaterial = new THREE.ShaderMaterial({
    glslVersion: THREE.GLSL3, uniforms: { uFade }, vertexShader: TRAIL_VERT, fragmentShader: TRAIL_FRAG,
    side: THREE.DoubleSide, transparent: true, depthWrite: false, toneMapped: false,
  });
  const glowMaterial = new THREE.ShaderMaterial({
    glslVersion: THREE.GLSL3, uniforms: { uFade }, vertexShader: TRAIL_VERT, fragmentShader: TRAIL_GLOW_FRAG,
    side: THREE.DoubleSide, transparent: true, depthWrite: false, blending: THREE.AdditiveBlending, toneMapped: false,
  });
  const trail = new THREE.Mesh(geometry, trailMaterial); trail.frustumCulled = false;
  trail.name = 'Blade trail';
  const trailGlow = new THREE.Mesh(geometry, glowMaterial); trailGlow.frustumCulled = false;
  trailGlow.name = 'Blade trail glow';
  trail.add(trailGlow);
  scene.add(trail);
  const points = [], base = new THREE.Vector3(), tip = new THREE.Vector3(), up = new THREE.Vector3(0, 1, 0);
  const gripPos = new THREE.Vector3(), prevGrip = new THREE.Vector3(), prevTip = new THREE.Vector3();
  const midGrip = new THREE.Vector3(), prevBlade = new THREE.Vector3(), currBlade = new THREE.Vector3(), midDir = new THREE.Vector3();
  const q = new THREE.Quaternion(), parentQ = new THREE.Quaternion(), before = new THREE.Vector3(), after = new THREE.Vector3();
  const target = new THREE.Vector3(), shoulder = new THREE.Vector3(), elbow = new THREE.Vector3(), wrist = new THREE.Vector3();
  const dir = new THREE.Vector3(), bend = new THREE.Vector3(), wantedElbow = new THREE.Vector3();
  const turnQ = new THREE.Quaternion(), yAxis = new THREE.Vector3(0, 1, 0), bladeDir = new THREE.Vector3();
  const tipSoriX = rival ? 0.042 : 0.03;
  let active = false, wasActive = false, attackType = 'cut', currentU = -1, prevU = -1, trailFade = 0, lastDt = 1 / 60;
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
      gripPos.set(0, 0, 0).applyMatrix4(sword.matrixWorld);
      base.set(0, 0.28, 0).applyMatrix4(sword.matrixWorld);
      tip.set(tipSoriX, 1.18, 0).applyMatrix4(sword.matrixWorld);
      const maxPts = attackType === 'spin' ? TRAIL_SAMPLES : 18;
      if (!wasActive || currentU < prevU - 0.12) {
        points.length = 0;
        points.unshift([base.clone(), tip.clone()]);
      } else if (lastDt > 0) {
        if (tip.distanceToSquared(prevTip) > 0.0009 && points.length > 0) {
          midGrip.addVectors(prevGrip, gripPos).multiplyScalar(0.5);
          prevBlade.subVectors(prevTip, prevGrip);
          currBlade.subVectors(tip, gripPos);
          const tipR = 0.5 * (prevBlade.length() + currBlade.length());
          const baseR = base.distanceTo(gripPos);
          midDir.addVectors(prevBlade.normalize(), currBlade.normalize());
          if (midDir.lengthSq() > 1e-6) {
            midDir.normalize();
            points.unshift([
              midGrip.clone().addScaledVector(midDir, baseR),
              midGrip.clone().addScaledVector(midDir, tipR),
            ]);
          }
        }
        points.unshift([base.clone(), tip.clone()]);
        while (points.length > maxPts) points.pop();
      }
      prevGrip.copy(gripPos);
      prevTip.copy(tip);
      trailFade = TRAIL_FADE;
    } else if (lastDt > 0) {
      trailFade = Math.max(0, trailFade - lastDt);
    }
    wasActive = active;
    prevU = currentU;
    if (trailFade <= 0) points.length = 0;
    const neon = rival ? TRAIL_NEON.crimson : attackType === 'heavy' ? TRAIL_NEON.gold : attackType === 'spin' ? TRAIL_NEON.violet : TRAIL_NEON.cyan;
    const span = Math.max(1, points.length - 1);
    let out = 0, uvOut = 0;
    for (let i = 0; i + 1 < points.length && out + 18 <= positions.length; i++) {
      for (const [j, k] of [[i, 0], [i, 1], [i + 1, 0], [i + 1, 0], [i, 1], [i + 1, 1]]) {
        const age = j / span, bPt = points[j][0], tPt = points[j][1];
        const taper = k === 0 ? age * age * 0.68 : 0;
        positions[out] = bPt.x + (tPt.x - bPt.x) * (k === 1 ? 1 : taper);
        positions[out + 1] = bPt.y + (tPt.y - bPt.y) * (k === 1 ? 1 : taper);
        positions[out + 2] = bPt.z + (tPt.z - bPt.z) * (k === 1 ? 1 : taper);
        const whiteHot = j === 0 ? 1 : Math.max(0, 1 - age * 4.2) ** 2;
        const bright = (0.52 + 0.48 * (1 - age)) * (k === 1 ? 1 : 0.85);
        colors[out] = (neon[0] * (1 - whiteHot) + whiteHot) * bright;
        colors[out + 1] = (neon[1] * (1 - whiteHot) + whiteHot) * bright;
        colors[out + 2] = (neon[2] * (1 - whiteHot) + whiteHot) * bright;
        uvs[uvOut] = age;
        uvs[uvOut + 1] = k;
        out += 3; uvOut += 2;
      }
    }
    geometry.attributes.position.needsUpdate = geometry.attributes.color.needsUpdate = geometry.attributes.uv.needsUpdate = true;
    geometry.setDrawRange(0, out / 3);
    uFade.value = trailFade > 0 ? Math.min(1, trailFade / TRAIL_FADE) : 0;
  }
  if (man.appearance) man.appearance.afterUpdate = finishWeapon;
  let guardBlend = 0;
  return { sword, trail, hipLantern, apply(dt, { u = -1, type = 'cut', hurt = 0, cast = 0, guard = false, guardImpact = 0, evade = 0, evadeAngle = 0 } = {}) {
    lastDt = dt; attackType = type; currentU = u;
    const frame = u >= 0 ? sampleAttack(type, u) : sampleAttack('cut', 0);
    const weight = u >= 0 ? Math.sin(Math.PI * Math.min(1, u) * 0.9) : 0;
    guardBlend += ((guard ? 1 : 0) - guardBlend) * (1 - Math.exp(-20 * dt));
    const brace = u < 0 ? guardBlend : 0;
    for (let i = 0; i < frame.length; i++) frame[i] = THREE.MathUtils.lerp(frame[i], GUARD[i], brace);
    frame[1] -= brace * guardImpact * 0.11;
    frame[2] -= brace * guardImpact * 0.15;
    const duck = evade > 0 ? Math.sin(Math.PI * evade / EVADE_TIME) : 0;
    frame[1] -= duck * 0.25;
    for (const [name, amount] of [['spine_01', 0.25], ['spine_02', 0.35], ['spine_03', 0.4]]) {
      turnQ.setFromAxisAngle(yAxis, frame[6] * amount); man.bones[name].quaternion.multiply(turnQ);
    }
    man.bones.spine_02.rotation.x += (type === 'heavy' || type === 'advance' ? 0.18 * weight : 0.04 * weight) - hurt * 0.22 - brace * guardImpact * 0.21 + duck * (0.18 + 0.13 * Math.cos(evadeAngle));
    man.bones.spine_02.rotation.z -= duck * Math.sin(evadeAngle) * 0.28;
    if (hipLantern) hipLantern.rotation.z = -0.10 - weight * 0.08 + hurt * 0.12;
    man.object.updateMatrixWorld(true);
    arm('l', target.set(...frame.slice(0, 3)));
    if (!lantern && (u >= 0 || cast > 0 || brace > 0.02 || duck > 0)) {
      arm('r', target.set(-0.4 + brace * 0.16, 1.2 + cast * 0.22 + brace * 0.12 - duck * 0.29,
        cast > 0 ? 0.55 : -0.12 + brace * 0.55));
    }
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
