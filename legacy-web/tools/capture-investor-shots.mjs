// Captures 8 high-resolution (1600x900) Investor Showcase screenshots into shots/:
//   CHROME=/usr/bin/google-chrome node tools/capture-investor-shots.mjs [http://127.0.0.1:5415] [--prologue]
import assert from 'node:assert/strict';
import { createReadStream, existsSync, mkdirSync, statSync } from 'node:fs';
import { createServer } from 'node:http';
import { extname, join, normalize, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { chromium } from 'playwright-core';
import { guard } from './no-pointer-lock.mjs';

guard(chromium);
const here = resolve(fileURLToPath(import.meta.url), '..', '..');
const outDir = resolve(here, 'shots');
mkdirSync(outDir, { recursive: true });

const argUrl = process.argv.slice(2).find((a) => a.startsWith('http'));
const prologueOnly = process.argv.includes('--prologue');
let server = null;
let baseURL = argUrl;

if (!baseURL) {
  const root = resolve(here, 'dist');
  const types = {
    '.html': 'text/html; charset=utf-8', '.js': 'text/javascript', '.mjs': 'text/javascript',
    '.css': 'text/css', '.json': 'application/json', '.svg': 'image/svg+xml', '.jpg': 'image/jpeg',
    '.png': 'image/png', '.ogg': 'audio/ogg', '.m4a': 'audio/mp4', '.wasm': 'application/wasm',
    '.glb': 'model/gltf-binary', '.vrm': 'model/gltf-binary', '.mp4': 'video/mp4',
  };
  server = createServer((req, res) => {
    const pathname = decodeURIComponent(new URL(req.url, 'http://x').pathname);
    let file = normalize(join(root, pathname));
    if (existsSync(file) && statSync(file).isDirectory()) file = join(file, 'index.html');
    if (!file.startsWith(root) || !existsSync(file)) {
      res.writeHead(404).end('not found');
      return;
    }
    res.writeHead(200, { 'Content-Type': types[extname(file).toLowerCase()] ?? 'application/octet-stream', 'Cache-Control': 'no-store' });
    createReadStream(file).pipe(res);
  });
  await new Promise((r) => server.listen(0, '127.0.0.1', r));
  baseURL = `http://127.0.0.1:${server.address().port}`;
}

const browser = await chromium.launch({
  executablePath: process.env.CHROME,
  headless: true,
  args: [
    '--mute-audio',
    '--ignore-gpu-blocklist',
    ...(process.platform === 'win32' ? ['--use-angle=d3d11'] : process.platform === 'darwin' ? ['--use-angle=metal'] : []),
    '--enable-gpu',
  ],
});

const errors = [];
const context = await browser.newContext({ viewport: { width: 1600, height: 900 }, deviceScaleFactor: 1 });

async function openScene(query) {
  const page = await context.newPage();
  page.on('pageerror', (e) => errors.push(`pageerror (${query}): ${e.message}`));
  page.on('console', (m) => { if (m.type() === 'error') errors.push(`console.error (${query}): ${m.text()}`); });
  await page.goto(`${baseURL}/${query}`);
  await page.waitForFunction(() => window.lab?.ready, null, { timeout: 120000 });
  await page.evaluate(() => {
    const L = window.lab;
    L.renderer.setAnimationLoop(null);
    L.graphics.setMode('high');
    // Native 1:1 1600x900 buffer with 2x MSAA keeps every silhouette crisp without exhausting headless VRAM
    L.renderer.setPixelRatio(1);
    L.renderer.setSize(innerWidth, innerHeight);
    L.frame.setSamples(Math.min(2, L.renderer.capabilities.maxSamples));
    L.frame.resize();
  });
  return page;
}

async function saveShot(page, filename) {
  const lost = await page.evaluate(() => {
    const L = window.lab;
    if (L.scene) L.frame.render(L.scene, L.camera);
    else L.step(1 / 240, 1, true);
    const gl = L.renderer.getContext();
    gl.finish();
    return gl.isContextLost();
  });
  assert.equal(lost, false, `WebGL context lost before capturing ${filename}`);
  const targetPath = join(outDir, filename);
  await page.screenshot({ path: targetPath, timeout: 90_000 });
  const sizeKB = Math.round(statSync(targetPath).size / 1024);
  assert.ok(sizeKB > 180, `Screenshot ${filename} unexpectedly small (${sizeKB} KB)`);
  console.log(`captured ${filename} (${sizeKB} KB)`);
}

async function captureCity() {
  // --- 01: Kael with glowing Cyber-Katana & HUD + Sora Radio Comms in Sunleaf Row at night ---
  {
    const page = await openScene('?scene=autumn&phase=night&fresh&nopad');
    await page.waitForFunction(() => {
      const sword = window.lab?.figure?.man?.object?.getObjectByName('Traveler katana');
      return sword && sword.children.length === 1;
    }, null, { timeout: 15000 });
    await page.evaluate(() => {
      const L = window.lab, h = L.hunt;
      document.getElementById('hint')?.remove();
      document.documentElement.removeAttribute('data-clean');
      for (const e of h.enemies) {
        if (!e.duel) {
          if (e.civ) { e.x = e.homeX = e.tx = 240; }
          e.aggro = 0; e.mode = 'roam'; e.wait = 1e4; e.windup = 0;
        }
      }
      const base = { move: { x: 0, y: 0 }, look: { x: 0, y: 0 }, walk: false, sprint: false, strafe: false, jump: false, jumpHeld: false, zoom: 0, device: 'keyboard' };
      const kx = -1.6, kz = 216.0;
      L.teleport(kx, kz, kx + 1.4, kz + 4.2);
      L.dayNight.setPhase('night', L.shared.uTime.value);
      L.syncDayNight(10);
      let n = 0;
      L.setInput(() => ({ ...base, attack: n++ === 0 }));
      L.step(1 / 60, 17, true);
      L.setInput(() => base);
      const gy = L.heightAt(kx, kz);
      L.view([kx + 1.65, gy + 1.42, kz + 3.15], [kx - 0.55, gy + 1.48, kz - 6.0]);
      h.hud.comms('sora', 'Ghost-Link online, Courier Zero-Seven. Carry Dr. Mira’s Komorebi Seed past Aethelgard’s jammers—wake Sunleaf Row!');
      L.step(1 / 120, 1, true);
      for (const sel of ['.chapter-banner', '.target', '.waypoint']) {
        const el = document.querySelector(sel);
        if (el) el.hidden = true;
      }
    });
    await page.waitForTimeout(150);
    await saveShot(page, 'aa-01-neon-night-hero.png');
    await page.close();
  }

  // --- 02, 03, 04, 06: Roam world showcases (Daybreak Skyline, Fleet & Ramen Stall, Factions & Mech, Echo Shrine) ---
  {
    const page = await openScene('?scene=autumn&phase=day&mode=roam&nopad&clean');

    // 02: Sunlit Neo-Kyoto skyline, crosswalks, towers & lush park trees (?scene=autumn&phase=day)
    await page.evaluate(() => {
      const L = window.lab, R = L.rides, h = L.hunt;
      L.dayNight.setPhase('day', L.shared.uTime.value);
      L.syncDayNight(10);
      const gt = R.rides.find((v) => v.kind === 'sports');
      if (gt) {
        gt.traffic = null;
        Object.assign(gt, { x: -38.4, z: 86.5, y: L.heightAt(-38.4, 86.5), yaw: 0, vx: 0, vz: 0, vf: 0, dirty: true });
      }
      const civs = h.enemies.filter((e) => e.kind === 'civ');
      if (civs[0]) { civs[0].x = -44.2; civs[0].z = 79.8; civs[0].yaw = Math.PI / 2; civs[0].wait = 1e4; civs[0].mesh.visible = true; }
      if (civs[1]) { civs[1].x = -34.2; civs[1].z = 83.5; civs[1].yaw = 0; civs[1].wait = 1e4; civs[1].mesh.visible = true; }

      L.teleport(-38.0, 82.0, -40, 110);
      const eye = [-36.2, L.heightAt(-36.2, 60.5) + 5.6, 60.5];
      const target = [-43.5, L.heightAt(-43.5, 118.0) + 7.2, 118.0];
      L.view(eye, target);
      L.step(1 / 60, 8, true);
      if (civs[0]) civs[0].mesh.visible = true;
      if (civs[1]) civs[1].mesh.visible = true;
      L.figure.man.object.visible = false;
      L.figure.cloakMesh.visible = false;
    });
    await saveShot(page, 'aa-02-daybreak-skyline.png');

    // 03: Fleet (Kestrel Coupe / GT / Razor Bike), Night-Market Ramen Stall & diverse human citizens
    await page.evaluate(() => {
      const L = window.lab, h = L.hunt, R = L.rides;
      document.documentElement.setAttribute('data-clean', '');
      L.dayNight.setPhase('night', L.shared.uTime.value);
      L.syncDayNight(10);
      const stalls = L.street.placements.filter((p) => p.name === 'noodle-stall');
      const stall = stalls[1] ?? stalls[0];
      const fx = Math.sin(stall.yaw), fz = Math.cos(stall.yaw);
      const rx = Math.cos(stall.yaw), rz = -Math.sin(stall.yaw);

      // Hide nearby checkpoint relay group so its pillar never overlaps the right side of the Ramen Stall plaza
      for (const m of L.scene.children) {
        if (m.isGroup && Math.hypot(m.position.x - stall.x, m.position.z - stall.z) < 16 && m !== L.street.group) {
          m.userData.__wasVis = m.visible;
          m.visible = false;
        }
      }
      for (const [i, v] of R.rides.entries()) {
        v.traffic = null; v.driver = false;
        if (v.rider) v.rider.visible = false;
        Object.assign(v, { x: 240, z: 120 - i * 8, y: 0, vx: 0, vz: 0, vf: 0, tilt: 0, lean: 0, steer: 0, dirty: true });
      }
      for (const e of h.enemies) {
        if (!e.duel) { e.x = 240; e.z = -180; e.aggro = 0; e.wait = 1e4; e.going = 0; e.windup = 0; }
      }

      const coupe = R.rides.find((v) => v.kind === 'car');
      const gt = R.rides.find((v) => v.kind === 'sports');
      const bike = R.rides.find((v) => v.kind === 'bike');
      const placeRide = (v, fOff, rOff, yawOff, lean = 0, steer = 0) => {
        if (!v) return;
        const vx = stall.x + fx * fOff + rx * rOff;
        const vz = stall.z + fz * fOff + rz * rOff;
        Object.assign(v, {
          x: vx, z: vz, y: L.heightAt(vx, vz),
          yaw: stall.yaw + yawOff, vx: 0, vz: 0, vf: 0,
          pitch: 0, roll: 0, tilt: 0, tipped: false, lean, steer, dirty: true,
        });
      };
      placeRide(bike, 4.1, -0.35, 0.85, 0.10, -0.22);
      placeRide(gt, 6.2, 2.35, -0.58, 0, 0.24);
      placeRide(coupe, 6.3, -3.15, 0.56, 0, -0.20);

      const civs = h.enemies.filter((e) => e.kind === 'civ');
      const courier = h.enemies.find((e) => e.kind === 'thief');
      const spots = [
        [civs[0], 1.65, -0.55, Math.PI + 0.15],
        [civs[1], 1.80, 0.55, Math.PI - 0.25],
        [civs[2], 3.15, 1.05, -0.30],
        [civs[3], 3.35, -1.75, 0.38],
        [courier, 4.85, 0.65, -0.22],
      ];
      for (const [actor, fOff, rOff, yawRel] of spots) {
        if (!actor) continue;
        actor.dead = 0; actor.gone = false;
        actor.x = actor.homeX = actor.tx = stall.x + fx * fOff + rx * rOff;
        actor.z = actor.homeZ = actor.tz = stall.z + fz * fOff + rz * rOff;
        actor.yaw = stall.yaw + yawRel;
        actor.wait = 1e4; actor.going = 0; actor.mesh.visible = true;
      }

      const camX = stall.x + fx * 11.8 - rx * 0.35;
      const camZ = stall.z + fz * 11.8 - rz * 0.35;
      const lookX = stall.x + fx * 3.4 - rx * 0.25;
      const lookZ = stall.z + fz * 3.4 - rz * 0.25;
      L.teleport(stall.x + fx * 4.5, stall.z + fz * 4.5, stall.x, stall.z);
      L.view([camX, L.heightAt(camX, camZ) + 2.55, camZ], [lookX, L.heightAt(lookX, lookZ) + 1.25, lookZ]);
      L.step(1 / 60, 4, true);
      for (const m of L.scene.children) {
        if (m.userData.__wasVis !== undefined) m.visible = false;
      }
      for (const [actor] of spots) if (actor?.mesh) actor.mesh.visible = true;
      L.figure.man.object.visible = false;
      L.figure.cloakMesh.visible = false;
      L.street.update({ x: stall.x, z: stall.z });
    });
    await saveShot(page, 'aa-03-fleet-and-ramen-stall.png');

    // 04: Aethelgard Titan Siege Mech & Cyber-Syndicate enemy squad in Ashen Hollow
    await page.evaluate(() => {
      const L = window.lab, h = L.hunt, R = L.rides;
      for (const m of L.scene.children) {
        if (m.userData.__wasVis !== undefined) { m.visible = m.userData.__wasVis; delete m.userData.__wasVis; }
      }
      L.dayNight.setPhase('night', L.shared.uTime.value);
      L.syncDayNight(10);
      for (const [i, v] of R.rides.entries()) {
        v.traffic = null;
        Object.assign(v, { x: 240, z: 120 - i * 8, y: 0, vx: 0, vz: 0, vf: 0, dirty: true });
      }
      for (const e of h.enemies) {
        if (!e.duel) { e.x = 240; e.z = -180; e.aggro = 0; e.mode = 'roam'; e.wait = 1e4; e.windup = 0; e.going = 0; }
      }
      const cx = -3.0, cz = -142.0;
      const mech = h.enemies.find((e) => e.kind === 'brute');
      const blade = h.enemies.find((e) => e.kind === 'blade');
      const gunner = h.enemies.find((e) => e.kind === 'gunner');
      const enforcer = h.enemies.find((e) => e.kind === 'enforcer');
      const punk = h.enemies.find((e) => e.kind === 'punk');

      const squad = [
        [mech, cx, cz, 0.08, 0.35],
        [blade, cx - 2.35, cz + 1.25, 0.24, 0.28],
        [enforcer, cx + 2.45, cz + 1.15, -0.22, 0],
        [gunner, cx - 4.05, cz + 0.35, 0.32, 0.55],
        [punk, cx + 4.0, cz + 0.45, -0.30, 0],
      ];
      for (const [foe, x, z, yaw, windup] of squad) {
        if (!foe) continue;
        foe.dead = 0; foe.gone = false;
        foe.x = foe.homeX = foe.tx = x;
        foe.z = foe.homeZ = foe.tz = z;
        foe.yaw = foe.attackYaw = yaw;
        foe.windup = windup;
        foe.cool = 99;
        foe.wait = 1e4;
        foe.mesh.visible = true;
      }
      L.teleport(cx, cz + 5.0, cx, cz);
      const gy = L.heightAt(cx, cz);
      L.view([cx + 0.35, gy + 1.85, cz + 6.9], [cx - 0.1, gy + 1.75, cz - 4.0]);
      L.step(1 / 60, 4, true);
      for (const [foe] of squad) {
        if (foe) {
          foe.mesh.visible = true;
          if (foe.aim) foe.aim.visible = false;
        }
      }
      L.figure.man.object.visible = false;
      L.figure.cloakMesh.visible = false;
    });
    await saveShot(page, 'aa-04-factions-and-mech.png');

    // 06: Glowing Old Kyoto Memory Echo Shrine pedestal on the street
    await page.evaluate(() => {
      const L = window.lab, h = L.hunt;
      L.dayNight.setPhase('night', L.shared.uTime.value);
      L.syncDayNight(10);
      for (const e of h.enemies) {
        if (!e.duel) { e.x = 240; e.z = 180; e.aggro = 0; e.wait = 1e4; }
      }
      const shrines = L.missions?.echoShrines ?? [];
      const shrine = shrines[2] ?? shrines[0];
      shrine.collected = false;
      shrine.root.visible = true;
      if (shrine.model) shrine.model.visible = true;

      const kx = shrine.x + 2.65, kz = shrine.z + 2.35;
      L.figure.man.object.visible = true;
      L.figure.cloakMesh.visible = true;
      const base = { move: { x: 0, y: 0 }, look: { x: 0, y: 0 }, walk: false, sprint: false, strafe: false, jump: false, jumpHeld: false, zoom: 0, device: 'keyboard' };
      L.setInput(() => base);
      L.teleport(kx, kz, shrine.x, shrine.z);
      const gy = L.heightAt(shrine.x, shrine.z);
      L.view([shrine.x + 3.85, gy + 1.92, shrine.z + 4.75], [shrine.x - 0.25, gy + 1.52, shrine.z - 0.5]);
      L.step(1 / 60, 18, true);
      shrine.collected = false;
      shrine.root.visible = true;
      if (shrine.model) shrine.model.visible = true;
    });
    await saveShot(page, 'aa-06-echo-shrine.png');
    await page.close();
  }

  // --- 05: Dramatic boss clash against Ren's Specter with Odachi & dual-layer blade trail ---
  {
    const page = await openScene('?scene=autumn&phase=night&mode=roam&nopad');
    await page.waitForFunction(() => {
      const kSword = window.lab?.figure?.man?.object?.getObjectByName('Traveler katana');
      const rival = window.lab?.hunt?.enemies?.find((e) => e.duel && e.seg === 5);
      const rSword = rival?.mesh?.getObjectByName('Rival katana');
      return kSword?.children.length === 1 && rSword?.children.length === 1;
    }, null, { timeout: 15000 });
    await page.evaluate(() => {
      const L = window.lab, h = L.hunt;
      document.getElementById('hint')?.remove();
      document.documentElement.removeAttribute('data-clean');
      L.dayNight.setPhase('night', L.shared.uTime.value);
      L.syncDayNight(10);
      for (const e of h.enemies) {
        if (!e.duel) { e.x = 240; e.z = 180; e.aggro = 0; e.wait = 1e4; }
      }
      const rival = h.enemies.find((e) => e.duel && e.seg === 5) ?? h.enemies.find((e) => e.duel);
      const rx = rival.homeX, rz = rival.homeZ;
      rival.x = rx; rival.z = rz;
      const base = { move: { x: 0, y: 0 }, look: { x: 0, y: 0 }, walk: false, sprint: false, strafe: false, jump: false, jumpHeld: false, zoom: 0, device: 'keyboard' };
      L.setInput(() => base);
      L.teleport(rx + 0.3, rz + 2.35, rx, rz);
      L.step(1 / 60, 10, false);

      rival.hp = 240;
      rival.evaded = true;
      rival.punishes = 1;
      rival.move = 'sweep';
      rival.phase = 'active';
      rival.timer = 0.20;
      rival.hitDone = true;
      const kx = rx + 0.28, kz = rz + 2.15;
      rival.attackYaw = rival.yaw = Math.atan2(kx - rival.x, kz - rival.z);
      L.player.teleport(kx, L.heightAt(kx, kz), kz);
      L.player.yaw = L.player.lastYaw = Math.atan2(rival.x - kx, rival.z - kz);
      let frame = 0;
      L.setInput(() => ({ ...base, attack: frame++ === 0 }));
      for (let i = 0; i < 16; i++) {
        rival.phase = 'active';
        rival.hitDone = true;
        rival.timer = Math.max(0.03, 0.20 - i * 0.0105);
        L.step(1 / 60, 1, i === 15);
      }
      L.setInput(() => base);
      const gy = L.heightAt(rx, rz);
      L.view([rx + 2.45, gy + 1.38, rz + 2.55], [rx - 0.15, gy + 1.24, rz + 0.95]);
      L.step(1 / 240, 1, true);
      for (const sel of ['.chapter-banner', '.story', '.waypoint']) {
        const el = document.querySelector(sel);
        if (el) el.hidden = true;
      }
      const toast = document.querySelector('.toast');
      if (toast) { toast.classList.remove('visible'); toast.hidden = true; }
    });
    await saveShot(page, 'aa-05-ren-boss-duel.png');
    await page.close();
  }

  // --- 07: Open Story & Memory Codex + Character Dossiers inside the menu ---
  {
    const seedPage = await context.newPage();
    await seedPage.goto(`${baseURL}/?scene=autumn&clean&capture`);
    await seedPage.evaluate(() => {
      localStorage.setItem('oilpaint.lore.v1', JSON.stringify([
        'echo-01', 'echo-02', 'echo-03', 'echo-04', 'echo-05', 'echo-06', 'echo-07', 'echo-08',
      ]));
      localStorage.setItem('oilpaint.levels.v1', JSON.stringify({
        cleared: 3, x: 30.9, z: 6, cash: 840, unlocked: ['car', 'bicycle', 'bike', 'sports', 'pickup'], missions: [0, 1, 2],
      }));
    });
    await seedPage.close();

    const page = await openScene('?scene=autumn&phase=night&nopad');
    await page.evaluate(() => {
      const L = window.lab;
      L.dayNight.setPhase('night', L.shared.uTime.value);
      L.syncDayNight(10);
      document.getElementById('hint')?.remove();
      for (const sel of ['.chapter-banner', '.target', '.story']) {
        const el = document.querySelector(sel);
        if (el) el.hidden = true;
      }
    });
    await page.click('#pause-button');
    await page.click('#codex-toggle');
    await page.click('[data-codex-tab="cast"]');
    await page.evaluate(() => {
      const codex = document.getElementById('codex-panel');
      codex?.scrollIntoView({ block: 'start' });
      const castPane = document.getElementById('codex-cast');
      if (castPane) castPane.scrollTop = 0;
    });
    await page.waitForTimeout(200);
    await saveShot(page, 'aa-07-story-codex-ui.png');
    await page.close();
  }
}

async function capturePrologue() {
  // --- 08: Snowbound ridge & cabin under the enhanced Aurora (?scene=aurora) ---
  const page = await openScene('?scene=aurora&nopad&clean');
  await page.waitForFunction(() => window.lab.lantern.object.getObjectByName('Spirit lantern'), null, { timeout: 15000 });
  await page.evaluate(() => {
    const L = window.lab;
    L.dayNight.setPhase('night', 0);
    if (L.hunt?.enemies) {
      for (const e of L.hunt.enemies) { e.x = 240; e.z = 290; e.mesh.visible = false; }
    }
    L.trail(162, 142.4);
    const kx = L.crestX(142), kz = 142;
    L.teleport(kx, kz, L.crestX(110), 110);
    L.wait(18.0);
    const base = { move: { x: 0, y: 0 }, look: { x: 0, y: 0 }, walk: false, sprint: false, strafe: false, jump: false, jumpHeld: false, zoom: 0, device: 'keyboard' };
    L.setInput(() => base);
    L.follow.pitch = -0.11;
    L.follow.want = L.follow.distance = 6.2;
    L.step(1 / 60, 24, true);
  });
  await saveShot(page, 'aa-08-snowbound-prologue.png');
  await page.close();
}

try {
  if (!prologueOnly) await captureCity();
  await capturePrologue();
  assert.deepEqual(errors, [], `Browser errors during screenshot capture:\n${errors.join('\n')}`);
  console.log(prologueOnly ? 'Captured the Snowbound prologue.' : 'Captured all eight Investor Showcase screenshots.');
} finally {
  await context.close();
  await browser.close();
  server?.close();
}
