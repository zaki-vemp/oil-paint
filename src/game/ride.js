// Cars and bikes at every cairn, with local traffic yielding to people: G takes a slow vehicle, G again steps out.
// The Eclipse SRC body (credits in public/vehicles/SOURCE.md) and the original bike/wheels share the scene’s
// painted materials. The handling is a small tyre model, not a rigid body: the engine pulls
// hardest from rest and has nothing left at top speed, the front wheels turn the heading (a bicycle model, less
// lock at speed), and the tyres scrub sideways speed off at their grip, until past the slip limit or on the
// handbrake they let go and it slides. It follows the land's heightAt on both axles, leaves the ground over a
// crest and comes down under gravity. Chassis sweeps stop walls, oriented boxes exchange momentum between
// vehicles, and a sideways impact can tip the chassis onto its side or roof. G beside a stopped wreck rights it.
// Whatever stands in its way is rammed (hunt.ram): crews, the mech, and the street's own people, at his risk.
import * as THREE from 'three';
import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';
import { paint } from '../figure/figure.js';
import { RAM } from './hunt.js';

// len/wid/base in metres; top and rev in m/s; grip and drift are how fast sideways speed is scrubbed (1/s), gripping
// and sliding; slip is the sideways speed where the tyres let go; calm is the speed at which steering lock has halved.
export const RIDES = {
  car: { name: 'Eclipse SRC', len: 4.2, wid: 1.9, base: 2.6, wheel: 0.36, top: 25, rev: 8, accel: 12, brake: 24, grip: 7, drift: 1.3, slip: 6, steer: 0.62, calm: 9, lean: 0, heft: 0.65, mass: 1200, eye: 10.5 },
  bike: { name: 'Hayate 400', len: 2.2, wid: 0.7, base: 1.45, wheel: 0.36, top: 31, rev: 4, accel: 17, brake: 22, grip: 9, drift: 1.7, slip: 7.5, steer: 0.5, calm: 8, lean: 1, heft: 0.45, mass: 240, eye: 8.5 },
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
 */
export function drive(v, s, { throttle = 0, steer = 0, hand = false } = {}, dt, grounded = true) {
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
    const before = Math.hypot(vf, vs), slide = hand || Math.abs(vs) > s.slip;
    vs *= Math.exp(-(slide ? s.drift : s.grip) * dt);
    // gripping tyres turn the car's speed with it rather than burn it: three quarters of the scrub comes back
    const after = Math.hypot(vf, vs);
    if (!slide && after > 1e-3) { const k = (after + (before - after) * 0.75) / after; vf *= k; vs *= k; }
    v.sliding = slide && Math.abs(vs) > 2;
  }
  v.vx = fx * vf + fz * vs; v.vz = fz * vf - fx * vs; v.vf = vf;
  return v;
}

/** A side silhouette (z forward, y up) pulled across `width`, centred on x. */
function profile(points, width) {
  const g = new THREE.ExtrudeGeometry(new THREE.Shape(points.map(([z, y]) => new THREE.Vector2(z, y))), { depth: width, bevelEnabled: false });
  return g.rotateY(-Math.PI / 2).translate(width / 2, 0, 0);
}

const BODIES = [
  [[0.2, 0.86, 0.95], [0.08, 0.5, 0.62], [0.04, 0.2, 0.3]],
  [[0.98, 0.3, 0.55], [0.66, 0.14, 0.36], [0.3, 0.06, 0.2]],
  [[0.94, 0.94, 0.9], [0.6, 0.62, 0.66], [0.3, 0.32, 0.4]],
  [[0.98, 0.84, 0.16], [0.7, 0.5, 0.08], [0.34, 0.22, 0.06]],
];
const DARK = [[0.2, 0.22, 0.28], [0.1, 0.11, 0.15], [0.04, 0.05, 0.08]];
const GLASS = [[0.3, 0.4, 0.5], [0.1, 0.16, 0.24], [0.03, 0.05, 0.1]];
const CLOTH = [[0.36, 0.7, 0.86], [0.2, 0.44, 0.62], [0.1, 0.2, 0.34]];
const SKIN = [[1.0, 0.8, 0.64], [0.78, 0.55, 0.42], [0.45, 0.3, 0.28]];

