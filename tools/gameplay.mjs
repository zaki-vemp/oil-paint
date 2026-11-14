import { LEVELS, GOAL } from '../src/game/levels.js';
const PLAN = LEVELS.map(l => ({ checkpoint: l.checkpoint, grant: l.grant }));
// Exercises the real browser game: movement, short hops, collision-swept dash, manual combat,
// chapter unlocks, save restoration, pause and roam. Build/serve first, then:
//   CHROME=/path/to/chrome node tools/gameplay.mjs [http://127.0.0.1:5405]
import assert from 'node:assert/strict';
import { chromium } from 'playwright-core';
import { guard } from './no-pointer-lock.mjs';
guard(chromium);
const baseURL = process.argv[2] ?? 'http://127.0.0.1:5405';
const browser = await chromium.launch({ executablePath: process.env.CHROME, headless: true });
const errors = [];
try {
  const page = await browser.newPage({ viewport: { width: 1440, height: 900 } });
  page.on('pageerror', e => errors.push(e.message));
  page.on('console', m => { if (m.type() === 'error') errors.push(m.text()); });
  await page.goto(`${baseURL}/?scene=autumn&fresh&nopad`);
  await page.waitForFunction(() => window.lab?.ready, null, { timeout: 120000 });
  const result = await page.evaluate((plan) => {
    window.__paused = true;
    const L = window.lab, P = L.player, h = L.hunt;
    const base = { move: { x: 0, y: 0 }, look: { x: 0, y: 0 }, walk: false, sprint: false, strafe: false, jump: false, jumpHeld: true, zoom: 0, device: 'keyboard' };
    const input = state => L.setInput(() => ({ ...base, ...state }));
    const check = (condition, message) => { if (!condition) throw Error(message); };
    const results = {};
    check(L.figure.avatar.bindings.length >= 40, 'anime humanoid bindings');
    check(h.enemies.filter(e => e.duel).length === plan.length, 'one mirror rival per chapter');
    L.teleport(0, 78, 0, 60); input({ move: { x: 0, y: 1 } }); L.step(1 / 60, 30);
    results.runDistance = 78 - P.feet.z; check(results.runDistance > 1, 'responsive running');
    input({}); L.step(1 / 60, 20); check(P.speed < 0.05, 'release stops');
    function jump(held) {
      L.teleport(0, 78, 0, 60); const y = P.feet.y; let peak = y, n = 0;
      L.setInput(() => { peak = Math.max(peak, P.feet.y); return { ...base, jump: n++ === 0, jumpHeld: held }; });
      L.step(1 / 60, 80); check(P.grounded, 'landed'); return peak - y;
    }
    results.holdJump = jump(true); results.tapJump = jump(false);
    check(results.holdJump > 1 && results.tapJump < results.holdJump * 0.8, 'variable jumps');
    P.teleport(0, L.heightAt(0, 78) + 0.08, 78); P.grounded = false; P.coyote = 0; P.mode = 'air'; P.vel.y = -1;
    let frame = 0, bufferedLaunch = false;
    L.setInput(() => { bufferedLaunch ||= P.vel.y > 2; return { ...base, jump: frame++ === 0 }; });
    L.step(1 / 60, 20); check(bufferedLaunch, 'jump buffered before landing');
    P.teleport(0, L.heightAt(0, 78) + 0.08, 78); P.grounded = false; P.coyote = 0.08;
    input({ jump: true }); L.step(1 / 60, 1); check(P.vel.y > 2, 'coyote jump after leaving ground');
    input({});
    // Test the windup before isolating the beasts for reproducible damage/progression checks.
    const first = h.enemies[0];
    first.aggro = 0; first.wait = 100;
    L.teleport(first.x, first.z + 3, first.x, first.z); input({ move: { x: 0, y: 1 } }); L.step(1 / 60, 60);
    check(Math.hypot(P.feet.x - first.x, P.feet.z - first.z) > 0.6, 'enemy capsule blocks movement');
    input({}); first.aggro = 9; L.teleport(first.x, first.z + 1.4, first.x, first.z);
    const hp = h.hp; L.step(1 / 60, 1); check(first.windup > 0 && h.hp === hp, 'windup before damage');
    L.teleport(first.x + 5, first.z + 4, first.x, first.z); L.step(1 / 60, 35);
    check(h.hp === hp, 'moving out dodges the committed attack');
    first.windup = first.cool = 0;
    const chapterStyles = [];
    let checkedRetry = false;
    for (let segment = 0; segment < plan.length; segment++) {
      const pack = h.enemies.filter(e => !e.duel && e.seg === segment);
      for (const e of pack) { e.aggro = 0; e.mode = 'roam'; e.wait = 100; }
      for (const e of pack) {
        // Keep this a damage/balance test; the windup was tested with active AI above.
        input({}); L.step(1 / 60, 60); e.aggro = 0; e.mode = 'roam'; e.wait = 100; e.windup = 0;
        L.teleport(e.x, e.z + 2.1, e.x, e.z);
        const before = e.hp; L.step(1 / 60, 20); check(e.hp === before, 'proximity never attacks: ' + segment + ' ' + e.kind);
        let frame = 0;
        const heavy = h.powers.heavy;
        L.setInput(() => {
          // Follow the new knockback while isolating damage/balance from pursuit AI.
          if (!e.dead && Math.hypot(P.feet.x - e.x, P.feet.z - e.z) > 2.2) {
            P.teleport(e.x, L.heightAt(e.x, e.z + 1.9), e.z + 1.9); P.yaw = P.lastYaw = Math.PI;
          }
          return { ...base, attack: !heavy && frame++ % 60 === 0, heavy: heavy && frame++ % 60 === 0 };
        });
        L.step(1 / 60, heavy ? 330 : 370);
        check(e.dead, 'sword defeats ' + e.kind + ' in chapter ' + segment);
      }
      input({}); L.step(1 / 60, 2);
      check(h.seg === segment, 'creatures alone cannot clear chapter');
      const rival = h.enemies.find(e => e.duel && e.seg === segment);
      L.teleport(rival.x, rival.z + 4.5, rival.x, rival.z); L.step(1 / 60, 60, false);
      if (!checkedRetry) { L.respawn(); check(Math.hypot(P.feet.x - rival.homeX, P.feet.z - rival.homeZ - 6) < 0.1, 'nearby duel retry point'); checkedRetry = true; }
      for (let frame = 0; frame < 1800 && !rival.dead; frame++) {
        if (rival.phase === 'recovery') {
          if (frame % 8 === 0) L.teleport(rival.x, rival.z + 1.9, rival.x, rival.z);
          input({ attack: !h.powers.heavy && frame % 28 === 0, heavy: h.powers.heavy && frame % 55 === 0 });
        } else {
          input({});
          if (frame % 8 === 0) L.teleport(rival.x, rival.z + (rival.phase === 'approach' || rival.phase === 'intro' ? 2.2 : 4.2), rival.x, rival.z);
        }
        L.step(1 / 60, 1, false);
      }
      check(rival.dead && rival.evaded && rival.punishes > 0, 'rival lesson and victory ' + segment + ' ' + JSON.stringify({ phase: rival.phase, hp: rival.hp, evaded: rival.evaded, punishes: rival.punishes }));
      input({}); L.step(1 / 60, 3);
      check(h.seg === segment + 1, 'ordered chapter clear ' + segment);
      const expected = plan[segment].grant;
      if (expected) check(h.powers[expected], 'earned ' + expected);
      const next = plan[segment + 1]?.checkpoint ?? plan[0].goal; const cp = [next.x, next.z];
      L.teleport(cp[0], cp[1], cp[0], cp[1] - 10); L.step(1 / 60, 90);
      chapterStyles.push(document.documentElement.dataset.style);
    }
    check(h.won, 'sanctuary victory');
    results.chapterStyles = chapterStyles;
    results.rivalLessons = h.enemies.filter(e => e.duel).map(e => ({ chapter: e.seg + 1, evaded: e.evaded, punishes: e.punishes }));
    results.save = JSON.parse(localStorage.getItem('oilpaint.levels.v1'));
    check(results.save.cleared === plan.length, 'victory persists');
    // After winning, create a temporary wall in open space to test the actual capsule sweep.
    L.teleport(0, 78, 0, 60); input({}); const y = P.feet.y;
    const wall = P.world.createCollider(P.R.ColliderDesc.cuboid(3, 2, 0.2).setTranslation(0, y + 1, 75.5));
    P.world.step(); check(P.startDash(), 'ground dash starts'); L.step(1 / 60, 30);
    results.dashEndZ = P.feet.z;
    check(P.feet.z > 75.9 && P.feet.z < 77, 'dash stops at the wall');
    check(Math.abs(P.visualY - P.feet.y) < 0.2, 'dash visual history');
    P.world.removeCollider(wall, true); L.setInput(null);
    return results;
  }, PLAN.map(p => ({...p, goal: GOAL})));
  // Pause freezes the real animation loop, and its controls remain keyboard accessible.
  await page.click('#pause-button');
  assert.equal(await page.locator('#menu').evaluate(el => el.open), true);
  const before = await page.evaluate(() => window.lab.shared.uTime.value);
  await page.waitForTimeout(150);
  assert.equal(await page.evaluate(() => window.lab.shared.uTime.value), before);
  await page.keyboard.press('Escape');
  assert.equal(await page.locator('#menu').evaluate(el => el.open), false);
  // Restore an existing checkpoint without using ?fresh. Keep the same storage context.
  await page.goto(`${baseURL}/?scene=autumn&nopad`);
  await page.waitForFunction(() => window.lab?.ready, null, { timeout: 120000 });
  assert.equal(await page.evaluate(() => window.lab.hunt.seg), PLAN.length);
  const saved = await page.evaluate(() => localStorage.getItem('oilpaint.levels.v1'));
  await page.goto(`${baseURL}/?scene=autumn&mode=roam&nopad`);
  await page.waitForFunction(() => window.lab?.ready, null, { timeout: 120000 });
  assert.equal(await page.evaluate(() => Object.values(window.lab.hunt.powers).every(Boolean)), true);
  assert.equal(await page.evaluate(() => localStorage.getItem('oilpaint.levels.v1')), saved, 'roam preserves story save');
  await page.goto(`${baseURL}/?scene=autumn&fresh&nopad`);
  await page.waitForFunction(() => window.lab?.ready, null, { timeout: 120000 });
  const warning = await page.evaluate(() => {
    window.__paused = true;
    Storage.prototype.setItem = () => { throw new Error('storage denied'); };
    for (const e of window.lab.hunt.enemies) if (e.seg === 0) e.dead = 0.001;
    window.lab.step(1 / 60, 2);
    return document.querySelector('.toast').textContent;
  });
  assert.match(warning, /Progress is not saving/, 'save failure remains visible after unlock');
  assert.deepEqual(errors, [], 'browser errors');
  console.log(JSON.stringify(result, null, 2));
  console.log(`PASS: anime rigs, physics, combat, ${PLAN.length} rival lessons, chapters, save/restore, pause and free roam`);
} finally {
  await browser.close();
}
