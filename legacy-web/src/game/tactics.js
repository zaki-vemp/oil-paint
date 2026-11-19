// Crews share one melee opening and one firing lane per target. Move data keeps windup,
// hit shape and recovery together so the telegraph and the animation can follow the same clock.
export const PATTERNS = {
  punk: [
    { name: 'Quick cross', wind: 0.50, reach: 1.55, arc: 0.76, recover: 0.75 },
    { name: 'Running hook', wind: 0.68, reach: 1.6, arc: 1.05, recover: 0.92, rush: 0.95, damage: 11 },
  ],
  enforcer: [
    { name: 'Heavy straight', wind: 0.65, reach: 1.95, arc: 0.68, recover: 1.1 },
    { name: 'Shield bash', wind: 0.79, reach: 2.05, arc: 0.88, recover: 1.25, rush: 0.7, damage: 19 },
  ],
  gunner: [
    { name: 'Pulse shot', wind: 0.95, reach: 11, shot: true, recover: 1.2 },
    { name: 'Stock strike', wind: 0.65, reach: 1.8, arc: 0.88, recover: 1.05, damage: 10 },
  ],
  blade: [
    { name: 'Draw lunge', wind: 0.60, reach: 2.1, arc: 0.54, recover: 1.08, rush: 2.4 },
    { name: 'Reverse cut', wind: 0.83, reach: 2.35, arc: 1.18, recover: 1.34, rush: 0.55, damage: 17 },
  ],
  brute: [
    { name: 'Pile-driver', wind: 0.90, reach: 2.6, arc: 0.64, recover: 1.52, height: 1.7, unblockable: true },
    { name: 'Seismic sweep', wind: 1.22, reach: 3.4, arc: Math.PI, recover: 1.68, height: 0.65, damage: 20 },
  ],
};

export const DEFENSE = {
  punk: { kind: 'Evade', range: 3.2, time: 0.28, cooldown: 3.7, speed: 5.8 },
  enforcer: { kind: 'Guard', range: 3.45, time: 0.55, cooldown: 4.4 },
  gunner: { kind: 'Evade', range: 3.7, time: 0.32, cooldown: 4.2, speed: 5.4 },
  blade: { kind: 'Evade', range: 3.65, time: 0.31, cooldown: 3.45, speed: 6.4 },
  brute: { kind: 'Guard', range: 3.85, time: 0.7, cooldown: 5.2 },
};

export function chooseAttack(e, distance) {
  if (e.kind === 'gunner') return Number(distance < 2.05);
  if (e.kind === 'enforcer' && e.reprisal) return 1;
  return (e.attackCount ?? 0) % 2;
}

export function attackFor(e) {
  return PATTERNS[e.kind]?.[e.attackIndex ?? 0];
}

export function crewOrder(groups) {
  for (const group of groups.values()) {
    for (const ranged of [false, true]) {
      const candidates = group.filter(o => o.ranged === ranged);
      candidates.sort((a, b) => Number(b.e.windup > 0) - Number(a.e.windup > 0) || a.score - b.score);
      for (const [i, o] of candidates.entries()) {
        o.lead = i === 0;
        o.side = i % 2 ? -1 : 1;
        o.radius = ranged ? 7 + i % 2 : 3.6 + 0.65 * Math.floor((i - 1) / 2);
      }
    }
  }
}

export function demoTactics() {
  const a = { windup: 0 }, b = { windup: 0.3 }, c = { windup: 0 };
  const pair = [{ e: a, score: 0, ranged: false }, { e: b, score: 4, ranged: false }, { e: c, score: 3, ranged: true }];
  crewOrder(new Map([[null, pair]]));
  if (pair[0].lead || !pair[1].lead || !pair[2].lead) throw new Error('a committed melee keeps its turn while ranged support can aim');
  for (const [kind, moves] of Object.entries(PATTERNS)) {
    if (moves.length !== 2 || moves[0].name === moves[1].name || moves.some(m => m.wind <= 0.28 || m.recover < 0.6 || m.reach <= 0)) throw new Error('readable moves for ' + kind);
  }
  if (chooseAttack({ kind: 'gunner' }, 1.5) !== 1 || chooseAttack({ kind: 'gunner' }, 6) !== 0) throw new Error('gunner switches between stock strike and shot');
}

if (globalThis.process?.argv?.[1]?.endsWith('tactics.js')) demoTactics();
