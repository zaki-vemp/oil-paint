// Six mirror duels teach the same readable loop: see the committed attack, evade it, punish recovery.
// This state machine is pure game logic. Rendering, collisions and damage stay with hunt.js, so a rival
// uses the same terrain, capsule sweeps and save rules as the rest of the game.
import { SCALE } from './chapters.js';
export const DUELS = [
  { name: 'Ren, the Patient Blade', x: -7.5, z: 58, hp: 120, lesson: 'Jump or backstep the sweep. Strike when the blade falls.', moves: ['sweep'] },
  { name: 'Ren, the Piercing Wind', x: 3.8, z: 35, hp: 150, lesson: 'Sidestep the thrust. Use your spark during the opening.', moves: ['thrust', 'reverse'] },
  { name: 'Ren, the Twin Current', x: 10.5, z: 9, hp: 200, lesson: 'Wait for both cuts. Quickstep sideways when Ren circles.', moves: ['double', 'thrust', 'reverse'] },
  { name: 'Ren, the Iron Will', x: 0, z: -25, hp: 240, lesson: 'Two heavy strikes break his guard. Evade the overhead slam.', moves: ['slam', 'reply', 'double'] },
  { name: 'Ren, the Ember Spiral', x: -0.9, z: -57, hp: 270, lesson: 'Jump or step beyond the spiral. Punish recovery with heavy strikes to earn Whirl.', moves: ['spiral', 'reverse', 'slam'] },
  { name: 'Ren, your Mirror', x: 10, z: -88, hp: 320, lesson: 'Wait out the feint. Whirl cracks the guard, and Flare forces an opening.', moves: ['feint', 'double', 'reply', 'spiral', 'slam'] },
].map((d) => ({ ...d, x: d.x * SCALE, z: d.z * SCALE }));
export const MOVES = {
  sweep: { windup: 1.0, active: 0.2, recover: 1.35, reach: 2.8, arc: 1.5, damage: 12, pose: 'cut', cue: 'Sweep — jump or step out of the ring' },
  thrust: { windup: 0.95, active: 0.26, recover: 1.3, reach: 2.1, arc: 0.42, damage: 16, pose: 'thrust', dash: 10, cue: 'Thrust — sidestep the committed line' },
  double: { windup: 0.9, active: 0.75, recover: 1.4, reach: 2.7, arc: 1.3, damage: 13, pose: 'backhand', second: 'cut', low: true, cue: 'Two cuts — wait for the second one' },
  slam: { windup: 1.15, active: 0.23, recover: 1.6, reach: 3.1, arc: 0.65, damage: 24, pose: 'heavy', unblockable: true, cue: 'Overhead · unblockable — move sideways' },
  reverse: { windup: 0.7, active: 0.23, recover: 1.17, reach: 2.65, arc: 1.28, damage: 14, pose: 'backhand', cue: 'Reverse cut — retreat past the arc' },
  reply: { windup: 0.72, active: 0.25, recover: 1.35, reach: 2.25, arc: 0.55, damage: 17, pose: 'counter', dash: 3.4, cue: 'Riposte — step off his blade line' },
  spiral: { windup: 1.04, active: 0.48, recover: 1.65, reach: 3.25, arc: Math.PI, damage: 19, pose: 'spin', low: true, cue: 'Ember spiral — jump or leave the circle' },
  feint: { windup: 1.36, active: 0.31, recover: 1.47, reach: 2.65, arc: 0.76, damage: 21, pose: 'advance', dash: 5.5, cue: 'Feint — wait for the second windup' },
};
const angle = (x, z) => Math.atan2(x, z);
export function createDuel(index) {
  const d = DUELS[index];
  return { kind: 'rival', duel: true, seg: index, name: d.name, lesson: d.lesson, x: d.x, z: d.z, homeX: d.x, homeZ: d.z,
    hp: d.hp, maxHp: d.hp, phase: 'waiting', timer: 0, moveIndex: 0, move: 'sweep', attackYaw: 0, yaw: Math.PI,
    windup: 0, recovery: 0, cool: 0, attackU: -1, going: 0, hitDone: false, connected: false, beat: 0,
    react: 1.7, reactSide: 1, evaded: false, punishes: 0, guard: 2, ready: false };
}

