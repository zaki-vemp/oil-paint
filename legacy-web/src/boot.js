// Starts the selected world after a mobile screen choice and owns the pause menu. Chapter progression chooses the HUD style;
// traveling or changing game modes reloads the world so its saves and mechanics stay consistent.
import { SCENE, showScene } from './scenes.js';
import { qualityMode } from './quality.js';
import { getAudio, SPEAKERS } from './game/audio.js';
import { STORY_ACTS, CHARACTER_DOSSIERS, ECHO_SHARDS, loadEchoIds } from './game/story.js';
import { CHAPTERS } from './game/chapters.js';
import { loadProgress } from './game/levels.js';

const q = new URLSearchParams(location.search);
const root = document.documentElement;
root.dataset.scene = SCENE;
if (SCENE === 'aurora') { root.dataset.style = 'spirit'; root.style.setProperty('--chapter-accent', '#5ef2e0'); }
const clean = q.has('capture') || q.has('clean');
if (clean) root.dataset.clean = '';
const audio = getAudio({ scene: SCENE, silent: q.has('capture') });
const menu = document.getElementById('menu');
const screenChoice = document.getElementById('screen-choice');
let refreshMenuEnhancements = () => {};
function pause(on) {
  if (screenChoice.open) return;
  window.__paused = on;
  window.lab?.input.reset();
  if (on) {
    document.exitPointerLock?.();
    refreshMenuEnhancements();
    if (!menu.open) menu.showModal();
  } else if (menu.open) menu.close();
}
const resume = () => {
  audio.unlock();
  pause(false);
  if (matchMedia('(pointer: fine)').matches) window.lab?.renderer.domElement.requestPointerLock?.()?.catch?.(() => {});
};
document.getElementById('pause-button').addEventListener('click', () => { audio.play('uiClick'); pause(true); });
document.getElementById('resume').addEventListener('click', resume);

