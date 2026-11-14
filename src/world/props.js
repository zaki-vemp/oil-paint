// The reviewed terminals, supplies, cores, beacons and barriers make small service areas beside
// the shrines. Each area batches identical models and keeps its colliders off roads and fights.
// Distant areas are hidden as a whole; their geometry and the world-pinned paint never change.
import * as THREE from 'three';
import { loadStreetAsset, streetMaterials } from '../street-assets.js';
import { LEVELS } from '../game/levels.js';
import { DUELS } from '../game/duel.js';
import { ROAD, wayDist } from './land.js';

const SHAPES = {
  terminal: [0.66, 1.17, 0.53], 'supply-crate': [0.85, 0.57, 0.55],
  'power-core': [0.51, 0.92, 0.50], beacon: [0.75, 1.5, 0.7], barricade: [1.22, 0.68, 0.44],
};
export async function createStreetProps({ shared, heightAt, solids }) {
  const names = Object.keys(SHAPES);
  const models = Object.fromEntries(await Promise.all(names.map(async name => [name, await loadStreetAsset(name)])));
  const group = new THREE.Group(), shade = new THREE.Group(), colliders = [], areas = [], placements = [];
  const dummy = new THREE.Object3D(), materials = streetMaterials(shared);
  const keep = [...DUELS, ...LEVELS.flatMap(l => [...l.spots, ...(l.gate ? [l.gate] : [])])];
  let distance = 85;
  function clear(x, z, radius) {
    if (wayDist(x, z) < ROAD + radius + 0.5 || keep.some(p => Math.hypot(p.x - x, p.z - z) < radius + 3)) return false;
    return ![...solids, ...colliders].some(s => {
      if (!s.box) return Math.hypot(s.x - x, s.z - z) < radius + (s.r ?? 0.5);
      const dx = x - s.x, dz = z - s.z, c = Math.cos(s.ry ?? 0), sn = Math.sin(s.ry ?? 0);
      return Math.abs(dx * c - dz * sn) < s.box[0] + radius && Math.abs(dx * sn + dz * c) < s.box[2] + radius;
    });
  }
  for (const [index, level] of LEVELS.entries()) {
    const cp = level.checkpoint, area = new THREE.Group(), bins = Object.fromEntries(names.map(n => [n, []]));
    const entries = [['terminal', 8.5, -3], ['beacon', -8.5, -3], ['power-core', -8.5, 0],
      ['supply-crate', 9, 1], ['supply-crate', 11, 1], ['barricade', -10, 3]];
    for (const [name, dx, dz] of entries) {
      const shape = SHAPES[name], radius = Math.max(shape[0], shape[2]);
      let point;
      for (const shift of [0, 4, -4, 8, -8, 12, -12, 18, -18]) {
        const x = cp.x + dx, z = cp.z + dz + shift;
        if (clear(x, z, radius)) { point = { name, x, z, y: heightAt(x, z), yaw: Math.atan2(cp.x - x, cp.z - z) }; break; }
      }
      if (!point) continue;
      bins[name].push(point); placements.push(point);
      colliders.push({ x: point.x, y: point.y + shape[1], z: point.z, box: shape, ry: point.yaw });
    }
    for (const name of names) {
      const points = bins[name];
      if (!points.length) continue;
      models[name].scene.updateMatrixWorld(true);
      models[name].scene.traverse(o => {
        if (!o.isMesh) return;
        const geometry = o.geometry.clone().applyMatrix4(o.matrixWorld);
        const signal = o.material.name === 'Signal';
        const mesh = new THREE.InstancedMesh(geometry, signal ? materials.signal : materials.body, points.length);
        points.forEach((p, i) => {
          dummy.position.set(p.x, p.y, p.z); dummy.rotation.set(0, p.yaw, 0); dummy.scale.setScalar(1); dummy.updateMatrix();
          mesh.setMatrixAt(i, dummy.matrix);
        });
        mesh.computeBoundingSphere(); area.add(mesh);
        if (!signal) {
          const shadow = new THREE.InstancedMesh(geometry, new THREE.MeshBasicMaterial(), points.length);
          shadow.instanceMatrix = mesh.instanceMatrix; shadow.frustumCulled = false; shade.add(shadow);
        }
      });
    }
    area.name = `Service area ${index + 1}`;
    group.add(area); areas.push({ group: area, x: cp.x, z: cp.z });
  }
  return {
    group, shade, colliders, placements,
    setQuality(profile) { distance = profile.distance; },
    update(focus) { for (const area of areas) area.group.visible = Math.hypot(area.x - focus.x, area.z - focus.z) < distance; },
  };
}
