// The night sky: a deep wash with broad strokes of itself laid over, stars as single touches, the moon, and the
// aurora.
//
// A curtain of the aurora hangs from a line that wanders across the sky far off. Every stroke of it is pinned to a
// place on that line and a height above it and runs straight up, as the rays do; seen from below they lean
// together toward the top of the sky. The line is never still: great folds travel along it, smaller waves run
// through them the other way, and the strokes themselves are carried along the curtain like paint on a slow river,
// while the light races through them faster than they move.
//
// It is painted twice over. First its glow, in strokes of light that add up, so that a curtain seen edge on, where
// many stand behind each other, burns brightest. Then, over the glow, thick strokes of paint in its colours, the
// way a brush would put it down.
import * as THREE from 'three';
import { COMMON } from '../oilpaint/common.js';
import { dabMaterial, dabGeometry, dabMesh } from '../oilpaint/dabs.js';
import { rng } from '../world/noise.js';
import { NIGHT } from './paint.js';

const SHELL = 11000; // the aurora is drawn this far off

const CURTAIN = /* glsl */ `
// Where the foot of curtain k is at u (-1 west .. 1 east), in metres from the eye.
vec3 foot(float u, float k) {
  float t = uTime;
  if (k < 0.5) {
    float ph = u * 8.0 - t * 0.05; // the great folds travel east
    return vec3(11000.0 * u + 1500.0 * sin(ph),
      2300.0 + 260.0 * sin(u * 5.0 + t * 0.05) + 90.0 * sin(u * 31.0 - t * 0.7),
      -9200.0 + 2300.0 * cos(ph * 0.83 + 0.5) + 800.0 * sin(u * 19.0 + t * 0.33) + 260.0 * sin(u * 47.0 - t * 0.65));
  }
  if (k < 1.5) return vec3(16000.0 * u + 800.0 * sin(u * 6.0 + t * 0.04), 2700.0 + 120.0 * sin(u * 23.0 + t * 0.5),
    -17500.0 + 1600.0 * sin(u * 5.0 + 1.3 + t * 0.03) + 500.0 * sin(u * 29.0 - t * 0.4));
  // The third is seen along its length: a river of it that comes up out of the north behind the horn, climbs,
  // and passes overhead. u is the angle it is seen at from straight above, so that its strokes are as thick on
  // the canvas far off as near.
  float z = 2600.0 * tan(mix(-1.46, 1.15, u * 0.5 + 0.5));
  return vec3(1900.0 + 1500.0 * sin(z / 2600.0 + t * 0.05) + 420.0 * sin(z / 700.0 - t * 0.33), 2600.0 + 120.0 * sin(z / 500.0 + t * 0.6), z);
}
// One stroke of a curtain. Every stroke is planted: it starts at a height on the curtain (s0, a share of the
// curtain's height above its foot) and runs from there, so the loaded end of the brush is where it starts and the
// dry, broken end is where it leaves off. The curtain's lower edge is nothing but the feet of its rays.
struct Hang { vec3 P; float u; float s; float I; float len; float edge; float surge; };
Hang hang(vec4 aC, float s0) {
  Hang h;
  float k = aC.w;
  // carried along the curtain: east on the near ones, west on the far one
  float u = fract(aC.x + uTime * (k > 0.5 && k < 1.5 ? -0.002 : 0.0032)) * 2.0 - 1.0;
  h.u = u;
  // where along the curtain the light is: broad reaches of it, the rays within them going with the strokes, and
  // a finer shimmer running against them
  float reach = smoothstep(0.3, 0.7, texture(tNoise, vec2(u * 0.33 + uTime * 0.004 + k * 0.31, 0.21 + k * 0.17)).r);
  // (the river is seen from close under it, where its rays must be finer or they are lumps; and it never quite
  // goes out, or overhead it would be a string of beads)
  float near = step(1.5, k);
  reach = max(reach, 0.5 * near);
  float ray = mix(1.0, 0.45, near) * texture(tNoise, vec2(u * mix(5.0, 17.0, near) - uTime * 0.028, 0.63 + k * 0.29)).g + 0.33 * near;
  float fine = texture(tNoise, vec2(u * mix(21.0, 55.0, near) + uTime * 0.07, 0.11 + k * 0.4)).b;
  // and now and then a surge: one wave of brightness that travels the whole length of the curtain, through its
  // dark reaches too, with the rays leaping up as it passes
  h.surge = exp(-pow((fract(u * 0.5 + 0.5 - uTime * 0.035 + k * 0.37) - 0.5) / 0.07, 2.0));
  h.I = max(reach, 0.55 * h.surge) * (0.3 + 0.7 * smoothstep(0.3, 0.75, ray)) * (0.55 + 0.9 * fine) * (1.0 + 1.5 * h.surge);
  h.len = (k > 0.5 && k < 1.5 ? 2000.0 : 2600.0) * (0.5 + 0.9 * texture(tNoise, vec2(u * 1.1 - uTime * 0.003, 0.77)).a) * (1.0 + 0.45 * h.surge);
  h.s = s0;
  h.P = foot(u, k) + vec3(0.0, s0 * h.len, 0.0);
  h.edge = smoothstep(1.0, 0.8, abs(u)) * uGlow.x * smoothstep(0.06, 0.52, uNight);
  return h;
}
// The rays are not of a height: here and there one stands far above its neighbours (0..1, squared when used).
float spire(Hang h, float k) { return texture(tNoise, vec2(h.u * (k > 1.5 ? 83.0 : 37.0) - uTime * 0.02, 0.37 + k * 0.2)).a; }
// Counter-streaming ripples high in the upper violet corona (0..1).
float coronaWave(Hang h, float s) {
  float w1 = sin(s * 10.5 - uTime * 2.35 + h.u * 63.0);
  float w2 = sin(s * 5.2 + uTime * 1.45 - h.u * 39.0);
  return 0.5 + 0.32 * w1 + 0.18 * w2;
}
// How strongly the curtain burns at height s: richest emerald at its foot, then a shimmering violet corona above
// its brightest reaches. The light climbs the rays in pulses.
float burn(Hang h, float s) {
  float shimmer = coronaWave(h, s);
  float crown = (0.36 + 0.18 * h.surge) * smoothstep(0.36, 0.74, s) * smoothstep(0.32, 0.82, h.I) * (0.72 + 0.56 * shimmer);
  float pulse = 0.8 + 0.28 * sin(s * 7.0 - uTime * 1.7 + h.u * 55.0);
  return h.I * (exp(-s * 2.25) * pulse + crown) * h.edge;
}
// Deep emerald-jade at the foot, electric cyan-teal through the waist, then royal amethyst-violet and plum-magenta
// shimmering in the upper corona.
vec3 curtainColour(float s, float I, float shimmer) {
  vec3 emerald = vec3(0.03, 0.74, 0.52);
  vec3 teal = vec3(0.03, 0.44, 0.66);
  vec3 violet = mix(vec3(0.38, 0.14, 0.82), vec3(0.54, 0.22, 0.94), shimmer);
  vec3 corona = mix(vec3(0.60, 0.13, 0.56), vec3(0.76, 0.20, 0.68), shimmer);
  vec3 c = mix(emerald, teal, smoothstep(0.15, 0.46, s));
  c = mix(c, violet, smoothstep(0.40, 0.76, s));
  c = mix(c, corona, smoothstep(0.76, 1.12, s));
  c = mix(c, vec3(0.32, 0.95, 0.72), (1.0 - smoothstep(0.0, 0.17, s)) * smoothstep(0.28, 0.84, I));
  return mix(c, c * vec3(1.08, 0.9, 1.18), 0.35 * uTwilight);
}
// Pins a stroke of the curtain to the sky by its foot: its place, the way it runs on the canvas (with the
// leaning-together that distance gives things overhead) and its size as seen from here.
void pin(inout Dab d, Hang h, vec3 run, vec2 size) {
  float dist = length(h.P);
  vec3 v = h.P / dist;
  vec3 vp = mat3(viewMatrix) * v, vd = mat3(viewMatrix) * run;
  vec2 on = vd.xy * (-vp.z) + vp.xy * vd.z;
  d.dir = dot(on, on) > 1e-8 ? transpose(mat3(viewMatrix)) * vec3(on, 0.0) : run;
  size.y *= max(length(run - v * dot(run, v)), 0.15); // a ray pointed at the eye is seen end on
  d.pos = cameraPosition + v * ${SHELL}.0;
  d.size = size * ${SHELL}.0 / dist;
  d.lift = 0.0;
  d.base = 1.0;
}
`;

