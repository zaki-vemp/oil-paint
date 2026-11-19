// Shared rendering uniforms and unified AAA physically-based (PBR) GLSL shading library.
// Supplies sun/moon directional lighting with soft PCF shadows, analytic HDR sky/city/aurora IBL,
// GGX Cook-Torrance specular + clearcoat + subsurface scattering, procedural micro-normal mapping,
// dynamic street/lantern lighting, and linear HDR aerial perspective for both scenes.
import * as THREE from 'three';
import { createDetailMap, createSurfaceMap, createFoliageCard } from './textures.js';

export const SUN = new THREE.Vector3(-0.44, 0.5, -0.75).normalize(); // low golden-hour sun ahead-left of the way
export const WIND = new THREE.Vector2(0.8, 0.6).normalize(); // blows east-southeast
export const EXTENT = 360; // land map covers -EXTENT..EXTENT metres both ways
export const LAND_RES = 1025;

const blank = (r, g, b, a) => {
  const t = new THREE.DataTexture(new Uint8Array([r, g, b, a]), 1, 1);
  t.needsUpdate = true;
  return t;
};

/** A tiling noise sheet: four unrelated smooth noises in the four channels, with mipmaps. */
function noiseSheet(size = 256, seed = 7) {
  let s = seed >>> 0;
  const rnd = () => ((s = (Math.imul(s, 1664525) + 1013904223) >>> 0) / 4294967296);
  const data = new Uint8Array(size * size * 4);
  for (let ch = 0; ch < 4; ch++) {
    const field = new Float32Array(size * size);
    for (const [cells, amp] of [[8, 1], [16, 0.5], [32, 0.25], [64, 0.125]]) {
      const lat = new Float32Array(cells * cells);
      for (let i = 0; i < lat.length; i++) lat[i] = rnd();
      for (let y = 0; y < size; y++) {
        for (let x = 0; x < size; x++) {
          const fx = (x / size) * cells, fy = (y / size) * cells;
          const x0 = Math.floor(fx), y0 = Math.floor(fy);
          let tx = fx - x0, ty = fy - y0;
          tx = tx * tx * (3 - 2 * tx); ty = ty * ty * (3 - 2 * ty);
          const a = lat[(x0 % cells) + (y0 % cells) * cells], b = lat[((x0 + 1) % cells) + (y0 % cells) * cells];
          const c = lat[(x0 % cells) + ((y0 + 1) % cells) * cells], d = lat[((x0 + 1) % cells) + ((y0 + 1) % cells) * cells];
          field[x + y * size] += amp * (a + (b - a) * tx + (c - a) * ty + (a - b - c + d) * tx * ty);
        }
      }
    }
    let lo = Infinity, hi = -Infinity;
    for (const v of field) { lo = Math.min(lo, v); hi = Math.max(hi, v); }
    for (let i = 0; i < field.length; i++) data[i * 4 + ch] = Math.round(((field[i] - lo) / (hi - lo)) * 255);
  }
  const t = new THREE.DataTexture(data, size, size);
  t.wrapS = t.wrapT = THREE.RepeatWrapping;
  t.minFilter = THREE.LinearMipmapLinearFilter;
  t.magFilter = THREE.LinearFilter;
  t.generateMipmaps = true;
  t.needsUpdate = true;
  return t;
}