export function stepDuel(e, dt, { x, z, ready, move, threatening = false }) {
  e.ready = ready; e.going = 0;
  if (!ready) { e.phase = 'waiting'; e.attackU = -1; return null; }
  const dx = x - e.x, dz = z - e.z, distance = Math.hypot(dx, dz);
  e.timer = Math.max(0, e.timer - dt);
  e.react = Math.max(0, e.react - dt);
  const spec = MOVES[e.move];
  e.windup = e.phase === 'windup' ? e.timer : 0;
  e.recovery = e.phase === 'recovery' ? e.timer : 0;
  if (e.phase === 'waiting') {
    if (distance < 9) { e.phase = 'intro'; e.timer = 1.4; return 'intro'; }
    return null;
  }
  // Leaving the duel resets its health and lesson counters, while the cleared creature pack stays cleared.
  if (distance > 17) { Object.assign(e, createDuel(e.seg)); e.ready = true; return 'reset'; }
  if (e.phase === 'intro') { if (e.timer === 0) e.phase = 'approach'; return null; }
  function prepare(name) {
    e.move = name; e.phase = 'windup'; e.timer = MOVES[name].windup;
    e.hitDone = e.connected = false; e.beat = 0; e.guard = 2; e.attackYaw = e.yaw;
    return 'windup';
  }
  if (e.phase === 'approach') {
    e.attackU = -1; e.yaw = angle(dx, dz);
    if (e.seg >= 2 && e.react === 0 && threatening && distance < 3.45) {
      e.phase = e.seg === 2 || e.seg === 4 ? 'sidestep' : 'guard';
      e.timer = e.phase === 'sidestep' ? 0.32 : 0.5;
      e.attackYaw = e.yaw; e.react = 4.2; e.reactSide *= -1;
      return null;
    }
    if (distance > 2.5) {
      const speed = e.seg > 1 ? 3.5 : 2.5, d = Math.min(speed * dt, distance - 2.5);
      move(dx / distance * d, dz / distance * d); e.going = speed;
    } else {
      const moves = DUELS[e.seg].moves;
      return prepare(moves[e.moveIndex++ % moves.length]);
    }
  } else if (e.phase === 'guard' || e.phase === 'sidestep') {
    e.yaw = angle(dx, dz);
    if (e.phase === 'sidestep') {
      move((Math.cos(e.attackYaw) * e.reactSide - Math.sin(e.attackYaw) * 0.25) * dt * 5,
        (-Math.sin(e.attackYaw) * e.reactSide - Math.cos(e.attackYaw) * 0.25) * dt * 5);
    }
    if (e.timer === 0) return prepare(e.phase === 'guard' ? 'reply' : 'reverse');
  } else if (e.phase === 'windup') {
    const p = 1 - e.timer / spec.windup;
    if (p < (e.move === 'feint' ? 0.70 : 0.5)) e.attackYaw = angle(dx, dz);
    e.yaw = e.attackYaw;
    e.attackU = e.move === 'feint' ? 0.24 * Math.sin(Math.PI * Math.min(p / 0.67, 1)) + 0.28 * Math.max(0, (p - 0.67) / 0.33) : 0.28 * p;
    if (e.timer === 0) { e.phase = 'active'; e.timer = spec.active; return 'strike'; }
  } else if (e.phase === 'active') {
    const p = 1 - e.timer / spec.active;
    const beat = Number(!!spec.second && p >= 0.51);
    if (beat !== e.beat) { e.beat = beat; e.hitDone = false; }
    e.attackU = spec.second ? (beat ? 0.23 + (p - 0.51) * 0.98 : 0.3 + p * 0.84) : 0.3 + p * 0.43;
    if (spec.dash) move(Math.sin(e.attackYaw) * dt * spec.dash, Math.cos(e.attackYaw) * dt * spec.dash);
    if (e.timer === 0) {
      if (!e.connected && distance < 8) e.evaded = true;
      e.phase = 'recovery'; e.timer = spec.recover; return 'opening';
    }
  } else if (e.phase === 'recovery') {
    e.attackU = 0.76 + 0.24 * (1 - e.timer / Math.max(spec.recover, e.timer));
    if (e.timer === 0) { e.phase = 'approach'; e.attackU = -1; }
  }
  return null;
}

export function duelHit(e, x, y, z, floor) {
  const m = MOVES[e.move], dx = x - e.x, dz = z - e.z;
  if (e.phase !== 'active' || e.hitDone || e.attackU < 0.36 || e.attackU > 0.67 || Math.hypot(dx, dz) > m.reach ||
      y - floor > (m.low || e.move === 'sweep' ? 0.8 : 1.8)) return false;
  return Math.abs(Math.atan2(Math.sin(angle(dx, dz) - e.attackYaw), Math.cos(angle(dx, dz) - e.attackYaw))) <= m.arc;
}
export function damageRival(e, amount, source) {
  if (source === 'flare') { e.phase = 'recovery'; e.timer = 2.2; }
  if (e.phase !== 'recovery') {
    if ((source === 'heavy' || source === 'whirl') && e.seg >= 3 && --e.guard <= 0) { e.phase = 'recovery'; e.timer = 1.8; }
    else return Math.max(1, Math.round(amount * 0.08));
  }
  e.punishes++; return Math.round(amount * 1.25);
}

