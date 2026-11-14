// The eye: well back from the wanderer and a little to one side, so the figure sits small in the lower third of the
// canvas and the wood is the picture. It trails softly, stays off the ground and comes in when a trunk is in the way.
import * as THREE from 'three';

const damp = (a, b, rate, dt) => b + (a - b) * Math.exp(-rate * dt);

export class Follow {
  constructor(camera) {
    this.camera = camera;
    this.yaw = 0; // looks along (sin yaw, 0, cos yaw)
    this.pitch = 0.22; // + looks down on him
    this.want = 7.2;
    this.distance = 7.2;
    this.side = 0.6;
    this.lift = 0.25;
    this.pivot = new THREE.Vector3();
    this.free = null; // { position, target }: a fixed view, for stills
    this.sink = 0; // when the ground lifts the eye, how far the look comes down with it to keep him in the picture (0..1)
    this.shake = 0;
    this.shakeTime = 0;
    this.reducedMotion = globalThis.matchMedia?.('(prefers-reduced-motion: reduce)').matches ?? false;
  }

  update(dt, target, look, zoom, castRay, groundAt) {
    const cam = this.camera;
    if (this.free) { cam.position.copy(this.free.position); cam.lookAt(this.free.target); return; }
    this.yaw -= look.x;
    this.pitch = THREE.MathUtils.clamp(this.pitch + look.y, -0.75, 1.2);
    this.want = THREE.MathUtils.clamp(this.want + zoom * 0.5, 2.8, 13);
    const goal = new THREE.Vector3(target.x, target.y + 1.35, target.z);
    if (this.pivot.lengthSq() === 0) this.pivot.copy(goal);
    this.pivot.x = damp(this.pivot.x, goal.x, 10, dt);
    this.pivot.z = damp(this.pivot.z, goal.z, 10, dt);
    this.pivot.y = damp(this.pivot.y, goal.y, 5, dt);
    const cp = Math.cos(this.pitch), sp = Math.sin(this.pitch);
    const back = new THREE.Vector3(-Math.sin(this.yaw) * cp, sp, -Math.cos(this.yaw) * cp);
    const right = new THREE.Vector3(-Math.cos(this.yaw), 0, Math.sin(this.yaw));
    const k = Math.min(1, this.want / 6.4);
    const from = this.pivot.clone().addScaledVector(right, this.side * k);
    // Sweep three rays across the camera's near plane so its edges cannot clip a trunk.
    let hit = null;
    for (const offset of [-0.22, 0, 0.22]) {
      const d = castRay(from.clone().addScaledVector(right, offset), back, this.want + 0.4);
      if (d != null) hit = hit == null ? d : Math.min(hit, d);
    }
    const free = hit == null ? this.want : Math.max(0.8, hit - 0.3);
    this.distance = free < this.distance ? damp(this.distance, free, 18, dt) : damp(this.distance, free, 2.5, dt);
    cam.position.copy(from).addScaledVector(back, this.distance);
    const floor = groundAt(cam.position.x, cam.position.z) + 0.45;
    let raised = 0;
    if (cam.position.y < floor) { raised = floor - cam.position.y; cam.position.y = floor; }
    cam.lookAt(from.x - back.x * 6, from.y - back.y * 6 + this.lift * k - raised * this.sink, from.z - back.z * 6);
    this.shakeTime += dt;
    if (!this.reducedMotion && this.shake > 0) {
      cam.position.x += Math.sin(this.shakeTime * 85) * this.shake * 0.3;
      cam.position.y += Math.cos(this.shakeTime * 71) * this.shake * 0.2;
    }
  }
}
