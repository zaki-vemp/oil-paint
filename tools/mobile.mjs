// Exercises mobile entry, native fullscreen, earned touch controls, powers and contextual driving.
// Uses mobile emulation; physical Safari/Chrome support still depends on the device browser.
import assert from 'node:assert/strict';
import { mkdirSync } from 'node:fs';
import { chromium } from 'playwright-core';
import { guard } from './no-pointer-lock.mjs';
import { LEVELS } from '../src/game/levels.js';

guard(chromium);
const url = process.argv[2] ?? 'http://127.0.0.1:5405';
const shots = process.argv.includes('--shots') ? process.argv[process.argv.indexOf('--shots') + 1] : null;
if (shots) mkdirSync(shots, { recursive: true });
const browser = await chromium.launch({ executablePath: process.env.CHROME, headless: true,
  args: ['--ignore-gpu-blocklist', ...(process.platform === 'darwin' ? ['--use-angle=metal'] : [])] });
const errors = [];
const open = async (mobile = true, api = 'native') => {
  const context = await browser.newContext({ viewport: mobile ? { width: 390, height: 844 } : { width: 1280, height: 800 },
    isMobile: mobile, hasTouch: mobile, deviceScaleFactor: 1 });
  await context.addInitScript(api => {
    window.fullscreenRequests = [];
    const native = Element.prototype.requestFullscreen;
    Element.prototype.requestFullscreen = api === 'absent' ? undefined : function (options) {
      window.fullscreenRequests.push({ active: navigator.userActivation.isActive, options });
      return api === 'rejected' ? Promise.reject(new Error('Fullscreen denied')) : native.call(this, options);
    };
    if (api === 'absent') Element.prototype.webkitRequestFullscreen = undefined;
  }, api);
  const page = await context.newPage();
  page.on('pageerror', e => errors.push(e.message));
  return { context, page };
};
const ready = page => page.waitForFunction(() => window.lab?.ready, null, { timeout: 120000 });
const watchInput = page => page.evaluate(() => {
  window.mobileActions = [];
  const read = window.lab.input.read.bind(window.lab.input);
  window.lab.input.read = dt => {
    const state = read(dt);
    for (const key of ['attack', 'jump', 'jumpHeld', 'dash', 'heavy', 'power', 'flare', 'whirl', 'ride']) {
      if (state[key]) window.mobileActions.push(key);
    }
    if (state.attack && Math.hypot(state.move.x, state.move.y) > 0.1) window.mobileCombo = true;
    return state;
  };
});
const initial = async page => {
  await page.goto(`${url}/?scene=autumn&quality=low&nopad`);
  await page.locator('#screen-choice').waitFor({ state: 'visible' });
  assert(await page.evaluate(() => window.__paused && !window.lab && !document.fullscreenElement && !window.fullscreenRequests.length),
    'the world waits for the choice, with no automatic fullscreen request');
};

