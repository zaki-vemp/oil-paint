// Street crews on the path, a sword in the left hand (the right keeps the lantern at night),
// and a spark you can throw three times. Empty, it fills again in POWER_FILL seconds.
// The crews are Quaternius' rigged people and machines (public/enemies): punks, enforcers, gunners,
// razor blades and a siege mech. Each keeps its own material colours, repainted through the shared
// cel shader (a white map and a tint, so a hit can flash one body).
// With `levels` (src/game/levels.js) the way becomes six gated levels: clearing a level's
// crew opens its gate and grants one power (spark, dash, heavy swing, lantern flare, whirl), and
// each level's cairn is the checkpoint you return to. With `roam` every gate stands open,
// every power is unlocked and the fallen come back.
// Feel: every body flashes white and is knocked back along the blow (swept, never through walls);
// heavy blows lift the light ones off their feet and the fallen are thrown. A hit shoves the traveler too.
// A dash in the last moment of a windup is a perfect dodge: time slows, a spark comes back. A strike
// landed in that same moment is a parry. Gunners' shots can be cut back at them. A strike in the air
// is a plunge that shakes everyone near where he lands. Hits chain into a combo count the HUD shows.
import * as THREE from 'three';
import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';
import * as SkeletonUtils from 'three/addons/utils/SkeletonUtils.js';
import { paint } from '../figure/figure.js';
import { createHUD } from './hud.js';
import { ATTACKS, createCombatPose, demoCombat } from './combat.js';
import { DUELS, MOVES, createDuel, stepDuel, duelHit, damageRival, demoDuels } from './duel.js';
import { createRivalAppearance } from '../figure/avatar.js';
import { LEVELS, GOAL, KINDS, POWERS, POWER_ORDER, powersFor, loadProgress, saveProgress, demoLevels } from './levels.js';

export const POWER_MAX = 3;
export const POWER_FILL = 42; // seconds from empty to full, inside the asked 30–50s
export const DASH_FILL = 26; // the dash quickens him: the spark comes back sooner too
export const WHIRL = 1.8; // a whirl hit is worth this many plain swings
export const PLUNGE = 1.4; // ...and so is the landing of a plunge, on everyone within reach
const PARRY_MUL = 1.5;
export const HIT = {
  hp: { punk: 60, enforcer: 120, gunner: 70, blade: 150, brute: 330, rival: 120 },
  sword: { punk: 34, enforcer: 30, gunner: 34, blade: 28, brute: 22, rival: 30 },
  bolt: { punk: 80, enforcer: 50, gunner: 80, blade: 45, brute: 40, rival: 32 },
};
const MAX_HP = 100;
// wind: seconds of windup; push: shove on the traveler; cap: capsule [radius, half height].
// shot: a gunner's bolt speed (he fires from `reach` and backs off inside `keep`); lunge: metres a blade closes as it strikes.
const KIND = {
  punk: { dmg: 9, roam: 1.6, chase: 3.6, aggro: 9, reach: 1.55, leash: 18, home: 3.2, scale: 1.1, boltR: 0.8, wind: 0.5, push: 4, cap: [0.32, 0.5] },
  enforcer: { dmg: 16, roam: 1.5, chase: 4.6, aggro: 12, reach: 1.95, leash: 20, home: 4, scale: 1.2, boltR: 0.9, wind: 0.65, push: 6, cap: [0.36, 0.52] },
  gunner: { dmg: 12, roam: 1.4, chase: 3.4, aggro: 16, reach: 11, leash: 22, home: 4, scale: 1.15, boltR: 0.8, wind: 0.95, push: 3, cap: [0.32, 0.5], shot: 15, keep: 5 },
  blade: { dmg: 20, roam: 1.6, chase: 5.2, aggro: 13, reach: 2.1, leash: 22, home: 4, scale: 1.2, boltR: 0.9, wind: 0.6, push: 6, cap: [0.34, 0.52], lunge: 2.4 },
  brute: { dmg: 26, roam: 1.6, chase: 5.2, aggro: 12, reach: 2.6, leash: 20, home: 4.5, scale: 2.2, boltR: 1.9, wind: 0.9, push: 9, cap: [1.0, 0.7] },
};
// Who plays each kind: the file, the one prop kept in hand, and the clip that is its attack.
const CAST = {
  punk: { file: 'punk.gltf', keep: 'Knife_1', attack: 'Punch' },
  enforcer: { file: 'enforcer.gltf', keep: 'Knife_2', attack: 'Punch' },
  gunner: { file: 'gunner.gltf', keep: 'SMG', attack: 'Idle_Shoot' },
  blade: { file: 'blade.glb', attack: 'Sword_Slash' },
  brute: { file: 'mech.glb', attack: 'Attack' },
};
const PROPS = /^(AK|GrenadeLauncher|Knife_\d|Pistol|Revolver(_Small)?|RocketLauncher|ShortCannon|Shotgun|Shovel|SMG|Sniper(_2)?)$/;
// Neon repaints by source material name; every other material keeps its own colour.
const NEON = { Enemy_Red: [1, 0.16, 0.5], Red: [1, 0.16, 0.5], Character_Main: [0.13, 0.32, 0.4], Pants: [0.16, 0.18, 0.26], Hazmat_Main: [0.96, 0.9, 0.12],
  Accent: [0, 0.9, 1], Accent_Dark: [0, 0.42, 0.52], Blade: [1, 0.2, 0.62], Blade_Edge: [1, 0.66, 0.88], Eye: [1, 0.12, 0.25] };