const muteBtn = document.getElementById('mute-button');
const menuMuteBtn = document.getElementById('audio-mute');
const sliders = [
  ['master', 'vol-master', 'out-master'],
  ['music', 'vol-music', 'out-music'],
  ['voice', 'vol-voice', 'out-voice'],
  ['sfx', 'vol-sfx', 'out-sfx'],
];
function syncAudioUI(s) {
  if (muteBtn) {
    muteBtn.setAttribute('aria-pressed', String(s.muted));
    muteBtn.classList.toggle('muted', s.muted);
    muteBtn.querySelector('use')?.setAttribute('href', s.muted ? '#i-mute' : '#i-sound');
    const span = muteBtn.querySelector('span');
    if (span) span.textContent = s.muted ? 'Muted' : 'Audio';
  }
  if (menuMuteBtn) {
    menuMuteBtn.textContent = s.muted ? 'Muted (click to unmute)' : 'Sound on';
    menuMuteBtn.classList.toggle('on', !s.muted);
    menuMuteBtn.setAttribute('aria-pressed', String(!s.muted));
  }
  for (const [key, inputId, outId] of sliders) {
    const pct = Math.round((s[key] ?? 0) * 100);
    const input = document.getElementById(inputId);
    const out = document.getElementById(outId);
    if (input) input.value = String(pct);
    if (out) out.textContent = `${pct}%`;
  }
}
syncAudioUI(audio.getSettings());
audio.onChange(syncAudioUI);
muteBtn?.addEventListener('click', () => { const s = audio.toggleMute(); if (!s.muted) audio.play('uiClick'); muteBtn.blur(); });
menuMuteBtn?.addEventListener('click', () => { const s = audio.toggleMute(); if (!s.muted) audio.play('uiClick'); });
for (const [key, inputId] of sliders) {
  const input = document.getElementById(inputId);
  input?.addEventListener('input', () => {
    audio.unlock();
    audio.setSettings({ [key]: Number(input.value) / 100, ...(Number(input.value) > 0 && key === 'master' ? { muted: false } : {}) });
  });
  input?.addEventListener('change', () => {
    if (key === 'sfx') audio.play('missionStep');
    else if (key === 'voice') audio.speak('Audio check, Courier Zero-Seven.', 'sora');
  });
}
const coarse = matchMedia('(pointer: coarse)').matches;
const fill = document.getElementById('fill');
const fullscreen = () => document.fullscreenElement || document.webkitFullscreenElement;
const fullscreenMessage = 'Full screen is unavailable in this browser. Choose Normal screen to play.';
function screenStatus(id, message = '') {
  const status = document.getElementById(id);
  status.textContent = message;
  status.hidden = !message;
}
// Keep the native request inside the button click: mobile browsers require a fresh user gesture.
async function fillScreen() {
  if (!fullscreen()) {
    const request = root.requestFullscreen || root.webkitRequestFullscreen;
    if (!request) throw new Error('Fullscreen unavailable');
    await request.call(root, { navigationUI: 'hide' });
  }
  try { await screen.orientation?.lock?.('landscape'); } catch { /* Rotate by hand when orientation locking is unsupported. */ }
}
function screenChanged() {
  fill.textContent = fullscreen() ? 'Normal screen' : 'Full screen';
  if (!fullscreen()) screen.orientation?.unlock?.();
  dispatchEvent(new Event('resize'));
}
document.addEventListener('fullscreenchange', screenChanged);
document.addEventListener('webkitfullscreenchange', screenChanged);
screenChanged();
fill.addEventListener('click', async () => {
  screenStatus('fill-status');
  fill.disabled = true;
  try {
    if (fullscreen()) {
      const exit = document.exitFullscreen || document.webkitExitFullscreen;
      await exit.call(document);
    } else await fillScreen();
  } catch {
    screenStatus('fill-status', fullscreen() ? 'Could not leave full screen. Use your browser’s exit control.' : 'Full screen is unavailable. You can keep playing in your browser.');
  } finally { fill.disabled = false; }
});
let screenReady = Promise.resolve();
if (coarse && !clean) {
  window.__paused = true;
  screenReady = new Promise(resolve => {
    const full = document.getElementById('start-fullscreen');
    const normal = document.getElementById('start-normal');
    const start = () => {
      audio.unlock();
      screenChoice.close();
      window.__paused = false;
      resolve();
    };
    normal.addEventListener('click', start);
    full.addEventListener('click', async () => {
      screenStatus('screen-status');
      full.disabled = normal.disabled = true;
      try {
        await fillScreen();
        start();
      } catch { screenStatus('screen-status', fullscreenMessage); }
      finally { full.disabled = normal.disabled = false; }
    });
    screenChoice.addEventListener('cancel', e => { e.preventDefault(); if (!normal.disabled) start(); });
    screenChoice.showModal();
  });
  document.addEventListener('touchmove', e => { if (!menu.open && !screenChoice.open) e.preventDefault(); }, { passive: false });
  window.visualViewport?.addEventListener('resize', () => dispatchEvent(new Event('resize')));
}
menu.addEventListener('cancel', e => { e.preventDefault(); pause(false); });
addEventListener('keydown', e => {
  if (clean || !window.lab?.ready || e.repeat) return;
  if (e.code === 'Escape' && window.lab?.showcase?.active && !menu.open) {
    e.preventDefault();
    window.lab.showcase.stop();
    return;
  }
  if (e.code === 'Escape' || ['KeyP', 'KeyH'].includes(e.code) && document.activeElement?.tagName !== 'INPUT') {
    e.preventDefault();
    pause(!window.__paused);
  }
});
let locked = false;
document.addEventListener('pointerlockchange', () => {
  const now = !!document.pointerLockElement;
  if (locked && !now && !clean && !window.lab?.showcase?.active) pause(true);
  locked = now;
});
document.addEventListener('visibilitychange', () => { if (document.hidden && window.lab?.ready && !clean) pause(true); });
for (const button of document.querySelectorAll('#scenes button')) {
  button.classList.toggle('on', button.dataset.scene === SCENE);
  button.setAttribute('aria-pressed', String(button.dataset.scene === SCENE));
  button.addEventListener('click', () => button.dataset.scene === SCENE ? resume() : showScene(button.dataset.scene));
}
for (const button of document.querySelectorAll('#mode button')) {
  const selected = (['roam', 'online'].includes(q.get('mode')) ? q.get('mode') : 'level') === button.dataset.mode;
  button.classList.toggle('on', selected);
  button.setAttribute('aria-pressed', String(selected));
  button.addEventListener('click', () => {
    if (selected) return resume();
    const next = new URLSearchParams(location.search);
    next.set('mode', button.dataset.mode);
    location.search = next.toString();
  });
}
// Snowbound has no free roam: it is played alone or online.
if (SCENE !== 'autumn') {
  document.querySelector('#mode [data-mode="roam"]').hidden = true;
  document.querySelector('#mode [data-mode="level"]').textContent = 'Solo';
  document.querySelector('#mode [data-mode="online"]').textContent = 'Online';
  const dnBtn = document.getElementById('daynight-toggle');
  if (dnBtn) dnBtn.hidden = true;
}