try {
  let { context, page } = await open();
  await initial(page);
  for (const [width, height] of [[320, 568], [390, 844], [568, 320], [844, 390]]) {
    await page.setViewportSize({ width, height });
    await page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));
    assert(await page.evaluate(() => ['start-fullscreen', 'start-normal'].every(id => {
      const r = document.getElementById(id).getBoundingClientRect();
      return r.left >= 0 && r.top >= 0 && r.right <= innerWidth && r.bottom <= innerHeight;
    })), `both choices fit ${width}x${height}`);
    if (shots) await page.screenshot({ path: `${shots}/choice-${width}.png` });
  }
  await page.tap('#start-normal');
  await ready(page);
  assert.equal(await page.evaluate(() => window.fullscreenRequests.length), 0, 'Normal does not request fullscreen');
  assert.equal(await page.evaluate(() => window.__paused), false);
  assert(await page.locator('#stick').isVisible(), 'touch controls are present');
  assert.deepEqual(await page.locator('.touch-pad button:visible').allTextContents(), ['Attack', 'Jump'], 'a new run has just two actions');
  await watchInput(page);
  const before = await page.evaluate(() => ({ x: window.lab.player.feet.x, z: window.lab.player.feet.z }));
  const stick = await page.locator('#stick').boundingBox();
  const cdp = await context.newCDPSession(page);
  const thumb = { x: stick.x + stick.width / 2, y: stick.y + 14, id: 1 };
  await cdp.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [thumb] });
  await page.waitForTimeout(700);
  const attack = await page.locator('.touch-attack').boundingBox();
  await cdp.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [thumb, { x: attack.x + attack.width / 2, y: attack.y + attack.height / 2, id: 2 }] });
  await page.waitForFunction(() => window.mobileCombo);
  await cdp.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] });
  const moved = await page.evaluate(before => Math.hypot(window.lab.player.feet.x - before.x, window.lab.player.feet.z - before.z), before);
  assert(moved > 0.5, 'a real touch drag moves the player');
  const jump = await page.locator('#touch-jump').boundingBox();
  await cdp.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [{ x: jump.x + 32, y: jump.y + 32, id: 1 }] });
  await page.waitForFunction(() => window.mobileActions.includes('jumpHeld'));
  await cdp.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] });
  assert.equal(await page.evaluate(() => window.fullscreenRequests.length), 0, 'gameplay touches never force fullscreen');
  await page.tap('#pause-button');
  assert(await page.evaluate(() => window.__paused && document.getElementById('menu').open));
  await page.tap('#fill');
  await page.waitForFunction(() => !!document.fullscreenElement && document.getElementById('fill').textContent === 'Normal screen');
  assert.equal(await page.locator('#fill').textContent(), 'Normal screen');
  assert(await page.evaluate(() => window.fullscreenRequests.every(r => r.active)), 'native requests have a user gesture');
  await page.tap('#fill');
  await page.waitForFunction(() => !document.fullscreenElement);
  await page.tap('#resume');
  assert.equal(await page.evaluate(() => window.__paused), false, 'normal mode resumes the same game');
  await page.evaluate(() => {
    const L = window.lab, v = L.rides.rides.find(v => v.kind === 'car' && !v.traffic);
    L.teleport(v.x + 1.8, v.z, v.x, v.z);
  });
  await page.tap('.ride-prompt');
  await page.waitForFunction(() => !!window.lab.rides.driven);
  assert.deepEqual(await page.locator('.touch-pad button:visible').allTextContents(), ['Brake'], 'driving replaces combat with Brake');
  assert.equal(await page.locator('.ride-prompt').innerText(), 'Exit vehicle');
  await page.evaluate(() => { window.mobileActions = []; });
  const brake = await page.locator('#touch-jump').boundingBox();
  await cdp.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [{ x: brake.x + brake.width / 2, y: brake.y + brake.height / 2, id: 1 }] });
  await page.waitForFunction(() => window.mobileActions.includes('jumpHeld'));
  await cdp.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] });
  assert.equal(await page.evaluate(() => window.lab.input.keys.has('Space')), false, 'releasing Brake clears the hold');
  if (shots) await page.screenshot({ path: `${shots}/mobile-driving.png` });
  await page.tap('.ride-prompt');
  await page.waitForFunction(() => !window.lab.rides.driven);
  assert.equal(await page.locator('#touch-jump span').textContent(), 'Jump');
  if (shots) await page.screenshot({ path: `${shots}/mobile-game.png` });
  await context.close();

  ({ context, page } = await open());
  await page.goto(`${url}/?scene=autumn&mode=roam&quality=low&nopad`);
  await page.setViewportSize({ width: 844, height: 390 });
  await page.tap('#start-normal');
  await ready(page);
  await watchInput(page);
  assert.equal(await page.locator('.touch-pad button:visible').count(), 4, 'all powers still need only four main buttons');
  assert.equal(await page.locator('#power-options').isVisible(), false, 'extra powers start closed');
  await page.tap('#touch-powers');
  assert.deepEqual(await page.locator('#power-options button:visible').evaluateAll(buttons => buttons.map(b => b.dataset.action)),
    ['spark', 'heavy', 'flare', 'whirl'], 'the drawer contains earned abilities without duplicate attack or dash');
  await page.tap('[data-action="heavy"]');
  await page.waitForFunction(() => window.mobileActions.includes('heavy'));
  assert.equal(await page.locator('#power-options').isVisible(), false, 'using a power closes the drawer');
  await page.tap('#touch-dash');
  await page.waitForFunction(() => document.getElementById('touch-dash').disabled);
  assert.match(await page.locator('#touch-dash small').textContent(), /s$/, 'dash explains its cooldown');
  await page.tap('#touch-powers');
  await page.tap('[data-action="spark"]');
  await page.waitForFunction(() => window.mobileActions.includes('power'));
  await page.tap('#touch-powers');
  await page.tap('[data-action="flare"]');
  await page.waitForFunction(() => window.mobileActions.includes('flare'));
  await page.tap('#touch-powers');
  assert(await page.locator('[data-action="whirl"]').isDisabled(), 'uncharged powers cannot be wasted');
  assert.equal(await page.locator('[data-action="whirl"] small').textContent(), 'No charge');
  await page.tap('#pause-button');
  assert.equal(await page.locator('#power-options').isVisible(), false, 'opening the menu closes powers');
  await page.locator('#menu summary').click();
  assert(await page.locator('.touch-help').isVisible(), 'mobile has its own control guide');
  assert.equal(await page.locator('.controls-grid').isVisible(), false);
  await page.locator('#menu summary').click();
  await page.tap('#resume');
  if (await page.locator('.story button').isVisible()) await page.tap('.story button');
  for (const [width, height] of [[568, 320], [844, 390], [1024, 768]]) {
    await page.setViewportSize({ width, height });
    await page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));
    assert(await page.locator('.touch-pad button:visible').evaluateAll(buttons => buttons.every(b => {
      const r = b.getBoundingClientRect();
      return r.width >= 44 && r.height >= 44 && r.left >= 0 && r.top >= 0 && r.right <= innerWidth && r.bottom <= innerHeight;
    })), 'thumb controls fit at ' + width);
    if (shots) await page.screenshot({ path: `${shots}/controls-${width}.png` });
    await page.tap('#touch-powers');
    assert(await page.locator('#power-options').evaluate(el => {
      const r = el.getBoundingClientRect();
      return r.left >= 0 && r.top >= 0 && r.right <= innerWidth && r.bottom <= innerHeight;
    }), 'powers fit at ' + width);
    if (shots) await page.screenshot({ path: `${shots}/powers-${width}.png` });
    await page.tap('#touch-powers');
  }
  await context.close();

  ({ context, page } = await open());
  await page.goto(`${url}/?scene=aurora&quality=low&nopad`);
  await page.locator('#screen-choice').waitFor({ state: 'visible' });
  await page.setViewportSize({ width: 844, height: 390 });
  await page.tap('#start-fullscreen');
  await ready(page);
  assert(await page.evaluate(() => !!document.fullscreenElement && !window.__paused && !document.getElementById('screen-choice').open),
    'the fullscreen startup path loads Snowbound and starts play');
  assert.equal(await page.evaluate(() => window.fullscreenRequests.length), 1);
  await page.evaluate(() => document.exitFullscreen());
  await page.tap('#view canvas', { position: { x: 400, y: 200 } });
  assert.equal(await page.evaluate(() => window.fullscreenRequests.length), 1, 'leaving fullscreen never triggers automatic re-entry');
  await context.close();

  for (const api of ['rejected', 'absent']) {
    ({ context, page } = await open(true, api));
    if (api === 'rejected') await context.addInitScript(checkpoint => {
      localStorage.setItem('oilpaint.levels.v1', JSON.stringify({ cleared: 1, x: checkpoint.x, z: checkpoint.z }));
    }, LEVELS[1].checkpoint);
    await initial(page);
    await page.tap('#start-fullscreen');
    await page.locator('#screen-status').waitFor({ state: 'visible' });
    assert(await page.evaluate(() => document.getElementById('screen-choice').open && !window.lab), 'an unavailable request leaves a usable choice');
    assert(await page.locator('#start-normal').isEnabled());
    if (shots && api === 'absent') await page.screenshot({ path: `${shots}/fullscreen-unavailable.png` });
    await page.tap('#start-normal');
    await ready(page);
    assert.equal(await page.evaluate(() => window.__paused), false, api + ' falls back to a running game');
    if (api === 'rejected') {
      assert.equal(await page.locator('#touch-powers').getAttribute('hidden'), null, 'the first earned power appears after resuming a checkpoint');
      assert.equal(await page.locator('#touch-dash').getAttribute('hidden'), '', 'dash stays hidden until its own unlock');
    }
    await context.close();
  }

  ({ context, page } = await open(false));
  await page.goto(`${url}/?scene=autumn&quality=low&nopad`);
  await ready(page);
  assert.equal(await page.locator('#screen-choice').isVisible(), false, 'desktop starts normally');
  assert(await page.locator('.actions').isVisible(), 'desktop keeps its existing abilities bar');
  assert.equal(await page.locator('#touch-controls').isVisible(), false);
  await context.close();
  ({ context, page } = await open());
  await page.goto(`${url}/?scene=autumn&capture&quality=low&nopad`);
  await ready(page);
  assert.equal(await page.locator('#screen-choice').isVisible(), false, 'capture tools bypass the choice');
  await context.close();
  assert.deepEqual(errors, [], 'no uncaught browser errors');
  console.log('PASS: screen choice, fullscreen/fallback, earned touch actions, multitouch, power drawer/cooldowns, driving, three sizes, both scenes and desktop');
} finally { await browser.close(); }
