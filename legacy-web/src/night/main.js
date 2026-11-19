// Memory 00 // Ridge Above Old Kyoto: Kael on the snowy mountain ridge and training cabin where he and Ren grew up
// before the Silent Autumn, painted with the same painter as the city (src/oilpaint) under the aurora (src/scenes.js).
//   ?capture       no clock: window.lab.step(dt, n) advances it (for stills and films)
//   ?quality=low   for slower machines: fewer pixels, no veil behind the aurora, less snow in the air
//   ?canvas=0 ?impasto=0..2 ?ink=0..1    parts of the finish, for comparing
import * as THREE from 'three';
import { patchBlock } from 'patch-block';
import recipe from '../../recipe.json';
import RAPIER from '../figure/rapier/rapier.mjs';
import { createShared, WIND } from '../oilpaint/common.js';
import { createBrushSheet } from '../oilpaint/brush.js';
import { createShade } from '../oilpaint/shade.js';
import { createFrame } from '../oilpaint/finish.js';
import { createGraphics } from '../quality.js';
import { noise } from '../world/noise.js';
import { createFigure } from '../figure/figure.js';
import { Animator } from '../figure/animator.js';
import { Player } from '../figure/player.js';
import { Input } from '../figure/input.js';
import { Follow } from '../figure/camera.js';
import { createHunt } from '../game/hunt.js';
import { createNet } from '../game/net.js';
import { getAudio } from '../game/audio.js';
import { createDayNight } from '../game/daynight.js';
import { prepareLand, createLand, heightAt, crestX, START, MOON, EXTENT, CABIN } from './land.js';
import { createSnow } from './snow.js';
import { createSky } from './sky.js';
import { createFar } from './far.js';
import { createTracks } from './tracks.js';
import { createLantern } from './lantern.js';
import { createDrift } from './drift.js';
import { createPines } from './pines.js';
import { createCabin } from './cabin.js';

const q = new URLSearchParams(location.search);
const capture = q.has('capture');

let shown = true;
let loaded = false;
const block = patchBlock({
  ...recipe.block,
  onSet() {},
  onTrigger() {},
  onVisible(visible) { shown = visible; if (loaded) run(); },
});
block.progress(0.03, 'Laying the ground');
const timings = {};
let mark = performance.now();
const lap = (name) => { const now = performance.now(); timings[name] = Math.round(now - mark); mark = now; };

const renderer = new THREE.WebGLRenderer({ antialias: false, powerPreference: 'high-performance' });
renderer.setPixelRatio(capture ? 1 : Math.min(devicePixelRatio, 1.5));
renderer.setSize(innerWidth, innerHeight);
renderer.autoClear = false;
renderer.outputColorSpace = THREE.LinearSRGBColorSpace;
document.getElementById('view').appendChild(renderer.domElement);

// the night: a low moon for the sun, thin clear air, and the lantern's colour
const shared = createShared();
shared.uAnime.value = 1;
shared.uImpasto.value = 0;
shared.tBrush.value = createBrushSheet();
shared.uSunDir.value.copy(MOON);
// a darker, closer night than before: thick blue air swallows the far ridges, so the eye stays near the lantern
shared.uHaze.value.setRGB(0.018, 0.04, 0.07);
shared.uHazeSun.value.setRGB(0.04, 0.08, 0.12);
shared.uHazeGlow.value.setRGB(0.03, 0.045, 0.06);
shared.uHazeDensity.value = 0.0021;
shared.uRim.value.setRGB(0.22, 0.55, 0.62); // the aurora's light catching the edge of figures
shared.uShadeBias.value = 0.0004;
shared.uBounce.value.setRGB(0.015, 0.06, 0.07);
shared.uLampCol.value.setRGB(1.0, 0.6, 0.24);
shared.uGlazeMul.value.setRGB(0.3, 0.38, 0.62); // his shadow on the snow is blue
shared.uGlazeAdd.value.setRGB(0.0, 0.008, 0.03);
shared.uGlow = { value: new THREE.Vector3(1, 0, 0) };
shared.uWarm = { value: [new THREE.Vector4(), new THREE.Vector4(), new THREE.Vector4()] };
shared.uRoom = { value: new THREE.Vector4(0, 0, -1, -1) };
if (q.has('impasto')) shared.uImpasto.value = +q.get('impasto');
const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(46, innerWidth / innerHeight, 0.3, 16000);
// Listened for from the start, not once loaded: the page may go fullscreen while the world is still being made.
// The frame's target joins in once it exists.
let fitFrame = () => {};
let graphics = null;
function fitView() {
  renderer.setSize(innerWidth, innerHeight);
  camera.aspect = innerWidth / innerHeight;
  camera.updateProjectionMatrix();
  if (graphics) graphics.resize();
  else fitFrame();
}
addEventListener('resize', fitView);
lap('start');

