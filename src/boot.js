// Starts the selected world and owns the pause menu. Chapter progression chooses the HUD style;
// traveling or changing game modes reloads the world so its saves and mechanics stay consistent.
import { SCENE, showScene } from './scenes.js';

const q = new URLSearchParams(location.search);
const root = document.documentElement;
root.dataset.scene = SCENE;
if (SCENE === 'aurora') { root.dataset.style = 'spirit'; root.style.setProperty('--chapter-accent', '#8cd5cc'); }
const clean = q.has('capture') || q.has('clean');
if (clean) root.dataset.clean = '';
const menu = document.getElementById('menu');
function pause(on) {
  window.__paused = on;
  window.lab?.input.reset();
  if (on) {
    document.exitPointerLock?.();
    if (!menu.open) menu.showModal();
  } else if (menu.open) menu.close();
}
const resume = () => {
  pause(false);
  if (matchMedia('(pointer: fine)').matches) window.lab?.renderer.domElement.requestPointerLock?.()?.catch?.(() => {});
};
document.getElementById('pause-button').addEventListener('click', () => pause(true));
document.getElementById('resume').addEventListener('click', resume);
menu.addEventListener('cancel', e => { e.preventDefault(); pause(false); });
addEventListener('keydown', e => {
  if (clean || !window.lab?.ready || e.repeat) return;
  if (['Escape', 'KeyP', 'KeyH'].includes(e.code)) {
    e.preventDefault();
    pause(!window.__paused);
  }
});
let locked = false;
document.addEventListener('pointerlockchange', () => {
  const now = !!document.pointerLockElement;
  if (locked && !now && !clean) pause(true);
  locked = now;
});
document.addEventListener('visibilitychange', () => { if (document.hidden && window.lab?.ready && !clean) pause(true); });
for (const button of document.querySelectorAll('#scenes button')) {
  button.classList.toggle('on', button.dataset.scene === SCENE);
  button.setAttribute('aria-pressed', String(button.dataset.scene === SCENE));
  button.addEventListener('click', () => button.dataset.scene === SCENE ? resume() : showScene(button.dataset.scene));
}
for (const button of document.querySelectorAll('#mode button')) {
  const selected = (q.get('mode') === 'roam' ? 'roam' : 'level') === button.dataset.mode;
  button.classList.toggle('on', selected);
  button.setAttribute('aria-pressed', String(selected));
  button.addEventListener('click', () => {
    if (selected) return resume();
    const next = new URLSearchParams(location.search);
    next.set('mode', button.dataset.mode);
    location.search = next.toString();
  });
}
if (SCENE !== 'autumn') document.getElementById('mode').closest('section').hidden = true;
const quality = document.getElementById('quality');
quality.value = q.get('quality') === 'low' ? 'low' : 'high';
quality.addEventListener('change', () => {
  const next = new URLSearchParams(location.search); next.set('quality', quality.value); location.search = next.toString();
});

try {
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