export function createShared() {
  return {
    uTime: { value: 0 },
    uPixel: { value: 0.001 }, // metres one pixel covers at one metre from the eye
    uSunDir: { value: SUN.clone() },
    uWind: { value: new THREE.Vector3(WIND.x, WIND.y, 1) }, // direction on ground (x, z) and strength
    uExtent: { value: EXTENT },
    tHeight: { value: blank(0, 0, 0, 0) },
    tLand: { value: blank(0, 0, 128, 0) }, // r path, g clearing, b moss, a nature
    tStreetLights: { value: blank(0, 0, 0, 255) }, // baked light from city fixtures, mapped to world x/z
    tNoise: { value: noiseSheet() },
    tDetail: { value: createDetailMap(256) },
    tSurface: { value: createSurfaceMap(256) },
    tLeafCard: { value: createFoliageCard('broadleaf', 256) },
    tPineCard: { value: createFoliageCard('pine', 256) },
    tBrush: { value: blank(255, 255, 128, 255) },
    tTreeShade: { value: null },
    uTreeShadeMat: { value: new THREE.Matrix4() },
    tFigShade: { value: null },
    uFigShadeMat: { value: new THREE.Matrix4() },
    uHaze: { value: new THREE.Color(0.6, 0.6, 0.67) },
    uHazeSun: { value: new THREE.Color(1.0, 0.89, 0.62) },
    uHazeDensity: { value: 0.0065 },
    uHazeGlow: { value: new THREE.Color(0.1, 0.08, 0.02) },
    uShadeBias: { value: 0.0009 },
    uBounce: { value: new THREE.Color(0.1, 0.045, 0.0) },
    uLamp: { value: new THREE.Vector4(0, 0, 0, 0) }, // carried lantern / dynamic point light: xyz, strength
    uLampCol: { value: new THREE.Color(1.0, 0.62, 0.25) },
    uGlazeMul: { value: new THREE.Color(0.5, 0.38, 0.46) },
    uGlazeAdd: { value: new THREE.Color(0.03, 0.02, 0.06) },
    uImpasto: { value: 0 },
    uAnime: { value: 1 },
    uAurora: { value: 0 }, // 1 in Snowbound prologue, 0 in Neo-Kyoto city
    uExposure: { value: 1.08 },
    uRim: { value: new THREE.Color(0, 0, 0) },
    uGrass: { value: new THREE.Color(0.39, 0.65, 0.30) },
    uFoliage: { value: new THREE.Color(0.25, 0.57, 0.32) },
    uLeafLight: { value: new THREE.Color(0.64, 0.79, 0.34) },
    uPath: { value: new THREE.Color(0.88, 0.78, 0.53) },
    uZoneSplit: { value: new THREE.Vector3(90, -10, -30) },
    uGrid: { value: new THREE.Vector3(-200, 40, 3.6) },
    uZoneGrass: { value: [0, 1, 2, 3].map(() => new THREE.Vector3(0.1, 0.12, 0.16)) },
    uZoneFoliage: { value: [0, 1, 2, 3].map(() => new THREE.Vector3(0.2, 0.8, 0.9)) },
    uZoneLeaf: { value: [0, 1, 2, 3].map(() => new THREE.Vector3(1, 0.86, 0.5)) },
    uZonePath: { value: [0, 1, 2, 3].map(() => new THREE.Vector3(0.2, 0.22, 0.26)) },
    uSkyHigh: { value: new THREE.Color(0.31, 0.67, 0.82) },
    uFocus: { value: new THREE.Vector3() },
    uNight: { value: 0 },
    uTwilight: { value: 0 },
  };
}

