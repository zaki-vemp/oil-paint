// Six gated segments down the way (a night city now; the file names still say autumn): each level is a stretch of the path,
// a checkpoint cairn at its start, a stone gate at its end, and one power granted
// for clearing it. Segment 6 ends at the clearing itself. Free roam opens every
// gate, unlocks every power and lets the fallen come back. Each crew is followed
// by its mirror duel (duel.js); only victory over both clears the segment. The crews escalate:
// punks, then enforcers, gunners from level 3, the mech from 4, razor blades from 5.
import { DUELS } from './duel.js';
export const KINDS = ['punk', 'enforcer', 'gunner', 'blade', 'brute'];
export const POWER_ORDER = ['spark', 'dash', 'heavy', 'flare', 'whirl'];

export const POWERS = {
  spark: { name: 'Spark', key: 'Q', hint: 'Q throws a spark' },
  dash: { name: 'Dash', key: 'E', hint: 'E dashes through danger' },
  heavy: { name: 'Heavy swing', key: 'F', hint: 'F: a heavy swing' },
  flare: { name: 'Lantern flare', key: 'R', hint: 'R: a flare that staggers the pack' },
  whirl: { name: 'Whirl', key: 'C', hint: 'C: a spinning slash that hits all around (one spark)' },
};

export const LEVELS = [
  {
    name: 'The strip',
    checkpoint: { x: 0, z: 78, name: 'The start' },
    gate: { x: -7.5, z: 54 },
    grant: 'spark',
    spots: [
      { kind: 'punk', x: 3, z: 66 },
      { kind: 'punk', x: -6, z: 62 },
    ],
  },
  {
    name: 'The arcade',
    checkpoint: { x: -7, z: 51, name: 'Arcade stile' },
    gate: { x: 3.8, z: 30 },
    grant: 'dash',
    spots: [
      { kind: 'punk', x: -9, z: 44 },
      { kind: 'punk', x: 8, z: 42 },
      { kind: 'enforcer', x: -1, z: 34 },
    ],
  },
  {
    name: 'The heights',
    checkpoint: { x: 3.6, z: 28, name: 'Heights watch' },
    gate: { x: 10.5, z: 4 },
    grant: 'heavy',
    spots: [
      { kind: 'punk', x: 12, z: 18 },
      { kind: 'enforcer', x: 4, z: 10 },
      { kind: 'gunner', x: -4, z: 8 },
    ],
  },
  {
    name: 'The stacks',
    checkpoint: { x: 10.3, z: 2, name: 'Stacks gate' },
    gate: { x: 0, z: -30 },
    grant: 'flare',
    spots: [
      { kind: 'enforcer', x: 2, z: -14 },
      { kind: 'gunner', x: -4, z: -22 },
      { kind: 'brute', x: 5, z: -26 },
    ],
  },
  {
    name: 'The ashen hollow',
    checkpoint: { x: 0, z: -32, name: 'Ash cairn' },
    gate: { x: 1.6, z: -62 },
    grant: 'whirl',
    spots: [
      { kind: 'punk', x: -6, z: -36 },
      { kind: 'blade', x: -8, z: -44 },
      { kind: 'brute', x: -3, z: -48 },
      { kind: 'gunner', x: 3, z: -53 },
    ],
  },
  {
    name: 'The sanctuary run',
    checkpoint: { x: 2.5, z: -64, name: 'Last cairn' },
    gate: null,
    grant: null,
    spots: [
      { kind: 'blade', x: 0, z: -69 },
      { kind: 'brute', x: 8, z: -72 },
      { kind: 'gunner', x: 3, z: -78 },
      { kind: 'brute', x: 11, z: -82 },
    ],
  },
];

export const GOAL = { x: 11, z: -96, name: 'The clearing' };

export const SAVE_KEY = 'oilpaint.levels.v1';

/** Powers earned by clearing `cleared` segments. Derived, never stored. */
export function powersFor(cleared) {
  const out = Object.fromEntries(POWER_ORDER.map((k) => [k, false]));
  for (let i = 0; i < Math.min(cleared, LEVELS.length); i++) {
    const g = LEVELS[i].grant;
    if (g) out[g] = true;
  }
  return out;
}

/** Saved progress: how many segments are cleared and where the checkpoint is. */
export function loadProgress() {
  try {
    if (new URLSearchParams(globalThis.location?.search ?? '').has('fresh')) return null;
    const raw = globalThis.localStorage?.getItem(SAVE_KEY);
    if (!raw) return null;
    const s = JSON.parse(raw);
    if (!Number.isInteger(s?.cleared) || s.cleared < 0 || s.cleared > LEVELS.length) return null;
    const cp = LEVELS[Math.min(s.cleared, LEVELS.length - 1)].checkpoint;
    const x = Number.isFinite(s?.x) ? s.x : cp.x;
    const z = Number.isFinite(s?.z) ? s.z : cp.z;
    return { cleared: s.cleared, x, z };
  } catch {
    return null;
  }
}

/** Returns true when the save landed; false must be shown, never swallowed. */
export function saveProgress(state) {
  try {
    globalThis.localStorage?.setItem(SAVE_KEY, JSON.stringify(state));
    return true;
  } catch {
    return false;
  }
}

export function demoLevels() {
  if (LEVELS.length < 6) throw new Error('at least six levels, ' + LEVELS.length);
  for (const [i, L] of LEVELS.entries()) {
    if (!L.name || !L.checkpoint || !Array.isArray(L.spots) || !L.spots.length) throw new Error('level ' + i);
    if (L.grant && !POWERS[L.grant]) throw new Error('bad grant ' + L.grant);
    const rival = DUELS[i];
    if (!rival || rival.z >= L.checkpoint.z || rival.z <= (L.gate?.z ?? GOAL.z)) throw new Error('rival must end chapter ' + i);
    for (const s of L.spots) {
      if (!KINDS.includes(s.kind)) throw new Error('bad kind ' + s.kind);
    }
  }
  for (const k of KINDS) if (!LEVELS.some((L) => L.spots.some((s) => s.kind === k))) throw new Error('every kind is met on the way: ' + k);
  const firstWith = (k) => LEVELS.findIndex((L) => L.spots.some((s) => s.kind === k));
  if (!(firstWith('punk') === 0 && firstWith('gunner') > 1 && firstWith('brute') > 2)) throw new Error('the crews escalate');
  for (let i = 1; i < LEVELS.length; i++) {
    const a = LEVELS[i - 1].gate, b = LEVELS[i].gate;
    if (a && b && !(a.z > b.z)) throw new Error('gates must run down the way');
  }
  const p = powersFor(2);
  if (!p.spark || !p.dash || p.heavy || p.flare || p.whirl) throw new Error('powers for 2 cleared');
  for (const k of POWER_ORDER) if (!POWERS[k] || LEVELS.filter((L) => L.grant === k).length !== 1) throw new Error('one level grants ' + k);
}

if (globalThis.process?.argv?.[1]?.endsWith('levels.js')) demoLevels();
