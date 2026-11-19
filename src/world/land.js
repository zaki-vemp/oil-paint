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
      float stripe(float p, float lo, float hi, float width) {
        return smoothstep(lo - width, lo + width, p) * (1.0 - smoothstep(hi - width, hi + width, p));
      }
      // Smooth wide-filter terrain normal prevents staircase moire on steep alpine ridges
      vec3 terrainNormal(vec2 xz) {
        const float e = 1.25;
        float hx = heightAt(xz + vec2(e, 0.0)) - heightAt(xz - vec2(e, 0.0));
        float hz = heightAt(xz + vec2(0.0, e)) - heightAt(xz - vec2(0.0, e));
        return normalize(vec3(-hx, 2.0 * e, -hz));
      }
      // Animated concentric raindrop ripples in wet night puddles
      vec2 rainRipple(vec2 p, float t) {
        vec2 cell = floor(p);
        vec2 f = fract(p) - 0.5;
        vec4 rnd = hash4(cell, 19.3);
        float life = fract(t * (0.85 + 0.55 * rnd.x) + rnd.y);
        vec2 center = (rnd.zw - 0.5) * 0.52;
        vec2 d = f - center;
        float dist = length(d);
        float ring = sin((dist - life * 0.48) * 52.0) * exp(-abs(dist - life * 0.48) * 18.0) * (1.0 - life) * smoothstep(0.02, 0.08, life);
        return (d / max(dist, 0.01)) * ring * 0.048;
      }
      void main() {
        vec4 m = texture(tLand, landUv(vWorld.xz));
        float h = vWorld.y;
        vec3 baseN = h > 0.04 ? terrainNormal(vWorld.xz) : vec3(0.0, 1.0, 0.0);
        vec3 V = normalize(cameraPosition - vWorld);
        float distCam = length(cameraPosition - vWorld);
        float detailFade = 1.0 - smoothstep(26.0, 90.0, distCam);

        vec4 nMacro = texture(tNoise, vWorld.xz / 18.0);
        vec4 nMed = texture(tNoise, vWorld.xz / 5.5);
        vec4 detFine = texture(tDetail, vWorld.xz * 0.55);
        vec4 detMicro = texture(tDetail, vWorld.xz * 1.45);
        vec4 detCoarse = texture(tDetail, vWorld.xz * 0.12);
        vec2 aa = clamp(fwidth(vWorld.xz) * 0.5, vec2(0.014), vec2(0.64));

        // 1. Concrete / Granite Sidewalk Paving Slabs
        float slabMask = 1.0 - smoothstep(0.15, 0.65, m.a);
        vec2 slab = abs(fract(vWorld.xz / 3.2) - 0.5);
        vec2 fineSlab = abs(fract(vWorld.xz / 1.6) - 0.5);
        vec2 seam = smoothstep(vec2(0.478) - aa / 3.2, vec2(0.478) + aa / 3.2, slab);
        vec2 hairline = smoothstep(vec2(0.486) - aa / 1.6, vec2(0.486) + aa / 1.6, fineSlab);
        float joint = max(max(seam.x, seam.y), 0.45 * max(hairline.x, hairline.y)) * slabMask;
        vec2 tileId = floor(vWorld.xz / 1.6);
        float tileVar = 0.88 + 0.22 * hash4(tileId, 3.1).x;

        vec3 paveBase = mix(vec3(0.20, 0.21, 0.23), vec3(0.28, 0.28, 0.29), nMacro.r) * tileVar;
        paveBase *= 0.84 + 0.26 * detCoarse.b;
        paveBase *= 1.0 - 0.42 * joint;
        vec3 albedo = paveBase;
        float rough = mix(0.76, 0.58, nMed.g * 0.6) + joint * 0.16;
        float metal = 0.0;
        float coat = 0.0;
        float sss = 0.0;
        vec2 bumpXY = (detFine.rg * 2.0 - 1.0) * 0.22 * detailFade;

        // 2. Wet Crushed-Aggregate Asphalt Roads & Rainwater Puddles
        float road = smoothstep(0.14, 0.85, m.r);
        float dx = gridAxis(vWorld.x), dz = gridAxis(vWorld.z), gdist = min(dx, dz);
        float flatCity = step(abs(vWorld.x), 282.0) * step(abs(vWorld.z), 282.0) * (1.0 - smoothstep(0.3, 1.2, h));

        // Sidewalk apron, bevelled granite curbstones, and damp gutter
        float walk = smoothstep(uGrid.z + 0.06, uGrid.z + 0.30, gdist) * (1.0 - smoothstep(uGrid.z + 2.2, uGrid.z + 3.2, gdist));
        albedo = mix(albedo, paveBase * 1.18, walk * flatCity * (1.0 - road));
        float kerbLip = smoothstep(uGrid.z - 0.02, uGrid.z + 0.06, gdist) * (1.0 - smoothstep(uGrid.z + 0.26, uGrid.z + 0.36, gdist));
        float kerbBlock = 0.88 + 0.12 * step(0.08, fract((dx < dz ? vWorld.z : vWorld.x) * 0.85));
        albedo = mix(albedo, vec3(0.32, 0.33, 0.35) * kerbBlock * (0.88 + 0.22 * detFine.b), kerbLip * flatCity * (1.0 - road) * slabMask);
        rough = mix(rough, 0.56, kerbLip * flatCity * (1.0 - road));

        // Asphalt surface with fine crushed tarmac grain, tar micro-cracks, tire polish tracks & realistic puddles
        float tw = (min(dx, dz) - 1.75) / 0.65;
        float tireWear = exp(-tw * tw) * flatCity;
        float crack = smoothstep(0.14, 0.03, detCoarse.a);
        vec3 asphaltCol = mix(vec3(0.022, 0.024, 0.029), vec3(0.038, 0.040, 0.046), detMicro.b) * (0.86 + 0.24 * nMacro.g);
        asphaltCol *= 1.0 - crack * 0.32;
        asphaltCol *= 1.0 - 0.18 * tireWear;
        float gutter = smoothstep(uGrid.z - 0.44, uGrid.z - 0.06, gdist) * (1.0 - smoothstep(uGrid.z - 0.02, uGrid.z + 0.08, gdist));
        asphaltCol *= 1.0 - 0.35 * gutter * flatCity;

        // Rainwater puddle mask (stronger & slicker at night and in gutters; subtle dampness by day)
        float wetRaw = nMacro.r * 0.58 + nMed.b * 0.42 + gutter * 0.28;
        float puddle = smoothstep(mix(0.72, 0.50, uNight), mix(0.88, 0.72, uNight), wetRaw) * flatCity;
        float damp = smoothstep(mix(0.45, 0.22, uNight), mix(0.78, 0.58, uNight), wetRaw) * flatCity;
        asphaltCol *= mix(1.0, 0.54, max(puddle * 0.9, damp * 0.45 * uNight));

        float asphaltRough = mix(mix(0.84, 0.46, damp * (0.35 + 0.65 * uNight)), 0.045, puddle);
        asphaltRough = mix(asphaltRough, asphaltRough * 0.78, tireWear * 0.5);
        vec2 asphaltBump = (detMicro.rg * 2.0 - 1.0) * mix(0.14, 0.012, puddle) * detailFade;
        if (puddle > 0.05 && uNight > 0.15 && distCam < 38.0) {
          vec2 rip = rainRipple(vWorld.xz * 1.45, uTime) + rainRipple(vWorld.xz * 2.1 + 3.7, uTime * 1.15);
          asphaltBump += rip * puddle * uNight * (1.0 - smoothstep(16.0, 38.0, distCam));
        }

        // Crisp worn thermoplastic road markings (lane dashes, stop bars & crosswalks)
        float lane = 0.0;
        if (dx < uGrid.z && dz > uGrid.z + 5.1) {
          lane = stripe(dx, 1.08, 1.22, aa.x) * stripe(fract(vWorld.z * 0.16), 0.55, 0.98, aa.y * 0.16);
        } else if (dz < uGrid.z && dx > uGrid.z + 5.1) {
          lane = stripe(dz, 1.08, 1.22, aa.y) * stripe(fract(vWorld.x * 0.16), 0.55, 0.98, aa.x * 0.16);
        }
        float crosswalk = 0.0, stopBar = 0.0;
        float across = dx < dz ? dx : dz, along = max(dx, dz);
        if (across < uGrid.z && along < uGrid.z + 5.3) {
          float wa = dx < dz ? aa.x : aa.y, wb = dx < dz ? aa.y : aa.x;
          float coord = dx < dz ? vWorld.x : vWorld.z;
          float ends = 1.0 - smoothstep(uGrid.z - 0.43 - wa, uGrid.z - 0.43 + wa, across);
          float bar = stripe(fract(coord * 1.05), 0.25, 0.75, wa * 1.05);
          crosswalk = ends * bar * stripe(along, uGrid.z + 1.26, uGrid.z + 3.74, wb);
          stopBar = ends * stripe(along, uGrid.z + 4.30, uGrid.z + 4.58, wb);
        }
        float paintMask = clamp(lane + crosswalk + stopBar, 0.0, 1.0) * flatCity * road;
        float paintWear = smoothstep(0.18, 0.46, detFine.b) * (0.78 + 0.22 * nMed.r);
        vec3 markAlbedo = vec3(0.66, 0.63, 0.56) * mix(0.65, 1.0, paintWear);
        asphaltCol = mix(asphaltCol, markAlbedo, paintMask * (0.75 + 0.25 * paintWear));
        asphaltRough = mix(asphaltRough, mix(0.62, 0.24, puddle), paintMask);

        albedo = mix(albedo, asphaltCol, road);
        rough = mix(rough, asphaltRough, road);
        coat = mix(damp * 0.18 * uNight * slabMask, max(puddle * 0.95, damp * 0.35 * uNight), road) * (1.0 - paintMask * 0.6);
        bumpXY = mix(bumpXY, asphaltBump, road);

        // 3. Park Grass, Clover & Woodland Loam
        float nature = smoothstep(0.16, 0.64, m.a) * (1.0 - smoothstep(0.2, 0.7, m.r));
        if (nature > 0.001) {
          vec3 grassDeep = vec3(0.055, 0.135, 0.036);
          vec3 grassBright = vec3(0.115, 0.225, 0.052);
          vec3 loam = vec3(0.095, 0.072, 0.048);
          vec3 gAlb = mix(grassDeep, grassBright, smoothstep(0.22, 0.78, nMacro.r * 0.65 + detFine.b * 0.35));
          gAlb = mix(gAlb, loam, smoothstep(0.64, 0.90, m.b) * 0.72);
          gAlb *= 0.78 + 0.38 * detFine.b;
          albedo = mix(albedo, gAlb, nature);
          rough = mix(rough, 0.86, nature);
          coat = mix(coat, 0.0, nature);
          sss = mix(sss, 0.14, nature);
          bumpXY = mix(bumpXY, (detFine.rg * 2.0 - 1.0) * 0.35 * detailFade, nature);
        }

        // 4. Photorealistic Stratified Alpine Mountains (Triplanar Rock + Windswept Snowpack)
        vec3 N = normalize(vec3(baseN.x + bumpXY.x, baseN.y, baseN.z + bumpXY.y));
        float rockMask = clamp(smoothstep(1.4, 6.5, h) * 0.90 + (1.0 - smoothstep(0.68, 0.88, baseN.y)), 0.0, 1.0) * (1.0 - road);
        if (rockMask > 0.001) {
          vec3 blend = pow(abs(baseN), vec3(4.0));
          blend /= max(blend.x + blend.y + blend.z, 1e-4);
          vec4 tx = texture(tDetail, vWorld.zy * 0.085);
          vec4 ty = texture(tDetail, vWorld.xz * 0.085);
          vec4 tz = texture(tDetail, vWorld.xy * 0.085);
          vec4 tri = tx * blend.x + ty * blend.y + tz * blend.z;
          vec4 triMacro = texture(tNoise, vWorld.xz * 0.025 + vec2(h * 0.012, 0.0));

          float strata = 0.5 + 0.5 * sin(h * 0.42 + triMacro.r * 5.2 + tri.b * 1.4);
          vec3 granite = mix(vec3(0.085, 0.092, 0.105), vec3(0.195, 0.185, 0.172), strata * 0.65 + tri.b * 0.35);
          granite *= 0.68 + 0.48 * smoothstep(0.08, 0.45, tri.a);
          // Subtle lichen/moss on lower damp slopes
          float foothillMoss = (1.0 - smoothstep(4.0, 18.0, h)) * smoothstep(0.55, 0.82, baseN.y);
          granite = mix(granite, vec3(0.075, 0.125, 0.055), foothillMoss * 0.55);

          // Windswept alpine snowpack on upper ridges & ledges
          float snowMask = smoothstep(22.0, 35.0, h + (triMacro.g - 0.5) * 9.0) * smoothstep(0.46, 0.72, baseN.y + (tri.b - 0.5) * 0.18);
          vec3 snowAlb = mix(vec3(0.68, 0.74, 0.84), vec3(0.86, 0.89, 0.94), tri.b);
          vec3 mtnAlb = mix(granite, snowAlb, snowMask);
          float mtnRough = mix(0.82 - 0.12 * tri.b, 0.38 + 0.18 * tri.b, snowMask);
          float mtnSSS = snowMask * 0.38;

          // Triplanar rock normal perturbation
          vec3 rockN = normalize(baseN + (vec3(tri.r, 0.5, tri.g) * 2.0 - 1.0) * mix(0.45, 0.14, snowMask));
          albedo = mix(albedo, mtnAlb, rockMask);
          rough = mix(rough, mtnRough, rockMask);
          coat = mix(coat, snowMask * 0.12, rockMask);
          sss = mix(sss, mtnSSS, rockMask);
          N = normalize(mix(N, rockN, rockMask));
        }

        // Combined soft static + dynamic shadow
        float shadow = min(woodSunAt(vWorld + vec3(0.0, 0.14, 0.0)), figSunAt(vWorld));
        vec3 col = pbrShade(vWorld, N, V, albedo, rough, metal, coat, sss, 1.0, shadow, vec3(0.0));

        // High-contrast wet asphalt reflections of streetlights, glowing storefronts & neon signs at night
        if (uNight > 0.02 && flatCity > 0.0) {
          vec3 toEye = cameraPosition - vWorld;
          vec3 reflDir = reflect(-V, N);
          float fres = 0.04 + 0.96 * pow(1.0 - max(dot(N, V), 0.0), 4.0);
          float wetSpec = mix(slabMask * damp * 0.28, mix(0.16, 1.0, puddle), road) * (1.0 - paintMask * 0.72);
          // Reflected fixture plane at y = 4.2m (streetlamps) + y = 11m (neon signs)
          vec2 srcLamp = vWorld.xz + reflDir.xz * (4.2 / max(0.14, reflDir.y));
          vec2 srcSign = vWorld.xz + reflDir.xz * (10.5 / max(0.22, reflDir.y));
          vec3 rLamp = toLinear(streetLightAt(srcLamp + (detMicro.rg - 0.5) * mix(1.6, 0.16, puddle)));
          vec3 rSign = toLinear(streetLightAt(srcSign + (detMicro.rg - 0.5) * mix(2.4, 0.28, puddle)));
          float stretch = mix(0.35, 1.35, fres);
          col += (rLamp * 1.40 + rSign * 0.62) * wetSpec * stretch * uNight;
        }

        outC = vec4(hazed(col, vWorld), 1.0);
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
