// The night city's blocks. Avenues run north–south, streets east–west, on one spacing,
// inside the land map (EXTENT 256). The winding trail is a separate road drawn through
// this grid; land.js reports the distance to whichever is closer.
export const SPACING = 40;
export const GRID_MIN = -200;
export const GRID_MAX = 200;

const line = (a, b, step) => {
  const out = [];
  for (let v = a; v <= b + 1e-3; v += step) out.push(v);
  return out;
};
export const AVENUES = line(GRID_MIN, GRID_MAX, SPACING);
export const STREETS = AVENUES;

// z above downtown, then above market, then x east of the split is the docks. Matches uZoneSplit.
export const ZONE_SPLIT = { downtown: 90, market: -10, east: -30 };
export const ZONES = [
  { name: 'Downtown', chapter: 0 },
  { name: 'Market', chapter: 1 },
  { name: 'Docks', chapter: 4 },
  { name: 'Suburbs', chapter: 2 },
];

const KERB = 5.8; // sidewalk: road half-width 3.6 plus a step up off the asphalt
const nearestLine = (list, v) => list.reduce((best, p) => Math.abs(p - v) < Math.abs(best - v) ? p : best);
/**
 * The next point along a sidewalk. `dir` is which way the kerb runs. `cross` uses the
 * intersection instead of the middle of the block, so people cross where the stripes are.
 */
export function nextPavement(x, z, dir, cross) {
  const ax = nearestLine(AVENUES, x), sz = nearestLine(STREETS, z);
  const clamp = (v) => Math.max(GRID_MIN + KERB, Math.min(GRID_MAX - KERB, v));
  if (Math.abs(x - ax) <= Math.abs(z - sz)) {
    const side = Math.sign(x - ax) || 1;
    if (cross && Math.abs(z - sz) < 7) return { x: ax - side * KERB, z: sz, crossed: true };
    return { x: ax + side * KERB, z: clamp(z + dir * 12), crossed: false };
  }
  const side = Math.sign(z - sz) || 1;
  if (cross && Math.abs(x - ax) < 7) return { x: ax, z: sz - side * KERB, crossed: true };
  return { x: clamp(x + dir * 12), z: sz + side * KERB, crossed: false };
}

export function zoneAt(x, z) {
  if (z > ZONE_SPLIT.downtown) return 0;
  if (z > ZONE_SPLIT.market) return 1;
  if (x > ZONE_SPLIT.east) return 2;
  return 3;
}

/** Metres to the nearest avenue or street. Past the outer roads, distance to the nearest end. */
export function gridDist(x, z) {
  let best = Infinity;
  const pastZ = z < GRID_MIN || z > GRID_MAX;
  const pastX = x < GRID_MIN || x > GRID_MAX;
  for (const ax of AVENUES) {
    best = Math.min(best, pastZ ? Math.hypot(x - ax, z < GRID_MIN ? z - GRID_MIN : z - GRID_MAX) : Math.abs(x - ax));
  }
  for (const sz of STREETS) {
    best = Math.min(best, pastX ? Math.hypot(z - sz, x < GRID_MIN ? x - GRID_MIN : x - GRID_MAX) : Math.abs(z - sz));
  }
  return best;
}

// One lot per corner of a block, plus a heart tower where the district is tall. Inset so the
// footprint stays off the kerb even after a metre of jitter.
const SLOTS = [[-8, -8], [8, -8], [-8, 8], [8, 8], [0, 0]];
const LOTS = [5, 4, 3, 2];
export function blockLots() {
  const out = [];
  for (let i = 0; i < AVENUES.length - 1; i++) {
    for (let j = 0; j < STREETS.length - 1; j++) {
      const cx = (AVENUES[i] + AVENUES[i + 1]) / 2, cz = (STREETS[j] + STREETS[j + 1]) / 2;
      const zone = zoneAt(cx, cz);
      for (let s = 0; s < LOTS[zone]; s++) out.push({ x: cx + SLOTS[s][0], z: cz + SLOTS[s][1], zone, heart: s === 4 });
    }
  }
  return out;
}

export function demoGrid() {
  if (AVENUES.length !== 11 || AVENUES[0] !== GRID_MIN || AVENUES.at(-1) !== GRID_MAX) throw new Error('avenues');
  if (gridDist(0, 100) > 1e-6) throw new Error('on an avenue');
  if (Math.abs(gridDist(20, 20) - 20) > 1e-6) throw new Error('block centre');
  if (zoneAt(0, 150) !== 0 || zoneAt(0, 20) !== 1 || zoneAt(40, -40) !== 2 || zoneAt(-80, -40) !== 3) throw new Error('zones');
  const lots = blockLots();
  if (lots.length < 250) throw new Error('city is too small: ' + lots.length);
  for (const lot of lots) if (gridDist(lot.x, lot.z) < 12) throw new Error('lot on a road');
  const step = nextPavement(5.8, 100, 1, false);
  if (Math.abs(step.x - 5.8) > 0.01 || step.z <= 100 || step.crossed) throw new Error('sidewalk continues along the kerb');
  const over = nextPavement(5.8, 6, 1, true);
  if (!over.crossed || over.x > 0) throw new Error('a crossing swaps sides at the intersection');
}

if (globalThis.process?.argv?.[1]?.endsWith('grid.js')) demoGrid();
