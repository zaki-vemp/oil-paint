// Procedural Web Audio engine: adaptive multi-layer music, voiced radio comms (OS SpeechSynthesis
// with automatic formant-synth fallback), combat/vehicle/mission SFX, continuous RPM engine audio,
// and persisted Master / Music / Voice / SFX volume controls. Safe in headless and ?capture runs.

export const AUDIO_KEY = 'oilpaint.audio.v1';
export const DEFAULT_AUDIO = Object.freeze({ master: 0.8, music: 0.55, voice: 0.85, sfx: 0.75, muted: false });

export const SPEAKERS = {
  sora: { name: 'Sora // Handler', pitch: 1.16, rate: 1.04, f0: 235, color: '#00f0ff', preferFemale: true },
  ren: { name: 'Ren // Architect', pitch: 0.78, rate: 0.93, f0: 122, color: '#ff2a6d', preferFemale: false },
  kael: { name: 'Kael // Courier 07', pitch: 0.98, rate: 1.02, f0: 155, color: '#fcee0a', preferFemale: false },
  dispatch: { name: 'Grid Dispatch', pitch: 0.88, rate: 1.08, f0: 175, color: '#ffb000', preferFemale: false },
};

const clamp01 = (v, fallback) => {
  const n = Number(v);
  return Number.isFinite(n) ? Math.max(0, Math.min(1, n)) : fallback;
};

/** Normalizes persisted or partial audio settings into finite [0..1] volumes and boolean mute. */
export function normalizeSettings(raw) {
  const s = raw && typeof raw === 'object' ? raw : {};
  return {
    master: clamp01(s.master, DEFAULT_AUDIO.master),
    music: clamp01(s.music, DEFAULT_AUDIO.music),
    voice: clamp01(s.voice, DEFAULT_AUDIO.voice),
    sfx: clamp01(s.sfx, DEFAULT_AUDIO.sfx),
    muted: Boolean(s.muted),
  };
}

// Formant targets [F1, F2, F3] in Hz for vowel families used by the headless/fallback voice synth.
const VOWELS = {
  a: [730, 1090, 2440],
  e: [530, 1840, 2480],
  i: [290, 2290, 3010],
  o: [570, 840, 2410],
  u: [320, 870, 2240],
};

/** Breaks text into timed speech syllables with vowel formants and punctuation pauses for fallback synthesis. */
export function syllablesOf(text) {
  const clean = String(text ?? '').trim();
  if (!clean) return [];
  const words = clean.split(/\s+/);
  const out = [];
  for (let wi = 0; wi < words.length; wi++) {
    const raw = words[wi];
    const pause = /[.!?]$/.test(raw) ? 0.16 : /[,;:—–-]$/.test(raw) ? 0.08 : 0.025;
    const word = raw.toLowerCase().replace(/[^a-z0-9]/g, '');
    if (!word) continue;
    const chunks = word.match(/[^aeiouy]*[aeiouy]+(?:[^aeiouy]*(?=$|[^aeiouy]))?/g) || [word];
    for (let ci = 0; ci < chunks.length; ci++) {
      const ch = chunks[ci];
      const vMatch = ch.match(/[aeiouy]/);
      const vKey = vMatch ? (vMatch[0] === 'y' ? 'i' : vMatch[0]) : 'e';
      const unvoiced = /^[tpkfsxh]/.test(ch);
      const dur = Math.max(0.065, Math.min(0.16, 0.055 + ch.length * 0.015));
      const stress = ci === 0 ? 1.08 : 0.96;
      out.push({
        vowel: vKey,
        formants: VOWELS[vKey] ?? VOWELS.e,
        dur,
        pause: ci === chunks.length - 1 ? pause : 0.01,
        stress,
        unvoiced,
      });
    }
  }
  return out;
}

const midiHz = (m) => 440 * 2 ** ((m - 69) / 12);

// D Dorian synthwave palettes for the 6 city districts, plus E minor arctic motif for aurora.
const AUTUMN_CHAPTERS = [
  { root: 38, arp: [62, 65, 69, 72, 74, 72, 69, 65], bass: [38, 38, 41, 43, 38, 38, 45, 43], lead: [74, 0, 77, 79, 81, 0, 79, 77] },
  { root: 41, arp: [65, 69, 72, 74, 77, 74, 72, 69], bass: [41, 41, 45, 48, 41, 41, 43, 45], lead: [77, 0, 81, 79, 77, 0, 74, 72] },
  { root: 40, arp: [64, 67, 71, 74, 76, 74, 71, 67], bass: [40, 40, 43, 45, 40, 40, 47, 45], lead: [76, 0, 79, 83, 81, 0, 79, 76] },
  { root: 43, arp: [67, 71, 74, 77, 79, 77, 74, 71], bass: [43, 43, 45, 48, 43, 43, 41, 45], lead: [79, 0, 81, 84, 83, 0, 79, 77] },
  { root: 36, arp: [60, 64, 67, 71, 72, 71, 67, 64], bass: [36, 36, 40, 43, 36, 36, 41, 40], lead: [72, 0, 76, 79, 77, 0, 74, 72] },
  { root: 38, arp: [62, 66, 69, 74, 76, 74, 69, 66], bass: [38, 38, 45, 43, 38, 38, 47, 45], lead: [74, 78, 81, 0, 86, 81, 78, 74] },
];
const AURORA_BELLS = [64, 67, 71, 74, 76, 79, 71, 67];

function loadSaved() {
  try {
    const raw = globalThis.localStorage?.getItem(AUDIO_KEY);
    return normalizeSettings(raw ? JSON.parse(raw) : null);
  } catch {
    return { ...DEFAULT_AUDIO };
  }
}

