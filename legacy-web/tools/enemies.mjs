// Verifies tactical decisions and their actual skeletal clips in the production game.
import assert from 'node:assert/strict';
import { chromium } from 'playwright-core';
import { guard } from './no-pointer-lock.mjs';

guard(chromium);
const url = process.argv[2] ?? 'http://127.0.0.1:5405';
const browser = await chromium.launch({ executablePath: process.env.CHROME, headless: true,
  args: ['--mute-audio', '--enable-gpu', '--ignore-gpu-blocklist'] });
const errors = [];
try {
  const page = await browser.newPage({ viewport: { width: 960, height: 540 } });
  page.on('pageerror', error => errors.push(error.message));
  page.on('console', msg => { if (msg.type() === 'error') errors.push(msg.text()); });
  await page.goto(`${url}/?scene=autumn&mode=roam&quality=low&nopad`);
  await page.waitForFunction(() => window.lab?.ready, null, { timeout: 120000 });
  const tactics = await page.evaluate(() => {
    const L = window.lab, H = L.hunt, { z } = L.player.feet;
    L.renderer.setAnimationLoop(null);
    const selected = Object.fromEntries(['punk', 'enforcer', 'blade', 'brute', 'gunner'].map(kind => [kind, H.enemies.find(e => e.kind === kind)]));
    const base = { move: { x: 0, y: 0 }, look: { x: 0, y: 0 }, zoom: 0, device: 'keyboard' };
    const step = (n = 1) => L.step(1 / 60, n, false);
    const put = (e, x, dz, fields = {}) => Object.assign(e, { x, z: z + dz, homeX: x, homeZ: z + dz, tx: x, tz: z + dz, yaw: 0,
      hp: e.maxHp, mode: 'chase', aggro: 15, cool: 0, windup: 0, recover: 0, defend: 0, defendCool: 0, stagger: 0, air: 0, vy: 0, kx: 0, kz: 0, attackCount: 0, reprisal: false, ...fields });
    const park = e => put(e, 220, 0, { aggro: 0, mode: 'roam', wait: 1e4 });
    for (const e of H.enemies) if (!e.duel) park(e);
    for (const [i, v] of L.rides.rides.entries()) {
      v.traffic = null; v.x = 240; v.z = z - i * 7; v.y = L.heightAt(v.x, v.z); v.vx = v.vz = 0; v.dirty = true;
      v.home = { x: v.x, z: v.z, yaw: v.yaw };
    }
    const { punk, enforcer, blade, gunner } = selected;
    const casts = Object.fromEntries(Object.entries(selected).map(([kind, e]) => [kind, e.mesh.userData.clips.Alternate.tracks.length]));

    L.setInput(() => base);
    L.teleport(0, z, 0, z - 1);
    put(punk, 0, -1.5); put(enforcer, 2.3, -2.3); put(blade, -3.1, -3.2);
    step(12);
    const flank = { swinging: [punk, enforcer, blade].filter(e => e.windup > 0).length,
      moved: Math.max(Math.hypot(enforcer.x - 2.3, enforcer.z - z + 2.3), Math.hypot(blade.x + 3.1, blade.z - z + 3.2)) };
    park(punk); park(blade); park(enforcer);
    step(55);

    L.teleport(0, z, 0, z - 1);
    put(enforcer, 0, -2.1, { cool: 2 });
    let n = 0;
    L.setInput(() => ({ ...base, attack: n++ === 0 }));
    step(29);
    const block = { defending: enforcer.defending, clip: enforcer.mesh.userData.clip, damage: enforcer.maxHp - enforcer.hp, reprisal: enforcer.reprisal };
    enforcer.cool = 0;
    L.setInput(() => base);
    step(24);
    const bash = enforcer.windup > 0 && enforcer.attackIndex === 1 && enforcer.mesh.userData.clip === 'Alternate';
    park(enforcer);
    step(60);

    L.teleport(0, z, 0, z - 1);
    put(blade, 0, -2.6, { cool: 2 });
    n = 0;
    L.setInput(() => ({ ...base, attack: n++ === 0 }));
    step(19);
    const dodge = { mode: blade.defending, clip: blade.mesh.userData.clip, sidestep: Math.abs(blade.x), damage: blade.maxHp - blade.hp };
    park(blade);
    L.setInput(() => base);
    step(35);

    put(gunner, 0, -1.75); step();
    const close = { attack: gunner.attackIndex, clip: gunner.mesh.userData.clip };
    const P = L.player;
    const cover = P.world.createCollider(P.R.ColliderDesc.cuboid(1, 2, 0.25).setTranslation(0, 1, z - 4.5));
    P.world.step();
    put(gunner, 0, -8); step();
    const coverRead = gunner.windup === 0 && gunner.laneStep > 0;
    P.world.removeCollider(cover, true);
    P.world.step();
    put(gunner, 0, -8, { laneStep: 0 }); step();
    const ranged = { attack: gunner.attackIndex, clip: gunner.mesh.userData.clip, laser: gunner.aim.visible };
    return { flank, block, bash, dodge, close, ranged, casts, coverRead };
  });
  assert.equal(tactics.flank.swinging, 1, 'only the melee lead commits');
  assert(tactics.flank.moved > 0.25, 'support moves to the flank');
  assert.equal(tactics.block.defending, 'Guard');
  assert.equal(tactics.block.clip, 'Guard');
  assert(tactics.block.damage > 0 && tactics.block.damage < 12 && tactics.block.reprisal, JSON.stringify(tactics.block));
  assert(tactics.bash, 'the enforcer follows a block with its shield bash');
  assert.equal(tactics.dodge.mode, 'Evade');
  assert(tactics.dodge.clip.startsWith('Evade') && tactics.dodge.sidestep > 0.4 && tactics.dodge.damage === 0, JSON.stringify(tactics.dodge));
  assert.deepEqual(tactics.close, { attack: 1, clip: 'Alternate' });
  assert(tactics.coverRead, 'a gunner relocates when cover blocks the shot');
  assert.deepEqual(tactics.ranged, { attack: 0, clip: 'Attack', laser: true });
  assert(Object.values(tactics.casts).every(n => n >= 6), 'each enemy has a working alternate skeletal attack');
  assert.deepEqual(errors, []);
  console.log('PASS: melee coordination and flanking, guard and riposte setup, animated evade, ranged and close patterns, all five rigs');
} finally {
  await browser.close();
}
