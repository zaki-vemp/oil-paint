// The spirit trail: six cel-shaded chapters with one shared wanderer, terrain and physics world.
// Street crews on the way (src/game/hunt.js): click or F swings, Q throws a spark, cairns mark the way to the clearing.
// Shown with ?scene=autumn (src/scenes.js).
//   ?capture       no clock: window.lab.step(dt, n) advances it (for stills and films)
//   ?quality=low   fewer leaves in the air, no sun in the mist
//   ?shafts=0 ?canvas=0 ?impasto=0..2    parts of the finish, for comparing
import * as THREE from 'three';
import { patchBlock } from 'patch-block';
import recipe from '../recipe.json';
import RAPIER from './figure/rapier/rapier.mjs';
import { createShared, WIND, EXTENT } from './oilpaint/common.js';
import { createBrushSheet } from './oilpaint/brush.js';
import { prepareLand, createLand, heightAt, wayDist, forestAt, mountainHeight, noise, way, START } from './world/land.js';
import { createAnimeWorld } from './world/anime.js';
import { createStreetProps } from './world/props.js';
import { createGraphics } from './quality.js';
import { createShade } from './oilpaint/shade.js';
import { createFrame } from './oilpaint/finish.js';
import { createFigure } from './figure/figure.js';
import { Animator } from './figure/animator.js';
import { Player } from './figure/player.js';
import { Input } from './figure/input.js';
import { Follow } from './figure/camera.js';
import { createHunt } from './game/hunt.js';
import { createMissions } from './game/missions.js';
import { createRides } from './game/ride.js';
import { createNet } from './game/net.js';
import { getAudio } from './game/audio.js';
import { chapterAt } from './game/chapters.js';
import { LEVELS, loadProgress } from './game/levels.js';
import { createDayNight } from './game/daynight.js';
import { createShowcase } from './game/showcase.js';

const q = new URLSearchParams(location.search);
const capture = q.has('capture');
window.__mode = ['roam', 'online'].includes(q.get('mode')) ? q.get('mode') : 'level';
const online = window.__mode === 'online' && !capture;
let hunt = null;
const dayNight = createDayNight({
  startPhase: 'day',
  online,
  q,
  onShift(phase) {
    hunt?.hud?.toast(phase === 'night' ? 'Night falls — 10 min until dawn' : 'Dawn breaks — 10 min until nightfall');
    hunt?.hud?.flashText(phase === 'night' ? 'Nightfall' : 'Daybreak');
  },
});

// Patch's page talks to the demo through this. There are no controls yet; it carries loading, ready and hiding.
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

const renderer = new THREE.WebGLRenderer({ antialias: true, powerPreference: 'high-performance' });
renderer.setPixelRatio(capture ? 1 : Math.min(devicePixelRatio, 1.5));
renderer.setSize(innerWidth, innerHeight);
renderer.autoClear = false;
renderer.outputColorSpace = THREE.LinearSRGBColorSpace;
document.getElementById('view').appendChild(renderer.domElement);

const shared = createShared();
shared.uAnime.value = 1;
shared.uImpasto.value = 0;
shared.uHazeDensity.value = 0.008; // smog: the towers a street back already sit in it
shared.uBounce.value.setRGB(0.06, 0.1, 0.05);
shared.uGlazeMul.value.setRGB(0.65, 0.72, 0.75);
shared.uRim.value.setRGB(0.3, 0.26, 0.14); // warm back light on figures, the cel look's separation
shared.tBrush.value = createBrushSheet();
if (q.has('impasto')) shared.uImpasto.value = +q.get('impasto');
const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(52, innerWidth / innerHeight, 0.2, 2000);
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
// the wanderer's files are the large ones: they start coming down now, while the wood is made
let told = 0;
const say = (value, label) => { told = Math.max(told, value); block.progress(told, label); };
const figureComing = createFigure(shared, (done) => say(0.05 + 0.55 * done, 'Fetching the wanderer'));

const turn = () => new Promise((resolve) => setTimeout(resolve, 0));
await prepareLand((done) => { say(0.03 + 0.07 * done, 'Laying the ground'); return turn(); });
const land = createLand(shared);
lap('land');
say(0.1, 'Raising the city');
await turn();
const forest = createAnimeWorld(shared);
const dn0 = dayNight.update(0);
shared.uNight.value = dn0.night;
shared.uTwilight.value = dn0.twilight;
forest.update(10, START.z, 0, true);
let lastNight = dn0.night;
const street = await createStreetProps({ shared, heightAt, solids: forest.colliders });
forest.group.add(street.group);
forest.shade.add(street.shade);
forest.colliders.push(...street.colliders);
lap('forest');
// the baked shade has to hold the whole expanded city district, not only the first small wood
const shade = createShade(renderer, shared, forest.shade, { box: [640, 640], centre: [0, 24, 0], back: 950, far: 2400 });
lap('shade');
say(0.25, 'Fetching the wanderer');
await turn();
scene.add(land.mesh, forest.group);