// Knockback in m/s by source; big beasts give less ground.
const KNOCK = { sword: 3.2, thrust: 6.5, heavy: 9, whirl: 7.5, spark: 4, flare: 0 };
const HEFT = { punk: 1, enforcer: 0.75, gunner: 1, blade: 0.7, brute: 0.32 };
const LAUNCH = 6.5; // m/s upward under a heavy blow or a whirl, scaled by HEFT
export const PERFECT = 0.32; // seconds before a strike lands in which a dash counts as perfect
const AIM_LOCK = 0.3; // a gunner stops tracking this long before he fires: the time to step aside
export const PARRY = 0.28; // ...and in which the traveler's own blow turns the strike aside
const PAL = {
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
  const kinds = [...new Set(spots.map((p) => p.kind))];
  const sources = Object.fromEntries(await Promise.all(kinds.map(async (k) => [k, await loader.loadAsync('./enemies/' + CAST[k].file)])));
  const white = new THREE.DataTexture(new Uint8Array([255, 255, 255, 255]), 1, 1);
  white.needsUpdate = true;
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
    const src = sources[kind], cast = CAST[kind];
    const model = SkeletonUtils.clone(src.scene), mesh = new THREE.Group();
    const spare = [];
    model.traverse((o) => { if (PROPS.test(o.name) && o.name !== cast.keep) spare.push(o); });
    for (const o of spare) o.removeFromParent();
    model.updateMatrixWorld(true);
    const bounds = new THREE.Box3().setFromObject(model);
    const normalScale = 1.5 / (bounds.max.y - bounds.min.y);
    model.scale.setScalar(normalScale); model.position.y = -bounds.min.y * normalScale;
    mesh.add(model);
    // Materials belong to one body (same shader, so the same program) so a hit can flash just that one.
    const own = new Map();
    model.traverse((o) => {
      if (!o.isMesh) return;
      const key = o.material.name + (o.isSkinnedMesh ? '' : '|rigid');
      if (!own.has(key)) {
        const tint = NEON[o.material.name] ?? o.material.color.toArray().map((v) => v ** (1 / 2.2));
        const m = paint(shared, PAL.steel, { grain: 2.6, skinned: true, map: white, tint });
        own.set(key, { m, tint: m.uniforms.uTint.value.clone() });
      }
      o.material = own.get(key).m;
      o.frustumCulled = false;
      o.layers.enable(1);
    });
    const named = {};
    for (const clip of src.animations) named[CLIP(clip)] = clip;
    const clips = { Idle: named.Idle_Sword ?? named.Idle, Walk: named.Walk, Run: named.Run, Attack: named[cast.attack], Hit: named.HitReact ?? named.HitRecieve, Death: named.Death };
    const mixer = new THREE.AnimationMixer(mesh);
    const action = mixer.clipAction(clips.Idle);
    action.play();
    Object.assign(mesh.userData, { mixer, clips, action, clip: 'Idle', mats: [...own.values()] });
    mesh.scale.setScalar(KIND[kind].scale);
    return mesh;
  }

  // A gunner's sight: a thin line along his aim while he winds up.
  const aimGeo = new THREE.BoxGeometry(0.03, 0.03, 1).translate(0, 0, 0.5);
  const enemies = spots.map((s) => {
    const mesh = beastMesh(s.kind);
    scene.add(mesh);
    const tell = new THREE.Mesh(new THREE.RingGeometry(0.88, 1, 32), new THREE.MeshBasicMaterial({ color: 0xff2a6d, transparent: true, opacity: 0.7, side: THREE.DoubleSide, depthWrite: false }));
    tell.rotation.x = -Math.PI / 2; tell.visible = false; scene.add(tell);
    let aim = null;
    if (KIND[s.kind].shot) {
      aim = new THREE.Mesh(aimGeo, new THREE.MeshBasicMaterial({ color: 0xff2a6d, transparent: true, opacity: 0.6, depthWrite: false }));
      aim.visible = false; scene.add(aim);
    }
    return { ...s, ...KIND[s.kind], mesh, tell, aim, air: 0, vy: 0, hp: HIT.hp[s.kind], maxHp: HIT.hp[s.kind], homeX: s.x, homeZ: s.z, tx: s.x, tz: s.z, yaw: Math.random() * 6.28, wait: Math.random() * 1.5, mode: 'roam', cool: 0, windup: 0, stagger: 0, phase: Math.random() * 6, flash: 0, dead: 0, gone: false, going: 0, seg: s.seg ?? -1, respawnT: 0, kx: 0, kz: 0 };
  });
  if (segs) {
    const actors = await Promise.all(segs.map(() => createRivalAppearance(man, shared)));
    actors.forEach((actor, index) => {
      const e = { ...createDuel(index), mesh: actor.man.object, actor, scale: 1, flash: 0, stagger: 0, dead: 0, gone: false, respawnT: 0, boltR: 0.65, kx: 0, kz: 0 };
      e.mesh.visible = false;
      e.combat = createCombatPose(actor.man, shared, scene, { rival: true });
      e.tell = new THREE.Mesh(new THREE.RingGeometry(0.94, 1, 48), new THREE.MeshBasicMaterial({ color: 0xef9c77, transparent: true, opacity: 0.8, side: THREE.DoubleSide, depthWrite: false }));
      e.tell.rotation.x = -Math.PI / 2; e.tell.visible = false;
      scene.add(e.mesh, e.tell); enemies.push(e);
    });
  }
  function addBody(e) {
    const [r, half] = e.duel ? [0.3, 0.6] : e.cap;
    e.bodyLift = r + half;
    e.body = player.world.createRigidBody(player.R.RigidBodyDesc.kinematicPositionBased().setTranslation(e.x, heightAt(e.x, e.z) + e.bodyLift, e.z));
    e.collider = player.world.createCollider(player.R.ColliderDesc.capsule(half, r).setFriction(0), e.body);
  }
  for (const e of enemies) if (!e.duel) addBody(e);

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

  const combatPose = createCombatPose(man, shared, scene, { lantern: !segs });

  const bolts = [];
  const flares = [];
  const boltGeo = new THREE.SphereGeometry(0.16, 6, 5);
  const impacts = [];
  const pulses = [];
  const ringGeo = new THREE.RingGeometry(0.8, 1, 32);
  const spellMat = new THREE.MeshBasicMaterial({ color: 0x9dece7 });
  const shotMat = new THREE.MeshBasicMaterial({ color: 0xff2a6d });
  function pulse(x, y, z, color, ground = false) {
    if (pulses.length >= 12) return;
    const material = new THREE.MeshBasicMaterial({ color, transparent: true, opacity: 0.85, side: THREE.DoubleSide, depthWrite: false });
    const mesh = new THREE.Mesh(ringGeo, material);
    mesh.position.set(x, y, z);
    if (ground) mesh.rotation.x = -Math.PI / 2;
    else mesh.lookAt(player.feet.x, player.feet.y + 1, player.feet.z);
    scene.add(mesh); pulses.push({ mesh, t: 0, ground });
  }

  const hud = createHUD({ levels: segs, roam, marks, route });
  let hp = MAX_HP, power = POWER_MAX, swing = -1, swingH = false, struck = new Set(), invuln = 0, won = false;
  let dashCool = 0, flareCool = 0, gateToastT = 0, lockToastT = 0, saveWarned = false, runTold = false;
  let attackBuffer = 0, queuedHeavy = false, comboIndex = -1, comboWindow = 0, swingType = 'cut';
  let hitPause = 0, cameraKick = 0, hurtPose = 0, castPose = 0, slowMo = 0, combo = 0, comboT = 0;
  let saveWarningPending = false, plunge = false;

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
        if (e.seg < n) { scene.remove(e.mesh); if (e.body) player.world.removeRigidBody(e.body); e.body = e.collider = null; e.dead = 1; e.gone = true; }
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
  function toast(msg) { hud.toast(msg); }

  function hurt(e, n, source = 'sword') {
    if (e.dead) return;
    if (e.duel && (!e.ready || e.phase === 'intro' || e.phase === 'waiting')) return;
    const guarded = e.duel && e.phase !== 'recovery';
    if (e.duel) n = damageRival(e, n, source);
    if (guarded && e.phase === 'recovery') toast('Guard broken — strike now!');
    e.hp -= n;
    e.flash = 0.16;
    combo++; comboT = 2.2; hud.combo(combo);
    if (!e.duel) {
      e.stagger = n > 40 ? 0.55 : 0.22; e.windup = 0;
      const k = (KNOCK[source] ?? 3) * (HEFT[e.kind] ?? 1), dx = e.x - player.feet.x, dz = e.z - player.feet.z, d = Math.hypot(dx, dz) || 1;
      e.kx = dx / d * k; e.kz = dz / d * k;
      // a heavy blow takes the light ones off their feet; in the air they cannot answer
      if ((source === 'heavy' || source === 'whirl') && e.air <= 0) e.vy = LAUNCH * HEFT[e.kind];
    }
    e.cool = Math.max(e.cool, 0.45);
    if (!guarded || e.duel && e.phase === 'recovery' || source === 'flare') {
      const big = source === 'heavy' || source === 'whirl' || source === 'thrust';
      hitPause = Math.max(hitPause, (big ? 0.075 : 0.045) + Math.min(combo, 8) * 0.004); cameraKick = big ? 0.12 : 0.055;
    }
    pulse(e.x, heightAt(e.x, e.z) + (e.duel ? 1.1 : 0.8), e.z, guarded ? 0xf3c77b : 0xb8f5ec);
    if (source === 'heavy') pulse(e.x, heightAt(e.x, e.z) + 0.07, e.z, 0xf6d493, true);
    for (let i = 0; i < 5 && impacts.length < 40; i++) {
      const mesh = put(scene, boltGeo, mat('gold'));
      mesh.scale.setScalar(0.35);
      impacts.push({ mesh, x: e.x, y: heightAt(e.x, e.z) + (e.duel ? 1.15 : 0.75), z: e.z, vx: (Math.random() - 0.5) * 9, vy: 2 + Math.random() * 3, vz: (Math.random() - 0.5) * 9, life: 0.3 });
    }
    if (e.hp <= 0) {
      e.dead = 0.001; e.tell.visible = false; power = Math.min(POWER_MAX, power + 0.35);
      player.world.removeRigidBody(e.body); e.body = e.collider = null;
      if (!e.duel) { e.kx *= 1.7; e.kz *= 1.7; e.vy = Math.max(e.vy, 3.5 * HEFT[e.kind]); } // the fallen are thrown
      if (e.duel) { toast(`${e.name} — lesson mastered`); hitPause = 0.12; cameraKick = 0.16; }
    }
  }

  function takeHit(damage, from = null, shove = 5) {
    if (invuln > 0) return false;
    hp -= damage; invuln = 0.7; hurtPose = 1; hitPause = 0.07; cameraKick = 0.16;
    combo = 0; hud.combo(0);
    if (from) player.knock(player.feet.x - from.x, player.feet.z - from.z, shove);
    if (hp <= 0) die();
    return true;
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
      if (e.duel) { Object.assign(e, createDuel(e.seg)); continue; }
      if (Math.hypot(e.x - player.feet.x, e.z - player.feet.z) < 5) {
        e.x = e.homeX; e.z = e.homeZ; e.mode = 'roam'; e.tx = e.homeX; e.tz = e.homeZ;
        e.windup = e.stagger = e.cool = e.air = e.vy = e.kx = e.kz = 0;
      }
    }
    plunge = false;
    for (const b of bolts) if (b.foe) b.life = 0;
    toast('Back to the last checkpoint');
  }

  function playClip(e, name) {
    if (e.mesh.userData.clip === name) return;
    const u = e.mesh.userData;
    const next = u.mixer.clipAction(u.clips[name]);
    if (name === 'Death' || name === 'Attack') { next.setLoop(THREE.LoopOnce, 1); next.clampWhenFinished = true; }
    else next.setLoop(THREE.LoopRepeat);
    next.reset().fadeIn(0.15).play();
    u.action.fadeOut(0.15);
    u.action = next;
    u.clip = name;
  }

  function lay(e, dt) {
    if (e.duel) {
      e.mesh.visible = !e.gone && (e.ready || !!e.dead) && Math.hypot(e.x - player.feet.x, e.z - player.feet.z) < 36;
      e.combat.trail.visible = e.mesh.visible;
      if (!e.mesh.visible) { e.tell.visible = false; return; }
      e.mesh.position.set(e.x, heightAt(e.x, e.z), e.z); e.mesh.rotation.set(0, e.yaw, 0);
      if (e.body) e.body.setTranslation({ x: e.x, y: heightAt(e.x, e.z) + e.bodyLift, z: e.z }, true);
      e.actor.update(dt, e.going, !!e.dead);
      if (e.dead) e.actor.man.bones.spine_02.rotation.x += 0.3;
      e.combat.apply(dt, { u: e.dead ? -1 : e.attackU, type: MOVES[e.move].pose, hurt: e.flash * 3 });
      e.actor.avatar.update();
      e.tell.visible = !e.dead && e.ready && e.phase !== 'waiting';
      e.tell.position.set(e.x, heightAt(e.x, e.z) + 0.04, e.z);
      e.tell.scale.setScalar(MOVES[e.move].reach);
      e.tell.material.color.setHex(e.phase === 'recovery' ? 0x8de8cc : 0xef9c77);
      e.tell.material.opacity = e.phase === 'windup' ? 0.4 + 0.5 * (1 - e.timer / MOVES[e.move].windup) : e.phase === 'recovery' ? 0.55 : 0.15;
      return;
    }
    const u = e.mesh.userData;
    for (const { m, tint } of u.mats) m.uniforms.uTint.value.copy(tint).multiplyScalar(1 + e.flash * 14);
    const sc = e.scale * (e.flash > 0 ? 1.06 : 1);
    e.mesh.scale.set(sc, sc * (e.flash > 0 ? 0.86 : 1), sc);
    e.mesh.position.set(e.x, heightAt(e.x, e.z) + e.air - (e.dead ? Math.min(0.4, Math.max(0, e.dead - 1.2) * 0.45) : 0), e.z);
    e.mesh.rotation.set(e.air > 0 ? -0.5 * Math.min(1, e.air) : e.windup > 0 ? -Math.sin(e.windup * 8) * 0.04 : 0, e.yaw, 0);
    if (e.body) e.body.setTranslation({ x: e.x, y: heightAt(e.x, e.z) + e.air + e.bodyLift, z: e.z }, true);
    if (e.aim) {
      // steady while he tracks, flickering once the line is locked
      e.aim.visible = !e.dead && e.windup > 0;
      e.aim.position.set(e.x, heightAt(e.x, e.z) + 1.25, e.z); e.aim.rotation.y = e.attackYaw ?? 0; e.aim.scale.z = e.reach + 2;
      e.aim.material.opacity = e.windup < AIM_LOCK ? 0.55 + 0.45 * Math.sin(e.windup * 70) : 0.35;
    }
    e.tell.visible = !e.dead && e.windup > 0 && !e.shot;
    e.tell.position.set(e.x, heightAt(e.x, e.z) + 0.04, e.z);
    e.tell.scale.setScalar(e.reach * (0.6 + e.windup * 0.4));
    const name = e.dead ? 'Death' : e.flash > 0 && u.clips.Hit ? 'Hit' : e.windup > 0 ? 'Attack' : e.going > 2.4 ? 'Run' : e.going > 0.25 ? 'Walk' : 'Idle';
    playClip(e, name);
    e.mesh.userData.mixer.update(dt);
  }

  function pose() {
    if (swingType === 'spin' && swing >= 0) {
      const k = Math.min(1, swing / 0.9);
      man.object.rotateY(-k * k * (3 - 2 * k) * Math.PI * 2);
      man.object.updateMatrixWorld(true);
    }
    combatPose.apply(1 / 60, { u: swing, type: swingType, hurt: hurtPose, cast: castPose });
  }

  /** Moves a body up to `dist` along the unit (ux, uz), stopped by walls and trees like walking. */
  function sweep(e, ux, uz, dist) {
    const y = heightAt(e.x, e.z) + e.air + 0.6;
    const hit = player.castRay({ x: e.x, y, z: e.z }, { x: ux, y: 0, z: uz }, dist + 0.6, e.collider ?? undefined);
    const go = hit == null ? dist : Math.max(0, hit - 0.6);
    e.x += ux * go; e.z += uz * go;
  }

  /** Knockback and the fall after a launch, for the living and the just-fallen alike. */
  function shove(e, dt) {
    const kd = Math.hypot(e.kx, e.kz);
    if (kd > 0.05) {
      sweep(e, e.kx / kd, e.kz / kd, kd * dt);
      const fade = Math.exp(-(e.air > 0 ? 2.5 : 7) * dt); e.kx *= fade; e.kz *= fade; // nothing to grip in the air
    } else e.kx = e.kz = 0;
    if (e.air > 0 || e.vy > 0) {
      e.vy -= 22 * dt; e.air += e.vy * dt;
      if (e.air <= 0) { e.air = e.vy = 0; pulse(e.x, heightAt(e.x, e.z) + 0.06, e.z, 0x00f0ff, true); }
      else if (!e.dead) { e.stagger = Math.max(e.stagger, 0.15); e.windup = 0; }
    }
  }

  function fire(e) {
    const sx = Math.sin(e.attackYaw), sz = Math.cos(e.attackYaw);
    const mesh = put(scene, new THREE.SphereGeometry(0.12, 6, 5), shotMat);
    mesh.scale.set(1, 1, 3.2); mesh.rotation.y = e.attackYaw;
    const y = heightAt(e.x, e.z) + 1.25, flight = Math.hypot(player.feet.x - e.x, player.feet.z - e.z) / e.shot;
    bolts.push({ mesh, foe: e.dmg, g: 0, life: 1.3, x: e.x + sx * 0.7, y, z: e.z + sz * 0.7,
      vx: sx * e.shot, vy: Math.max(-4, Math.min(4, (player.feet.y + 1.05 - y) / Math.max(flight, 0.2))), vz: sz * e.shot });
  }

  const swingMul = () => swingH ? 3 : swingType === 'spin' ? WHIRL : swingType === 'thrust' ? 1.3 : 1;
  const swingSource = () => swingH ? 'heavy' : swingType === 'spin' ? 'whirl' : swingType === 'thrust' ? 'thrust' : 'sword';

  function update(dt, input, realDt = dt) {
    hitPause = Math.max(0, hitPause - realDt);
    slowMo = Math.max(0, slowMo - realDt);
    if ((comboT -= realDt) <= 0 && combo) { combo = 0; hud.combo(0); }
    cameraKick = Math.max(0, cameraKick - realDt * 0.7);
    hurtPose = Math.max(0, hurtPose - dt * 5); castPose = Math.max(0, castPose - dt * 3);
    comboWindow = Math.max(0, comboWindow - dt);
    invuln = Math.max(0, invuln - dt);
    dashCool = Math.max(0, dashCool - dt);
    flareCool = Math.max(0, flareCool - dt);
    gateToastT = Math.max(0, gateToastT - dt);
    lockToastT = Math.max(0, lockToastT - dt);
    attackBuffer = Math.max(0, attackBuffer - dt);
    for (const m of marks) { m.crystal.rotation.y += dt; m.crystal.position.y = 1.5 + Math.sin(m.crystal.rotation.y * 1.8) * 0.12; }
    const px = player.feet.x, pz = player.feet.z;
    let yaw = player.yaw;
    const c = segs ? clearedCount() : 0;
    const closestRival = roam ? enemies.filter(e => e.duel && !e.dead).sort((a, b) => Math.hypot(a.x - px, a.z - pz) - Math.hypot(b.x - px, b.z - pz))[0] : null;
    // A closed gate stands between two z values: neither side's teeth reach through.
    const gated = (ez) => segs && !roam && !won && gates.some((g) => c <= g.seg && (ez - g.group.position.z) * (pz - g.group.position.z) < 0);
    if (input?.attack || input?.heavy && (!segs || has('heavy'))) {
      attackBuffer = 0.55;
      queuedHeavy = !!(segs && input?.heavy && has('heavy'));
    }
    if (input?.whirl && segs && !has('whirl') && lockToastT <= 0) {
      lockToastT = 4; toast('Clear Chapter 5 to learn the whirl');
    } else if (input?.whirl && has('whirl') && hp > 0 && (swing < 0 || swing > 0.6)) {
      if (power >= 1) {
        power -= 1; swingType = 'spin'; swing = 0; swingH = false; struck = new Set(); attackBuffer = 0; comboWindow = 0;
        pulse(px, heightAt(px, pz) + 0.06, pz, 0xc9a8ff, true);
      } else if (lockToastT <= 0) { lockToastT = 3; toast('The whirl needs one spark'); }
    }
    if (swing < 0 && attackBuffer > 0 && hp > 0) {
      comboIndex = comboWindow > 0 ? (comboIndex + 1) % 3 : 0;
      swingType = queuedHeavy ? 'heavy' : ['cut', 'backhand', 'thrust'][comboIndex];
      swing = 0; swingH = queuedHeavy; struck = new Set(); attackBuffer = 0;
      // A strike in the air is a plunge: he drops on the blade and the landing does the rest.
      plunge = !player.grounded && player.feet.y - heightAt(px, pz) > 0.7;
      if (plunge) { swingType = 'heavy'; swingH = false; player.slam(26); }
      const target = plunge ? null : enemies.filter(e => !e.dead && (!e.duel || e.ready) && !gated(e.z) && inFront(px, pz, yaw, e.x, e.z, 3.6, 1.8)).sort((a, b) => Math.hypot(a.x - px, a.z - pz) - Math.hypot(b.x - px, b.z - pz))[0];
      if (target) {
        const turn = Math.atan2(Math.sin(Math.atan2(target.x - px, target.z - pz) - yaw), Math.cos(Math.atan2(target.x - px, target.z - pz) - yaw)); yaw = player.yaw += Math.max(-0.7, Math.min(0.7, turn));
        // close the gap a knockback opened: a short swept lunge to striking distance
        const gap = Math.hypot(target.x - px, target.z - pz) - (target.kind === 'brute' ? 2.1 : 1.5);
        if (gap > 0.1) player.knock(target.x - px, target.z - pz, Math.min(gap, 1.6) * 8.4);
      }
    }
    if (input?.heavy && segs && !has('heavy') && lockToastT <= 0) {
      lockToastT = 4;
      toast('Clear Chapter 3 to learn the heavy swing');
    }
    if (swing >= 0) { swing += dt / ATTACKS[swingType].duration; if (swing >= 1) { swing = -1; swingH = false; comboWindow = 0.7; } }
    const hitting = swing >= ATTACKS[swingType].hit[0] && swing <= ATTACKS[swingType].hit[1];
    if (plunge && player.grounded) {
      plunge = false; cameraKick = Math.max(cameraKick, 0.14);
      pulse(px, heightAt(px, pz) + 0.07, pz, 0xfcee0a, true);
      for (const e of enemies) {
        if (e.dead || e.gone || e.duel && !e.ready || gated(e.z) || Math.hypot(e.x - px, e.z - pz) > 3.4) continue;
        struck.add(e); hurt(e, PLUNGE * HIT.sword[e.kind], 'heavy');
      }
    }

    // A dash: one quick step where he faces, too fast to be bitten.
    if (input?.dash && has('dash') && dashCool <= 0 && hp > 0) {
      if (player.startDash()) {
        dashCool = 2.5; invuln = Math.max(invuln, 0.3);
        // Perfect: a strike was about to land on him.
        const close = enemies.some((e) => !e.dead && !e.gone && !gated(e.z) && (e.duel
          ? (e.phase === 'windup' && e.timer < PERFECT || e.phase === 'active') && Math.hypot(e.x - px, e.z - pz) < MOVES[e.move].reach + 1
          : e.windup > 0 && e.windup < PERFECT && Math.hypot(e.x - px, e.z - pz) < e.reach + 1));
        if (close) {
          slowMo = 0.7; invuln = Math.max(invuln, 0.6); power = Math.min(POWER_MAX, power + 1);
          pulse(px, player.feet.y + 1, pz, 0xffffff); hud.flashText('Perfect dodge');
        }
      }
    } else if (input?.dash && !has('dash') && lockToastT <= 0) {
      lockToastT = 4; toast('Clear Chapter 2 to learn the dash');
    }

    for (const e of enemies) {
      e.flash = Math.max(0, e.flash - dt);
      e.stagger = Math.max(0, e.stagger - dt);
      if (e.gone) {
        // In free roam the fallen come back after a while, at home and calm.
        if (roam && (e.respawnT -= dt) <= 0) {
          e.hp = e.maxHp; e.dead = 0; e.gone = false;
          e.x = e.homeX; e.z = e.homeZ; e.tx = e.homeX; e.tz = e.homeZ;
          e.mode = 'roam'; e.cool = e.windup = e.stagger = e.air = e.vy = e.kx = e.kz = 0; e.wait = 1; e.mesh.visible = true;
          if (e.duel) Object.assign(e, createDuel(e.seg));
          if (!e.duel) addBody(e);
        } else continue;
      }
      if (e.dead) {
        e.dead += dt;
        e.going = 0;
        shove(e, dt);
        lay(e, dt);
        if (e.dead > 2.2) {
          if (roam) { e.gone = true; e.respawnT = 20; e.mesh.visible = false; }
          else { scene.remove(e.mesh); e.gone = true; }
        }
        continue;
      }
      shove(e, dt);
      const dxp = px - e.x, dzp = pz - e.z, pd = Math.hypot(dxp, dzp);
      const homeD = Math.hypot(e.x - e.homeX, e.z - e.homeZ);
      // Far beasts keep their pose but skip the thinking: the frame stays cheap.
      if (pd > 36) { e.going = 0; e.cool = Math.max(0, e.cool - dt); lay(e, 0); continue; }
      if (e.duel) {
        const ready = roam ? e === closestRival : !enemies.some(b => !b.duel && b.seg === e.seg && !b.dead);
        if (ready && !e.body) addBody(e);
        if (!ready && e.body) { player.world.removeRigidBody(e.body); e.body = e.collider = null; }
        const event = stepDuel(e, dt, { x: px, z: pz, ready, move(dx, dz) {
          const distance = Math.hypot(dx, dz); if (distance < 1e-6) return;
          const hit = player.castRay({ x: e.x, y: heightAt(e.x, e.z) + 0.9, z: e.z }, { x: dx / distance, y: 0, z: dz / distance }, distance + 0.5, e.collider);
          const amount = Math.min(distance, Math.max(0, (hit ?? distance + 0.5) - 0.5)) / distance;
          e.x += dx * amount; e.z += dz * amount;
        } });
        if (event === 'intro') {
          // A lesson retries at its entrance, with the resources needed to practise it.
          moveSpawn(e.homeX, e.homeZ + 6); hp = MAX_HP; power = POWER_MAX;
          pulse(e.x, heightAt(e.x, e.z) + 0.05, e.z, 0xa8efdc, true); toast(`${e.name} · ${e.lesson}`);
        }
        if (event === 'reset') toast('Duel reset. Return to the shrine when you are ready.');
        if (event === 'opening') toast('Opening — strike now!');
        if (duelHit(e, px, player.feet.y, pz, heightAt(e.x, e.z)) && !gated(e.z)) {
          if (takeHit(MOVES[e.move].damage, e, e.move === 'slam' ? 8 : 5.5)) e.hitDone = true;
        }
        if (hitting && !struck.has(e) && !gated(e.z) && inFront(px, pz, yaw, e.x, e.z, ATTACKS[swingType].reach, swingType === 'spin' ? Math.PI : 1.3)) {
          struck.add(e); hurt(e, swingMul() * HIT.sword.rival, swingSource());
        }
        lay(e, dt); continue;
      }
      if (e.mode === 'roam' && pd < e.aggro && !gated(e.z)) e.mode = 'chase';
      if (e.mode === 'chase' && (pd > e.aggro + 5 || homeD > e.leash)) {
        e.mode = 'roam'; e.tx = e.homeX; e.tz = e.homeZ; e.wait = 0;
      }
      let speed = 0;
      if (e.mode === 'chase') {
        e.tx = px; e.tz = pz; speed = pd > e.reach * 0.83 ? e.chase : 0;
        if (e.keep && pd < e.keep) { e.tx = e.x - dxp; e.tz = e.z - dzp; speed = e.chase; } // rushed, a gunner gives ground
      }
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
      } else if (e.shot && e.mode === 'chase' && e.windup <= 0) e.yaw = Math.atan2(dxp, dzp);
      e.phase += dt * (3 + e.going * 2);
      e.cool = Math.max(0, e.cool - dt);
      const near = Math.hypot(px - e.x, pz - e.z);
      if (e.windup > 0) {
        if (e.shot && e.windup > AIM_LOCK) e.yaw = e.attackYaw = Math.atan2(px - e.x, pz - e.z);
        if (e.lunge && e.windup < 0.2) sweep(e, Math.sin(e.attackYaw), Math.cos(e.attackYaw), e.lunge / 0.2 * dt);
        e.windup = Math.max(0, e.windup - dt);
        if (e.windup === 0 && e.shot) { if (hp > 0 && !gated(e.z)) fire(e); }
        else if (e.windup === 0 && invuln <= 0 && hp > 0 && !gated(e.z) && strikeHits(e, px, player.feet.y, pz, heightAt(e.x, e.z))) {
          takeHit(e.dmg, e, e.push);
        }
        if (e.windup === 0 && e.kind === 'brute') { pulse(e.x, heightAt(e.x, e.z) + 0.06, e.z, 0xff9f7a, true); cameraKick = Math.max(cameraKick, 0.06); }
      } else if (e.mode === 'chase' && near < e.reach + 0.25 + (e.lunge ?? 0) && e.cool <= 0 && e.stagger <= 0 && hp > 0 && !gated(e.z)) {
        e.windup = e.wind;
        e.attackYaw = Math.atan2(px - e.x, pz - e.z);
        e.yaw = e.attackYaw;
        e.cool = e.windup + 1.0;
      }
      if (hitting && !struck.has(e) && !gated(e.z) && Math.abs(player.feet.y - heightAt(e.x, e.z)) < 1.7 && inFront(px, pz, yaw, e.x, e.z, ATTACKS[swingType].reach, swingType === 'spin' ? Math.PI : swingH ? 1.6 : 1.2)) {
        struck.add(e);
        // A blow landed in the last moment of his windup turns the strike aside.
        const parry = !e.shot && e.windup > 0 && e.windup < PARRY;
        hurt(e, swingMul() * HIT.sword[e.kind] * (parry ? PARRY_MUL : 1), swingSource());
        if (parry && !e.dead) {
          e.stagger = 1.2; power = Math.min(POWER_MAX, power + 0.5); slowMo = Math.max(slowMo, 0.25);
          hud.flashText('Parry');
        }
      }
      lay(e, dt);
    }
    for (let i = 0; i < enemies.length; i++) {
      for (let j = i + 1; j < enemies.length; j++) {
        const a = enemies[i], b = enemies[j];
        if (a.dead || b.dead || a.duel || b.duel || Math.hypot(a.x - px, a.z - pz) > 36 || Math.hypot(b.x - px, b.z - pz) > 36) continue;
        const minD = 0.45 * (a.scale + b.scale); // the mech needs more room than a punk
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
          castPose = 1;
          const mesh = put(scene, new THREE.OctahedronGeometry(0.17), spellMat);
          const halo = new THREE.Mesh(new THREE.TorusGeometry(0.25, 0.018, 4, 16), spellMat); halo.rotation.y = Math.PI / 2; mesh.add(halo);
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
          hurt(e, 60, 'flare');
          const push = Math.max(0, 3 - d * 0.25) || 1.5;
          if (!e.duel) { e.x += ((e.x - px) / (d || 1)) * push; e.z += ((e.z - pz) / (d || 1)) * push; }
          e.cool = Math.max(e.cool, 2);
          e.mode = 'roam'; e.tx = e.homeX; e.tz = e.homeZ; e.wait = 1;
        }
        const mesh = new THREE.Mesh(ringGeo, new THREE.MeshBasicMaterial({ color: 0xf5ce84, transparent: true, opacity: 0.8, side: THREE.DoubleSide, depthWrite: false }));
        mesh.rotation.x = -Math.PI / 2; scene.add(mesh);
        flares.push({ mesh, t: 0, x: px, y: player.feet.y + 1.3, z: pz });
      } else if (lockToastT <= 0) {
        lockToastT = 4;
        toast('The flare needs two spark');
      }
    }
    for (let i = flares.length - 1; i >= 0; i--) {
      const f = flares[i];
      f.t += dt;
      f.mesh.position.set(f.x, heightAt(f.x, f.z) + 0.08, f.z);
      f.mesh.scale.setScalar(1 + f.t * 30);
      f.mesh.material.opacity = Math.max(0, 0.8 * (1 - f.t / 0.5));
      if (f.t > 0.5) { scene.remove(f.mesh); f.mesh.material.dispose(); flares.splice(i, 1); }
    }
    for (let i = pulses.length - 1; i >= 0; i--) {
      const p = pulses[i]; p.t += dt;
      p.mesh.scale.setScalar(0.08 + p.t * (p.ground ? 9 : 3));
      p.mesh.material.opacity = Math.max(0, 0.85 * (1 - p.t / 0.32));
      if (p.t > 0.32) { scene.remove(p.mesh); p.mesh.material.dispose(); pulses.splice(i, 1); }
    }
    for (let i = impacts.length - 1; i >= 0; i--) {
      const p = impacts[i]; p.life -= dt; p.vy -= 15 * dt;
      p.x += p.vx * dt; p.y += p.vy * dt; p.z += p.vz * dt;
      p.mesh.position.set(p.x, p.y, p.z); p.mesh.scale.setScalar(Math.max(0, p.life));
      if (p.life <= 0) { scene.remove(p.mesh); impacts.splice(i, 1); }
    }
    for (let i = bolts.length - 1; i >= 0; i--) {
      const b = bolts[i];
      b.life -= dt; b.vy -= (b.g ?? 7) * dt;
      const dx = b.vx * dt, dy = b.vy * dt, dz = b.vz * dt, length = Math.hypot(dx, dy, dz);
      const collision = length > 0 ? player.castRay({ x: b.x, y: b.y, z: b.z }, { x: dx / length, y: dy / length, z: dz / length }, length) : null;
      const fraction = collision == null ? 1 : collision / length;
      b.x += dx * fraction; b.y += dy * fraction; b.z += dz * fraction;
      if (!b.g) b.mesh.rotation.y = Math.atan2(b.vx, b.vz); else b.mesh.rotation.z += dt * 9;
      b.mesh.position.set(b.x, b.y, b.z);
      let gone = b.life <= 0 || b.y < heightAt(b.x, b.z) + 0.25;
      if (!gone && b.foe) {
        const near = Math.hypot(b.x - px, b.z - pz);
        if (hitting && near < 2.4 && inFront(px, pz, yaw, b.x, b.z, 2.4, 1.6, 0.9)) {
          // A swing in time sends the shot back the way it came, as his own.
          b.vx *= -1.5; b.vz *= -1.5; b.vy = 0; b.foe = 0; b.life = 1.2; b.mesh.material = spellMat;
          power = Math.min(POWER_MAX, power + 0.25); hitPause = Math.max(hitPause, 0.05);
          pulse(b.x, b.y, b.z, 0x00f0ff); hud.flashText('Deflected');
        } else if (near < 0.55 && Math.abs(b.y - player.feet.y - 1) < 1.1 && hp > 0) {
          gone = takeHit(b.foe, { x: b.x - b.vx, z: b.z - b.vz }, 3); // a dash goes straight through it
        }
      } else if (!gone) {
        for (const e of enemies) {
          if (e.dead || gated(e.z)) continue;
          if (Math.abs(b.y - heightAt(e.x, e.z) - e.bodyLift) < e.bodyLift + 0.3 && Math.hypot(b.x - e.x, b.z - e.z) < (e.boltR ?? 1.05)) {
            hurt(e, HIT.bolt[e.kind], 'spark');
            gone = true;
            break;
          }
        }
      }
      if (collision != null) gone = true;
      if (gone) { scene.remove(b.mesh); b.mesh.geometry.dispose(); for (const child of b.mesh.children) child.geometry.dispose(); bolts.splice(i, 1); }
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
    const boss = enemies.find(e => e.duel && !e.dead && e.ready && e.phase !== 'waiting' && Math.hypot(e.x - player.feet.x, e.z - player.feet.z) < 18);
    hud.update(dt, { hp, power, cleared: c, won, enemies, player, has, dashCool, flareCool, invuln, boss });
  }

  return { update, pose, enemies, get hp() { return hp; }, get power() { return power; },
    get seg() { return segs ? clearedCount() : 0; }, get powers() { return { ...powers }; },
    get roam() { return roam; }, get won() { return won; }, get hitPause() { return hitPause; }, get cameraKick() { return cameraKick; },
    /** How fast the world runs this frame: hit-stop, then the perfect-dodge slow motion. */
    get timeScale() { return hitPause > 0 ? 0.12 : slowMo > 0 ? 0.35 : 1; },
    get combo() { return swingType; }, get chain() { return combo; } };
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
  if (HIT.hp.punk - HIT.bolt.punk > 0) throw new Error('spark should drop a punk');
  if (HIT.hp.enforcer - HIT.bolt.enforcer <= 0) throw new Error('spark should not drop an enforcer');
  if (HIT.hp.punk - 2 * HIT.sword.punk > 0) throw new Error('two swings drop a punk');
  if (HIT.hp.enforcer - 3 * HIT.sword.enforcer <= 0) throw new Error('an enforcer takes more than three swings');
  if (HIT.hp.brute <= HIT.hp.blade || HIT.hp.blade <= HIT.hp.enforcer) throw new Error('brute outlasts blade outlasts enforcer');
  if (HIT.hp.brute - 5 * (3 * HIT.sword.brute) > 0) throw new Error('five heavy swings drop a brute');
  if (!(KIND.brute.scale > KIND.enforcer.scale)) throw new Error('a brute looms over an enforcer');
  if (HIT.hp.punk - WHIRL * HIT.sword.punk > 0 || HIT.hp.enforcer - WHIRL * HIT.sword.enforcer <= 0) throw new Error('a whirl clears punks, not enforcers');
  if (HIT.hp.brute - 5 * (WHIRL * HIT.sword.brute) <= 0) throw new Error('a brute outlasts five whirls');
  if (!(KNOCK.heavy * HEFT.brute < KNOCK.sword * HEFT.punk)) throw new Error('a brute barely moves under a heavy swing');
  for (const k of KINDS) {
    if (!KIND[k] || !CAST[k] || !HEFT[k] || !HIT.hp[k] || !HIT.sword[k] || !HIT.bolt[k]) throw new Error('every kind is cast and statted: ' + k);
    if (!(PARRY < KIND[k].wind)) throw new Error('a parry window fits inside the windup: ' + k);
    if (PARRY_MUL * HIT.sword[k] * 3 >= HIT.hp.brute) throw new Error('a parry is a bonus, not a kill');
  }
  if (HIT.hp.gunner - HIT.bolt.gunner > 0) throw new Error('his own shot, cut back, drops a gunner');
  if (!(KIND.gunner.keep < KIND.gunner.reach && AIM_LOCK < KIND.gunner.wind)) throw new Error('a gunner holds range and tracks before he locks');
  // sidestepping at a run during the lock and the flight clears the shot
  if (!(4 * (AIM_LOCK + KIND.gunner.keep / KIND.gunner.shot) > 0.55 + 0.5)) throw new Error('a shot must be dodgeable');
  if (!(LAUNCH * HEFT.brute < 2.2 && LAUNCH * HEFT.punk > 5)) throw new Error('punks fly, the mech stays down');
  if (!(PLUNGE * HIT.sword.punk < HIT.hp.punk)) throw new Error('a plunge alone does not clear a crew');
  if (!(PERFECT > 0 && PERFECT < 0.5)) throw new Error('perfect-dodge window');
  demoLevels();
  demoDuels(); demoCombat();
  const earned = powersFor(LEVELS.length);
  for (const k of POWER_ORDER) if (!earned[k]) throw new Error('all powers by the clearing: ' + k);
}

if (globalThis.process?.argv?.[1]?.endsWith('hunt.js')) demo();
