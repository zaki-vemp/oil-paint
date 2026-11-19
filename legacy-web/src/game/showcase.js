// Cinematic Investor Showcase & Camera Tour for "Komorebi: Protocol Zero-Seven".
// Smoothly glides `follow.free` across six curated 7-second shots spanning Neo-Kyoto's landmarks,
// wet-asphalt neon reflections, sunlit woodland parks, street life, skybridges, siege mechs, and
// the Amber Sanctuary Spire. Starts from `?tour=1` or the Tour button.
import * as THREE from 'three';
import { EXTENT } from '../oilpaint/common.js';
import { createDayNight } from './daynight.js';

export const SHOT_DURATION = 7.0;

export const SHOTS = [
  {
    id: 'sunleaf-torii',
    kicker: 'Shot 01 // Sunleaf Row · Midnight Neon',
    title: 'Sunleaf Row Cyber-Skyline & Neon Torii Gateways',
    subtitle: 'World-Pinned Paint · Neon Reflections on Wet Asphalt',
    phase: 'night',
    accent: '#00f0ff',
    fromPos: [-118.2, 3.6, 214.0],
    toPos: [-121.2, 5.8, 191.5],
    fromTarget: [-120.0, 6.8, 172.0],
    toTarget: [-82.0, 11.5, 156.0],
  },
  {
    id: 'daybreak-parks',
    kicker: 'Shot 02 // West Woodland & Skyline · Daybreak Preview',
    title: 'Daybreak Over Neo-Kyoto & Lush Woodland Parks',
    subtitle: 'Dynamic 10-Minute Day / Night Cycle · Cel-Painted Canopies, Sky-Glass High-Rises & Alpine Peaks',
    phase: 'day',
    accent: '#fcee0a',
    fromPos: [-112.0, 13.5, 76.0],
    toPos: [-86.0, 10.8, 52.0],
    fromTarget: [-28.0, 9.0, 96.0],
    toTarget: [-12.0, 12.0, 118.0],
  },
  {
    id: 'petal-arcade',
    kicker: 'Shot 03 // Petal Arcade · Living Street Simulation',
    title: 'Petal Arcade Bustling Street Life, Night-Market Stalls & Fleet Vehicles',
    subtitle: 'Market Stalls, Pedestrians & Five Vehicle Classes',
    phase: 'night',
    accent: '#ff4fa3',
    fromPos: [-26.5, 3.8, 164.0],
    toPos: [-16.5, 3.1, 157.5],
    fromTarget: [-11.0, 1.9, 146.5],
    toTarget: [-18.5, 2.3, 136.0],
  },
  {
    id: 'moonveil-skybridges',
    kicker: 'Shot 04 // Jade Heights & Moonveil Stacks · Vertical City',
    title: 'Jade Heights & Moonveil Elevated Skybridges',
    subtitle: '6 Distinct Cyberpunk Districts · Batched Instanced Architecture · GPU-Driven Spirit Petals',
    phase: 'night',
    accent: '#19ffc6',
    fromPos: [-38.5, 5.4, -22.0],
    toPos: [-41.2, 10.2, -45.5],
    fromTarget: [-40.0, 14.2, -62.0],
    toTarget: [-12.0, 12.8, -68.0],
  },
  {
    id: 'ashen-mech',
    kicker: 'Shot 05 // Ashen Hollow · Combat & Enemy Escalation',
    title: 'Ashen Hollow Aethelgard Siege Mech & Syndicate Garrison',
    subtitle: 'Swordplay, Parries & Perfect Dodges · Aethelgard Infantry and Siege Mechs',
    phase: 'night',
    accent: '#ff5a4d',
    fromPos: [4.5, 4.9, -127.5],
    toPos: [-1.8, 3.6, -132.5],
    fromTarget: [-9.0, 2.4, -144.0],
    toTarget: [-12.0, 2.2, -145.5],
  },
  {
    id: 'amber-sanctuary',
    kicker: 'Shot 06 // Amber Sanctuary · Protocol Zero-Seven Finale',
    title: 'The Amber Sanctuary Spire — Finale Overlook',
    subtitle: 'Six-Sector Campaign · Eight-Player Online City',
    phase: 'night',
    accent: '#ffb000',
    fromPos: [21.0, 5.4, -256.0],
    toPos: [33.0, 10.6, -263.5],
    fromTarget: [33.0, 8.6, -288.0],
    toTarget: [33.0, 12.8, -288.0],
  },
];

const EXIT_KEYS = new Set(['KeyW', 'KeyA', 'KeyS', 'KeyD', 'ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight']);