// --- the ground and the trunks, for his feet ---
await RAPIER.init();
const physics = new RAPIER.World({ x: 0, y: -9.81, z: 0 });
{
  const f = land.heightField();
  physics.createCollider(RAPIER.ColliderDesc.heightfield(f.cells, f.cells, f.heights, { x: 2 * f.half, y: 1, z: 2 * f.half }));
  for (const t of forest.colliders) {
    const shape = t.rock ? RAPIER.ColliderDesc.ball(t.r).setTranslation(t.x, t.y, t.z)
      : t.box ? RAPIER.ColliderDesc.cuboid(...t.box).setTranslation(t.x, t.y, t.z).setRotation({ x: 0, y: Math.sin(t.ry / 2), z: 0, w: Math.cos(t.ry / 2) })
      : RAPIER.ColliderDesc.cylinder(t.h / 2, t.r).setTranslation(t.x, t.y + t.h / 2, t.z);
    physics.createCollider(shape);
  }
}
lap('physics');

const figure = await figureComing;
const { man } = figure;
scene.add(man.object, figure.cloakMesh, figure.dabs);
const animator = new Animator(man);
const spawn = new THREE.Vector3(START.x, heightAt(START.x, START.z), START.z);
const player = new Player({ RAPIER, world: physics, man, animator, spawn, yaw: Math.PI });
const follow = new Follow(camera);
follow.yaw = Math.PI;
const input = new Input(renderer.domElement, { pads: !capture && !q.has('nopad') });
physics.step();
lap('figure');

const frame = createFrame(renderer, shared);
addEventListener('pagehide', () => {
  renderer.setAnimationLoop(null);
  frame.target.dispose();
  renderer.dispose();
});
fitFrame = () => frame.resize();
frame.uniforms.uCanvas.value = 0;
// Realistic AAA mode: no comic ink outlines; use HDR bloom, SSAO & ACES filmic tone mapping.
frame.uniforms.uInk.value = 0;
frame.uniforms.uInkColor.value.setRGB(0.02, 0.02, 0.04);
frame.uniforms.uInkFar.value = 90;
if (q.has('ink')) frame.uniforms.uInk.value = +q.get('ink');
shared.uAurora.value = 0;
shared.uExposure.value = 1.08;
graphics = createGraphics({ renderer, capture, apply(profile) {
  frame.setSamples(profile.samples);
  frame.resize();
  frame.uniforms.uShafts.value = profile.level === 'low' ? 0 : 0.28;
  frame.uniforms.uAOStrength.value = profile.level === 'low' ? 0 : profile.level === 'medium' ? 0.36 : 0.50;
  frame.uniforms.uBloomStrength.value = profile.level === 'low' ? 0.24 : 0.36;
  forest.setQuality(profile.decorations);
  street.setQuality(profile);
} });

// the wind as the cloak feels it: the same bands the leaves move in
const wind = { x: WIND.x, z: WIND.y };
let time = 0;
function air(x, y, z, out) {
  const along = x * wind.x + z * wind.z, across = -x * wind.z + z * wind.x;
  const gust = THREE.MathUtils.clamp(0.5 + 0.8 * noise((along - time * 4) / 40, across / 90) + 0.3 * noise((along - time * 6.5) / 11, across / 17), 0, 1);
  const speed = 0.4 + 3.2 * gust * gust;
  return out.set(wind.x * speed, 0.25 * Math.sin(time * 2.3 + x) * gust, wind.z * speed);
}

let rides = null;
let net = null;
let missions = null;
let showcase = null;
const audio = getAudio({ scene: 'autumn', silent: capture });
let stepDist = 0;
/** After he has been put somewhere by hand: the animation starts from standing, not from where it was. */
function settle() {
  animator.lock = null; animator.lockOff = null;
  animator.enter(animator.locoState({ strafe: false }), 0.1);
}
function respawn() {
  rides?.leave(true); // a fall takes him out of his seat; the vehicle stays where it was
  hunt?.clearFocus();
  player.teleport(spawn.x, spawn.y, spawn.z);
  follow.pivot.set(0, 0, 0);
  settle();
}
function syncDayNight(dt = 0) {
  const dn = dayNight.update(time);
  shared.uNight.value = dn.night;
  shared.uTwilight.value = dn.twilight;
  frame.uniforms.uVignette.value = 0.08 + 0.22 * dn.night;
  const blendDt = Math.abs(dn.night - lastNight) > 0.25 ? 10 : dt;
  lastNight = dn.night;
  const focusZ = showcase?.active && follow.free ? follow.free.target.z : player.feet.z;
  forest.update(blendDt, focusZ, hunt?.seg, Boolean(hunt?.roam || showcase?.active));
}

