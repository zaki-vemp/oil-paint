// Realistic dark arctic fjord water with animated normal-mapped swell & Fresnel aurora/moon reflections,
// ringed by three smooth stratified alpine mountain ranges fading into atmospheric blue mist.
import * as THREE from 'three';
import { COMMON } from '../oilpaint/common.js';
import { noise } from '../world/noise.js';
import { NIGHT } from './paint.js';

const RINGS = [
  { r: 2300, top: 620, seed: 3.1, open: [0.0, 0.3, 1.0] },
  { r: 4300, top: 1000, seed: 11.7, open: [0.35, 0.12, 0.6] },
  { r: 7600, top: 1500, seed: 23.9, open: [0, 0, 0] },
];
const sstep = (a, b, x) => { const t = Math.min(1, Math.max(0, (x - a) / (b - a))); return t * t * (3 - 2 * t); };

function crest(k, az, broad = false) {
  const o = RINGS[k], c = Math.cos(az), s = Math.sin(az);
  let n = 0.45 + 0.55 * noise(c * 2.2 + o.seed, s * 2.2) + 0.28 * noise(c * 6.5, s * 6.5 + o.seed);
  if (!broad) n += 0.12 * noise(c * 17 + o.seed, s * 17) + 0.05 * noise(c * 41, s * 41 + o.seed);
  const turn = Math.abs(((az - o.open[0] + Math.PI * 3) % (Math.PI * 2)) - Math.PI);
  const gap = o.open[2] > 0 ? 0.1 + 0.9 * sstep(o.open[1], o.open[2], turn) : 1;
  return o.top * gap * Math.min(1.4, Math.max(0.1, n));
}

export function createFar(shared) {
  const group = new THREE.Group();
  const lay = (mesh, order) => {
    mesh.material.depthTest = false;
    mesh.material.depthWrite = false;
    mesh.renderOrder = order;
    mesh.frustumCulled = false;
    group.add(mesh);
    return mesh;
  };

  // Animated normal-mapped arctic fjord water bands reflecting aurora & moonlight shimmer
  const bands = [[60, 2200], [2200, 4200], [4200, 7500], [7500, 15500]];
  const waterMat = new THREE.ShaderMaterial({
    glslVersion: THREE.GLSL3,
    uniforms: shared,
    vertexShader: /* glsl */ `
      out vec3 vWorld;
      void main() { vWorld = position; gl_Position = projectionMatrix * viewMatrix * vec4(position, 1.0); }`,
    fragmentShader: /* glsl */ `
      ${COMMON}
      ${NIGHT}
      in vec3 vWorld;
      layout(location = 0) out vec4 outC;
      void main() {
        vec3 V = normalize(cameraPosition - vWorld);
        vec2 w1 = texture(tDetail, vWorld.xz * 0.0085 + vec2(uTime * 0.015, -uTime * 0.010)).rg * 2.0 - 1.0;
        vec2 w2 = texture(tDetail, vWorld.xz * 0.024 - vec2(uTime * 0.022, uTime * 0.014)).rg * 2.0 - 1.0;
        vec3 N = normalize(vec3((w1.x * 0.14 + w2.x * 0.08), 1.0, (w1.y * 0.14 + w2.y * 0.08)));
        vec3 R = reflect(-V, N);
        R.y = abs(R.y);
        float fres = 0.02 + 0.98 * pow(1.0 - max(dot(N, V), 0.0), 4.2);
        vec3 skyRefl = nightSky(R) * 1.45 + auroraSoft(R) * 1.35 + auroraSoft(normalize(vec3(R.x, R.y * 2.2 + 0.12, R.z))) * 0.65;
        float moonSpec = pow(max(dot(R, uSunDir), 0.0), 64.0) * 1.8 + pow(max(dot(R, uSunDir), 0.0), 10.0) * 0.18;
        vec3 deepWater = vec3(0.005, 0.015, 0.028);
        vec3 col = mix(deepWater, skyRefl, fres) + vec3(0.65, 0.80, 1.0) * moonSpec * (0.35 + 0.65 * fres);
        outC = vec4(hazed(col, vWorld), 1.0);
      }`,
  });
  bands.forEach(([inner, outer], b) => {
    lay(new THREE.Mesh(new THREE.RingGeometry(inner, outer, 128, 1).rotateX(-Math.PI / 2), waterMat), -6.4 - 0.4 * b);
  });

  // Three stratified distant alpine mountain silhouettes
  const SEG = 720;
  RINGS.forEach((o, k) => {
    const order = -6.6 - 0.4 * k;
    const wall = new THREE.BufferGeometry();
    const p = new Float32Array((SEG + 1) * 2 * 3);
    const uv = new Float32Array((SEG + 1) * 2 * 2);
    const index = [];
    for (let i = 0; i <= SEG; i++) {
      const az = (i / SEG) * Math.PI * 2, x = Math.sin(az) * o.r, z = -Math.cos(az) * o.r;
      p.set([x, -20, z, x, crest(k, az), z], i * 6);
      uv.set([i / SEG, 0, i / SEG, 1], i * 4);
      if (i < SEG) index.push(i * 2, i * 2 + 1, i * 2 + 2, i * 2 + 1, i * 2 + 3, i * 2 + 2);
    }
    wall.setAttribute('position', new THREE.BufferAttribute(p, 3));
    wall.setAttribute('uv', new THREE.BufferAttribute(uv, 2));
    wall.setIndex(index);
    lay(new THREE.Mesh(wall, new THREE.ShaderMaterial({
      glslVersion: THREE.GLSL3,
      uniforms: shared,
      side: THREE.DoubleSide,
      vertexShader: /* glsl */ `
        out vec3 vWorld; out vec2 vUv;
        void main() { vWorld = position; vUv = uv; gl_Position = projectionMatrix * viewMatrix * vec4(position, 1.0); }`,
      fragmentShader: /* glsl */ `
        ${COMMON}
        ${NIGHT}
        in vec3 vWorld; in vec2 vUv;
        layout(location = 0) out vec4 outC;
        void main() {
          float n = texture(tNoise, vec2(vUv.x * 24.0, vUv.y * 4.5)).r;
          float n2 = texture(tDetail, vec2(vUv.x * 48.0, vUv.y * 9.0)).b;
          float snow = smoothstep(0.32, 0.78, vUv.y + (n - 0.5) * 0.28);
          vec3 faceDir = normalize(vec3(-vWorld.x, 0.35 * length(vWorld.xz), -vWorld.z));
          float lit = clamp(dot(faceDir, uSunDir) * 0.8 + 0.28, 0.0, 1.0) * (0.72 + 0.45 * n2);
          vec3 rock = rockPaint(lit, n, n2);
          vec3 sn = snowPaint(lit, n, 0.45 * uGlow.x, n2);
          vec3 col = mix(rock, sn, snow) * 0.85;
          outC = vec4(hazed(col, vWorld), 1.0);
        }`,
    })), order);
  });

  return { group };
}
