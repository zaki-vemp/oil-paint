// Photorealistic Arctic Night Sky & Volumetric Aurora Borealis:
// - High-res atmospheric sky dome with pinpoint multi-temperature twinkling stars, subtle Milky Way dust lane,
//   and realistic cratered moon with icy atmospheric halo
// - Three billowing, silky 3D volumetric aurora ribbon curtains with fine vertical geomagnetic striations,
//   luminous emerald-mint foot rising into shimmering cyan-teal & royal amethyst/magenta upper corona (HDR Bloom)
import * as THREE from 'three';
import { COMMON } from '../oilpaint/common.js';
import { NIGHT } from './paint.js';

function buildAuroraCurtainGeo(segmentsU = 180, segmentsV = 12) {
  const geo = new THREE.PlaneGeometry(2, 1, segmentsU, segmentsV);
  // u in [-1..1] along curtain, v in [0..1] from curtain foot to upper corona
  const pos = geo.attributes.position;
  for (let i = 0; i < pos.count; i++) pos.setY(i, pos.getY(i) + 0.5);
  return geo;
}

export function createSky(shared) {
  const group = new THREE.Group();

  // 1. Starry polar night / alpine day sky dome with cratered moon & Milky Way band
  const dome = new THREE.Mesh(new THREE.SphereGeometry(900, 36, 24), new THREE.ShaderMaterial({
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
      void main() {
        vec3 v = normalize(vDir);
        vec3 col = nightSky(v);
        float nightVis = smoothstep(0.18, 0.78, uNight);

        // Subtle overhead Milky Way starlight dust band
        if (nightVis > 0.01 && v.y > 0.04) {
          float galCoord = dot(v, normalize(vec3(0.45, 0.55, -0.70)));
          float band = exp(-galCoord * galCoord * 16.0) * smoothstep(0.04, 0.35, v.y);
          float dust = texture(tNoise, v.xz / (v.y + 0.35) * 0.45 + 0.2).r;
          float rift = texture(tNoise, v.xz / (v.y + 0.35) * 1.15 - 0.1).g;
          col += vec3(0.018, 0.026, 0.046) * band * smoothstep(0.32, 0.85, dust) * (0.45 + 0.55 * rift) * nightVis;

          // Multi-layer crisp pinpoint twinkling starfield (dense faint stars + bright multi-temp beacons)
          vec2 g1 = v.xz / (v.y + 0.20) * 155.0;
          vec2 id1 = floor(g1), f1 = fract(g1) - 0.5;
          vec4 r1 = hash4(id1, 13.7);
          if (r1.x > 0.978) {
            float d = length(f1 - (r1.yz - 0.5) * 0.55);
            float pt = exp(-d * d * 56.0);
            float tw = 0.68 + 0.32 * sin(uTime * (2.2 + 5.0 * r1.w) + r1.x * 90.0);
            vec3 sc = mix(vec3(0.72, 0.86, 1.0), vec3(1.0, 0.84, 0.64), r1.w);
            col += sc * pt * tw * (0.55 + 1.35 * smoothstep(0.992, 0.999, r1.x)) * smoothstep(0.04, 0.24, v.y) * nightVis;
          }
          vec2 g2 = v.xz / (v.y + 0.25) * 72.0;
          vec2 id2 = floor(g2), f2 = fract(g2) - 0.5;
          vec4 r2 = hash4(id2, 57.3);
          if (r2.x > 0.991) {
            float d = length(f2 - (r2.yz - 0.5) * 0.50);
            float pt = exp(-d * d * 42.0);
            vec3 sc = mix(vec3(0.80, 0.92, 1.0), vec3(1.0, 0.90, 0.72), r2.w);
            col += sc * pt * 1.9 * smoothstep(0.08, 0.30, v.y) * nightVis;
          }
        }

        // Realistic cratered moon disc & atmospheric icy halo
        float mDot = dot(v, uSunDir);
        float moonEdge = smoothstep(0.9985, 0.9991, mDot) * uNight;
        if (moonEdge > 0.001) {
          vec3 mR = normalize(cross(vec3(0.0, 1.0, 0.0), uSunDir));
          vec3 mU = cross(uSunDir, mR);
          vec2 mUv = vec2(dot(v, mR), dot(v, mU)) * 16.0;
          float maria = 0.68 + 0.32 * texture(tNoise, mUv * 0.38 + 0.5).r;
          col = mix(col, vec3(2.2, 2.35, 2.55) * maria, moonEdge);
        }

        outC = vec4(col, 1.0);
      }`,
  }));
  dome.frustumCulled = false;
  dome.renderOrder = -10;
  group.add(dome);

  // 2. Three silky volumetric 3D aurora borealis curtains (no brush dabs — pure continuous HDR light ribbons)
  const curtainGeo = buildAuroraCurtainGeo(180, 14);
  const curtains = [];
  for (let k = 0; k < 3; k++) {
    const mat = new THREE.ShaderMaterial({
      glslVersion: THREE.GLSL3,
      uniforms: { ...shared, uCurtain: { value: k }, uGain: { value: k === 0 ? 1.45 : k === 1 ? 0.95 : 1.15 } },
      side: THREE.DoubleSide,
      transparent: true,
      depthTest: false,
      depthWrite: false,
      blending: THREE.AdditiveBlending,
      vertexShader: /* glsl */ `
        ${COMMON}
        ${NIGHT}
        uniform float uCurtain;
        out vec2 vUv;
        out float vWave;
        out float vEdge;
        vec3 ribbonFoot(float u, float k) {
          float t = uTime;
          if (k < 0.5) {
            float ph = u * 7.2 - t * 0.06;
            return vec3(11500.0 * u + 1600.0 * sin(ph),
              2150.0 + 260.0 * sin(u * 4.5 + t * 0.05) + 95.0 * sin(u * 19.0 - t * 0.45),
              -8800.0 + 2400.0 * cos(ph * 0.82 + 0.5) + 850.0 * sin(u * 15.0 + t * 0.28));
          }
          if (k < 1.5) {
            return vec3(16500.0 * u + 1100.0 * sin(u * 5.2 + t * 0.04),
              2650.0 + 160.0 * sin(u * 16.0 + t * 0.35),
              -16200.0 + 2100.0 * sin(u * 4.6 + 1.3 + t * 0.03) + 650.0 * sin(u * 21.0 - t * 0.32));
          }
          float z = 2500.0 * tan(mix(-1.42, 0.95, u * 0.5 + 0.5));
          return vec3(1800.0 + 1650.0 * sin(z / 2500.0 + t * 0.05) + 460.0 * sin(z / 680.0 - t * 0.28),
            2550.0 + 140.0 * sin(z / 520.0 + t * 0.42), z);
        }
        void main() {
          float u = position.x; // -1..1
          float s = position.y; // 0..1 vertical height along rays
          vUv = vec2(u * 0.5 + 0.5, s);
          vec3 foot = ribbonFoot(u, uCurtain);
          // Tall geomagnetic rays sweep gracefully upward with gentle lateral shimmer
          float tall = (uCurtain > 0.5 && uCurtain < 1.5 ? 2800.0 : 3400.0);
          vec3 P = foot + vec3(sin(u * 9.0 + s * 2.5 - uTime * 0.35) * 180.0 * s, s * tall, cos(u * 7.0 - s * 2.0 + uTime * 0.25) * 180.0 * s);
          vEdge = smoothstep(1.0, 0.72, abs(u));
          vWave = 0.5 + 0.5 * sin(u * 12.0 - uTime * 0.3 + uCurtain * 2.1);
          vec3 v = normalize(P);
          vec3 world = cameraPosition + v * 11000.0;
          gl_Position = projectionMatrix * viewMatrix * vec4(world, 1.0);
        }`,
      fragmentShader: /* glsl */ `
        ${COMMON}
        ${NIGHT}
        uniform float uCurtain;
        uniform float uGain;
        in vec2 vUv;
        in float vWave;
        in float vEdge;
        layout(location = 0) out vec4 outC;
        void main() {
          float nightVis = smoothstep(0.06, 0.52, uNight) * uGlow.x;
          if (nightVis < 0.005) discard;
          float u = vUv.x;
          float s = vUv.y;
          // Multi-scale silky vertical magnetic-field striations streaming along the curtain
          float macro = texture(tNoise, vec2(u * 1.4 + uTime * 0.008 + uCurtain * 0.31, 0.22 + uCurtain * 0.17)).r;
          float rays1 = texture(tNoise, vec2(u * 9.5 - uTime * 0.022 + uCurtain * 0.19, s * 0.18 + 0.41)).g;
          float rays2 = texture(tNoise, vec2(u * 27.0 + uTime * 0.045, s * 0.24 - uTime * 0.03)).b;
          float fine  = texture(tNoise, vec2(u * 68.0 - uTime * 0.065, s * 0.12 + 0.73)).a;

          float st = (fract(u - uTime * 0.032 + uCurtain * 0.37) - 0.5) / 0.11;
          float surge = exp(-st * st);
          float rayStrength = (0.35 + 0.65 * smoothstep(0.24, 0.80, macro)) * (0.32 + 0.68 * smoothstep(0.22, 0.78, rays1)) * (0.55 + 0.65 * rays2) * (0.72 + 0.45 * fine);
          rayStrength *= 1.0 + 0.85 * surge;

          // Sharp luminous lower curtain boundary, long silky exponential vertical decay upward
          float lowerBorder = smoothstep(0.0, 0.085, s);
          float upperFade = exp(-s * (2.05 - 0.55 * rays1 - 0.35 * surge)) * (1.0 - smoothstep(0.82, 1.0, s));
          float density = lowerBorder * upperFade * rayStrength * vEdge * nightVis;

          // Physical atmospheric emission spectrum:
          // Low 100km oxygen (557.7nm luminous emerald-mint) -> mid cyan-teal -> high 250km oxygen/nitrogen (violet-magenta)
          vec3 pinkFringe = vec3(1.15, 0.22, 0.48);
          vec3 emerald = vec3(0.06, 1.15, 0.66);
          vec3 cyanTeal = vec3(0.05, 0.72, 0.92);
          vec3 violet = vec3(0.46, 0.16, 1.05);
          vec3 magenta = vec3(0.78, 0.14, 0.66);

          vec3 col = mix(emerald, cyanTeal, smoothstep(0.14, 0.42, s));
          col = mix(col, violet, smoothstep(0.34, 0.68, s));
          col = mix(col, magenta, smoothstep(0.66, 0.94, s));
          // Intense luminous emerald-white core right along the sharp lower foot + rare crimson-pink nitrogen fringe underneath
          float ct = (s - 0.09) / 0.06;
          col = mix(col, vec3(0.58, 1.35, 0.95), exp(-ct * ct) * smoothstep(0.35, 0.85, rayStrength) * 0.75);
          col = mix(pinkFringe, col, smoothstep(0.015, 0.075, s));

          vec3 hdr = col * density * uGain;
          outC = vec4(hdr, clamp(density, 0.0, 1.0));
        }`,
    });
    const mesh = new THREE.Mesh(curtainGeo, mat);
    mesh.frustumCulled = false;
    mesh.renderOrder = -8.5 + k * 0.1;
    group.add(mesh);
    curtains.push(mesh);
  }

  function setQuality(level) {
    curtains[1].visible = level !== 'low';
  }
  return { group, setQuality };
}
