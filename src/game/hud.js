// The adventure HUD reads live game state: health, earned abilities, enemies and the next checkpoint.
// Its small canvas is an actual local map of the trail, not a decorative radar. Chapter art direction
// changes its treatment automatically; keyboard and touch buttons feed the same Input state.
import { CHAPTERS } from './chapters.js';
import { POWERS, POWER_ORDER } from './levels.js';

export const BEAST_NAMES = { dog: 'Bramble pup', wolf: 'Mist wolf', brute: 'Grove guardian' };
const icon = name => `<svg aria-hidden="true" viewBox="0 0 24 24"><use href="#i-${name}"/></svg>`;

export function createHUD({ levels, roam, marks, route }) {
  const hud = document.createElement('div');
  hud.id = 'hud';
  hud.innerHTML = `
    <section class="quest" aria-label="Current objective">
      <div class="chapter-label">${levels ? 'Chapter 1 of 5' : 'Side story · Snowbound'}</div>
      <h2>${levels ? CHAPTERS[0].name : 'Under the aurora'}</h2>
      <p class="objective">Find your way through the wood.</p>
      <div class="quest-count"><span class="count"></span><span class="distance"></span></div>
      ${levels ? '<div class="journey" aria-label="Chapter progress">' + levels.map((_, i) => `<i data-step="${i}"></i>`).join('') + '</div>' : ''}
    </section>
    <div class="map-wrap"><canvas class="minimap" width="288" height="288" aria-label="Local trail map: your position, beasts and checkpoints"></canvas><span class="north">N</span><span class="map-label">${roam ? 'Free exploration' : levels ? 'The spirit trail' : 'Snowbound ridge'}</span></div>
    <section class="vitals" aria-label="Traveler status">
      <div class="portrait" aria-hidden="true"><svg viewBox="0 0 64 74"><path fill="#243d48" d="M9 74 17 42 46 42 56 74Z"/><path fill="#478e9c" d="m31 41-14 4-8 29h45l-8-29Z"/><path fill="#e6b98b" d="M23 24h19v17l-10 7-9-8Z"/><path fill="#243d48" d="M22 26h22v8l-5-6-5 4-8-2-4 5Z"/><path fill="#ebce83" d="m7 26 15-8 4-14h12l6 14 14 8-24 6Z"/><path fill="#31594f" d="m22 18 22 0 2 5-26 0Z"/><path fill="#e88165" d="m27 44 7 3 10-4-5 10-12-4Z"/></svg></div>
      <div class="vitals-data"><div class="player-name">Traveler <span class="hp-number">100 / 100</span></div>
        <div class="hp" role="meter" aria-label="Health" aria-valuemin="0" aria-valuemax="100"><span></span></div>
        <div class="spark" aria-label="Spirit energy"><i><b></b></i><i><b></b></i><i><b></b></i><em>Spirit</em></div>
      </div>
    </section>
    <div class="target" hidden><div><strong></strong><span></span></div><div class="target-hp"><i></i></div><p></p></div>
    <nav class="actions" aria-label="Abilities">
      <button data-action="attack" data-code="KeyJ" title="J or left click: sword strike">${icon('sword')}<kbd>J</kbd><span>Strike</span><small></small></button>
      ${(levels ? POWER_ORDER : ['spark']).map(k => `<button data-action="${k}" data-code="Key${POWERS[k].key}" title="${POWERS[k].hint}">${icon(k)}<kbd>${POWERS[k].key}</kbd><span>${k === 'heavy' ? 'Heavy' : k === 'flare' ? 'Flare' : POWERS[k].name}</span><small></small></button>`).join('')}
    </nav>
    <div class="toast" role="status" aria-live="polite"></div>
    <div class="chapter-banner" aria-hidden="true"><span></span><strong></strong><p></p></div>
    <div class="hit-feedback" aria-hidden="true"></div>`;
  document.body.appendChild(hud);
  const get = s => hud.querySelector(s);
  const els = {
    hp: get('.hp'), fill: get('.hp span'), health: get('.hp-number'), pips: [...hud.querySelectorAll('.spark b')],
    title: get('.quest h2'), chapter: get('.chapter-label'), objective: get('.objective'), count: get('.count'), distance: get('.distance'),
    steps: [...hud.querySelectorAll('.journey i')], actions: [...hud.querySelectorAll('.actions button')],
    target: get('.target'), targetName: get('.target strong'), targetHp: get('.target span'), targetFill: get('.target-hp i'), tell: get('.target p'),
  };
  hud.querySelectorAll('[data-code]').forEach(button => button.addEventListener('click', () => {
    if (!window.__paused) window.lab?.input.pressed.add(button.dataset.code);
    button.blur();
  }));
  let chapter = 0, bannerTime = 4.5, toastTime = 0, mapTime = 0, lastHp = 100;
  const banner = get('.chapter-banner');
  function showChapter(c) {
    chapter = c.index;
    banner.querySelector('span').textContent = `Chapter ${chapter + 1} · ${c.style === 'field' ? 'Field journal' : 'Spirit watch'}`;
    banner.querySelector('strong').textContent = c.name;
    banner.querySelector('p').textContent = c.subtitle;
    bannerTime = 4.5;
  }
  if (levels) showChapter({ index: 0, ...CHAPTERS[0] });
  else banner.hidden = true;
  addEventListener('chapterchange', e => showChapter(e.detail));
  const map = get('.minimap').getContext('2d');
  function drawMap(player, enemies) {
    const scale = 2.5, px = player.feet.x, pz = player.feet.z;
    const point = (x, z) => [144 + (x - px) * scale, 144 + (z - pz) * scale];
    map.clearRect(0, 0, 288, 288);
    map.fillStyle = '#294c41'; map.fillRect(0, 0, 288, 288);
    map.strokeStyle = '#52775a'; map.lineWidth = 24; map.lineCap = 'round';
    map.beginPath();
    (route ?? marks.map(m => [m.x, m.z])).forEach(([x, z], i) => { const p = point(x, z); i ? map.lineTo(...p) : map.moveTo(...p); });
    map.stroke(); map.strokeStyle = '#e9d6a0'; map.lineWidth = 5; map.stroke();
    for (const m of marks) {
      const [x, z] = point(m.x, m.z);
      map.fillStyle = m.used ? '#9dd6ae' : '#f4e8bd';
      map.fillRect(x - 4, z - 4, 8, 8);
    }
    for (const e of enemies) {
      if (e.dead || Math.hypot(e.x - px, e.z - pz) > 42) continue;
      const [x, z] = point(e.x, e.z);
      map.beginPath(); map.arc(x, z, e.kind === 'brute' ? 5 : 3.5, 0, Math.PI * 2);
      map.fillStyle = '#ee967b'; map.fill();
    }
    map.save(); map.translate(144, 144); map.rotate(-player.yaw);
    map.beginPath(); map.moveTo(0, 10); map.lineTo(-7, -6); map.lineTo(0, -2); map.lineTo(7, -6); map.closePath();
    map.fillStyle = '#fffdf0'; map.fill(); map.restore();
  }
  function toast(message) { get('.toast').textContent = message; toastTime = 3.5; get('.toast').classList.add('visible'); }
  function update(dt, { hp, power, cleared, won, enemies, player, has, dashCool, flareCool, invuln }) {
    bannerTime = Math.max(0, bannerTime - dt);
    banner.classList.toggle('visible', bannerTime > 0);
    toastTime = Math.max(0, toastTime - dt);
    get('.toast').classList.toggle('visible', toastTime > 0);
    hud.classList.toggle('hurt', hp < lastHp);
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
    const alive = enemies.filter(e => !e.dead && (!levels || roam || e.seg === index));
    const total = levels && !roam ? levels[index].spots.length : enemies.length;
    const next = marks.find(m => !m.used) ?? marks[marks.length - 1];
    els.chapter.textContent = !levels ? 'Side story · Snowbound' : roam ? 'Free exploration · All abilities' : won ? 'Journey complete' : `Chapter ${chapter + 1} of ${levels.length}`;
    els.title.textContent = levels ? CHAPTERS[chapter].name : 'Under the aurora';
    els.objective.textContent = won ? 'The sanctuary is safe. Take a breath.' : cleared >= (levels?.length ?? Infinity) ? 'Follow the trail to the sanctuary.' : roam ? 'Explore freely. Shrines restore health.' : levels && cleared > chapter ? 'Chapter clear. Find the next shrine.' : levels ? 'Defeat the beasts to open the shrine.' : 'Follow the ridge to the lantern-lit cabin.';
    els.count.textContent = won ? 'All chapters cleared' : levels && !roam ? `${total - alive.length} / ${total} beasts defeated` : `${alive.length} beasts on the trail`;
    els.distance.textContent = `${Math.round(Math.hypot(player.feet.x - next.x, player.feet.z - next.z))} m to shrine`;
    els.steps.forEach((el, i) => { el.className = i < cleared ? 'done' : i === cleared ? 'active' : ''; });
    for (const el of els.actions) {
      const key = el.dataset.action, unlocked = key === 'attack' || has(key);
      const cool = key === 'dash' ? dashCool : key === 'flare' ? flareCool : 0;
      const empty = key === 'spark' && power < 1 || key === 'flare' && power < 2;
      el.classList.toggle('locked', !unlocked);
      el.classList.toggle('cooling', unlocked && (cool > 0 || empty));
      el.setAttribute('aria-disabled', String(!unlocked || cool > 0 || empty));
      el.querySelector('small').textContent = !unlocked ? `Ch. ${POWER_ORDER.indexOf(key) + 1}` : cool > 0 ? `${Math.ceil(cool)}s` : empty ? 'Recharging' : '';
    }
    const target = enemies.filter(e => !e.dead).sort((a, b) => Math.hypot(a.x - player.feet.x, a.z - player.feet.z) - Math.hypot(b.x - player.feet.x, b.z - player.feet.z))[0];
    els.target.hidden = !target || Math.hypot(target.x - player.feet.x, target.z - player.feet.z) > 10;
    if (!els.target.hidden) {
      els.targetName.textContent = BEAST_NAMES[target.kind];
      els.targetHp.textContent = `${Math.ceil(target.hp)} HP`;
      els.targetFill.style.transform = `scaleX(${target.hp / target.maxHp})`;
      els.tell.textContent = target.windup > 0 ? 'Incoming strike — move or dash!' : target.stagger > 0 ? 'Staggered — strike now' : 'Keep moving. Watch for the windup.';
      els.target.classList.toggle('warning', target.windup > 0);
    }
    if ((mapTime -= dt) <= 0) { drawMap(player, enemies); mapTime = 0.1; }
  }
  return { update, toast, element: hud };
}