export const COMMON = /* glsl */ `
precision highp float;
precision highp sampler2DShadow;
#define TAU 6.28318530718
#define PI 3.14159265359
uniform float uTime;
uniform float uPixel;
uniform vec3 uSunDir;
uniform vec3 uWind;
uniform float uExtent;
uniform sampler2D tHeight;
uniform sampler2D tLand;
uniform sampler2D tStreetLights;
uniform sampler2D tNoise;
uniform sampler2D tDetail;
uniform sampler2D tSurface;
uniform sampler2D tLeafCard;
uniform sampler2D tPineCard;
uniform sampler2D tBrush;
uniform sampler2DShadow tTreeShade;
uniform mat4 uTreeShadeMat;
uniform sampler2DShadow tFigShade;
uniform mat4 uFigShadeMat;
uniform vec3 uHaze;
uniform vec3 uHazeSun;
uniform float uHazeDensity;
uniform vec3 uHazeGlow;
uniform float uShadeBias;
uniform vec3 uBounce;
uniform vec4 uLamp;
uniform vec3 uLampCol;
uniform vec3 uGlazeMul;
uniform vec3 uGlazeAdd;
uniform float uImpasto;
uniform float uAnime;
uniform float uAurora;
uniform float uExposure;
uniform vec3 uRim;
uniform vec3 uGrass;
uniform vec3 uFoliage;
uniform vec3 uLeafLight;
uniform vec3 uPath;
uniform vec3 uZoneSplit;
uniform vec3 uGrid;
uniform vec3 uZoneGrass[4];
uniform vec3 uZoneFoliage[4];
uniform vec3 uZoneLeaf[4];
uniform vec3 uZonePath[4];
uniform vec3 uSkyHigh;
uniform vec3 uFocus;
uniform float uNight;
uniform float uTwilight;

vec3 toLinear(vec3 c) { vec3 m = max(c, vec3(0.0)); return m * m; }

vec3 zonePick(vec2 xz, vec3 a, vec3 b, vec3 c, vec3 d) {
  vec3 south = mix(d, c, smoothstep(-6.0, 6.0, xz.x - uZoneSplit.z));
  vec3 mid = mix(south, b, smoothstep(-6.0, 6.0, xz.y - uZoneSplit.y));
  return mix(mid, a, smoothstep(-6.0, 6.0, xz.y - uZoneSplit.x));
}
vec3 zoneGrassAt(vec2 xz) { return zonePick(xz, uZoneGrass[0], uZoneGrass[1], uZoneGrass[2], uZoneGrass[3]); }
vec3 zoneFoliageAt(vec2 xz) { return zonePick(xz, uZoneFoliage[0], uZoneFoliage[1], uZoneFoliage[2], uZoneFoliage[3]); }
vec3 zoneLeafAt(vec2 xz) { return zonePick(xz, uZoneLeaf[0], uZoneLeaf[1], uZoneLeaf[2], uZoneLeaf[3]); }
vec3 zonePathAt(vec2 xz) { return zonePick(xz, uZonePath[0], uZonePath[1], uZonePath[2], uZonePath[3]); }
float gridAxis(float p) {
  return abs(mod(p - uGrid.x + uGrid.y * 0.5, uGrid.y) - uGrid.y * 0.5);
}

vec4 hash4(vec2 p, float salt) {
  vec4 q = fract(vec4(p.xyxy) * vec4(0.1031, 0.1030, 0.0973, 0.1099) + salt * 0.0713);
  q += dot(q, q.wzxy + 33.33);
  return fract((q.xxyz + q.yzzw) * q.zywx);
}
vec4 hash4(float n) {
  vec4 q = fract(vec4(n) * vec4(0.1031, 0.1030, 0.0973, 0.1099));
  q += dot(q, q.wzxy + 33.33);
  return fract((q.xxyz + q.yzzw) * q.zywx);
}

vec2 landUv(vec2 xz) { return (xz + uExtent) / (2.0 * uExtent) * (1024.0 / 1025.0) + 0.5 / 1025.0; }
vec3 streetLightAt(vec2 xz) { return texture(tStreetLights, xz / (2.0 * uExtent) + 0.5).rgb; }
float heightAt(vec2 xz) { return texture(tHeight, landUv(xz)).r; }
vec3 normalAt(vec2 xz) {
  const float e = 0.75;
  float hx = heightAt(xz + vec2(e, 0.0)) - heightAt(xz - vec2(e, 0.0));
  float hz = heightAt(xz + vec2(0.0, e)) - heightAt(xz - vec2(0.0, e));
  return normalize(vec3(-hx, 2.0 * e, -hz));
}

float gustAt(vec2 xz) {
  vec2 w = uWind.xy;
  vec2 q = vec2(dot(xz, w), dot(xz, vec2(-w.y, w.x)));
  float a = texture(tNoise, vec2(q.x - uTime * 4.0, q.y * 0.45) / 95.0).r;
  float b = texture(tNoise, vec2(q.x - uTime * 6.5, q.y * 0.6) / 23.0 + 0.37).g;
  return clamp(smoothstep(0.36, 0.74, a) * 0.8 + (b - 0.5) * 0.6, 0.0, 1.0) * uWind.z;
}

// Fast hardware-filtered 2x2 PCF static shadow lookup (WebGL2 LinearFilter + LessEqualCompare).
float woodSunRaw(vec3 world) {
  vec4 t = uTreeShadeMat * vec4(world, 1.0);
  if (t.x <= 0.002 || t.x >= 0.998 || t.y <= 0.002 || t.y >= 0.998 || t.z >= 1.0) return 1.0;
  return texture(tTreeShade, vec3(t.xy, t.z - uShadeBias));
}
float woodSunAt(vec3 world) { return woodSunRaw(world); }

// 3-tap triangle hardware-PCF dynamic shadow around protagonist/vehicles (12 hardware depth comparisons).
float figSunSoft(vec3 f) {
  if (f.x <= 0.004 || f.x >= 0.996 || f.y <= 0.004 || f.y >= 0.996 || f.z >= 1.0) return 1.0;
  float z = f.z - 0.0028;
  const float e = 0.0068;
  float s = texture(tFigShade, vec3(f.xy + vec2(0.0, e), z));
  s += texture(tFigShade, vec3(f.xy + vec2(e * 0.866, -e * 0.5), z));
  s += texture(tFigShade, vec3(f.xy + vec2(-e * 0.866, -e * 0.5), z));
  return s * (1.0 / 3.0);
}
float figSunAt(vec3 world) { return figSunSoft((uFigShadeMat * vec4(world, 1.0)).xyz); }
float sunAt(vec3 world) { return min(woodSunAt(world), figSunAt(world)); }

// Legacy smooth helper retained for compatibility (no hard cartoon cel stepping in realistic mode).
vec3 paint3(vec3 cl, vec3 cm, vec3 cs, float x, float r) {
  float k = clamp(x, 0.0, 1.0);
  vec3 c = mix(cs, cm, smoothstep(0.05, 0.52, k));
  return mix(c, cl, smoothstep(0.45, 0.95, k));
}

vec3 jitter(vec3 c, vec3 r, float amount) {
  amount *= 1.0 - uAnime * 0.92;
  float a = (r.y - 0.5) * 0.9 * amount;
  vec3 k = vec3(0.57735);
  c = c * cos(a) + cross(k, c) * sin(a) + k * dot(k, c) * (1.0 - cos(a));
  float g = dot(c, vec3(0.3, 0.59, 0.11));
  c = mix(vec3(g), c, 1.0 + (r.z - 0.5) * 0.7 * amount);
  return max(c * (1.0 + (r.x - 0.5) * 0.5 * amount), 0.0);
}

// --- Physically-based atmosphere & aerial perspective (Linear HDR) ---
vec3 airColour(vec3 v) {
  float sunward = max(dot(v, uSunDir), 0.0);
  float day = 1.0 - uNight;
  if (uAurora > 0.5) {
    vec3 nightAir = vec3(0.008, 0.018, 0.036) + vec3(0.006, 0.024, 0.022) * max(-v.z, 0.0);
    vec3 dayAir = vec3(0.38, 0.54, 0.76) + vec3(0.35, 0.25, 0.14) * pow(sunward, 4.0);
    return mix(dayAir, nightAir, uNight);
  }
  vec3 dayHaze = mix(vec3(0.36, 0.50, 0.68), vec3(0.72, 0.68, 0.58), pow(sunward, 2.0));
  vec3 twiHaze = vec3(0.85, 0.34, 0.14) * pow(sunward, 2.5) * uTwilight * 0.95;
  vec3 nightCityHaze = mix(vec3(0.018, 0.026, 0.052), toLinear(uHaze) * 0.35 + toLinear(uFoliage) * 0.09, 0.55);
  vec3 air = mix(dayHaze + twiHaze, nightCityHaze, uNight);
  air += vec3(0.45, 0.35, 0.18) * pow(sunward, 14.0) * day;
  return air;
}

float lampAt(vec3 world, vec3 n) {
  vec3 to = uLamp.xyz - world;
  float d2 = dot(to, to);
  float ndl = max(dot(n, to * inversesqrt(d2 + 1e-4)), 0.0);
  return uLamp.w * (ndl * 0.85 + 0.15) / (0.35 + d2 * 0.28);
}

float mistBetween(vec3 world) {
  vec3 d = world - cameraPosition;
  float dist = length(d);
  float up = clamp((0.5 * (world.y + cameraPosition.y) - uFocus.y - 2.0) / 38.0, 0.0, 1.0);
  float dens = uAurora > 0.5 ? uHazeDensity : mix(0.00145, 0.0036, uNight);
  return 1.0 - exp(-dist * dens * mix(1.0, 0.42, up));
}

vec3 hazed(vec3 c, vec3 world) {
  vec3 d = world - cameraPosition;
  float len = max(length(d), 1e-4);
  return mix(c, airColour(d / len), mistBetween(world));
}

// --- Unified AAA PBR BRDF + Analytic HDR IBL + Cotangent Normal Mapping ---
float pbrD_GGX(float ndh, float r) {
  float a = max(r * r, 0.002);
  float a2 = a * a;
  float d = (ndh * ndh) * (a2 - 1.0) + 1.0;
  return a2 / max(PI * d * d, 1e-5);
}
float pbrG_Smith(float ndv, float ndl, float r) {
  float k = (r + 1.0);
  k = (k * k) * 0.125;
  return (ndv / (ndv * (1.0 - k) + k)) * (ndl / (ndl * (1.0 - k) + k));
}
float pow5(float x) { float x2 = x * x; return x2 * x2 * x; }
vec3 pbrF_Schlick(float cosT, vec3 F0) {
  return F0 + (1.0 - F0) * pow5(clamp(1.0 - cosT, 0.0, 1.0));
}
vec3 pbrF_SchlickRough(float cosT, vec3 F0, float r) {
  return F0 + (max(vec3(1.0 - r), F0) - F0) * pow5(clamp(1.0 - cosT, 0.0, 1.0));
}

// Fast analytic diffuse hemisphere irradiance (zero transcendental ALU).
vec3 envIrradiance(vec3 N, vec3 fol, vec3 pale) {
  float h = N.y * 0.5 + 0.5;
  vec3 daySky = mix(vec3(0.38, 0.58, 0.92), vec3(0.54, 0.32, 0.31), uTwilight);
  vec3 dayGnd = vec3(0.18, 0.19, 0.21);
  vec3 dayIrr = mix(dayGnd, daySky, h);
  vec3 nightIrr;
  if (uAurora > 0.5) {
    vec3 aSky = vec3(0.045, 0.145, 0.145) + vec3(0.03, 0.10, 0.08) * max(-N.z, 0.0);
    nightIrr = mix(vec3(0.026, 0.042, 0.070), aSky, h);
  } else {
    vec3 nSky = vec3(0.042, 0.060, 0.105) + fol * 0.14 + pale * 0.07;
    vec3 nGnd = vec3(0.024, 0.030, 0.044) + fol * 0.05;
    nightIrr = mix(nGnd, nSky, h);
  }
  return mix(dayIrr, nightIrr, uNight);
}

// Analytic HDR IBL specular environment sampler (Day / Twilight / Night City / Aurora).
// Uses smooth roughness-dependent horizon blending and hardware sqrt for fast, NaN-free reflections.
vec3 envRadiance(vec3 R, float rough, vec3 fol, vec3 pale) {
  float up = clamp(R.y, -1.0, 1.0);
  float skyMix = smoothstep(-0.20 - rough * 0.38, 0.14 + rough * 0.38, up);
  float upPow = sqrt(max(up, 0.0));
  float downT = clamp(-up * 2.2, 0.0, 1.0);
  float day = 1.0 - uNight;

  vec3 zenithDay = mix(vec3(0.16, 0.42, 0.92), vec3(0.14, 0.20, 0.40), uTwilight);
  vec3 horizDay = mix(vec3(0.62, 0.76, 0.92), vec3(0.95, 0.44, 0.22), uTwilight);
  vec3 groundDay = vec3(0.12, 0.125, 0.135);
  vec3 dayEnv = mix(mix(horizDay * 0.55, groundDay, downT), mix(horizDay, zenithDay, upPow), skyMix);

  vec3 nightEnv;
  if (uAurora > 0.5) {
    vec3 zenithA = vec3(0.012, 0.025, 0.055);
    vec3 horizA = vec3(0.032, 0.082, 0.105);
    float a1 = (up - 0.34) / 0.26;
    float a2 = (up - 0.58) / 0.22;
    vec3 auroraGlow = vec3(0.06, 0.36, 0.28) * exp(-a1 * a1) * (0.45 + 0.55 * max(-R.z, 0.0));
    auroraGlow += vec3(0.18, 0.06, 0.34) * exp(-a2 * a2) * max(-R.z, 0.0);
    vec3 aSky = mix(horizA, zenithA, upPow) + auroraGlow * mix(1.0, 0.45, rough);
    nightEnv = mix(vec3(0.024, 0.040, 0.068), aSky, skyMix);
  } else {
    vec3 zenithN = vec3(0.016, 0.028, 0.068);
    vec3 horizN = vec3(0.060, 0.082, 0.135) + fol * 0.20 + pale * 0.10;
    vec3 groundN = vec3(0.024, 0.030, 0.044) + fol * 0.05;
    // Urban skyline reflection streaks on glossy metal/glass/wet-asphalt (skipped on rough matte surfaces)
    vec3 neonGlint = vec3(0.0);
    if (rough < 0.72) {
      float cb = (up - 0.10) / (0.16 + rough * 0.38);
      float cityBand = exp(-cb * cb);
      float windows = 0.5 + 0.5 * sin(R.x * 16.0 + R.z * 11.0 + R.y * 8.0) * cos(R.z * 21.0 - R.x * 13.0);
      float gl = 1.0 - rough;
      neonGlint = mix(fol, pale, smoothstep(0.3, 0.85, windows)) * cityBand * (gl * gl) * 0.85;
    }
    vec3 nSky = mix(horizN, zenithN, upPow) + neonGlint;
    nightEnv = mix(mix(horizN * 0.48, groundN, downT), nSky, skyMix);
  }
  vec3 env = mix(dayEnv, nightEnv, uNight);
  // Specular sun / moon highlight in environment reflection
  float sunDot = max(dot(R, uSunDir), 0.0);
  if (sunDot > 0.18) {
    float expPow = mix(64.0, 4.0, rough * rough);
    vec3 sunDisk = mix(vec3(2.8, 2.5, 2.0), vec3(0.42, 0.58, 0.88), uNight);
    env += sunDisk * pow(sunDot, expPow) * mix(1.4, 0.18, rough) * (0.45 + 0.55 * day);
  }
  return env;
}

// Primary directional key radiance (Sun by day, golden twilight, crisp Moon by night).
vec3 keyLightCol() {
  vec3 dayCol = mix(vec3(3.35, 3.05, 2.65), vec3(3.75, 1.65, 0.78), uTwilight);
  vec3 nightCol = uAurora > 0.5 ? vec3(0.58, 0.76, 1.08) : vec3(0.42, 0.54, 0.82);
  return mix(dayCol, nightCol, uNight);
}

// Full physically-based surface shading in Linear HDR space.
vec3 pbrShade(
  vec3 world, vec3 N, vec3 V,
  vec3 albedo, float rough, float metal,
  float clearcoat, float sss, float ao,
  float shadow, vec3 emissive
) {
  float r = clamp(rough, 0.045, 0.98);
  float ndv = max(dot(N, V), 0.001);
  vec3 F0 = mix(vec3(0.04), albedo, metal);

  // 1. Direct Sun / Moon Key Light (Cook-Torrance GGX)
  vec3 L = uSunDir;
  vec3 H = normalize(V + L);
  float ndl = max(dot(N, L), 0.0);
  float ndh = max(dot(N, H), 0.0);
  float vdh = max(dot(V, H), 0.0);
  vec3 keyCol = keyLightCol();

  vec3 F = pbrF_Schlick(vdh, F0);
  float D = pbrD_GGX(ndh, r);
  float G = pbrG_Smith(ndv, max(ndl, 0.001), r);
  vec3 spec = (D * G * F) / max(4.0 * ndv * max(ndl, 0.001), 0.001);
  vec3 kD = (1.0 - F) * (1.0 - metal);
  vec3 direct = (kD * albedo / PI + spec) * keyCol * ndl * shadow;

  // Subsurface scattering wrap for human skin, foliage & snow
  if (sss > 0.001) {
    float wrap = clamp((dot(N, L) + 0.45) / 1.45, 0.0, 1.0);
    float b = clamp(dot(V, -L + N * 0.4), 0.0, 1.0);
    float back = b * b * b * 0.5;
    vec3 sssTint = uAurora > 0.5 ? vec3(0.45, 0.78, 1.0) : vec3(1.0, 0.36, 0.20);
    direct += albedo * sssTint * keyCol * (wrap * 0.28 + back * 0.35) * sss * (0.35 + 0.65 * shadow);
  }

  // Clearcoat direct lobe (car paint, wet surfaces, eyes, glass)
  if (clearcoat > 0.001) {
    float Dc = pbrD_GGX(ndh, 0.06);
    float Gc = pbrG_Smith(ndv, max(ndl, 0.001), 0.06);
    float Fc = 0.04 + 0.96 * pow5(1.0 - vdh);
    direct += vec3(Dc * Gc * Fc / max(4.0 * ndv * max(ndl, 0.001), 0.001)) * keyCol * ndl * shadow * clearcoat * 0.65;
  }

  // 2. Analytic HDR Image-Based Lighting (1x diffuse hemisphere + 1x specular reflection)
  vec3 fol = vec3(0.0), pale = vec3(0.0);
  if (uAurora < 0.5 && uNight > 0.02) {
    fol = toLinear(zoneFoliageAt(world.xz));
    pale = toLinear(zoneLeafAt(world.xz));
  }
  vec3 R = reflect(-V, N);
  vec3 irradiance = envIrradiance(N, fol, pale);
  vec3 F_ibl = pbrF_SchlickRough(ndv, F0, r);
  vec3 diffIBL = (1.0 - F_ibl) * (1.0 - metal) * albedo * irradiance * 0.85;
  float specR = clearcoat > 0.25 ? min(r, 0.14) : r;
  vec3 prefSpec = envRadiance(R, specR, fol, pale);
  float envBrdf = mix(1.0, 0.38, r * r);
  vec3 specIBL = prefSpec * F_ibl * envBrdf;

  if (clearcoat > 0.001) {
    float Fc_ibl = 0.04 + 0.96 * pow5(1.0 - ndv);
    specIBL += prefSpec * Fc_ibl * clearcoat * 0.95;
  }
  vec3 ambient = (diffIBL + specIBL) * ao;

  // 3. Neo-Kyoto Urban Streetlight & Storefront Pools (diffuse + directional glossy bounce at night)
  vec3 urban = vec3(0.0);
  if (uAurora < 0.5 && uNight > 0.02) {
    vec3 pool = toLinear(streetLightAt(world.xz + N.xz * 0.65));
    float heightFade = 1.0 - smoothstep(1.5, 11.0, max(world.y, 0.0));
    float sideCatch = 0.45 + 0.55 * max(N.y * 0.5 + 0.5, 0.0);
    vec3 sL = normalize(vec3(-N.x * 0.25, 0.85, -N.z * 0.25) + vec3(0.01, 0.0, 0.01));
    vec3 sH = normalize(V + sL);
    float sSpec = pbrD_GGX(max(dot(N, sH), 0.0), max(r, 0.18)) * 0.10;
    urban = (kD * albedo * 0.85 + (F0 + vec3(clearcoat * 0.06)) * sSpec) * pool * heightFade * sideCatch * uNight * 1.05;
  }

  // 4. Carried Lantern / Dynamic Point Light (physical inverse-square attenuation + specular glint)
  vec3 lamp = vec3(0.0);
  if (uLamp.w > 0.001) {
    vec3 toL = uLamp.xyz - world;
    float d2 = dot(toL, toL);
    vec3 lDir = toL * inversesqrt(d2 + 1e-4);
    float atten = uLamp.w / (0.58 + d2 * 0.34);
    float lNdl = max(dot(N, lDir), 0.0);
    vec3 lH = normalize(V + lDir);
    float lSpec = pbrD_GGX(max(dot(N, lH), 0.0), r) * 0.18;
    vec3 lCol = toLinear(uLampCol) * 2.25;
    lamp = (kD * albedo * (lNdl + sss * 0.18) + (F0 + vec3(clearcoat * 0.05)) * lSpec * lNdl) * lCol * atten;
  }

  return direct + ambient + urban + lamp + emissive;
}
`;

// Fragment-only cotangent normal mapping helper (uses dFdx/dFdy, so kept out of vertex-shared COMMON).
export const PERTURB_NORMAL = /* glsl */ `
vec3 perturbNormal(vec3 N, vec3 world, vec2 uv, vec2 nXY, float strength) {
  vec3 dp1 = dFdx(world);
  vec3 dp2 = dFdy(world);
  vec2 duv1 = dFdx(uv);
  vec2 duv2 = dFdy(uv);
  vec3 dp2perp = cross(dp2, N);
  vec3 dp1perp = cross(N, dp1);
  vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
  vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;
  float invmax = inversesqrt(max(dot(T, T), dot(B, B)) + 1e-8);
  vec2 off = (nXY * 2.0 - 1.0) * strength;
  return normalize(N + (T * off.x + B * off.y) * invmax);
}
`;
