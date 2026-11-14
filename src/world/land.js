// The ground of the wood: gentle rises and hollows, a worn way winding north through the trees to a clearing
// where the canopy opens. The same height is used by the picture (a map the shaders read), by the wanderer's feet
// (a height field for the physics) and by everything that is planted on it.
import * as THREE from 'three';
import { COMMON, EXTENT, LAND_RES } from '../oilpaint/common.js';

import { noise, rng } from './noise.js';
import { SCALE } from '../game/chapters.js';
import { gridDist, GRID_MIN, SPACING } from './grid.js';
export { noise, rng };

// --- the way through ---------------------------------------------------------------------------------------------
const WAY = [[2, 96], [0, 80], [-7, 56], [3, 32], [10, 6], [3, -20], [-5, -44], [4, -68], [10, -90]].map(([x, z]) => [x * SCALE, z * SCALE]);
export const START = { x: 0, z: 78 * SCALE };
export const CLEARING = { x: 11 * SCALE, z: -96 * SCALE, r: 21 };
/** Half the width of the road, kerb to middle: wide enough to drive. */
export const ROAD = 3.6;
const waySamples = [];
{
  const at = (i) => WAY[Math.max(0, Math.min(WAY.length - 1, i))];
  for (let i = 0; i < WAY.length - 1; i++) {
    const p0 = at(i - 1), p1 = at(i), p2 = at(i + 1), p3 = at(i + 2);
    for (let k = 0; k < 24; k++) {
      const t = k / 24, t2 = t * t, t3 = t2 * t;
      const c = (a, b, c_, d) => 0.5 * (2 * b + (-a + c_) * t + (2 * a - 5 * b + 4 * c_ - d) * t2 + (-a + 3 * b - 3 * c_ + d) * t3);
      waySamples.push([c(p0[0], p1[0], p2[0], p3[0]), c(p0[1], p1[1], p2[1], p3[1])]);
    }
  }
  waySamples.push(WAY[WAY.length - 1]);
}
export const way = waySamples;
function spineDist(x, z) {
  let best = Infinity;
  for (let i = 0; i < waySamples.length - 1; i++) {
    const [ax, az] = waySamples[i], [bx, bz] = waySamples[i + 1];
    const dx = bx - ax, dz = bz - az;
    const t = Math.max(0, Math.min(1, ((x - ax) * dx + (z - az) * dz) / (dx * dx + dz * dz)));
    const d = Math.hypot(x - ax - dx * t, z - az - dz * t);
    if (d < best) best = d;
  }
  return best;
}
/** Metres from (x, z) to the nearest road: the winding trail, or a grid avenue or street. */
export function wayDist(x, z) {
  const g = gridDist(x, z);
  // The trail only wanders near x = 0. Off that strip the grid is the whole answer, and the
  // million-point land map does not have to walk the trail samples.
  if (Math.abs(x) > 48 || z > 230 || z < -236) return g;
  const s = spineDist(x, z);
  return s < g ? s : g;
}
export const clearingAt = (x, z) => {
  const d = Math.hypot(x - CLEARING.x, z - CLEARING.z) + noise(x * 0.11, z * 0.11) * 3.5;
  return 1 - Math.min(1, Math.max(0, (d - CLEARING.r * 0.72) / (CLEARING.r * 0.5)));
};

const sstep = (a, b, x) => { const t = Math.min(1, Math.max(0, (x - a) / (b - a))); return t * t * (3 - 2 * t); };
function rawHeight(x, z, wd) {
  let h = 2.8 * noise(x / 74 + 3.1, z / 74 - 1.7) + 1.2 * noise(x / 26, z / 26) + 0.3 * noise(x / 8.3, z / 8.3);
  h += 5.5 * sstep(70 * SCALE, -110 * SCALE, z); // the land climbs gently toward the clearing
  h -= 0.28 * Math.exp(-(wd * wd) / (2 * ROAD * ROAD)); // the road lies a little below the pavement
  return h;
}

// --- maps ------------------------------------------------------------------------------------------------------
const N = LAND_RES;
const heights = new Float32Array(N * N);
const land = new Uint8Array(N * N * 4);
/**
 * Works the maps out, a million points of ground. It takes a second or so, so it stops every few rows and waits
 * for `pause(done 0..1)`, which lets the page breathe. Nothing here can be used before it has finished.
 */