function saveSettings(settings) {
  try {
    globalThis.localStorage?.setItem(AUDIO_KEY, JSON.stringify(settings));
  } catch { /* ignore private-browsing storage errors */ }
}

export function createAudio({ scene = 'autumn', silent = false } = {}) {
  let settings = loadSaved();
  let currentScene = scene;
  let isSilent = Boolean(silent);
  let ctx = null;
  let masterGain = null;
  let musicGain = null;
  let musicFilter = null;
  let voiceGain = null;
  let sfxGain = null;
  let delayNode = null;
  let delayFeedback = null;
  let reverbNode = null;
  let noiseBuf = null;
  let unlocked = false;
  let disposed = false;

  // Voice tracking
  let activeSpeechUntil = 0;
  let activeFormantNodes = [];
  let currentVoiceLevel = 0;
  let lastSpeakAt = 0;
  let lastVoiceCheck = 0;
  let hasGesture = false;
  let voicesHooked = false;
  let cachedVoices = [];
  const listeners = new Set();

  // Music adaptive state
  const state = {
    chapter: 0,
    combat: 0,
    boss: false,
    driving: false,
    speed: 0,
    paused: false,
    nearHostiles: 0,
  };
  let combatBlend = 0;
  let driveBlend = 0;
  let stepIndex = 0;
  let nextNoteTime = 0;
  let schedulerTimer = null;

  // Persistent vehicle engine synth
  let rideOscA = null;
  let rideOscB = null;
  let rideFilter = null;
  let rideGain = null;
  let skidGain = null;
  let skidFilter = null;
  let skidSrc = null;

  const notify = () => {
    const snap = getSettings();
    for (const fn of listeners) {
      try { fn(snap); } catch { /* listener error isolated */ }
    }
  };

  function getSettings() {
    return { ...settings };
  }

  function applyGains(now) {
    if (!ctx || !masterGain) return;
    const t = now ?? ctx.currentTime;
    const m = settings.muted ? 0 : settings.master;
    masterGain.gain.setTargetAtTime(m, t, 0.03);
    const voiceBusy = currentVoiceLevel > 0.02 || performance.now() < activeSpeechUntil;
    const duck = voiceBusy ? 0.42 : 1.0;
    const pauseMul = state.paused ? 0.55 : 1.0;
    musicGain.gain.setTargetAtTime(settings.music * duck * pauseMul, t, 0.08);
    if (musicFilter) {
      musicFilter.frequency.setTargetAtTime(state.paused ? 950 : 9000, t, 0.12);
    }
    voiceGain.gain.setTargetAtTime(settings.voice, t, 0.02);
    sfxGain.gain.setTargetAtTime(settings.sfx, t, 0.02);
  }

  function buildNoise(c) {
    const len = Math.floor(c.sampleRate * 1.5);
    const buf = c.createBuffer(1, len, c.sampleRate);
    const ch = buf.getChannelData(0);
    for (let i = 0; i < len; i++) ch[i] = Math.random() * 2 - 1;
    return buf;
  }

  function buildImpulse(c, seconds = 2.4) {
    const len = Math.floor(c.sampleRate * seconds);
    const buf = c.createBuffer(2, len, c.sampleRate);
    for (let k = 0; k < 2; k++) {
      const ch = buf.getChannelData(k);
      for (let i = 0; i < len; i++) {
        const decay = Math.pow(1 - i / len, 2.6);
        ch[i] = (Math.random() * 2 - 1) * decay * 0.45;
      }
    }
    return buf;
  }

  function ensureContext() {
    if (isSilent || disposed) return null;
    const AC = globalThis.AudioContext || globalThis.webkitAudioContext;
    if (!AC) return null;
    if (!ctx) {
      try {
        ctx = new AC();
      } catch {
        return null;
      }
      const comp = ctx.createDynamicsCompressor();
      comp.threshold.value = -10;
      comp.knee.value = 12;
      comp.ratio.value = 4;
      comp.attack.value = 0.005;
      comp.release.value = 0.16;

      masterGain = ctx.createGain();
      musicGain = ctx.createGain();
      musicFilter = ctx.createBiquadFilter();
      musicFilter.type = 'lowpass';
      musicFilter.frequency.value = 9000;
      voiceGain = ctx.createGain();
      sfxGain = ctx.createGain();

      delayNode = ctx.createDelay(1.0);
      delayNode.delayTime.value = currentScene === 'aurora' ? 0.44 : 0.258;
      delayFeedback = ctx.createGain();
      delayFeedback.gain.value = currentScene === 'aurora' ? 0.38 : 0.26;
      const delayFilter = ctx.createBiquadFilter();
      delayFilter.type = 'lowpass';
      delayFilter.frequency.value = 2600;
      delayNode.connect(delayFilter);
      delayFilter.connect(delayFeedback);
      delayFeedback.connect(delayNode);
      delayFilter.connect(musicFilter);

      reverbNode = ctx.createConvolver();
      reverbNode.buffer = buildImpulse(ctx, 2.6);
      const revGain = ctx.createGain();
      revGain.gain.value = currentScene === 'aurora' ? 0.45 : 0.14;
      reverbNode.connect(revGain);
      revGain.connect(musicFilter);

      musicFilter.connect(musicGain);
      musicGain.connect(masterGain);
      voiceGain.connect(masterGain);
      sfxGain.connect(masterGain);
      masterGain.connect(comp);
      comp.connect(ctx.destination);

      noiseBuf = buildNoise(ctx);
      nextNoteTime = ctx.currentTime + 0.08;
      applyGains(ctx.currentTime);
      startScheduler();
    }
    if (ctx.state === 'suspended') {
      ctx.resume().then(() => { unlocked = true; }).catch(() => {});
    } else if (ctx.state === 'running') {
      unlocked = true;
    }
    return ctx;
  }

  function refreshVoices() {
    try {
      if (globalThis.speechSynthesis?.getVoices) {
        cachedVoices = globalThis.speechSynthesis.getVoices();
      }
    } catch { cachedVoices = []; }
  }

  function hookVoices() {
    if (voicesHooked || isSilent) return;
    voicesHooked = true;
    refreshVoices();
    try { globalThis.speechSynthesis?.addEventListener?.('voiceschanged', refreshVoices); } catch { /* ignore */ }
  }

  if (typeof window !== 'undefined' && !isSilent) {
    const unlock = () => {
      hasGesture = true;
      hookVoices();
      const c = ensureContext();
      if (c && c.state === 'suspended') c.resume().catch(() => {});
    };
    window.addEventListener('pointerdown', unlock, { passive: true });
    window.addEventListener('keydown', unlock, { passive: true });
    window.addEventListener('touchstart', unlock, { passive: true });
  }

  // --- Synth building blocks --------------------------------------------------------------------
  function playTone({ freq = 440, endFreq = null, type = 'sine', dur = 0.15, gain = 0.2, attack = 0.006, dest = sfxGain, time = null, filterFreq = 0, q = 1, detune = 0 }) {
    const c = ensureContext();
    if (!c || !dest) return;
    const t = time ?? c.currentTime;
    const osc = c.createOscillator();
    const g = c.createGain();
    osc.type = type;
    osc.frequency.setValueAtTime(Math.max(20, freq), t);
    if (detune) osc.detune.setValueAtTime(detune, t);
    if (endFreq != null) osc.frequency.exponentialRampToValueAtTime(Math.max(20, endFreq), t + dur);
    g.gain.setValueAtTime(0.0001, t);
    g.gain.linearRampToValueAtTime(gain, t + attack);
    g.gain.exponentialRampToValueAtTime(0.0001, t + dur);
    if (filterFreq > 0) {
      const f = c.createBiquadFilter();
      f.type = 'lowpass';
      f.frequency.setValueAtTime(filterFreq, t);
      f.Q.value = q;
      osc.connect(f);
      f.connect(g);
    } else {
      osc.connect(g);
    }
    g.connect(dest);
    osc.start(t);
    osc.stop(t + dur + 0.02);
  }

  function playNoise({ dur = 0.12, gain = 0.2, filterType = 'bandpass', freq = 1200, endFreq = null, q = 1.2, dest = sfxGain, time = null }) {
    const c = ensureContext();
    if (!c || !noiseBuf || !dest) return;
    const t = time ?? c.currentTime;
    const src = c.createBufferSource();
    src.buffer = noiseBuf;
    const f = c.createBiquadFilter();
    f.type = filterType;
    f.frequency.setValueAtTime(Math.max(60, freq), t);
    if (endFreq != null) f.frequency.exponentialRampToValueAtTime(Math.max(60, endFreq), t + dur);
    f.Q.value = q;
    const g = c.createGain();
    g.gain.setValueAtTime(0.0001, t);
    g.gain.linearRampToValueAtTime(gain, t + 0.006);
    g.gain.exponentialRampToValueAtTime(0.0001, t + dur);
    src.connect(f); f.connect(g); g.connect(dest);
    src.start(t, Math.random() * 0.4, dur + 0.02);
  }

  // --- Adaptive Procedural Music ---------------------------------------------------------------
  function scheduleAutumnStep(step, t) {
    const pal = AUTUMN_CHAPTERS[Math.max(0, Math.min(AUTUMN_CHAPTERS.length - 1, state.chapter | 0))];
    const step16 = step % 16;
    const barStep = step % 32;
    const chordNote = pal.bass[Math.floor((step % 64) / 8)] ?? pal.root;

    // 1. Pad chord every 2 bars
    if (barStep === 0) {
      const notes = [chordNote + 12, chordNote + 15, chordNote + 19];
      for (const m of notes) {
        const osc = ctx.createOscillator();
        const osc2 = ctx.createOscillator();
        const f = ctx.createBiquadFilter();
        const g = ctx.createGain();
        osc.type = 'sawtooth'; osc2.type = 'sawtooth';
        osc.frequency.setValueAtTime(midiHz(m), t);
        osc2.frequency.setValueAtTime(midiHz(m), t);
        osc.detune.value = -7; osc2.detune.value = 7;
        f.type = 'lowpass';
        f.frequency.setValueAtTime(480 + combatBlend * 420, t);
        f.frequency.exponentialRampToValueAtTime(950 + combatBlend * 650, t + 1.8);
        g.gain.setValueAtTime(0.0001, t);
        g.gain.linearRampToValueAtTime(0.036, t + 0.45);
        g.gain.exponentialRampToValueAtTime(0.0001, t + 4.0);
        osc.connect(f); osc2.connect(f); f.connect(g); g.connect(musicFilter);
        if (reverbNode) g.connect(reverbNode);
        osc.start(t); osc2.start(t); osc.stop(t + 4.1); osc2.stop(t + 4.1);
      }
    }

    // 2. Kick on beats 0, 4, 8, 12
    if (step16 % 4 === 0) {
      const kickGain = 0.22 + combatBlend * 0.1 + driveBlend * 0.08;
      playTone({ freq: 130, endFreq: 38, type: 'sine', dur: 0.18, gain: kickGain, attack: 0.003, dest: musicFilter, time: t });
    }

    // 3. Rolling 8th/16th synth bass
    if (step16 % 2 === 0 || (driveBlend > 0.35 && step16 % 2 === 1)) {
      const oct = step16 % 4 === 2 ? 12 : 0;
      const bGain = (step16 % 2 === 0 ? 0.11 : 0.06 * driveBlend) + combatBlend * 0.04;
      playTone({
        freq: midiHz(chordNote + oct), type: 'sawtooth', dur: 0.12, gain: bGain,
        attack: 0.005, dest: musicFilter, time: t, filterFreq: 340 + combatBlend * 420 + driveBlend * 320, q: 3.2,
      });
    }

    // 4. Dorian Arp
    if (step16 % 2 === 0 || driveBlend > 0.25) {
      const arpNote = pal.arp[step16 % pal.arp.length] + (driveBlend > 0.5 && step16 % 4 === 3 ? 12 : 0);
      const aGain = 0.038 + driveBlend * 0.035;
      const osc = ctx.createOscillator();
      const f = ctx.createBiquadFilter();
      const g = ctx.createGain();
      osc.type = 'triangle';
      osc.frequency.setValueAtTime(midiHz(arpNote), t);
      f.type = 'lowpass'; f.frequency.setValueAtTime(1600 + driveBlend * 1400, t);
      g.gain.setValueAtTime(0.0001, t);
      g.gain.linearRampToValueAtTime(aGain, t + 0.006);
      g.gain.exponentialRampToValueAtTime(0.0001, t + 0.16);
      osc.connect(f); f.connect(g); g.connect(musicFilter);
      if (delayNode) g.connect(delayNode);
      osc.start(t); osc.stop(t + 0.18);
    }

    // 5. Combat & Boss layers: snare on 4/12, hi-hats, urgent lead
    if (combatBlend > 0.08 || driveBlend > 0.25) {
      if (step16 === 4 || step16 === 12) {
        const sLevel = Math.max(combatBlend, driveBlend * 0.7);
        playNoise({ dur: 0.15, gain: 0.12 * sLevel, filterType: 'bandpass', freq: 1800, q: 0.8, dest: musicFilter, time: t });
        playTone({ freq: 210, endFreq: 110, type: 'triangle', dur: 0.08, gain: 0.09 * sLevel, dest: musicFilter, time: t });
      }
      if (step16 % 2 === 1) {
        const hLevel = Math.max(combatBlend, driveBlend * 0.8);
        playNoise({ dur: 0.045, gain: 0.05 * hLevel, filterType: 'highpass', freq: 6200, q: 0.9, dest: musicFilter, time: t });
      }
    }
    if (combatBlend > 0.2 && step16 % 2 === 0) {
      const leadNote = pal.lead[Math.floor(step16 / 2) % pal.lead.length];
      if (leadNote > 0) {
        const lGain = (state.boss ? 0.075 : 0.05) * combatBlend;
        playTone({
          freq: midiHz(leadNote), type: 'sawtooth', dur: 0.22, gain: lGain, attack: 0.012,
          dest: delayNode || musicFilter, time: t, filterFreq: 2100, q: 1.5,
        });
      }
    }
  }

  function scheduleAuroraStep(step, t) {
    const step16 = step % 16;
    if (step % 32 === 0) {
      for (const m of [40, 47, 52, 55]) {
        const osc = ctx.createOscillator();
        const f = ctx.createBiquadFilter();
        const g = ctx.createGain();
        osc.type = 'sine';
        osc.frequency.setValueAtTime(midiHz(m), t);
        f.type = 'lowpass'; f.frequency.setValueAtTime(620 + combatBlend * 380, t);
        g.gain.setValueAtTime(0.0001, t);
        g.gain.linearRampToValueAtTime(0.045, t + 1.2);
        g.gain.exponentialRampToValueAtTime(0.0001, t + 7.0);
        osc.connect(f); f.connect(g); g.connect(musicFilter);
        if (reverbNode) g.connect(reverbNode);
        osc.start(t); osc.stop(t + 7.1);
      }
    }
    if (step16 % 4 === 0 && ((step * 7) % 5 < 3 || combatBlend > 0.2)) {
      const note = AURORA_BELLS[(step >> 2) % AURORA_BELLS.length] + (combatBlend > 0.4 ? 12 : 0);
      const osc = ctx.createOscillator();
      const mod = ctx.createOscillator();
      const modGain = ctx.createGain();
      const g = ctx.createGain();
      const hz = midiHz(note);
      osc.type = 'sine'; osc.frequency.setValueAtTime(hz, t);
      mod.type = 'sine'; mod.frequency.setValueAtTime(hz * 2, t);
      modGain.gain.setValueAtTime(hz * 0.45, t);
      modGain.gain.exponentialRampToValueAtTime(1, t + 0.8);
      g.gain.setValueAtTime(0.0001, t);
      g.gain.linearRampToValueAtTime(0.045 + combatBlend * 0.03, t + 0.02);
      g.gain.exponentialRampToValueAtTime(0.0001, t + 1.6);
      mod.connect(modGain); modGain.connect(osc.frequency);
      osc.connect(g); g.connect(musicFilter);
      if (reverbNode) g.connect(reverbNode);
      if (delayNode) g.connect(delayNode);
      osc.start(t); mod.start(t); osc.stop(t + 1.7); mod.stop(t + 1.7);
    }
  }

  function startScheduler() {
    if (schedulerTimer || isSilent) return;
    schedulerTimer = setInterval(() => {
      if (!ctx || ctx.state !== 'running' || settings.muted || settings.master <= 0.001 || settings.music <= 0.001) {
        if (ctx) nextNoteTime = Math.max(nextNoteTime, ctx.currentTime + 0.05);
        return;
      }
      const stepSec = currentScene === 'aurora' ? 60 / 68 / 4 : 60 / 116 / 4;
      while (nextNoteTime < ctx.currentTime + 0.14) {
        if (currentScene === 'aurora') scheduleAuroraStep(stepIndex, nextNoteTime);
        else scheduleAutumnStep(stepIndex, nextNoteTime);
        nextNoteTime += stepSec;
        stepIndex++;
      }
    }, 35);
  }

  // --- Voice Engine (SpeechSynthesis + Automatic Formant Fallback) -----------------------------
  function radioSquelch(open = true) {
    const c = ensureContext();
    if (!c) return;
    const t = c.currentTime;
    playNoise({ dur: open ? 0.065 : 0.05, gain: 0.14, filterType: 'bandpass', freq: open ? 2400 : 1650, endFreq: open ? 3200 : 900, q: 2.2, dest: voiceGain, time: t });
    playTone({ freq: open ? 1180 : 880, endFreq: open ? 1520 : 620, type: 'sine', dur: 0.055, gain: 0.09, dest: voiceGain, time: t });
  }

  function stopVoice() {
    try { globalThis.speechSynthesis?.cancel?.(); } catch { /* ignore */ }
    for (const n of activeFormantNodes) {
      try { n.stop?.(); n.disconnect?.(); } catch { /* ignore */ }
    }
    activeFormantNodes = [];
    activeSpeechUntil = 0;
    currentVoiceLevel = 0;
    applyGains();
  }

  function speakFormant(line, profile) {
    const c = ensureContext();
    if (!c) return 1.5;
    const syl = syllablesOf(line);
    if (!syl.length) return 0.5;
    let t = c.currentTime + 0.07;
    const f0Base = profile.f0;
    for (let i = 0; i < syl.length; i++) {
      const s = syl[i];
      const osc = c.createOscillator();
      osc.type = 'sawtooth';
      const contour = 1 + 0.08 * Math.sin((i / Math.max(1, syl.length - 1)) * Math.PI) - 0.08 * (i / Math.max(1, syl.length));
      osc.frequency.setValueAtTime(f0Base * s.stress * contour, t);
      osc.frequency.linearRampToValueAtTime(f0Base * contour * (i === syl.length - 1 ? 0.86 : 1.02), t + s.dur);

      const radioBand = c.createBiquadFilter();
      radioBand.type = 'bandpass';
      radioBand.frequency.value = 1450;
      radioBand.Q.value = 0.7;

      const env = c.createGain();
      env.gain.setValueAtTime(0.0001, t);
      env.gain.linearRampToValueAtTime(0.18, t + 0.015);
      env.gain.setValueAtTime(0.16, t + s.dur * 0.72);
      env.gain.exponentialRampToValueAtTime(0.0001, t + s.dur);

      for (const [fi, freq] of s.formants.entries()) {
        const bp = c.createBiquadFilter();
        bp.type = 'bandpass';
        bp.frequency.setValueAtTime(freq * (profile === SPEAKERS.sora ? 1.1 : 0.96), t);
        bp.Q.value = fi === 0 ? 5.5 : 8;
        const fg = c.createGain();
        fg.gain.value = fi === 0 ? 0.7 : fi === 1 ? 0.5 : 0.25;
        osc.connect(bp); bp.connect(fg); fg.connect(radioBand);
      }
      if (s.unvoiced && noiseBuf) {
        const ns = c.createBufferSource();
        ns.buffer = noiseBuf;
        const nf = c.createBiquadFilter();
        nf.type = 'highpass'; nf.frequency.value = 3200;
        const ng = c.createGain();
        ng.gain.setValueAtTime(0.06, t);
        ng.gain.exponentialRampToValueAtTime(0.0001, t + 0.035);
        ns.connect(nf); nf.connect(ng); ng.connect(voiceGain);
        ns.start(t, 0, 0.04);
        activeFormantNodes.push(ns);
      }
      radioBand.connect(env);
      env.connect(voiceGain);
      osc.start(t);
      osc.stop(t + s.dur + 0.02);
      activeFormantNodes.push(osc);
      t += s.dur + s.pause;
    }
    const totalSec = Math.max(0.6, t - c.currentTime);
    setTimeout(() => radioSquelch(false), Math.round(totalSec * 1000));
    return totalSec;
  }

  function speak(line, speaker = 'sora', { interrupt = true, squelch = true } = {}) {
    if (isSilent || !line) return;
    const profile = SPEAKERS[speaker] ?? SPEAKERS.sora;
    if (interrupt) stopVoice();
    const now = performance.now();
    lastSpeakAt = now;
    if (!hasGesture && !ctx) {
      activeSpeechUntil = now + Math.max(1200, line.length * 48);
      return;
    }
    if (squelch) radioSquelch(true);

    const effectiveVol = settings.muted ? 0 : Math.min(1, settings.master * settings.voice);
    if (hasGesture) refreshVoices();
    const synth = globalThis.speechSynthesis;
    const hasOsVoice = Boolean(hasGesture && synth && cachedVoices.length > 0);
    if (hasOsVoice && effectiveVol > 0.01) {
      try {
        const u = new SpeechSynthesisUtterance(line);
        const en = cachedVoices.filter((v) => /^en/i.test(v.lang));
        const pool = en.length ? en : cachedVoices;
        const pick = pool.find((v) => profile.preferFemale ? /female|samantha|victoria|karen|zira|aria|google us english/i.test(v.name) : /male|daniel|alex|david|guy|ryan|google uk english male/i.test(v.name)) || pool[0];
        if (pick) u.voice = pick;
        u.pitch = profile.pitch;
        u.rate = profile.rate;
        u.volume = effectiveVol;
        const estMs = Math.max(1400, line.length * 62);
        activeSpeechUntil = now + estMs;
        u.onend = () => {
          activeSpeechUntil = 0;
          if (squelch) radioSquelch(false);
          applyGains();
        };
        u.onerror = () => { activeSpeechUntil = 0; applyGains(); };
        synth.speak(u);
        applyGains();
        return;
      } catch { /* fall through to formant synth */ }
    }
    const sec = speakFormant(line, profile);
    activeSpeechUntil = now + sec * 1000;
    applyGains();
  }

  function bark(kind) {
    if (isSilent || settings.muted || (!hasGesture && !ctx)) return;
    const c = ensureContext();
    if (!c) return;
    const map = {
      swing: [185, 210, 0.09, 'e'],
      heavy: [130, 95, 0.15, 'o'],
      hurt: [220, 145, 0.14, 'a'],
      parry: [310, 420, 0.12, 'i'],
      clear: [240, 320, 0.14, 'a'],
    };
    const [fStart, fEnd, dur, vKey] = map[kind] ?? map.swing;
    const t = c.currentTime;
    const osc = c.createOscillator();
    osc.type = 'sawtooth';
    osc.frequency.setValueAtTime(fStart, t);
    osc.frequency.exponentialRampToValueAtTime(fEnd, t + dur);
    const bp = c.createBiquadFilter();
    bp.type = 'bandpass';
    bp.frequency.value = (VOWELS[vKey]?.[1] ?? 1300) * 0.85;
    bp.Q.value = 3.2;
    const g = c.createGain();
    g.gain.setValueAtTime(0.0001, t);
    g.gain.linearRampToValueAtTime(0.085, t + 0.015);
    g.gain.exponentialRampToValueAtTime(0.0001, t + dur);
    osc.connect(bp); bp.connect(g); g.connect(voiceGain);
    osc.start(t); osc.stop(t + dur + 0.02);
  }

  // --- Sound Effects ---------------------------------------------------------------------------
  function play(name, detail = 1) {
    if (isSilent || settings.muted || (!hasGesture && !ctx)) return;
    const c = ensureContext();
    if (!c) return;
    const t = c.currentTime;
    const s = Math.max(0.4, Math.min(1.6, Number(detail) || 1));
    switch (name) {
      case 'slash':
        playNoise({ dur: 0.11, gain: 0.19, filterType: 'bandpass', freq: 2400 * s, endFreq: 750, q: 1.5, time: t });
        playTone({ freq: 520 * s, endFreq: 190, type: 'triangle', dur: 0.1, gain: 0.09, time: t });
        break;
      case 'heavy':
        playNoise({ dur: 0.22, gain: 0.26, filterType: 'bandpass', freq: 1400, endFreq: 260, q: 1.1, time: t });
        playTone({ freq: 240, endFreq: 72, type: 'sawtooth', dur: 0.22, gain: 0.16, filterFreq: 900, time: t });
        break;
      case 'hit':
        playTone({ freq: 195 * s, endFreq: 55, type: 'triangle', dur: 0.12, gain: 0.28, time: t });
        playNoise({ dur: 0.08, gain: 0.22, filterType: 'lowpass', freq: 1800, time: t });
        break;
      case 'parry':
        playTone({ freq: 1480, endFreq: 2150, type: 'sine', dur: 0.26, gain: 0.25, time: t });
        playTone({ freq: 2240, endFreq: 1880, type: 'triangle', dur: 0.22, gain: 0.16, time: t + 0.015 });
        break;
      case 'dodge':
        playNoise({ dur: 0.16, gain: 0.18, filterType: 'highpass', freq: 1800, endFreq: 4200, time: t });
        playTone({ freq: 440, endFreq: 880, type: 'sine', dur: 0.18, gain: 0.14, time: t });
        break;
      case 'deflect':
        playTone({ freq: 920, endFreq: 1760, type: 'triangle', dur: 0.18, gain: 0.22, time: t });
        playNoise({ dur: 0.09, gain: 0.18, filterType: 'bandpass', freq: 3400, time: t });
        break;
      case 'spark':
        playTone({ freq: 720, endFreq: 1320, type: 'sine', dur: 0.14, gain: 0.18, time: t });
        playTone({ freq: 1120, endFreq: 560, type: 'triangle', dur: 0.16, gain: 0.1, time: t + 0.02 });
        break;
      case 'dash':
        playNoise({ dur: 0.15, gain: 0.2, filterType: 'bandpass', freq: 950, endFreq: 2800, q: 1.0, time: t });
        break;
      case 'flare':
        playTone({ freq: 320, endFreq: 960, type: 'sawtooth', dur: 0.35, gain: 0.2, filterFreq: 2400, time: t });
        playNoise({ dur: 0.32, gain: 0.2, filterType: 'bandpass', freq: 2200, endFreq: 600, time: t });
        break;
      case 'whirl':
        playNoise({ dur: 0.28, gain: 0.22, filterType: 'bandpass', freq: 1200, endFreq: 2600, q: 2.0, time: t });
        playTone({ freq: 340, endFreq: 680, type: 'triangle', dur: 0.26, gain: 0.15, time: t });
        break;
      case 'plunge':
        playTone({ freq: 140, endFreq: 36, type: 'sine', dur: 0.28, gain: 0.32, time: t });
        playNoise({ dur: 0.22, gain: 0.25, filterType: 'lowpass', freq: 1100, endFreq: 240, time: t });
        break;
      case 'bolt':
        playTone({ freq: 980, endFreq: 310, type: 'sawtooth', dur: 0.12, gain: 0.15, filterFreq: 2600, time: t });
        break;
      case 'lock':
        playTone({ freq: 1320, type: 'sine', dur: 0.07, gain: 0.11, time: t });
        playTone({ freq: 1760, type: 'sine', dur: 0.07, gain: 0.11, time: t + 0.09 });
        break;
      case 'playerHurt':
        playTone({ freq: 165, endFreq: 62, type: 'sawtooth', dur: 0.18, gain: 0.26, filterFreq: 700, time: t });
        playNoise({ dur: 0.12, gain: 0.2, filterType: 'lowpass', freq: 900, time: t });
        break;
      case 'guilt':
        playTone({ freq: 220, endFreq: 146, type: 'sawtooth', dur: 0.26, gain: 0.2, filterFreq: 850, time: t });
        break;
      case 'step':
        playNoise({ dur: 0.045, gain: 0.055 * s, filterType: 'lowpass', freq: currentScene === 'aurora' ? 680 : 950, time: t });
        break;
      case 'enterRide':
        playTone({ freq: 95, endFreq: 220, type: 'sawtooth', dur: 0.22, gain: 0.18, filterFreq: 850, time: t });
        break;
      case 'exitRide':
        playTone({ freq: 180, endFreq: 90, type: 'triangle', dur: 0.12, gain: 0.14, time: t });
        break;
      case 'crash':
        playTone({ freq: 110, endFreq: 34, type: 'sawtooth', dur: 0.24, gain: 0.28 * Math.min(1.4, s), filterFreq: 950, time: t });
        playNoise({ dur: 0.2, gain: 0.26 * Math.min(1.4, s), filterType: 'lowpass', freq: 1600, time: t });
        break;
      case 'horn':
        playTone({ freq: 392, type: 'sawtooth', dur: 0.18, gain: 0.14, filterFreq: 1600, time: t });
        playTone({ freq: 493.88, type: 'sawtooth', dur: 0.18, gain: 0.14, filterFreq: 1600, time: t });
        break;
      case 'objHit':
        playTone({ freq: 420, endFreq: 220, type: 'square', dur: 0.09, gain: 0.16, filterFreq: 1600, time: t });
        break;
      case 'objBreak':
        playTone({ freq: 280, endFreq: 68, type: 'sawtooth', dur: 0.34, gain: 0.26, filterFreq: 1800, time: t });
        playNoise({ dur: 0.26, gain: 0.22, filterType: 'bandpass', freq: 1200, endFreq: 320, time: t });
        break;
      case 'syncTick':
        playTone({ freq: 620 + s * 420, type: 'sine', dur: 0.06, gain: 0.09, time: t });
        break;
      case 'missionStep':
        playTone({ freq: 523.25, type: 'triangle', dur: 0.11, gain: 0.17, time: t });
        playTone({ freq: 659.25, type: 'triangle', dur: 0.16, gain: 0.17, time: t + 0.08 });
        break;
      case 'missionDone':
      case 'gateOpen':
        [523.25, 659.25, 783.99, 1046.5].forEach((hz, i) => {
          playTone({ freq: hz, type: 'triangle', dur: 0.24, gain: 0.18, time: t + i * 0.075 });
        });
        break;
      case 'uiClick':
        playTone({ freq: 880, endFreq: 660, type: 'sine', dur: 0.045, gain: 0.1, time: t });
        break;
      default:
        break;
    }
  }

  // --- Continuous Engine & Skid Audio -----------------------------------------------------------
  function ensureRideNodes() {
    if (rideOscA || !ctx) return;
    rideOscA = ctx.createOscillator();
    rideOscB = ctx.createOscillator();
    rideFilter = ctx.createBiquadFilter();
    rideGain = ctx.createGain();
    rideOscA.type = 'sawtooth';
    rideOscB.type = 'triangle';
    rideOscA.frequency.value = 55;
    rideOscB.frequency.value = 110;
    rideFilter.type = 'lowpass';
    rideFilter.frequency.value = 320;
    rideGain.gain.value = 0.0001;
    rideOscA.connect(rideFilter);
    rideOscB.connect(rideFilter);
    rideFilter.connect(rideGain);
    rideGain.connect(sfxGain);
    rideOscA.start();
    rideOscB.start();

    if (noiseBuf) {
      skidSrc = ctx.createBufferSource();
      skidSrc.buffer = noiseBuf;
      skidSrc.loop = true;
      skidFilter = ctx.createBiquadFilter();
      skidFilter.type = 'bandpass';
      skidFilter.frequency.value = 1600;
      skidFilter.Q.value = 2.4;
      skidGain = ctx.createGain();
      skidGain.gain.value = 0.0001;
      skidSrc.connect(skidFilter);
      skidFilter.connect(skidGain);
      skidGain.connect(sfxGain);
      skidSrc.start();
    }
  }

  function updateRide({ driving = false, kind = 'car', speed = 0, top = 25, throttle = 0, sliding = false, air = false, paused = false } = {}) {
    if (!ctx || isSilent) return;
    ensureRideNodes();
    if (!rideGain) return;
    const t = ctx.currentTime;
    const active = driving && !paused && !settings.muted;
    if (!active) {
      rideGain.gain.setTargetAtTime(0.0001, t, 0.06);
      skidGain?.gain.setTargetAtTime(0.0001, t, 0.06);
      return;
    }
    const norm = Math.max(0, Math.min(1.2, Math.abs(speed) / Math.max(1, top)));
    const baseHz = kind === 'bike' ? 68 : kind === 'bicycle' ? 42 : kind === 'pickup' ? 46 : 54;
    const pitch = baseHz + norm * (kind === 'bike' ? 165 : kind === 'bicycle' ? 65 : 120) + (air ? 22 : 0) + Math.abs(throttle) * 14;
    rideOscA.frequency.setTargetAtTime(pitch, t, 0.05);
    rideOscB.frequency.setTargetAtTime(pitch * (kind === 'bike' ? 1.5 : 2), t, 0.05);
    rideFilter.frequency.setTargetAtTime(240 + norm * 1100 + Math.abs(throttle) * 260, t, 0.06);
    const engineVol = kind === 'bicycle' ? 0.03 + norm * 0.04 : 0.055 + norm * 0.09 + Math.abs(throttle) * 0.025;
    rideGain.gain.setTargetAtTime(engineVol, t, 0.05);
    if (skidGain) {
      skidGain.gain.setTargetAtTime(sliding && !air ? 0.08 : 0.0001, t, 0.04);
    }
  }

  function update(dt, nextState = {}) {
    Object.assign(state, nextState);
    const targetCombat = state.boss ? 1 : Math.min(1, (state.combat ?? 0));
    const targetDrive = state.driving && Math.abs(state.speed ?? 0) > 4 ? 1 : 0;
    const k = 1 - Math.exp(-dt * 2.4);
    combatBlend += (targetCombat - combatBlend) * k;
    driveBlend += (targetDrive - driveBlend) * k;

    const now = performance.now();
    if (now < activeSpeechUntil) {
      const elapsed = (now - lastSpeakAt) * 0.001;
      currentVoiceLevel = 0.45 + 0.5 * Math.abs(Math.sin(elapsed * 14) * Math.cos(elapsed * 5.7));
    } else {
      currentVoiceLevel = Math.max(0, currentVoiceLevel - dt * 6);
    }
    if (ctx && now - lastVoiceCheck > 120) {
      lastVoiceCheck = now;
      applyGains();
    }
  }

  function setSettings(patch) {
    settings = normalizeSettings({ ...settings, ...patch });
    saveSettings(settings);
    applyGains();
    notify();
    return getSettings();
  }

  function toggleMute() {
    ensureContext();
    return setSettings({ muted: !settings.muted });
  }

  function setScene(next) {
    currentScene = next;
    if (delayNode && ctx) {
      delayNode.delayTime.setTargetAtTime(next === 'aurora' ? 0.44 : 0.258, ctx.currentTime, 0.2);
      delayFeedback.gain.setTargetAtTime(next === 'aurora' ? 0.38 : 0.26, ctx.currentTime, 0.2);
    }
  }

  function dispose() {
    disposed = true;
    if (schedulerTimer) { clearInterval(schedulerTimer); schedulerTimer = null; }
    stopVoice();
    try { ctx?.close?.(); } catch { /* ignore */ }
    ctx = null;
  }

  return {
    getSettings,
    setSettings,
    toggleMute,
    setScene,
    onChange(fn) { listeners.add(fn); return () => listeners.delete(fn); },
    play,
    bark,
    speak,
    stopVoice,
    update,
    updateRide,
    unlock() { hasGesture = true; hookVoices(); return ensureContext(); },
    dispose,
    get ready() { return unlocked; },
    get voiceActive() { return performance.now() < activeSpeechUntil; },
    get voiceLevel() { return currentVoiceLevel; },
  };
}

