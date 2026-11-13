// Dogs and wolves on the path, a sword in the left hand (the right keeps the lantern at night),
// and a spark you can throw three times. Empty, it fills again in POWER_FILL seconds.
// The animals are Quaternius' rigs (public/animals), loaded before they are placed. Anime mode
// retains the original atlas for eyes, muzzles and fur markings under the shared cel shader.
// With `levels` (src/game/levels.js) the way becomes five gated levels: clearing a level's
// beasts opens its gate and grants one power (spark, dash, heavy swing, lantern flare), and
// each level's cairn is the checkpoint you return to. With `roam` every gate stands open,
// every power is unlocked and the fallen come back.
import * as THREE from 'three';
import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';
import * as SkeletonUtils from 'three/addons/utils/SkeletonUtils.js';
import { paint } from '../figure/figure.js';
import { createHUD } from './hud.js';
import { LEVELS, GOAL, POWERS, POWER_ORDER, powersFor, loadProgress, saveProgress, demoLevels } from './levels.js';

export const POWER_MAX = 3;
export const POWER_FILL = 42; // seconds from empty to full, inside the asked 30–50s
export const DASH_FILL = 26; // the dash quickens him: the spark comes back sooner too
export const HIT = {
  hp: { dog: 60, wolf: 120, brute: 330 },
  sword: { dog: 34, wolf: 30, brute: 22 },
  bolt: { dog: 80, wolf: 50, brute: 40 },
};
const MAX_HP = 100;
const SWING = 0.55;
const ARC = [
  [0.32, 0.84, -0.05],
  [0.42, 1.36, 0.22],
  [0.08, 1.16, 0.58],
  [-0.4, 0.9, 0.34],
  [0.32, 0.84, -0.05],
];
const KIND = {
  dog: { dmg: 9, roam: 1.8, chase: 3.5, aggro: 9, reach: 1.55, leash: 18, home: 3.2, scale: 0.75, boltR: 0.8 },
  wolf: { dmg: 16, roam: 1.5, chase: 4.8, aggro: 12, reach: 1.95, leash: 20, home: 4, scale: 1.05, boltR: 1.1 },
  brute: { dmg: 26, roam: 1.6, chase: 5.2, aggro: 12, reach: 2.6, leash: 20, home: 4.5, scale: 2.35, boltR: 1.9 },
};
const PAL = {
  dog: [[0.8, 0.52, 0.24], [0.55, 0.32, 0.15], [0.32, 0.17, 0.1]],
  wolf: [[0.7, 0.72, 0.74], [0.42, 0.44, 0.48], [0.22, 0.23, 0.27]],
  brute: [[0.34, 0.2, 0.2], [0.19, 0.1, 0.1], [0.09, 0.05, 0.05]],
  steel: [[0.78, 0.8, 0.84], [0.42, 0.46, 0.5], [0.16, 0.18, 0.22]],
  hilt: [[0.45, 0.26, 0.12], [0.24, 0.13, 0.08], [0.1, 0.06, 0.05]],
  stone: [[0.74, 0.68, 0.56], [0.48, 0.42, 0.34], [0.26, 0.22, 0.18]],
  gold: [[0.95, 0.78, 0.35], [0.72, 0.42, 0.12], [0.35, 0.16, 0.05]],
  shrine: [[0.84, 0.31, 0.23], [0.60, 0.18, 0.16], [0.30, 0.12, 0.14]],
};

/** Power left after one throw, or null when a full charge isn't ready. */
export function spendPower(power) {
  return power >= 1 ? power - 1 : null;
}

/** Refill toward POWER_MAX. A full bar from empty takes POWER_FILL seconds. */
export function stepPower(power, dt, fill = POWER_FILL, max = POWER_MAX) {
  if (power >= max) return max;
  return Math.min(max, power + (dt * max) / fill);
}

/** True when (ex, ez) is inside the swing. `close` metres counts even behind him (they're on him). */
export function inFront(px, pz, yaw, ex, ez, reach, arc, close = 0.65) {
  const dx = ex - px, dz = ez - pz, d = Math.hypot(dx, dz);
  if (close > 0 && d <= close) return true;
  if (d > reach) return false;
  const diff = Math.atan2(Math.sin(Math.atan2(dx, dz) - yaw), Math.cos(Math.atan2(dx, dz) - yaw));
  return Math.abs(diff) <= arc;
}

/** A committed enemy strike can be dodged sideways, backwards, or over with a jump. */
export function strikeHits(e, x, y, z, groundY) {
  return Math.abs(y - groundY) < 0.9 && inFront(e.x, e.z, e.attackYaw, x, z, e.reach + 0.2, 0.85, 0.35);
}

function sample(u) {
  const x = Math.min(0.999, Math.max(0, u)) * (ARC.length - 1);
  const i = Math.floor(x), t = x - i, s = t * t * (3 - 2 * t);
  const a = ARC[i], b = ARC[i + 1];
  return [a[0] + (b[0] - a[0]) * s, a[1] + (b[1] - a[1]) * s, a[2] + (b[2] - a[2]) * s];
}

