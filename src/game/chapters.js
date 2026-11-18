// The trail's six art directions. Position selects a chapter, progression caps what is reachable,
// and the same entry drives the scenery and the HUD (a neon accent and one of two treatments). No visual setting is saved.
// grass: pavement, path: road, foliage / leafLight: the district's neon and its pale light.
export const CHAPTERS = [
  { name: 'Sunleaf Row', subtitle: 'A small step into a wider grid', style: 'field', color: '#00f0ff',
    grass: [0.10, 0.13, 0.17], foliage: [0.0, 0.85, 0.95], leafLight: [1.0, 0.86, 0.45],
    sky: [0.03, 0.05, 0.12], haze: [0.10, 0.22, 0.32], path: [0.20, 0.22, 0.27],
    day: { grass: [0.60, 0.62, 0.65], path: [0.34, 0.36, 0.40], sky: [0.18, 0.52, 0.88], haze: [0.70, 0.81, 0.91] } },
  { name: 'Petal Arcade', subtitle: 'Follow the falling signal', style: 'field', color: '#ff4fa3',
    grass: [0.13, 0.10, 0.16], foliage: [1.0, 0.25, 0.6], leafLight: [1.0, 0.74, 0.86],
    sky: [0.07, 0.03, 0.12], haze: [0.30, 0.12, 0.28], path: [0.22, 0.20, 0.26],
    day: { grass: [0.64, 0.60, 0.63], path: [0.36, 0.35, 0.40], sky: [0.24, 0.50, 0.86], haze: [0.76, 0.79, 0.89] } },
  { name: 'Jade Heights', subtitle: 'The city learns your name', style: 'spirit', color: '#19ffc6',
    grass: [0.08, 0.14, 0.14], foliage: [0.1, 1.0, 0.75], leafLight: [0.68, 1.0, 0.6],
    sky: [0.02, 0.08, 0.10], haze: [0.08, 0.26, 0.24], path: [0.18, 0.23, 0.24],
    day: { grass: [0.56, 0.62, 0.61], path: [0.32, 0.36, 0.38], sky: [0.16, 0.55, 0.84], haze: [0.68, 0.82, 0.88] } },
  { name: 'Moonveil Stacks', subtitle: 'Keep a little light with you', style: 'spirit', color: '#6fb7ff',
    grass: [0.08, 0.10, 0.17], foliage: [0.35, 0.6, 1.0], leafLight: [0.78, 0.87, 1.0],
    sky: [0.02, 0.03, 0.10], haze: [0.10, 0.16, 0.34], path: [0.17, 0.19, 0.27],
    day: { grass: [0.58, 0.60, 0.66], path: [0.33, 0.35, 0.41], sky: [0.20, 0.52, 0.90], haze: [0.72, 0.80, 0.92] } },
  { name: 'Ashen Hollow', subtitle: 'Embers remember every blade', style: 'spirit', color: '#ff5a4d',
    grass: [0.14, 0.10, 0.11], foliage: [1.0, 0.3, 0.25], leafLight: [1.0, 0.62, 0.36],
    sky: [0.08, 0.03, 0.05], haze: [0.30, 0.12, 0.12], path: [0.23, 0.20, 0.20],
    day: { grass: [0.62, 0.58, 0.57], path: [0.37, 0.35, 0.37], sky: [0.26, 0.48, 0.82], haze: [0.78, 0.78, 0.85] } },
  { name: 'Amber Sanctuary', subtitle: 'Every run leaves a light', style: 'field', color: '#ffb000',
    grass: [0.15, 0.13, 0.10], foliage: [1.0, 0.7, 0.05], leafLight: [1.0, 0.9, 0.55],
    sky: [0.09, 0.06, 0.03], haze: [0.34, 0.22, 0.10], path: [0.25, 0.22, 0.18],
    day: { grass: [0.64, 0.61, 0.56], path: [0.37, 0.36, 0.34], sky: [0.22, 0.56, 0.92], haze: [0.78, 0.84, 0.88] } },
];

/** The whole trail is laid out small and stretched by this: the way, every spawn, gate, duel and boundary. */
export const SCALE = 3.0;
const BOUNDARIES = [52, 29, 3, -31, -63].map((z) => z * SCALE);
export function chapterAt(z, cleared = CHAPTERS.length, roam = false) {
  let i = 0;
  while (i < BOUNDARIES.length && z < BOUNDARIES[i]) i++;
  return roam ? i : Math.min(i, Math.max(0, cleared));
}

export function demoChapters() {
  if (CHAPTERS.length !== BOUNDARIES.length + 1 || CHAPTERS.length < 6 || new Set(CHAPTERS.map(c => c.style)).size !== 2) throw new Error('six chapters, two HUD styles');
  const at = (z, ...rest) => chapterAt(z * SCALE, ...rest);
  if (at(78) !== 0 || at(-50) !== 4 || at(-70) !== 5 || at(-70, 1) !== 1 || at(-70, 0, true) !== 5) throw new Error('chapter progression');
  for (const c of CHAPTERS) {
    for (const key of ['grass', 'foliage', 'leafLight', 'sky', 'haze', 'path']) {
      if (c[key].length !== 3 || c[key].some(v => v < 0 || v > 1)) throw new Error('palette ' + c.name);
    }
    for (const key of ['grass', 'path', 'sky', 'haze']) {
      if (!c.day || c.day[key].length !== 3 || c.day[key].some(v => v < 0 || v > 1)) throw new Error('day palette ' + c.name + ' ' + key);
    }
  }
}
if (globalThis.process?.argv?.[1]?.endsWith('chapters.js')) demoChapters();
