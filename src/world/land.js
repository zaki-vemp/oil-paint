// The ground of the world: strictly flat city streets and open plains (`y = 0`), three lush woodland
// parks/forests, and ridged rocky/snow-capped mountains around the corners and perimeter. The same height
// is used by the picture (a map the shaders read), by the wanderer's feet (physics) and by scenery.
import * as THREE from 'three';
import { COMMON, EXTENT, LAND_RES } from '../oilpaint/common.js';

import { noise, rng } from './noise.js';
import { SCALE } from '../game/chapters.js';
import { gridDist, GRID_MIN, SPACING } from './grid.js';
export { noise, rng };

// --- the way through ---------------------------------------------------------------------------------------------
const WAY = [[2, 96], [0, 80], [-7, 56], [3, 32], [10, 6], [3, -20], [-5, -44], [4, -68], [10, -90]].map(([x, z]) => [x * SCALE, z * SCALE]);
export const START = { x: 0, z: 78 * SCALE };
export const CLEARING = { x: 11 * SCALE, z: -96 * SCALE, r: 26 };
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
  if (x < -42 || x > 62 || z > 305 || z < -318) return g;
  const s = spineDist(x, z);
  return s < g ? s : g;
}
export const clearingAt = (x, z) => {
  const d = Math.hypot(x - CLEARING.x, z - CLEARING.z) + noise(x * 0.11, z * 0.11) * 3.5;
  return 1 - Math.min(1, Math.max(0, (d - CLEARING.r * 0.72) / (CLEARING.r * 0.5)));
};

const sstep = (a, b, x) => { const t = Math.min(1, Math.max(0, (x - a) / (b - a))); return t * t * (3 - 2 * t); };
const boxMask = (x, z, x0, x1, z0, z1, feather) => {
  const dx = Math.max(x0 - x, 0, x - x1);
  const dz = Math.max(z0 - z, 0, z - z1);
  return 1 - sstep(0, feather, Math.hypot(dx, dz));
};

/**
 * Mountain elevation in metres (>= 0). Strictly 0 across all urban streets, missions,
 * checkpoints, duels, traffic lines, the starter-bike zone (-20, 305) and Sanctuary clearing.
 */
export function mountainHeight(x, z) {
  const mNW = boxMask(x, z, -360, -196, 238, 360, 22);
  const mNE = boxMask(x, z, 196, 360, 238, 360, 22);
  const mSW = boxMask(x, z, -360, -196, -360, -164, 22);
  const mE = boxMask(x, z, 272, 360, -204, -76, 18);
  const outerX = sstep(288, 342, Math.abs(x));
  const northZ = sstep(328, 354, z);
  const southZ = sstep(-318, -354, z);
  const mRim = Math.max(outerX, northZ, southZ);

  const wMax = Math.max(mNW, mNE, mSW, mE, mRim);
  if (wMax <= 0) return 0;

  const ridge1 = 1 - Math.abs(noise(x / 68 + 1.7, z / 68 - 2.4));
  const ridge2 = 1 - Math.abs(noise(x / 31 - 4.1, z / 31 + 1.9));
  const crag = 0.5 + 0.5 * noise(x / 14 + 3.0, z / 14 - 1.2);
  const peakProfile = 0.42 + 0.44 * ridge1 * ridge1 + 0.22 * ridge2 + 0.08 * crag;

  // Toe-shape each envelope slightly so foothills blend gently up from the y = 0 plain.
  const toe = (w) => (w <= 0 ? 0 : w * (0.35 + 0.65 * w));
  const localH = (48 * toe(mNW) + 46 * toe(mNE) + 54 * toe(mSW) + 42 * toe(mE)) * peakProfile;
  const rimH = toe(mRim) * (34 + 42 * ridge1 * ridge1 + 14 * ridge2);
  return Math.max(localH, rimH);
}

