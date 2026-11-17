// Proves the reviewed art is active, the people really animate, and graphics controls resize both
// the canvas and its finish target without restarting a run. Timings are simulated only for the
// adaptation assertions; tools/fps.mjs measures real GPU performance separately.
import assert from 'node:assert/strict';
import { mkdirSync } from 'node:fs';
import { chromium } from 'playwright-core';
import { guard } from './no-pointer-lock.mjs';
guard(chromium);
const url = process.argv[2] ?? 'http://127.0.0.1:5405';
const shots = process.argv.includes('--shots') ? process.argv[process.argv.indexOf('--shots') + 1] : null;
if (shots) mkdirSync(shots, { recursive: true });
const browser = await chromium.launch({ executablePath: process.env.CHROME, headless: true,
  args: ['--mute-audio', '--ignore-gpu-blocklist', ...(process.platform === 'win32' ? ['--use-angle=d3d11'] : process.platform === 'darwin' ? ['--use-angle=metal'] : []), '--enable-gpu'] });
const errors = [];
try {
  const page = await browser.newPage({ viewport: { width: 1440, height: 900 }, deviceScaleFactor: 2 });
  page.on('pageerror', e => errors.push(e.message));
  page.on('console', m => { if (m.type() === 'error') errors.push(m.text()); });
  await page.goto(`${url}/?scene=autumn&mode=roam&quality=high&nopad`);
  await page.waitForFunction(() => window.lab?.ready, null, { timeout: 120000 });
  const art = await page.evaluate(() => {
    window.__paused = true;
    const L = window.lab;
    const names = [...new Set(L.street.placements.map(p => p.name))];
    const vehicles = L.rides.rides.map(v => ({ kind: v.kind, asset: v.root.userData.asset, wheels: v.wheels.length, rider: !!v.rider }));
    const people = L.hunt.enemies.filter(e => e.civ);
    const meshes = [];
    people[0].mesh.traverse(o => { if (o.isSkinnedMesh) meshes.push(o); });
    const u = people[0].mesh.userData, mixer = u.mixer;
    mixer.stopAllAction();
    mixer.clipAction(u.clips.Walk).reset().play(); mixer.setTime(0);
    const before = meshes[0].skeleton.bones.map(b => b.quaternion.clone());
    mixer.setTime(0.25);
    const animated = meshes[0].skeleton.bones.some((b, i) => before[i].angleTo(b.quaternion) > 0.01);
    mixer.stopAllAction(); u.action = mixer.clipAction(u.clips.Idle).reset().play(); mixer.setTime(0);
    for (const e of L.hunt.enemies) { if (!e.duel) { e.aggro = 0; e.angry = 0; e.wait = 1e4; e.cool = 99; } }
    window.graphicsRunToken = Math.random();
    return { names, placements: L.street.placements.length, vehicles, animated, people: people.length, token: window.graphicsRunToken };
  });
  assert.deepEqual(art.names.sort(), ['barricade', 'beacon', 'power-core', 'supply-crate', 'terminal']);
  assert(art.placements >= 20 && art.people >= 24 && art.animated, 'props, crowd and a deforming walk clip');
  for (const kind of ['car', 'sports', 'pickup', 'bike', 'bicycle']) {
    const v = art.vehicles.find(v => v.kind === kind);
    assert(v?.asset && v.wheels === (kind === 'bike' || kind === 'bicycle' ? 2 : 4), kind + ' imported body and wheels');
  }
  if (shots) {
    await page.evaluate(() => {
      const L = window.lab, p = L.street.placements.find(p => p.name === 'terminal');
      document.documentElement.dataset.clean = '';
      L.teleport(p.x - 3, p.z + 5, p.x, p.z);
      L.view([p.x + 7, p.y + 4, p.z + 7], [p.x - 4, p.y + 1, p.z - 1]);
      L.step(1 / 60, 3);
    });
    await page.screenshot({ path: `${shots}/city-props.png` });
    await page.evaluate(() => {
      const L = window.lab, v = L.rides.rides.find(v => v.kind === 'bike');
      const base = { move: { x: 0, y: 0 }, look: { x: 0, y: 0 }, jumpHeld: false, zoom: 0, device: 'keyboard' };
      L.teleport(v.x + 1.1, v.z, v.x, v.z);
      let n = 0; L.setInput(() => ({ ...base, ride: n++ === 0 })); L.step(1 / 60, 1);
      L.setInput(() => base);
      const f = { x: Math.sin(v.yaw), z: Math.cos(v.yaw) }, r = { x: f.z, z: -f.x };
      L.view([v.x + f.x * 3.5 + r.x * 4, v.y + 2.7, v.z + f.z * 3.5 + r.z * 4], [v.x, v.y + 0.9, v.z]);
      L.step(1 / 60, 3);
      if (L.rides.driven !== v || !v.rider.visible) throw Error('the seated rider appears on entering the bike');
    });
    await page.screenshot({ path: `${shots}/bike-rider.png` });
    await page.evaluate(() => {
      const L = window.lab;
      L.rides.leave(true);
      const x = 8, z = 149, y = L.heightAt(x, z);
      const people = [L.hunt.enemies.find(e => e.kind === 'civ'), L.hunt.enemies.filter(e => e.kind === 'civ')[1], L.hunt.enemies.find(e => e.kind === 'thief')];
      people.forEach((e, i) => { e.x = e.homeX = e.tx = x + (i - 1) * 1.8; e.z = e.homeZ = e.tz = z; e.yaw = 0; e.wait = 1e4; });
      L.teleport(x, z + 6, x, z);
      L.view([x + 4, y + 2.5, z + 7], [x, y + 0.9, z]); L.step(1 / 60, 3);
    });
    await page.screenshot({ path: `${shots}/street-npcs.png` });
    await page.evaluate(() => { delete document.documentElement.dataset.clean; });
  }
  await page.click('#pause-button');
  await page.selectOption('#quality', 'low');
  let state = await page.evaluate(() => {
    const L = window.lab, size = L.renderer.getDrawingBufferSize(new L.THREE.Vector2());
    return { ...L.graphics.status, samples: L.frame.target.samples, aligned: size.equals(L.frame.uniforms.uSize.value), token: window.graphicsRunToken };
  });
  assert.equal(state.level, 'low'); assert(state.pixelRatio <= 0.8 && state.samples === 0 && state.aligned);
  assert.equal(state.token, art.token, 'changing graphics keeps the same run');
  await page.selectOption('#quality', 'high');
  await page.evaluate(() => { for (let i = 0; i < 600; i++) window.lab.graphics.sample(0.05); });
  assert.equal(await page.evaluate(() => window.lab.graphics.status.level), 'high', 'manual choice stays fixed');
  await page.selectOption('#quality', 'auto');
  const adaptive = await page.evaluate(() => {
    const g = window.lab.graphics;
    for (let i = 0; i < 600; i++) g.sample(0.05);
    const slow = g.status.level;
    for (let i = 0; i < 3000; i++) g.sample(1 / 60);
    return { slow, recovered: g.status.level };
  });
  assert.equal(adaptive.slow, 'low'); assert.equal(adaptive.recovered, 'high');
  for (const width of [1440, 768, 390]) {
    await page.setViewportSize({ width, height: width === 390 ? 844 : 900 });
    await page.locator('#quality').scrollIntoViewIfNeeded();
    assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), 'menu fits at ' + width);
    if (shots) await page.screenshot({ path: `${shots}/graphics-menu-${width}.png` });
  }
  await page.close();
  const night = await browser.newPage({ viewport: { width: 1440, height: 900 }, deviceScaleFactor: 1 });
  night.on('pageerror', e => errors.push(e.message));
  night.on('console', m => { if (m.type() === 'error') errors.push(m.text()); });
  await night.goto(`${url}/?scene=aurora&quality=auto&nopad`);
  await night.waitForFunction(() => window.lab?.ready, null, { timeout: 120000 });
  await night.click('#pause-button'); await night.selectOption('#quality', 'medium');
  const width = await night.evaluate(() => window.lab.frame.target.width);
  await night.selectOption('#quality', 'high');
  assert.equal(await night.evaluate(() => window.lab.frame.target.width), width, 'MSAA can change without changing the pixel dimensions');
  assert.equal(await night.evaluate(() => window.lab.frame.target.samples), 4);
  await night.selectOption('#quality', 'low');
  assert.equal(await night.evaluate(() => window.lab.graphics.status.level), 'low', 'Snowbound uses the shared graphics controls');
  assert.deepEqual(errors, [], 'no shader, loading or browser errors');
  console.log(JSON.stringify({ props: art.placements, people: art.people, animated: art.animated, adaptive, menuWidths: [1440, 768, 390] }));
  console.log('PASS: reviewed models, animated NPCs, seated rider, live graphics controls and adaptation in both scenes');
} finally { await browser.close(); }
