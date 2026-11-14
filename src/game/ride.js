// Cars and bikes at every cairn, with local traffic yielding to people: G takes a slow vehicle, G again steps out.
// The reviewed Blender coupe, delivery van, motorcycles and seated rider share the city’s
// vertex-painted materials; wheels keep their own steering and spin pivots. The handling is a small tyre model, not a rigid body: the engine pulls
// hardest from rest and has nothing left at top speed, the front wheels turn the heading (a bicycle model, less
// lock at speed), and the tyres scrub sideways speed off at their grip, until past the slip limit or on the
// handbrake they let go and it slides. It follows the land's heightAt on both axles, leaves the ground over a
// crest and comes down under gravity. Chassis sweeps stop walls, oriented boxes exchange momentum between
// vehicles, and a sideways impact can tip the chassis onto its side or roof. G beside a stopped wreck rights it.
// Whatever stands in its way is rammed (hunt.ram): crews, the mech, and the street's own people, at his risk.
import * as THREE from 'three';
import { loadStreetAsset, streetClone } from '../street-assets.js';
import { RAM } from './hunt.js';
import { wayDist, ROAD } from '../world/land.js';
import { AVENUES, STREETS, GRID_MIN, GRID_MAX } from '../world/grid.js';

// len/wid/base in metres; top and rev in m/s; grip and drift are how fast sideways speed is scrubbed (1/s), gripping
// and sliding; slip is the sideways speed where the tyres let go; calm is the speed at which steering lock has halved.
// heft is the share of speed kept after hitting someone (a heavy truck keeps more). hull is hit points.
// throw: a hard lean at speed, or a rollover, puts the rider on the pavement. `car` is the sedan — the street checks look it up by that key.
export const RIDES = {
  car: { name: 'Kestrel', len: 4.2, wid: 1.9, base: 2.6, wheel: 0.36, top: 25, rev: 8, accel: 12, brake: 24, grip: 7.4, drift: 1.3, slip: 6, steer: 0.58, calm: 9, lean: 0, heft: 0.65, mass: 1200, eye: 10.5, hull: 140, throw: 0 },
  sports: { name: 'Kestrel GT', len: 4.3, wid: 1.85, base: 2.45, wheel: 0.34, top: 34, rev: 9, accel: 18, brake: 30, grip: 8.6, drift: 0.95, slip: 7.2, steer: 0.72, calm: 11, lean: 0, heft: 0.58, mass: 1000, eye: 11.5, hull: 90, throw: 0 },
  pickup: { name: 'Parcel van', len: 5.3, wid: 2.05, base: 3.25, wheel: 0.44, top: 18, rev: 6, accel: 8, brake: 16, grip: 6.4, drift: 1.5, slip: 5.5, steer: 0.46, calm: 7, lean: 0, heft: 0.84, mass: 2400, eye: 12.2, hull: 240, throw: 0 },
  bike: { name: 'Razor bike', len: 2.2, wid: 0.7, base: 1.45, wheel: 0.36, top: 31, rev: 4, accel: 17, brake: 22, grip: 9, drift: 1.7, slip: 7.5, steer: 0.5, calm: 8, lean: 1, heft: 0.45, mass: 240, eye: 8.5, hull: 70, throw: 1 },
  bicycle: { name: 'Courier scooter', len: 1.7, wid: 0.5, base: 1.05, wheel: 0.32, top: 12, rev: 3, accel: 5, brake: 10, grip: 8, drift: 2.2, slip: 4, steer: 0.7, calm: 5, lean: 1, heft: 0.28, mass: 80, eye: 7.6, hull: 28, throw: 1 },
};
const GRAVITY = 20;
const BAIL = 9; // m/s: faster than this he stays in his seat
const wrap = (a) => Math.atan2(Math.sin(a), Math.cos(a));
const toward = (v, by) => Math.sign(v) * Math.max(0, Math.abs(v) - by);

/** A sideways impulse rocks the suspension; enough energy crosses its balance point and rolls the chassis. */
export function rock(v, dt) {
  v.rollRate = ((v.rollRate ?? 0) - 18 * Math.sin(v.tilt) * Math.cos(v.tilt) * dt) * Math.exp(-2.3 * dt);
  v.tilt = wrap(v.tilt + v.rollRate * dt);
  v.tipped = Math.cos(v.tilt) < 0.5;
}

