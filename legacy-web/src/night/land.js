// The night's ground: one long ridge of snow standing out of a fjord, walked along its crest from the south, with
// two horns of dark rock at its north end. Water lies at height 0 all round. As in the wood, the same heights are
// used by the picture (a map the shaders read), by the wanderer's feet and by everything painted onto it.
import * as THREE from 'three';
import { COMMON, LAND_RES } from '../oilpaint/common.js';
import { noise } from '../world/noise.js';
import { NIGHT } from './paint.js';

export const EXTENT = 512; // the map covers -EXTENT..EXTENT metres both ways, a metre a point
export const MOON = new THREE.Vector3(-0.78, 0.44, 0.3).normalize(); // low in the west, a little behind the way north

const sstep = (a, b, x) => { const t = Math.min(1, Math.max(0, (x - a) / (b - a))); return t * t * (3 - 2 * t); };
const smin = (a, b, k) => { const h = Math.max(0, Math.min(1, 0.5 + 0.5 * (b - a) / k)); return b + (a - b) * h - k * h * (1 - h); };

/** The crest of the ridge: where it is (x) at each z, and how high. He walks it north, toward -z. */
export const crestX = (z) => 30 * Math.sin(z / 140) + 12 * Math.sin(z / 47 + 1);
// he comes up from the south onto a knoll (KNOLL), and from it the crest dips to a saddle and climbs into the horn
const crestH = (z) => 222 + 28 * Math.exp(-(((z - 150) / 80) ** 2));
export const START = { x: crestX(300), z: 300 }; // among the spruces at the south end
export const KNOLL = { x: crestX(150), z: 150 };
/** Where the cabin stands: a level shelf on the crest, just short of where the horn begins to climb. y is its floor. */
export const CABIN = { x: crestX(-64) - 0.5, y: 226.6, z: -64 }; // raised a little, so that its window shows from the knoll

// the horns: the great one where the crest ends, and a lesser one behind its western shoulder. Each is a
// pyramid: straight sides falling from a point, three sharp edges running down it with hollow faces between.
const HORNS = [
  { x: crestX(-175) + 4, z: -175, top: 378, fall: 1.52, sx: 0.94, sz: -0.34, sheer: 3.0, turn: 0.2 },
  { x: crestX(-175) - 96, z: -226, top: 318, fall: 1.5, sx: -0.9, sz: -0.44, sheer: 1.8, turn: 1.9 },
];
export const PEAK = HORNS[0];
function horn(x, z, o) {
  let dx = x - o.x, dz = z - o.z;
  // one side of it is sheer: distance counts for more that way
  const t = dx * o.sx + dz * o.sz;
  if (t > 0) { dx += o.sx * t * (o.sheer - 1); dz += o.sz * t * (o.sheer - 1); }
  const r = Math.hypot(dx, dz);
  const face = 1 - Math.abs(Math.cos(1.5 * (Math.atan2(dz, dx) - o.turn)));
  // its sides steepen toward the top
  const rr = r * (1 + 0.5 * face * sstep(0, 30, r) + 0.26 * noise(x / 52 + o.x, z / 52) + 0.09 * noise(x / 19, z / 19 + o.z));
  return o.top - o.fall * 100 * Math.pow(rr / 100, 0.76);
}

/** Height at (x, z) and, in out[1], how much of it is horn (0..1). */
function rawHeight(x, z, out) {
  const d = x - crestX(z), ad = Math.abs(d);
  const steep = d < 0 ? 0.95 : 0.72; // the west side falls faster
  // the crest is narrow, a few strides of level snow with the fjord falling away on both hands; the knoll is broader
  const w = 15 + 16 * Math.exp(-(((z - 150) / 60) ** 2));
  let h = crestH(z) - steep * (Math.sqrt(d * d + w * w) - w);
  // broad swells on the flanks, gullies running down them, drifts everywhere; the crest itself is left easy to walk
  h += 16 * noise(x / 190 + 2.3, z / 190 - 1.1) * sstep(15, 120, ad);
  h += 5 * noise(x / 55, z / 55 + 7) * sstep(8, 60, ad);
  const gully = 1 - Math.abs(noise(z / 44 + 0.5 * noise(x / 90, z / 90), x / 420 + 3));
  h -= 15 * gully * gully * gully * sstep(30, 150, ad);
  h += 0.9 * noise(x / 17 + 3, z / 17) * sstep(3, 25, ad) + 0.3 * noise(x / 9, z / 9 + 2);
  const horns = -smin(-horn(x, z, HORNS[0]), -horn(x, z, HORNS[1]), 10);
  const hw = sstep(-6, 30, horns - h);
  // crags beside the way: knuckles of rock standing through the snow
  const crag = Math.max(0, noise(x / 27 + 11, z / 27 - 4) - 0.4);
  h += 24 * Math.pow(crag, 1.15) * sstep(8, 26, ad) * (1 - hw);
  h = -smin(-h, -horns, 14);
  // the rock lies in tilted beds: on anything steep they step out as ledges, which is where snow will sit
  const beds = Math.max(hw, 0.4 * sstep(60, 150, ad), sstep(0.05, 0.3, crag) * (1 - hw));
  const tilt = h + 0.42 * x + 0.2 * z + 9 * noise(x / 90, z / 90);
  const broken = sstep(-0.25, 0.3, noise(x / 33 + 5, z / 33 - 2)); // a bed shows in places and is buried in others
  h += beds * (1.6 * broken * Math.sin(tilt * 0.571) + 2.6 * hw * Math.sin(tilt * 0.17 + 1.3));
  // and the whole of it goes down into the water before the map ends
  const edge = EXTENT - Math.pow(x * x * x * x + z * z * z * z, 0.25);
  h = smin(h, 1.25 * (edge - 18), 22);
  const shelf = Math.hypot(x - CABIN.x, z - CABIN.z);
  if (shelf < 27) h += (CABIN.y - h) * sstep(27, 9, shelf);
  out[0] = h; out[1] = hw;
  return h;
}

