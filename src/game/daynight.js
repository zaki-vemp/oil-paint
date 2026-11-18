// Ten-minute day/night cycle: every 600 seconds the world shifts smoothly between day and night.
// Solo and free roam advance with simulation time (pausing freezes the clock); online syncs to wall time.

export const PHASE_SECONDS = 600;
export const TRANSITION_SECONDS = 12;

/** Formats remaining seconds (0..600) as MM:SS. */
export function formatClock(seconds) {
  const s = Math.max(0, Math.ceil(seconds));
  const m = Math.floor(s / 60);
  const rem = s % 60;
  return `${String(m).padStart(2, '0')}:${String(rem).padStart(2, '0')}`;
}

const sstep01 = (t) => {
  const u = Math.min(1, Math.max(0, t));
  return u * u * (3 - 2 * u);
};

/** Computes the day/night state at `elapsedSeconds`, starting at day (or night when `startNight`). */
export function dayNightAt(elapsedSeconds, startNight = false) {
  const total = Math.max(0, elapsedSeconds);
  const slot = Math.floor(total / PHASE_SECONDS);
  const inSlot = total - slot * PHASE_SECONDS;
  const remaining = PHASE_SECONDS - inSlot;
  const targetOf = (k) => (((k % 2) === 1) !== Boolean(startNight)) ? 1 : 0;
  const isNight = targetOf(slot) === 1;
  const phase = isNight ? 'night' : 'day';

  const half = TRANSITION_SECONDS * 0.5;
  let night = targetOf(slot);
  if (inSlot > PHASE_SECONDS - half) {
    const u = (inSlot - (PHASE_SECONDS - half)) / TRANSITION_SECONDS;
    const from = targetOf(slot);
    const to = targetOf(slot + 1);
    night = from + (to - from) * sstep01(u);
  } else if (slot > 0 && inSlot < half) {
    const u = 0.5 + inSlot / TRANSITION_SECONDS;
    const from = targetOf(slot - 1);
    const to = targetOf(slot);
    night = from + (to - from) * sstep01(u);
  }
  const twilight = 4 * night * (1 - night);
  return { phase, night, twilight, remaining, mmss: formatClock(remaining), slot };
}

/**
 * Stateful day/night controller exposed on `window.lab.dayNight`.
 * `q` may carry `?phase=day` or `?phase=night` to override the starting phase.
 */
export function createDayNight({ startPhase = 'day', online = false, q = null, onShift = null } = {}) {
  const asked = q?.get?.('phase');
  const initial = asked === 'night' || asked === 'day' ? asked : startPhase;
  const startNight = initial === 'night';
  let offset = 0;
  let manualBase = null;
  let lastSlot = 0;
  let initialized = false;
  let state = dayNightAt(0, startNight);

  function clock(worldTime = 0) {
    const base = manualBase != null ? manualBase + worldTime : (online ? Date.now() / 1000 : worldTime);
    return base + offset;
  }

  function update(worldTime = 0) {
    state = dayNightAt(clock(worldTime), startNight);
    if (globalThis.document?.documentElement) {
      globalThis.document.documentElement.setAttribute('data-daytime', state.phase);
    }
    if (!initialized) {
      initialized = true;
      lastSlot = state.slot;
    } else if (state.slot !== lastSlot) {
      lastSlot = state.slot;
      onShift?.(state.phase, state);
    }
    return state;
  }

  function setPhase(want, worldTime = 0) {
    if (want !== 'day' && want !== 'night') return state;
    const targetNight = want === 'night';
    const slot = targetNight === startNight ? 0 : 1;
    manualBase = slot * PHASE_SECONDS + TRANSITION_SECONDS - worldTime;
    offset = 0;
    return update(worldTime);
  }

  function advance(sec, worldTime = 0) {
    offset += sec;
    return update(worldTime);
  }

  return {
    cycleSeconds: PHASE_SECONDS,
    update,
    setPhase,
    advance,
    get state() { return state; },
    get phase() { return state.phase; },
    get night() { return state.night; },
    get twilight() { return state.twilight; },
    get remaining() { return state.remaining; },
    get mmss() { return state.mmss; },
  };
}

export function demoDayNight() {
  if (PHASE_SECONDS !== 600) throw new Error('phase must be 10 minutes (600 s)');
  const s0 = dayNightAt(0, false);
  if (s0.phase !== 'day' || s0.night !== 0 || s0.remaining !== 600 || s0.mmss !== '10:00') throw new Error('t=0 day: ' + JSON.stringify(s0));
  const s590 = dayNightAt(590, false);
  if (s590.phase !== 'day' || s590.night !== 0) throw new Error('t=590 still day');
  const s600 = dayNightAt(600, false);
  if (s600.phase !== 'night' || s600.twilight <= 0.9) throw new Error('t=600 shifts to night with twilight: ' + JSON.stringify(s600));
  const s610 = dayNightAt(610, false);
  if (s610.phase !== 'night' || s610.night !== 1) throw new Error('t=610 full night');
  const s1200 = dayNightAt(1200, false);
  if (s1200.phase !== 'day' || s1200.twilight <= 0.9) throw new Error('t=1200 shifts back to day');
  const s1210 = dayNightAt(1210, false);
  if (s1210.phase !== 'day' || s1210.night !== 0) throw new Error('t=1210 full day');

  const n0 = dayNightAt(0, true);
  if (n0.phase !== 'night' || n0.night !== 1) throw new Error('startNight at t=0');
  const n600 = dayNightAt(600, true);
  if (n600.phase !== 'day' || n600.twilight <= 0.9) throw new Error('startNight shifts to day at 600');
  const n1210 = dayNightAt(1210, true);
  if (n1210.phase !== 'night' || n1210.night !== 1) throw new Error('startNight shifts back to night');

  const shifts = [];
  const dn = createDayNight({ startPhase: 'day', onShift: (p) => shifts.push(p) });
  dn.update(0);
  if (dn.phase !== 'day' || dn.night !== 0) throw new Error('ctrl init');
  dn.update(600);
  dn.update(1200);
  if (shifts.join(',') !== 'night,day') throw new Error('expected shifts night,day, got ' + shifts.join(','));
  dn.setPhase('night', 10);
  if (dn.phase !== 'night' || dn.night !== 1) throw new Error('setPhase night');
  dn.setPhase('day', 10);
  if (dn.phase !== 'day' || dn.night !== 0) throw new Error('setPhase day');
}

if (globalThis.process?.argv?.[1]?.endsWith('daynight.js')) demoDayNight();
