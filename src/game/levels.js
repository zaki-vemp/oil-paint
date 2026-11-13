// Five gated segments down the autumn way: each level is a stretch of the path,
// a checkpoint cairn at its start, a stone gate at its end, and one power granted
// for clearing it. Segment 5 ends at the clearing itself. Free roam opens every
// gate, unlocks every power and lets fallen beasts come back.
export const POWER_ORDER = ['spark', 'dash', 'heavy', 'flare'];

export const POWERS = {
  spark: { name: 'Spark', key: 'Q', hint: 'Q throws a spark' },
  dash: { name: 'Dash', key: 'E', hint: 'E dashes through danger' },
  heavy: { name: 'Heavy swing', key: 'F', hint: 'F: a heavy swing' },
  flare: { name: 'Lantern flare', key: 'R', hint: 'R: a flare that staggers the pack' },
};

export const LEVELS = [
  {
    name: 'The path',
    checkpoint: { x: 0, z: 78, name: 'The start' },
    gate: { x: -7.5, z: 54 },
    grant: 'spark',
    spots: [
      { kind: 'dog', x: 3, z: 66 },
      { kind: 'dog', x: -6, z: 62 },
    ],
  },
  {
    name: 'The birches',
    checkpoint: { x: -7, z: 51, name: 'Birch stile' },
    gate: { x: 3.8, z: 30 },
    grant: 'dash',
    spots: [
      { kind: 'dog', x: -9, z: 44 },
      { kind: 'dog', x: 8, z: 42 },
      { kind: 'wolf', x: -1, z: 34 },
    ],
  },
  {
    name: 'The pines',
    checkpoint: { x: 3.6, z: 28, name: 'Pine watch' },
    gate: { x: 10.5, z: 4 },
    grant: 'heavy',
    spots: [
      { kind: 'dog', x: 12, z: 18 },
      { kind: 'wolf', x: 4, z: 10 },
      { kind: 'dog', x: -4, z: 8 },
    ],
  },
  {
    name: 'The dark',
    checkpoint: { x: 10.3, z: 2, name: 'Dark gate' },
    gate: { x: 0, z: -30 },
    grant: 'flare',
    spots: [
      { kind: 'wolf', x: 2, z: -14 },
      { kind: 'dog', x: -4, z: -22 },
      { kind: 'brute', x: 5, z: -26 },
    ],
  },
  {
    name: 'The clearing run',
    checkpoint: { x: 0, z: -32, name: 'Last cairn' },
    gate: null,
    grant: null,
    spots: [
      { kind: 'dog', x: -6, z: -36 },
      { kind: 'brute', x: -3, z: -48 },
      { kind: 'wolf', x: 7, z: -58 },
      { kind: 'brute', x: 8, z: -70 },
      { kind: 'dog', x: 2, z: -78 },
    ],
  },
];

export const GOAL = { x: 11, z: -96, name: 'The clearing' };

export const SAVE_KEY = 'oilpaint.levels.v1';

/** Powers earned by clearing `cleared` segments. Derived, never stored. */
export function powersFor(cleared) {
  const out = { spark: false, dash: false, heavy: false, flare: false };
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
  if (LEVELS.length !== 5) throw new Error('five levels, ' + LEVELS.length);
  for (const [i, L] of LEVELS.entries()) {
    if (!L.name || !L.checkpoint || !Array.isArray(L.spots) || !L.spots.length) throw new Error('level ' + i);
    if (L.grant && !POWERS[L.grant]) throw new Error('bad grant ' + L.grant);
    for (const s of L.spots) {
      if (!['dog', 'wolf', 'brute'].includes(s.kind)) throw new Error('bad kind ' + s.kind);
    }
  }
  for (let i = 1; i < LEVELS.length; i++) {
    const a = LEVELS[i - 1].gate, b = LEVELS[i].gate;
    if (a && b && !(a.z > b.z)) throw new Error('gates must run down the way');
  }
  const p = powersFor(2);
  if (!p.spark || !p.dash || p.heavy || p.flare) throw new Error('powers for 2 cleared');
}

if (globalThis.process?.argv?.[1]?.endsWith('levels.js')) demoLevels();