const CLIP = (clip) => clip.name.split('|').pop().replace(/ \d+$/, '');

export async function createHunt({ scene, shared, man, animator, player, heightAt, marks, spots, moveSpawn, down, levels = null, roam = false, route = null }) {
  // Level mode builds its own marks and cairns from the level list; the night
  // scene passes marks and spots directly, which keeps working as before.
  const segs = levels ? levels.map((L) => ({ ...L })) : null;
  if (segs) {
    marks = [...segs.map((s) => ({ ...s.checkpoint })), { ...GOAL, goal: true }];
    spots = segs.flatMap((s, i) => s.spots.map((p) => ({ ...p, seg: i })));
  }
  const loader = new GLTFLoader();
  const [dogGltf, wolfGltf] = await Promise.all([
    loader.loadAsync('./animals/dog.glb'),
    loader.loadAsync('./animals/wolf.glb'),
  ]);
  const sources = { dog: dogGltf, wolf: wolfGltf };
  const mats = new Map();
  const mat = (name) => {
    if (!mats.has(name)) mats.set(name, paint(shared, PAL[name], { grain: name === 'steel' ? 4 : 2.6 }));
    return mats.get(name);
  };
  const put = (parent, geometry, material, place) => {
    const m = new THREE.Mesh(geometry, material);
    m.layers.enable(1);
    place?.(m);
    parent.add(m);
    return m;
  };
  const GEO = { rock: new THREE.SphereGeometry(1, 6, 5), cap: new THREE.SphereGeometry(1, 7, 5) };

  function beastMesh(kind) {
    const src = sources[kind === 'brute' ? 'wolf' : kind]; // the brute is a grown wolf: same rig, darker paint
    const mesh = SkeletonUtils.clone(src.scene);
    mesh.traverse((o) => {
      if (!o.isMesh) return;
      const key = 'beast-' + kind;
      if (!mats.has(key)) mats.set(key, paint(shared, PAL[kind], { grain: 2.6, skinned: true,
        map: shared.uAnime.value > 0.5 ? o.material.map : null,
        tint: kind === 'brute' ? [0.65, 0.78, 0.82] : kind === 'dog' ? [1.8, 1.6, 1.4] : [1.3, 1.4, 1.4] }));
      o.material = mats.get(key);
      o.frustumCulled = false;
      o.layers.enable(1);
    });
    const clips = {};
    for (const clip of src.animations) clips[CLIP(clip)] = clip;
    const mixer = new THREE.AnimationMixer(mesh);
    const action = mixer.clipAction(clips.Idle);
    action.play();
    mesh.userData.mixer = mixer;
    mesh.userData.clips = clips;
    mesh.userData.action = action;
    mesh.userData.clip = 'Idle';
    mesh.scale.setScalar(KIND[kind].scale);
    return mesh;
  }

  const enemies = spots.map((s) => {
    const mesh = beastMesh(s.kind);
    scene.add(mesh);
    const tell = new THREE.Mesh(new THREE.RingGeometry(0.88, 1, 32), new THREE.MeshBasicMaterial({ color: 0xed8a60, transparent: true, opacity: 0.7, side: THREE.DoubleSide, depthWrite: false }));
    tell.rotation.x = -Math.PI / 2; tell.visible = false; scene.add(tell);
    return { ...s, ...KIND[s.kind], mesh, tell, hp: HIT.hp[s.kind], maxHp: HIT.hp[s.kind], homeX: s.x, homeZ: s.z, tx: s.x, tz: s.z, yaw: Math.random() * 6.28, wait: Math.random() * 1.5, mode: 'roam', cool: 0, windup: 0, stagger: 0, phase: Math.random() * 6, flash: 0, dead: 0, gone: false, going: 0, seg: s.seg ?? -1, respawnT: 0 };
  });
  function addBody(e) {
    const r = e.kind === 'brute' ? 1.05 : e.kind === 'wolf' ? 0.5 : 0.35;
    const half = e.kind === 'brute' ? 0.65 : 0.2;
    e.bodyLift = r + half;
    e.body = player.world.createRigidBody(player.R.RigidBodyDesc.kinematicPositionBased().setTranslation(e.x, heightAt(e.x, e.z) + e.bodyLift, e.z));
    e.collider = player.world.createCollider(player.R.ColliderDesc.capsule(half, r).setFriction(0), e.body);
  }
  for (const e of enemies) addBody(e);

  // Stone gates across the way, one at the end of every level but the last.
  // They stand while their level's beasts are still out and are taken away opened.
  const gates = [];
  if (segs && !roam) {
    for (const [i, s] of segs.entries()) {
      if (!s.gate) continue;
      const g = new THREE.Group();
      for (const x of [-5.3, 5.3]) {
        put(g, new THREE.CylinderGeometry(0.17, 0.24, 4.8, 8), mat('shrine'), m => m.position.set(x, 2.4, 0));
        put(g, new THREE.CylinderGeometry(0.33, 0.4, 0.45, 8), mat('stone'), m => m.position.set(x, 0.2, 0));
      }
      for (const [y, width] of [[3.6, 12], [4.8, 13]]) {
        put(g, new THREE.BoxGeometry(width, 0.26, 0.5), mat('shrine'), m => m.position.y = y);
      }
      for (const x of [-6.1, 6.1]) put(g, new THREE.BoxGeometry(1.3, 0.24, 0.52), mat('shrine'), m => { m.position.set(x, 4.95, 0); m.rotation.z = Math.sign(x) * 0.2; });
      const veil = new THREE.Mesh(new THREE.PlaneGeometry(10.5, 3.4), new THREE.MeshBasicMaterial({ color: 0x9cdccc, transparent: true, opacity: 0.22, side: THREE.DoubleSide, depthWrite: false }));
      veil.position.y = 1.9; g.add(veil);
      for (let k = -4; k <= 4; k++) put(g, new THREE.BoxGeometry(0.045, 3.3, 0.03), mat('gold'), m => m.position.set(k * 1.15, 1.9, 0));
      g.position.set(s.gate.x, heightAt(s.gate.x, s.gate.z), s.gate.z);
      scene.add(g);
      const collider = player.world.createCollider(player.R.ColliderDesc.cuboid(90, 4, 0.4).setTranslation(s.gate.x, g.position.y + 3, s.gate.z));
      gates.push({ group: g, seg: i, collider });
    }
  }

  for (const m of marks) {
    const g = new THREE.Group();
    for (let i = 0; i < 3; i++) {
      put(g, GEO.rock, mat(m.goal && i === 2 ? 'gold' : 'stone'), (rock) => {
        rock.scale.set(0.55 - i * 0.10, 0.23, 0.48 - i * 0.10);
        rock.position.y = 0.18 + i * 0.3;
        rock.rotation.y = i * 0.7;
      });
    }
    const crystal = put(g, new THREE.OctahedronGeometry(0.28), mat('gold'), cap => cap.position.y = 1.5);
    m.crystal = crystal;
    // Place the shrine beside its trigger so the starting one never hides the traveler.
    const mx = m.x + (segs ? 3.6 : 0);
    g.position.set(mx, heightAt(mx, m.z), m.z);
    scene.add(g);
    m.used = false;
  }

  const sword = new THREE.Group();
  put(sword, new THREE.BoxGeometry(0.04, 0.7, 0.012), mat('steel'), (m) => { m.position.y = 0.4; });
  put(sword, new THREE.BoxGeometry(0.18, 0.028, 0.04), mat('steel'));
  put(sword, new THREE.BoxGeometry(0.032, 0.16, 0.032), mat('hilt'), (m) => { m.position.y = -0.1; });
  man.object.add(sword);

  const bolts = [];
  const flares = [];
  const boltGeo = new THREE.SphereGeometry(0.16, 6, 5);
  const impacts = [];
  const slash = new THREE.Mesh(new THREE.TorusGeometry(1.25, 0.04, 4, 28, Math.PI * 1.25), new THREE.MeshBasicMaterial({ color: 0xc6f7e2, transparent: true, opacity: 0.85, depthWrite: false }));
  slash.rotation.x = Math.PI / 2; slash.position.y = 1; slash.visible = false; man.object.add(slash);

  const hud = createHUD({ levels: segs, roam, marks, route });
  let hp = MAX_HP, power = POWER_MAX, swing = -1, swingH = false, struck = new Set(), invuln = 0, won = false;
  let dashCool = 0, flareCool = 0, gateToastT = 0, lockToastT = 0, saveWarned = false, runTold = false;
  let attackBuffer = 0, queuedHeavy = false;
  let saveWarningPending = false;

  // --- level state -----------------------------------------------------------------
  // `cleared` is the count of fully cleared segments; the current level is segs[cleared].
  const segAlive = (i) => enemies.some((e) => e.seg === i && !e.dead);
  const clearedCount = () => { let n = 0; if (segs) while (n < segs.length && !segAlive(n)) n++; return n; };
  const remaining = () => enemies.reduce((n, e) => n + (e.dead ? 0 : 1), 0);
  let powers = roam && segs ? powersFor(segs.length) : { spark: false, dash: false, heavy: false, flare: false };
  const has = (k) => !segs || roam || powers[k];
  function persist(cleared) {
    if (!segs || roam) return;
    const cp = segs[Math.min(cleared, segs.length - 1)].checkpoint;
    if (!saveProgress({ cleared, x: +cp.x.toFixed(2), z: +cp.z.toFixed(2) }) && !saveWarned) {
      saveWarned = true;
      saveWarningPending = true;
    }
  }
  if (segs && !roam) {
    marks[0].used = true; // he starts on the first checkpoint: no toast for standing still
    const saved = loadProgress();
    if (saved && saved.cleared > 0) {
      const n = Math.min(saved.cleared, segs.length);
      for (const e of enemies) {
        if (e.seg < n) { scene.remove(e.mesh); player.world.removeRigidBody(e.body); e.body = e.collider = null; e.dead = 1; e.gone = true; }
      }
      for (let i = 0; i < n; i++) marks[i].used = true;
      powers = powersFor(n);
      moveSpawn(saved.x, saved.z);
      down();
      toast(n >= segs.length ? 'The clearing lies ahead' : `Level ${n + 1} · ${segs[n].name}`);
    }
  } else if (segs && roam) {
    for (const m of marks) m.cool = 0;
  }
  const vFrom = new THREE.Vector3(), vTo = new THREE.Vector3(), vDir = new THREE.Vector3();
  const vTarget = new THREE.Vector3(), vLeft = new THREE.Vector3(), vUp = new THREE.Vector3(0, 1, 0);
  const qA = new THREE.Quaternion(), qP = new THREE.Quaternion(), qW = new THREE.Quaternion();

  function toast(msg) { hud.toast(msg); }

  function aim(bone, child, dir) {
    if (dir.lengthSq() < 1e-8) return;
    dir.normalize();
    vFrom.setFromMatrixPosition(man.bones[child].matrixWorld);
    vTo.setFromMatrixPosition(man.bones[bone].matrixWorld);
    vFrom.sub(vTo);
    if (vFrom.lengthSq() < 1e-8 || vFrom.normalize().dot(dir) > 0.999) return;
    qA.setFromUnitVectors(vFrom, dir);
    const b = man.bones[bone];
    b.parent.getWorldQuaternion(qP);
    b.getWorldQuaternion(qW);
    b.quaternion.copy(qP.invert().multiply(qA.multiply(qW)));
    b.updateMatrixWorld(true);
  }

  function hurt(e, n) {
    if (e.dead) return;
    e.hp -= n;
    e.flash = 0.16;
    e.stagger = n > 40 ? 0.55 : 0.22;
    e.windup = 0;
    e.cool = Math.max(e.cool, 0.45);
    for (let i = 0; i < 5 && impacts.length < 40; i++) {
      const mesh = put(scene, boltGeo, mat('gold'));
      mesh.scale.setScalar(0.35);
      impacts.push({ mesh, x: e.x, y: heightAt(e.x, e.z) + 0.75, z: e.z, vx: (Math.random() - 0.5) * 7, vy: 2 + Math.random() * 3, vz: (Math.random() - 0.5) * 7, life: 0.3 });
    }
    if (e.hp <= 0) {
      e.dead = 0.001; e.tell.visible = false; power = Math.min(POWER_MAX, power + 0.35);
      player.world.removeRigidBody(e.body); e.body = e.collider = null;
    }
  }

  function holdAt(x, z) {
    player.teleport(x, heightAt(x, z), z);
    animator.lock = null; animator.lockOff = null;
    animator.enter(animator.locoState({ strafe: false }), 0.08);
  }

  function die() {
    down();
    hp = MAX_HP;
    invuln = 2.6;
    swing = -1; swingH = false;
    attackBuffer = 0;
    for (const e of enemies) {
      if (e.dead) continue;
      if (Math.hypot(e.x - player.feet.x, e.z - player.feet.z) < 5) {
        e.x = e.homeX; e.z = e.homeZ; e.mode = 'roam'; e.tx = e.homeX; e.tz = e.homeZ;
        e.windup = e.stagger = e.cool = 0;
      }
    }
    toast('Back to the last checkpoint');
  }

  function playClip(e, name) {
    if (e.mesh.userData.clip === name) return;
    const u = e.mesh.userData;
    const next = u.mixer.clipAction(u.clips[name]);
    if (name === 'Death' || name === 'Headbutt') { next.setLoop(THREE.LoopOnce, 1); next.clampWhenFinished = true; }
    else next.setLoop(THREE.LoopRepeat);
    next.reset().fadeIn(0.15).play();
    u.action.fadeOut(0.15);
    u.action = next;
    u.clip = name;
  }

  function lay(e, dt) {
    const sc = e.scale * (e.flash > 0 ? 1.06 : 1);
    e.mesh.scale.set(sc, sc * (e.flash > 0 ? 0.86 : 1), sc);
    e.mesh.position.set(e.x, heightAt(e.x, e.z) - (e.dead ? Math.min(0.4, Math.max(0, e.dead - 0.9) * 0.45) : 0), e.z);
    e.mesh.rotation.set(0, e.yaw, 0);
    if (e.body) e.body.setTranslation({ x: e.x, y: heightAt(e.x, e.z) + e.bodyLift, z: e.z }, true);
    e.tell.visible = !e.dead && e.windup > 0;
    e.tell.position.set(e.x, heightAt(e.x, e.z) + 0.04, e.z);
    e.tell.scale.setScalar(e.reach * (0.6 + e.windup * 0.4));
    const name = e.dead ? 'Death' : e.windup > 0 ? 'Headbutt' : e.going > 2.4 ? 'Run' : e.going > 0.25 ? 'Walk' : 'Idle';
    playClip(e, name);
    e.mesh.userData.mixer.update(dt);
  }

  function pose() {
    if (swing < 0) {
      sword.position.set(0.32, 0.84, -0.05);
      sword.quaternion.setFromUnitVectors(vUp, vDir.set(0.15, -1, -0.2).normalize());
      return;
    }
    const p = sample(swing), n = sample(Math.min(0.999, swing + 0.06));
    sword.position.set(p[0], p[1], p[2]);
    vDir.set(n[0] - p[0], n[1] - p[1], n[2] - p[2]);
    sword.quaternion.setFromUnitVectors(vUp, vDir.lengthSq() < 1e-8 ? vDir.set(0, 1, 0) : vDir.normalize());
    man.object.updateMatrixWorld(true);
    sword.getWorldPosition(vTarget);
    vTo.setFromMatrixPosition(man.bones.upperarm_l.matrixWorld);
    vLeft.set(1, 0, 0).applyQuaternion(man.object.quaternion);
    vDir.copy(vTarget).sub(vTo).addScaledVector(vUp, 0.22).addScaledVector(vLeft, 0.12);
    aim('upperarm_l', 'lowerarm_l', vDir);
    vTo.setFromMatrixPosition(man.bones.lowerarm_l.matrixWorld);
    sword.getWorldPosition(vTarget);
    aim('lowerarm_l', 'hand_l', vTarget.sub(vTo));
    for (const f of animator.fingerPoses ?? []) {
      if (f.bone.name.endsWith('_l')) f.bone.quaternion.slerpQuaternions(f.flat, f.fist, f.thumb ? 0.7 : 0.95);
    }
  }

  function update(dt, input) {
    invuln = Math.max(0, invuln - dt);
    dashCool = Math.max(0, dashCool - dt);
    flareCool = Math.max(0, flareCool - dt);
    gateToastT = Math.max(0, gateToastT - dt);
    lockToastT = Math.max(0, lockToastT - dt);
    attackBuffer = Math.max(0, attackBuffer - dt);
    for (const m of marks) { m.crystal.rotation.y += dt; m.crystal.position.y = 1.5 + Math.sin(m.crystal.rotation.y * 1.8) * 0.12; }
    const px = player.feet.x, pz = player.feet.z, yaw = player.yaw;
    const c = segs ? clearedCount() : 0;
    // A closed gate stands between two z values: neither side's teeth reach through.
    const gated = (ez) => segs && !roam && !won && gates.some((g) => c <= g.seg && (ez - g.group.position.z) * (pz - g.group.position.z) < 0);
    if (input?.attack || input?.heavy && (!segs || has('heavy'))) {
      attackBuffer = 0.2;
      queuedHeavy = !!(segs && input?.heavy && has('heavy'));
    }
    if (swing < 0 && attackBuffer > 0 && hp > 0) { swing = 0; swingH = queuedHeavy; struck = new Set(); attackBuffer = 0; }
    if (input?.heavy && segs && !has('heavy') && lockToastT <= 0) {
      lockToastT = 4;
      toast('Earn the heavy swing in Level 3');
    }
    if (swing >= 0) { swing += dt / (swingH ? 0.85 : SWING); if (swing >= 1) { swing = -1; swingH = false; } }
    const hitting = swingH ? swing >= 0.3 && swing <= 0.75 : swing >= 0.36 && swing <= 0.72;
    slash.visible = hitting;
    slash.rotation.z = -swing * Math.PI * 2;
    slash.scale.setScalar(swingH ? 1.45 : 1);

    // A dash: one quick step where he faces, too fast to be bitten.
    if (input?.dash && has('dash') && dashCool <= 0 && hp > 0) {
      if (player.startDash()) { dashCool = 2.5; invuln = Math.max(invuln, 0.3); }
    } else if (input?.dash && !has('dash') && lockToastT <= 0) {
      lockToastT = 4; toast('Clear Chapter 2 to learn the dash');
    }

    for (const e of enemies) {
      e.flash = Math.max(0, e.flash - dt);
      e.stagger = Math.max(0, e.stagger - dt);
      if (e.gone) {
        // In free roam the fallen come back after a while, at home and calm.
        if (roam && (e.respawnT -= dt) <= 0) {
          e.hp = HIT.hp[e.kind]; e.dead = 0; e.gone = false;
          e.x = e.homeX; e.z = e.homeZ; e.tx = e.homeX; e.tz = e.homeZ;
          e.mode = 'roam'; e.cool = e.windup = e.stagger = 0; e.wait = 1; e.mesh.visible = true;
          addBody(e);
        } else continue;
      }
      if (e.dead) {
        e.dead += dt;
        e.going = 0;
        lay(e, dt);
        if (e.dead > 1.8) {
          if (roam) { e.gone = true; e.respawnT = 20; e.mesh.visible = false; }
          else { scene.remove(e.mesh); e.gone = true; }
        }
        continue;
      }
      const dxp = px - e.x, dzp = pz - e.z, pd = Math.hypot(dxp, dzp);
      const homeD = Math.hypot(e.x - e.homeX, e.z - e.homeZ);
      // Far beasts keep their pose but skip the thinking: the frame stays cheap.
      if (pd > 36) { e.going = 0; e.cool = Math.max(0, e.cool - dt); lay(e, 0); continue; }
      if (e.mode === 'roam' && pd < e.aggro && !gated(e.z)) e.mode = 'chase';
      if (e.mode === 'chase' && (pd > e.aggro + 5 || homeD > e.leash)) {
        e.mode = 'roam'; e.tx = e.homeX; e.tz = e.homeZ; e.wait = 0;
      }
      let speed = 0;
      if (e.mode === 'chase') { e.tx = px; e.tz = pz; speed = pd > e.reach * 0.83 ? e.chase : 0; }
      else if (e.wait > 0) e.wait -= dt;
      else if (Math.hypot(e.tx - e.x, e.tz - e.z) < 0.45) {
        e.wait = 0.7 + Math.random() * 1.8;
        const a = Math.random() * Math.PI * 2, r = Math.random() * e.home;
        e.tx = e.homeX + Math.sin(a) * r; e.tz = e.homeZ + Math.cos(a) * r;
      } else speed = e.roam;
      const mx = e.tx - e.x, mz = e.tz - e.z, md = Math.hypot(mx, mz) || 1;
      if (e.windup > 0 || e.stagger > 0) speed = 0;
      let step = Math.min(speed * dt, md);
      if (step > 0) {
        const hit = player.castRay({ x: e.x, y: heightAt(e.x, e.z) + 0.6, z: e.z }, { x: mx / md, y: 0, z: mz / md }, step + 0.6, e.collider);
        if (hit != null) step = Math.max(0, hit - 0.6);
      }
      const x0 = e.x, z0 = e.z;
      e.x += (mx / md) * step; e.z += (mz / md) * step;
      e.going = Math.hypot(e.x - x0, e.z - z0) / Math.max(dt, 1e-4);
      if (step > 0.0001 && e.windup <= 0) {
        const want = Math.atan2(mx, mz);
        const turn = Math.atan2(Math.sin(want - e.yaw), Math.cos(want - e.yaw));
        e.yaw += Math.max(-8 * dt, Math.min(8 * dt, turn));
      }
      e.phase += dt * (3 + e.going * 2);
      e.cool = Math.max(0, e.cool - dt);
      const near = Math.hypot(px - e.x, pz - e.z);
      if (e.windup > 0) {
        e.windup = Math.max(0, e.windup - dt);
        if (e.windup === 0 && invuln <= 0 && hp > 0 && !gated(e.z) && strikeHits(e, px, player.feet.y, pz, heightAt(e.x, e.z))) {
          invuln = 0.7;
          hp -= e.dmg;
          if (hp <= 0) die();
        }
      } else if (e.mode === 'chase' && near < e.reach + 0.25 && e.cool <= 0 && e.stagger <= 0 && hp > 0 && !gated(e.z)) {
        e.windup = e.kind === 'brute' ? 0.9 : e.kind === 'wolf' ? 0.65 : 0.5;
        e.attackYaw = Math.atan2(px - e.x, pz - e.z);
        e.yaw = e.attackYaw;
        e.cool = e.windup + 1.0;
      }
      if (hitting && !struck.has(e) && !gated(e.z) && Math.abs(player.feet.y - heightAt(e.x, e.z)) < 1.7 && inFront(px, pz, yaw, e.x, e.z, swingH ? 3.4 : 2.5, swingH ? 1.6 : 1.2)) {
        struck.add(e);
        hurt(e, (swingH ? 3 : 1) * HIT.sword[e.kind]);
      }
      lay(e, dt);
    }
    for (let i = 0; i < enemies.length; i++) {
      for (let j = i + 1; j < enemies.length; j++) {
        const a = enemies[i], b = enemies[j];
        if (a.dead || b.dead || Math.hypot(a.x - px, a.z - pz) > 36 || Math.hypot(b.x - px, b.z - pz) > 36) continue;
        const minD = 0.65 * (a.scale + b.scale); // brutes need more room than dogs
        const dx = b.x - a.x, dz = b.z - a.z, d = Math.hypot(dx, dz) || 0.001;
        if (d < minD) {
          const push = (minD - d) * 0.5;
          a.x -= (dx / d) * push; b.x += (dx / d) * push; a.z -= (dz / d) * push; b.z += (dz / d) * push;
          lay(a, dt); lay(b, dt);
        }
      }
    }

    power = stepPower(power, dt, has('dash') && segs ? DASH_FILL : POWER_FILL);
    if (input?.power && hp > 0) {
      if (!has('spark')) {
        if (segs && !roam && lockToastT <= 0) {
          lockToastT = 4;
          toast('Clear Level 1 to earn the spark');
        }
      } else {
        const next = spendPower(power);
        if (next != null) {
          power = next;
          const mesh = put(scene, boltGeo, mat('gold'));
          bolts.push({
            mesh, life: 1.05,
            x: px + Math.sin(yaw) * 0.7, y: player.feet.y + 1.3, z: pz + Math.cos(yaw) * 0.7,
            vx: Math.sin(yaw) * 18, vy: 2.4, vz: Math.cos(yaw) * 18,
          });
        }
      }
    }
    // A lantern flare: blinding light that staggers every beast close by. Costs two spark.
    if (input?.flare && has('flare') && flareCool <= 0 && hp > 0) {
      if (power >= 2) {
        power -= 2;
        flareCool = 20;
        for (const e of enemies) {
          if (e.dead) continue;
          const d = Math.hypot(px - e.x, pz - e.z);
          if (d > 9 || gated(e.z)) continue;
          hurt(e, 60);
          const push = Math.max(0, 3 - d * 0.25) || 1.5;
          e.x += ((e.x - px) / (d || 1)) * push; e.z += ((e.z - pz) / (d || 1)) * push;
          e.cool = Math.max(e.cool, 2);
          e.mode = 'roam'; e.tx = e.homeX; e.tz = e.homeZ; e.wait = 1;
        }
        const mesh = put(scene, boltGeo, mat('gold'));
        flares.push({ mesh, t: 0, x: px, y: player.feet.y + 1.3, z: pz });
      } else if (lockToastT <= 0) {
        lockToastT = 4;
        toast('The flare needs two spark');
      }
    }
    for (let i = flares.length - 1; i >= 0; i--) {
      const f = flares[i];
      f.t += dt;
      f.mesh.position.set(f.x, f.y, f.z);
      f.mesh.scale.setScalar(1 + f.t * 30);
      if (f.t > 0.5) { scene.remove(f.mesh); flares.splice(i, 1); }
    }
    for (let i = impacts.length - 1; i >= 0; i--) {
      const p = impacts[i]; p.life -= dt; p.vy -= 15 * dt;
      p.x += p.vx * dt; p.y += p.vy * dt; p.z += p.vz * dt;
      p.mesh.position.set(p.x, p.y, p.z); p.mesh.scale.setScalar(Math.max(0, p.life));
      if (p.life <= 0) { scene.remove(p.mesh); impacts.splice(i, 1); }
    }
    for (let i = bolts.length - 1; i >= 0; i--) {
      const b = bolts[i];
      b.life -= dt; b.vy -= 7 * dt;
      b.x += b.vx * dt; b.y += b.vy * dt; b.z += b.vz * dt;
      b.mesh.position.set(b.x, b.y, b.z);
      let gone = b.life <= 0 || b.y < heightAt(b.x, b.z) + 0.25;
      if (!gone) {
        for (const e of enemies) {
          if (e.dead || gated(e.z)) continue;
          if (Math.hypot(b.x - e.x, b.z - e.z) < (e.boltR ?? 1.05)) {
            hurt(e, HIT.bolt[e.kind]);
            gone = true;
            break;
          }
        }
      }
      if (gone) { scene.remove(b.mesh); bolts.splice(i, 1); }
    }

    // Open every gate whose level is cleared; hold him at the first closed one.
    if (segs && !roam && !won) {
      for (const g of gates) {
        if (c > g.seg && g.group.parent) {
          scene.remove(g.group);
          player.world.removeCollider(g.collider, true);
        }
      }
      const shut = gates.find((g) => c <= g.seg);
      if (shut && pz < shut.group.position.z + 1.3) {
        // The collider handles ordinary contact; this only catches explicit teleports past a gate.
        if (pz < shut.group.position.z + 0.45) holdAt(px, shut.group.position.z + 1.6);
        if (gateToastT <= 0) {
          gateToastT = 4;
          const n = enemies.reduce((k, e) => k + (e.seg === c && !e.dead ? 1 : 0), 0);
          toast(`Clear ${segs[Math.min(c, segs.length - 1)].name} first — ${n} left`);
        }
      }
    }

    // New powers arrive the moment their level falls.
    if (segs && !roam && !won) {
      for (let i = 0; i < c; i++) {
        const g = segs[i].grant;
        if (g && !powers[g]) {
          persist(c);
          toast(`Level ${i + 1} clear — ${POWERS[g].name} earned (${POWERS[g].key})`);
        }
      }
      powers = powersFor(c);
      if (c >= segs.length && !runTold) { runTold = true; persist(c); toast('The sanctuary lies ahead — follow the trail'); }
    }

    if (hp > 0) {
      for (const [mi, m] of marks.entries()) {
        if (m.cool > 0) m.cool -= dt;
        if (m.used) continue;
        if (Math.hypot(px - m.x, pz - m.z) > (m.goal ? 3.6 : 3.1)) continue;
        if (segs && !roam) {
          if (m.goal) {
            if (c >= segs.length) {
              m.used = true; moveSpawn(m.x, m.z); hp = MAX_HP; won = true; persist(c);
              toast('The clearing is safe — you did it');
            } else if (gateToastT <= 0) {
              gateToastT = 4;
              toast(`The clearing is not safe yet — ${remaining()} still out`);
            }
          } else if (mi <= c) {
            m.used = true; moveSpawn(m.x, m.z); hp = MAX_HP; invuln = Math.max(invuln, 1.5); persist(c);
            toast(`Checkpoint · ${m.name}`);
          }
        } else if (segs && roam) {
          // Cairns heal and catch him when he falls, as often as he visits.
          if ((m.cool ?? 0) > 0) continue;
          m.cool = 10;
          moveSpawn(m.x, m.z); hp = MAX_HP;
          toast(m.goal ? m.name : `Checkpoint · ${m.name}`);
        } else {
          m.used = true;
          moveSpawn(m.x, m.z);
          hp = MAX_HP;
          if (m.goal) won = true;
          toast(m.goal ? m.name : `Checkpoint · ${m.name}`);
        }
      }
    }

    player.world.propagateModifiedBodyPositionsToColliders();
    // Show save failure after other progress messages, so an unlock cannot overwrite the warning.
    if (saveWarningPending) { toast('Progress is not saving in this browser'); saveWarningPending = false; }
    hud.update(dt, { hp, power, cleared: c, won, enemies, player, has, dashCool, flareCool, invuln });
  }

  return { update, pose, enemies, get hp() { return hp; }, get power() { return power; },
    get seg() { return segs ? clearedCount() : 0; }, get powers() { return { ...powers }; },
    get roam() { return roam; }, get won() { return won; } };
}

