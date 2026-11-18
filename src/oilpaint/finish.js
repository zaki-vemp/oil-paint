// The canvas. The world is painted into a picture with a depth beside it; this then lays in the one thing dabs
// can't do, the sun standing in the mist between the trunks, and finishes the surface: the weave of the canvas
// under thin paint, and a little varnish.
import * as THREE from 'three';
import { COMMON } from './common.js';

export function createFrame(renderer, shared, { samples = 4 } = {}) {
  const size = renderer.getDrawingBufferSize(new THREE.Vector2());
  const target = new THREE.WebGLRenderTarget(size.x, size.y, { samples, depthBuffer: true, generateMipmaps: false });
  target.depthTexture = new THREE.DepthTexture(size.x, size.y, THREE.UnsignedIntType);
  target.texture.minFilter = target.texture.magFilter = THREE.LinearFilter;

  const uniforms = {
    ...shared,
    tColor: { value: target.texture },
    tDepth: { value: target.depthTexture },
    uInvViewProj: { value: new THREE.Matrix4() },
    uEye: { value: new THREE.Vector3() },
    uShafts: { value: 1 },
    uSteps: { value: 22 },
    uCanvas: { value: 1 },
    uSize: { value: size.clone() },
    // the anime finish: ink lines where depth breaks (0: none), a darkening toward the corners, and how
    // far the ink reaches before the air swallows it
    uInk: { value: 0 },
    uInkColor: { value: new THREE.Color(0.02, 0.03, 0.06) },
    uInkFar: { value: 140 },
    uVignette: { value: 0 },
  };
  const material = new THREE.ShaderMaterial({
    glslVersion: THREE.GLSL3,
    uniforms,
    depthTest: false,
    depthWrite: false,
    vertexShader: /* glsl */ `
      out vec2 vUv;
      void main() { vUv = uv; gl_Position = vec4(position.xy, 0.0, 1.0); }`,
    fragmentShader: /* glsl */ `
      ${COMMON}
      uniform sampler2D tColor;
      uniform sampler2D tDepth;
      uniform mat4 uInvViewProj;
      uniform vec3 uEye;
      uniform float uShafts;
      uniform float uSteps;
      uniform float uCanvas;
      uniform vec2 uSize;
      uniform float uInk;
      uniform vec3 uInkColor;
      uniform float uInkFar;
      uniform float uVignette;
      in vec2 vUv;

      float distAt(vec2 uv) {
        float d = texture(tDepth, uv).r;
        vec4 h = uInvViewProj * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
        return d >= 1.0 ? 1e5 : length(h.xyz / h.w - uEye);
      }
      layout(location = 0) out vec4 outC;

      float weave(vec2 p) {
        // warp and weft of a medium linen, each thread a little uneven
        vec2 q = p / 2.6;
        float n = texture(tNoise, p / 310.0).r;
        float a = sin(q.x * 3.14159) * sin(q.y * 3.14159 + floor(q.x) * 3.14159);
        return a * (0.3 + 1.4 * n * n);
      }

      void main() {
        vec3 col = texture(tColor, vUv).rgb;
        float depth = texture(tDepth, vUv).r;
        vec4 h = uInvViewProj * vec4(vUv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
        vec3 world = h.xyz / h.w;
        vec3 ray = world - uEye;
        float far = length(ray);
        ray /= far;

        // sun in the mist: walk the line of sight and count how much of the air along it stands in the light
        if (uShafts > 0.0) {
          float reach = min(far, 85.0);
          float jit = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
          float lit = 0.0, weight = 0.0;
          for (float i = 0.0; i < uSteps; i++) {
            float t = (i + jit) / uSteps;
            float w = exp(-t * reach * 0.022);
            lit += sunAt(uEye + ray * (t * reach)) * w;
            weight += w;
          }
          lit /= max(weight, 1e-3);
          float sunward = max(dot(ray, uSunDir), 0.0);
          float thick = 1.0 - exp(-reach * 0.03);
          // lit air glows toward the sun; shaded air is a touch darker and cooler than the plain mist
          float streak = 0.75 + 0.5 * texture(tNoise, vec2(dot(world.xz, vec2(uSunDir.z, -uSunDir.x)) * 0.11, world.y * 0.02)).g;
          vec3 glow = vec3(1.0, 0.86, 0.56) * (0.16 + 0.95 * pow(sunward, 2.5));
          col += glow * (lit - 0.34) * thick * streak * uShafts * 0.8;
        }

        if (uCanvas > 0.0) {
          vec2 px = vUv * uSize;
          float w = weave(px);
          float dx = weave(px + vec2(1.0, 0.0)) - w, dy = weave(px + vec2(0.0, 1.0)) - w;
          col *= 1.0 + (0.6 * dx - 0.8 * dy) * 0.022 * uCanvas + w * 0.006 * uCanvas;
        }
        if (uInk > 0.0 && depth < 1.0 && far < uInkFar) {
          // Near silhouettes get crisp manga sumi-ink; mid-ground skylines shift toward dark indigo and district haze
          vec2 px = mix(1.32, 0.95, clamp(far / 42.0, 0.0, 1.0)) / uSize;
          float n = max(max(distAt(vUv + vec2(px.x, 0.0)), distAt(vUv - vec2(px.x, 0.0))), max(distAt(vUv + vec2(0.0, px.y)), distAt(vUv - vec2(0.0, px.y))));
          float relJump = (n - far) / max(far, 0.5);
          float edge = smoothstep(0.045, 0.135, relJump);
          float fade = 1.0 - smoothstep(uInkFar * 0.42, uInkFar, far);
          float distMix = smoothstep(7.0, uInkFar * 0.72, far);
          vec3 air = airColour(ray);
          vec3 deepIndigo = mix(vec3(0.08, 0.10, 0.22), vec3(0.04, 0.06, 0.15), uNight);
          vec3 distInk = mix(deepIndigo, air * mix(0.48, 0.72, uNight) + uFoliage * (0.06 * uNight * uAnime), 0.64);
          vec3 inkCol = mix(uInkColor, distInk, distMix);
          float rimBand = smoothstep(0.02, 0.055, relJump) * (1.0 - edge) * smoothstep(10.0, 34.0, far) * step(35.0, n - far);
          col += (air * 0.12 + uFoliage * (0.06 * uNight * uAnime)) * rimBand * fade * uInk;
          col = mix(col, inkCol, edge * fade * uInk);
        }
        if (uVignette > 0.0) {
          vec2 v = (vUv - 0.5) * vec2(uSize.x / uSize.y, 1.0);
          col *= 1.0 - uVignette * smoothstep(0.25, 1.05, length(v));
        }
        if (uAnime > 0.5) { outC = vec4(col, 1.0); return; }
        // the brightest passages roll off toward the white of the paint and never past it
        vec3 over = max(col - 0.8, 0.0);
        col = min(col, 0.8) + over / (1.0 + over * 5.0);
        // varnish: a little more depth in the darks, the lights left alone
        col = mix(col, col * col * (3.0 - 2.0 * col), 0.22);
        outC = vec4(col, 1.0);
      }`,
  });
  const quad = new THREE.Mesh(new THREE.PlaneGeometry(2, 2), material);
  quad.frustumCulled = false;
  const scene = new THREE.Scene();
  scene.add(quad);
  const flat = new THREE.Camera();
  const viewProj = new THREE.Matrix4();

  return {
    target,
    uniforms,
    setSamples(samples) {
      if (target.samples === samples) return;
      target.dispose();
      target.samples = samples;
    },
    resize() {
      renderer.getDrawingBufferSize(size);
      target.setSize(size.x, size.y);
      uniforms.uSize.value.copy(size);
    },
    render(world, camera) {
      renderer.setRenderTarget(target);
      renderer.clear();
      renderer.render(world, camera);
      renderer.setRenderTarget(null);
      viewProj.multiplyMatrices(camera.projectionMatrix, camera.matrixWorldInverse);
      uniforms.uInvViewProj.value.copy(viewProj).invert();
      uniforms.uEye.value.copy(camera.position);
      renderer.render(scene, flat);
    },
  };
}