const size = new THREE.Vector2();
const LIMIT = EXTENT - 16;
function tick(dt, draw = true) {
  dt = Math.min(dt, 1 / 20);
  const realDt = dt;
  // Online the street goes on under the menu (the host runs it for everyone): he just stands still.
  if (window.__paused || hunt?.downed) input.reset(); // down online, he waits for a hand
  const rawInp = input.read(realDt);
  if (showcase?.active && Math.hypot(rawInp.move.x, rawInp.move.y) > 0.15) {
    showcase.stop();
  }
  dt *= showcase?.active ? 1 : hunt?.timeScale ?? 1;
  time += dt;
  if (showcase?.active && !online) {
    // A solo preview leaves the player, fights and objectives at the point where the tour started.
    showcase.update(realDt);
    syncDayNight(dt);
    follow.update(realDt, player.feet, rawInp.look, rawInp.zoom);
    if (draw) renderScene();
    return;
  }
  let inp = showcase?.active
    ? { ...rawInp, move: { x: 0, y: 0 }, look: { x: 0, y: 0 }, zoom: 0, jump: false, jumpHeld: false, sprint: false, attack: false, heavy: false, power: false, dash: false, flare: false, whirl: false, ride: false }
    : rawInp;
  // At the wheel his legs are left alone: the vehicle carries him and his hands are busy.
  const driving = rides?.update(dt, inp, follow) ?? false;
  if (driving || showcase?.active || window.__paused) hunt?.combatInput(dt, null, follow.yaw);
  else inp = hunt?.combatInput(dt, inp, follow.yaw) ?? inp;
  if (!driving) player.update(dt, inp, inp.combatYaw ?? follow.yaw);
  // Keep him cleanly inside the 720m city grid without killing exploration on side streets.
  if (!driving) {
    if (player.feet.y < heightAt(player.feet.x, player.feet.z) - 6) respawn();
    else if (Math.abs(player.feet.x) > LIMIT || Math.abs(player.feet.z) > LIMIT) {
      const cx = THREE.MathUtils.clamp(player.feet.x, -LIMIT + 1, LIMIT - 1);
      const cz = THREE.MathUtils.clamp(player.feet.z, -LIMIT + 1, LIMIT - 1);
      player.teleport(cx, heightAt(cx, cz), cz);
      hunt?.hud?.flashText('City limits');
    }
  }
  if (!driving && player.grounded && player.speed > 1.2) {
    stepDist += player.speed * dt;
    if (stepDist >= 2.2) { stepDist = 0; audio.play('step', Math.min(1.4, player.speed / 5)); }
  } else if (!driving && player.grounded) {
    stepDist = Math.min(stepDist, 1.5);
  }
  hunt?.update(dt, driving ? {} : { ...inp, viewYaw: follow.yaw }, realDt);
  net?.update(realDt, { player, driving });
  showcase?.update(realDt);
  const focusZ = showcase?.active && follow.free ? follow.free.target.z : player.feet.z;
  const activeBoss = !showcase?.active && hunt?.enemies?.some((e) => e.duel && !e.dead && e.ready && e.phase !== 'waiting' && Math.hypot(e.x - player.feet.x, e.z - player.feet.z) < 22);
  audio.update(realDt, {
    chapter: chapterAt(focusZ, hunt?.seg, Boolean(hunt?.roam || showcase?.active)),
    combat: showcase?.active ? 0 : (hunt?.combat ?? 0),
    boss: !!activeBoss,
    driving: !showcase?.active && driving,
    speed: !showcase?.active && rides?.driven ? Math.hypot(rides.driven.vx, rides.driven.vz) : 0,
    paused: !!window.__paused,
  });
  man.object.visible = !driving;
  syncDayNight(dt);
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
  hunt?.pose();
  man.object.updateMatrixWorld(true);
  figure.updateCloak(dt, air, heightAt);

  follow.shake = showcase?.active ? 0 : (hunt?.cameraKick ?? 0);
  if (inp.combatYaw != null && !follow.free) follow.track(inp.combatYaw, realDt);
  // at the wheel the eye must not be pushed in by the very car it follows
  follow.update(realDt, root, inp.combatYaw == null ? inp.look : { x: 0, y: inp.look.y }, inp.zoom, null, heightAt,
    (origin, dir, distance, radius) => player.castCamera(origin, dir, distance, radius, driving));
  if (draw) renderScene();
}