let sharedInstance = null;
/** Singleton helper so boot.js UI controls and the active scene share one audio engine. */
export function getAudio(opts = {}) {
  if (!sharedInstance) sharedInstance = createAudio(opts);
  else if (opts.scene) sharedInstance.setScene(opts.scene);
  return sharedInstance;
}

export function demoAudio() {
  const s1 = normalizeSettings({ master: 1.8, music: -0.4, voice: 'bad', sfx: 0.42, muted: 1 });
  if (s1.master !== 1 || s1.music !== 0 || s1.voice !== DEFAULT_AUDIO.voice || s1.sfx !== 0.42 || s1.muted !== true) {
    throw new Error('normalizeSettings failed: ' + JSON.stringify(s1));
  }
  const s2 = normalizeSettings(null);
  if (s2.master !== DEFAULT_AUDIO.master || s2.muted !== false) throw new Error('default audio mismatch');
  const syl = syllablesOf('Target locked, Courier. Overload the two jammers!');
  if (syl.length < 10 || syl.some((x) => !Number.isFinite(x.dur) || x.formants.length !== 3)) {
    throw new Error('syllablesOf invalid');
  }
  const a = createAudio({ scene: 'autumn', silent: true });
  a.setSettings({ master: 0.6, muted: false });
  a.play('slash');
  a.speak('Testing radio link', 'sora');
  a.update(1 / 60, { chapter: 2, combat: 1, driving: true, speed: 20 });
  a.updateRide({ driving: true, kind: 'bike', speed: 18, top: 31, throttle: 1 });
  if (a.getSettings().master !== 0.6) throw new Error('settings update failed');
  a.dispose();
  console.log('audio.js: ok');
}

if (globalThis.process?.argv?.[1]?.endsWith('audio.js')) demoAudio();
