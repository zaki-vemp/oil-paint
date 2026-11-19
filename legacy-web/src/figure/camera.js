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

  track(yaw, dt) {
    const delta = yaw - this.yaw;
    this.yaw += Math.atan2(Math.sin(delta), Math.cos(delta)) * (1 - Math.exp(-11 * dt));
  }

  update(dt, target, look, zoom, castRay, groundAt, sweep) {
    const cam = this.camera;
    if (this.free) { cam.position.copy(this.free.position); cam.lookAt(this.free.target); return; }
    this.yaw -= look.x;
    this.pitch = THREE.MathUtils.clamp(this.pitch + look.y, -0.75, 1.2);
    this.want = THREE.MathUtils.clamp(this.want + zoom * 0.5, 2.8, 13);
    const reach = Math.min(18, this.want + (this.boost || 0));
    const goal = new THREE.Vector3(target.x, target.y + 1.35, target.z);
    if (this.pivot.lengthSq() === 0) this.pivot.copy(goal);
    this.pivot.x = damp(this.pivot.x, goal.x, 10, dt);
    this.pivot.z = damp(this.pivot.z, goal.z, 10, dt);
    this.pivot.y = damp(this.pivot.y, goal.y, 5, dt);
    const cp = Math.cos(this.pitch), sp = Math.sin(this.pitch);
    const back = new THREE.Vector3(-Math.sin(this.yaw) * cp, sp, -Math.cos(this.yaw) * cp);
    const right = new THREE.Vector3(-Math.cos(this.yaw), 0, Math.sin(this.yaw));
    const k = Math.min(1, reach / 6.4);
    const shoulder = this.side * k;
    const from = this.pivot.clone();
    let free = reach;
    if (sweep) {
      // Cover the entire near plane and sweep from the pivot so the shoulder offset also clears corners.
      const nearHeight = cam.near * Math.tan(THREE.MathUtils.degToRad(cam.fov) / 2);
      const radius = Math.hypot(cam.near, nearHeight, nearHeight * cam.aspect) + 0.04;
      const length = Math.hypot(reach, shoulder);
      const dir = back.clone().multiplyScalar(reach).addScaledVector(right, shoulder).divideScalar(length);
      const hit = sweep(this.pivot, dir, length, radius);
      if (hit != null) free = Math.max(0.06, hit * reach / length);
    } else {
      // Snowbound also checks painted trunks that have no physics body.
      from.addScaledVector(right, shoulder);
      for (const offset of [-0.22, 0, 0.22]) {
        const hit = castRay(from.clone().addScaledVector(right, offset), back, reach + 0.4);
        if (hit != null) free = Math.min(free, Math.max(0.06, hit - 0.3));
      }
    }
    // A new wall must stop the eye on this frame. Only the recovery eases out.
    this.distance = Math.min(free, damp(this.distance, free, 2.5, dt));
    if (sweep) from.addScaledVector(right, shoulder * this.distance / reach);
    cam.position.copy(from).addScaledVector(back, this.distance);
    const floor = groundAt(cam.position.x, cam.position.z) + 0.45;
    let raised = 0;
    if (cam.position.y < floor) { raised = floor - cam.position.y; cam.position.y = floor; }
    cam.lookAt(from.x - back.x * 6, from.y - back.y * 6 + this.lift * k - raised * this.sink, from.z - back.z * 6);
    this.shakeTime += dt;
    if (!this.reducedMotion && this.shake > 0) {
      const shake = free === reach ? this.shake : 0;
      cam.position.x += Math.sin(this.shakeTime * 85) * shake * 0.3;
      cam.position.y += Math.cos(this.shakeTime * 71) * shake * 0.2;
    }
  }
}
