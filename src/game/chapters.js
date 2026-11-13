// The trail's five art directions. Position selects a chapter, progression caps what is reachable,
// and the same palette drives the scenery and the two HUD treatments. No visual setting is saved.
export const CHAPTERS = [
  { name: 'Sunleaf Trail', subtitle: 'A small step into a wider world', style: 'field', color: '#34795b',
    grass: [0.39, 0.65, 0.30], foliage: [0.25, 0.57, 0.32], leafLight: [0.64, 0.79, 0.34],
    sky: [0.31, 0.67, 0.82], haze: [0.68, 0.82, 0.68], path: [0.88, 0.78, 0.53] },
  { name: 'Petalwood', subtitle: 'Follow the falling blossoms', style: 'field', color: '#aa5069',
    grass: [0.46, 0.67, 0.39], foliage: [0.84, 0.43, 0.52], leafLight: [1.0, 0.76, 0.72],
    sky: [0.48, 0.71, 0.85], haze: [0.85, 0.78, 0.77], path: [0.92, 0.80, 0.66] },
  { name: 'Jade Highlands', subtitle: 'The forest learns your name', style: 'spirit', color: '#429f9d',
    grass: [0.24, 0.47, 0.42], foliage: [0.12, 0.38, 0.36], leafLight: [0.39, 0.66, 0.51],
    sky: [0.25, 0.51, 0.62], haze: [0.49, 0.68, 0.65], path: [0.63, 0.66, 0.54] },
  { name: 'Moonveil Grove', subtitle: 'Keep a little light with you', style: 'spirit', color: '#77c8cf',
    grass: [0.18, 0.32, 0.37], foliage: [0.12, 0.27, 0.34], leafLight: [0.30, 0.52, 0.59],
    sky: [0.08, 0.21, 0.34], haze: [0.26, 0.43, 0.53], path: [0.44, 0.53, 0.57] },
  { name: 'Amber Sanctuary', subtitle: 'Every journey leaves a light', style: 'field', color: '#a56334',
    grass: [0.62, 0.63, 0.30], foliage: [0.83, 0.45, 0.23], leafLight: [0.98, 0.75, 0.35],
    sky: [0.53, 0.70, 0.76], haze: [0.88, 0.79, 0.60], path: [0.91, 0.79, 0.54] },
];

const BOUNDARIES = [52, 29, 3, -31];
export function chapterAt(z, cleared = CHAPTERS.length, roam = false) {
  let i = 0;
  while (i < BOUNDARIES.length && z < BOUNDARIES[i]) i++;
  return roam ? i : Math.min(i, Math.max(0, cleared));
}

export function demoChapters() {
  if (CHAPTERS.length !== 5 || new Set(CHAPTERS.map(c => c.style)).size !== 2) throw new Error('five chapters, two HUD styles');
  if (chapterAt(78) !== 0 || chapterAt(-70) !== 4 || chapterAt(-70, 1) !== 1 || chapterAt(-70, 0, true) !== 4) throw new Error('chapter progression');
  for (const c of CHAPTERS) for (const key of ['grass', 'foliage', 'leafLight', 'sky', 'haze', 'path']) {
    if (c[key].length !== 3 || c[key].some(v => v < 0 || v > 1)) throw new Error('palette ' + c.name);
  }
}
if (globalThis.process?.argv?.[1]?.endsWith('chapters.js')) demoChapters();