// --- maps ------------------------------------------------------------------------------------------------------
const N = LAND_RES;
const heights = new Float32Array(N * N);
const land = new Uint8Array(N * N * 4); // r rock, g horn, b how level the ground is, a unused
const at = (i) => -EXTENT + (i / (N - 1)) * 2 * EXTENT;
/** Works the maps out; stops every few rows for `pause(done 0..1)` so the page can breathe. */
export async function prepareLand(pause) {
  const out = [0, 0];
  for (let j = 0; j < N; j++) {
    if (pause && j % 24 === 0) await pause(0.8 * j / N);
    const z = at(j);
    for (let i = 0; i < N; i++) {
      heights[i + j * N] = rawHeight(at(i), z, out);
      land[(i + j * N) * 4 + 1] = Math.round(255 * out[1]);
    }
  }
  // rock is where the ground is too steep for snow to lie; the horns shed it sooner
  for (let j = 0; j < N; j++) {
    if (pause && j % 64 === 0) await pause(0.8 + 0.2 * j / N);
    for (let i = 0; i < N; i++) {
      const i0 = Math.max(0, i - 1), i1 = Math.min(N - 1, i + 1), j0 = Math.max(0, j - 1), j1 = Math.min(N - 1, j + 1);
      const hx = (heights[i1 + j * N] - heights[i0 + j * N]) / (i1 - i0), hz = (heights[i + j1 * N] - heights[i + j0 * N]) / (j1 - j0);
      const ny = 1 / Math.sqrt(1 + hx * hx + hz * hz);
      const k = (i + j * N) * 4, x = at(i), z = at(j);
      const limit = 0.64 + 0.05 * (land[k + 1] / 255);
      land[k] = Math.round(255 * sstep(limit + 0.05, limit - 0.05, ny + 0.07 * noise(x / 9, z / 9) + 0.14 * Math.max(0, noise(x / 41 + 8, z / 41))));
      land[k + 2] = Math.round(255 * ny);
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
/** What the map says at (x, z): [rock, horn], each 0..1. */
export function landAt(x, z) {
  const i = Math.min(N - 1, Math.max(0, Math.round(((x + EXTENT) / (2 * EXTENT)) * (N - 1))));
  const j = Math.min(N - 1, Math.max(0, Math.round(((z + EXTENT) / (2 * EXTENT)) * (N - 1))));
  const k = (i + j * N) * 4;
  return [land[k] / 255, land[k + 1] / 255];
}
export function normalAt(x, z, out = new THREE.Vector3()) {
  const e = 1.2;
  return out.set(heightAt(x - e, z) - heightAt(x + e, z), 2 * e, heightAt(x, z - e) - heightAt(x, z + e)).normalize();
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
  shared.uExtent.value = EXTENT;

  // the ground itself carries the underpainting: the same three mixes as the strokes laid over it, a little
  // deeper, so that what shows between them is paint too. It lies just under the true ground.
  const seg = 512;
  const geometry = new THREE.PlaneGeometry(2 * EXTENT, 2 * EXTENT, seg, seg);
  geometry.rotateX(-Math.PI / 2);
  const pos = geometry.attributes.position;
  for (let i = 0; i < pos.count; i++) pos.setY(i, heightAt(pos.getX(i), pos.getZ(i)) - 0.06);
  const material = new THREE.ShaderMaterial({
    glslVersion: THREE.GLSL3,
    uniforms: shared,
    vertexShader: /* glsl */ `
      ${COMMON}
      out vec3 vWorld;
      void main() { vWorld = position; gl_Position = projectionMatrix * viewMatrix * vec4(position, 1.0); }`,
    fragmentShader: /* glsl */ `
      ${COMMON}
      ${NIGHT}
      in vec3 vWorld;
      layout(location = 0) out vec4 outC;
      void main() {
        if (vWorld.y < -3.0) discard;
        vec4 m = texture(tLand, landUv(vWorld.xz));
        vec3 baseN = normalAt(vWorld.xz);
        vec3 V = normalize(cameraPosition - vWorld);
        float distCam = length(cameraPosition - vWorld);
        float detailFade = 1.0 - smoothstep(25.0, 120.0, distCam);

        // Wind-carved snow sastrugi + micro-sparkle crystals vs. triplanar alpine basalt cliffs
        vec4 nMed = texture(tNoise, vWorld.xz / 14.0);
        vec4 det = texture(tDetail, vWorld.xz * 0.42);
        vec4 fine = texture(tDetail, vWorld.xz * 2.1);

        // Triplanar rock detail so sheer alpine horn walls never stretch
        vec3 triW = pow(abs(baseN), vec3(4.0));
        triW /= max(triW.x + triW.y + triW.z, 1e-4);
        vec4 rX = texture(tDetail, vWorld.zy * 0.11);
        vec4 rY = texture(tDetail, vWorld.xz * 0.11);
        vec4 rZ = texture(tDetail, vWorld.xy * 0.11);
        vec4 rockTri = rX * triW.x + rY * triW.y + rZ * triW.z;

        float rockMask = smoothstep(0.36, 0.64, m.r + (nMed.a - 0.5) * 0.24 - smoothstep(0.62, 0.85, baseN.y) * 0.18);
        // Windswept snow albedo (slightly icy blue on exposed wind crusts)
        float ice = smoothstep(0.66, 0.88, nMed.r);
        vec3 snowAlb = mix(vec3(0.76, 0.81, 0.88), vec3(0.56, 0.72, 0.86), ice) * (0.90 + 0.16 * det.b);
        vec3 rockAlb = mix(vec3(0.036, 0.044, 0.062), vec3(0.095, 0.105, 0.125), rockTri.b) * (0.68 + 0.45 * rockTri.a);
        // Ledges on dark horns catch wind-blown snow streaks
        float ledgeSnow = smoothstep(0.50, 0.76, baseN.y) * smoothstep(0.35, 0.72, rockTri.b) * rockMask * 0.65;
        vec3 albedo = mix(snowAlb, mix(rockAlb, snowAlb * 0.82, ledgeSnow), rockMask);

        vec3 snowN = normalize(baseN + vec3((det.r - 0.5) * 0.28 + (nMed.g - 0.5) * 0.14, 0.0, (det.g - 0.5) * 0.28) * detailFade);
        vec3 rockN = normalize(baseN + (vec3(rockTri.r, 0.5, rockTri.g) * 2.0 - 1.0) * 0.55);
        vec3 N = normalize(mix(snowN, rockN, rockMask));

        float rough = mix(mix(0.46, 0.22, ice), 0.80, rockMask);
        float coat = mix(0.16 + 0.24 * ice, 0.0, rockMask);
        float sss = mix(0.62, 0.0, rockMask);

        float shadow = min(woodSunAt(vWorld + N * 0.45), figSunAt(vWorld));
        vec3 col = pbrShade(vWorld, N, V, albedo, rough, 0.0, coat, sss, 1.0, shadow, vec3(0.0));

        // Ethereal green-teal aurora bounce light washing across the open snow slopes
        float aur = auroraOn(vWorld.xz, N) * uNight;
        col += albedo * vec3(0.04, 0.26, 0.20) * aur * (1.0 - rockMask * 0.65) * 0.65;

        // Warm firelight spilling from the cabin windows & open doorway onto the snow outside
        float wLight = warmAt(vWorld, N);
        if (wLight > 0.001) {
          col += albedo * vec3(2.8, 1.35, 0.42) * wLight;
        }

        // Photographic micro-crystal snow glints catching moonlight & lantern firelight
        if (rockMask < 0.75 && distCam < 38.0) {
          float glintHash = hash4(floor(vWorld.xz * 14.0), 7.1).x;
          float spark = step(0.968, glintHash) * smoothstep(0.65, 0.96, fine.a) * (1.0 - rockMask);
          float viewAng = pow(max(dot(reflect(-V, snowN), uSunDir), 0.0), 12.0) + lampAt(vWorld, snowN) * 0.85;
          col += mix(vec3(0.65, 0.85, 1.0), vec3(1.4, 0.95, 0.55), clamp(lampAt(vWorld, snowN), 0.0, 1.0)) * spark * viewAng * 2.4 * (1.0 - smoothstep(14.0, 38.0, distCam));
        }

        outC = vec4(hazed(col, vWorld), 1.0);
      }`,
  });
  const mesh = new THREE.Mesh(geometry, material);
  mesh.frustumCulled = false;
  // the same ground again, for the moon to see: it is what casts the horns' shadow down the ridge
  const caster = new THREE.Mesh(geometry, new THREE.MeshBasicMaterial({ side: THREE.DoubleSide }));
  caster.frustumCulled = false;
  const shade = new THREE.Scene();
  shade.add(caster);

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
  return { mesh, shade, heightField };
}