export const ARENA_RADIUS = 8.6;
export function duelVisual(e) {
  const spec = MOVES[e.move] ?? MOVES.sweep;
  const engaged = !!(e.ready && !e.dead && e.phase !== 'waiting');
  if (e.phase === 'recovery') {
    const remain = Math.max(0, Math.min(1, e.timer / Math.max(spec.recover, 0.001)));
    return { engaged, mode: 'opening', color: [0.0, 0.96, 0.82], accent: [0.62, 1.0, 0.93], progress: 1 - remain, reach: spec.reach, arc: Math.PI, spin: 2.4 };
  }
  if (e.phase === 'windup' || e.phase === 'active') {
    const progress = e.phase === 'active' ? 1 : Math.max(0, Math.min(1, 1 - e.timer / Math.max(spec.windup, 0.001)));
    return { engaged, mode: 'danger', color: spec.unblockable ? [1.0, 0.41, 0.14] : [1.0, 0.165, 0.427],
      accent: spec.unblockable ? [1.0, 0.82, 0.56] : [1.0, 0.76, 0.84], progress, reach: spec.reach, arc: spec.arc,
      spin: e.phase === 'active' ? 5.4 : 2.6 + progress * 2.2 };
  }
  if (e.phase === 'guard') return { engaged, mode: 'stance', color: [1.0, 0.68, 0.22], accent: [1.0, 0.88, 0.64], progress: 0, reach: 1.75, arc: 1.15, spin: 1.6 };
  return { engaged, mode: 'stance', color: [1.0, 0.22, 0.45], accent: [1.0, 0.62, 0.42], progress: 0, reach: spec.reach, arc: spec.arc, spin: 1.1 };
}

export function demoDuels() {
  for (let i = 0; i < DUELS.length; i++) {
    const e = createDuel(i);
    stepDuel(e, 1 / 60, { x: e.x, z: e.z + 2, ready: false, move() {} });
    if (e.phase !== 'waiting') throw new Error('creatures first');
    const ctx = { x: e.x, z: e.z + 2, ready: true, move() {} };
    if (stepDuel(e, 1 / 60, ctx) !== 'intro') throw new Error('duel intro');
    for (let k = 0; k < 300 && !duelHit(e, e.x, 0, e.z + 1, 0); k++) stepDuel(e, 1 / 60, ctx);
    if (!duelHit(e, e.x, 0, e.z + 1, 0) || duelHit(e, e.x + 5, 0, e.z, 0)) throw new Error('committed attack');
    if (duelVisual(e).mode !== 'danger') throw new Error('danger visual');
    e.phase = 'windup'; if (damageRival(e, 30, 'sword') >= 10) throw new Error('guard');
    e.phase = 'recovery'; if (damageRival(e, 30, 'sword') <= 30 || !e.punishes) throw new Error('punish recovery');
    if (duelVisual(e).mode !== 'opening') throw new Error('opening visual');
  }
  const e = createDuel(3); e.phase = 'windup'; damageRival(e, 66, 'heavy'); damageRival(e, 66, 'heavy');
  if (e.phase !== 'recovery') throw new Error('heavy guard break');
  e.phase = 'windup'; damageRival(e, 60, 'flare'); if (e.phase !== 'recovery') throw new Error('flare guard break');
  const twin = createDuel(2), twinCtx = { x: twin.x, z: twin.z + 2, ready: true, move() {} };
  let beats = 0;
  for (let k = 0; k < 280 && twin.phase !== 'recovery'; k++) {
    stepDuel(twin, 1 / 60, twinCtx);
    if (duelHit(twin, twinCtx.x, 0, twinCtx.z, 0)) { beats++; twin.hitDone = twin.connected = true; }
  }
  if (beats !== 2 || twin.evaded) throw new Error('each cut in the Twin Current carries its own impact window');
  const iron = createDuel(3);
  Object.assign(iron, { phase: 'approach', react: 0 });
  const ironCtx = { x: iron.x, z: iron.z + 2, ready: true, threatening: true, move() {} };
  stepDuel(iron, 1 / 60, ironCtx);
  if (iron.phase !== 'guard') throw new Error('Iron Will responds to a visible windup by guarding');
  stepDuel(iron, 0.6, ironCtx);
  if (iron.phase !== 'windup' || iron.move !== 'reply') throw new Error('Iron Will answers the guard with a committed riposte');
}
if (globalThis.process?.argv?.[1]?.endsWith('duel.js')) demoDuels();