// what he wears by night: light, half-tone, shade under the moon, then the cloth's own colour for the lantern
const NIGHT_CLOTHES = {
  CLOAK: [[0.2, 0.42, 0.6], [0.07, 0.17, 0.32], [0.025, 0.06, 0.15], [0.16, 0.3, 0.5]],
  HAT: [[0.4, 0.46, 0.44], [0.17, 0.2, 0.22], [0.06, 0.075, 0.1], [0.85, 0.72, 0.42]],
  SHIRT: [[0.38, 0.45, 0.5], [0.15, 0.2, 0.27], [0.055, 0.08, 0.13], [0.8, 0.72, 0.56]],
  TROUSERS: [[0.26, 0.28, 0.3], [0.12, 0.13, 0.17], [0.05, 0.055, 0.09], [0.45, 0.32, 0.2]],
  SKIN: [[0.6, 0.53, 0.5], [0.3, 0.25, 0.27], [0.12, 0.1, 0.14], [0.85, 0.62, 0.46]],
  BOOTS: [[0.2, 0.18, 0.17], [0.09, 0.08, 0.09], [0.04, 0.035, 0.05], [0.35, 0.22, 0.14]],
  HAIR: [[0.18, 0.15, 0.14], [0.09, 0.07, 0.08], [0.04, 0.035, 0.05], [0.3, 0.2, 0.13]],
  EYES: [[0.1, 0.09, 0.09], [0.06, 0.05, 0.06], [0.03, 0.03, 0.04], [0.15, 0.1, 0.08]],
};
let told = 0;
const say = (value, label) => { told = Math.max(told, value); block.progress(told, label); };
const figureComing = createFigure(shared, (done) => say(0.05 + 0.55 * done, 'Fetching the wanderer'), NIGHT_CLOTHES);

const turn = () => new Promise((resolve) => setTimeout(resolve, 0));
await prepareLand((done) => { say(0.03 + 0.09 * done, 'Laying the ground'); return turn(); });
const land = createLand(shared);
lap('land');
say(0.12, 'Laying the snow');
await turn();
const snow = createSnow(shared);
const pines = createPines(shared);
land.shade.add(pines.caster); // the moon lays their shadows down the snow with the horns'
const cabin = createCabin(shared);
land.shade.add(cabin.caster);
lap('snow');
const sky = createSky(shared);
const far = createFar(shared);
const tracks = createTracks(shared);
const drift = createDrift(shared);
const lantern = createLantern(shared);
// the moon sees the whole ridge: its picture of the shade is what lays the horns' shadow down the snow
const shade = createShade(renderer, shared, land.shade, { sun: MOON, size: [4096, 3072], box: [740, 560], centre: [0, 150, 0], back: 1600, near: 700, far: 2500 });
lap('shade');
say(0.25, 'Fetching the wanderer');
await turn();
scene.add(sky.group, far.group, land.mesh, snow.group, pines.group, cabin.group, tracks.mesh, lantern.object, lantern.glow, drift);

await RAPIER.init();
const physics = new RAPIER.World({ x: 0, y: -9.81, z: 0 });
{
  const f = land.heightField();
  physics.createCollider(RAPIER.ColliderDesc.heightfield(f.cells, f.cells, f.heights, { x: 2 * f.half, y: 1, z: 2 * f.half }));
  for (const t of pines.colliders) physics.createCollider(RAPIER.ColliderDesc.cylinder(4, t.r).setTranslation(t.x, t.y + 4, t.z));
  for (const s of cabin.solid) physics.createCollider(RAPIER.ColliderDesc.cuboid(s.hx, s.hy, s.hz).setTranslation(s.x, s.y, s.z));
}
lap('physics');