export function createSky(shared) {
  const group = new THREE.Group();
  const dome = new THREE.Mesh(new THREE.SphereGeometry(900, 24, 16), new THREE.ShaderMaterial({
    glslVersion: THREE.GLSL3,
    uniforms: shared,
    side: THREE.BackSide,
    depthTest: false,
    depthWrite: false,
    vertexShader: /* glsl */ `
      ${COMMON}
      out vec3 vDir;
      void main() { vDir = position; gl_Position = projectionMatrix * viewMatrix * vec4(position + cameraPosition, 1.0); }`,
    fragmentShader: /* glsl */ `
      ${COMMON}
      ${NIGHT}
      in vec3 vDir;
      layout(location = 0) out vec4 outC;
      void main() { outC = vec4(nightSky(normalize(vDir)) * 0.96, 1.0); }`,
  }));
  dome.frustumCulled = false;
  dome.renderOrder = -10;
  group.add(dome);

  const rand = rng(909);
  // --- the wash, in strokes ---
  {
    const count = 2600;
    const dirs = new Float32Array(count * 4);
    for (let i = 0; i < count; i++) {
      const a = rand() * Math.PI * 2, y = 0.02 + Math.pow(rand(), 1.4) * 0.98, r = Math.sqrt(Math.max(0, 1 - y * y));
      dirs.set([Math.cos(a) * r, y, Math.sin(a) * r, rand() * 1000], i * 4);
    }
    group.add(dabMesh(dabGeometry(count, { aDir: [dirs, 4] }), dabMaterial(shared, {
      head: NIGHT + /* glsl */ `in vec4 aDir;`,
      body: /* glsl */ `
        void makeDab(inout Dab d) {
          vec4 r = hash4(aDir.w);
          vec3 v = normalize(aDir.xyz);
          d.pos = cameraPosition + v * 800.0;
          // strokes lie level, tipping a little
          vec3 level = normalize(cross(vec3(0.0, 1.0, 0.0), v));
          d.dir = normalize(level + vec3(0.0, (r.x - 0.5) * 0.7, 0.0));
          d.size = vec2(46.0 + 40.0 * r.y, 150.0 + 130.0 * r.z);
          vec3 c = nightSky(v);
          c += vec3(0.009, 0.003, 0.022) * smoothstep(0.38, 0.88, v.y) * r.x * uGlow.x * uNight;
          d.c1 = jitter(c, r.yzw, 0.13);
          d.c2 = jitter(c * 1.06 + vec3(0.0, 0.004, 0.006), r.wxy, 0.18);
          d.seed = aDir.w;
          d.lift = 0.0;
        }`,
      uniforms: { uImpasto: { value: 0.55 } },
      minPx: [10, 20], maxPx: [900, 1800], cells: [0, 8], depthTest: false, depthWrite: false,
    }), -9));
  }

  // --- stars, and the moon among them ---
  {
    const count = 3000;
    const stars = new Float32Array(count * 4);
    for (let i = 0; i < count; i++) {
      const a = rand() * Math.PI * 2, y = 0.03 + rand() * 0.97, r = Math.sqrt(Math.max(0, 1 - y * y));
      stars.set([Math.cos(a) * r, y, Math.sin(a) * r, rand() * 1000], i * 4);
    }
    group.add(dabMesh(dabGeometry(count, { aDir: [stars, 4] }), dabMaterial(shared, {
      head: NIGHT + /* glsl */ `in vec4 aDir;`,
      body: /* glsl */ `
        void makeDab(inout Dab d) {
          vec4 r = hash4(aDir.w);
          bool moon = gl_InstanceID == 0;
          float big = r.x * r.x * r.x;
          float beacon = smoothstep(0.9, 0.995, r.x);
          float emerge = mix(0.44, 0.18, beacon);
          if (!moon && uNight < emerge) { d.alive = 0.0; return; }
          vec3 v = moon ? uSunDir : normalize(aDir.xyz);
          d.pos = cameraPosition + v * 800.0;
          d.dir = vec3(0.0, 1.0, 0.0);
          d.spin = r.z * TAU;
          d.size = moon ? vec2(30.0) : vec2(800.0 * (0.0013 + 0.0038 * big + 0.0018 * beacon));
          // Crisp scintillation: livelier low down, steadier on bright beacons, hushed inside the moon's glare.
          float waver = 0.8 + 0.2 * sin(uTime * (1.6 + 4.4 * r.y) + r.w * 60.0) * (1.0 - 0.35 * beacon);
          float moonGlare = 1.0 - 0.6 * pow(max(dot(v, uSunDir), 0.0), 28.0);
          vec3 tint = mix(vec3(0.76, 0.88, 1.0), vec3(0.98, 0.99, 1.0), smoothstep(0.35, 0.85, r.x));
          tint = mix(tint, vec3(1.0, 0.87, 0.66), step(0.82, r.w));
          tint = mix(tint, vec3(0.68, 0.98, 0.92), step(r.w, 0.1));
          float lum = (0.46 + 0.82 * big + 0.42 * beacon) * waver * moonGlare * smoothstep(0.02, 0.22, v.y) * smoothstep(emerge, min(0.92, emerge + 0.44), uNight);
          vec3 c = tint * lum;
          d.c1 = moon ? mix(vec3(1.0, 0.97, 0.86), vec3(0.93, 0.96, 0.90), uNight) : max(c, nightSky(v));
          d.c2 = d.c1;
          d.seed = aDir.w;
          d.lift = 0.0;
        }`,
      uniforms: { uImpasto: { value: 0.2 } },
      minPx: [1.7, 1.7], maxPx: [60, 60], cells: [4, 2], depthTest: false, depthWrite: false,
    }), -8));
  }

  // --- the aurora ---
  // Strokes are scattered over three curtains: [how many on each]. Each has a place along its curtain, a height
  // on it (more of them low, where it burns), a number of its own, and its curtain. Additive glow strokes are
  // proportionally interleaved across curtains so ?quality=low can trim instanceCount while keeping all three whole.
  const scatter = (counts, interleave = false) => {
    const groups = counts.map((n, k) => {
      const list = [];
      for (let i = 0; i < n; i++) { const s = rand(); list.push([rand(), s * s, rand() * 1000, k]); }
      return list;
    });
    const data = [];
    if (!interleave) {
      for (const list of groups) for (const item of list) data.push(...item);
    } else {
      const total = groups.reduce((acc, g) => acc + g.length, 0);
      const idx = groups.map(() => 0);
      for (let step = 0; step < total; step++) {
        let best = -1, bestBehind = -1;
        for (let k = 0; k < groups.length; k++) {
          if (idx[k] >= groups[k].length) continue;
          const behind = (step + 1) * (groups[k].length / total) - idx[k];
          if (behind > bestBehind) { bestBehind = behind; best = k; }
        }
        data.push(...groups[best][idx[best]++]);
      }
    }
    return new Float32Array(data);
  };
  // First the paint: thick strokes in the curtain's deep colours, from the emerald foot up into the violet corona,
  // growing as the light reaches them and sinking back into the night wash when dawn comes.
  const paintData = scatter([5200, 0, 4200]); // the far curtain is light alone
  group.add(dabMesh(dabGeometry(paintData.length / 4, { aC: [paintData, 4] }), dabMaterial(shared, {
    head: NIGHT + CURTAIN + /* glsl */ `in vec4 aC;`,
    body: /* glsl */ `
      void makeDab(inout Dab d) {
        if (uNight < 0.06 || uGlow.x < 0.03) { d.alive = 0.0; return; }
        vec4 r = hash4(aC.z + 7.7);
        bool high = r.x > 0.8;
        float s0 = r.x < 0.52 ? (r.w - 0.35) * 0.05 : high ? 0.38 + aC.y * 0.5 : aC.y * 0.44;
        Hang h = hang(aC, s0);
        float tall = (high ? 0.22 + 0.26 * r.z : 0.16 + 0.2 * r.z), mid = h.s + tall * 0.5;
        float shimmer = coronaWave(h, mid);
        float tone = clamp(burn(h, mid) * (high ? 2.5 : 2.0), 0.0, 1.0);
        d.alive = smoothstep(0.18, 0.54, tone + (r.y - 0.5) * 0.2);
        if (d.alive <= 0.0) return;
        vec3 c = curtainColour(mid, h.I, shimmer);
        pin(d, h, normalize(vec3((r.w - 0.5) * 0.16, 1.0, 0.0)), vec2((150.0 + 130.0 * r.y) * (high ? 1.15 : 1.0), h.len * tall));
        vec3 sky = nightSky(normalize(h.P));
        float body = smoothstep(0.14, 0.74, tone);
        d.c1 = mix(sky, jitter(c, r.yzx, 0.32) * (0.14 + 0.36 * tone), body);
        d.c2 = mix(sky, mix(c, vec3(0.58, 1.0, 0.82), 0.28 * (1.0 - smoothstep(0.35, 0.75, mid))) * (0.18 + 0.42 * tone), body);
        d.seed = aC.z;
      }`,
    uniforms: { uImpasto: { value: 0.9 } },
    minPx: [4, 10], maxPx: [500, 1600], cells: [0, 3], depthTest: false, depthWrite: false,
  }), -7.9));

  // Behind the curtains, the veil: great dim patches lying level in the sky, emerald low down and amethyst-violet
  // overhead, pulsing slowly (skipped on ?quality=low).
  let veilMesh = null;
  {
    const count = 420, dirs = new Float32Array(count * 4);
    for (let i = 0; i < count; i++) {
      const a = (rand() - 0.5) * Math.PI * 1.7, y = 0.12 + 0.8 * rand(), rr = Math.sqrt(1 - y * y); // north, east and west, and overhead
      dirs.set([Math.sin(a) * rr, y, -Math.cos(a) * rr, rand() * 1000], i * 4);
    }
    const veil = dabMesh(dabGeometry(count, { aDir: [dirs, 4] }), dabMaterial(shared, {
      head: NIGHT + /* glsl */ `in vec4 aDir;`,
      body: /* glsl */ `
        void makeDab(inout Dab d) {
          if (uNight < 0.08 || uGlow.x < 0.03) { d.alive = 0.0; return; }
          vec4 r = hash4(aDir.w);
          vec3 v = normalize(aDir.xyz);
          float az = atan(v.x, -v.z);
          float patch_ = texture(tNoise, vec2(az * 0.3 + uTime * 0.004, v.y * 0.8 - uTime * 0.003)).r;
          float beat = 0.68 + 0.32 * sin(uTime * (0.7 + 0.65 * r.x) + r.y * TAU);
          float power = smoothstep(0.4, 0.75, patch_) * beat * uGlow.x * smoothstep(0.08, 0.55, uNight);
          d.alive = smoothstep(0.0, 0.28, power);
          if (d.alive <= 0.0) return;
          d.pos = cameraPosition + v * 800.0;
          d.dir = normalize(cross(vec3(0.0, 1.0, 0.0), v) + vec3(0.0, (r.z - 0.5) * 0.5, 0.0));
          d.size = vec2(110.0 + 90.0 * r.y, 300.0 + 240.0 * r.w);
          vec3 lowCol = mix(vec3(0.03, 0.54, 0.34), vec3(0.04, 0.4, 0.52), r.z);
          vec3 highCorona = mix(vec3(0.34, 0.13, 0.72), vec3(0.52, 0.16, 0.64), r.x);
          vec3 col = mix(lowCol, highCorona, smoothstep(0.42, 0.86, v.y));
          d.c1 = jitter(col, r.yzx, 0.28) * power * 0.09;
          d.c2 = d.c1 * 1.3;
          d.seed = aDir.w;
          d.lift = 0.0;
        }`,
      uniforms: { uImpasto: { value: 0.4 } },
      minPx: [10, 20], maxPx: [900, 1800], cells: [0, 8], depthTest: false, depthWrite: false,
    }), -7.85);
    veil.material.blending = THREE.AdditiveBlending;
    veil.material.alphaToCoverage = false;
    veil.name = 'veil';
    veilMesh = veil;
    group.add(veil);
  }

  // Then the light, in strokes that add up. Five kinds of stroke make it:
  //   rays       planted on the lower edge or a little above it, running up into violet spires
  //   glow       broad and dim, what the rays stand in
  //   the edge   short, wide, emerald-mint touches side by side along the foot of the brightest reaches
  //   the fringe short rose touches hanging below that edge, where it burns hardest
  //   corona     shimmering royal-violet and orchid filaments dancing in the upper crown
  const glowData = scatter([14000, 4000, 11000], true);
  const totalGlow = glowData.length / 4;
  const glow = dabMesh(dabGeometry(totalGlow, { aC: [glowData, 4] }), dabMaterial(shared, {
    head: NIGHT + CURTAIN + /* glsl */ `in vec4 aC; uniform float uGain;`,
    body: /* glsl */ `
      void makeDab(inout Dab d) {
        if (uNight < 0.06 || uGlow.x < 0.03) { d.alive = 0.0; return; }
        vec4 r = hash4(aC.z);
        float kind = r.w < 0.46 ? 0.0 : r.w < 0.69 ? 1.0 : r.w < 0.83 ? 2.0 : r.w < 0.90 ? 3.0 : 4.0;
        vec4 q = hash4(aC.z * 1.7 + 3.0);
        // (the feet of the rays are not all on one line: the edge is ragged, as a brush leaves it)
        float s0 = kind < 0.5 ? (r.x < 0.5 ? (q.y - 0.35) * 0.05 : aC.y * 0.78)
          : kind < 1.5 ? aC.y * 0.62
          : kind < 3.5 ? (q.y - 0.5) * 0.03
          : 0.52 + 0.42 * q.y;
        Hang h = hang(aC, s0);
        vec3 run = vec3(0.0, 1.0, 0.0), c;
        vec2 size;
        float power;
        if (kind < 0.5) {
          float sp = spire(h, aC.w);
          float tall = (0.2 + 0.3 * r.z) * (0.65 + 1.75 * sp * sp), mid = h.s + tall * 0.46;
          // no two rays are loaded alike, and one in eight is a broad bold one: they are what reads as brushwork
          bool bold = q.x > 0.875;
          float shimmer = coronaWave(h, mid);
          power = burn(h, mid) * (0.5 + 1.0 * q.z * q.z) * (bold ? 1.25 : 1.0);
          c = curtainColour(mid, h.I, shimmer);
          size = vec2((95.0 + 110.0 * r.y) * (bold ? 2.3 : 1.0), h.len * tall);
        } else if (kind < 1.5) {
          float tall = 0.22 + 0.24 * r.z, mid = h.s + tall * 0.5;
          float shimmer = coronaWave(h, mid);
          power = burn(h, mid) * 0.44;
          c = curtainColour(mid, h.I, shimmer);
          size = vec2(520.0 + 160.0 * r.y, h.len * tall);
        } else if (kind < 2.5) {
          power = smoothstep(0.26, 0.78, h.I) * 0.88 * h.edge;
          c = mix(vec3(0.24, 0.92, 0.68), vec3(0.68, 0.97, 0.88), 0.32 + 0.42 * r.x);
          size = vec2(130.0 + 130.0 * r.y, h.len * (0.05 + 0.07 * r.z));
        } else if (kind < 3.5) {
          power = smoothstep(0.44, 0.9, h.I) * (0.45 + 0.52 * h.surge) * h.edge;
          c = mix(vec3(1.0, 0.24, 0.52), vec3(0.84, 0.18, 0.78), r.x);
          run = vec3(0.0, -1.0, 0.0);
          size = vec2(80.0 + 80.0 * r.y, h.len * (0.07 + 0.09 * r.z));
        } else {
          float tall = 0.18 + 0.26 * r.z, mid = h.s + tall * 0.5;
          float shimmer = coronaWave(h, mid);
          float flicker = 0.58 + 0.42 * sin(uTime * (2.1 + 1.5 * q.x) + h.u * 59.0 + q.w * TAU);
          power = smoothstep(0.3, 0.82, h.I) * (0.34 + 0.32 * shimmer + 0.28 * h.surge) * flicker * h.edge;
          c = mix(vec3(0.42, 0.15, 0.88), vec3(0.72, 0.18, 0.74), 0.5 * q.z + 0.5 * shimmer);
          size = vec2(140.0 + 150.0 * r.y, h.len * tall);
        }
        power = min(power, 0.78); // where strokes stand behind each other it adds up by itself
        d.alive = smoothstep(0.03, 0.14, power);
        if (d.alive <= 0.0) return;
        pin(d, h, run, size);
        d.c1 = jitter(c, r.yzx, 0.28) * power * uGain;
        d.c2 = d.c1 * 1.4;
        d.seed = aC.z;
      }`,
    uniforms: { uGain: { value: 0.052 }, uImpasto: { value: 0.3 } },
    minPx: [3, 8], maxPx: [500, 1600], cells: [0, 3], depthTest: false, depthWrite: false,
  }), -7.8);
  glow.material.blending = THREE.AdditiveBlending; // drawn in its turn with the sky, not sorted with glass
  glow.material.alphaToCoverage = false;
  group.add(glow);

  function setQuality(level) {
    if (veilMesh) veilMesh.visible = level !== 'low';
    if (level === 'low') {
      glow.geometry.instanceCount = Math.round(totalGlow * 0.45);
      glow.material.uniforms.uGain.value = 0.052 * (totalGlow / glow.geometry.instanceCount) * 0.94;
    } else if (level === 'medium') {
      glow.geometry.instanceCount = Math.round(totalGlow * 0.75);
      glow.material.uniforms.uGain.value = 0.052 * (totalGlow / glow.geometry.instanceCount) * 0.97;
    } else {
      glow.geometry.instanceCount = totalGlow;
      glow.material.uniforms.uGain.value = 0.052;
    }
  }
  return { group, setQuality };
}