export async function prepareLand(pause) {
 for (let j = 0; j < N; j++) {
  if (pause && j % 24 === 0) await pause(j / N);
  for (let i = 0; i < N; i++) {
    const x = -EXTENT + (i / (N - 1)) * 2 * EXTENT, z = -EXTENT + (j / (N - 1)) * 2 * EXTENT;
    const covered = Math.abs(x) < 236 && Math.abs(z) < 242;
    const wd = covered ? wayDist(x, z) : 99;
    heights[i + j * N] = rawHeight(x, z, wd);
    const k = (i + j * N) * 4;
    // Grid roads stay straight. The trail keeps a ragged edge; a hard line on it showed every tooth.
    const ragged = gridDist(x, z) < ROAD + 1.4 ? 0 : noise(x * 0.9, z * 0.9) * 0.35 + noise(x * 0.23, z * 0.23) * 0.5;
    land[k] = Math.round(255 * (1 - sstep(ROAD - 0.5, ROAD + 0.7, wd + ragged * 0.45)));
    land[k + 1] = Math.round(255 * clearingAt(x, z));
    land[k + 2] = Math.round(255 * Math.min(1, Math.max(0, 0.5 + 0.6 * noise(x / 17 + 9, z / 17) + 0.3 * noise(x / 5, z / 5 + 4))));
    land[k + 3] = 255;
  }
 }
}
/** Height of the ground at (x, z), read from the same map the picture uses. */
export function heightAt(x, z) {
  const fx = Math.min(N - 1.001, Math.max(0, ((x + EXTENT) / (2 * EXTENT)) * (N - 1)));
  const fz = Math.min(N - 1.001, Math.max(0, ((z + EXTENT) / (2 * EXTENT)) * (N - 1)));
  const i = Math.floor(fx), j = Math.floor(fz), tx = fx - i, tz = fz - j;
  const a = heights[i + j * N], b = heights[i + 1 + j * N], c = heights[i + (j + 1) * N], d = heights[i + 1 + (j + 1) * N];
  return (a + (b - a) * tx) * (1 - tz) + (c + (d - c) * tx) * tz;
}