function renderScene() {
  const focus = showcase?.active && follow.free ? follow.free.target : player.feet;
  camera.updateMatrixWorld(true);
  hunt?.hud.focusMarker(showcase?.active ? null : hunt.visibleTarget);
  street.update(focus);
  showcase?.revealWorld();
  renderer.getDrawingBufferSize(size);
  shared.uTime.value = time;
  shared.uPixel.value = (2 * Math.tan(THREE.MathUtils.degToRad(camera.fov) / 2)) / size.y;
  shared.uFocus.value.copy(focus);
  shade.update(scene, focus);
  frame.render(scene, camera);
}

window.lab = {
  THREE, player, follow, camera, input, animator, figure, shared, frame, renderer, graphics, street, timings, counts: forest.counts, heightAt, forestAt, mountainHeight, dayNight, syncDayNight, respawn, way, scene, audio,
  step(dt = 1 / 60, n = 1, draw = true) { for (let i = 0; i < n; i++) tick(dt, draw && i === n - 1); },
  setInput(fn) { input.override = fn; },
  /** Puts him at (x, z) on the ground, facing (fx, fz), with the eye behind him. */
  teleport(x, z, fx, fz) {
    hunt?.clearFocus();
    player.teleport(x, heightAt(x, z), z);
    player.yaw = player.lastYaw = Math.atan2(fx - x, fz - z);
    follow.yaw = player.yaw; follow.pivot.set(0, 0, 0);
    animator.lock = null; animator.lockOff = null;
    animator.enter(animator.locoState({ strafe: false }), 0.05);
    tick(1 / 60);
  },
  /** A fixed view for stills: eye and what it looks at. Call with nothing to go back to following him. */
  view(eye, target) { follow.free = eye ? { position: new THREE.Vector3(...eye), target: new THREE.Vector3(...target) } : null; },
};

if (!capture) {
  if (online) net = window.lab.net = createNet({ scene, shared, man, camera, heightAt, route: way, contract: true });
  const roam = (window.__mode ?? 'level') !== 'level';
  const saved = !roam && !online ? loadProgress() : null;
  missions = await createMissions({ scene, shared, player, heightAt, levels: LEVELS, roam: roam || online, audio, saved });
  hunt = await createHunt({
    scene, shared, man, animator, player, heightAt, camera,
    moveSpawn: (x, z) => spawn.set(x, heightAt(x, z), z),
    down: respawn,
    levels: LEVELS,
    route: way,
    roam,
    net,
    events: online,
    audio,
    missions,
  });
  missions.bind(hunt);
  window.lab.hunt = hunt;
  window.lab.missions = missions;
  rides = await createRides({ scene, shared, player, heightAt, hunt, route: way, checkpoints: LEVELS.map((L) => L.checkpoint),
    inBounds: (x, z) => Math.abs(x) < LIMIT && Math.abs(z) < LIMIT, settle, audio, missions });
  window.lab.rides = rides;
  net?.attach(hunt, rides);
}
showcase = createShowcase({ follow, player, heightAt, dayNight, hud: hunt?.hud });
window.lab.showcase = showcase;

// --- warming up: every shader is built and drawn once, and the GPU has caught up, before anyone is let in ---
say(0.68, 'Waking the forest');
/** Resolves when the GPU has done everything it has been sent: JavaScript finishing isn't the GPU finishing. */
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
  say(0.86, 'Opening the trail');
}
for (let i = 0; i < (capture ? 1 : 5); i++) {
  tick(1 / 60);
  if (capture) break;
  await gpuDone();
  say(0.88 + 0.025 * i, 'Almost there');
  await turn();
}
lap('first frame');

// Draws while the page says the demo can be seen, and not at all while it can't.
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
      audio.update(1 / 60, { paused: true, driving: false, speed: 0 });
      audio.updateRide({ driving: false, paused: true });
    }
    last = now;
    renderScene();
  } : null);
}
if (!capture) {
  const hint = document.getElementById('hint');
  addEventListener('keydown', (e) => {
    // M walks the other way: level mode or free roam, from the start.
    if (!e.repeat && e.code === 'KeyM' && !window.__paused && document.activeElement?.tagName !== 'INPUT') {
      const n = new URLSearchParams(location.search);
      n.set('mode', (window.__mode ?? 'level') === 'roam' ? 'level' : 'roam');
      location.search = n.toString();
    }
  });
  setTimeout(() => hint?.classList.add('faded'), 18000);
}
fitView();
if (!capture && q.has('tour') && q.get('tour') !== '0' && q.get('tour') !== 'false') {
  showcase.start(0);
  tick(1 / 60);
}
loaded = true;
run();
document.getElementById('loading')?.remove();
if (q.has('clean') || capture) document.getElementById('hint')?.remove();
block.ready();
window.lab.ready = true;
// for the measuring tools
window.__block = { ready: true, block, timings, set() {} };
