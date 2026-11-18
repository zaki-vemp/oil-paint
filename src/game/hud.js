// The heads-up display reads live game state: health, earned abilities, hostiles, active mission
// checklist, 3D world-space waypoint diamond, voiced radio comms and live minimap scan.
// The look is street-level cyberpunk (src/ui.css): cut plates, cell bars, one hot colour.
import * as THREE from 'three';
import { CHAPTERS } from './chapters.js';
import { POWERS, POWER_ORDER } from './levels.js';
import { MOVES } from './duel.js';
import { SPEAKERS } from './audio.js';
import { AVENUES, STREETS, GRID_MIN, GRID_MAX, zoneAt, ZONES } from '../world/grid.js';

/** Display name and threat tier for each enemy kind. */
export const FOES = { punk: ['Street punk', 1], enforcer: ['Corp enforcer', 2], gunner: ['Hazmat gunner', 2], blade: ['Razor ronin', 3], brute: ['Siege mech', 4], rival: ['Mirror rival', 5],
  civ: ['Citizen', 0], thief: ['Syndicate courier', 0] };
const COOL = { dash: 2.5, flare: 20 }; // seconds, to draw the drain on a slot
const pad = (n) => String(n).padStart(2, '0');
const icon = name => `<svg aria-hidden="true" viewBox="0 0 24 24"><use href="#i-${name}"/></svg>`;
const SPEAKER_META = {
  sora: { callsign: 'Sora', role: 'Ghost-Line Handler', freq: 'CH-07 // RESISTANCE LINK' },
  mira: { callsign: 'Dr. Mira', role: 'Archive Imprint', freq: 'SEED // NEURAL ARCHIVE' },
  ren: { callsign: 'Ren', role: 'Chief Architect', freq: 'SP-01 // SPECTER CHANNEL' },
  kael: { callsign: 'Kael // 07', role: 'Free Courier', freq: 'SUIT // COLLAR LINK' },
  dispatch: { callsign: 'Grid Dispatch', role: 'Security Overwatch', freq: 'AE-99 // CIVIC ALERT' },
  hana: { callsign: 'Hana', role: 'Amber Echo', freq: 'MEM-00 // CONSERVATORY' },
};