const figure = await figureComing;
const { man } = figure;
// Realistic PBR lighting handles moonlight & warm lantern naturally without darkening base albedo.
const avatarTints = [];
for (const m of figure.avatar?.materials.values() ?? []) {
  avatarTints.push({ mat: m, base: m.uniforms.uTint.value.clone(), night: m.uniforms.uTint.value.clone() });
}
scene.add(man.object, figure.cloakMesh, figure.dabs);
const animator = new Animator(man);
const spawn = new THREE.Vector3(START.x, heightAt(START.x, START.z), START.z);
const player = new Player({ RAPIER, world: physics, man, animator, spawn, yaw: Math.PI });
const follow = new Follow(camera);
follow.yaw = Math.PI;
// the eye sits low and looks up past him: the picture is mostly sky
follow.pitch = -0.07;
follow.lift = 1.5;
follow.sink = 0.7;
const input = new Input(renderer.domElement, { pads: !capture && !q.has('nopad') });
physics.step();
// he has come up from the south: his tracks are already there behind him
tracks.lay(crestX, START.z + 16, START.z + 0.4);
lap('figure');

const frame = createFrame(renderer, shared);
addEventListener('pagehide', () => {
  renderer.setAnimationLoop(null);
  frame.target.dispose();
  renderer.dispose();
});
fitFrame = () => frame.resize();
frame.uniforms.uCanvas.value = 0;
frame.uniforms.uShafts.value = 0;
// Realistic AAA Snowbound mode: no ink outlines, rich HDR aurora & cabin bloom, subtle optical vignette
frame.uniforms.uInk.value = 0;
frame.uniforms.uInkColor.value.setRGB(0.008, 0.014, 0.035);
frame.uniforms.uInkFar.value = 170;
frame.uniforms.uVignette.value = 0.28;
if (q.has('ink')) frame.uniforms.uInk.value = +q.get('ink');
shared.uAurora.value = 1;
shared.uExposure.value = 1.15;
graphics = createGraphics({ renderer, capture, apply(profile) {
  frame.setSamples(profile.samples);
  frame.resize();
  frame.uniforms.uAOStrength.value = profile.level === 'low' ? 0 : profile.level === 'medium' ? 0.30 : 0.42;
  frame.uniforms.uBloomStrength.value = profile.level === 'low' ? 0.28 : 0.44;
  sky.setQuality(profile.level);
  drift.geometry.instanceCount = profile.drift;
} });

const wind = { x: WIND.x, z: WIND.y };
let time = 0;
let hunt = null;
let net = null;
let hudChapter = null, hudTitle = null, hudObjective = null;
const memoryQueue = [];
const audio = getAudio({ scene: 'aurora', silent: capture });
function emitMemory(speaker, line) {
  if (!hunt || !line) return;
  audio.speak(line, speaker);
  hunt.hud?.comms?.(speaker, line);
}
const ridgeMarks = [
  {
    x: crestX(212), z: 212, name: 'First Pine Cairn',
    speaker: 'ren',
    line: 'Father stacked these first stones at the timberline so we could find home in any whiteout. Keep your lantern steady, Kael.',
  },
  {
    x: crestX(150), z: 150, name: 'Hana’s Overlook',
    speaker: 'mira',
    line: 'Hana’s Overlook... Look north under the corona—one warm window at the foot of the horn, where her cello waited for you both.',
  },
  {
    x: crestX(22), z: 22, name: 'Old Training Ridge',
    speaker: 'ren',
    line: 'Two steps in the drift, one clean parry on the ice. Every trial my Specters set in Neo-Kyoto began on this ridge, little brother.',
  },
  {
    x: CABIN.x, z: CABIN.z + 7, name: 'The Brothers’ Cabin', goal: true,
    speaker: 'kael',
    line: 'The fire is still burning inside... Before the Silent Autumn, we were whole here. Rest easy, Ren—I’m carrying this warmth back to Kyoto.',
  },
];
let stepDist = 0;
const online = q.get('mode') === 'online' && !capture;
const dayNight = createDayNight({
  startPhase: 'night',
  online,
  q,
  onShift(phase) {
    hunt?.hud?.toast(phase === 'day' ? 'Arctic dawn breaks over Old Kyoto Ridge — 10 min until night' : 'Polar night falls — the corona awakens');
    hunt?.hud?.flashText(phase === 'day' ? 'Arctic Dawn' : 'Polar Night');
  },
});
const dn0 = dayNight.update(0);
shared.uNight.value = dn0.night;
shared.uTwilight.value = dn0.twilight;
function air(x, y, z, out) {
  const along = x * wind.x + z * wind.z, across = -x * wind.z + z * wind.x;
  const gust = THREE.MathUtils.clamp(0.5 + 0.8 * noise((along - time * 4) / 40, across / 90) + 0.3 * noise((along - time * 6.5) / 11, across / 17), 0, 1);
  const speed = 0.6 + 3.6 * gust * gust;
  return out.set(wind.x * speed, 0.25 * Math.sin(time * 2.3 + x) * gust, wind.z * speed);
}

