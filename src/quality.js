// Graphics follow sustained frame performance, not a guessed GPU model. Auto starts conservatively,
// lowers detail sooner than it raises it, and ignores loading, pauses and hidden tabs. Explicit
// URL/menu choices keep their level; capture always uses fixed pixels for repeatable pictures.
export const PROFILES = {
  low: { level: 'low', label: 'Low', ratio: 0.8, pixels: 1e6, samples: 0, decorations: 0.35, drift: 900, distance: 60 },
  medium: { level: 'medium', label: 'Balanced', ratio: 1.1, pixels: 2e6, samples: 2, decorations: 0.65, drift: 1500, distance: 75 },
  high: { level: 'high', label: 'High', ratio: 1.5, pixels: 4e6, samples: 4, decorations: 1, drift: 2400, distance: 85 },
};
const LEVELS = Object.keys(PROFILES);
export const qualityMode = (value) => Object.hasOwn(PROFILES, value) ? value : 'auto';
const renderRatio = (profile, width, height, dpr, auto) => Math.min(dpr || 1, profile.ratio,
  auto ? Math.sqrt(profile.pixels / Math.max(1, width * height)) : Infinity);

/** The timing policy is pure, so slow/fast devices and tab changes can be checked without a GPU. */
export function createQualityPolicy(initial = 'medium') {
  let index = Math.max(0, LEVELS.indexOf(initial));
  let average = 0, warm = 3, cooldown = 0, slow = 0, fast = 0;
  return {
    get level() { return LEVELS[index]; },
    get fps() { return average > 0 ? Math.round(1000 / average) : null; },
    sample(seconds, active = true) {
      if (!active || !Number.isFinite(seconds) || seconds <= 0 || seconds > 0.25) {
        slow = fast = average = 0; warm = 1;
        return false;
      }
      const ms = seconds * 1000;
      average = average ? average + (ms - average) * (1 - Math.exp(-seconds)) : ms;
      if (warm > 0) { warm -= seconds; return false; }
      if (cooldown > 0) { cooldown -= seconds; return false; }
      slow = average > 23 ? slow + seconds : 0;
      fast = average < 17.8 ? fast + seconds : 0;
      const next = slow >= 2.5 ? Math.max(0, index - 1) : fast >= 10 ? Math.min(2, index + 1) : index;
      if (next === index) return false;
      index = next; slow = fast = 0; cooldown = 6;
      return true;
    },
  };
}

export function createGraphics({ renderer, capture = false, apply }) {
  let mode = qualityMode(new URLSearchParams(location.search).get('quality'));
  const modest = navigator.hardwareConcurrency <= 4 || (navigator.deviceMemory > 0 && navigator.deviceMemory <= 4);
  let policy = createQualityPolicy(modest ? 'low' : 'medium');
  let current;
  const effective = () => mode !== 'auto' ? mode : window.patch?.quality === 'low' ? 'low' : policy.level;
  function resize() {
    current = PROFILES[effective()];
    renderer.setPixelRatio(capture ? 1 : renderRatio(current, innerWidth, innerHeight, devicePixelRatio, mode === 'auto'));
    renderer.setSize(innerWidth, innerHeight);
    apply({ ...current, samples: Math.min(current.samples, renderer.capabilities.maxSamples) });
    const label = document.getElementById('quality-status');
    if (label) label.textContent = `${mode === 'auto' ? 'Auto · ' : ''}${current.label} detail`;
    document.documentElement.dataset.quality = current.level;
  }
  resize();
  window.patch?.addEventListener?.('qualitychange', resize);
  return {
    resize,
    setMode(value) {
      mode = qualityMode(value);
      policy = createQualityPolicy(current.level);
      resize();
    },
    sample(seconds, active = true) {
      if (!capture && mode === 'auto' && window.patch?.quality !== 'low' && policy.sample(seconds, active && !document.hidden)) resize();
    },
    get status() { return { mode, level: current.level, pixelRatio: renderer.getPixelRatio(), fps: policy.fps }; },
  };
}

export function demoQuality() {
  const check = (ok, message) => { if (!ok) throw new Error(message); };
  const feed = (p, seconds, frame, active = true) => { for (let i = 0; i < seconds / frame; i++) p.sample(frame, active); };
  const slow = createQualityPolicy('high');
  feed(slow, 25, 1 / 30);
  check(slow.level === 'low', 'sustained slow frames lower detail');
  feed(slow, 1, 1 / 144);
  check(slow.level === 'low', 'a brief fast patch cannot raise detail');
  feed(slow, 45, 1 / 60);
  check(slow.level === 'high', 'sustained headroom restores detail');
  const idle = createQualityPolicy('high');
  feed(idle, 30, 1 / 20, false); idle.sample(3); idle.sample(NaN);
  check(idle.level === 'high', 'paused, hidden and suspended pages do not downgrade');
  const middle = createQualityPolicy('medium');
  feed(middle, 60, 1 / 50);
  check(middle.level === 'medium', 'the gap between thresholds prevents oscillation');
  check(qualityMode('low') === 'low' && qualityMode('invalid') === 'auto' && qualityMode('__proto__') === 'auto' && qualityMode(null) === 'auto', 'explicit modes and safe default');
  check(Object.values(PROFILES).every(p => p.ratio <= 1.5), 'pixel ratio keeps its existing ceiling');
  const ratio = renderRatio(PROFILES.low, 3840, 2160, 2, true);
  check(3840 * 2160 * ratio ** 2 <= 1e6 + 1, 'a large monitor cannot overwhelm the low auto preset');
  check(renderRatio(PROFILES.high, 3840, 2160, 2, false) === 1.5, 'manual high keeps full detail within the original ceiling');
}
if (globalThis.process?.argv?.[1]?.endsWith('quality.js')) demoQuality();