/** Separating axes for two chassis. Small simulation steps keep even opposite-moving bikes from tunnelling. */
export function collide(a, b) {
  if (Math.abs(a.y - b.y) > 1.5) return 0;
  const dx = b.x - a.x, dz = b.z - a.z;
  if (Math.hypot(dx, dz) > (Math.hypot(a.spec.len, a.spec.wid) + Math.hypot(b.spec.len, b.spec.wid)) / 2) return 0;
  const axes = (v) => [[Math.cos(v.yaw), -Math.sin(v.yaw)], [Math.sin(v.yaw), Math.cos(v.yaw)]];
  const aa = axes(a), bb = axes(b);
  const radius = (v, axes, x, z) => Math.abs(axes[0][0] * x + axes[0][1] * z) * v.spec.wid / 2 + Math.abs(axes[1][0] * x + axes[1][1] * z) * v.spec.len / 2;
  let depth = Infinity, nx = 0, nz = 0;
  for (const [x, z] of [...aa, ...bb]) {
    const d = dx * x + dz * z, overlap = radius(a, aa, x, z) + radius(b, bb, x, z) - Math.abs(d);
    if (overlap <= 0) return 0;
    if (overlap < depth) { depth = overlap; nx = x * (d < 0 ? -1 : 1); nz = z * (d < 0 ? -1 : 1); }
  }
  const ia = 1 / a.spec.mass, ib = 1 / b.spec.mass, total = ia + ib;
  a.x -= nx * (depth + 0.002) * ia / total; a.z -= nz * (depth + 0.002) * ia / total;
  b.x += nx * (depth + 0.002) * ib / total; b.z += nz * (depth + 0.002) * ib / total;
  const closing = (a.vx - b.vx) * nx + (a.vz - b.vz) * nz;
  if (closing <= 0) return 0;
  const impulse = closing * 1.15 / total;
  a.vx -= nx * impulse * ia; a.vz -= nz * impulse * ia;
  b.vx += nx * impulse * ib; b.vz += nz * impulse * ib;
  for (const [v, sign, inv] of [[a, -1, ia], [b, 1, ib]]) {
    const side = (nx * Math.cos(v.yaw) - nz * Math.sin(v.yaw)) * sign;
    v.rollRate = (v.rollRate ?? 0) - side * impulse * inv * (v.spec.lean ? 0.9 : 0.55);
    // An off-centre contact turns it too; cap the spin so a bike cannot whip a car round.
    const lever = Math.max(-1, Math.min(1, (dx * nz - dz * nx) / v.spec.len));
    v.spinRate = (v.spinRate ?? 0) + Math.max(-2, Math.min(2, lever * impulse * inv / v.spec.base));
  }
  if (a.liable > 0 || b.liable > 0) a.liable = b.liable = 5; // a shunted parked car still belongs to the driver's mistake
  return closing;
}

/**
 * One step of the handling on flat ground: `v` is { yaw, vx, vz, steer } and is changed in place.
 * throttle -1..1 (the same pedal brakes when it opposes the motion), steer -1..1 (+ is his right), hand: handbrake.
 * gripMul is 1 on asphalt and lower on dirt: the same slip limit arrives sooner, and the tyres scrub less of it.
 */
export function drive(v, s, { throttle = 0, steer = 0, hand = false } = {}, dt, grounded = true, gripMul = 1) {
  const fx = Math.sin(v.yaw), fz = Math.cos(v.yaw);
  let vf = v.vx * fx + v.vz * fz, vs = v.vx * fz - v.vz * fx; // along the heading, and across it
  v.yawRate = 0; v.sliding = false;
  if (grounded) {
    if (throttle * vf < 0 && Math.abs(vf) > 0.5) vf = toward(vf, s.brake * Math.abs(throttle) * dt);
    else if (throttle > 0) vf += s.accel * throttle * Math.max(0, 1 - (vf / s.top) ** 2) * dt;
    else if (throttle < 0) vf += s.accel * 0.6 * throttle * Math.max(0, 1 - (vf / s.rev) ** 2) * dt;
    // the air takes a share of the speed; rolling and the idle engine take a little flat
    vf = toward(vf - vf * 0.1 * dt, (throttle === 0 ? 1.8 : 0.3) * dt);
    if (hand) vf = toward(vf, s.brake * 0.2 * dt); // it locks the rear, it does not stop the car
    const lock = s.steer / (1 + Math.abs(vf) / s.calm);
    v.steer += (steer * lock - v.steer) * Math.min(1, 9 * dt);
    v.yawRate = -(vf / s.base) * Math.tan(v.steer) * (hand ? 1.35 : 1); // a locked rear swings round
    v.yaw += v.yawRate * dt;
    const grip = s.grip * gripMul, slip = s.slip * (0.35 + 0.65 * gripMul);
    const before = Math.hypot(vf, vs), slide = hand || Math.abs(vs) > slip;
    vs *= Math.exp(-(slide ? s.drift * (gripMul < 1 ? 0.7 : 1) : grip) * dt);
    // gripping tyres turn the car's speed with it rather than burn it: three quarters of the scrub comes back
    const after = Math.hypot(vf, vs);
    if (!slide && after > 1e-3) { const k = (after + (before - after) * 0.75) / after; vf *= k; vs *= k; }
    v.sliding = slide && Math.abs(vs) > 2;
  }
  v.vx = fx * vf + fz * vs; v.vz = fz * vf - fx * vs; v.vf = vf;
  return v;
}

// Existing vehicle keys keep their handling and story unlocks; the reviewed art supplies their bodies.
const ART = {
  car: { file: 'coupe', axle: 2.56, radius: 0.39, track: 0.87 },
  sports: { file: 'coupe', axle: 2.56, radius: 0.39, track: 0.87 },
  pickup: { file: 'van', axle: 2.07, radius: 0.32, track: 0.76 },
  bike: { file: 'motorbike', axle: 1.79, radius: 0.36 },
  bicycle: { file: 'scooter', axle: 1.55, radius: 0.285 },
};