function place(x, z, yaw) {
  player.teleport(x, heightAt(x, z), z);
  player.yaw = player.lastYaw = yaw;
  animator.lock = null; animator.lockOff = null;
  animator.enter(animator.locoState({ strafe: false }), 0.05);
}

// He holds the lantern out at his side: the right arm is set to that after the clips have had it, upper arm out
// and a little forward, forearm forward and level. Directions are in his own frame (+x his left, +z ahead).
const HOLD = [['upperarm_r', 'lowerarm_r', new THREE.Vector3(-0.72, -0.52, 0.42).normalize()], ['lowerarm_r', 'hand_r', new THREE.Vector3(-0.42, 0.16, 0.86).normalize()]];
const from = new THREE.Vector3(), to = new THREE.Vector3(), swing = new THREE.Quaternion(), pq = new THREE.Quaternion(), wq = new THREE.Quaternion();
const grip = new THREE.Vector3(), forearm = new THREE.Vector3(), back = new THREE.Vector3(), v1 = new THREE.Vector3(), v2 = new THREE.Vector3(), want = new THREE.Vector3();
const UP = new THREE.Vector3(0, 1, 0);
const at = (name, out) => out.setFromMatrixPosition(man.bones[name].matrixWorld);
const HAND_TURN = q.has('hand') ? +q.get('hand') : 0; // a turn of the wrist added to that, for trying
function holdLantern() {
  for (const [bone, child, dir] of HOLD) {
    const b = man.bones[bone];
    from.setFromMatrixPosition(man.bones[child].matrixWorld).sub(to.setFromMatrixPosition(b.matrixWorld)).normalize();
    to.copy(dir).applyQuaternion(man.object.quaternion);
    swing.setFromUnitVectors(from, to);
    b.parent.getWorldQuaternion(pq); b.getWorldQuaternion(wq);
    b.quaternion.copy(pq.invert().multiply(swing.multiply(wq)));
    b.updateMatrixWorld(true);
  }
  // The hand is turned about the forearm until its back is uppermost, and closed into a fist: the lantern's
  // handle hangs from inside it. (The back of the hand is found from the knuckles: along the hand, across it.)
  const hand = man.bones.hand_r;
  forearm.copy(at('hand_r', v1)).sub(at('lowerarm_r', v2)).normalize();
  at('middle_01_r', v1).sub(at('hand_r', want));
  at('index_01_r', v2).sub(at('pinky_01_r', want));
  back.crossVectors(v1, v2);
  back.addScaledVector(forearm, -back.dot(forearm)).normalize();
  want.copy(UP).addScaledVector(forearm, -UP.dot(forearm)).normalize();
  animator.rotateWorld(hand, forearm, Math.atan2(forearm.dot(v1.crossVectors(back, want)), back.dot(want)) + HAND_TURN);
  for (const f of animator.fingerPoses ?? []) if (f.bone.name.endsWith('_r')) f.bone.quaternion.slerpQuaternions(f.flat, f.fist, f.thumb ? 0.75 : 1);
  hand.updateMatrixWorld(true);
  grip.set(0, 0, 0);
  for (const name of ['index_01_r', 'ring_01_r', 'index_02_r', 'ring_02_r']) grip.add(at(name, v1));
  grip.multiplyScalar(0.25);
}

