// Drives the production game's real key bindings, aim, guard, and swept movement.
import assert from 'node:assert/strict';
import { chromium } from 'playwright-core';
import { guard } from './no-pointer-lock.mjs';

guard(chromium);
const url = process.argv[2] ?? 'http://127.0.0.1:5405';
const browser = await chromium.launch({ executablePath: process.env.CHROME, headless: true,
  args: ['--mute-audio', '--enable-gpu', '--ignore-gpu-blocklist'] });
try {
  const page = await browser.newPage({ viewport: { width: 1024, height: 640 } });
  const errors = [];
  page.on('pageerror', error => errors.push(error.message));
  await page.goto(`${url}/?scene=autumn&fresh&quality=low&nopad`);
  await page.waitForFunction(() => window.lab?.ready, null, { timeout: 120000 });
  await page.evaluate(() => {
    const L = window.lab, P = L.player, H = L.hunt;
    L.renderer.setAnimationLoop(null);
    const z = P.feet.z;
    const crew = H.enemies.filter(e => e.seg === 0 && !e.duel);
    const [front, right] = crew;
    for (const e of H.enemies) {
      if (e.duel) continue;
      e.aggro = e.windup = 0; e.wait = 1e4;
      e.x = e.homeX = e.tx = 220;
    }
    Object.assign(front, { x: 0, z: z - 2.3 });
    Object.assign(right, { x: -4.5, z: z - 5 });
    const citizen = H.enemies.find(e => e.civ && !e.mark);
    Object.assign(citizen, { x: 0, z: z - 1.3 });
    L.teleport(0, z, 0, z - 1);
    window.combatTest = { front, right, citizen, z, step: (n = 1, draw = false) => L.step(1 / 60, n, draw) };
  });
  const step = (n = 1, draw = false) => page.evaluate(([n, draw]) => window.combatTest.step(n, draw), [n, draw]);
  const target = () => page.evaluate(() => {
    const { front, right } = window.combatTest, locked = window.lab.hunt.target;
    return locked === front ? 'front' : locked === right ? 'right' : locked ? 'other' : null;
  });

  await page.keyboard.press('t'); await step(1, true);
  assert.equal(await target(), 'front', 'lock selects the hostile past a calm civilian');
  assert(await page.locator('.focus-marker').isVisible());
  await page.keyboard.press('y'); await step();
  assert.equal(await target(), 'right', 'cycle changes which enemy is locked');
  await page.keyboard.press('y'); await step();
  assert.equal(await target(), 'front', 'cycle wraps');
  await page.evaluate(() => { window.combatTest.citizen.x = 220; });
  await step(8);

  await page.keyboard.down('z'); await step();
  const guarded = await page.evaluate(() => {
    const H = window.lab.hunt, before = H.hp;
    return H.hurtMe(28, window.combatTest.front) && before === H.hp;
  });
  assert(guarded, 'a timed guard catches an incoming frontal strike');
  await step();
  assert.match(await page.locator('.combat-status span').textContent(), /counter/i);
  await page.keyboard.up('z'); await page.keyboard.press('j'); await step(18);
  assert(await page.evaluate(() => window.lab.hunt.combo === 'counter' && window.combatTest.front.hp < window.combatTest.front.maxHp),
    'the distinct counter animation hits the selected enemy');
  await step(30);
  const beforeEvade = await page.evaluate(() => window.lab.player.feet.z);
  await page.keyboard.press('x'); await step(19);
  const afterEvade = await page.evaluate(() => window.lab.player.feet.z);
  assert(afterEvade - beforeEvade > 1.2, `neutral quickstep retreats while keeping the target (${afterEvade - beforeEvade} m)`);
  assert.equal(await target(), 'front');
  await page.keyboard.press('x'); await step();
  assert.equal(await page.evaluate(() => window.lab.player.evadeTime), 0, 'quickstep has a cooldown');
  await page.evaluate(() => {
    const L = window.lab, { front } = window.combatTest, P = L.player;
    const z = (P.feet.z + front.z) / 2;
    window.combatTest.wall = P.world.createCollider(P.R.ColliderDesc.cuboid(4, 2, 0.16).setTranslation(0, P.feet.y + 1, z));
    P.world.step();
  });
  await step(28, true);
  assert.equal(await target(), null, 'cover breaks the lock');
  assert(await page.locator('.focus-marker').isHidden());
  await page.evaluate(() => {
    const L = window.lab, { wall, front, right, z } = window.combatTest;
    L.player.world.removeCollider(wall, true);
    front.x = 220;
    Object.assign(right, { x: 0, z: z - 5.0 });
    L.teleport(0, z, 0, right.z);
  });
  await step(55);
  await page.keyboard.press('t'); await step();
  await page.keyboard.down('w'); await page.keyboard.press('j'); await step(24);
  await page.keyboard.up('w');
  assert(await page.evaluate(() => window.lab.hunt.combo === 'advance'), 'forward and strike starts an advancing cut');
  assert(await page.evaluate(() => window.lab.player.feet.z < window.combatTest.z - 1.8), 'the advancing cut closes distance');
  assert(await page.evaluate(() => window.combatTest.right.hp < window.combatTest.right.maxHp), 'the advancing cut reaches the distant enemy');
  await page.close();

  const touch = await browser.newPage({ viewport: { width: 844, height: 390 }, isMobile: true, hasTouch: true });
  touch.on('pageerror', error => errors.push(error.message));
  await touch.goto(`${url}/?scene=autumn&fresh&quality=low&nopad`);
  await touch.tap('#start-normal');
  await touch.waitForFunction(() => window.lab?.ready, null, { timeout: 120000 });
  await touch.evaluate(() => {
    const L = window.lab, { z } = L.player.feet;
    const foe = L.hunt.enemies.find(e => e.seg === 0 && !e.duel);
    Object.assign(foe, { x: 0, z: z - 2.8, wait: 1e4, aggro: 0 });
    L.teleport(0, z, 0, z - 1);
  });
  await touch.tap('#touch-lock');
  await touch.waitForFunction(() => window.lab.hunt.target);
  assert(await touch.locator('#touch-next').isVisible());
  await touch.tap('#touch-evade');
  await touch.waitForFunction(() => document.getElementById('touch-evade').disabled);
  await touch.close();
  assert.deepEqual(errors, []);
  console.log('PASS: target lock and cycling, civilian filtering, timed guard, counter hit, quickstep, cover, advancing cut and touch controls');
} finally {
  await browser.close();
}
