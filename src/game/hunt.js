// Street crews on the path, a sword in the left hand (the right keeps the lantern at night),
// and a spark you can throw three times. Empty, it fills again in POWER_FILL seconds.
// The crews and street folk are authored Blender rigs (public/street): punks, enforcers, gunners,
// razor blades, a siege mech, and four citizen archetypes plus hooded couriers, painted through the
// shared cel shader (with glowing Signal accents and per-body uTint so a hit flashes one body).
// With `levels` (src/game/levels.js) the way becomes six gated levels: clearing a level's
// crew opens its gate and grants one power (spark, dash, heavy swing, lantern flare, whirl), and
// each level's cairn is the checkpoint you return to. With `roam` every gate stands open,
// every power is unlocked and the fallen come back.
// Feel: every body flashes white and is knocked back along the blow (swept, never through walls);
// heavy blows lift the light ones off their feet and the fallen are thrown. A hit shoves the traveler too.
// A dash in the last moment of a windup is a perfect dodge: time slows, a spark comes back. A strike
// landed in that same moment is a parry. Gunners' shots can be cut back at them. A strike in the air
// is a plunge that shakes everyone near where he lands. Hits chain into a combo count the HUD shows.
// The street has people on it too (level mode): citizens strolling the pavement and hooded runners. They are
// bodies like any other, so a blade, a spark or a bumper (ride.js calls ram()) hurts them the same, and that
// is the risk: hurting a citizen costs him health, killing one turns every citizen near on him, while a
// runner who was a thief costs nothing. Two runners in three are thieves; the loot at the hip tells which.
import * as THREE from 'three';
import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';
import * as SkeletonUtils from 'three/addons/utils/SkeletonUtils.js';
import { paint } from '../figure/figure.js';
import { createHUD } from './hud.js';
import { ATTACKS, createCombatPose, demoCombat } from './combat.js';
import { DUELS, MOVES, ARENA_RADIUS, createDuel, stepDuel, duelHit, damageRival, duelVisual, demoDuels } from './duel.js';
import { createRivalAppearance } from '../figure/avatar.js';
import { LEVELS, GOAL, KINDS, POWERS, POWER_ORDER, STORY, powersFor, unlocksFor, loadProgress, saveProgress, demoLevels } from './levels.js';
import { loadStreetAsset, streetClone } from '../street-assets.js';
import { nextPavement } from '../world/grid.js';
import { GUILT, CASH, MOB, WITNESS, RAM, HEAT, demoConfig } from './config.js';
export { GUILT, MOB, RAM, HEAT, CASH, WITNESS };