const size = new THREE.Vector2();
// Through his own eyes: always in the cabin and on its doorstep, where there is no room for an eye behind him,
// and anywhere else with V. `first` is how far the eye has gone over to it, 0..1.
let first = 0, firstKey = false;
const outlook = new THREE.Vector3(), eyeAt = new THREE.Vector3(), gaze = new THREE.Vector3();
addEventListener('keydown', (e) => { if (e.code === 'KeyV') firstKey = !firstKey; });
function tick(dt, draw = true) {
  dt = Math.min(dt, 1 / 20);
  const realDt = dt;
  dt *= hunt?.timeScale ?? 1;
  time += dt;
  // Online the ridge goes on under the menu (the host runs it for everyone), and a downed wanderer waits for a hand.
  if (window.__paused || hunt?.downed) input.reset();
  let inp = input.read(realDt);
  const isIn = cabin.inside(player.feet);
  first += ((firstKey || cabin.threshold(player.feet) ? 1 : 0) - first) * (1 - Math.exp(-6 * dt));
  const fp = first * first * (3 - 2 * first);
  cabin.update(time);
  if (window.__paused) hunt?.combatInput(dt, null, follow.yaw);
  else inp = hunt?.combatInput(dt, inp, follow.yaw, fp < 0.5) ?? inp;
  player.update(dt, inp, inp.combatYaw ?? follow.yaw);
  // off the ridge and into the fjord, or off the map: he starts again
  if (player.feet.y < 1.5 || Math.max(Math.abs(player.feet.x), Math.abs(player.feet.z)) > EXTENT - 30) place(spawn.x, spawn.z, Math.PI);
  const root = player.visualRoot(dt);
  man.object.position.copy(root);
  man.object.rotation.set(0, player.yaw, 0);
  man.object.updateMatrixWorld(true);
  const local = player.vel.clone().applyAxisAngle(new THREE.Vector3(0, 1, 0), -player.yaw);
  animator.update(dt, { speed: player.mode === 'move' ? player.speed : Math.hypot(local.x, local.z), moveAngle: player.moveAngle, grounded: player.grounded });
  animator.relaxHands(player.speed);
  animator.uprightTrunk(dt, player.speed, player.grounded && player.mode !== 'jump' && player.mode !== 'air' ? 1 : 0.3);
  const lookW = player.mode === 'move' || player.mode === 'idleExtra' ? THREE.MathUtils.clamp(1 - (player.speed - 1.5) / 2, 0, 1) : 0;
  animator.lookToward(dt, camera.getWorldDirection(new THREE.Vector3()), lookW);
  animator.armClearance(dt);
  if (player.mode === 'move' && !player.strafe) animator.leanInto(dt, player.turnRate, player.speed, player.accelF);
  else animator.leanInto(dt, 0, 0, 0);
  const onGround = player.grounded && player.mode !== 'jump' && player.mode !== 'air';
  animator.placeFeet(dt, (x, z, from) => player.ground(x, z, from, 1.4), root.y, onGround ? 1 : 0);
  man.object.updateMatrixWorld(true);
  holdLantern();
  dayNight.update(time);
  hunt?.update(dt, { ...inp, viewYaw: follow.yaw }, realDt);
  if (hunt) {
    if (hudChapter) hudChapter.textContent = hunt.won ? 'Memory 00 // Complete' : 'Memory 00 // Prologue';
    if (hudTitle) hudTitle.textContent = 'Ridge Above Old Kyoto';
    if (hudObjective && !online) {
      const nextMark = ridgeMarks.find((m) => !m.used);
      hudObjective.textContent = hunt.won
        ? 'Step inside the Brothers’ Cabin and rest by the hearth.'
        : nextMark ? `Reach ${nextMark.name}.` : 'Follow the ridge cairns north.';
    }
    for (const m of ridgeMarks) {
      if (m.used && !m.spoken) {
        m.spoken = true;
        memoryQueue.length = 0;
        emitMemory(m.speaker, m.line);
      }
    }
    if (memoryQueue.length > 0 && !window.__paused) {
      memoryQueue[0].timer -= realDt;
      if (memoryQueue[0].timer <= 0) {
        const next = memoryQueue.shift();
        emitMemory(next.speaker, next.line);
      }
    }
  }
  net?.update(realDt, { player, driving: false });
  if (player.grounded && player.speed > 1.2) {
    stepDist += player.speed * dt;
    if (stepDist >= 2.2) { stepDist = 0; audio.play('step', Math.min(1.4, player.speed / 5)); }
  } else if (player.grounded) {
    stepDist = Math.min(stepDist, 1.5);
  }
  audio.update(realDt, { combat: hunt?.combat ?? 0, paused: !!window.__paused });
  hunt?.pose();
  man.object.updateMatrixWorld(true);
  figure.updateCloak(dt, air, heightAt);
  if (man.appearance) {
    const fingers = ['index_01_r', 'ring_01_r', 'index_02_r', 'ring_02_r'];
    grip.set(0, 0, 0);
    for (const name of fingers) grip.add(man.appearance.bindings.find(b => b.from.name === name).to.getWorldPosition(v1));
    grip.multiplyScalar(0.25);
  }
  lantern.update(dt, grip, player.yaw);
  if (!isIn) tracks.update(root, player.yaw, player.speed, onGround);

  // a trunk between the eye and him brings the eye in; over the ground it rises instead
  follow.shake = hunt?.cameraKick ?? 0;
  if (inp.combatYaw != null && !follow.free) follow.track(inp.combatYaw, realDt);
  follow.update(realDt, root, inp.combatYaw == null ? inp.look : { x: 0, y: inp.look.y }, inp.zoom, (o, d, max) => {
    let hit = player.castRay(o, d, max);
    if (hit != null && o.y + d.y * hit - heightAt(o.x + d.x * hit, o.z + d.z * hit) < 0.5) hit = null;
    const tree = pines.block(o, d, max);
    return tree != null && (hit == null || tree < hit) ? tree : hit;
  }, (x, z) => heightAt(x, z) + 0.55);
  if (fp > 0.002) {
    // the eye comes forward into his head and looks where the mouse has it look
    const cp = Math.cos(follow.pitch), sp = Math.sin(follow.pitch);
    gaze.set(Math.sin(follow.yaw) * cp, -sp, Math.cos(follow.yaw) * cp);
    eyeAt.set(root.x + Math.sin(follow.yaw) * 0.08, root.y + 1.66, root.z + Math.cos(follow.yaw) * 0.08);
    camera.getWorldDirection(outlook).multiplyScalar(6).add(camera.position);
    camera.position.lerp(eyeAt, fp);
    camera.lookAt(outlook.lerp(eyeAt.addScaledVector(gaze, 6), fp));
  }
  // seen from so close the picture is wider, and nothing of him is in it: the lantern's light is, the lantern isn't
  lantern.object.visible = lantern.glow.visible = fp < 0.5;
  camera.fov = 46 + 18 * fp;
  camera.near = 0.3 - 0.2 * fp;
  camera.updateProjectionMatrix();
  man.object.visible = figure.cloakMesh.visible = fp < 0.5;
  figure.dabs.visible = shared.uAnime.value < 0.5 && fp < 0.5;
  if (draw) renderScene();
}

