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
const browser = await chromium.launch({ executablePath: process.env.CHROME, headless: true,
  args: ['--mute-audio', '--ignore-gpu-blocklist', ...(process.platform === 'win32' ? ['--use-angle=d3d11'] : process.platform === 'darwin' ? ['--use-angle=metal'] : []), '--enable-gpu'] });
const errors = [];
try {
  const page = await browser.newPage({ viewport: { width: 1024, height: 640 } });
  page.on('pageerror', e => errors.push(e.message));
  page.on('console', m => { if (m.type() === 'error') errors.push(m.text()); });
  await page.goto(`${baseURL}/?scene=autumn&fresh&quality=low&nopad`);
  await page.waitForFunction(() => window.lab?.ready, null, { timeout: 120000 });
  await page.waitForFunction(() => {
    const L = window.lab;
    const blades = [L.figure.man.object.getObjectByName('Traveler katana'), ...L.hunt.enemies.filter(e => e.duel).map(e => e.mesh.getObjectByName('Rival katana'))];
    return blades.every(blade => blade?.children.length === 1 && blade.children[0].isGroup);
  }, null, { timeout: 15000 });
  const result = await page.evaluate((plan) => {
    window.__paused = true;
    const L = window.lab, P = L.player, h = L.hunt;
    // Rendering was warmed up at startup. The teleports below still update the camera and world
    // but do not pile hundreds of redundant frames onto software WebGL during this synchronous run.
    const render = L.frame.render;
    L.frame.render = () => {};
    const base = { move: { x: 0, y: 0 }, look: { x: 0, y: 0 }, walk: false, sprint: false, strafe: false, jump: false, jumpHeld: true, zoom: 0, device: 'keyboard' };
    const input = state => L.setInput(() => ({ ...base, ...state }));
    const check = (condition, message) => { if (!condition) throw Error(message); };
    const results = {};
    const Z0 = plan[0].checkpoint.z; // the start: every fixed place below is measured from it, whatever the map's scale
    // This run measures combat and progression. The street's people are moved out of it; tools/street.mjs is theirs.
    for (const e of h.enemies) if (e.civ) { if (!e.mark) e.x = e.homeX = e.tx = 240; e.wait = 1e4; }
    // A second crew member must not hit during the single-attacker dodge check. Traffic is tested in street.mjs.
    for (const e of h.enemies) if (!e.duel) { e.aggro = 0; e.mode = 'roam'; e.wait = 1e4; e.windup = 0; }
    for (const [i, v] of L.rides.rides.entries()) if (v.traffic) {
      v.traffic = null; v.x = 230; v.z = 100 - i * 7; v.y = L.heightAt(v.x, v.z); v.vx = v.vz = v.vf = 0;
    }
    check(L.figure.avatar.bindings.length >= 40, 'anime humanoid bindings');
    check(h.enemies.filter(e => e.duel).length === plan.length, 'one mirror rival per chapter');
    check(!!L.missions && L.missions.sectorOps.length === plan.length, 'six sector missions created');
    check(!!L.audio && typeof L.audio.setSettings === 'function', 'audio engine active');
    L.audio.setSettings({ master: 0.65, music: 0.4, voice: 0.9, sfx: 0.8, muted: false });
    check(Math.abs(JSON.parse(localStorage.getItem('oilpaint.audio.v1')).master - 0.65) < 0.01, 'audio settings persist');
    // Verify the starting bike is unlocked and does not eject the rider during sharp turns.
    const starterBike = L.rides.rides.find(v => v.kind === 'bike' && !v.traffic);
    check(starterBike && h.unlocked('bike'), 'starter bike unlocked from sector 0');
    L.teleport(starterBike.x + 1.2, starterBike.z, starterBike.x, starterBike.z);
    let rodeFrame = 0;
    L.setInput(() => ({ ...base, ride: rodeFrame++ === 0 }));
    L.step(1 / 60, 2);
    check(L.rides.driven === starterBike, 'can board starter bike at sector 0');
    Object.assign(starterBike, { x: -20, z: 305, y: L.heightAt(-20, 305), yaw: Math.PI / 2, vx: 0, vz: 0, vf: 0 });
    input({ move: { x: 1, y: 1 } });
    L.step(1 / 60, 180);
    check(L.rides.driven === starterBike && Math.abs(starterBike.lean) > 0.3, 'sharp bike cornering leans without kicking rider off');
    L.rides.leave(true);
    Object.assign(starterBike, { x: starterBike.home.x, z: starterBike.home.z, y: L.heightAt(starterBike.home.x, starterBike.home.z), yaw: starterBike.home.yaw, vx: 0, vz: 0, vf: 0, tilt: 0, rollRate: 0, tipped: false });
    L.teleport(0, Z0, 0, Z0 - 18); input({ move: { x: 0, y: 1 } }); L.step(1 / 60, 30);
    results.runDistance = Z0 - P.feet.z; check(results.runDistance > 1, 'responsive running');
    input({}); L.step(1 / 60, 20); check(P.speed < 0.05, 'release stops');
    function jump(held) {
      L.teleport(0, Z0, 0, Z0 - 18); const y = P.feet.y; let peak = y, n = 0;
      L.setInput(() => { peak = Math.max(peak, P.feet.y); return { ...base, jump: n++ === 0, jumpHeld: held }; });
      L.step(1 / 60, 80); check(P.grounded, 'landed'); return peak - y;
    }
    results.holdJump = jump(true); results.tapJump = jump(false);
    check(results.holdJump > 1 && results.tapJump < results.holdJump * 0.8, 'variable jumps');
    P.teleport(0, L.heightAt(0, Z0) + 0.08, Z0); P.grounded = false; P.coyote = 0; P.mode = 'air'; P.vel.y = -1;
    let frame = 0, bufferedLaunch = false;
    L.setInput(() => { bufferedLaunch ||= P.vel.y > 2; return { ...base, jump: frame++ === 0 }; });
    L.step(1 / 60, 20); check(bufferedLaunch, 'jump buffered before landing');
    P.teleport(0, L.heightAt(0, Z0) + 0.08, Z0); P.grounded = false; P.coyote = 0.08;
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
    results.fieldOps = [];
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
      check(!rival.ready, 'field operation gates Ren in sector ' + segment);
      const op = L.missions.sectorOps[segment];
      for (const t of op.targets) {
        if (t.kind === 'destructible') {
          L.teleport(t.x, t.z + 2.1, t.x, t.z);
          for (let attempt = 0; attempt < 3 && !t.done; attempt++) {
            let frame = 0;
            L.setInput(() => ({ ...base, attack: frame++ === 0 }));
            L.step(1 / 60, 65, false);
          }
        } else if (t.kind === 'courier') {
          const courier = h.enemies.find(e => e.mark);
          check(courier && !courier.dead, 'marked courier is live');
          for (let attempt = 0; attempt < 3 && !courier.dead; attempt++) {
            L.teleport(courier.x, courier.z + 1.9, courier.x, courier.z);
            let frame = 0;
            L.setInput(() => ({ ...base, attack: frame++ === 0 }));
            L.step(1 / 60, 65, false);
          }
        } else {
          input({});
          L.teleport(t.x, t.z + (t.kind === 'terminal' ? 2.1 : 0), t.x, t.z - 1);
          L.step(1 / 60, t.kind === 'terminal' ? 130 : 1, false);
        }
        check(t.done, 'field target complete: ' + t.label);
      }
      input({}); L.step(1 / 60, 2, false);
      check(op.done && rival.ready, 'completed operation activates Ren');
      check(JSON.parse(localStorage.getItem('oilpaint.levels.v1')).missions.includes(segment), 'operation saves before the duel');
      results.fieldOps.push(op.type);
      for (const shrine of L.missions.echoShrines.filter(s => s.seg === segment)) {
        L.teleport(shrine.x, shrine.z + 1.7, shrine.x, shrine.z);
        check(shrine.collected && !shrine.root.visible, 'recovered shrine ' + shrine.id);
      }
      L.teleport(rival.x, rival.z + 4.5, rival.x, rival.z); L.step(1 / 60, 60, false);
      check(rival.aura.visible, 'Ren shows his arena aura in a live duel');
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
    check(results.save.cleared === plan.length && results.save.won === true, 'victory persists');
    check(L.missions.echoes.collected === 12 && JSON.parse(localStorage.getItem('oilpaint.lore.v1')).length === 12, 'all twelve Echo Shards persist');
    // After winning, create a temporary wall in open space to test the actual capsule sweep.
    L.teleport(0, Z0, 0, Z0 - 18); input({}); const y = P.feet.y;
    const wall = P.world.createCollider(P.R.ColliderDesc.cuboid(3, 2, 0.2).setTranslation(0, y + 1, Z0 - 2.5));
    P.world.step(); check(P.startDash(), 'ground dash starts'); L.step(1 / 60, 30);
    results.dashEndZ = P.feet.z;
    check(P.feet.z > Z0 - 2.1 && P.feet.z < Z0 - 1, 'dash stops at the wall');
    check(Math.abs(P.visualY - P.feet.y) < 0.2, 'dash visual history');
    P.world.removeCollider(wall, true); L.setInput(null);
    L.frame.render = render;
    return results;
  }, PLAN.map(p => ({...p, goal: GOAL})));
  // Pause freezes the real animation loop, and its controls remain keyboard accessible.
  await page.click('#pause-button');
  assert.equal(await page.locator('#menu').evaluate(el => el.open), true);
  await page.click('#codex-toggle');
  assert.equal(await page.locator('#codex-acts .codex-status.done').count(), PLAN.length);
  await page.click('[data-codex-tab="cast"]');
  assert.equal(await page.locator('#codex-cast .dossier-portrait').count(), 5);
  await page.click('[data-codex-tab="echoes"]');
  assert.equal(await page.locator('#codex-echoes .echo-voice-btn').count(), 12);
  const before = await page.evaluate(() => window.lab.shared.uTime.value);
  const firstPhase = await page.evaluate(() => window.lab.dayNight.phase);
  await page.click('#daynight-toggle');
  assert.notEqual(await page.evaluate(() => window.lab.dayNight.phase), firstPhase, 'menu instantly changes lighting while paused');
  assert.equal(await page.evaluate(() => window.lab.shared.uNight.value), firstPhase === 'day' ? 1 : 0);
  await page.waitForTimeout(150);
  assert.equal(await page.evaluate(() => window.lab.shared.uTime.value), before);
  await page.keyboard.press('Escape');
  assert.equal(await page.locator('#menu').evaluate(el => el.open), false);
  // Restore an existing checkpoint without using ?fresh. Keep the same storage context.
  await page.goto(`${baseURL}/?scene=autumn&quality=low&nopad`);
  await page.waitForFunction(() => window.lab?.ready, null, { timeout: 120000 });
  assert.equal(await page.evaluate(() => window.lab.hunt.seg), PLAN.length);
  assert.equal(await page.evaluate(() => window.lab.hunt.won), true, 'restored game keeps its sanctuary victory');
  assert.equal(await page.evaluate(() => window.lab.missions.echoes.collected), 12, 'restored archive contains every recovered shard');
  const saved = await page.evaluate(() => localStorage.getItem('oilpaint.levels.v1'));
  await page.goto(`${baseURL}/?scene=autumn&mode=roam&phase=day&tour=1&quality=low&nopad`);
  await page.waitForFunction(() => window.lab?.ready, null, { timeout: 120000 });
  assert.equal(await page.evaluate(() => Object.values(window.lab.hunt.powers).every(Boolean)), true);
  assert.equal(await page.evaluate(() => window.lab.missions.sectorOps[1].targets[1].col), null, 'roam has no hidden terminal collision');
  assert.equal(await page.evaluate(() => localStorage.getItem('oilpaint.levels.v1')), saved, 'roam preserves story save');
  assert.equal(await page.locator('#showcase-overlay').isVisible(), true);
  assert.equal(await page.locator('.showcase-dot').count(), 6);
  const glides = await page.evaluate(() => {
    const L = window.lab;
    L.renderer.setAnimationLoop(null);
    const start = L.follow.free.position.clone(), playerStart = L.player.feet.clone();
    const health = L.hunt.hp, remaining = L.dayNight.remaining;
    L.step(1 / 60, 32, false);
    return L.follow.free.position.distanceTo(start) > 0.3 && L.player.feet.equals(playerStart)
      && L.hunt.hp === health && L.dayNight.remaining < remaining;
  });
  assert.equal(glides, true, 'tour moves the camera while holding the solo run');
  await page.locator('.showcase-next').click();
  assert.equal(await page.evaluate(() => window.lab.showcase.index), 1);
  assert.equal(await page.evaluate(() => window.lab.shared.uNight.value), 0);
  await page.locator('.showcase-dot').last().click();
  assert.equal(await page.evaluate(() => window.lab.showcase.index), 5);
  assert.equal(await page.evaluate(() => window.lab.shared.uNight.value), 1);
  await page.keyboard.press('Escape');
  assert.equal(await page.locator('#showcase-overlay').isHidden(), true);
  assert.equal(await page.evaluate(() => window.lab.dayNight.phase), 'day', 'exit releases the lighting preview');
  await page.goto(`${baseURL}/?scene=autumn&fresh&quality=low&nopad`);
  await page.waitForFunction(() => window.lab?.ready, null, { timeout: 120000 });
  assert.equal(await page.evaluate(() => window.lab.missions.echoes.collected), 12, 'fresh checkpoints preserve the archive');
  const warning = await page.evaluate(() => {
    window.__paused = true;
    Storage.prototype.setItem = () => { throw new Error('storage denied'); };
    window.lab.missions?.completeSector(0);
    for (const e of window.lab.hunt.enemies) if (e.seg === 0) e.dead = 0.001;
    window.lab.step(1 / 60, 2);
    return document.querySelector('.toast').textContent;
  });
  assert.match(warning, /Progress is not saving/, 'save failure remains visible after unlock');
  await page.goto(`${baseURL}/?scene=aurora&quality=low&nopad`);
  await page.waitForFunction(() => window.lab?.ready && window.lab.lantern.object.getObjectByName('Spirit lantern'), null, { timeout: 120000 });
  assert.match(await page.locator('.chapter-label').textContent(), /Memory 00/);
  assert.match(await page.locator('.story p').textContent(), /ridge above Old Kyoto/i);
  await page.evaluate(() => {
    const L = window.lab, z = 212, x = L.crestX(z);
    L.teleport(x, z, x, z - 1);
  });
  assert.match(await page.locator('.story p').textContent(), /first stones at the timberline/);
  assert.deepEqual(errors, [], 'browser errors');
  console.log(JSON.stringify(result, null, 2));
  console.log(`PASS: Blender weapons and lantern, physics, combat, ${PLAN.length} field operations and duels, 12 Echoes, codex, lighting preview, showcase tour, save/restore, roam and Snowbound`);
} finally {
  await browser.close();
}