export async function createRides({ scene, shared, player, heightAt, hunt, route, checkpoints, inBounds, settle }) {
  const files = [...new Set([...Object.values(ART).map(a => a.file), 'rider'])];
  const sources = Object.fromEntries(await Promise.all(files.map(async name => [name, await loadStreetAsset(name)])));
  const R = player.R, world = player.world;
  const ray = new R.Ray({ x: 0, y: 0, z: 0 }, { x: 0, y: 0, z: 1 });
  const add = (parent, geometry, material, x = 0, y = 0, z = 0) => {
    const m = new THREE.Mesh(geometry, material); m.position.set(x, y, z); parent.add(m); return m;
  };
  function build(kind) {
    const s = RIDES[kind], art = ART[kind], root = new THREE.Group(), shell = new THREE.Group(), wheels = [];
    root.rotation.order = 'YXZ'; root.add(shell);
    const model = streetClone(sources[art.file], shared);
    model.updateMatrixWorld(true);
    const nodes = [];
    model.traverse(o => { if (o.isMesh && o.name.startsWith('Wheel_')) nodes.push(o); });
    for (const node of nodes) {
      const p = new THREE.Vector3().setFromMatrixPosition(node.matrixWorld);
      const transform = node.matrixWorld.clone().setPosition(0, 0, 0);
      const geometry = node.geometry.clone().applyMatrix4(transform);
      geometry.scale(s.wheel / art.radius, s.wheel / art.radius, s.wheel / art.radius);
      const pivot = new THREE.Group(), spin = new THREE.Group();
      const front = node.name.startsWith('Wheel_F');
      pivot.position.set(art.track ? Math.sign(p.x) * (s.wid / 2 - 0.12) : 0, s.wheel, (front ? 1 : -1) * s.base / 2);
      spin.add(new THREE.Mesh(geometry, node.material)); pivot.add(spin); root.add(pivot);
      spin.children[0].layers.enable(1);
      wheels.push({ pivot, spin, front });
      node.removeFromParent();
    }
    const sx = art.track ? (s.wid / 2 - 0.12) / art.track : kind === 'bicycle' ? 0.9 : 1;
    model.scale.set(sx, s.wheel / art.radius, s.base / art.axle);
    shell.add(model);
    let rider = null;
    if (s.lean) {
      rider = streetClone(sources.rider, shared);
      rider.scale.copy(model.scale); rider.visible = false; shell.add(rider);
    }
    const smoke = add(shell, new THREE.SphereGeometry(0.28, 6, 5), new THREE.MeshBasicMaterial({ color: 0x9aa3ad, transparent: true, opacity: 0.45, depthWrite: false }), 0, 1.3, -s.len * 0.35);
    const fire = add(shell, new THREE.SphereGeometry(0.34, 6, 5), new THREE.MeshBasicMaterial({ color: 0xff5a1f, transparent: true, opacity: 0.85, depthWrite: false }), 0, 1.15, -s.len * 0.15);
    smoke.visible = fire.visible = false;
    root.userData.asset = art.file;
    scene.add(root);
    return { root, shell, wheels, rider, smoke, fire };
  }

  const rides = [];
  let driven = null, eyeBefore = 0, lookIdle = 0, told = 0;
  function park(kind, x, z, yaw) {
    const s = RIDES[kind], y = heightAt(x, z);
    const body = world.createRigidBody(R.RigidBodyDesc.kinematicPositionBased().setTranslation(x, y + 0.7, z));
    const collider = world.createCollider(R.ColliderDesc.cuboid(s.wid / 2, 0.6, s.len / 2).setFriction(0), body);
    const v = { kind, spec: s, x, y, z, yaw, vx: 0, vz: 0, vy: 0, vf: 0, steer: 0, yawRate: 0, air: 0, pitch: 0, roll: 0, lean: 0, squat: 0, spin: 0,
      tilt: 0, rollRate: 0, spinRate: 0, tipped: false, liable: 0, traffic: null, driver: false,
      hp: s.hull, wrecked: false, boom: 0,
      shape: new R.Cuboid(s.wid / 2, 0.4, s.len / 2), home: { x, z, yaw }, body, collider, ...build(kind) };
    rides.push(v); place(v, 0);
    return v;
  }
  // Two different kinds just past each cairn, rotating so all five show up along the trail.
  const parked = [['car', 'bike'], ['sports', 'bicycle'], ['pickup', 'car'], ['bike', 'sports'], ['bicycle', 'pickup'], ['car', 'bike']];
  checkpoints.forEach((cp, n) => {
    let i = 0;
    for (let k = 1; k < route.length - 1; k++) if (Math.hypot(route[k][0] - cp.x, route[k][1] - cp.z) < Math.hypot(route[i][0] - cp.x, route[i][1] - cp.z)) i = k;
    parked[n].forEach((kind, k) => {
      const j = Math.min(route.length - 2, i + (k ? 2 : 5));
      const [ax, az] = route[j], [bx, bz] = route[j + 1], l = Math.hypot(bx - ax, bz - az) || 1, side = k ? -2.8 : 2.4;
      park(kind, ax + (bz - az) / l * side, az - (bx - ax) / l * side, Math.atan2(bx - ax, bz - az));
    });
  });

  // Traffic keeps to a grid lane (a street or an avenue), in the right-hand side, and turns round at the map edge.
  // A street runs across the gates, so these trips are not stopped by a closed level.
  const beats = [
    { kind: 'car', axis: 'x', line: 160, dir: 1, at: 20 },
    { kind: 'sports', axis: 'x', line: 80, dir: -1, at: -24 },
    { kind: 'pickup', axis: 'x', line: 40, dir: 1, at: 56 },
    { kind: 'car', axis: 'x', line: 0, dir: -1, at: -48 },
    { kind: 'sports', axis: 'x', line: -40, dir: 1, at: 16 },
    { kind: 'pickup', axis: 'z', line: 120, dir: -1, at: 24 },
  ];
  for (const b of beats) {
    const side = 1.6 * (b.axis === 'x' ? -b.dir : b.dir);
    const x = b.axis === 'x' ? b.at : b.line + side, z = b.axis === 'z' ? b.at : b.line + side;
    const yaw = b.axis === 'x' ? (b.dir > 0 ? Math.PI / 2 : -Math.PI / 2) : (b.dir > 0 ? 0 : Math.PI);
    const v = park(b.kind, x, z, yaw);
    v.driver = true; v.traffic = { axis: b.axis, dir: b.dir, line: b.line, cruise: 8, wait: 0, flee: 0, swerve: 1, horn: 0 };
    if (v.rider) v.rider.visible = true;
  }

  function trafficInput(v, dt) {
    const t = v.traffic, sp = Math.hypot(v.vx, v.vz);
    const along = t.axis === 'x' ? v.x : v.z;
    // The lattice ends. Turning round keeps the same six cars in the city.
    if ((t.dir > 0 && along > GRID_MAX - 16) || (t.dir < 0 && along < GRID_MIN + 16)) t.dir *= -1;
    const ahead = 6 + sp * 0.45, side = 1.6 * (t.axis === 'x' ? -t.dir : t.dir);
    const tx = t.axis === 'x' ? v.x + t.dir * ahead : t.line + side;
    const tz = t.axis === 'z' ? v.z + t.dir * ahead : t.line + side;
    const fx = Math.sin(v.yaw), fz = Math.cos(v.yaw);
    let angle = wrap(Math.atan2(tx - v.x, tz - v.z) - v.yaw);
    if (t.flee > 0) { t.flee -= dt; angle += t.swerve * 0.55; }
    let want = Math.min(t.flee > 0 ? t.cruise + 4 : t.cruise, Math.abs(angle) > 0.6 ? 4 : Infinity);
    // The other road of a crossing: stop when someone is already in it, otherwise roll through.
    const lines = t.axis === 'x' ? AVENUES : STREETS;
    const cross = lines.find(p => { const d = (p - along) * t.dir; return d > 1 && d < 9; });
    if (cross != null && t.flee <= 0) {
      const ix = t.axis === 'x' ? cross : t.line, iz = t.axis === 'z' ? cross : t.line;
      const busy = rides.some(o => o !== v && Math.hypot(o.x - ix, o.z - iz) < 6 && Math.hypot(o.vx, o.vz) > 0.8)
        || hunt.enemies.some(e => !e.dead && !e.gone && Math.hypot(e.x - ix, e.z - iz) < 5);
      if (busy) want = 0;
    }
    const stopping = 3 + sp * sp / (2 * v.spec.brake) + sp * 0.4;
    const blocked = (x, z, radius) => {
      const dx = x - v.x, dz = z - v.z, gap = dx * fx + dz * fz - v.spec.len / 2;
      return gap > -0.5 && gap < stopping + radius && Math.abs(dx * fz - dz * fx) < v.spec.wid / 2 + radius + 0.5;
    };
    if (v.tipped || v.wrecked || hunt.enemies.some(e => !e.dead && !e.gone && blocked(e.x, e.z, 0.45)) ||
      (!driven && blocked(player.feet.x, player.feet.z, 0.6)) || rides.some(o => o !== v && blocked(o.x, o.z, o.spec.wid / 2))) want = 0;
    t.wait = Math.max(0, t.wait - dt);
    if (t.wait > 0) want = 0;
    if (t.horn > 0) {
      t.horn -= dt;
      if (!t.honked && Math.hypot(v.x - player.feet.x, v.z - player.feet.z) < 30) { t.honked = true; hunt.hud.flashText('Honk'); }
    }
    return { throttle: want === 0 && sp < 0.7 ? 0 : want > sp + 0.3 ? 0.55 : want < sp - 0.1 ? -1 : 0.15, steer: Math.max(-1, Math.min(1, -angle * 2.4)), hand: want === 0 && sp < 0.7 };
  }

  /** Puts the solids where the numbers say: heading, the land's tilt, a bike's lean, the body's squat, the wheels. */
  function place(v, dt) {
    const s = v.spec;
    v.root.visible = v === driven || Math.hypot(v.x - player.feet.x, v.z - player.feet.z) < 110;
    // Rotate round the chassis centre, lifting the roof/side clear of the ground after a rollover.
    const roll = v.roll + v.tilt + v.lean * s.lean;
    const lift = Math.abs(Math.sin(v.tilt)) * s.wid / 2 + Math.abs(Math.cos(v.tilt)) * 0.7 - 0.7;
    v.root.rotation.set(v.pitch, v.yaw, roll);
    const offset = new THREE.Vector3(0, -0.7, 0).applyEuler(v.root.rotation);
    v.root.position.set(v.x + offset.x, v.y + 0.7 + lift + offset.y, v.z + offset.z);
    v.shell.rotation.set(v.squat, 0, s.lean ? 0 : -v.lean * 0.16); // a car's body rolls out of the turn its tyres hold
    v.spin += v.vf * dt / s.wheel;
    for (const w of v.wheels) { w.spin.rotation.x = v.spin; if (w.front) w.pivot.rotation.y = -v.steer; }
    if (v.rider) v.rider.visible = v === driven || v.driver;
    const hurt = v.spec.hull;
    v.smoke.visible = v.hp < hurt * 0.66;
    v.fire.visible = v.hp < hurt * 0.34 || v.boom > 0;
    if (v.smoke.visible || v.fire.visible) {
      const bob = 1 + Math.sin((v.boom > 0 ? v.boom : v.hp) * 18 + (v.smoke.visible ? 1 : 0)) * 0.15;
      v.smoke.position.y = 1.3 + bob * 0.35; v.fire.position.y = 1.05 + bob * 0.2;
      if (v.boom > 0) { v.boom = Math.max(0, v.boom - dt); v.fire.scale.setScalar(1 + (0.45 - v.boom) * 6); }
      else v.fire.scale.setScalar(1);
    }
    v.body.setTranslation({ x: v.x, y: v.y + 0.7 + lift, z: v.z }, true);
    v.body.setRotation(v.root.quaternion, true);
  }

  function sweep(v, ux, uz, dist) {
    return world.castShape({ x: v.x, y: v.y + 0.8, z: v.z }, { x: 0, y: Math.sin(v.yaw / 2), z: 0, w: Math.cos(v.yaw / 2) },
      { x: ux, y: 0, z: uz }, v.shape, 0.02, dist, true, R.QueryFilterFlags.EXCLUDE_KINEMATIC, undefined, undefined, undefined,
      c => c.shapeType() !== R.ShapeType.HeightField); // heightAt handles the ground; triangle edges must not catch a bumper
  }

  function correct(v, x, z) {
    const dx = v.x - x, dz = v.z - z, dist = Math.hypot(dx, dz);
    if (dist < 1e-6) return;
    v.x = x; v.z = z;
    const hit = sweep(v, dx / dist, dz / dist, dist), go = hit ? Math.max(0, hit.time_of_impact - 0.02) : dist;
    v.x += dx / dist * Math.min(dist, go); v.z += dz / dist * Math.min(dist, go);
  }

  function scar(v, otherMass, closing) {
    if (closing < 5 || v.wrecked) return;
    v.hp = Math.max(0, v.hp - closing * otherMass / 1400);
    if (v.hp <= 0) wreck(v);
  }
  function wreck(v) {
    if (v.wrecked) return;
    v.wrecked = true; v.hp = 0; v.boom = 0.45;
    if (v.traffic) v.traffic = null;
    if (v === driven) bail(v, 'Wrecked');
    else hunt.jolt(0.08);
  }
  /** Out of the seat and off to one side. `why` is the line he sees. */
  function bail(v, why) {
    const lx = Math.cos(v.yaw), lz = -Math.sin(v.yaw);
    leave(true);
    const x = v.x + lx * (v.spec.wid / 2 + 0.9), z = v.z + lz * (v.spec.wid / 2 + 0.9);
    player.teleport(x, heightAt(x, z), z);
    player.yaw = player.lastYaw = v.yaw;
    settle();
    if (why) hunt.hud.flashText(why);
  }
  function step(v, dt, input) {
    const s = v.spec, before = v.vf;
    v.liable = Math.max(0, v.liable - dt);
    if (v === driven) v.liable = 5;
    if (v.wrecked) input = { throttle: 0, steer: 0, hand: true };
    // Dirt (off the asphalt mask) gives the tyres less to push against.
    const paved = wayDist(v.x, v.z) < ROAD + 0.8;
    drive(v, s, input, dt, v.air <= 0 && !v.tipped, paved ? 1 : 0.4);
    if (v.tipped) { const drag = Math.exp(-2.5 * dt); v.vx *= drag; v.vz *= drag; }
    v.yaw += v.spinRate * dt; v.spinRate *= Math.exp(-3 * dt);
    rock(v, dt);
    const fx = Math.sin(v.yaw), fz = Math.cos(v.yaw), sp = Math.hypot(v.vx, v.vz);
    if (sp > 0.02) {
      const ux = v.vx / sp, uz = v.vz / sp, dist = sp * dt;
      // Sweep the entire chassis, including a sliding car's long side; three forward rays missed narrow poles.
      const stop = sweep(v, ux, uz, dist);
      let go = stop ? Math.max(0, stop.time_of_impact - 0.02) : dist;
      let nx = stop?.normal1.x ?? 0, nz = stop?.normal1.z ?? 0;
      if (!stop && !inBounds(v.x + ux * dist, v.z + uz * dist)) { go = 0; nx = -ux; nz = -uz; } // the district's edge turns it back
      v.x += ux * Math.min(go, dist); v.z += uz * Math.min(go, dist);
      const nl = Math.hypot(nx, nz);
      if (nl > 1e-3) {
        nx /= nl; nz /= nl;
        const into = v.vx * nx + v.vz * nz;
        if (into < 0) {
          // what ran into the wall comes back as a third; what ran along it carries on, a little poorer
          v.vx = (v.vx - 1.3 * into * nx) * 0.92; v.vz = (v.vz - 1.3 * into * nz) * 0.92;
          v.rollRate += (nx * fz - nz * fx) * into * (s.lean ? 0.9 : 0.55);
          if (v === driven && -into > 5) hunt.jolt(Math.min(0.22, -into * 0.012));
          if (v === driven && -into > 11) hunt.crash(Math.min(18, (-into - 11) * 1.4));
          // A wall has no mass of its own; the blow is the speed he brought into it.
          scar(v, 900, -into);
        }
      }
      if (sp > RAM.min && v.liable > 0 && !v.wrecked) {
        for (const e of hunt.enemies) {
          if (e.dead || e.gone) continue;
          const dx = e.x - v.x, dz = e.z - v.z, r = e.cap?.[0] ?? 0.3;
          if (Math.abs(dx * fz - dz * fx) > s.wid / 2 + r || Math.abs(dx * fx + dz * fz) > s.len / 2 + r) continue;
          if (dx * ux + dz * uz < -0.4 || Math.abs(heightAt(e.x, e.z) + (e.air ?? 0) - v.y) > 2) continue; // behind it, or over it
          // Heavier metal keeps more of its speed and hits harder. The sedan is the 1.0 the street checks measure.
          if (hunt.ram(e, sp, ux, uz, s.mass / 1200)) { const keep = e.kind === 'brute' ? 0.25 : s.heft; v.vx *= keep; v.vz *= keep; hunt.jolt(0.07); }
        }
      }
    }
    // the ground under both axles; in free flight until it comes back up to meet the wheels
    const hb = s.base / 2, hw = s.wid / 2;
    const front = heightAt(v.x + fx * hb, v.z + fz * hb), rear = heightAt(v.x - fx * hb, v.z - fz * hb);
    const ground = (front + rear) / 2, flight = v.y + v.vy * dt - GRAVITY * dt * dt / 2;
    const ease = 1 - Math.exp(-12 * dt);
    if (flight > ground + 0.04) {
      v.y = flight; v.vy -= GRAVITY * dt; v.air += dt;
      v.pitch += (0.12 - v.pitch) * (1 - Math.exp(-1.2 * dt)); // the nose drops a little in the air
    } else {
      if (v === driven && v.air > 0.2 && v.vy < -5) hunt.jolt(Math.min(0.2, -v.vy * 0.014));
      v.vy = dt > 0 ? Math.max(-30, Math.min(12, (ground - v.y) / dt)) : 0;
      v.y = ground; v.air = 0;
      v.pitch += (-Math.atan2(front - rear, s.base) - v.pitch) * ease;
      v.roll += (Math.atan2(heightAt(v.x + fz * hw, v.z - fx * hw) - heightAt(v.x - fz * hw, v.z + fx * hw), s.wid) - v.roll) * ease;
    }
    v.lean += (Math.max(-0.6, Math.min(0.6, -v.yawRate * v.vf / 9.8)) - v.lean) * (1 - Math.exp(-8 * dt));
    v.squat += (Math.max(-0.06, Math.min(0.06, dt > 0 ? (before - v.vf) / dt * 0.004 : 0)) - v.squat) * ease;
    v.vf = v.vx * Math.sin(v.yaw) + v.vz * Math.cos(v.yaw);
    // A bike's lean at speed, or any rollover while he is in the seat, throws him.
    if (v === driven && (v.tipped || (s.throw && Math.abs(v.vf) > Math.max(8, s.top * 0.75) && Math.abs(v.lean) > 0.5))) bail(v, 'Thrown');
  }

  function enter(v) {
    const stolen = v.driver;
    driven = v; v.traffic = null; v.driver = false; v.liable = 5; eyeBefore = follow.want; follow.want = Math.max(follow.want, v.spec.eye);
    if (v.rider) v.rider.visible = true;
    if (stolen) hunt.carjack();
    hunt.hud.toast(`${v.spec.name} · W A S D drive · Space handbrake · G out`);
  }

  /** Steps out to whichever side is free. `quiet`: he has been taken elsewhere (a fall), the seat is only emptied. */
  function leave(quiet = false) {
    const v = driven;
    if (!v) return;
    if (quiet) { driven = null; follow.want = eyeBefore; if (v.rider) v.rider.visible = false; return; }
    const lx = Math.cos(v.yaw), lz = -Math.sin(v.yaw), d = v.spec.wid / 2 + 0.75;
    const exits = [[lx * d, lz * d], [-lx * d, -lz * d], [-lz * (v.spec.len / 2 + 0.9), lx * (v.spec.len / 2 + 0.9)]];
    const exit = exits.find(([dx, dz]) => {
      const x = v.x + dx, z = v.z + dz;
      ray.origin = { x: v.x, y: v.y + 0.9, z: v.z }; ray.dir = { x: dx / Math.hypot(dx, dz), y: 0, z: dz / Math.hypot(dx, dz) };
      return !world.castRay(ray, Math.hypot(dx, dz), true, undefined, undefined, player.collider, v.body) &&
        !world.intersectionWithShape({ x, y: heightAt(x, z) + 0.95, z }, { x: 0, y: 0, z: 0, w: 1 }, player.collider.shape,
          undefined, undefined, player.collider, v.body);
    });
    if (!exit) { hunt.hud.flashText('No room to step out'); return; }
    driven = null; follow.want = eyeBefore;
    if (v.rider) v.rider.visible = false;
    const x = v.x + exit[0], z = v.z + exit[1];
    player.teleport(x, heightAt(x, z), z);
    player.yaw = player.lastYaw = v.yaw;
    settle();
  }

  let follow = null;
  /** Each frame, before the player is moved. Returns true while he is driving (the caller then leaves his legs alone). */
  function update(dt, inp, eye) {
    follow = eye; told = Math.max(0, told - dt);
    let near = null;
    if (!driven) {
      let best = 3.4;
      for (const v of rides) { const d = Math.hypot(v.x - player.feet.x, v.z - player.feet.z) - v.spec.wid / 2; if (d < best) { best = d; near = v; } }
    }
    if (inp.ride) {
      if (driven && Math.hypot(driven.vx, driven.vz) > BAIL) { if (told <= 0) { told = 2; hunt.hud.flashText('Too fast to bail'); } }
      else if (driven) leave();
      else if (near && Math.hypot(near.vx, near.vz) > 2) hunt.hud.flashText('Wait for it to stop');
      else if (near && !hunt.roam && hunt.unlocked && !hunt.unlocked(near.kind)) hunt.hud.flashText('Not yours yet');
      else if (near?.net) hunt.hud.flashText('Someone is driving it');
      else if (near?.wrecked) hunt.hud.flashText('It is finished');
      else if (near?.tipped) {
        Object.assign(near, { tilt: 0, rollRate: 0, spinRate: 0, tipped: false, vx: 0, vz: 0, vy: 0, air: 0 });
        hunt.hud.toast('Back on its wheels · G to drive');
      }
      else if (near && hunt.hp > 0) { enter(near); near = null; }
    }
    for (const v of rides) {
      if (v === driven || v.net) continue;
      // left far from home and out of sight, it is brought back: every cairn keeps its wheels
      if (!v.traffic && Math.hypot(v.x - v.home.x, v.z - v.home.z) > 60 && Math.hypot(v.x - player.feet.x, v.z - player.feet.z) > 110) {
        Object.assign(v, { x: v.home.x, z: v.home.z, yaw: v.home.yaw, y: heightAt(v.home.x, v.home.z), pitch: 0, roll: 0, lean: 0,
          tilt: 0, tipped: false, rollRate: 0, spinRate: 0, vx: 0, vz: 0, vf: 0, vy: 0, air: 0, liable: 0, hp: v.spec.hull, wrecked: false, boom: 0 });
      }
    }
    // Physics gets small steps even on a long frame: no jumping through another bike or a pedestrian.
    const count = Math.max(1, Math.ceil(dt * 120)), h = dt / count;
    for (let i = 0; i < count; i++) {
      for (const v of rides) {
        if (v.net && v !== driven) continue;
        if (v === driven) step(v, h, { throttle: inp.move.y, steer: inp.move.x, hand: !!inp.jumpHeld });
        else if (v.traffic && Math.hypot(v.x - player.feet.x, v.z - player.feet.z) < 110) step(v, h, trafficInput(v, h));
        else if (Math.hypot(v.vx, v.vz) > 0.05 || v.air > 0 || Math.abs(v.rollRate) > 0.005) step(v, h, { hand: true });
      }
      for (let a = 0; a < rides.length; a++) for (let b = a + 1; b < rides.length; b++) {
        const va = rides[a], vb = rides[b], ax = va.x, az = va.z, bx = vb.x, bz = vb.z, impact = collide(va, vb);
        correct(va, ax, az); correct(vb, bx, bz); // separation must not push a parked car through a wall
        if (impact > 3) {
          scar(va, vb.spec.mass, impact); scar(vb, va.spec.mass, impact);
          for (const t of [va, vb]) if (t.traffic && impact > 6) { t.traffic.wait = 0.35; t.traffic.flee = 2.4; t.traffic.swerve = Math.random() < 0.5 ? -1 : 1; t.traffic.horn = 0.5; t.traffic.honked = false; }
          if (va === driven || vb === driven) {
            hunt.jolt(Math.min(0.22, impact * 0.012));
            if (impact > 11) hunt.crash(Math.min(18, (impact - 11) * 1.4));
          }
        }
      }
    }
    // Online, another page drives this one (net.js sets v.net ten times a second): it glides to where that page says.
    for (const v of rides) {
      const n = v.net;
      if (!n || v === driven) continue;
      const k = Math.min(1, dt * 8), age = (performance.now() - n.at) / 1000;
      v.x += (n.x + n.vx * age - v.x) * k; v.z += (n.z + n.vz * age - v.z) * k; v.y = heightAt(v.x, v.z);
      v.yaw += wrap(n.yaw - v.yaw) * k; v.vx = n.vx; v.vz = n.vz; v.air = 0;
    }
    for (const v of rides) { v.vf = v.vx * Math.sin(v.yaw) + v.vz * Math.cos(v.yaw); place(v, dt); }
    world.propagateModifiedBodyPositionsToColliders();
    if (driven) {
      const v = driven;
      player.teleport(v.x, v.y, v.z);
      player.yaw = player.lastYaw = v.yaw;
      world.timestep = dt; world.step();
      // the eye swings in behind once the mouse has been still a moment
      lookIdle = Math.abs(inp.look.x) + Math.abs(inp.look.y) > 1e-4 ? 0 : lookIdle + dt;
      if (lookIdle > 0.7 && v.vf > 2) follow.yaw += wrap(v.yaw - follow.yaw) * Math.min(1, 2.4 * dt);
      follow.boost = Math.min(4.2, Math.hypot(v.vx, v.vz) * 0.13);
    } else if (follow) follow.boost = 0;
    const v = driven ?? near;
    const hurt = v && (v.wrecked ? 'dead' : v.hp < v.spec.hull * 0.34 ? 'fire' : v.hp < v.spec.hull * 0.66 ? 'smoke' : '');
    hunt.hud.ride(v ? { driving: !!driven, name: v.spec.name, kmh: Math.hypot(v.vx, v.vz) * 3.6, sliding: v.sliding, air: v.air > 0.15, tipped: v.tipped, hurt, driver: v.driver } : null);
    return !!driven;
  }

  return { update, leave, rides, get driven() { return driven; } };
}