const DAY_HAZE = new THREE.Color(0.62, 0.78, 0.90), NIGHT_HAZE = new THREE.Color(0.018, 0.04, 0.07);
const DAY_HAZE_SUN = new THREE.Color(0.95, 0.92, 0.82), NIGHT_HAZE_SUN = new THREE.Color(0.04, 0.08, 0.12);
const DAY_HAZE_GLOW = new THREE.Color(0.12, 0.10, 0.04), NIGHT_HAZE_GLOW = new THREE.Color(0.03, 0.045, 0.06);
const DAY_RIM = new THREE.Color(0.28, 0.34, 0.38), NIGHT_RIM = new THREE.Color(0.22, 0.55, 0.62);
const DAY_BOUNCE = new THREE.Color(0.10, 0.14, 0.18), NIGHT_BOUNCE = new THREE.Color(0.015, 0.06, 0.07);
const DAY_GLAZE = new THREE.Color(0.55, 0.62, 0.78), NIGHT_GLAZE = new THREE.Color(0.3, 0.38, 0.62);
function renderScene() {
  camera.updateMatrixWorld(true);
  hunt?.hud.focusMarker(hunt.visibleTarget);
  camera.updateMatrixWorld(true);
  renderer.getDrawingBufferSize(size);
  const dn = dayNight.update(time);
  const n = dn.night;
  shared.uNight.value = n;
  shared.uTwilight.value = dn.twilight;
  shared.uTime.value = time;
  shared.uPixel.value = (2 * Math.tan(THREE.MathUtils.degToRad(camera.fov) / 2)) / size.y;
  shared.uFocus.value.copy(player.feet);
  // Interpolate between Polar Night (n = 1) and crisp Arctic Day (n = 0).
  shared.uHaze.value.copy(DAY_HAZE).lerp(NIGHT_HAZE, n);
  shared.uHazeSun.value.copy(DAY_HAZE_SUN).lerp(NIGHT_HAZE_SUN, n);
  shared.uHazeGlow.value.copy(DAY_HAZE_GLOW).lerp(NIGHT_HAZE_GLOW, n);
  shared.uRim.value.copy(DAY_RIM).lerp(NIGHT_RIM, n);
  shared.uBounce.value.copy(DAY_BOUNCE).lerp(NIGHT_BOUNCE, n);
  shared.uGlazeMul.value.copy(DAY_GLAZE).lerp(NIGHT_GLAZE, n);
  shared.uLamp.value.w *= 0.15 + 0.85 * n;
  frame.uniforms.uVignette.value = 0.15 + 0.47 * n;
  for (const t of avatarTints) t.mat.uniforms.uTint.value.copy(t.base).lerp(t.night, n);
  // the aurora breathes: brighter and dimmer over a minute or so, and the snow under it with it
  shared.uGlow.value.x = (0.62 + 0.17 * Math.sin(time * 0.11) + 0.09 * Math.sin(time * 0.37 + 1.0) + 0.05 * Math.sin(time * 1.1 + 2.0)) * n;
  snow.follow(camera.position.x, camera.position.z);
  shade.update(scene, player.feet);
  frame.render(scene, camera);
}