export async function createRides({ scene, shared, player, heightAt, hunt, route, checkpoints, inBounds, settle }) {
  const carModel = await new GLTFLoader().loadAsync('./vehicles/eclipse.glb');
  const R = player.R, world = player.world;
  const ray = new R.Ray({ x: 0, y: 0, z: 0 }, { x: 0, y: 0, z: 1 });
  const glow = (c) => new THREE.MeshBasicMaterial({ color: c });
  const dark = paint(shared, DARK, { grain: 3 }), glass = paint(shared, GLASS, { grain: 3 });
  const lamps = { white: glow(0xeaf6ff), red: glow(0xff3040), cyan: glow(0x00f0ff), pink: glow(0xff2a6d) };
  const tyre = (r, w) => new THREE.CylinderGeometry(r, r, w, 12).rotateZ(Math.PI / 2);
  const add = (parent, geometry, material, x = 0, y = 0, z = 0) => {
    const m = new THREE.Mesh(geometry, material); m.position.set(x, y, z); parent.add(m); return m;
  };

  function build(kind, n) {
    const s = RIDES[kind], root = new THREE.Group(), shell = new THREE.Group(), wheels = [];
    root.rotation.order = 'YXZ'; root.add(shell);
    const body = paint(shared, BODIES[n % BODIES.length], { grain: 3 }), neon = n % 2 ? lamps.pink : lamps.cyan;
    const wheel = (x, z, width, front) => {
      const pivot = new THREE.Group(), spin = new THREE.Group();
      pivot.position.set(x, s.wheel, z); pivot.add(spin); root.add(pivot);
      add(spin, tyre(s.wheel, width), dark); add(spin, tyre(s.wheel * 0.5, width + 0.03), neon);
      add(spin, new THREE.BoxGeometry(width + 0.04, s.wheel * 1.5, 0.07), dark); // a spoke, so the turning shows
      wheels.push({ pivot, spin, front });
    };
    let rider = null;
    if (kind === 'car') {
      const model = carModel.scene.clone(true);
      model.traverse(o => {
        if (!o.isMesh) return;
        const name = o.material.name;
        o.material = /Color|Body/.test(name) ? body : /Window$|Glass|Mirror/.test(name) ? glass : /RearLight|BrakeLight|Taillight/.test(name) ? lamps.red : /ReverseLight|^Light|Headlight/.test(name) ? lamps.white : /Signal|Accent/.test(name) ? neon : dark;
      });
      shell.add(model);
      for (const x of [-1, 1]) {
        add(shell, new THREE.BoxGeometry(0.04, 0.05, 3.5), neon, x * (s.wid / 2 + 0.005), 0.46, 0);
      }
      for (const x of [-1, 1]) for (const z of [-1, 1]) wheel(x * (s.wid / 2 - 0.12), z * s.base / 2, 0.26, z > 0);
    } else {
      add(shell, profile([[-1.0, 0.56], [-0.95, 0.96], [-0.2, 0.9], [0.42, 1.12], [0.95, 0.82], [0.72, 0.46], [-0.4, 0.4]], 0.34), body);
      add(shell, new THREE.BoxGeometry(0.72, 0.05, 0.05), dark, 0, 1.16, 0.46);
      add(shell, new THREE.BoxGeometry(0.36, 0.04, 1.15), neon, 0, 0.4, 0);
      add(shell, new THREE.BoxGeometry(0.2, 0.1, 0.06), lamps.white, 0, 0.96, 0.9);
      add(shell, new THREE.BoxGeometry(0.22, 0.06, 0.05), lamps.red, 0, 0.9, -0.99);
      wheel(0, s.base / 2, 0.16, true); wheel(0, -s.base / 2, 0.2, false);
      // He is hidden while he rides, so the bike carries a plain seated figure in his colours.
      rider = new THREE.Group(); rider.visible = false; shell.add(rider);
      const cloth = paint(shared, CLOTH), skin = paint(shared, SKIN);
      add(rider, new THREE.BoxGeometry(0.4, 0.6, 0.26), cloth, 0, 1.36, -0.22).rotation.x = 0.45;
      add(rider, new THREE.SphereGeometry(0.16, 8, 6), skin, 0, 1.78, 0.02);
      add(rider, new THREE.SphereGeometry(0.17, 8, 6, 0, Math.PI * 2, 0, 1.6), dark, 0, 1.8, 0);
      for (const x of [-1, 1]) {
        add(rider, new THREE.BoxGeometry(0.1, 0.1, 0.62), cloth, x * 0.26, 1.34, 0.16).rotation.x = 0.35;
        add(rider, new THREE.BoxGeometry(0.15, 0.46, 0.17), cloth, x * 0.2, 0.9, -0.12).rotation.x = -0.9;
        add(rider, new THREE.BoxGeometry(0.13, 0.44, 0.14), dark, x * 0.2, 0.56, -0.1);
      }
    }
    scene.add(root);
    return { root, shell, wheels, rider };
  }

  const rides = [];
  function park(kind, x, z, yaw) {
    const s = RIDES[kind], y = heightAt(x, z);
    const body = world.createRigidBody(R.RigidBodyDesc.kinematicPositionBased().setTranslation(x, y + 0.7, z));
    const collider = world.createCollider(R.ColliderDesc.cuboid(s.wid / 2, 0.6, s.len / 2).setFriction(0), body);
    const v = { kind, spec: s, x, y, z, yaw, vx: 0, vz: 0, vy: 0, vf: 0, steer: 0, yawRate: 0, air: 0, pitch: 0, roll: 0, lean: 0, squat: 0, spin: 0,
      tilt: 0, rollRate: 0, spinRate: 0, tipped: false, liable: 0, traffic: null,
      shape: new R.Cuboid(s.wid / 2, 0.4, s.len / 2), home: { x, z, yaw }, body, collider, ...build(kind, rides.length) };
    rides.push(v); place(v, 0);
    return v;
  }
  // A car on one kerb and a bike on the other, a little past every cairn, pointing down the way.
  for (const cp of checkpoints) {
    let i = 0;
    for (let k = 1; k < route.length - 1; k++) if (Math.hypot(route[k][0] - cp.x, route[k][1] - cp.z) < Math.hypot(route[i][0] - cp.x, route[i][1] - cp.z)) i = k;
    for (const [kind, ahead, side] of [['car', 4, 2.4], ['bike', 2, -2.8]]) {
      const j = Math.min(route.length - 2, i + ahead), [ax, az] = route[j], [bx, bz] = route[j + 1], l = Math.hypot(bx - ax, bz - az) || 1;
      park(kind, ax + (bz - az) / l * side, az - (bx - ax) / l * side, Math.atan2(bx - ax, bz - az));
    }
  }

  // Metres along the same way used by the land. Each district keeps its own traffic behind its gates.
  const lengths = [0];
  for (let i = 1; i < route.length; i++) lengths.push(lengths[i - 1] + Math.hypot(route[i][0] - route[i - 1][0], route[i][1] - route[i - 1][1]));
  const nearest = (x, z) => {
    let best = Infinity, at = 0;
    for (let i = 0; i < route.length - 1; i++) {
      const [ax, az] = route[i], [bx, bz] = route[i + 1], dx = bx - ax, dz = bz - az, len = lengths[i + 1] - lengths[i];
      const t = Math.max(0, Math.min(1, ((x - ax) * dx + (z - az) * dz) / (len * len)));
      const d = Math.hypot(x - ax - dx * t, z - az - dz * t);
      if (d < best) { best = d; at = lengths[i] + len * t; }
    }
    return at;
  };
  function lane(at, dir) {
    at = Math.max(0, Math.min(lengths.at(-1) - 0.001, at));
    let i = 0;
    while (lengths[i + 1] < at) i++;
    const [ax, az] = route[i], [bx, bz] = route[i + 1], len = lengths[i + 1] - lengths[i], t = (at - lengths[i]) / len;
    const fx = (bx - ax) / len * dir, fz = (bz - az) / len * dir;
    return { x: ax + (bx - ax) * t - fz * 1.5, z: az + (bz - az) * t + fx * 1.5, yaw: Math.atan2(fx, fz) };
  }
  for (const [i, cp] of checkpoints.entries()) {
    const lo = nearest(cp.x, cp.z) + 12, hi = nearest(checkpoints[i + 1]?.x ?? route.at(-1)[0], checkpoints[i + 1]?.z ?? route.at(-1)[1]) - 14;
    if (hi - lo < 12) continue;
    const dir = i % 2 ? -1 : 1, at = (lo + hi) / 2, p = lane(at, dir);
    const v = park(i % 3 === 1 ? 'bike' : 'car', p.x, p.z, p.yaw);
    v.traffic = { lo, hi, dir, cruise: 7 + i % 3, wait: 0 };
    if (v.rider) v.rider.visible = true;
  }

  function trafficInput(v, dt) {
    const t = v.traffic, at = nearest(v.x, v.z), remaining = t.dir > 0 ? t.hi - at : at - t.lo;
    const sp = Math.hypot(v.vx, v.vz);
    // A trip ends at the district boundary. Recycle only out of sight, with an empty starting lane.
    if (remaining < 2 && sp < 0.7 && Math.hypot(v.x - player.feet.x, v.z - player.feet.z) > 95) {
      const p = lane(t.dir > 0 ? t.lo : t.hi, t.dir);
      if (Math.hypot(p.x - player.feet.x, p.z - player.feet.z) > 95 && !rides.some(o => o !== v && Math.hypot(o.x - p.x, o.z - p.z) < 7)) {
        Object.assign(v, p, { y: heightAt(p.x, p.z), vx: 0, vz: 0, vy: 0, tilt: 0, tipped: false, rollRate: 0, spinRate: 0, air: 0, liable: 0 });
      }
    }
    const target = lane(at + t.dir * Math.max(5, sp * 0.8), t.dir), fx = Math.sin(v.yaw), fz = Math.cos(v.yaw);
    const angle = wrap(Math.atan2(target.x - v.x, target.z - v.z) - v.yaw);
    let want = Math.min(t.cruise, Math.sqrt(Math.max(0, remaining - 2) * 5), Math.abs(angle) > 0.5 ? 4 : Infinity);
    const stopping = 3 + sp * sp / (2 * v.spec.brake) + sp * 0.4;
    const blocked = (x, z, radius) => {
      const dx = x - v.x, dz = z - v.z, ahead = dx * fx + dz * fz - v.spec.len / 2;
      return ahead > -0.5 && ahead < stopping + radius && Math.abs(dx * fz - dz * fx) < v.spec.wid / 2 + radius + 0.5;
    };
    if (v.tipped || hunt.enemies.some(e => !e.dead && !e.gone && blocked(e.x, e.z, 0.45)) ||
      (!driven && blocked(player.feet.x, player.feet.z, 0.6)) || rides.some(o => o !== v && blocked(o.x, o.z, o.spec.wid / 2))) want = 0;
    t.wait = Math.max(0, t.wait - dt);
    if (t.wait > 0) want = 0;
    return { throttle: want === 0 && sp < 0.7 ? 0 : want > sp + 0.3 ? 0.55 : want < sp - 0.1 ? -1 : 0.15, steer: Math.max(-1, Math.min(1, -angle * 2.5)), hand: want === 0 && sp < 0.7 };
  }

  /** Puts the solids where the numbers say: heading, the land's tilt, a bike's lean, the body's squat, the wheels. */
  function place(v, dt) {
    const s = v.spec;
    // Rotate round the chassis centre, lifting the roof/side clear of the ground after a rollover.
    const roll = v.roll + v.tilt + v.lean * s.lean;
    const lift = Math.abs(Math.sin(v.tilt)) * s.wid / 2 + Math.abs(Math.cos(v.tilt)) * 0.7 - 0.7;
    v.root.rotation.set(v.pitch, v.yaw, roll);
    const offset = new THREE.Vector3(0, -0.7, 0).applyEuler(v.root.rotation);
    v.root.position.set(v.x + offset.x, v.y + 0.7 + lift + offset.y, v.z + offset.z);
    v.shell.rotation.set(v.squat, 0, s.lean ? 0 : -v.lean * 0.16); // a car's body rolls out of the turn its tyres hold
    v.spin += v.vf * dt / s.wheel;
    for (const w of v.wheels) { w.spin.rotation.x = v.spin; if (w.front) w.pivot.rotation.y = -v.steer; }
    v.body.setTranslation({ x: v.x, y: v.y + 0.7 + lift, z: v.z }, true);
    v.body.setRotation(v.root.quaternion, true);
  }

  let driven = null, eyeBefore = 0, lookIdle = 0, told = 0;

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

  function step(v, dt, input) {
    const s = v.spec, before = v.vf;
    v.liable = Math.max(0, v.liable - dt);
    if (v === driven) v.liable = 5;
    drive(v, s, input, dt, v.air <= 0 && !v.tipped);
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
        }
      }
      if (sp > RAM.min && v.liable > 0) {
        for (const e of hunt.enemies) {
          if (e.dead || e.gone) continue;
          const dx = e.x - v.x, dz = e.z - v.z, r = e.cap?.[0] ?? 0.3;
          if (Math.abs(dx * fz - dz * fx) > s.wid / 2 + r || Math.abs(dx * fx + dz * fz) > s.len / 2 + r) continue;
          if (dx * ux + dz * uz < -0.4 || Math.abs(heightAt(e.x, e.z) + (e.air ?? 0) - v.y) > 2) continue; // behind it, or over it
          if (hunt.ram(e, sp, ux, uz)) { const keep = e.kind === 'brute' ? 0.25 : s.heft; v.vx *= keep; v.vz *= keep; hunt.jolt(0.07); }
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
  }

  function enter(v) {
    driven = v; v.traffic = null; v.liable = 5; eyeBefore = follow.want; follow.want = Math.max(follow.want, v.spec.eye);
    if (v.rider) v.rider.visible = true;
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
      else if (near?.tipped) {
        Object.assign(near, { tilt: 0, rollRate: 0, spinRate: 0, tipped: false, vx: 0, vz: 0, vy: 0, air: 0 });
        hunt.hud.toast('Back on its wheels · G to drive');
      }
      else if (near && hunt.hp > 0) { enter(near); near = null; }
    }
    for (const v of rides) {
      if (v === driven) continue;
      // left far from home and out of sight, it is brought back: every cairn keeps its wheels
      if (!v.traffic && Math.hypot(v.x - v.home.x, v.z - v.home.z) > 60 && Math.hypot(v.x - player.feet.x, v.z - player.feet.z) > 110) {
        Object.assign(v, { x: v.home.x, z: v.home.z, yaw: v.home.yaw, y: heightAt(v.home.x, v.home.z), pitch: 0, roll: 0, lean: 0,
          tilt: 0, tipped: false, rollRate: 0, spinRate: 0, vx: 0, vz: 0, vf: 0, vy: 0, air: 0, liable: 0 });
      }
    }
    // Physics gets small steps even on a long frame: no jumping through another bike or a pedestrian.
    const count = Math.max(1, Math.ceil(dt * 120)), h = dt / count;
    for (let i = 0; i < count; i++) {
      for (const v of rides) {
        if (v === driven) step(v, h, { throttle: inp.move.y, steer: inp.move.x, hand: !!inp.jumpHeld });
        else if (v.traffic && Math.hypot(v.x - player.feet.x, v.z - player.feet.z) < 110) step(v, h, trafficInput(v, h));
        else if (Math.hypot(v.vx, v.vz) > 0.05 || v.air > 0 || Math.abs(v.rollRate) > 0.005) step(v, h, { hand: true });
      }
      for (let a = 0; a < rides.length; a++) for (let b = a + 1; b < rides.length; b++) {
        const va = rides[a], vb = rides[b], ax = va.x, az = va.z, bx = vb.x, bz = vb.z, impact = collide(va, vb);
        correct(va, ax, az); correct(vb, bx, bz); // separation must not push a parked car through a wall
        if (impact > 3) {
          if (va.traffic) va.traffic.wait = 2;
          if (vb.traffic) vb.traffic.wait = 2;
          if (va === driven || vb === driven) {
            hunt.jolt(Math.min(0.22, impact * 0.012));
            if (impact > 11) hunt.crash(Math.min(18, (impact - 11) * 1.4));
          }
        }
      }
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
    }
    const v = driven ?? near;
    hunt.hud.ride(v ? { driving: !!driven, name: v.spec.name, kmh: Math.hypot(v.vx, v.vz) * 3.6, sliding: v.sliding, air: v.air > 0.15, tipped: v.tipped } : null);
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
  if (!(RIDES.bike.top > RIDES.car.top && RIDES.bike.heft < RIDES.car.heft)) throw new Error('the bike is the quick one, the car the heavy one');
  if (!(BAIL < RIDES.car.top / 2)) throw new Error('no stepping out at speed');
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