export const POWER_MAX = 3;
export const POWER_FILL = 42; // seconds from empty to full, inside the asked 30–50s
export const DASH_FILL = 26; // the dash quickens him: the spark comes back sooner too
export const WHIRL = 1.8; // a whirl hit is worth this many plain swings
export const PLUNGE = 1.4; // ...and so is the landing of a plunge, on everyone within reach
const PARRY_MUL = 1.5;
export const HIT = {
  hp: { punk: 60, enforcer: 120, gunner: 70, blade: 150, brute: 330, rival: 120, civ: 50, thief: 50 },
  sword: { punk: 34, enforcer: 30, gunner: 34, blade: 28, brute: 22, rival: 30, civ: 30, thief: 30 },
  bolt: { punk: 80, enforcer: 50, gunner: 80, blade: 45, brute: 40, rival: 32, civ: 60, thief: 60 },
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
// The street's own people. civ marks a bystander body; aggro is 0 until a killing turns the street (`angry`).
Object.assign(KIND, {
  civ: { civ: true, dmg: 7, roam: 1.3, chase: 4.1, aggro: 0, reach: 1.5, leash: 45, home: 3, scale: 1.08, boltR: 0.8, wind: 0.55, push: 3, cap: [0.3, 0.5] },
  thief: { civ: true, dmg: 7, roam: 4.5, chase: 4.5, aggro: 0, reach: 1.5, leash: 90, home: 3, scale: 1.05, boltR: 0.8, wind: 0.55, push: 3, cap: [0.3, 0.5] },
});
/** Which band a hit of `force` (m/s, times mass against a sedan) lands in. Crews are not banded; bystanders are. */
export function ramBand(force) {
  if (force < RAM.min) return 'none';
  if (force < RAM.stumble) return 'stumble';
  if (force < RAM.kill) return 'knock';
  return 'kill';
}
// Who plays each kind: the file, the one prop kept in hand, and the clip that is its attack.
const CAST = {
  punk: { file: '../street/foe-punk.glb', attack: 'Punch' },
  enforcer: { file: '../street/foe-enforcer.glb', attack: 'Punch' },
  gunner: { file: '../street/foe-gunner.glb', attack: 'Idle_Shoot' },
  blade: { file: '../street/foe-blade.glb', attack: 'Sword_Slash' },
  brute: { file: '../street/foe-mech.glb', attack: 'Attack' },
  civ: { file: '../street/resident.glb', alternates: ['../street/mechanic.glb', '../street/executive.glb', '../street/vendor.glb'], attack: 'Punch' },
  thief: { file: '../street/courier.glb', attack: 'Punch' },
};
const SIGNAL_PAL = [[1.15, 1.15, 1.15], [1.0, 1.0, 1.0], [0.85, 0.85, 0.85]];
const CIV_TINTS = [[1, 1, 1], [0.92, 0.98, 1.08], [1.06, 0.95, 0.90], [0.94, 1.05, 0.96], [1.04, 0.94, 1.06]];
const PROPS = /^(AK|GrenadeLauncher|Knife_\d|Pistol|Revolver(_Small)?|RocketLauncher|ShortCannon|Shotgun|Shovel|SMG|Sniper(_2)?)$/;
// Neon repaints by source material name; every other material keeps its own colour.
const NEON = { Enemy_Red: [1, 0.16, 0.5], Red: [1, 0.16, 0.5], Character_Main: [0.13, 0.32, 0.4], Pants: [0.16, 0.18, 0.26], Hazmat_Main: [0.96, 0.9, 0.12],
  Accent: [0, 0.9, 1], Accent_Dark: [0, 0.42, 0.52], Blade: [1, 0.2, 0.62], Blade_Edge: [1, 0.66, 0.88], Eye: [1, 0.12, 0.25] };
// Knockback in m/s by source; big beasts give less ground.
const KNOCK = { sword: 3.2, thrust: 6.5, heavy: 9, whirl: 7.5, spark: 4, flare: 0 };
const HEFT = { punk: 1, enforcer: 0.75, gunner: 1, blade: 0.7, brute: 0.32, civ: 1.1, thief: 1.1 };
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

export async function createHunt({ scene, shared, man, animator, player, heightAt, marks, spots, moveSpawn, down, levels = null, roam = false, route = null, camera = null, net = null, events = false, audio = null, missions = null }) {
  // Level mode builds its own marks and mission relays from the level list; the night
  // scene passes marks and spots directly, which keeps working as before.
  const segs = levels ? levels.map((L) => ({ ...L })) : null;
  if (segs) {
    marks = [...segs.map((s) => ({ ...s.checkpoint })), { ...GOAL, goal: true }];
    spots = segs.flatMap((s, i) => s.spots.map((p) => ({ ...p, seg: i })));
  }
  const loader = new GLTFLoader();
  const kinds = [...new Set([...spots.map((p) => p.kind), ...(segs && route ? ['civ', 'thief'] : []), ...(events ? ['brute'] : [])])];
  const [sourcesList, relayAsset] = await Promise.all([
    Promise.all(kinds.map(async (k) => [k, await Promise.all(
      [CAST[k].file, CAST[k].alternate, ...(CAST[k].alternates ?? [])].filter(Boolean).map(file => loader.loadAsync('./enemies/' + file)))])),
    segs ? loadStreetAsset('terminal') : Promise.resolve(null),
  ]);
  const sources = Object.fromEntries(sourcesList);
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

  function beastMesh(kind, variant = 0) {
    const src = sources[kind][variant % sources[kind].length], cast = CAST[kind];
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
    const shadeMeshes = [];
    model.traverse((o) => {
      if (!o.isMesh) return;
      const signal = o.material.name === 'Signal';
      const key = o.material.name + (o.isSkinnedMesh ? '' : '|rigid');
      if (!own.has(key)) {
        const picks = cast.paint?.[o.material.name.split('.')[0]];
        const vertexColors = !!o.geometry.getAttribute('color');
        const tint = signal ? [1.6, 1.6, 1.6]
          : vertexColors ? (kind === 'civ' ? CIV_TINTS[variant % CIV_TINTS.length] : [1, 1, 1])
          : o.material.map ? [1.2, 1.2, 1.2]
          : picks?.[variant % picks.length] ?? NEON[o.material.name] ?? o.material.color.toArray().map((v) => v ** (1 / 2.2));
        const m = paint(shared, signal ? SIGNAL_PAL : PAL.steel, { grain: 2.6, skinned: o.isSkinnedMesh, vertexColors, map: vertexColors ? null : o.material.map ?? white, tint });
        if (vertexColors) m.side = THREE.FrontSide;
        own.set(key, { m, tint: m.uniforms.uTint.value.clone() });
      }
      o.material = own.get(key).m;
      o.frustumCulled = false;
      shadeMeshes.push(o);
    });
    const named = {};
    for (const clip of src.animations) named[CLIP(clip)] = clip;
    const clips = { Idle: named.Idle_Sword ?? named.Idle, Walk: named.Walk ?? named.Run, Run: named.Run, Attack: named[cast.attack], Hit: named.HitReact ?? named.HitRecieve ?? named.RecieveHit, Death: named.Death };
    const mixer = new THREE.AnimationMixer(mesh);
    const action = mixer.clipAction(clips.Idle);
    action.play();
    Object.assign(mesh.userData, { mixer, clips, action, clip: 'Idle', mats: [...own.values()], shadeMeshes, inShade: false });
    mesh.scale.setScalar(KIND[kind].scale);
    return mesh;
  }

  // A gunner's sight: a thin line along his aim while he winds up.
  const aimGeo = new THREE.BoxGeometry(0.03, 0.03, 1).translate(0, 0, 0.5);
  const make = (s, variant = 0) => {
    const mesh = beastMesh(s.kind, variant);
    scene.add(mesh);
    const tell = new THREE.Mesh(new THREE.RingGeometry(0.88, 1, 32), new THREE.MeshBasicMaterial({ color: 0xff2a6d, transparent: true, opacity: 0.7, side: THREE.DoubleSide, depthWrite: false }));
    tell.rotation.x = -Math.PI / 2; tell.visible = false; scene.add(tell);
    let aim = null;
    if (KIND[s.kind].shot) {
      aim = new THREE.Mesh(aimGeo, new THREE.MeshBasicMaterial({ color: 0xff2a6d, transparent: true, opacity: 0.6, depthWrite: false }));
      aim.visible = false; scene.add(aim);
    }
    return { fleeT: 0, chatT: 0, chatCool: 0, chatYaw: 0, ...s, ...KIND[s.kind], mesh, tell, aim, air: 0, vy: 0, hp: HIT.hp[s.kind], maxHp: HIT.hp[s.kind], homeX: s.x, homeZ: s.z, tx: s.x, tz: s.z, yaw: Math.random() * 6.28, wait: Math.random() * 1.5, mode: 'roam', cool: 0, windup: 0, stagger: 0, phase: Math.random() * 6, flash: 0, dead: 0, gone: false, going: 0, seg: s.seg ?? -1, respawnT: 0, kx: 0, kz: 0 };
  };
  const enemies = spots.map((p) => make(p));
  // Online events: one siege mech waits off stage until the host's page calls it into a district (summon()).
  let mech = null;
  if (events) {
    mech = make({ kind: 'brute', x: spots[0].x, z: spots[0].z, seg: -1, event: true });
    Object.assign(mech, { gone: true, dead: 1, hp: 0 }); mech.mesh.visible = false;
    enemies.push(mech);
  }
  // Online, the crews (not the street's people, not the rivals) are shared: netId is their place in the level list.
  if (net) enemies.forEach((e, i) => { if (!e.civ) e.netId = i; });
  /** A shared body some other page's host is thinking for: this page only shows it and reports its own blows. */
  const puppet = (e) => !!net && e.netId != null && !net.host;
  const r2 = (v) => Math.round(v * 100) / 100;
  if (segs && route) {
    // A fixed pool. Bodies the player has left behind are reused on a sidewalk near him; a long wait
    // means something parked them on purpose (the headless checks do), and they stay there.
    const loot = new THREE.MeshBasicMaterial({ color: 0xff2a6d });
    const CROWD = 40;
    let civVariant = 0;
    for (let i = 0; i < CROWD; i++) {
      const runner = i % 5 === 0, thief = runner && i % 10 !== 0;
      const spot = nextPavement(player.feet.x + ((i % 8) - 3.5) * 8, player.feet.z - 6 - Math.floor(i / 8) * 8, i % 2 ? 1 : -1, false);
      const e = make({ kind: runner ? 'thief' : 'civ', x: spot.x, z: spot.z, leg: 0, angry: 0, thief, pool: true, dir: i % 2 ? 1 : -1, seed: i, snatch: 0, chatCool: (i % 7) * 1.5 }, runner ? i : civVariant++);
      e.homeX = spot.x; e.homeZ = spot.z;
      const nxt = nextPavement(spot.x, spot.z, e.dir, false);
      e.tx = nxt.x; e.tz = nxt.z; e.wait = Math.random() * 2;
      if (e.thief) put(e.mesh, new THREE.BoxGeometry(0.2, 0.24, 0.12), loot, (m) => m.position.set(0.24, 0.82, -0.14));
      enemies.push(e);
    }
  }
  if (segs) {
    const RING_VERT = /* glsl */ `
      out vec2 vLocal;
      void main() { vLocal = position.xz; gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0); }
    `;
    const ARENA_IN = ARENA_RADIUS - 0.54;
    const ARENA_FRAG = /* glsl */ `
      uniform vec3 uColor; uniform vec3 uAccent; uniform float uSpin; uniform float uAlpha;
      in vec2 vLocal; layout(location = 0) out vec4 outC;
      void main() {
        float rNorm = clamp((length(vLocal) - ${ARENA_IN.toFixed(2)}) / 0.54, 0.0, 1.0);
        float ang = atan(vLocal.x, vLocal.y);
        float outerRim = smoothstep(0.68, 0.90, rNorm) * smoothstep(1.0, 0.93, rNorm);
        float orbitArc = smoothstep(-0.14, 0.22, sin(ang * 8.0 + uSpin)) * smoothstep(0.22, 0.40, rNorm) * smoothstep(0.72, 0.54, rNorm);
        float innerTicks = smoothstep(0.38, 0.68, sin(ang * 24.0 - uSpin * 1.6)) * smoothstep(0.02, 0.14, rNorm) * smoothstep(0.28, 0.15, rNorm);
        float wash = smoothstep(0.0, 0.5, rNorm) * smoothstep(1.0, 0.65, rNorm) * 0.22;
        float mask = clamp(outerRim + orbitArc * 0.92 + innerTicks * 0.72 + wash, 0.0, 1.0);
        vec3 col = mix(uColor, uAccent, clamp(outerRim * 0.68 + orbitArc * 0.38, 0.0, 1.0));
        outC = vec4(col, mask * uAlpha);
      }
    `;
    const TELL_FRAG = /* glsl */ `
      uniform vec3 uColor; uniform vec3 uAccent; uniform float uArc; uniform float uSpin; uniform float uAlpha; uniform float uOpening;
      in vec2 vLocal; layout(location = 0) out vec4 outC;
      void main() {
        float rNorm = clamp((length(vLocal) - 0.72) / 0.28, 0.0, 1.0);
        float ang = atan(vLocal.x, vLocal.y);
        float sector = uOpening > 0.5 ? 1.0 : (1.0 - smoothstep(uArc - 0.08, uArc + 0.06, abs(ang)));
        float rim = smoothstep(0.66, 0.92, rNorm) * smoothstep(1.0, 0.94, rNorm);
        float band = smoothstep(0.0, 0.28, rNorm) * (0.45 + 0.55 * rNorm);
        float chev = uOpening > 0.5 ? (0.68 + 0.32 * sin(ang * 12.0 - uSpin * 2.2)) : 1.0;
        float baseRing = rim * 0.34;
        float threat = (band * 0.72 + rim * 0.95) * sector * chev;
        vec3 col = mix(uColor, uAccent, clamp(rim * (0.35 + 0.55 * sector), 0.0, 1.0));
        outC = vec4(col, clamp(max(baseRing, threat), 0.0, 1.0) * uAlpha);
      }
    `;
    const AURA_FRAG = /* glsl */ `
      uniform vec3 uColor; uniform vec3 uAccent; uniform float uSpin; uniform float uAlpha;
      in vec2 vLocal; layout(location = 0) out vec4 outC;
      void main() {
        float rNorm = clamp((length(vLocal) - 0.78) / 0.22, 0.0, 1.0);
        float ang = atan(vLocal.x, vLocal.y);
        float arc = 0.45 + 0.55 * smoothstep(-0.22, 0.28, sin(ang * 6.0 - uSpin * 2.4));
        float crest = sin(rNorm * 3.14159);
        vec3 col = mix(uColor, uAccent, smoothstep(0.45, 0.95, crest));
        outC = vec4(col, crest * arc * uAlpha);
      }
    `;
    const duelTellGeo = new THREE.RingGeometry(0.72, 1, 64).rotateX(-Math.PI / 2);
    const duelAuraGeo = new THREE.RingGeometry(0.78, 1, 48).rotateX(-Math.PI / 2);
    const actors = await Promise.all(segs.map(() => createRivalAppearance(man, shared)));
    actors.forEach((actor, index) => {
      const e = { ...createDuel(index), mesh: actor.man.object, actor, scale: 1, flash: 0, stagger: 0, dead: 0, gone: false, respawnT: 0, boltR: 0.65, kx: 0, kz: 0, duelSpin: index * 1.1 };
      e.mesh.visible = false;
      e.combat = createCombatPose(actor.man, shared, scene, { rival: true });
      e.tell = new THREE.Mesh(duelTellGeo, new THREE.ShaderMaterial({
        glslVersion: THREE.GLSL3, vertexShader: RING_VERT, fragmentShader: TELL_FRAG,
        uniforms: { uColor: { value: new THREE.Color(1, 0.165, 0.427) }, uAccent: { value: new THREE.Color(1, 0.76, 0.84) }, uArc: { value: 1.5 }, uSpin: { value: 0 }, uAlpha: { value: 0 }, uOpening: { value: 0 } },
        transparent: true, side: THREE.DoubleSide, depthWrite: false, toneMapped: false,
      }));
      e.tell.visible = false;
      e.aura = new THREE.Mesh(duelAuraGeo, new THREE.ShaderMaterial({
        glslVersion: THREE.GLSL3, vertexShader: RING_VERT, fragmentShader: AURA_FRAG,
        uniforms: { uColor: { value: new THREE.Color(1, 0.165, 0.427) }, uAccent: { value: new THREE.Color(1, 0.76, 0.84) }, uSpin: { value: 0 }, uAlpha: { value: 0 } },
        transparent: true, side: THREE.DoubleSide, depthWrite: false, blending: THREE.AdditiveBlending, toneMapped: false,
      }));
      e.aura.visible = false;
      const arenaGeo = new THREE.RingGeometry(ARENA_IN, ARENA_RADIUS, 72).rotateX(-Math.PI / 2);
      const ap = arenaGeo.attributes.position;
      for (let i = 0; i < ap.count; i++) {
        const lx = ap.getX(i), lz = ap.getZ(i);
        ap.setY(i, heightAt(e.homeX + lx, e.homeZ + lz) + 0.05);
      }
      e.arena = new THREE.Mesh(arenaGeo, new THREE.ShaderMaterial({
        glslVersion: THREE.GLSL3, vertexShader: RING_VERT, fragmentShader: ARENA_FRAG,
        uniforms: { uColor: { value: new THREE.Color(1, 0.165, 0.427) }, uAccent: { value: new THREE.Color(1, 0.76, 0.84) }, uSpin: { value: 0 }, uAlpha: { value: 0 } },
        transparent: true, side: THREE.DoubleSide, depthWrite: false, toneMapped: false,
      }));
      e.arena.position.set(e.homeX, 0, e.homeZ); e.arena.visible = false;
      e.beacon = new THREE.Mesh(new THREE.CylinderGeometry(0.16, 0.28, 22, 8, 1, true), new THREE.MeshBasicMaterial({ color: 0xff2a6d, transparent: true, opacity: 0.28, side: THREE.DoubleSide, depthWrite: false, toneMapped: false }));
      e.beacon.visible = false;
      scene.add(e.mesh, e.tell, e.aura, e.arena, e.beacon); enemies.push(e);
    });
  }
  function addBody(e) {
    const [r, half] = e.duel ? [0.3, 0.6] : e.cap;
    e.bodyLift = r + half;
    e.body = player.world.createRigidBody(player.R.RigidBodyDesc.kinematicPositionBased().setTranslation(e.x, heightAt(e.x, e.z) + e.bodyLift, e.z));
    e.collider = player.world.createCollider(player.R.ColliderDesc.capsule(half, r).setFriction(0), e.body);
  }
  for (const e of enemies) if (!e.duel && !e.gone) addBody(e);

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
      // The collider already crosses the whole city. Posts show that, and follow the slope so a side street is not a secret way through.
      const posts = [];
      for (const side of [-1, 1]) {
        for (let x = 14; x <= 280; x += 14) {
          const wx = s.gate.x + side * x;
          posts.push({ x: side * x, y: heightAt(wx, s.gate.z) - g.position.y });
        }
      }
      const wallDummy = new THREE.Object3D();
      const wall = new THREE.InstancedMesh(new THREE.BoxGeometry(14.2, 2.3, 0.45), new THREE.MeshBasicMaterial({ color: 0x1a2430 }), posts.length);
      const stripe = new THREE.InstancedMesh(new THREE.BoxGeometry(14.2, 0.16, 0.5), new THREE.MeshBasicMaterial({ color: 0xfcee0a }), posts.length);
      posts.forEach((p, n) => {
        wallDummy.position.set(p.x, p.y + 1.15, 0); wallDummy.updateMatrix(); wall.setMatrixAt(n, wallDummy.matrix);
        wallDummy.position.set(p.x, p.y + 2.2, 0); wallDummy.updateMatrix(); stripe.setMatrixAt(n, wallDummy.matrix);
      });
      wall.instanceMatrix.needsUpdate = stripe.instanceMatrix.needsUpdate = true;
      wall.computeBoundingSphere(); stripe.computeBoundingSphere();
      g.add(wall, stripe);
      g.traverse((o) => { if (o.isMesh) o.layers.disable(1); });
      const collider = player.world.createCollider(player.R.ColliderDesc.cuboid(360, 70, 0.4).setTranslation(s.gate.x, g.position.y + 66, s.gate.z));
      gates.push({ group: g, seg: i, collider });
    }
  }

  for (const m of marks) {
    const g = new THREE.Group();
    if (segs && relayAsset) {
      const relay = streetClone(relayAsset, shared);
      relay.traverse((o) => { if (o.isMesh) o.layers.disable(1); });
      relay.scale.setScalar(1.35);
      g.add(relay);
      m.relay = relay;
      const crystal = put(g, new THREE.OctahedronGeometry(0.32), mat(m.goal ? 'gold' : 'shrine'), cap => cap.position.y = 2.7);
      m.crystal = crystal;
      const mx = m.x + 8;
      g.position.set(mx, heightAt(mx, m.z), m.z);
      const ringGeo = new THREE.RingGeometry(6.8, 7, 64).rotateX(-Math.PI / 2);
      const positions = ringGeo.attributes.position;
      for (let i = 0; i < positions.count; i++) {
        const x = m.x + positions.getX(i), z = m.z + positions.getZ(i);
        positions.setXYZ(i, x, heightAt(x, z) + 0.045, z);
      }
      ringGeo.computeVertexNormals();
      const ring = new THREE.Mesh(ringGeo, new THREE.MeshBasicMaterial({ color: m.goal ? 0xffb000 : 0x42d9ef, transparent: true, opacity: 0.28, side: THREE.DoubleSide, depthWrite: false }));
      scene.add(ring); m.ring = ring;
      const beam = new THREE.Mesh(new THREE.BoxGeometry(0.14, 18, 0.14), new THREE.MeshBasicMaterial({ color: m.goal ? 0xffb000 : 0x42d9ef, transparent: true, opacity: 0.18, depthWrite: false }));
      beam.position.y = 9; g.add(beam); m.beam = beam;
    } else {
      for (let i = 0; i < 3; i++) {
        put(g, GEO.rock, mat(m.goal && i === 2 ? 'gold' : 'stone'), (rock) => {
          rock.scale.set(0.55 - i * 0.10, 0.23, 0.48 - i * 0.10);
          rock.position.y = 0.18 + i * 0.3;
          rock.rotation.y = i * 0.7;
        });
      }
      const crystal = put(g, new THREE.OctahedronGeometry(0.28), mat('gold'), cap => cap.position.y = 1.5);
      m.crystal = crystal;
      g.position.set(m.x, heightAt(m.x, m.z), m.z);
    }
    g.traverse((o) => { if (o.isMesh) o.layers.disable(1); });
    m.group = g;
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

  const hud = createHUD({ levels: segs, roam, marks, route, camera, audio });
  let hp = MAX_HP, power = POWER_MAX, swing = -1, swingH = false, struck = new Set(), invuln = 0, won = false;
  let dashCool = 0, flareCool = 0, gateToastT = 0, lockToastT = 0, saveWarned = false, runTold = false;
  let attackBuffer = 0, queuedHeavy = false, comboIndex = -1, comboWindow = 0, swingType = 'cut';
  let hitPause = 0, cameraKick = 0, hurtPose = 0, castPose = 0, slowMo = 0, combo = 0, comboT = 0, poolT = 0;
  let toldHeat = false;
  let heat = 0, cash = 0, cops = [], block = null;
  let saveWarningPending = false, plunge = false;

  // --- level state -----------------------------------------------------------------
  // `cleared` is the count of fully cleared segments; the current level is segs[cleared].
  const segAlive = (i) => enemies.some((e) => e.seg === i && !e.dead);
  const segDone = (i) => !segAlive(i) && (!missions || roam || missions.isSectorDone(i));
  const clearedCount = () => { let n = 0; if (segs) while (n < segs.length && segDone(n)) n++; return n; };
  const remaining = () => enemies.reduce((n, e) => n + (e.dead || e.civ ? 0 : 1), 0);
  let powers = roam && segs ? powersFor(segs.length) : { spark: false, dash: false, heavy: false, flare: false };
  const has = (k) => !segs || roam || powers[k];
  function persist(cleared) {
    if (!segs || roam) return;
    const cp = segs[Math.min(cleared, segs.length - 1)].checkpoint;
    if (!saveProgress({ cleared, x: +cp.x.toFixed(2), z: +cp.z.toFixed(2), cash, unlocked: unlocksFor(cleared), missions: missions?.doneSectors ?? [] }) && !saveWarned) {
      saveWarned = true;
      saveWarningPending = true;
    }
  }
  if (segs && !roam) {
    marks[0].used = true; // he starts on the first relay: no toast for standing still
    const saved = loadProgress();
    if (saved && saved.cleared > 0) {
      const n = Math.min(saved.cleared, segs.length);
      for (const e of enemies) {
        if (e.seg >= 0 && e.seg < n) { scene.remove(e.mesh); if (e.body) player.world.removeRigidBody(e.body); e.body = e.collider = null; e.dead = 1; e.gone = true; }
      }
      for (let i = 0; i < n; i++) { marks[i].used = true; missions?.completeSector(i); }
      powers = powersFor(n);
      cash = saved.cash || 0;
      moveSpawn(saved.x, saved.z);
      down();
      toast(n >= segs.length ? 'Amber Sanctuary lies ahead' : `Sector ${n + 1} · ${segs[n].name}`);
    }
  } else if (segs && roam) {
    for (const m of marks) m.cool = 0;
  }
  function toast(msg) { hud.toast(msg); }

  /** A witness sees the player unless a wall sits between them. Anyone close enough does not need the ray. */
  function sees(o) {
    const x = player.feet.x, z = player.feet.z, d = Math.hypot(x - o.x, z - o.z);
    if (d > WITNESS.radius) return false;
    if (d < WITNESS.close) return true;
    const dx = x - o.x, dz = z - o.z;
    const hit = player.castRay({ x: o.x, y: heightAt(o.x, o.z) + 1.4, z: o.z }, { x: dx / d, y: 0, z: dz / d }, d, o.collider);
    return hit == null || hit > d - 1.2;
  }

  /** What hurting one of the street's own costs. A thief pays cash and nothing else. A hostile one is fair game. */
  function blame(e, killed, provoked) {
    if (e.thief) {
      if (killed) { cash += CASH.thief; power = Math.min(POWER_MAX, power + 1); hud.flashText('Thief — no penalty'); toast(`+${CASH.thief}`); }
      return;
    }
    if (provoked) return;
    hp = Math.max(1, hp - GUILT * (killed ? 2 : 1));
    heat = Math.min(HEAT.max, heat + (killed ? HEAT.kill : HEAT.hurt));
    cameraKick = Math.max(cameraKick, killed ? 0.22 : 0.09);
    audio?.play('guilt');
    combo = 0; hud.combo(0);
    hud.flashText('Innocent!');
    if (!toldHeat && heat >= HEAT.stars[0]) { toldHeat = true; missions?.speak?.('heat'); }
    if (!killed) return;
    let n = 0;
    for (const o of enemies) {
      if (!o.civ || o.thief || o.dead || o.gone || o === e || !sees(o)) continue;
      o.angry = WITNESS.time; o.aggro = 18; o.mode = 'chase'; o.wait = 0; o.fleeT = 0; o.chatT = 0; n++;
    }
    toast(n ? `The street saw that — ${n} coming for you` : 'A citizen is dead. That one is on you.');
  }

  /** A vehicle's blow (ride.js): damage by speed, thrown along its travel. Returns false if there was nothing to hit. */
  function ram(e, speed, ux, uz, scale = 1) {
    if (e.dead || e.gone || e.rammed > 0 || e.duel && (!e.ready || e.phase === 'waiting')) return false;
    // force = speed × (mass / sedan). A truck's scale is about 2, a bicycle's a fraction.
    const band = e.civ ? ramBand(speed * scale) : (speed < RAM.min ? 'none' : 'kill');
    if (band === 'none') return false;
    e.rammed = 0.5;
    if (band === 'kill') {
      // scale is the vehicle's mass against a sedan. The cap moves with it, so a truck is not stopped at the same bruise.
      hurt(e, Math.min((e.civ ? RAM.max : RAM.crew) * scale, speed * RAM.per * scale), 'ram');
    } else {
      // A nudge bruises and annoys. A harder one throws them and stops one short of killing.
      hurt(e, band === 'stumble' ? 6 : Math.min(18, Math.max(0, e.hp - 1)), 'ram');
      if (e.hp > 0) { e.angry = Math.max(e.angry ?? 0, 10); e.aggro = 14; e.mode = 'chase'; e.wait = 0; e.fleeT = 0; e.chatT = 0; }
      e.stagger = Math.max(e.stagger, band === 'knock' ? 1.15 : 0.4);
    }
    if (!e.duel) {
      const mul = band === 'stumble' ? 0.28 : band === 'knock' ? 0.7 : 0.9;
      const k = speed * mul * HEFT[e.kind];
      e.kx = ux * k; e.kz = uz * k; e.vy = Math.max(e.vy, Math.min(7.5, speed * (band === 'stumble' ? 0.12 : 0.4)) * HEFT[e.kind]);
    }
    return true;
  }

  // `from` is another wanderer's blow, passed on by the host: no hit-stop or combo for him here, knocked from there.
  function hurt(e, n, source = 'sword', from = null) {
    if (e.dead) return;
    const shadow = !from && puppet(e);
    if (shadow) net.send({ t: 'hit', i: e.netId, n: Math.round(n), src: source, x: r2(player.feet.x), z: r2(player.feet.z) });
    if (!from) e.mineT = performance.now();
    const provoked = e.angry > 0;
    if (e.duel && (!e.ready || e.phase === 'intro' || e.phase === 'waiting')) return;
    const guarded = e.duel && e.phase !== 'recovery';
    if (e.duel) n = damageRival(e, n, source);
    if (guarded && e.phase === 'recovery') toast('Guard broken — strike now!');
    if (!shadow) e.hp -= n;
    e.flash = 0.16;
    if (!from) { combo++; comboT = 2.2; hud.combo(combo); }
    if (!e.duel) {
      e.stagger = n > 40 ? 0.55 : 0.22; e.windup = 0;
      const by = from ?? player.feet;
      const k = (KNOCK[source] ?? 3) * (HEFT[e.kind] ?? 1), dx = e.x - by.x, dz = e.z - by.z, d = Math.hypot(dx, dz) || 1;
      e.kx = dx / d * k; e.kz = dz / d * k;
      // a heavy blow takes the light ones off their feet; in the air they cannot answer
      if ((source === 'heavy' || source === 'whirl') && e.air <= 0) e.vy = LAUNCH * HEFT[e.kind];
    }
    e.cool = Math.max(e.cool, 0.45);
    if (!from && (!guarded || e.duel && e.phase === 'recovery' || source === 'flare')) {
      const big = source === 'heavy' || source === 'whirl' || source === 'thrust';
      hitPause = Math.max(hitPause, (big ? 0.075 : 0.045) + Math.min(combo, 8) * 0.004); cameraKick = big ? 0.12 : 0.055;
      audio?.play(source === 'heavy' ? 'heavy' : 'hit', e.kind === 'brute' ? 0.72 : 1);
    }
    pulse(e.x, heightAt(e.x, e.z) + (e.duel ? 1.1 : 0.8), e.z, guarded ? 0xf3c77b : 0xb8f5ec);
    if (source === 'heavy') pulse(e.x, heightAt(e.x, e.z) + 0.07, e.z, 0xf6d493, true);
    for (let i = 0; i < 5 && impacts.length < 40; i++) {
      const mesh = put(scene, boltGeo, mat('gold'));
      mesh.layers.disable(1);
      mesh.scale.setScalar(0.35);
      impacts.push({ mesh, x: e.x, y: heightAt(e.x, e.z) + (e.duel ? 1.15 : 0.75), z: e.z, vx: (Math.random() - 0.5) * 9, vy: 2 + Math.random() * 3, vz: (Math.random() - 0.5) * 9, life: 0.3 });
    }
    if (e.civ) blame(e, e.hp <= 0, provoked);
    if (!shadow && e.hp <= 0) fall(e);
  }

  // Online, everyone who struck a body in its last seconds is paid for it: an assist is a kill.
  function fall(e) {
    e.dead = 0.001; e.tell.visible = false;
    if (e.aura) e.aura.visible = false;
    if (e.arena) e.arena.visible = false;
    if (!e.civ && !e.duel && performance.now() - (e.mineT ?? -1e9) < 4000) { cash += CASH.enemy; power = Math.min(POWER_MAX, power + 0.35); hud.flashText(`+${CASH.enemy}`); }
    if (e.body) player.world.removeRigidBody(e.body);
    e.body = e.collider = null;
    if (!e.duel) { e.kx *= 1.7; e.kz *= 1.7; e.vy = Math.max(e.vy, 3.5 * HEFT[e.kind]); } // the fallen are thrown
    if (e.duel) { toast(`${e.name} — lesson mastered`); hitPause = 0.12; cameraKick = 0.16; }
  }

  /** A puppet goes where the host last put it, a little behind; its windup runs down between word from the host. */
  function follow(e, dt) {
    const s = net.foe(e);
    if (!s) return;
    const k = Math.min(1, dt * 10);
    if (Math.hypot(s.x - e.x, s.z - e.z) > 6) { e.x = s.x; e.z = s.z; } else { e.x += (s.x - e.x) * k; e.z += (s.z - e.z) * k; }
    e.yaw += Math.atan2(Math.sin(s.yaw - e.yaw), Math.cos(s.yaw - e.yaw)) * k;
    e.going = s.going; e.hp = s.hp;
    if (e.netAt !== s.at) { e.netAt = s.at; e.windup = s.windup; } else e.windup = Math.max(0, e.windup - dt);
    if (e.windup > 0) e.attackYaw = e.yaw;
  }

  function takeHit(damage, from = null, shove = 5) {
    if (invuln > 0) return false;
    hp -= damage; invuln = 0.7; hurtPose = 1; hitPause = 0.07; cameraKick = 0.16;
    audio?.play('playerHurt'); audio?.bark('hurt');
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

  // Online a fall is not the end at once: he is down for DOWN seconds and a teammate who stands by him lifts him up.
  const DOWN = 15;
  let downT = 0;
  function die(force = false) {
    if (net && !force) {
      if (downT > 0) return;
      downT = DOWN; hp = 0; invuln = DOWN + 1; swing = -1; swingH = false; attackBuffer = 0;
      window.lab?.rides?.leave(true);
      toast('Down · a teammate can revive you by standing close');
      return;
    }
    downT = 0;
    down();
    hp = MAX_HP;
    invuln = 2.6;
    swing = -1; swingH = false;
    attackBuffer = 0;
    for (const e of enemies) {
      if (e.dead) continue;
      if (e.duel) { Object.assign(e, createDuel(e.seg)); continue; }
      if (e.civ) { e.angry = 0; e.aggro = 0; e.mode = 'roam'; e.fleeT = 0; e.chatT = 0; } // his fall settles the street's score
      if (Math.hypot(e.x - player.feet.x, e.z - player.feet.z) < 5) {
        e.x = e.homeX; e.z = e.homeZ; e.mode = 'roam'; e.tx = e.homeX; e.tz = e.homeZ;
        e.windup = e.stagger = e.cool = e.air = e.vy = e.kx = e.kz = 0;
      }
    }
    plunge = false;
    for (const b of bolts) if (b.foe) b.life = 0;
    toast(segs ? 'Back to Mission Relay' : 'Back to the last checkpoint');
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

  function setShade(e, on) {
    const u = e.mesh.userData;
    if (u.shadeMeshes && u.inShade !== on) {
      u.inShade = on;
      for (const sm of u.shadeMeshes) on ? sm.layers.enable(1) : sm.layers.disable(1);
    }
  }

  function lay(e, dt) {
    if (e.duel) {
      const vis = duelVisual(e);
      e.duelSpin = (e.duelSpin ?? 0) + (dt > 0 ? dt * vis.spin : 0);
      const spin = e.duelSpin + (e.phase === 'waiting' ? shared.uTime.value * 0.7 : 0);
      const pd = Math.hypot(e.x - player.feet.x, e.z - player.feet.z);
      if (e.beacon) {
        e.beacon.visible = !e.dead && !e.gone && e.ready;
        if (e.beacon.visible) {
          e.beacon.position.set(e.x, heightAt(e.x, e.z) + 11, e.z);
          e.beacon.material.color.setRGB(...vis.color);
          e.beacon.material.opacity = vis.engaged ? 0.16 + 0.08 * Math.sin(spin * 2) : 0.3;
        }
      }
      e.mesh.visible = !e.gone && (e.ready || !!e.dead) && pd < SEEN;
      e.combat.trail.visible = e.mesh.visible;
      if (!e.mesh.visible) {
        e.tell.visible = false;
        if (e.aura) e.aura.visible = false;
        if (e.arena) e.arena.visible = false;
        return;
      }
      e.mesh.position.set(e.x, heightAt(e.x, e.z), e.z); e.mesh.rotation.set(0, e.yaw, 0);
      if (e.body) e.body.setTranslation({ x: e.x, y: heightAt(e.x, e.z) + e.bodyLift, z: e.z }, true);
      e.actor.update(dt, e.going, !!e.dead);
      if (e.dead) e.actor.man.bones.spine_02.rotation.x += 0.3;
      e.combat.apply(dt, { u: e.dead ? -1 : e.attackU, type: MOVES[e.move].pose, hurt: e.flash * 3 });
      e.actor.avatar.update();
      const gy = heightAt(e.x, e.z);
      if (e.arena) {
        e.arena.visible = !e.dead && e.ready;
        if (e.arena.visible) {
          const au = e.arena.material.uniforms;
          au.uColor.value.setRGB(...vis.color);
          au.uAccent.value.setRGB(...vis.accent);
          au.uSpin.value = spin;
          au.uAlpha.value = vis.engaged ? (vis.mode === 'opening' ? 0.92 : 0.88) : 0.34;
        }
      }
      e.tell.visible = vis.engaged;
      if (e.tell.visible) {
        const tu = e.tell.material.uniforms;
        e.tell.position.set(e.x, gy + 0.045, e.z);
        e.tell.rotation.y = e.attackYaw ?? e.yaw;
        e.tell.scale.setScalar(vis.reach);
        tu.uColor.value.setRGB(...vis.color);
        tu.uAccent.value.setRGB(...vis.accent);
        tu.uArc.value = vis.arc;
        tu.uSpin.value = spin;
        tu.uOpening.value = vis.mode === 'opening' ? 1 : 0;
        tu.uAlpha.value = e.phase === 'active' ? 0.92 : e.phase === 'windup' ? 0.42 + 0.50 * vis.progress : vis.mode === 'opening' ? 0.78 : 0.24;
      }
      if (e.aura) {
        e.aura.visible = vis.engaged;
        if (e.aura.visible) {
          const qu = e.aura.material.uniforms;
          e.aura.position.set(e.x, gy + 0.058, e.z);
          const auraR = e.phase === 'windup' ? 0.48 + vis.progress * (vis.reach * 0.98 - 0.48)
            : e.phase === 'active' ? vis.reach
            : vis.mode === 'opening' ? 1.22 + 0.14 * Math.sin(spin * 4.2)
            : 1.06 + 0.06 * Math.sin(spin * 2.5);
          e.aura.scale.setScalar(auraR);
          qu.uColor.value.setRGB(...(e.flash > 0 ? [1, 1, 1] : vis.color));
          qu.uAccent.value.setRGB(...vis.accent);
          qu.uSpin.value = spin;
          qu.uAlpha.value = e.phase === 'active' ? 0.88 : e.phase === 'windup' ? 0.45 + 0.42 * vis.progress : vis.mode === 'opening' ? 0.76 : 0.34;
        }
      }
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
    if (dt > 0 && e.mesh.visible) e.mesh.userData.mixer.update(dt); // a pose nobody sees, or a frozen one, is not worked out
  }

  function pose() {
    if (swingType === 'spin' && swing >= 0) {
      const k = Math.min(1, swing / 0.9);
      man.object.rotateY(-k * k * (3 - 2 * k) * Math.PI * 2);
      man.object.updateMatrixWorld(true);
    }
    combatPose.apply(1 / 60, { u: swing, type: swingType, hurt: hurtPose, cast: castPose });
  }

  // A body is many small skinned meshes with no bounds worth trusting, so they cannot be left to the renderer's
  // own culling. Each is shown only when it is within SEEN metres and inside the eye's view (last frame's, with
  // room to spare); the few right beside him always are, since they stand in his shadow picture.
  const SEEN = 85, frustum = new THREE.Frustum(), viewMatrix = new THREE.Matrix4(), ball = new THREE.Sphere();
  const seen = (e, pd) => pd < 9 || pd < SEEN && (!camera || frustum.intersectsSphere(ball.set(ball.center.set(e.x, heightAt(e.x, e.z) + 1.2, e.z), 3.2 * e.scale)));

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

  /** A gunner's shot at `to` (him, or online whoever it tracks), or with the rise `vy` the host already chose. */
  function fire(e, to, vy) {
    const sx = Math.sin(e.attackYaw), sz = Math.cos(e.attackYaw);
    const mesh = put(scene, new THREE.SphereGeometry(0.12, 6, 5), shotMat);
    mesh.scale.set(1, 1, 3.2); mesh.rotation.y = e.attackYaw;
    const y = heightAt(e.x, e.z) + 1.25;
    vy ??= Math.max(-4, Math.min(4, (to.y + 1.05 - y) / Math.max(Math.hypot(to.x - e.x, to.z - e.z) / e.shot, 0.2)));
    bolts.push({ mesh, foe: e.dmg, g: 0, life: 1.3, x: e.x + sx * 0.7, y, z: e.z + sz * 0.7, vx: sx * e.shot, vy, vz: sz * e.shot });
    audio?.play('bolt');
    return vy;
  }

  const swingMul = () => swingH ? 3 : swingType === 'spin' ? WHIRL : swingType === 'thrust' ? 1.3 : 1;
  const swingSource = () => swingH ? 'heavy' : swingType === 'spin' ? 'whirl' : swingType === 'thrust' ? 'thrust' : 'sword';

  function update(dt, input, realDt = dt) {
    if (downT > 0 && (downT -= realDt) <= 0) die(true);
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
    if (camera) frustum.setFromProjectionMatrix(viewMatrix.multiplyMatrices(camera.projectionMatrix, camera.matrixWorldInverse));
    for (const m of marks) {
      m.crystal.rotation.y += dt;
      m.crystal.position.y = (segs ? 2.7 : 1.5) + Math.sin(m.crystal.rotation.y * 1.8) * 0.12;
      if (m.relay) m.relay.visible = Math.hypot(m.x - player.feet.x, m.z - player.feet.z) < 75;
      if (!m.ring) continue;
      const s = 0.5 + 0.5 * Math.sin(m.crystal.rotation.y * 2.2), hot = !m.used;
      m.ring.material.opacity = (hot ? 0.18 : 0.08) + (hot ? 0.12 : 0.05) * s;
      m.beam.material.opacity = (hot ? 0.12 : 0.04) + (hot ? 0.08 : 0.03) * s;
    }
    const px = player.feet.x, pz = player.feet.z;
    let yaw = player.yaw;
    const c = segs ? clearedCount() : 0;
    const closestRival = roam ? enemies.filter(e => e.duel && !e.dead).sort((a, b) => Math.hypot(a.x - px, a.z - pz) - Math.hypot(b.x - px, b.z - pz))[0] : null;
    // A closed gate stands between two z values: neither side's teeth reach through.
    const gated = (ez) => segs && !roam && !won && gates.some((g) => c <= g.seg && (ez - g.group.position.z) * (pz - g.group.position.z) < 0);
    /** His own blade against one body. */
    function swingAt(e) {
      if (!hitting || struck.has(e) || gated(e.z) || Math.abs(player.feet.y - heightAt(e.x, e.z)) >= 1.7 || !inFront(px, pz, yaw, e.x, e.z, ATTACKS[swingType].reach, swingType === 'spin' ? Math.PI : swingH ? 1.6 : 1.2)) return;
      struck.add(e);
      // A blow landed in the last moment of his windup turns the strike aside.
      const parry = !e.shot && e.windup > 0 && e.windup < PARRY;
      hurt(e, swingMul() * HIT.sword[e.kind] * (parry ? PARRY_MUL : 1), swingSource());
      if (parry && !e.dead) {
        e.stagger = 1.2; power = Math.min(POWER_MAX, power + 0.5); slowMo = Math.max(slowMo, 0.25);
        audio?.play('parry'); audio?.bark('parry');
        hud.flashText('Parry');
      }
    }
    if (input?.attack || input?.heavy && (!segs || has('heavy'))) {
      attackBuffer = 0.55;
      queuedHeavy = !!(segs && input?.heavy && has('heavy'));
    }
    if (input?.whirl && segs && !has('whirl') && lockToastT <= 0) {
      lockToastT = 4; toast('Clear Sector 5 to learn the whirl');
    } else if (input?.whirl && has('whirl') && hp > 0 && (swing < 0 || swing > 0.6)) {
      if (power >= 1) {
        power -= 1; swingType = 'spin'; swing = 0; swingH = false; struck = new Set(); attackBuffer = 0; comboWindow = 0;
        audio?.play('whirl');
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
      if (queuedHeavy || plunge) { audio?.play('heavy'); audio?.bark('heavy'); }
      else { audio?.play('slash', swingType === 'thrust' ? 0.85 : 1); audio?.bark('swing'); }
      const target = plunge ? null : enemies.filter(e => !e.dead && (!e.civ || e.angry > 0) && (!e.duel || e.ready) && !gated(e.z) && inFront(px, pz, yaw, e.x, e.z, 3.6, 1.8)).sort((a, b) => Math.hypot(a.x - px, a.z - pz) - Math.hypot(b.x - px, b.z - pz))[0];
      if (target) {
        const turn = Math.atan2(Math.sin(Math.atan2(target.x - px, target.z - pz) - yaw), Math.cos(Math.atan2(target.x - px, target.z - pz) - yaw)); yaw = player.yaw += Math.max(-0.7, Math.min(0.7, turn));
        // close the gap a knockback opened: a short swept lunge to striking distance
        const gap = Math.hypot(target.x - px, target.z - pz) - (target.kind === 'brute' ? 2.1 : 1.5);
        if (gap > 0.1) player.knock(target.x - px, target.z - pz, Math.min(gap, 1.6) * 8.4);
      }
    }
    if (input?.heavy && segs && !has('heavy') && lockToastT <= 0) {
      lockToastT = 4;
      toast('Clear Sector 3 to learn the heavy swing');
    }
    if (swing >= 0) { swing += dt / ATTACKS[swingType].duration; if (swing >= 1) { swing = -1; swingH = false; comboWindow = 0.7; } }
    const hitting = swing >= ATTACKS[swingType].hit[0] && swing <= ATTACKS[swingType].hit[1];
    if (hitting) {
      missions?.hitAt(px + Math.sin(yaw) * 1.4, pz + Math.cos(yaw) * 1.4, ATTACKS[swingType].reach, swingMul() * 34, swingSource(), struck, c);
    }
    if (plunge && player.grounded) {
      plunge = false; cameraKick = Math.max(cameraKick, 0.14);
      audio?.play('plunge');
      pulse(px, heightAt(px, pz) + 0.07, pz, 0xfcee0a, true);
      missions?.hitAt(px, pz, 3.6, PLUNGE * 34, 'heavy', struck, c);
      for (const e of enemies) {
        if (e.dead || e.gone || e.duel && !e.ready || gated(e.z) || Math.hypot(e.x - px, e.z - pz) > 3.4) continue;
        struck.add(e); hurt(e, PLUNGE * HIT.sword[e.kind], 'heavy');
      }
    }

    // A dash: one quick step where he faces, too fast to be bitten.
    if (input?.dash && has('dash') && dashCool <= 0 && hp > 0) {
      if (player.startDash()) {
        dashCool = 2.5; invuln = Math.max(invuln, 0.3);
        audio?.play('dash');
        // Perfect: a strike was about to land on him.
        const close = enemies.some((e) => !e.dead && !e.gone && !gated(e.z) && (e.duel
          ? (e.phase === 'windup' && e.timer < PERFECT || e.phase === 'active') && Math.hypot(e.x - px, e.z - pz) < MOVES[e.move].reach + 1
          : e.windup > 0 && e.windup < PERFECT && Math.hypot(e.x - px, e.z - pz) < e.reach + 1));
        if (close) {
          slowMo = 0.7; invuln = Math.max(invuln, 0.6); power = Math.min(POWER_MAX, power + 1);
          audio?.play('dodge');
          pulse(px, player.feet.y + 1, pz, 0xffffff); hud.flashText('Perfect dodge');
        }
      }
    } else if (input?.dash && !has('dash') && lockToastT <= 0) {
      lockToastT = 4; toast('Clear Sector 2 to learn the dash');
    }

    // One body a second follows him onto a nearby sidewalk. A long wait is a body a check parked on purpose.
    poolT -= dt;
    if (poolT <= 0) {
      poolT = 0.9;
      const far = enemies.find(e => e.pool && !e.dead && !e.gone && e.wait < 6 && !(e.angry > 0) && Math.hypot(e.x - px, e.z - pz) > 52);
      if (far) {
        const spot = nextPavement(px + ((far.seed % 5) - 2) * 7, pz + ((far.seed % 3) - 1) * 8, far.dir, false);
        far.x = far.homeX = spot.x; far.z = far.homeZ = spot.z; far.kx = far.kz = far.vy = far.air = 0; far.fleeT = far.chatT = 0;
        const nxt = nextPavement(spot.x, spot.z, far.dir, false);
        far.tx = nxt.x; far.tz = nxt.z;
      }
    }

    for (const g of gates) g.group.visible = Math.abs(pz - g.group.position.z) < 120;
    for (const m of marks) {
      const md = Math.hypot(px - m.x, pz - m.z);
      if (m.group) m.group.visible = md < 120;
      if (m.ring) m.ring.visible = md < 95;
    }
    const fighting = swing >= 0 || castPose > 0 || flares.length > 0 || !!(input?.flare && has('flare') && power >= 2 && flareCool <= 0)
      || enemies.some((o) => !o.dead && !o.gone && (o.duel ? o.ready && o.phase !== 'waiting' : !o.civ && (o.mode === 'chase' || o.windup > 0)) && Math.hypot(o.x - px, o.z - pz) < 20);
    for (const e of enemies) {
      e.flash = Math.max(0, e.flash - dt);
      e.rammed = Math.max(0, (e.rammed ?? 0) - dt);
      e.stagger = Math.max(0, e.stagger - dt);
      if (e.gone) {
        // In free roam the fallen come back after a while, at home and calm.
        if (puppet(e) ? (net.foe(e)?.hp ?? 0) > 0 : (roam || e.civ) && !e.event && (e.respawnT -= dt) <= 0) { // the street refills in level mode too
          e.hp = e.maxHp; e.dead = 0; e.gone = false;
          e.x = e.homeX; e.z = e.homeZ; e.tx = e.homeX; e.tz = e.homeZ;
          e.mode = 'roam'; e.cool = e.windup = e.stagger = e.air = e.vy = e.kx = e.kz = 0; e.wait = 1; e.mesh.visible = true;
          if (e.civ) { e.angry = 0; e.aggro = 0; e.fleeT = 0; e.chatT = 0; }
          if (e.duel) Object.assign(e, createDuel(e.seg));
          if (!e.duel) addBody(e);
        } else continue;
      }
      if (puppet(e) && !e.dead && (net.foe(e)?.hp ?? 1) <= 0) fall(e);
      if (e.dead) {
        e.dead += dt;
        e.going = 0;
        shove(e, dt);
        lay(e, dt);
        if (e.dead > 2.2) {
          setShade(e, false);
          if (e.combat) e.combat.trail.visible = false;
          if (roam || e.civ) { e.gone = true; e.respawnT = e.civ ? 45 : 20; e.mesh.visible = false; }
          else { scene.remove(e.mesh); e.gone = true; }
        }
        continue;
      }
      const ld = Math.hypot(px - e.x, pz - e.z);
      if (puppet(e)) { follow(e, dt); e.mesh.visible = seen(e, ld); setShade(e, !e.duel && e.mesh.visible && ld < 7.5); swingAt(e); lay(e, dt); continue; }
      shove(e, dt);
      // Online the host's crews go for the nearest wanderer, his or another page's.
      let ax = px, az = pz, ay = player.feet.y;
      const other = net && e.netId != null ? net.near(e.x, e.z) : null;
      if (other && other.d < ld) { ax = other.x; az = other.z; ay = other.y; }
      const dxp = ax - e.x, dzp = az - e.z, pd = Math.hypot(dxp, dzp);
      const homeD = Math.hypot(e.x - e.homeX, e.z - e.homeZ);
      // Far beasts keep their pose but skip the thinking: the frame stays cheap. Citizens keep strolling down the block.
      if (pd > (e.civ ? 46 : 36)) { e.going = 0; e.cool = Math.max(0, e.cool - dt); if (!e.duel) { e.mesh.visible = seen(e, ld); setShade(e, e.mesh.visible && ld < 7.5); } lay(e, 0); continue; }
      if (!e.duel) { e.mesh.visible = seen(e, ld); setShade(e, e.mesh.visible && ld < 7.5); }
      if (e.chatCool > 0) e.chatCool = Math.max(0, e.chatCool - dt);
      if (e.chatT > 0) e.chatT = Math.max(0, e.chatT - dt);
      if (e.fleeT > 0) e.fleeT = Math.max(0, e.fleeT - dt);
      if (fighting && e.civ && !e.thief && !(e.angry > 0) && e.wait < 50 && pd > 3.2 && pd < 15) {
        if (e.pool) {
          const pPos = nextPavement(e.x, e.z, 1, false), pNeg = nextPavement(e.x, e.z, -1, false);
          const away = Math.hypot(pPos.x - px, pPos.z - pz) >= Math.hypot(pNeg.x - px, pNeg.z - pz) ? 1 : -1;
          if (!(e.fleeT > 0) || e.dir !== away) {
            e.dir = away;
            const nxt = away > 0 ? pPos : pNeg;
            e.tx = nxt.x; e.tz = nxt.z;
          }
        }
        e.fleeT = 4.5; e.wait = 0; e.chatT = 0;
      }
      const dv = window.lab?.rides?.driven;
      if (e.angry > 0 && e.civ && !e.thief && pd < WITNESS.pull && dv && Math.hypot(dv.vx, dv.vz) < 3.2 && Math.random() < dt * 1.2) {
        window.lab.rides.leave();
        hud.flashText('Dragged out');
      }
      if (e.angry > 0 && (e.angry -= dt) <= 0) {
        e.aggro = 0; e.mode = 'roam';
        if (e.walk) [e.tx, e.tz] = e.walk[e.leg];
        else if (e.pool) { const n = nextPavement(e.x, e.z, e.dir, false); e.tx = n.x; e.tz = n.z; }
      }
      if (e.duel) {
        const ready = roam ? e === closestRival : (!enemies.some(b => !b.duel && b.seg === e.seg && !b.dead) && (!missions || missions.isSectorDone(e.seg)));
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
          missions?.speak?.('duel' + e.seg);
          pulse(e.x, heightAt(e.x, e.z) + 0.05, e.z, 0xa8efdc, true); toast(`${e.name} · ${e.lesson}`);
        }
        if (event === 'reset') toast('Duel reset. Approach the beacon when you are ready.');
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
      if (e.mode === 'chase' && !(e.angry > 0) && (pd > e.aggro + 5 || homeD > e.leash)) {
        e.mode = 'roam'; e.tx = e.homeX; e.tz = e.homeZ; e.wait = 0;
      }
      let speed = 0;
      const pace = e.fleeT > 0 ? (e.kind === 'civ' ? e.roam * 2.1 : Math.max(e.roam, 4.8)) : e.snatch > 0 ? e.roam * 1.5 : e.roam;
      if (e.mode === 'chase') {
        e.tx = ax; e.tz = az; speed = pd > e.reach * 0.83 ? e.chase : 0;
        if (e.keep && pd < e.keep) { e.tx = e.x - dxp; e.tz = e.z - dzp; speed = e.chase; } // rushed, a gunner gives ground
      }
      else if (e.wait > 0) e.wait -= dt;
      else if (Math.hypot(e.tx - e.x, e.tz - e.z) < 0.45) {
        e.wait = e.fleeT > 0 ? 0 : 0.7 + Math.random() * 1.8;
        const a = Math.random() * Math.PI * 2, r = Math.random() * e.home;
        e.tx = e.homeX + Math.sin(a) * r; e.tz = e.homeZ + Math.cos(a) * r;
        // the street's people keep to their beat: there and back along the pavement
        if (e.walk) { e.leg ^= 1; [e.tx, e.tz] = e.walk[e.leg]; e.wait = e.fleeT > 0 ? 0 : e.thief || e.kind === 'thief' ? 0.2 : Math.random() * 3; }
        else if (e.pool) {
          // nextPavement only actually crosses when this point is already in the intersection.
          let nxt = nextPavement(e.x, e.z, e.dir, e.fleeT > 0 ? false : (e.thief || Math.random() < 0.4));
          if (Math.hypot(nxt.x - e.x, nxt.z - e.z) < 0.8) { e.dir *= -1; nxt = nextPavement(e.x, e.z, e.dir, false); }
          e.tx = nxt.x; e.tz = nxt.z; e.wait = e.fleeT > 0 ? 0 : e.thief ? 0.15 : Math.random() * 1.4;
          // A thief beside a civilian sometimes grabs and runs. The bag is the tell; the grab is the other one.
          if (e.thief && !(e.angry > 0) && Math.random() < 0.35) {
            const mark = enemies.find(o => o.kind === 'civ' && !o.dead && !o.gone && !(o.angry > 0) && o.wait < 50 && Math.hypot(o.x - e.x, o.z - e.z) < 2.4);
            if (mark) { e.dir *= -1; e.snatch = 2.5; mark.stagger = 0.55; mark.kx = (mark.x - e.x) * 1.5; mark.kz = (mark.z - e.z) * 1.5; }
          }
        }
        if (e.wait <= 0) speed = pace;
      } else speed = pace;
      if (e.snatch > 0) e.snatch -= dt;
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
      else if (e.chatT > 0 && e.windup <= 0 && !(e.angry > 0) && !(e.fleeT > 0)) {
        const turn = Math.atan2(Math.sin(e.chatYaw - e.yaw), Math.cos(e.chatYaw - e.yaw));
        e.yaw += Math.max(-6 * dt, Math.min(6 * dt, turn));
      }
      e.phase += dt * (3 + e.going * 2);
      e.cool = Math.max(0, e.cool - dt);
      const near = pd;
      if (e.windup > 0) {
        if (e.shot && e.windup > AIM_LOCK) e.yaw = e.attackYaw = Math.atan2(ax - e.x, az - e.z);
        if (e.shot && e.windup < AIM_LOCK && !e.lockedPlayed) { e.lockedPlayed = true; audio?.play('lock'); }
        if (e.lunge && e.windup < 0.2) sweep(e, Math.sin(e.attackYaw), Math.cos(e.attackYaw), e.lunge / 0.2 * dt);
        e.windup = Math.max(0, e.windup - dt);
        // Shared strikes and shots are announced; every page tests them against its own wanderer.
        if (e.windup === 0 && e.shot) {
          e.lockedPlayed = false;
          if (hp > 0 && !gated(e.z)) {
            const vy = fire(e, { x: ax, y: ay, z: az });
            if (net && e.netId != null) net.send({ t: 'ev', k: 'bolt', i: e.netId, yaw: r2(e.attackYaw), vy: r2(vy) });
          }
        } else if (e.windup === 0) {
          if (net && e.netId != null) net.send({ t: 'ev', k: 'strike', i: e.netId });
          if (invuln <= 0 && hp > 0 && !gated(e.z) && strikeHits(e, px, player.feet.y, pz, heightAt(e.x, e.z))) takeHit(e.dmg, e, e.push);
        }
        if (e.windup === 0 && e.kind === 'brute') { pulse(e.x, heightAt(e.x, e.z) + 0.06, e.z, 0xff9f7a, true); cameraKick = Math.max(cameraKick, 0.06); }
      } else if (e.mode === 'chase' && near < e.reach + 0.25 + (e.lunge ?? 0) && e.cool <= 0 && e.stagger <= 0 && hp > 0 && !gated(e.z)) {
        e.windup = e.wind; e.lockedPlayed = false;
        e.attackYaw = Math.atan2(ax - e.x, az - e.z);
        e.yaw = e.attackYaw;
        e.cool = e.windup + 1.0;
      }
      swingAt(e);
      lay(e, dt);
    }
    for (let i = 0; i < enemies.length; i++) {
      for (let j = i + 1; j < enemies.length; j++) {
        const a = enemies[i], b = enemies[j];
        if (a.dead || b.dead || a.gone || b.gone || a.duel || b.duel) continue;
        const limit = a.civ && b.civ && a.wait < 50 && b.wait < 50 ? 46 : 36;
        if (Math.hypot(a.x - px, a.z - pz) > limit || Math.hypot(b.x - px, b.z - pz) > limit) continue;
        const dx = b.x - a.x, dz = b.z - a.z, d = Math.hypot(dx, dz) || 0.001;
        if (
          !fighting && a.kind === 'civ' && b.kind === 'civ' && d > 1.0 && d < 1.9
          && !(a.angry > 0) && !(b.angry > 0) && !(a.fleeT > 0) && !(b.fleeT > 0)
          && a.wait <= 0 && b.wait <= 0 && a.wait < 50 && b.wait < 50
          && (a.chatCool ?? 0) <= 0 && (b.chatCool ?? 0) <= 0 && Math.random() < dt * 0.55
        ) {
          a.wait = b.wait = a.chatT = b.chatT = 1.2;
          a.chatCool = b.chatCool = 14;
          a.chatYaw = Math.atan2(dx, dz);
          b.chatYaw = Math.atan2(-dx, -dz);
        }
        const minD = 0.45 * (a.scale + b.scale); // the mech needs more room than a punk
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
          toast('Clear Sector 1 to earn the spark');
        }
      } else {
        const next = spendPower(power);
        if (next != null) {
          power = next;
          castPose = 1;
          audio?.play('spark');
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
        audio?.play('flare');
        missions?.hitAt(px, pz, 9, 60, 'flare', null, c);
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
          audio?.play('deflect');
          pulse(b.x, b.y, b.z, 0x00f0ff); hud.flashText('Deflected');
        } else if (near < 0.55 && Math.abs(b.y - player.feet.y - 1) < 1.1 && hp > 0) {
          gone = takeHit(b.foe, { x: b.x - b.vx, z: b.z - b.vz }, 3); // a dash goes straight through it
        }
      } else if (!gone) {
        if (missions?.hitBolt(b.x, b.y, b.z, 1.2, 75, c)) {
          gone = true;
        } else for (const e of enemies) {
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
          audio?.play('gateOpen');
        }
      }
      const shut = gates.find((g) => c <= g.seg);
      if (shut && pz < shut.group.position.z + 1.3) {
        // The collider handles ordinary contact; this only catches explicit teleports past a gate.
        if (pz < shut.group.position.z + 0.45) holdAt(px, shut.group.position.z + 1.6);
        if (gateToastT <= 0) {
          gateToastT = 4;
          const n = enemies.reduce((k, e) => k + (e.seg === c && !e.dead ? 1 : 0), 0);
          toast(!missions?.isSectorDone(c) ? `Complete ${segs[Math.min(c, segs.length - 1)].name} mission first` : `Clear ${segs[Math.min(c, segs.length - 1)].name} — ${n} left`);
        }
      }
    }

    // New powers arrive the moment their level falls.
    if (segs && !roam && !won) {
      for (let i = 0; i < c; i++) {
        const g = segs[i].grant;
        if (g && !powers[g]) {
          persist(c);
          missions?.speak?.('clear' + i);
          toast(`Sector ${i + 1} clear — ${POWERS[g].name} earned (${POWERS[g].key})`);
        }
      }
      powers = powersFor(c);
      if (c >= segs.length && !runTold) {
        runTold = true; persist(c);
        missions?.speak?.('clear5');
        toast('Amber Sanctuary lies ahead — enter the golden terminal');
      }
    }

    if (hp > 0) {
      for (const [mi, m] of marks.entries()) {
        if (m.cool > 0) m.cool -= dt;
        if (m.used) continue;
        if (Math.hypot(px - m.x, pz - m.z) > (segs ? 7 : m.goal ? 3.6 : 3.1)) continue;
        if (segs && !roam) {
          if (m.goal) {
            if (c >= segs.length) {
              m.used = true; moveSpawn(m.x, m.z); hp = MAX_HP; won = true; persist(c);
              audio?.play('missionDone');
              missions?.speak?.('win');
              toast('Amber Sanctuary stabilized — you did it');
            } else if (gateToastT <= 0) {
              gateToastT = 4;
              toast(`The sanctuary is not safe yet — ${remaining()} still out`);
            }
          } else if (mi <= c) {
            m.used = true; moveSpawn(m.x, m.z); hp = MAX_HP; heat = Math.max(0, heat - 35); invuln = Math.max(invuln, 1.5); persist(c);
            audio?.play('missionStep');
            toast(`Mission Relay · ${m.name}`);
          }
        } else if (segs && roam) {
          // Relays heal and catch him when he falls, as often as he visits.
          if ((m.cool ?? 0) > 0) continue;
          m.cool = 10;
          moveSpawn(m.x, m.z); hp = MAX_HP; heat = Math.max(0, heat - 35);
          audio?.play('missionStep');
          toast(m.goal ? m.name : `Mission Relay · ${m.name}`);
        } else {
          m.used = true;
          moveSpawn(m.x, m.z);
          hp = MAX_HP;
          if (m.goal) { won = true; audio?.play('missionDone'); }
          else audio?.play('missionStep');
          toast(m.goal ? m.name : `Beacon · ${m.name}`);
        }
      }
    }

    player.world.propagateModifiedBodyPositionsToColliders();
    // Show save failure after other progress messages, so an unlock cannot overwrite the warning.
    if (saveWarningPending) { toast('Progress is not saving in this browser'); saveWarningPending = false; }
    const boss = enemies.find(e => e.duel && !e.dead && e.ready && e.phase !== 'waiting' && Math.hypot(e.x - player.feet.x, e.z - player.feet.z) < 18);
    const mission = missions?.update(dt, { seg: c, cleared: c, won, enemies, driven: globalThis.window?.lab?.rides?.driven ?? null, heat, boss }) ?? null;
    const task = net ? net.task(player) : (mission?.opTitle ?? '');
    const watched = enemies.some(e => e.angry > 0 && !e.dead && !e.gone && sees(e));
    const atCairn = marks?.some(m => Math.hypot(m.x - px, m.z - pz) < 8);
    if (atCairn) heat = Math.max(0, heat - HEAT.safe * dt);
    else if (!watched) heat = Math.max(0, heat - HEAT.decay * dt);
    const stars = HEAT.stars.filter(t => heat >= t).length;
    if (segs && stars >= 2) {
      const alive = cops.filter(e => !e.dead && !e.gone);
      if (cops.length < HEAT.cops) {
        const spot = nextPavement(px + 16, pz - 10, 1, false);
        const cop = make({ kind: 'enforcer', x: spot.x, z: spot.z, police: true, seg: -1 }, cops.length);
        cop.homeX = spot.x; cop.homeZ = spot.z; cop.aggro = 36; cop.leash = 80; cop.mode = 'chase';
        addBody(cop); enemies.push(cop); cops.push(cop);
        if (alive.length === 0) toast('Police are in the district');
      }
    }
    if (stars >= 3 && !block) {
      const x = px + Math.sin(player.yaw) * 16, z = pz + Math.cos(player.yaw) * 16, y = heightAt(x, z);
      const mesh = new THREE.Mesh(new THREE.BoxGeometry(12, 1.3, 0.7), new THREE.MeshBasicMaterial({ color: 0x141c28 }));
      mesh.position.set(x, y + 0.65, z); mesh.rotation.y = player.yaw; scene.add(mesh);
      const col = player.world.createCollider(player.R.ColliderDesc.cuboid(6, 0.65, 0.35).setTranslation(x, y + 0.65, z).setRotation({ x: 0, y: Math.sin(player.yaw / 2), z: 0, w: Math.cos(player.yaw / 2) }));
      block = { mesh, col };
    } else if (stars < 3 && block) {
      scene.remove(block.mesh); player.world.removeCollider(block.col, true); block = null;
    }
    hud.update(dt, { hp, power, cleared: c, won, enemies, player, has, dashCool, flareCool, invuln, boss, heat, cash, stars, task, mission });
  }

  return { update, pose, enemies, hud, ram, get swing() { return swing; },
    /** Restores health cleanly from side contract completions or relays. */
    heal(n) { hp = Math.min(MAX_HP, hp + n); },
    /** The room's payment for a finished contract. */
    reward(n, text) { cash += n; hud.flashText(`+${n}`); toast(text); },
    get downed() { return downT > 0; }, get downLeft() { return downT; }, get mech() { return mech; },
    /** A teammate stood by him long enough. */
    revive() { if (downT <= 0) return false; downT = 0; hp = 45; invuln = 1.5; hurtPose = 0; toast('Back on your feet'); return true; },
    /** Another wanderer's blade in the duel ring. */
    hurtMe(n, from) { return downT <= 0 && takeHit(n, from, 4); },
    /** The host calls the mech into the street at (x, z). */
    summon(x, z) {
      const e = mech;
      if (!e) return;
      Object.assign(e, { x, z, homeX: x, homeZ: z, tx: x, tz: z, hp: e.maxHp, dead: 0, gone: false, mode: 'roam', cool: 0, windup: 0, stagger: 0, air: 0, vy: 0, kx: 0, kz: 0, wait: 1 });
      e.mesh.visible = true;
      if (!e.body) addBody(e);
    },
    /** What another page sent about a shared body: a guest's blow (host only), a strike or a shot (guests). */
    remote(m) {
      const e = enemies[m.i];
      if (!net || !e || e.netId !== m.i || e.dead) return;
      if (m.t === 'hit') {
        if (net.host) hurt(e, Math.max(0, Math.min(500, +m.n || 0)), KNOCK[m.src] != null || m.src === 'ram' ? m.src : 'sword', { x: +m.x || e.x, z: +m.z || e.z });
      } else if (!puppet(e)) return;
      else if (m.k === 'strike') {
        if (invuln <= 0 && hp > 0 && strikeHits(e, player.feet.x, player.feet.y, player.feet.z, heightAt(e.x, e.z))) takeHit(e.dmg, e, e.push);
      } else if (m.k === 'bolt' && e.shot) { e.attackYaw = +m.yaw || 0; fire(e, null, Math.max(-4, Math.min(4, +m.vy || 0))); }
    },
    /** For the vehicles: a jolt through the camera, and a knock he takes from his own crash. */
    jolt(k) { cameraKick = Math.max(cameraKick, k); }, crash(n) { hp -= n; hurtPose = 1; audio?.play('playerHurt'); if (hp <= 0) die(); },
    carjack() { hp = Math.max(1, hp - GUILT); heat = Math.min(HEAT.max, heat + HEAT.carjack); cameraKick = Math.max(cameraKick, 0.12); audio?.play('guilt'); hud.flashText('Innocent!'); toast('That driver did not hand you the keys.'); },
    get hp() { return hp; }, get power() { return power; },
    get combat() { return enemies.some((e) => !e.dead && !e.gone && (e.duel ? e.ready && e.phase !== 'waiting' : e.mode === 'chase') && Math.hypot(e.x - player.feet.x, e.z - player.feet.z) < 24) ? 1 : 0; },
    get seg() { return segs ? clearedCount() : 0; }, get powers() { return { ...powers }; },
    get roam() { return roam; }, get won() { return won; }, get hitPause() { return hitPause; }, get cameraKick() { return cameraKick; },
    unlocked: (kind) => unlocksFor(roam ? LEVELS.length : clearedCount()).includes(kind),
    /** How fast the world runs this frame: hit-stop, then the perfect-dodge slow motion. */
    get timeScale() { return net ? 1 : hitPause > 0 ? 0.12 : slowMo > 0 ? 0.35 : 1; }, // online the street never waits
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
  for (const k of ['civ', 'thief']) {
    if (!KIND[k].civ || KIND[k].aggro !== 0 || KIND[k].dmg <= 0 || !CAST[k] || !HEFT[k]) throw new Error('the street\'s people are bystanders until provoked, then can fight back: ' + k);
    if (HIT.hp[k] - HIT.sword[k] <= 0) throw new Error('one careless swing hurts, it does not kill: ' + k);
    if (HIT.hp[k] - 2 * HIT.sword[k] > 0) throw new Error('a second swing is a choice, and it kills: ' + k);
    if (RAM.per * RAM.kill < HIT.hp[k]) throw new Error('a car at speed kills on the pavement: ' + k);
    if (!(ramBand(RAM.stumble - 0.1) === 'stumble' && ramBand(RAM.kill - 0.1) === 'knock' && ramBand(RAM.kill) === 'kill')) throw new Error('a bump, a knockdown, then a kill');
    if (!(6 < HIT.hp[k] - 1 && 18 < HIT.hp[k])) throw new Error('the lower bands do not kill by themselves: ' + k);
  }
  if (KINDS.includes('civ') || KINDS.includes('thief')) throw new Error('bystanders are never part of a level\'s crew');
  if (!(GUILT * 2 < MAX_HP && GUILT > 0)) throw new Error('guilt hurts but one mistake is not death');
  if (!(RAM.per * RAM.min < HIT.hp.punk && RAM.max < HIT.hp.brute)) throw new Error('a nudge is not a kill, and no car drops the mech in one');
  if (!(RAM.crew < HIT.hp.punk && RAM.crew * 2 >= HIT.hp.punk)) throw new Error('a punk survives one ram, but not two committed hits');
  if (!(PERFECT > 0 && PERFECT < 0.5)) throw new Error('perfect-dodge window');
  demoConfig();
  demoLevels();
  demoDuels(); demoCombat();
  const earned = powersFor(LEVELS.length);
  for (const k of POWER_ORDER) if (!earned[k]) throw new Error('all powers by the clearing: ' + k);
}

if (globalThis.process?.argv?.[1]?.endsWith('hunt.js')) demo();