/**
 * Woodland/park biome density in 0..1. Covers three named interior forests, the outer
 * greenbelt and low mountain foothills while leaving the clearing and spawn plazas open.
 */
export function forestAt(x, z) {
  const fWest = boxMask(x, z, -144, -52, -20, 98, 12);
  const fNE = boxMask(x, z, 54, 104, 56, 104, 10);
  const fSE = boxMask(x, z, 52, 144, -216, -92, 12);
  const span = Math.max(Math.abs(x), Math.abs(z));
  const outer = sstep(254, 276, span) * (1 - sstep(302, 325, span));
  const mh = mountainHeight(x, z);
  const foothill = mh > 0.05 && mh < 18 ? sstep(0.05, 2.5, mh) * (1 - sstep(10, 18, mh)) : 0;

  let f = Math.max(fWest, fNE, fSE, outer, foothill);
  if (f <= 0) return 0;
  f *= 1 - clearingAt(x, z);
  f *= sstep(14, 24, Math.hypot(x - START.x, z - START.z));
  f *= sstep(20, 30, Math.hypot(x + 20, z - 305));
  return Math.min(1, Math.max(0, f));
}

function rawHeight(x, z, wd) {
  const mh = mountainHeight(x, z);
  if (mh > 0) return mh;
  const f = forestAt(x, z);
  if (f > 0.4 && wd >= ROAD + 7) {
    return f * sstep(ROAD + 7, ROAD + 16, wd) * 0.28 * Math.max(0, noise(x / 22 + 5, z / 22 - 3));
  }
  return 0;
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
      const covered = Math.abs(x) < EXTENT - 18 && Math.abs(z) < EXTENT - 14;
      const mh = mountainHeight(x, z);
      const wd = covered && mh <= 0.8 ? wayDist(x, z) : 99;
      heights[i + j * N] = rawHeight(x, z, wd);
      const k = (i + j * N) * 4;
      // Grid roads stay straight. The trail keeps a ragged edge; a hard line on it showed every tooth.
      const ragged = gridDist(x, z) < ROAD + 1.4 ? 0 : noise(x * 0.9, z * 0.9) * 0.35 + noise(x * 0.23, z * 0.23) * 0.5;
      land[k] = Math.round(255 * (1 - sstep(ROAD - 0.5, ROAD + 0.7, wd + ragged * 0.45)));
      land[k + 1] = Math.round(255 * clearingAt(x, z));
      land[k + 2] = Math.round(255 * Math.min(1, Math.max(0, 0.5 + 0.6 * noise(x / 17 + 9, z / 17) + 0.3 * noise(x / 5, z / 5 + 4))));
      land[k + 3] = Math.round(255 * Math.max(forestAt(x, z), mh > 0.15 ? 1 : 0));
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
          vec3 Nn = normalAt(vWorld.xz);
          float h = vWorld.y;
          float day = 1.0 - uNight;
          vec3 pave = zoneGrassAt(vWorld.xz);
          vec3 neon = zoneFoliageAt(vWorld.xz);
          vec3 pale = zoneLeafAt(vWorld.xz);
          vec3 roadCol = zonePathAt(vWorld.xz);
          // Pavement in painted stone slabs with fine joints, raised kerbstones, and slate/wet-neon asphalt
          vec3 c = paint3(pave * 1.10, pave * 0.98, pave * 0.84, mix(0.32, 0.86, lit), patches);
          float slabMask = 1.0 - smoothstep(0.15, 0.65, m.a);
          vec2 slab = abs(fract(vWorld.xz / 3.2) - 0.5);
          vec2 fineSlab = abs(fract(vWorld.xz / 1.6) - 0.5);
          float joint = max(step(0.475, max(slab.x, slab.y)), 0.42 * step(0.484, max(fineSlab.x, fineSlab.y)));
          c *= 1.0 - 0.28 * joint * slabMask;

          float road = smoothstep(0.14, 0.85, m.r);
          vec2 agg = texture(tNoise, vWorld.xz * 0.62).ba;
          float grain = (agg.x - 0.5) * 0.10 + (agg.y - 0.5) * 0.06;
          float puddle = smoothstep(0.46, 0.74, patches * 0.72 + texture(tNoise, vWorld.xz / 6.8).b * 0.38);
          vec3 dayAsphalt = paint3(roadCol * 1.12, roadCol * 0.98, roadCol * 0.78, lit, patches) * (1.0 + grain * (0.35 + 0.85 * day));
          vec3 nightAsphalt = roadCol * mix(0.96, 0.62, puddle) * mix(0.74, 1.0, lit) * (1.0 + grain * 0.35);
          c = mix(c, mix(dayAsphalt, nightAsphalt, uNight), road);

          float dx = gridAxis(vWorld.x), dz = gridAxis(vWorld.z), gdist = min(dx, dz);
          float flatCity = step(abs(vWorld.x), 282.0) * step(abs(vWorld.z), 282.0) * (1.0 - smoothstep(0.3, 1.2, h));
          float walk = smoothstep(uGrid.z + 0.06, uGrid.z + 0.32, gdist) * (1.0 - smoothstep(uGrid.z + 2.2, uGrid.z + 3.3, gdist));
          c = mix(c, pave * mix(1.24, 1.36, uNight) * mix(0.78, 1.04, lit), walk * flatCity * (1.0 - road));
          // Granite kerbstone lip and dark recessed asphalt gutter around every grid block
          float kerbLip = smoothstep(uGrid.z - 0.02, uGrid.z + 0.06, gdist) * (1.0 - smoothstep(uGrid.z + 0.28, uGrid.z + 0.40, gdist));
          float kerbBlock = 0.88 + 0.12 * step(0.09, fract((dx < dz ? vWorld.z : vWorld.x) * 0.85));
          c = mix(c, pave * mix(1.48, 1.72, uNight) * kerbBlock * mix(0.80, 1.06, lit), kerbLip * flatCity * (1.0 - road) * slabMask);
          float gutter = smoothstep(uGrid.z - 0.42, uGrid.z - 0.06, gdist) * (1.0 - smoothstep(uGrid.z - 0.02, uGrid.z + 0.08, gdist));
          c *= 1.0 - 0.28 * gutter * flatCity * road;

          // Dashed lane lines, stop bars, and warm-ivory zebra crosswalks near grid intersections
          float lane = 0.0;
          if (dx < uGrid.z && dz > uGrid.z + 5.1) lane = step(0.55, fract(vWorld.z * 0.16)) * step(min(abs(dx - 1.15), abs(dx + 1.15)), 0.07);
          if (dz < uGrid.z && dx > uGrid.z + 5.1) lane = step(0.55, fract(vWorld.x * 0.16)) * step(min(abs(dz - 1.15), abs(dz + 1.15)), 0.07);
          float crosswalk = 0.0, stopBar = 0.0;
          if (dx < uGrid.z - 0.32 && dz > uGrid.z + 1.2 && dz < uGrid.z + 3.8) {
            float bar = smoothstep(0.28, 0.23, abs(fract(vWorld.x * 1.05) - 0.5));
            float ends = smoothstep(uGrid.z + 1.2, uGrid.z + 1.32, dz) * (1.0 - smoothstep(uGrid.z + 3.68, uGrid.z + 3.8, dz));
            crosswalk = bar * ends * (1.0 - smoothstep(uGrid.z - 0.48, uGrid.z - 0.32, dx));
          }
          if (dz < uGrid.z - 0.32 && dx > uGrid.z + 1.2 && dx < uGrid.z + 3.8) {
            float bar = smoothstep(0.28, 0.23, abs(fract(vWorld.z * 1.05) - 0.5));
            float ends = smoothstep(uGrid.z + 1.2, uGrid.z + 1.32, dx) * (1.0 - smoothstep(uGrid.z + 3.68, uGrid.z + 3.8, dx));
            crosswalk = max(crosswalk, bar * ends * (1.0 - smoothstep(uGrid.z - 0.48, uGrid.z - 0.32, dz)));
          }
          if (dx < uGrid.z - 0.45 && dz > uGrid.z + 4.30 && dz < uGrid.z + 4.58) stopBar = 1.0;
          if (dz < uGrid.z - 0.45 && dx > uGrid.z + 4.30 && dx < uGrid.z + 4.58) stopBar = 1.0;
          float paintMask = clamp(lane + crosswalk + stopBar, 0.0, 1.0) * flatCity;
          float paintWear = 0.90 + 0.10 * patches;
          vec3 ivoryDay = vec3(0.93, 0.90, 0.81) * mix(0.78, 1.04, lit) * paintWear;
          vec3 ivoryNight = (vec3(0.64, 0.66, 0.72) + neon * 0.14 + pale * 0.08) * paintWear;
          vec3 markCol = mix(ivoryDay, ivoryNight, uNight);
          c = mix(c, markCol * 0.92, lane * flatCity * road);
          c = mix(c, markCol, clamp(crosswalk + stopBar, 0.0, 1.0) * flatCity * road);

          // Wet-pavement neon specular streaks at night: crisp vertical reflections in rain-puddle patches
          float nightGlow = mix(0.05, 1.0, uNight);
          if (uNight > 0.01) {
            vec3 toCam = cameraPosition - vWorld;
            vec2 vDir = normalize(toCam.xz + vec2(1e-4));
            vec2 wv = vDir * vDir;
            float graze = smoothstep(0.18, 0.92, 1.0 - abs(toCam.y) * inversesqrt(dot(toCam, toCam) + 1e-4));
            float ripple = (texture(tNoise, vWorld.xz * vec2(0.22, 0.95)).g - 0.5) * 0.06;
            vec2 sNS = texture(tNoise, vec2(vWorld.x / 0.92 + ripple, vWorld.z / 16.0)).rg;
            vec2 sEW = texture(tNoise, vec2(vWorld.x / 16.0, vWorld.z / 0.92 + ripple)).gb;
            float streakFol = sNS.x * wv.y + sEW.x * wv.x;
            float streakPale = sNS.y * wv.y + sEW.y * wv.x;
            float puddlePatch = smoothstep(0.42, 0.72, puddle);
            float wetMask = road * (1.0 - paintMask * 0.85) * mix(0.10, 1.0, puddlePatch) * mix(0.25, 1.15, graze) * uNight;
            float bandFol = smoothstep(0.50, 0.68, streakFol) + 0.60 * smoothstep(0.74, 0.85, streakFol);
            float bandPale = smoothstep(0.55, 0.73, streakPale) + 0.55 * smoothstep(0.78, 0.89, streakPale);
            c += (neon * bandFol * 0.36 + pale * bandPale * 0.26) * wetMask;
            float paveWet = (1.0 - road) * slabMask * smoothstep(0.64, 0.84, patches) * graze * uNight;
            c += mix(neon, pale, step(0.5, streakPale)) * smoothstep(0.56, 0.76, streakFol) * paveWet * 0.16;
          }
          // Soft neon wash at the road edge; kept smooth so the winding trail's mask never shows teeth
          c = mix(c, neon, smoothstep(0.12, 0.42, m.r) * (1.0 - smoothstep(0.42, 0.78, m.r)) * 0.3 * nightGlow);

          // Forest floor: cel-painted grass, moss and earth replacing pavement off-road inside forest zones.
          float nature = smoothstep(0.18, 0.65, m.a) * (1.0 - smoothstep(0.2, 0.7, m.r));
          if (nature > 0.001) {
            float sunVal = lit * clamp(dot(Nn, uSunDir) * 0.55 + 0.5, 0.0, 1.0);
            vec3 dayG = mix(vec3(0.26, 0.50, 0.20), vec3(0.42, 0.64, 0.25), smoothstep(0.25, 0.75, patches));
            dayG = mix(dayG, vec3(0.35, 0.30, 0.21), smoothstep(0.66, 0.88, m.b) * 0.65);
            vec3 nightG = mix(vec3(0.05, 0.13, 0.12), vec3(0.09, 0.20, 0.16), smoothstep(0.25, 0.75, patches));
            nightG = mix(nightG, vec3(0.07, 0.07, 0.09), smoothstep(0.66, 0.88, m.b) * 0.65);
            vec3 gCol = mix(dayG, nightG, uNight);
            vec3 natureCol = paint3(gCol * 1.12, gCol * 0.88, gCol * vec3(0.58, 0.64, 0.72), sunVal, patches);
            c = mix(c, natureCol, nature);
          }

          // Mountains: stratified 3-band rock slopes rising into snow-capped alpine peaks.
          float rockMask = clamp(smoothstep(1.8, 7.0, h) * 0.88 + smoothstep(0.88, 0.70, Nn.y), 0.0, 1.0);
          if (rockMask > 0.001) {
            float sunVal = lit * clamp(dot(Nn, uSunDir) * 0.65 + 0.38, 0.0, 1.0);
            float strata = 0.5 + 0.5 * sin(h * 0.55 + patches * 3.0);
            vec3 dayRock = paint3(
              mix(vec3(0.54, 0.52, 0.50), vec3(0.60, 0.57, 0.53), strata),
              mix(vec3(0.35, 0.37, 0.43), vec3(0.39, 0.40, 0.44), strata),
              vec3(0.19, 0.22, 0.30),
              sunVal, patches
            );
            vec3 nightRock = paint3(
              mix(vec3(0.15, 0.20, 0.30), vec3(0.18, 0.22, 0.31), strata),
              vec3(0.07, 0.10, 0.17),
              vec3(0.03, 0.04, 0.08),
              sunVal, patches
            );
            vec3 rockCol = mix(dayRock, nightRock, uNight);
            float snowMask = smoothstep(26.0, 36.0, h) * smoothstep(0.48, 0.68, Nn.y);
            if (snowMask > 0.001) {
              vec3 daySnow = paint3(vec3(0.95, 0.96, 0.98), vec3(0.80, 0.85, 0.92), vec3(0.62, 0.71, 0.85), sunVal, patches);
              vec3 nightSnow = paint3(vec3(0.36, 0.46, 0.62), vec3(0.18, 0.26, 0.40), vec3(0.09, 0.14, 0.26), sunVal, patches);
              rockCol = mix(rockCol, mix(daySnow, nightSnow, uNight), snowMask);
            }
            c = mix(c, rockCol, rockMask * (1.0 - road));
          }

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
  for (const [x, z, wd] of [[0, 234, 0], [0, 0, 0], [120, 80, 0], [-20, 305, 99], [33, -288, 99]]) {
    if (rawHeight(x, z, wd) !== 0) throw new Error(`expected flat ground at (${x},${z}), got ${rawHeight(x, z, wd)}`);
  }
  if (forestAt(-95, 40) <= 0.8 || forestAt(95, -150) <= 0.8 || forestAt(0, 234) !== 0) {
    throw new Error(`forest masks unexpected: west=${forestAt(-95, 40)}, se=${forestAt(95, -150)}, start=${forestAt(0, 234)}`);
  }
  for (const [x, z, minH] of [[-265, 290, 30], [-268, -255, 30], [265, 288, 30], [0, 348, 25]]) {
    if (mountainHeight(x, z) <= minH) throw new Error(`expected mountain at (${x},${z}) > ${minH}, got ${mountainHeight(x, z)}`);
  }
  if (mountainHeight(0, 100) !== 0) throw new Error('mountain leaked into city');
}