export function demo() {
  let p = POWER_MAX;
  p = spendPower(p); p = spendPower(p); p = spendPower(p);
  if (p !== 0) throw new Error('empty ' + p);
  if (spendPower(p) !== null) throw new Error('a fourth throw should not fire');
  const one = stepPower(0, POWER_FILL / POWER_MAX);
  if (Math.abs(one - 1) > 1e-6) throw new Error('one charge ' + one);
  if (Math.abs(stepPower(0, POWER_FILL) - POWER_MAX) > 1e-6) throw new Error('full bar');
  if (POWER_FILL < 30 || POWER_FILL > 50) throw new Error('fill outside 30–50s');
  if (!inFront(0, 0, 0, 0.2, 1.2, 1.9, 1.2)) throw new Error('front');
  if (inFront(0, 0, 0, 0, -1.2, 1.9, 1.2, 0)) throw new Error('behind');
  if (!inFront(0, 0, 0, 0, -0.4, 1.9, 1.2, 0.65)) throw new Error('on him');
  const attack = { x: 0, z: 0, attackYaw: 0, reach: 2 };
  if (!strikeHits(attack, 0, 0, 1.5, 0)) throw new Error('committed strike');
  if (strikeHits(attack, 2, 0, 0, 0) || strikeHits(attack, 0, 1.1, 1.5, 0) || strikeHits(attack, 0, 0, 3, 0)) throw new Error('strike must be dodgeable');
  if (HIT.hp.dog - HIT.bolt.dog > 0) throw new Error('spark should drop a dog');
  if (HIT.hp.wolf - HIT.bolt.wolf <= 0) throw new Error('spark should not drop a wolf');
  if (HIT.hp.dog - 2 * HIT.sword.dog > 0) throw new Error('two swings kill a dog');
  if (HIT.hp.wolf - 3 * HIT.sword.wolf <= 0) throw new Error('a wolf takes more than three swings');
  if (HIT.hp.brute <= HIT.hp.wolf) throw new Error('a brute outlasts a wolf');
  if (HIT.hp.brute - 5 * (3 * HIT.sword.brute) > 0) throw new Error('five heavy swings drop a brute');
  if (!(KIND.brute.scale > KIND.wolf.scale)) throw new Error('a brute looms over a wolf');
  demoLevels();
  const earned = powersFor(LEVELS.length);
  for (const k of POWER_ORDER) if (!earned[k]) throw new Error('all powers by the clearing: ' + k);
}

if (globalThis.process?.argv?.[1]?.endsWith('hunt.js')) demo();