window.lab = {
  THREE, player, follow, camera, input, animator, figure, shared, frame, renderer, graphics, timings, counts: { ...snow.counts, ...pines.counts, ...cabin.counts }, cabin, heightAt, crestX, tracks, sky, lantern, audio, dayNight,
  step(dt = 1 / 60, n = 1, draw = true) { for (let i = 0; i < n; i++) tick(dt, draw && i === n - 1); },
  setInput(fn) { input.override = fn; },
  /** Puts him at (x, z) on the ground, facing (fx, fz), with the eye behind him. */
  teleport(x, z, fx, fz) {
    hunt?.clearFocus();
    place(x, z, Math.atan2(fx - x, fz - z));
    follow.yaw = player.yaw; follow.pivot.set(0, 0, 0);
    tick(1 / 60);
  },
  /** A fixed view for stills: eye and what it looks at. Call with nothing to go back to following him. */
  view(eye, target) { follow.free = eye ? { position: new THREE.Vector3(...eye), target: new THREE.Vector3(...target) } : null; },
  /** Moves the clock on without moving him, to see the sky at another moment. */
  wait(seconds) { time += seconds; },
  /** Through his eyes (true) or from behind him (false), as V does. */
  first(on) { firstKey = on; },
  /** His tracks along the crest from z0 to z1, as if he had walked it. */
  trail(z0, z1) { tracks.lay(crestX, z0, z1); },
};

