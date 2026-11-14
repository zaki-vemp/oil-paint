// Exercises the street in the real browser game: taking a car, its speed, a wall, ramming a crew member,
// traffic yielding, chassis impacts/rollovers, the seat rule, and what hurting the street's own people costs.
// Build/serve first, then:
//   CHROME=/path/to/chrome node tools/street.mjs [http://127.0.0.1:5405] [--shots dir]
import assert from 'node:assert/strict';
import { chromium } from 'playwright-core';
import { guard } from './no-pointer-lock.mjs';
import { GUILT, RAM } from '../src/game/hunt.js';
guard(chromium);
const args = process.argv.slice(2);
const shots = args.includes('--shots') ? args[args.indexOf('--shots') + 1] : null;
const baseURL = args.find((a) => a.startsWith('http')) ?? 'http://127.0.0.1:5405';
const browser = await chromium.launch({ executablePath: process.env.CHROME, headless: true });
const errors = [];
try {
  const page = await browser.newPage({ viewport: { width: 1440, height: 900 } });
  page.on('pageerror', (e) => errors.push(e.message));
  page.on('console', (m) => { if (m.type() === 'error') errors.push(m.text()); });
  await page.goto(`${baseURL}/?scene=autumn&mode=roam&nopad`);
  await page.waitForFunction(() => window.lab?.ready, null, { timeout: 120000 });
  const snap = async (name) => { if (shots) { await page.evaluate(() => window.lab.step(1 / 60, 1)); await page.screenshot({ path: `${shots}/${name}.png` }); } };
  await page.evaluate(() => {
    window.__paused = true;
    const L = window.lab, h = L.hunt;
    const base = { move: { x: 0, y: 0 }, look: { x: 0, y: 0 }, walk: false, sprint: false, strafe: false, jump: false, jumpHeld: false, zoom: 0, device: 'keyboard' };
    window.T = {
      base, input: (state) => L.setInput(() => ({ ...base, ...state })),
      once(state) { let n = 0; L.setInput(() => (n++ ? base : { ...base, ...state })); L.step(1 / 60, 1); L.setInput(() => base); },
      check(ok, message) { if (!ok) throw Error(message); },
      // everything that fights is told to stand down, so each check below meets only what it asks for
      calm() { for (const e of h.enemies) if (!e.duel) { e.aggro = 0; e.angry = 0; e.mode = 'roam'; e.wait = 1e4; e.windup = 0; e.cool = 99; } },
    };
    window.T.calm();
    // Handling is measured on an empty lane. Traffic has its own fresh-world checks below.
    for (const [i, v] of L.rides.rides.entries()) if (v.traffic) {
      v.traffic = null; v.x = 230; v.z = 100 - i * 7; v.y = L.heightAt(v.x, v.z); v.vx = v.vz = v.vf = 0;
    }
  });

  const drive = await page.evaluate(() => {
    const L = window.lab, h = L.hunt, P = L.player, R = L.rides, { input, once, check, base } = window.T, out = {};
    const car = R.rides.find((v) => v.kind === 'car'), bike = R.rides.find((v) => v.kind === 'bike');
    check(R.rides.length >= 12 && car && bike, 'a car and a bike at every cairn');
    L.teleport(car.x + 1.6, car.z, car.x, car.z); input({}); L.step(1 / 60, 5);
    check(document.querySelector('.ride-prompt').hidden === false, 'a prompt beside a vehicle in reach');
    once({ ride: true });
    check(R.driven === car && L.figure.man.object.visible === false, 'G takes the seat and he is out of sight');
    const x0 = car.x, z0 = car.z;
    input({ move: { x: 0, y: 1 } }); L.step(1 / 60, 150);
    out.speed = car.vf; out.run = Math.hypot(car.x - x0, car.z - z0);
    check(car.vf > 14 && out.run > 18, 'it pulls away: ' + JSON.stringify(out));
    check(Math.hypot(P.feet.x - car.x, P.feet.z - car.z) < 0.01, 'he goes where it goes');
    once({ ride: true });
    check(R.driven === car, 'no stepping out at speed');
    // a crew member in the road, a car length ahead
    const punk = h.enemies.find((e) => e.kind === 'punk' && !e.dead), ux = car.vx / Math.hypot(car.vx, car.vz), uz = car.vz / Math.hypot(car.vx, car.vz);
    punk.x = car.x + ux * 7; punk.z = car.z + uz * 7;
    const hp = punk.hp, before = Math.hypot(car.vx, car.vz); let peak = 0;
    L.setInput(() => { peak = Math.max(peak, punk.air); return { ...base, move: { x: 0, y: 1 } }; });
    L.step(1 / 60, 40);
    out.ram = { dealt: hp - punk.hp, thrown: +peak.toFixed(2), dead: !!punk.dead };
    check(out.ram.dealt === 42 && !punk.dead && peak > 0.3, 'a bumper throws a punk without a one-hit kill: ' + JSON.stringify(out.ram));
    // a wall across the road: it must stop, and give some of the speed back the other way
    // put back on the middle of the road at speed, so nothing but the wall is in its way
    const [ax, az] = L.way[12], [bx, bz] = L.way[13];
    Object.assign(car, { x: ax, z: az, y: L.heightAt(ax, az), yaw: Math.atan2(bx - ax, bz - az), air: 0, vy: 0, steer: 0 });
    const fx = Math.sin(car.yaw), fz = Math.cos(car.yaw), wx = car.x + fx * 16, wz = car.z + fz * 16;
    car.vx = fx * 20; car.vz = fz * 20; input({ move: { x: 0, y: 1 } }); L.step(1 / 60, 1);
    const wall = P.world.createCollider(P.R.ColliderDesc.cuboid(9, 3, 0.4).setTranslation(wx, car.y + 2, wz)
      .setRotation({ x: 0, y: Math.sin(car.yaw / 2), z: 0, w: Math.cos(car.yaw / 2) }));
    P.world.step();
    const hpDriver = h.hp, into = car.vf; let least = 99;
    L.setInput(() => { least = Math.min(least, car.vf); return { ...base, move: { x: 0, y: 1 } }; });
    L.step(1 / 60, 70);
    const past = (car.x - wx) * fx + (car.z - wz) * fz;
    out.wall = { into: +into.toFixed(1), least: +least.toFixed(1), short: +(-past).toFixed(2), hurt: hpDriver - h.hp };
    check(past < -1.8 && least < 0, 'the wall holds and throws it back: ' + JSON.stringify(out.wall));
    check(out.wall.hurt > 0, 'a hard crash is felt: ' + JSON.stringify(out.wall));
    P.world.removeCollider(wall, true);
    input({ jumpHeld: true }); L.step(1 / 60, 240);
    check(Math.abs(car.vf) < 1, 'the handbrake brings it to rest');
    once({ ride: true });
    check(!R.driven && L.figure.man.object.visible && Math.hypot(P.feet.x - car.x, P.feet.z - car.z) > 1.2, 'G again and he stands beside it');
    // the bike leans into its turns
    L.teleport(bike.x + 1.2, bike.z, bike.x, bike.z); once({ ride: true });
    check(R.driven === bike && bike.rider.visible, 'the bike shows a rider');
    input({ move: { x: 0, y: 1 } }); L.step(1 / 60, 70);
    input({ move: { x: 1, y: 1 } }); L.step(1 / 60, 25);
    out.lean = +bike.lean.toFixed(2);
    check(Math.abs(bike.lean) > 0.15, 'it leans: ' + out.lean);
    input({ jumpHeld: true }); L.step(1 / 60, 260); once({ ride: true });
    check(!R.driven, 'off the bike');
    return out;
  });
  await snap('street-car');

  const street = await page.evaluate(({ GUILT }) => {
    const L = window.lab, h = L.hunt, P = L.player, { input, once, check, base, calm } = window.T, out = {};
    calm();
    const strike = (e, swings) => {
      let n = 0;
      L.setInput(() => {
        if (!e.dead && Math.hypot(P.feet.x - e.x, P.feet.z - e.z) > 2) { P.teleport(e.x, L.heightAt(e.x, e.z + 1.6), e.z + 1.6); P.yaw = P.lastYaw = Math.PI; }
        P.yaw = P.lastYaw = Math.atan2(e.x - P.feet.x, e.z - P.feet.z); // he looks at what he means to hit
        return { ...base, attack: n++ % 60 === 0 && n <= swings * 60 };
      });
      L.step(1 / 60, swings * 60);
    };
    const civs = h.enemies.filter((e) => e.kind === 'civ'), runners = h.enemies.filter((e) => e.kind === 'thief');
    check(civs.length >= 12 && runners.length >= 6, 'people on every level');
    check(runners.some((e) => e.thief) && runners.some((e) => !e.thief), 'some runners are thieves and some are not');
    const [victim, witness] = civs;
    let hp = h.hp;
    L.teleport(victim.x, victim.z + 1.6, victim.x, victim.z); input({}); L.step(1 / 60, 30);
    check(h.hp === hp && !victim.windup, 'a citizen leaves him alone');
    witness.x = victim.x + 6; witness.z = victim.z + 4;
    hp = h.hp; strike(victim, 1);
    out.hurt = hp - h.hp;
    check(out.hurt === GUILT && !victim.dead && !(witness.angry > 0), 'hurting a citizen costs health and nothing more: ' + out.hurt);
    hp = h.hp; strike(victim, 1);
    out.killed = hp - h.hp;
    check(victim.dead && out.killed === GUILT * 2, 'killing one costs twice: ' + out.killed);
    check(witness.angry > 0 && witness.mode === 'chase' && !document.querySelector('.heat').hidden, 'and the street turns on him');
    hp = h.hp; input({}); witness.cool = 0;
    for (let i = 0; i < 400 && h.hp === hp; i++) L.step(1 / 60, 1);
    out.mob = hp - h.hp;
    check(out.mob > 0, 'the mob lands a blow');
    hp = h.hp; strike(witness, 2);
    check(witness.dead && h.hp >= hp - 14, 'defending himself from the mob is not held against him: ' + JSON.stringify({ dead: !!witness.dead, whp: witness.hp, angry: witness.angry, lost: hp - h.hp, d: Math.hypot(P.feet.x - witness.x, P.feet.z - witness.z) }));
    calm();
    const thief = runners.find((e) => e.thief), jogger = runners.find((e) => !e.thief);
    hp = h.hp; strike(thief, 2);
    out.thief = hp - h.hp;
    check(thief.dead && out.thief === 0, 'a thief costs nothing: ' + out.thief);
    hp = h.hp; strike(jogger, 1);
    out.jogger = hp - h.hp;
    check(out.jogger === GUILT, 'a jogger in a hood is still a citizen: ' + out.jogger);
    return out;
  }, { GUILT });
  await snap('street-people');

  // A fresh street keeps the collision and traffic checks independent of the earlier mob and stolen vehicles.
  await page.reload();
  await page.waitForFunction(() => window.lab?.ready, null, { timeout: 120000 });
  const physics = await page.evaluate(({ GUILT }) => {
    window.__paused = true;
    const L = window.lab, R = L.rides, h = L.hunt, P = L.player;
    const base = { move: { x: 0, y: 0 }, look: { x: 0, y: 0 }, jumpHeld: false, zoom: 0, device: 'keyboard' };
    const check = (ok, message) => { if (!ok) throw Error(message); };
    const input = (state = {}) => L.setInput(() => ({ ...base, ...state }));
    const once = (state) => { input(state); L.step(1 / 60, 1); input(); };
    const out = {};
    const reset = (v, x, z, yaw = 0, speed = 0) => Object.assign(v, { x, z, yaw, y: L.heightAt(x, z), vx: Math.sin(yaw) * speed, vz: Math.cos(yaw) * speed,
      vf: speed, vy: 0, air: 0, pitch: 0, roll: 0, tilt: 0, tipped: false, lean: 0, rollRate: 0, spinRate: 0, steer: 0 });
    const people = h.enemies.filter(e => !e.duel);
    for (const e of people) { e.x = 240; e.aggro = 0; e.wait = 1e4; e.cool = 99; }
    const traffic = R.rides.filter(v => v.traffic);
    check(traffic.length === 6, 'a moving vehicle in each district');
    const v = traffic[0], x0 = v.x, z0 = v.z;
    // Watch from outside the lane; standing eight metres ahead makes traffic correctly brake for us.
    L.teleport(v.x + 8, v.z + 8, v.x, v.z); input(); L.step(1 / 60, 120);
    out.trafficTravel = Math.hypot(v.x - x0, v.z - z0);
    check(out.trafficTravel > 4, 'traffic moves without a driver: ' + out.trafficTravel);
    reset(v, v.home.x, v.home.z, v.home.yaw);
    const fx = Math.sin(v.yaw), fz = Math.cos(v.yaw), witness = people.find(e => e.kind === 'civ');
    witness.x = v.x + fx * 8; witness.z = v.z + fz * 8; witness.tx = witness.x; witness.tz = witness.z;
    L.teleport(v.x + 8, v.z + 8, v.x, v.z); input();
    let closest = Infinity;
    const hp = witness.hp, driverHp = h.hp;
    for (let i = 0; i < 240; i++) { L.step(1 / 60, 1, false); closest = Math.min(closest, Math.hypot(v.x - witness.x, v.z - witness.z)); }
    out.yield = { gap: closest, speed: Math.hypot(v.vx, v.vz) };
    check(closest > 2.7 && out.yield.speed < 0.8 && witness.hp === hp && h.hp === driverHp, 'traffic brakes for a person without blaming the player: ' + JSON.stringify(out.yield));
    // Park the rest off the road; from here only the two chassis under test can interfere.
    R.leave(true);
    for (const [i, o] of R.rides.entries()) { o.traffic = null; reset(o, 220, 100 - i * 7); }
    for (const e of people) e.x = 240;
    const [ax, az] = L.way[9], [bx, bz] = L.way[10], yaw = Math.atan2(bx - ax, bz - az), ux = Math.sin(yaw), uz = Math.cos(yaw);
    const car = R.rides.find(o => o.kind === 'car'), other = R.rides.find(o => o.kind === 'car' && o !== car), bike = R.rides.find(o => o.kind === 'bike');
    reset(car, ax, az, yaw); reset(other, ax + ux * 7, az + uz * 7, yaw);
    L.teleport(car.x + Math.cos(yaw) * 1.7, car.z - Math.sin(yaw) * 1.7, car.x, car.z); once({ ride: true });
    check(R.driven === car, 'taking the collision-test car');
    check(!document.querySelector('.ride-prompt').hidden, 'the exit button stays available while driving, including on touch');
    car.vx = ux * 22; car.vz = uz * 22;
    input(); L.step(1 / 20, 12);
    out.collision = { pushed: (other.x - ax) * ux + (other.z - az) * uz - 7, gap: Math.hypot(other.x - car.x, other.z - car.z) };
    check(out.collision.pushed > 1 && out.collision.gap >= 4.1, 'a high-speed rear end pushes the parked car without tunnelling: ' + JSON.stringify(out.collision));
    R.leave(true); reset(other, 220, 0);
    // Hit a bike broadside, then watch the impact carry it over its balance point.
    reset(bike, ax, az, yaw);
    const sx = Math.cos(yaw), sz = -Math.sin(yaw);
    reset(car, ax - sx * 6, az - sz * 6, yaw + Math.PI / 2, 24);
    L.teleport(ax + 8, az, ax, az); input();
    let peak = 0;
    for (let i = 0; i < 360; i++) { L.step(1 / 60, 1, false); peak = Math.max(peak, Math.abs(bike.tilt)); }
    out.rollover = { peak, tilt: bike.tilt, tipped: bike.tipped };
    check(peak > 1.6 && bike.tipped, 'a broadside rolls the bike and it settles on its side/roof: ' + JSON.stringify(out.rollover));
    reset(car, 220, -20); // leave only the wreck in reach of G
    L.teleport(bike.x + 1.6, bike.z, bike.x, bike.z); once({ ride: true });
    check(!R.driven && !bike.tipped && Math.abs(bike.tilt) < 0.01, 'G beside a stopped wreck rights it');
    once({ ride: true }); check(R.driven === bike, 'the righted bike is driveable'); R.leave(true);
    reset(bike, 220, -10); reset(car, ax, az, yaw);
    // The same bumper must carry the citizen/thief rule, not just the sword tested above.
    const runOver = (e) => {
      R.leave(true); reset(car, ax, az, yaw);
      for (const p of people) { p.x = 240; p.angry = 0; p.aggro = 0; p.wait = 1e4; }
      e.x = ax + ux * 6; e.z = az + uz * 6; e.tx = e.x; e.tz = e.z;
      witness.x = e.x + Math.cos(yaw) * 7; witness.z = e.z - Math.sin(yaw) * 7;
      L.teleport(ax + Math.cos(yaw) * 1.7, az - Math.sin(yaw) * 1.7, ax, az); once({ ride: true });
      car.vx = ux * 20; car.vz = uz * 20;
      const hp = h.hp; input(); L.step(1 / 60, 25);
      return hp - h.hp;
    };
    const victim = people.find(e => e.kind === 'civ' && e !== witness), thief = people.find(e => e.thief);
    out.ramCitizen = runOver(victim);
    check(victim.dead && out.ramCitizen === GUILT * 2 && witness.angry > 0, 'ramming an innocent costs health and turns witnesses hostile: ' + out.ramCitizen);
    out.ramThief = runOver(thief);
    check(thief.dead && out.ramThief === 0 && !witness.angry, 'ramming a thief costs nothing and leaves witnesses calm: ' + out.ramThief);
    return out;
  }, { GUILT });
  await snap('street-physics');

  assert.deepEqual(errors, [], 'browser errors');
  console.log(JSON.stringify({ drive, street, physics, ram: RAM }, null, 2));
  console.log('PASS: handling, traffic/yielding, collisions, rollover/recovery, ramming, citizens, mob, thief and jogger');
} finally {
  await browser.close();
}