const easeGlide = (u) => {
  const t = Math.min(1, Math.max(0, u));
  const smooth = t * t * (3 - 2 * t);
  return 0.32 * t + 0.68 * smooth;
};

export function createShowcase({ follow, player = null, heightAt, dayNight = null, hud = null }) {
  let active = false;
  let index = 0;
  let elapsed = 0;
  let startedAtMs = 0;

  const eyePos = new THREE.Vector3();
  const lookTarget = new THREE.Vector3();

  const doc = globalThis.document ?? null;
  let overlay = doc?.getElementById('showcase-overlay') ?? null;
  if (doc && !overlay) {
    overlay = doc.createElement('section');
    overlay.id = 'showcase-overlay';
    overlay.hidden = true;
    overlay.setAttribute('aria-label', 'Cinematic showcase tour');
    overlay.innerHTML = `
      <div class="showcase-bar top">
        <div class="showcase-brand">
          <span class="showcase-badge">◈ Investor Showcase</span>
          <span class="showcase-counter">01 / 06</span>
          <span class="showcase-phase">☾ Neon Night</span>
        </div>
        <div class="showcase-dots" role="tablist" aria-label="Showcase camera shots"></div>
      </div>
      <div class="showcase-bar bottom">
        <div class="showcase-copy">
          <span class="showcase-kicker"></span>
          <h2 class="showcase-title"></h2>
          <p class="showcase-sub"></p>
        </div>
        <div class="showcase-controls">
          <button type="button" class="showcase-next">Next Shot <kbd>Space</kbd></button>
          <button type="button" class="showcase-exit">Exit Tour &amp; Play <kbd>Esc / Click</kbd></button>
        </div>
      </div>`;
    doc.body.appendChild(overlay);
  }

  const els = overlay ? {
    counter: overlay.querySelector('.showcase-counter'),
    phase: overlay.querySelector('.showcase-phase'),
    dotsWrap: overlay.querySelector('.showcase-dots'),
    kicker: overlay.querySelector('.showcase-kicker'),
    title: overlay.querySelector('.showcase-title'),
    sub: overlay.querySelector('.showcase-sub'),
    nextBtn: overlay.querySelector('.showcase-next'),
    exitBtn: overlay.querySelector('.showcase-exit'),
    dots: [],
    fills: [],
  } : null;

  if (els?.dotsWrap) {
    els.dotsWrap.innerHTML = SHOTS.map((s, i) => (
      `<button type="button" class="showcase-dot" role="tab" data-shot="${i}" aria-label="Shot ${i + 1}: ${s.title}" title="${s.title}"><i></i></button>`
    )).join('');
    els.dots = [...els.dotsWrap.querySelectorAll('.showcase-dot')];
    els.fills = els.dots.map((d) => d.querySelector('i'));
    els.dots.forEach((btn, i) => {
      btn.addEventListener('click', (e) => {
        e.stopPropagation();
        goTo(i);
        btn.blur();
      });
    });
    els.nextBtn?.addEventListener('click', (e) => {
      e.stopPropagation();
      next();
      els.nextBtn.blur();
    });
    els.exitBtn?.addEventListener('click', (e) => {
      e.stopPropagation();
      stop();
    });
  }

  function worldTimeNow() {
    return globalThis.window?.lab?.shared?.uTime?.value ?? 0;
  }

  function syncButtons() {
    if (!doc) return;
    const headerBtn = doc.getElementById('tour-button');
    if (headerBtn) {
      headerBtn.classList.toggle('active', active);
      headerBtn.setAttribute('aria-pressed', String(active));
      const span = headerBtn.querySelector('span:last-child');
      if (span) span.textContent = active ? 'Exit Tour' : 'Tour';
    }
    const menuBtn = doc.getElementById('menu-tour-button');
    if (menuBtn) {
      menuBtn.textContent = active ? '◈ Stop Investor Tour' : '◈ Launch Investor Tour';
    }
  }

  function applyShotMeta() {
    const shot = SHOTS[index];
    if (dayNight) {
      dayNight.previewPhase(shot.phase, worldTimeNow());
      globalThis.window?.lab?.syncDayNight?.(10);
    }
    if (!overlay || !els) return;
    overlay.style.setProperty('--tour-accent', shot.accent);
    if (els.counter) els.counter.textContent = `0${index + 1} / 0${SHOTS.length}`;
    if (els.phase) els.phase.textContent = shot.phase === 'day' ? '☀ Daylight Preview' : '☾ Neon Night';
    if (els.kicker) els.kicker.textContent = shot.kicker;
    if (els.title) els.title.textContent = shot.title;
    if (els.sub) els.sub.textContent = shot.subtitle;
    els.dots.forEach((dot, i) => {
      dot.classList.toggle('active', i === index);
      dot.classList.toggle('done', i < index);
      dot.setAttribute('aria-selected', String(i === index));
      if (els.fills[i]) els.fills[i].style.transform = `scaleX(${i < index ? 1 : 0})`;
    });
  }

  function samplePose(u) {
    const shot = SHOTS[index];
    const k = easeGlide(u);
    const px = shot.fromPos[0] + (shot.toPos[0] - shot.fromPos[0]) * k;
    const pz = shot.fromPos[2] + (shot.toPos[2] - shot.fromPos[2]) * k;
    const pyOff = shot.fromPos[1] + (shot.toPos[1] - shot.fromPos[1]) * k;
    const py = Math.max(heightAt(px, pz) + 1.4, heightAt(px, pz) + pyOff);

    const tx = shot.fromTarget[0] + (shot.toTarget[0] - shot.fromTarget[0]) * k;
    const tz = shot.fromTarget[2] + (shot.toTarget[2] - shot.fromTarget[2]) * k;
    const tyOff = shot.fromTarget[1] + (shot.toTarget[1] - shot.fromTarget[1]) * k;
    const ty = heightAt(tx, tz) + tyOff;

    eyePos.set(px, py, pz);
    lookTarget.set(tx, ty, tz);
    follow.free = { position: eyePos, target: lookTarget };
  }

  function goTo(nextIndex) {
    index = ((nextIndex % SHOTS.length) + SHOTS.length) % SHOTS.length;
    elapsed = 0;
    applyShotMeta();
    samplePose(0);
  }

  function start(startIndex = 0) {
    active = true;
    startedAtMs = globalThis.performance?.now?.() ?? 0;
    if (doc) {
      const menu = doc.getElementById('menu');
      if (menu?.open) menu.close();
      if (globalThis.window) globalThis.window.__paused = false;
      doc.documentElement.dataset.showcase = 'true';
      doc.exitPointerLock?.();
    }
    if (overlay) overlay.hidden = false;
    syncButtons();
    goTo(startIndex);
  }

  function stop({ silent = false } = {}) {
    if (!active) return;
    active = false;
    follow.free = null;
    if (player && follow) {
      follow.pivot.set(0, 0, 0);
    }
    if (overlay) overlay.hidden = true;
    if (doc) delete doc.documentElement.dataset.showcase;
    if (dayNight) {
      dayNight.previewPhase(null, worldTimeNow());
      globalThis.window?.lab?.syncDayNight?.(10);
    }
    syncButtons();
    if (!silent && !doc?.documentElement.hasAttribute('data-clean')) {
      (hud ?? globalThis.window?.lab?.hunt?.hud)?.toast?.('Tour exited — live controls active');
    }
  }

  function toggle() {
    if (active) stop();
    else start(0);
    return active;
  }

  function next() {
    if (!active) start(0);
    else goTo(index + 1);
  }

  function update(dt) {
    if (!active) return false;
    elapsed += Math.max(0, dt);
    if (elapsed >= SHOT_DURATION) {
      index = (index + 1) % SHOTS.length;
      elapsed -= SHOT_DURATION;
      applyShotMeta();
    }
    const u = Math.min(1, elapsed / SHOT_DURATION);
    samplePose(u);
    if (els?.fills[index]) {
      els.fills[index].style.transform = `scaleX(${u.toFixed(3)})`;
    }
    return true;
  }

  /** Keeps distant district crews, citizens, gates, relays and parked fleet visible around the tour camera. */
  function revealWorld() {
    if (!active) return;
    const lab = globalThis.window?.lab;
    if (!lab) return;
    const fx = lookTarget.x;
    const fz = lookTarget.z;
    const cx = eyePos.x;
    const cz = eyePos.z;
    if (lab.hunt?.enemies) {
      for (const e of lab.hunt.enemies) {
        if (e.dead || e.gone || (e.duel && !e.ready)) continue;
        const dTarget = Math.hypot(e.x - fx, e.z - fz);
        const dCam = Math.hypot(e.x - cx, e.z - cz);
        if (Math.min(dTarget, dCam) < 105 && e.mesh) e.mesh.visible = true;
      }
    }
    if (lab.rides?.rides) {
      for (const v of lab.rides.rides) {
        const dTarget = Math.hypot(v.x - fx, v.z - fz);
        const dCam = Math.hypot(v.x - cx, v.z - cz);
        if (Math.min(dTarget, dCam) < 120 && v.root) v.root.visible = true;
      }
    }
    if (lab.missions) {
      for (const sop of lab.missions.sectorOps ?? []) {
        for (const t of sop.targets ?? []) {
          if (t.done || !t.root || t.kind === 'courier') continue;
          const d = Math.min(Math.hypot(t.x - fx, t.z - fz), Math.hypot(t.x - cx, t.z - cz));
          if (d < 95) {
            t.root.visible = true;
            if (t.model) t.model.visible = d < 80;
            if (t.ring) t.ring.visible = true;
            if (t.beam) t.beam.visible = true;
          }
        }
      }
      for (const s of lab.missions.echoShrines ?? []) {
        if (s.collected || !s.root) continue;
        const d = Math.min(Math.hypot(s.x - fx, s.z - fz), Math.hypot(s.x - cx, s.z - cz));
        if (d < 95) {
          s.root.visible = true;
          if (s.model) s.model.visible = d < 80;
        }
      }
    }
  }

  if (globalThis.addEventListener) {
    globalThis.addEventListener('keydown', (e) => {
      if (!active || globalThis.window?.__paused) return;
      if (e.code === 'Space') {
        e.preventDefault();
        e.stopPropagation();
        next();
      } else if (EXIT_KEYS.has(e.code)) {
        stop();
      }
    });
    doc?.getElementById('view')?.addEventListener('pointerdown', () => {
      const now = globalThis.performance?.now?.() ?? 0;
      if (active && !globalThis.window?.__paused && now - startedAtMs > 180) {
        stop();
      }
    });
  }

  return {
    shots: SHOTS,
    shotDuration: SHOT_DURATION,
    start,
    stop,
    toggle,
    next,
    goTo,
    update,
    revealWorld,
    get active() { return active; },
    get index() { return index; },
    get shot() { return SHOTS[index]; },
    get focus() { return active ? lookTarget : null; },
  };
}