if (!capture) {
  const kinds = ['punk', 'punk', 'enforcer', 'punk', 'gunner', 'enforcer', 'blade'];
  if (online) net = window.lab.net = createNet({ scene, shared, man, camera, heightAt, pool: 'aurora' });
  hunt = await createHunt({
    net, roam: online, // online the crews come back, as in free roam
    scene, shared, man, animator, player, heightAt, camera, audio,
    moveSpawn: (x, z) => spawn.set(x, heightAt(x, z), z),
    down: () => place(spawn.x, spawn.z, Math.PI),
    route: Array.from({ length: 99 }, (_, i) => { const z = START.z + 20 - i * 4; return [crestX(z), z]; }),
    marks: ridgeMarks,
    spots: [275, 230, 190, 100, 45, 0, -28].map((z, i) => ({
      kind: kinds[i], x: crestX(z) + (i % 2 ? 2 : -2), z,
    })),
  });
  window.lab.hunt = hunt;
  net?.attach(hunt);
  const hudRoot = hunt.hud?.element;
  hudChapter = hudRoot?.querySelector('.chapter-label') ?? null;
  hudTitle = hudRoot?.querySelector('.quest h2') ?? null;
  hudObjective = hudRoot?.querySelector('.objective') ?? null;
  const hudMapLabel = hudRoot?.querySelector('.map-label');
  if (hudMapLabel) hudMapLabel.textContent = 'Scan · Old Kyoto Ridge';
}

say(0.68, 'Mixing paint');
function gpuDone() {
  const gl = renderer.getContext();
  const sync = gl.fenceSync(gl.SYNC_GPU_COMMANDS_COMPLETE, 0);
  gl.flush();
  return new Promise((resolve) => {
    const poll = () => {
      if (gl.clientWaitSync(sync, 0, 0) === gl.TIMEOUT_EXPIRED) return void setTimeout(poll, 4);
      gl.deleteSync(sync);
      resolve();
    };
    poll();
  });
}
if (!capture) {
  await renderer.compileAsync(scene, camera);
  lap('shaders');
  say(0.86, 'Mixing paint');
}
for (let i = 0; i < (capture ? 1 : 5); i++) {
  tick(1 / 60);
  if (capture) break;
  await gpuDone();
  say(0.88 + 0.025 * i, 'First strokes');
  await turn();
}
lap('first frame');

let running = false;
let last = 0;
let accumulator = 0;
function run() {
  const want = shown && !capture;
  if (want === running) return;
  running = want;
  last = performance.now();
  renderer.setAnimationLoop(want ? () => {
    const now = performance.now();
    graphics.sample((now - last) / 1000, !window.__paused);
    if (!window.__paused || online) {
      accumulator += Math.min((now - last) / 1000, 0.1);
      while (accumulator >= 1 / 60) { tick(1 / 60, false); accumulator -= 1 / 60; }
    } else {
      accumulator = 0;
      audio.update(1 / 60, { paused: true, driving: false });
    }
    last = now;
    renderScene();
  } : null);
}
if (!capture) {
  const hint = document.getElementById('hint');
  hint.textContent = 'WASD move · Space jump · J/F strike · Q spark · V first person · Esc menu';
  setTimeout(() => hint.classList.add('faded'), 9000);
  const banner = hunt?.hud?.element?.querySelector('.chapter-banner');
  if (banner) {
    banner.querySelector('span').textContent = 'Memory 00 // Ridge Above Old Kyoto';
    banner.querySelector('strong').textContent = 'Snowbound';
    banner.querySelector('p').textContent = 'Where Kael and Ren grew up before the Silent Autumn';
    banner.hidden = false;
    banner.classList.add('visible');
  }
  if (!q.has('clean')) {
    emitMemory('mira', 'Memory Zero-Zero — the ridge above Old Kyoto. Before the Silent Autumn, you and Ren learned every cut in this snow while Hana held the lantern.');
    memoryQueue.push({
      speaker: 'ren',
      line: 'Follow our father’s cairns north to the cabin, Kael. Show me your blade still remembers the winter wind.',
      timer: 8.2,
    });
  }
}
fitView();
loaded = true;
run();
document.getElementById('loading')?.remove();
if (q.has('clean') || capture) document.getElementById('hint')?.remove();
block.ready();
window.lab.ready = true;
window.__block = { ready: true, block, timings, set() {} };