// --- Investor Showcase Trigger, Day/Night Shift & Interactive Lore Codex ---
{
  const esc = (s) => String(s ?? '').replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
  const pad2 = (n) => String(n).padStart(2, '0');
  const tourBtn = document.getElementById('tour-button');
  const menuTourBtn = document.getElementById('menu-tour-button');
  const dayNightBtn = document.getElementById('daynight-toggle');
  const codexToggle = document.getElementById('codex-toggle');
  const codexBody = document.getElementById('codex-body');
  const codexBadge = document.getElementById('codex-echo-badge');
  const codexTabEchoes = document.getElementById('codex-tab-echoes');
  const actsPane = document.getElementById('codex-acts');
  const castPane = document.getElementById('codex-cast');
  const echoesPane = document.getElementById('codex-echoes');

  function handleTourClick(e) {
    e?.stopPropagation?.();
    audio.unlock();
    audio.play('uiClick');
    if (SCENE !== 'autumn') {
      const next = new URLSearchParams(location.search);
      next.set('scene', 'autumn');
      next.set('tour', '1');
      location.search = next.toString();
      return;
    }
    if (window.lab?.showcase) {
      if (menu.open) pause(false);
      window.lab.showcase.toggle();
    }
    e?.currentTarget?.blur?.();
  }
  tourBtn?.addEventListener('click', handleTourClick);
  menuTourBtn?.addEventListener('click', handleTourClick);

  function syncDayNightBtn() {
    if (!dayNightBtn || dayNightBtn.hidden) return;
    const phase = window.lab?.dayNight?.phase ?? (q.get('phase') === 'night' ? 'night' : 'day');
    dayNightBtn.textContent = phase === 'night' ? '☀ Shift to Daylight (Night)' : '☾ Shift to Midnight (Day)';
    dayNightBtn.setAttribute('aria-label', 'Shift day and night cycle');
  }
  dayNightBtn?.addEventListener('click', () => {
    audio.unlock();
    audio.play('uiClick');
    const dn = window.lab?.dayNight;
    if (!dn) return;
    const nextPhase = dn.phase === 'night' ? 'day' : 'night';
    dn.setPhase(nextPhase, window.lab?.shared?.uTime?.value ?? 0);
    window.lab?.syncDayNight?.(10);
    syncDayNightBtn();
  });

  function readUnlockedEchoIds() {
    return new Set(window.lab?.missions?.echoes.unlockedIds ?? loadEchoIds());
  }

  if (castPane) {
    castPane.innerHTML = CHARACTER_DOSSIERS.map((c) => `
      <article class="codex-card dossier-card" style="--card-accent:${esc(c.color)}">
        <div class="dossier-portrait"><img src="/characters/${esc(c.id)}.svg" alt="${esc(c.name)}" loading="lazy" /></div>
        <div>
          <div class="codex-card-head">
            <span class="codex-kicker">${esc(c.affiliation)}</span>
          </div>
          <h4>${esc(c.name)}</h4>
          <p class="dossier-role">${esc(c.role)}</p>
          <p class="codex-quote">${esc(c.quote)}</p>
          <p class="codex-copy">${esc(c.bio)}</p>
        </div>
      </article>`).join('');
    castPane.querySelectorAll('.dossier-portrait img').forEach((img) => {
      img.addEventListener('error', () => {
        if (!img.src.endsWith('/characters/portrait.png')) img.src = '/characters/portrait.png';
      });
    });
  }

  function renderCodexDynamic() {
    const unlocked = readUnlockedEchoIds();
    const total = ECHO_SHARDS.length;
    if (codexBadge) codexBadge.textContent = `◈ ${unlocked.size} / ${total}`;
    if (codexTabEchoes) codexTabEchoes.textContent = `Echoes (${unlocked.size}/${total})`;

    if (actsPane) {
      const roamMode = SCENE === 'autumn' && ['roam', 'online'].includes(q.get('mode'));
      const live = SCENE === 'autumn' ? window.lab?.hunt : null;
      const cleared = live?.seg ?? loadProgress()?.cleared ?? 0;
      actsPane.innerHTML = STORY_ACTS.map((act) => {
        const accent = CHAPTERS[act.seg]?.color ?? '#00f0ff';
        const state = roamMode ? 'done' : act.seg < cleared ? 'done' : act.seg === cleared ? 'active' : '';
        const stateLabel = roamMode ? 'Open District' : act.seg < cleared ? 'Liberated' : act.seg === cleared ? 'Active Op' : 'Quarantined';
        return `
          <article class="codex-card" style="--card-accent:${esc(accent)}">
            <div class="codex-card-head">
              <span class="codex-kicker">Sector ${pad2(act.act)} // ${esc(act.district)}</span>
              <span class="codex-status ${state}">${stateLabel}</span>
            </div>
            <h4>${esc(act.title)}</h4>
            <p class="codex-quote">${esc(act.epigraph)}</p>
            <p class="codex-copy">${esc(act.synopsis)}</p>
            <p class="codex-theme">◈ ${esc(act.memoryTheme)}</p>
          </article>`;
      }).join('');
    }

    if (echoesPane) {
      const pips = ECHO_SHARDS.map((s) => `<i class="${unlocked.has(s.id) ? 'on' : ''}"></i>`).join('');
      const cards = ECHO_SHARDS.map((s, idx) => {
        const district = CHAPTERS[s.seg]?.name ?? `Sector ${s.seg + 1}`;
        const sp = SPEAKERS[s.speaker] ?? SPEAKERS.sora;
        const open = unlocked.has(s.id);
        if (!open) {
          return `
            <article class="codex-card locked">
              <div class="codex-card-head">
                <span class="codex-kicker">Echo ${pad2(idx + 1)} // ${esc(district)}</span>
                <span class="codex-status">Encrypted</span>
              </div>
              <h4>Unrecovered Memory Shard</h4>
              <p class="codex-copy">Approach the cyan-and-gold Echo Shrine in ${esc(district)} to decrypt this Komorebi archive fragment.</p>
            </article>`;
        }
        return `
          <article class="codex-card" style="--card-accent:${esc(sp.color)}">
            <div class="codex-card-head">
              <span class="codex-kicker">Echo ${pad2(idx + 1)} // ${esc(district)}</span>
              <span class="codex-status done">Recovered</span>
            </div>
            <h4>${esc(s.title)}</h4>
            <p class="codex-quote">“${esc(s.quote)}”</p>
            <p class="codex-copy">${esc(s.lore)}</p>
            <div class="echo-speaker-row">
              <span class="echo-speaker-chip"><img src="/characters/${esc(s.speaker)}.svg" alt="" />${esc(sp.name)}</span>
              <button type="button" class="echo-voice-btn" data-echo-id="${esc(s.id)}">▶ Play Voice</button>
            </div>
          </article>`;
      }).join('');
      echoesPane.innerHTML = `
        <div class="echo-summary">
          <div class="echo-summary-row"><span>Recovered Memory Echoes</span><strong>${unlocked.size} / ${total}</strong></div>
          <div class="echo-meter" aria-hidden="true">${pips}</div>
        </div>
        ${cards}`;
      echoesPane.querySelectorAll('.echo-speaker-chip img').forEach((img) => {
        img.addEventListener('error', () => {
          if (!img.src.endsWith('/characters/portrait.png')) img.src = '/characters/portrait.png';
        });
      });
    }
  }

  echoesPane?.addEventListener('click', (e) => {
    const btn = e.target.closest('.echo-voice-btn');
    if (!btn) return;
    const shard = ECHO_SHARDS.find((s) => s.id === btn.dataset.echoId);
    if (!shard) return;
    audio.unlock();
    audio.speak(shard.quote, shard.speaker);
  });

  codexToggle?.addEventListener('click', () => {
    if (!codexBody) return;
    audio.play('uiClick');
    const nextOpen = codexBody.hidden;
    codexBody.hidden = !nextOpen;
    codexToggle.setAttribute('aria-expanded', String(nextOpen));
    if (nextOpen) renderCodexDynamic();
  });

  const tabBtns = [...document.querySelectorAll('[data-codex-tab]')];
  const panes = { acts: actsPane, cast: castPane, echoes: echoesPane };
  for (const btn of tabBtns) {
    btn.addEventListener('click', () => {
      audio.play('uiClick');
      const target = btn.dataset.codexTab;
      for (const b of tabBtns) {
        const on = b === btn;
        b.classList.toggle('on', on);
        b.setAttribute('aria-selected', String(on));
      }
      for (const [key, pane] of Object.entries(panes)) {
        if (pane) pane.hidden = key !== target;
      }
    });
  }

  refreshMenuEnhancements = () => {
    syncDayNightBtn();
    if (menuTourBtn && window.lab?.showcase) {
      menuTourBtn.textContent = window.lab.showcase.active ? '◈ Stop Investor Tour' : '◈ Launch Investor Tour';
    }
    renderCodexDynamic();
  };
  refreshMenuEnhancements();
}
// Online: no room in the address is a quick match with strangers; an invite is the page's link with its room named,
// and a private room is a fresh random name only a link reaches.
if (q.get('mode') === 'online' && !clean) {
  document.getElementById('online-panel').hidden = document.getElementById('net-pill').hidden = false;
  const status = document.getElementById('net-status');
  document.getElementById('net-invite').addEventListener('click', async () => {
    const room = window.lab?.net?.room;
    if (!room) { status.textContent = 'Not in a room yet'; return; }
    const link = new URL(location.href);
    link.searchParams.set('room', room);
    for (const key of ['fresh', 'capture', 'clean']) link.searchParams.delete(key);
    try { await navigator.clipboard.writeText(link.href); status.textContent = 'Invite link copied'; }
    catch { status.textContent = link.href; }
  });
  document.getElementById('net-match').addEventListener('click', () => {
    const next = new URLSearchParams(location.search);
    next.delete('room');
    location.search = next.toString();
  });
  document.getElementById('net-private').addEventListener('click', () => {
    const next = new URLSearchParams(location.search);
    next.set('room', crypto.randomUUID().slice(0, 8));
    location.search = next.toString();
  });
}
const quality = document.getElementById('quality');
quality.value = qualityMode(q.get('quality'));
quality.addEventListener('change', () => {
  const next = new URL(location.href);
  next.searchParams.set('quality', quality.value);
  history.replaceState(null, '', next);
  window.lab?.graphics?.setMode(quality.value);
});

try {
  await screenReady;
  if (SCENE === 'autumn') await import('./main.js');
  else await import('./night/main.js');
} catch (error) {
  console.error(error);
  const loading = document.getElementById('loading');
  if (loading) {
    loading.querySelector('p').textContent = 'The world could not open. Please reload to try again.';
    loading.querySelector('.loading-track')?.remove();
  }
}