export function demoRide() {
  for (const [kind, s] of Object.entries(RIDES)) {
    const fresh = () => ({ yaw: 0, vx: 0, vz: 0, steer: 0 });
    let v = fresh(), fastest = 0;
    for (let i = 0; i < 60 * 14; i++) { drive(v, s, { throttle: 1 }, 1 / 60); fastest = Math.max(fastest, v.vf); }
    if (!(fastest > s.top * 0.85 && fastest <= s.top * 1.001)) throw new Error(kind + ' top speed ' + fastest);
    let z = 0;
    for (let i = 0; i < 60 * 6 && v.vf > 0.01; i++) { drive(v, s, { throttle: -1 }, 1 / 60); z += v.vz / 60; }
    if (!(z < 45 && Math.abs(v.vf) < 1.2)) throw new Error(kind + ' braking ' + z);
    const corner = (hand) => {
      const c = fresh(); c.vz = 12; let slip = 0;
      for (let i = 0; i < 90; i++) { drive(c, s, { throttle: 0.4, steer: 1, hand }, 1 / 60); slip = Math.max(slip, Math.abs(c.vx * Math.cos(c.yaw) - c.vz * Math.sin(c.yaw))); }
      return { yaw: c.yaw, speed: Math.hypot(c.vx, c.vz), slip };
    };
    const held = corner(false), slid = corner(true);
    if (!(held.yaw < -0.8 && held.speed > 8)) throw new Error(kind + ' turns right and carries its speed ' + JSON.stringify(held));
    if (!(slid.slip > held.slip * 1.5)) throw new Error(kind + ' handbrake lets the tyres go');
    v = fresh();
    for (let i = 0; i < 60 * 8; i++) drive(v, s, { throttle: -1 }, 1 / 60);
    if (!(v.vf < -s.rev * 0.7 && v.vf >= -s.rev * 1.001)) throw new Error(kind + ' reverse ' + v.vf);
    v = fresh(); v.vz = 10; drive(v, s, { throttle: 1, steer: 1 }, 1 / 60, false);
    if (v.yaw !== 0 || Math.abs(v.vz - 10) > 1e-9) throw new Error(kind + ' has no say in the air');
    for (const n of [v.yaw, v.vx, v.vz, v.steer]) if (!Number.isFinite(n)) throw new Error(kind + ' numbers');
  }
  if (!(RIDES.sports.top > RIDES.bike.top && RIDES.bike.top > RIDES.car.top && RIDES.car.top > RIDES.pickup.top && RIDES.pickup.top > RIDES.bicycle.top)) throw new Error('sports, bike, sedan, pickup, bicycle');
  if (!(RIDES.pickup.mass > RIDES.car.mass && RIDES.car.mass > RIDES.sports.mass && RIDES.sports.mass > RIDES.bike.mass && RIDES.bike.mass > RIDES.bicycle.mass)) throw new Error('mass order');
  if (!(RIDES.pickup.heft > RIDES.car.heft && RIDES.bike.throw && RIDES.bicycle.throw && !RIDES.car.throw && RIDES.pickup.hull > RIDES.car.hull)) throw new Error('the truck keeps its speed and the bikes throw you');
  if (!(RIDES.bike.top > RIDES.car.top && RIDES.bike.heft < RIDES.car.heft)) throw new Error('the bike is the quick one, the car the heavy one');
  if (!(BAIL < RIDES.car.top / 2)) throw new Error('no stepping out at speed');
  const slipOf = (mul) => {
    const c = { yaw: 0, vx: 0, vz: 14, steer: 0 };
    for (let i = 0; i < 40; i++) drive(c, RIDES.car, { throttle: 0.5, steer: 1 }, 1 / 60, true, mul);
    return Math.abs(c.vx * Math.cos(c.yaw) - c.vz * Math.sin(c.yaw));
  };
  if (!(slipOf(0.4) > slipOf(1) * 1.15)) throw new Error('dirt lets the tail out');
  const chassis = (spec, x, z, vx = 0, vz = 0) => ({ spec, x, z, y: 0, yaw: 0, vx, vz, tilt: 0, rollRate: 0, spinRate: 0 });
  const a = chassis(RIDES.car, 0, 0, 0, 20), b = chassis(RIDES.car, 0, 4);
  const energy = (v) => v.spec.mass * (v.vx ** 2 + v.vz ** 2) / 2;
  const before = energy(a) + energy(b), momentum = a.spec.mass * a.vz;
  if (collide(a, b) !== 20 || b.vz < 10 || a.vz > 10 || b.z - a.z < RIDES.car.len) throw new Error('a rear end transfers speed and separates chassis');
  if (Math.abs(a.spec.mass * a.vz + b.spec.mass * b.vz - momentum) > 1e-6 || energy(a) + energy(b) > before) throw new Error('a collision conserves momentum and loses energy');
  const bike = chassis(RIDES.bike, 0, 0), car = chassis(RIDES.car, -1.2, 0, 25);
  if (!(collide(car, bike) > 20 && bike.vx > car.vx && Math.abs(bike.rollRate) > 7)) throw new Error('a heavy broadside tips a bike');
  for (let i = 0; i < 600; i++) rock(bike, 1 / 120);
  if (!bike.tipped || Math.cos(bike.tilt) > -0.9) throw new Error('a hard impact settles upside down');
  const gentle = { tilt: 0, rollRate: 1 };
  for (let i = 0; i < 600; i++) rock(gentle, 1 / 120);
  if (gentle.tipped || Math.abs(gentle.tilt) > 0.01) throw new Error('the suspension settles after a small bump');
  const high = chassis(RIDES.car, 0, 0); high.y = 3;
  if (collide(chassis(RIDES.car, 0, 0), high) !== 0) throw new Error('an airborne chassis clears the one beneath');
}

if (globalThis.process?.argv?.[1]?.endsWith('ride.js')) demoRide();