export function createLand(shared) {
  const tHeight = new THREE.DataTexture(heights, N, N, THREE.RedFormat, THREE.FloatType);
  tHeight.minFilter = tHeight.magFilter = THREE.LinearFilter;
  tHeight.needsUpdate = true;
  const tLand = new THREE.DataTexture(land, N, N);
  tLand.minFilter = tLand.magFilter = THREE.LinearFilter;
  tLand.needsUpdate = true;
  shared.tHeight.value = tHeight;
  shared.tLand.value = tLand;
  shared.uGrid.value.set(GRID_MIN, SPACING, ROAD);

  // the ground itself only carries the underpainting: a dark warm tone that shows between the dabs laid over it
  // it lies a few centimetres under the true ground, so that no facet of it can cover a leaf lying on the ground
  const seg = 512;
  const geometry = new THREE.PlaneGeometry(2 * EXTENT, 2 * EXTENT, seg, seg);
  geometry.rotateX(-Math.PI / 2);
  const pos = geometry.attributes.position;
  for (let i = 0; i < pos.count; i++) pos.setY(i, heightAt(pos.getX(i), pos.getZ(i)) - 0.04);
  const material = new THREE.ShaderMaterial({
    glslVersion: THREE.GLSL3,
    uniforms: shared,
    vertexShader: /* glsl */ `
      ${COMMON}
      out vec3 vWorld;
      void main() { vWorld = position; gl_Position = projectionMatrix * viewMatrix * vec4(position, 1.0); }`,
    fragmentShader: /* glsl */ `
      ${COMMON}
      in vec3 vWorld;
      layout(location = 0) out vec4 outC;
      void main() {
        vec4 m = texture(tLand, landUv(vWorld.xz));
        if (uAnime > 0.5) {
          float lit = woodSunAt(vWorld + vec3(0.0, 0.15, 0.0));
          float patches = texture(tNoise, vWorld.xz / 13.0).r;
          vec3 pave = zonePick(vWorld.xz, uZoneGrass[0], uZoneGrass[1], uZoneGrass[2], uZoneGrass[3]);
          vec3 neon = zonePick(vWorld.xz, uZoneFoliage[0], uZoneFoliage[1], uZoneFoliage[2], uZoneFoliage[3]);
          vec3 roadCol = zonePick(vWorld.xz, uZonePath[0], uZonePath[1], uZonePath[2], uZonePath[3]);
          vec3 c = pave * mix(0.9, 1.08, smoothstep(0.3, 0.7, patches));
          // pavement in slabs, then the road, wet enough to hold streaks of the district's neon
          vec2 slab = abs(fract(vWorld.xz / 3.2) - 0.5);
          c *= 1.0 - 0.3 * step(0.475, max(slab.x, slab.y));
          float road = smoothstep(0.14, 0.85, m.r);
          c = mix(c, roadCol, road);
          float dx = gridAxis(vWorld.x), dz = gridAxis(vWorld.z), gdist = min(dx, dz);
          float inside = step(abs(vWorld.x), 202.0) * step(abs(vWorld.z), 202.0);
          float walk = smoothstep(uGrid.z - 0.15, uGrid.z + 0.35, gdist) * (1.0 - smoothstep(uGrid.z + 2.1, uGrid.z + 3.3, gdist));
          c = mix(c, pave * 1.35, walk * inside * (1.0 - road));
          float lane = 0.0;
          if (dx < uGrid.z && dz > uGrid.z + 0.8) lane = step(0.55, fract(vWorld.z * 0.16)) * step(min(abs(dx - 1.15), abs(dx + 1.15)), 0.07);
          if (dz < uGrid.z && dx > uGrid.z + 0.8) lane = step(0.55, fract(vWorld.x * 0.16)) * step(min(abs(dz - 1.15), abs(dz + 1.15)), 0.07);
          c = mix(c, vec3(0.78, 0.76, 0.62), lane * road);
          float crosswalk = 0.0;
          if (dx < uGrid.z && dz > uGrid.z && dz < uGrid.z + 3.2) crosswalk = step(0.5, fract(vWorld.z * 1.6));
          if (dz < uGrid.z && dx > uGrid.z && dx < uGrid.z + 3.2) crosswalk = step(0.5, fract(vWorld.x * 1.6));
          c = mix(c, vec3(0.86, 0.84, 0.72), crosswalk * road);
          c += neon * 0.12 * road * smoothstep(0.5, 0.8, texture(tNoise, vWorld.xz / vec2(1.6, 11.0)).g);
          c *= mix(0.68, 1.0, lit);
          // the road's edge takes a soft wash of neon; a hard line would show every tooth of the way's mask
          c = mix(c, neon, smoothstep(0.12, 0.42, m.r) * (1.0 - smoothstep(0.42, 0.78, m.r)) * 0.3);
          float fig = figSunAt(vWorld);
          c = mix(c * uGlazeMul + uGlazeAdd, c, fig);
          outC = vec4(hazed(c, vWorld), 1.0);
          return;
        }
        // laid in broadly first: warm where the sun falls, plum where it doesn't
        // asked square by ragged square, so that what shows between the dabs is itself in touches
        vec2 sq = floor(vWorld.xz / 0.32 + (texture(tNoise, vWorld.xz / 2.3).rg - 0.5) * 1.8) * 0.32;
        float lit = step(0.5, woodSunAt(vec3(sq.x, vWorld.y + 0.2, sq.y)));
        vec3 c = mix(vec3(0.3, 0.16, 0.16), vec3(0.72, 0.44, 0.14), lit);
        c = mix(c, mix(vec3(0.21, 0.17, 0.2), vec3(0.6, 0.47, 0.33), lit), m.r);
        c *= 0.78 + 0.3 * texture(tNoise, vWorld.xz / 9.0).r;
        outC = vec4(hazed(c, vWorld), 1.0);
      }`,
  });
  const mesh = new THREE.Mesh(geometry, material);
  mesh.frustumCulled = false;

  /** Heights for the physics: `cells` squares a side over the middle `half` metres, column by column. */
  function heightField(cells = 512, half = EXTENT) {
    const out = new Float32Array((cells + 1) * (cells + 1));
    for (let col = 0; col <= cells; col++) {
      for (let row = 0; row <= cells; row++) {
        out[row + col * (cells + 1)] = heightAt(-half + (col / cells) * 2 * half, -half + (row / cells) * 2 * half);
      }
    }
    return { heights: out, cells, half };
  }
  return { mesh, heightField };
}

if (globalThis.process?.argv?.[1]?.endsWith('land.js')) {
  if (wayDist(200, 0) > 0.05) throw new Error('avenue ' + wayDist(200, 0));
  if (Math.abs(wayDist(180, 20) - 20) > 0.05) throw new Error('block ' + wayDist(180, 20));
  if (wayDist(0, 78 * SCALE) > 8) throw new Error('the start left the trail');
}