export function demoShowcase() {
  if (SHOTS.length !== 6) throw new Error('expected 6 curated showcase shots');
  if (SHOT_DURATION !== 7.0) throw new Error('expected 7-second showcase shots');
  for (const [i, s] of SHOTS.entries()) {
    if (!s.id || !s.title || !s.subtitle || (s.phase !== 'day' && s.phase !== 'night')) {
      throw new Error('invalid showcase metadata at ' + i);
    }
    for (const key of ['fromPos', 'toPos', 'fromTarget', 'toTarget']) {
      const v = s[key];
      if (!Array.isArray(v) || v.length !== 3 || v.some((n) => !Number.isFinite(n))) {
        throw new Error(`bad ${key} in ${s.id}`);
      }
      if (Math.abs(v[0]) > EXTENT - 16 || Math.abs(v[2]) > EXTENT - 16) {
        throw new Error(`${s.id} ${key} exceeds world extent`);
      }
    }
    const travel = Math.hypot(s.toPos[0] - s.fromPos[0], s.toPos[1] - s.fromPos[1], s.toPos[2] - s.fromPos[2]);
    if (travel < 4) throw new Error('shot camera should glide smoothly: ' + s.id);
  }
  if (!SHOTS.some((s) => s.phase === 'day') || !SHOTS.some((s) => s.phase === 'night')) {
    throw new Error('showcase must preview both day and night');
  }

  const follow = { free: null, pivot: new THREE.Vector3(1, 2, 3) };
  const player = { feet: new THREE.Vector3(0, 0, 234) };
  const dayNight = createDayNight({ startPhase: 'night' });
  const tour = createShowcase({ follow, player, heightAt: () => 0, dayNight });
  if (tour.active || follow.free !== null) throw new Error('must not auto-start');
  tour.start(0);
  if (!tour.active || !follow.free || tour.index !== 0) throw new Error('start shot 0');
  const startX = follow.free.position.x;
  tour.update(3.5);
  if (Math.abs(follow.free.position.x - startX) < 0.2) throw new Error('camera must glide over time');
  tour.update(3.6);
  if (tour.index !== 1 || dayNight.phase !== 'day') throw new Error('advance to shot 1 daybreak preview');
  tour.next();
  if (tour.index !== 2 || dayNight.phase !== 'night') throw new Error('next shot transitions back to night');
  dayNight.advance(120);
  tour.stop({ silent: true });
  if (tour.active || follow.free !== null || dayNight.phase !== 'night' || dayNight.remaining !== 480) {
    throw new Error('stop releases the camera and restores the live phase and countdown');
  }
  console.log('showcase.js: ok');
}

if (globalThis.process?.argv?.[1]?.endsWith('showcase.js')) demoShowcase();