export function createHUD({ levels, roam, marks, route, camera = null, audio = null }) {
  const hud = document.createElement('div');
  hud.id = 'hud';
  hud.innerHTML = `
    <section class="quest" aria-label="Current objective">
      <div class="chapter-label">${levels ? `Sector 01 / ${pad(levels.length)}` : 'Side op · Snowbound'}</div>
      <h2>${levels ? CHAPTERS[0].name : 'Under the aurora'}</h2>
      <p class="objective">Push through the district.</p>
      <ul class="quest-steps" aria-label="Mission checklist"></ul>
      <p class="quest-tip"></p>
      <div class="quest-count"><span class="count"></span><span class="distance"></span><span class="cash"></span></div>
      ${levels ? '<div class="journey" aria-label="Chapter progress">' + levels.map((_, i) => `<i data-step="${i}"></i>`).join('') + '</div>' : ''}
    </section>
    <div class="map-wrap"><canvas class="minimap" width="288" height="288" aria-label="Local trail scan: your position, hostiles and mission targets"></canvas><span class="north">N</span><span class="map-label">${roam ? 'Scan · free roam' : levels ? 'Scan · spirit trail' : 'Scan · snowbound'}</span><span class="day-clock" aria-label="Day and night cycle">☀ Day · 10:00</span><span class="echo-count" hidden aria-label="Recovered memory echoes">◈ 0/12 Echoes</span></div>
    <section class="vitals" aria-label="Traveler status">
      <div class="portrait" aria-hidden="true"><img src="/characters/portrait.png" alt="" /></div>
      <div class="vitals-data"><div class="player-name">Kael // 07 <span class="hp-number">100 / 100</span></div>
        <div class="hp" role="meter" aria-label="Health" aria-valuemin="0" aria-valuemax="100"><span></span></div>
        <div class="spark" aria-label="Spirit energy"><i><b></b></i><i><b></b></i><i><b></b></i><em>Charge</em></div>
      </div>
    </section>
    <div class="target" hidden><div><strong></strong><span></span></div><div class="target-hp"><i></i></div><p></p></div>
    <section class="bossbar" hidden aria-label="Rival duel">
      <div class="boss-heading"><span>Duel protocol</span><strong></strong><b></b></div>
      <div class="boss-health" role="meter" aria-label="Rival health" aria-valuemin="0"><i></i></div>
      <p class="boss-cue"></p><p class="boss-lesson"></p>
      <div class="lesson-steps"><span>Read</span><span>Evade</span><span>Punish</span></div>
    </section>
    <nav id="power-options" class="actions" aria-label="Abilities">
      <div class="powers-heading"><strong>Powers</strong><span>Tap to use</span></div>
      <button data-action="attack" data-code="KeyJ" title="J or left click: sword strike">${icon('sword')}<kbd>J</kbd><span>Strike</span><small></small></button>
      ${(levels ? POWER_ORDER : ['spark']).map(k => `<button data-action="${k}" data-code="Key${POWERS[k].key}" title="${POWERS[k].hint}">${icon(k)}<kbd>${POWERS[k].key}</kbd><span>${k === 'heavy' ? 'Heavy' : k === 'flare' ? 'Flare' : POWERS[k].name}</span><small></small></button>`).join('')}
    </nav>
    <div class="waypoint" hidden aria-hidden="true"><span class="wp-arrow">▲</span><span class="wp-diamond">◈</span><strong class="wp-label"></strong><small class="wp-dist"></small></div>
    <div class="toast" role="status" aria-live="polite"></div>
    <div class="story" hidden role="status">
      <div class="comms-portrait" aria-hidden="true"><img src="/characters/sora.svg" alt="" /></div>
      <div class="comms-body">
        <div class="comms-head">
          <div class="comms-id"><strong class="comms-speaker">Sora</strong><span class="comms-role">Ghost-Line Handler</span></div>
          <div class="comms-bars" aria-hidden="true"><i></i><i></i><i></i><i></i><i></i><i></i><i></i></div>
        </div>
        <p></p>
        <div class="comms-foot"><span class="comms-freq">CH-07 // RESISTANCE LINK</span><button type="button">Dismiss <kbd>K</kbd></button></div>
      </div>
    </div>
    <div class="chapter-banner" aria-hidden="true"><span></span><strong></strong><p></p></div>
    <div class="hit-feedback" aria-hidden="true"></div>
    <div class="combo" aria-hidden="true"><b></b><span>hits</span></div>
    <div class="flash-text" aria-hidden="true"></div>
    <div class="heat" hidden role="status"><strong>Street hostile</strong><span></span></div>
    <div class="ride" hidden aria-label="Vehicle"><b>0</b><span>km/h</span><em></em><i></i></div>
    <button class="ride-prompt" data-code="KeyG" hidden><kbd>G</kbd><span></span></button>`;
  document.body.appendChild(hud);
  const get = s => hud.querySelector(s);
  const els = {
    hp: get('.hp'), fill: get('.hp span'), health: get('.hp-number'), pips: [...hud.querySelectorAll('.spark b')],
    title: get('.quest h2'), chapter: get('.chapter-label'), objective: get('.objective'), stepsList: get('.quest-steps'), tip: get('.quest-tip'),
    count: get('.count'), distance: get('.distance'), cash: get('.cash'),
    steps: [...hud.querySelectorAll('.journey i')], actions: [...hud.querySelectorAll('.actions button')],
    target: get('.target'), targetName: get('.target strong'), targetHp: get('.target span'), targetFill: get('.target-hp i'), tell: get('.target p'),
    wp: get('.waypoint'), wpArrow: get('.wp-arrow'), wpLabel: get('.wp-label'), wpDist: get('.wp-dist'),
    dayClock: get('.day-clock'), echoCount: get('.echo-count'),
    commsImg: get('.comms-portrait img'), commsSpeaker: get('.comms-speaker'), commsRole: get('.comms-role'), commsFreq: get('.comms-freq'),
    commsBars: [...hud.querySelectorAll('.comms-bars i')],
  };
  els.commsImg?.addEventListener('error', () => {
    if (!els.commsImg.src.endsWith('/characters/portrait.png')) {
      els.commsImg.src = '/characters/portrait.png';
    }
  });
  const mobile = matchMedia('(pointer: coarse)').matches;
  const touchControls = document.getElementById('touch-controls');
  const powersButton = document.getElementById('touch-powers');
  const dashButton = document.getElementById('touch-dash');
  const powerOptions = get('.actions');
  function showPowers(open) {
    if (!mobile) return;
    powerOptions.hidden = !open;
    powersButton.setAttribute('aria-expanded', String(open));
    if (!open && powerOptions.contains(document.activeElement)) powersButton.focus({ preventScroll: true });
  }
  if (mobile) {
    showPowers(false);
    powersButton.addEventListener('click', () => { if (!window.__paused) showPowers(powerOptions.hidden); });
    document.addEventListener('pointerdown', e => {
      if (!powerOptions.contains(e.target) && !powersButton.contains(e.target)) showPowers(false);
    });
    addEventListener('keydown', e => { if (e.code === 'Escape') showPowers(false); });
    addEventListener('resize', () => showPowers(false));
    addEventListener('blur', () => showPowers(false));
    document.addEventListener('visibilitychange', () => { if (document.hidden) showPowers(false); });
  }
  hud.querySelectorAll('[data-code]').forEach(button => button.addEventListener('click', () => {
    if (!window.__paused) window.lab?.input.pressed.add(button.dataset.code);
    showPowers(false);
    button.blur();
  }));
  let chapter = 0, bannerTime = 4.5, toastTime = 0, mapTime = 0, lastHp = 100;
  const banner = get('.chapter-banner');
  function showChapter(c) {
    chapter = c.index;
    const phase = window.lab?.dayNight?.phase ?? (c.style === 'field' ? 'day' : 'night');
    banner.querySelector('span').textContent = `Sector ${pad(chapter + 1)} // ${phase === 'night' ? 'Night ops' : 'Day ops'}`;
    banner.querySelector('strong').textContent = c.name;
    banner.querySelector('p').textContent = c.subtitle;
    bannerTime = 4.5;
  }
  if (levels) showChapter({ index: 0, ...CHAPTERS[0] });
  else banner.hidden = true;
  addEventListener('chapterchange', e => showChapter(e.detail));
  const map = get('.minimap').getContext('2d');
  function drawMap(player, enemies, next, missionMarkers = []) {
    const scale = 1.8, px = player.feet.x, pz = player.feet.z;
    const point = (x, z) => [144 + (x - px) * scale, 144 + (z - pz) * scale];
    map.clearRect(0, 0, 288, 288);
    const neon = getComputedStyle(document.documentElement).getPropertyValue('--chapter-accent').trim() || '#00f0ff';
    map.fillStyle = '#070b10'; map.fillRect(0, 0, 288, 288);
    // a world-fixed grid, so the ground is seen to move under him
    map.strokeStyle = '#16232c'; map.lineWidth = 1; map.beginPath();
    for (let g = -8; g <= 8; g++) {
      const [gx] = point(Math.round(px / 10) * 10 + g * 10, 0), [, gz] = point(0, Math.round(pz / 10) * 10 + g * 10);
      map.moveTo(gx, 0); map.lineTo(gx, 288); map.moveTo(0, gz); map.lineTo(288, gz);
    }
    map.stroke();
    if (levels) {
      map.strokeStyle = '#1a3344'; map.lineWidth = 4; map.beginPath();
      for (const x of AVENUES) { const [a, ya] = point(x, GRID_MIN), [b, yb] = point(x, GRID_MAX); map.moveTo(a, ya); map.lineTo(b, yb); }
      for (const z of STREETS) { const [a, ya] = point(GRID_MIN, z), [b, yb] = point(GRID_MAX, z); map.moveTo(a, ya); map.lineTo(b, yb); }
      map.stroke();
    }
    map.strokeStyle = neon + '2e'; map.lineWidth = 12; map.lineCap = 'round';
    map.beginPath();
    (route ?? marks.map(m => [m.x, m.z])).forEach(([x, z], i) => { const p = point(x, z); i ? map.lineTo(...p) : map.moveTo(...p); });
    map.stroke(); map.strokeStyle = neon; map.lineWidth = 3; map.stroke();
    for (const m of marks) {
      const [x, z] = point(m.x, m.z);
      map.strokeStyle = map.fillStyle = '#42d9ef'; map.lineWidth = 2;
      m.used ? map.strokeRect(x - 4, z - 4, 8, 8) : map.fillRect(x - 4, z - 4, 8, 8);
    }
    for (const m of missionMarkers) {
      const [mx, mz] = point(m.x, m.z), dx = mx - 144, dy = mz - 144, dist = Math.hypot(dx, dy);
      map.fillStyle = map.strokeStyle = m.color || '#ff2a6d';
      if (dist < 130) {
        map.beginPath();
        if (m.kind === 'kiosk' || m.kind === 'contract') {
          map.moveTo(mx, mz - 6); map.lineTo(mx + 6, mz); map.lineTo(mx, mz + 6); map.lineTo(mx - 6, mz); map.closePath();
          map.fill();
        } else {
          map.arc(mx, mz, 6.5, 0, Math.PI * 2); map.lineWidth = 2.5; map.stroke();
          map.beginPath(); map.arc(mx, mz, 3, 0, Math.PI * 2); map.fill();
        }
      } else if (m.kind !== 'kiosk') {
        const s = 126 / dist;
        map.beginPath(); map.arc(144 + dx * s, 144 + dy * s, 5, 0, Math.PI * 2); map.fill();
      }
    }
    for (const e of enemies) {
      if (e.dead || e.gone || e.duel && !e.ready || Math.hypot(e.x - px, e.z - pz) > 42) continue;
      if (e.civ && !(e.angry > 0)) { // the street's own: a small pale dot until they turn
        const [x, z] = point(e.x, e.z);
        map.fillStyle = e.mark ? '#ff2a6d' : '#8aa0ab';
        map.fillRect(x - (e.mark ? 4 : 2.5), z - (e.mark ? 4 : 2.5), e.mark ? 8 : 5, e.mark ? 8 : 5);
        continue;
      }
      const [x, z] = point(e.x, e.z);
      const r = e.kind === 'brute' ? 7 : 4.5;
      map.beginPath(); map.moveTo(x, z - r); map.lineTo(x + r, z); map.lineTo(x, z + r); map.lineTo(x - r, z); map.closePath();
      map.fillStyle = e.windup > 0 ? '#fcee0a' : '#ff2a6d'; map.fill();
    }
    map.save(); map.translate(144, 144); map.rotate(-player.yaw);
    map.beginPath(); map.moveTo(0, 10); map.lineTo(-7, -6); map.lineTo(0, -2); map.lineTo(7, -6); map.closePath();
    map.fillStyle = '#ffffff'; map.fill(); map.restore();
    if (!next) return;
    const [nx, nz] = point(next.x, next.z), dx = nx - 144, dy = nz - 144, dist = Math.hypot(dx, dy);
    map.fillStyle = next.color || '#fcee0a';
    if (dist < 126) {
      map.beginPath(); map.arc(nx, nz, 7, 0, Math.PI * 2); map.fill();
    } else {
      const s = 118 / dist;
      map.save(); map.translate(144 + dx * s, 144 + dy * s); map.rotate(Math.atan2(dx, dy));
      map.beginPath(); map.moveTo(0, 8); map.lineTo(-5, -6); map.lineTo(5, -6); map.closePath(); map.fill();
      map.restore();
    }
  }
  const comboEl = get('.combo'), flashEl = get('.flash-text');
  /** The chain counter: shown from two hits, popped on each new one. */
  function combo(n) {
    comboEl.classList.toggle('visible', n >= 2);
    if (n < 2) return;
    comboEl.querySelector('b').textContent = n;
    comboEl.classList.remove('pop'); void comboEl.offsetWidth; comboEl.classList.add('pop');
  }
  function flashText(text) {
    flashEl.textContent = text;
    flashEl.classList.remove('show'); void flashEl.offsetWidth; flashEl.classList.add('show');
  }
  function toast(message) { get('.toast').textContent = message; toastTime = 3.5; get('.toast').classList.add('visible'); }
  const storyEl = get('.story');
  let storyTimer = 0;
  function comms(speakerKey, text) {
    const portraitKey = typeof speakerKey === 'string' && /^[a-z0-9_-]+$/i.test(speakerKey) ? speakerKey.toLowerCase() : 'sora';
    const sp = SPEAKERS[portraitKey] ?? SPEAKERS.sora;
    const parts = sp.name.split(' // ');
    const meta = SPEAKER_META[portraitKey] ?? { callsign: parts[0], role: parts[1] || 'Ghost-Line', freq: 'CH-07 // GHOST-LINK' };
    els.commsSpeaker.textContent = meta.callsign;
    if (els.commsRole) els.commsRole.textContent = meta.role;
    if (els.commsFreq) els.commsFreq.textContent = meta.freq;
    if (els.commsImg && els.commsImg.dataset.speaker !== portraitKey) {
      els.commsImg.dataset.speaker = portraitKey;
      els.commsImg.src = `/characters/${portraitKey}.svg`;
    }
    storyEl.style.setProperty('--comms-color', sp.color);
    storyEl.querySelector('p').textContent = text;
    storyEl.hidden = false;
    storyTimer = Math.max(6.2, Math.min(10, text.length * 0.068));
  }
  function story(text) { comms('sora', text); }
  const hideStory = () => { storyEl.hidden = true; storyTimer = 0; audio?.stopVoice?.(); };
  storyEl.querySelector('button').addEventListener('click', hideStory);
  addEventListener('keydown', (e) => { if (!storyEl.hidden && (e.code === 'Enter' || e.code === 'KeyK')) { e.preventDefault(); hideStory(); } });
  const rideEl = get('.ride'), promptEl = get('.ride-prompt'), heatEl = get('.heat');
  /** The vehicle he is in (speed, state), or the one in reach (a prompt), or null. */
  function ride(state) {
    rideEl.hidden = !state?.driving; promptEl.hidden = !state;
    const jumpBtn = document.getElementById('touch-jump');
    if (jumpBtn) {
      const label = state?.driving ? 'Brake' : 'Jump';
      jumpBtn.querySelector('span').textContent = label;
      jumpBtn.setAttribute('aria-label', label);
      jumpBtn.querySelector('use').setAttribute('href', state?.driving ? '#i-brake' : '#i-jump');
    }
    touchControls.dataset.driving = String(!!state?.driving);
    if (state?.driving) showPowers(false);
    hud.classList.toggle('driving', !!state?.driving);
    if (!state) return;
    promptEl.querySelector('span').textContent = state.driving ? 'Step out' : state.tipped ? `Right the ${state.name}` : state.driver ? `Carjack the ${state.name}` : `Take the ${state.name}`;
    if (mobile) promptEl.querySelector('span').textContent = state.driving ? 'Exit vehicle' : `${state.tipped ? 'Flip' : state.driver ? 'Take' : /bike|scooter/i.test(state.name) ? 'Ride' : 'Drive'} · ${state.name}`;
    if (!state.driving) return;
    rideEl.querySelector('b').textContent = Math.round(state.kmh);
    rideEl.querySelector('em').textContent = state.name;
    rideEl.querySelector('i').textContent = state.tipped ? (mobile ? 'Overturned · exit, then flip it' : 'Overturned · G out, then G beside it to right') : state.hurt === 'dead' ? 'Dead' : state.hurt === 'fire' ? 'On fire' : state.hurt === 'smoke' ? 'Smoking' : state.air ? 'Airborne' : state.sliding ? 'Sliding' : mobile ? 'Stick to steer · hold Brake' : 'G · step out';
    rideEl.classList.toggle('loose', !!(state.air || state.sliding));
  }
  const wpLocal = new THREE.Vector3(), wpNdc = new THREE.Vector3();
  let lastStepsKey = '', lastTip = '', lastObj = '', lastWpLabel = '', lastWpDist = '', lastClock = '', lastEchoes = '';
  function update(dt, { hp, power, cleared, won, enemies, player, has, dashCool, flareCool, invuln, boss, heat = 0, cash = 0, stars = 0, task = '', mission = null, dayNight = null }) {
    const dn = dayNight ?? window.lab?.dayNight;
    if (dn && els.dayClock) {
      const label = `${dn.phase === 'night' ? '☾ Night' : '☀ Day'} · ${dn.mmss}`;
      if (label !== lastClock) {
        lastClock = label;
        els.dayClock.textContent = label;
        els.dayClock.dataset.phase = dn.phase;
      }
    }
    if (els.echoCount) {
      const ec = mission?.echoes;
      if (ec && ec.total > 0) {
        els.echoCount.hidden = false;
        const echoLabel = `◈ ${ec.collected}/${ec.total}`;
        if (echoLabel !== lastEchoes) {
          lastEchoes = echoLabel;
          els.echoCount.textContent = `${echoLabel} Echoes`;
          els.echoCount.classList.toggle('complete', ec.collected >= ec.total);
        }
      } else {
        els.echoCount.hidden = true;
      }
    }
    if (!storyEl.hidden) {
      storyTimer = Math.max(0, storyTimer - dt);
      const lvl = audio?.voiceLevel ?? 0;
      const t = performance.now() * 0.015;
      els.commsBars.forEach((b, i) => {
        const wave = 0.38 + 0.62 * Math.abs(Math.sin(t + i * 1.15) * Math.cos(t * 0.61 - i * 0.65));
        const amp = lvl > 0.02 ? Math.min(1, 0.28 + lvl * 1.12) : (storyTimer > 0.5 ? 0.56 : 0.22);
        b.style.transform = `scaleY(${Math.max(0.18, Math.min(1, amp * wave)).toFixed(2)})`;
      });
      if (storyTimer <= 0 && !audio?.voiceActive) storyEl.hidden = true;
    }
    heatEl.hidden = !(heat > 0);
    if (heat > 0) {
      heatEl.querySelector('strong').textContent = 'Heat';
      heatEl.querySelector('span').textContent = '●'.repeat(Math.max(1, stars));
    }
    els.cash.textContent = cash ? `$${cash}` : '';
    bannerTime = Math.max(0, bannerTime - dt);
    banner.classList.toggle('visible', bannerTime > 0 && !boss);
    hud.classList.toggle('in-duel', !!boss);
    toastTime = Math.max(0, toastTime - dt);
    get('.toast').classList.toggle('visible', toastTime > 0);
    hud.classList.toggle('hurt', hp < lastHp);
    hud.classList.toggle('critical', hp > 0 && hp <= 30);
    if (hp < lastHp) {
      const feedback = get('.hit-feedback');
      feedback.classList.remove('flash'); void feedback.offsetWidth; feedback.classList.add('flash');
    }
    lastHp = hp;
    els.fill.style.transform = `scaleX(${Math.max(0, hp) / 100})`;
    els.hp.setAttribute('aria-valuenow', Math.max(0, Math.round(hp)));
    els.health.textContent = `${Math.max(0, Math.ceil(hp))} / 100`;
    els.fill.style.opacity = invuln > 0 ? '0.65' : '1';
    els.pips.forEach((el, i) => { el.style.transform = `scaleX(${Math.max(0, Math.min(1, power - i))})`; });
    const index = Math.min(chapter, (levels?.length ?? 1) - 1);
    const alive = enemies.filter(e => !e.dead && !e.duel && !e.civ && (!levels || roam || e.seg === index));
    const total = levels && !roam ? levels[index].spots.length : enemies.length;
    const next = mission?.waypoint ?? (marks.find(m => !m.used) ?? marks[marks.length - 1]);
    els.chapter.textContent = !levels ? 'Side op · Snowbound' : roam ? 'Free roam · all systems' : won ? 'Run complete' : `Sector ${pad(chapter + 1)} / ${pad(levels.length)}`;
    els.title.textContent = levels ? CHAPTERS[chapter].name : 'Under the aurora';
    const waitingRival = levels && enemies.find(e => e.duel && e.seg === index && !e.dead);
    const objText = task || (won ? 'The sanctuary is secure. Stand down.' : boss ? 'Read your rival. Punish the opening.' : cleared >= (levels?.length ?? Infinity) ? 'Enter Amber Sanctuary ahead.' : roam ? 'Roam freely. Green diamonds start $150 contracts.' : levels && cleared > chapter ? 'Sector clear. Head through the open gate.' : waitingRival && alive.length === 0 ? 'Meet Ren’s Specter at the crimson beacon.' : levels ? 'Complete the field mission, then defeat Ren.' : 'Follow the glowing beacons to the cabin.');
    if (objText !== lastObj) { lastObj = objText; els.objective.textContent = objText; }
    if (mission?.steps) {
      const key = mission.steps.map(s => (s.done ? '1' : s.active ? '2' : '0') + s.text).join('|');
      if (key !== lastStepsKey) {
        lastStepsKey = key;
        els.stepsList.innerHTML = mission.steps.map(s => `<li class="${s.done ? 'done' : s.active ? 'active' : ''}">${s.text}</li>`).join('');
      }
      els.stepsList.hidden = false;
      const tipText = mission.tip || '';
      if (tipText !== lastTip) { lastTip = tipText; els.tip.textContent = tipText; }
      els.tip.hidden = !tipText;
    } else {
      els.stepsList.hidden = true;
      els.tip.hidden = true;
    }
    els.count.textContent = won ? 'All sectors clear' : levels && !roam ? `${pad(total - alive.length)} / ${pad(total)} hostiles down` : `${pad(alive.length)} hostiles active`;
    els.distance.textContent = next ? `${Math.round(Math.hypot(player.feet.x - next.x, player.feet.z - next.z))} m · ${mission?.waypoint ? 'target' : levels ? 'relay' : 'beacon'}` : '';
    if (levels) get('.map-label').textContent = `Scan · ${ZONES[zoneAt(player.feet.x, player.feet.z)].name}`;
    els.steps.forEach((el, i) => { el.className = i < cleared ? 'done' : i === cleared ? 'active' : ''; });

    // 3D World-Space Floating Waypoint Diamond
    const wp = mission?.waypoint;
    if (wp && camera && !boss && !document.documentElement.hasAttribute('data-clean')) {
      const dist = Math.round(Math.hypot(wp.x - player.feet.x, wp.z - player.feet.z));
      wpLocal.set(wp.x, (wp.y ?? player.feet.y) + 2.35, wp.z).applyMatrix4(camera.matrixWorldInverse);
      wpNdc.set(wp.x, (wp.y ?? player.feet.y) + 2.35, wp.z).project(camera);
      let sx, sy;
      if (wpLocal.z < -0.15) {
        sx = (wpNdc.x + 1) * 0.5 * innerWidth;
        sy = (1 - wpNdc.y) * 0.5 * innerHeight;
      } else {
        const len = Math.hypot(wpLocal.x, wpLocal.y) || 1;
        sx = innerWidth * 0.5 + (wpLocal.x / len) * innerWidth * 0.6;
        sy = innerHeight * 0.5 + (-wpLocal.y / len) * innerHeight * 0.6;
      }
      const padX = 62, padTop = 74, padBot = 96;
      const cx = Math.max(padX, Math.min(innerWidth - padX, sx));
      const cy = Math.max(padTop, Math.min(innerHeight - padBot, sy));
      const clamped = wpLocal.z >= -0.15 || sx < padX || sx > innerWidth - padX || sy < padTop || sy > innerHeight - padBot;
      els.wp.hidden = false;
      els.wp.classList.toggle('clamped', clamped);
      els.wp.style.setProperty('--wp-color', wp.color || '#00f0ff');
      els.wp.style.transform = `translate(-50%, -50%) translate(${cx.toFixed(1)}px, ${cy.toFixed(1)}px)`;
      if (clamped) {
        const ang = Math.atan2(cx - innerWidth * 0.5, -(cy - innerHeight * 0.5)) * 180 / Math.PI;
        els.wpArrow.style.transform = `rotate(${ang.toFixed(1)}deg)`;
      }
      if (wp.label !== lastWpLabel) { lastWpLabel = wp.label; els.wpLabel.textContent = wp.label; }
      const dTxt = `${dist} m`;
      if (dTxt !== lastWpDist) { lastWpDist = dTxt; els.wpDist.textContent = dTxt; }
    } else {
      els.wp.hidden = true;
    }

    for (const el of els.actions) {
      const key = el.dataset.action, unlocked = key === 'attack' || has(key);
      const cool = key === 'dash' ? dashCool : key === 'flare' ? flareCool : 0;
      const empty = (key === 'spark' || key === 'whirl') && power < 1 || key === 'flare' && power < 2;
      el.classList.toggle('locked', !unlocked);
      el.classList.toggle('cooling', unlocked && (cool > 0 || empty));
      el.setAttribute('aria-disabled', String(!unlocked || cool > 0 || empty));
      if (mobile) {
        el.hidden = key === 'attack' || key === 'dash' || !unlocked;
        el.disabled = !unlocked || cool > 0 || empty;
      }
      el.style.setProperty('--cd', unlocked && cool > 0 ? Math.min(1, cool / COOL[key]) : 0);
      el.querySelector('small').textContent = !unlocked ? `Sec ${pad(POWER_ORDER.indexOf(key) + 1)}` : cool > 0 ? `${Math.ceil(cool)}s` : empty ? 'No charge' : mobile ? 'Ready' : '';
    }
    if (mobile) {
      dashButton.hidden = !has('dash');
      dashButton.disabled = dashCool > 0;
      dashButton.querySelector('small').textContent = dashCool > 0 ? `${Math.ceil(dashCool)}s` : '';
      powersButton.hidden = !els.actions.some(el => !el.hidden);
      if (powersButton.hidden) showPowers(false);
    }
    // a calm passer-by reads as a few metres further off, so a real threat beside them takes the panel
    const reach = e => Math.hypot(e.x - player.feet.x, e.z - player.feet.z) + (e.civ && !(e.angry > 0) ? 5 : 0);
    const target = enemies.filter(e => !e.dead && !e.gone).sort((a, b) => reach(a) - reach(b))[0];
    els.target.hidden = !!boss || !target || target.duel || Math.hypot(target.x - player.feet.x, target.z - player.feet.z) > 10;
    if (!els.target.hidden) {
      const [name, tier] = FOES[target.kind];
      els.targetName.textContent = name;
      els.targetHp.textContent = target.civ ? (target.angry > 0 ? 'Hostile' : target.mark ? 'Mission Target' : 'Bystander') : `T${tier} · ${Math.ceil(target.hp)} HP`;
      els.targetFill.style.transform = `scaleX(${target.hp / target.maxHp})`;
      els.tell.textContent = target.mark ? 'Mission target — defeat to recover the cipher key!' : target.kind === 'thief' ? 'Thief or jogger? A thief carries loot at the hip.' : target.civ && !(target.angry > 0) ? 'Not your fight. Hurting them costs you.' : target.windup > 0 ? (target.shot ? 'Laser lock — sidestep, dash or cut the shot back' : 'Incoming — dodge, or strike now to parry') : target.air > 0 ? 'Airborne — keep the chain going' : target.stagger > 0 ? 'Staggered — strike now' : target.shot ? 'Close the gap. He backs off.' : 'Keep moving. Watch for the windup.';
      els.target.classList.toggle('warning', target.windup > 0);
    }
    const bar = get('.bossbar'); bar.hidden = !boss;
    if (boss) {
      bar.querySelector('strong').textContent = boss.name;
      bar.querySelector('b').textContent = `${Math.max(0, Math.ceil(boss.hp))} / ${boss.maxHp}`;
      bar.querySelector('.boss-health i').style.transform = `scaleX(${Math.max(0, boss.hp / boss.maxHp)})`;
      bar.querySelector('.boss-health').setAttribute('aria-valuemax', boss.maxHp);
      bar.querySelector('.boss-health').setAttribute('aria-valuenow', Math.max(0, Math.ceil(boss.hp)));
      const open = boss.phase === 'recovery';
      bar.classList.toggle('opening', open);
      const keys = ['J', has('spark') ? 'Q' : null, has('heavy') ? 'F' : null].filter(Boolean).join(' / ');
      bar.querySelector('.boss-cue').textContent = open ? `Opening — strike now!  ${keys}` : boss.phase === 'windup' || boss.phase === 'active' ? MOVES[boss.move].cue : boss.phase === 'intro' ? 'A familiar face. A new lesson.' : 'Watch the blade. Keep room to move.';
      bar.querySelector('.boss-lesson').textContent = boss.lesson;
      [...bar.querySelectorAll('.lesson-steps span')].forEach((el, i) => el.classList.toggle('done', i === 0 ? boss.phase !== 'intro' : i === 1 ? boss.evaded : boss.punishes > 0));
    }
    if ((mapTime -= dt) <= 0) { drawMap(player, enemies, next, mission?.markers ?? []); mapTime = 0.1; }
  }
  return { update, toast, story, comms, combo, flashText, ride, element: hud };
}
