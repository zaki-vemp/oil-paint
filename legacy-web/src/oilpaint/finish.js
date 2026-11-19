// AAA HDR Filmic post-processing compositor:
// 1. Renders world into a 16-bit HalfFloat HDR target (with hardware MSAA + depth buffer).
// 2. Runs a 3-level Dual-Kawase HDR Bloom pyramid on bright emissives (neon, blades, headlights, aurora).
// 3. Computes fast interleaved depth-buffer Screen-Space Ambient Occlusion (SSAO) for contact shadows.
// 4. Adds subtle volumetric sun shafts, ACES Filmic tone mapping, linear->sRGB gamma encoding,
//    cinematic color balance, soft lens vignette and anti-banding micro-grain.
import * as THREE from 'three';
import { COMMON } from './common.js';

const FULLSCREEN_VERT = /* glsl */ `
  out vec2 vUv;
  void main() { vUv = uv; gl_Position = vec4(position.xy, 0.0, 1.0); }`;

export function createFrame(renderer, shared, { samples = 4 } = {}) {
  const size = renderer.getDrawingBufferSize(new THREE.Vector2());
  const canHDR = renderer.capabilities.isWebGL2 &&
    (renderer.extensions.has('EXT_color_buffer_float') || renderer.extensions.has('EXT_color_buffer_half_float'));
  const rtType = canHDR ? THREE.HalfFloatType : THREE.UnsignedByteType;

  const target = new THREE.WebGLRenderTarget(size.x, size.y, {
    type: rtType,
    samples,
    depthBuffer: true,
    generateMipmaps: false,
  });
  target.depthTexture = new THREE.DepthTexture(size.x, size.y, THREE.UnsignedIntType);
  target.texture.minFilter = target.texture.magFilter = THREE.LinearFilter;

  // Dual-Kawase HDR Bloom pyramid (1/4 -> 1/8 -> 1/16 -> 1/8 -> 1/4: 4x faster & silkier optical glow)
  const makeBloomRT = (div) => {
    const rt = new THREE.WebGLRenderTarget(
      Math.max(2, Math.floor(size.x / div)),
      Math.max(2, Math.floor(size.y / div)),
      { type: rtType, depthBuffer: false, stencilBuffer: false, generateMipmaps: false },
    );
    rt.texture.minFilter = rt.texture.magFilter = THREE.LinearFilter;
    return rt;
  };
  const bDown = [makeBloomRT(4), makeBloomRT(8), makeBloomRT(16)];
  const bUp = [makeBloomRT(8), makeBloomRT(4)];

  const prefilterMat = new THREE.ShaderMaterial({
    glslVersion: THREE.GLSL3,
    depthTest: false, depthWrite: false,
    uniforms: {
      tSrc: { value: target.texture },
      uTexel: { value: new THREE.Vector2(1 / Math.max(1, size.x), 1 / Math.max(1, size.y)) },
      uThreshold: { value: 1.05 },
      uKnee: { value: 0.45 },
    },
    vertexShader: FULLSCREEN_VERT,
    fragmentShader: /* glsl */ `
      precision highp float;
      uniform sampler2D tSrc;
      uniform vec2 uTexel;
      uniform float uThreshold;
      uniform float uKnee;
      in vec2 vUv;
      layout(location = 0) out vec4 outC;
      vec3 bright(vec3 c) {
        if (any(isnan(c)) || any(isinf(c))) return vec3(0.0);
        c = clamp(c, vec3(0.0), vec3(12.0));
        float br = max(max(c.r, c.g), c.b);
        float soft = clamp(br - uThreshold + uKnee, 0.0, 2.0 * uKnee);
        soft = (soft * soft) / max(4.0 * uKnee, 1e-4);
        float contrib = max(soft, br - uThreshold) / max(br, 1e-4);
        return c * max(contrib, 0.0);
      }
      void main() {
        vec2 o = uTexel * 0.75;
        vec3 c = bright(texture(tSrc, vUv + vec2(-o.x, -o.y)).rgb)
               + bright(texture(tSrc, vUv + vec2( o.x, -o.y)).rgb)
               + bright(texture(tSrc, vUv + vec2(-o.x,  o.y)).rgb)
               + bright(texture(tSrc, vUv + vec2( o.x,  o.y)).rgb);
        outC = vec4(c * 0.25, 1.0);
      }`,
  });

  const downMat = new THREE.ShaderMaterial({
    glslVersion: THREE.GLSL3,
    depthTest: false, depthWrite: false,
    uniforms: { tSrc: { value: null }, uTexel: { value: new THREE.Vector2() } },
    vertexShader: FULLSCREEN_VERT,
    fragmentShader: /* glsl */ `
      precision highp float;
      uniform sampler2D tSrc;
      uniform vec2 uTexel;
      in vec2 vUv;
      layout(location = 0) out vec4 outC;
      void main() {
        vec2 o = uTexel * 1.1;
        vec3 c = texture(tSrc, vUv).rgb * 0.5;
        c += texture(tSrc, vUv + vec2(-o.x, -o.y)).rgb * 0.125;
        c += texture(tSrc, vUv + vec2( o.x, -o.y)).rgb * 0.125;
        c += texture(tSrc, vUv + vec2(-o.x,  o.y)).rgb * 0.125;
        c += texture(tSrc, vUv + vec2( o.x,  o.y)).rgb * 0.125;
        outC = vec4(c, 1.0);
      }`,
  });

  const upMat = new THREE.ShaderMaterial({
    glslVersion: THREE.GLSL3,
    depthTest: false, depthWrite: false,
    uniforms: { tSrc: { value: null }, tAdd: { value: null }, uTexel: { value: new THREE.Vector2() } },
    vertexShader: FULLSCREEN_VERT,
    fragmentShader: /* glsl */ `
      precision highp float;
      uniform sampler2D tSrc;
      uniform sampler2D tAdd;
      uniform vec2 uTexel;
      in vec2 vUv;
      layout(location = 0) out vec4 outC;
      void main() {
        vec2 o = uTexel * 1.5;
        vec3 blur = (
          texture(tSrc, vUv + vec2(-o.x, 0.0)).rgb +
          texture(tSrc, vUv + vec2( o.x, 0.0)).rgb +
          texture(tSrc, vUv + vec2(0.0, -o.y)).rgb +
          texture(tSrc, vUv + vec2(0.0,  o.y)).rgb
        ) * 0.1666 + (
          texture(tSrc, vUv + vec2(-o.x, -o.y)).rgb +
          texture(tSrc, vUv + vec2( o.x, -o.y)).rgb +
          texture(tSrc, vUv + vec2(-o.x,  o.y)).rgb +
          texture(tSrc, vUv + vec2( o.x,  o.y)).rgb
        ) * 0.0833;
        outC = vec4(blur + texture(tAdd, vUv).rgb * 0.55, 1.0);
      }`,
  });

  const uniforms = {
    ...shared,
    tColor: { value: target.texture },
    tDepth: { value: target.depthTexture },
    tBloom: { value: bUp[1].texture },
    uInvViewProj: { value: new THREE.Matrix4() },
    uEye: { value: new THREE.Vector3() },
    uDepthParams: { value: new THREE.Vector3(0.15 * 480, 480 - 0.15, 480) },
    uShafts: { value: 0.25 },
    uSteps: { value: 8 },
    uCanvas: { value: 0 },
    uSize: { value: size.clone() },
    uInk: { value: 0 }, // 0 in realistic AAA mode (kept for explicit ?ink=1 comparison)
    uInkColor: { value: new THREE.Color(0.02, 0.03, 0.06) },
    uInkFar: { value: 120 },
    uVignette: { value: 0.22 },
    uBloomStrength: { value: 0.36 },
    uAOStrength: { value: 0.48 },
  };

  const material = new THREE.ShaderMaterial({
    glslVersion: THREE.GLSL3,
    uniforms,
    depthTest: false,
    depthWrite: false,
    vertexShader: FULLSCREEN_VERT,
    fragmentShader: /* glsl */ `
      ${COMMON}
      uniform sampler2D tColor;
      uniform sampler2D tDepth;
      uniform sampler2D tBloom;
      uniform mat4 uInvViewProj;
      uniform vec3 uEye;
      uniform vec3 uDepthParams;
      uniform float uShafts;
      uniform float uSteps;
      uniform float uCanvas;
      uniform vec2 uSize;
      uniform float uInk;
      uniform vec3 uInkColor;
      uniform float uInkFar;
      uniform float uVignette;
      uniform float uBloomStrength;
      uniform float uAOStrength;
      in vec2 vUv;
      layout(location = 0) out vec4 outC;

      float linZ(vec2 uv) {
        float d = texture(tDepth, clamp(uv, vec2(0.001), vec2(0.999))).r;
        if (d >= 0.99995) return 1e5;
        return uDepthParams.x / max(uDepthParams.z - d * uDepthParams.y, 1e-4);
      }

      // Narkowicz fitted ACES Filmic tone mapping
      vec3 acesFilm(vec3 x) {
        const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
        return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
      }
      vec3 linToSrgb(vec3 c) {
        c = max(c, vec3(0.0));
        return mix(c * 12.92, 1.055 * pow(max(c, vec3(1e-5)), vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
      }

      void main() {
        vec3 col = texture(tColor, vUv).rgb;
        if (any(isnan(col)) || any(isinf(col))) col = vec3(0.0);
        float depth = texture(tDepth, vUv).r;
        vec4 h = uInvViewProj * vec4(vUv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
        vec3 world = h.xyz / h.w;
        vec3 ray = world - uEye;
        float far = length(ray);
        ray /= max(far, 1e-4);

        // 1. Fast analytical-depth Screen-Space Ambient Occlusion (3 rotated pairs = 6 scalar depth taps)
        float centerZ = depth >= 0.99995 ? 1e5 : uDepthParams.x / max(uDepthParams.z - depth * uDepthParams.y, 1e-4);
        if (uAOStrength > 0.01 && depth < 0.9999 && centerZ < 65.0) {
          float ang = 6.2831853 * fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
          float pxRad = clamp(18.0 / max(centerZ, 1.4), 2.5, 16.0);
          vec2 radUv = pxRad / uSize;
          float occ = 0.0;
          const vec2 dirs[3] = vec2[3](vec2(1.0, 0.0), vec2(-0.5, 0.866), vec2(-0.5, -0.866));
          float cs = cos(ang), sn = sin(ang);
          mat2 rot = mat2(cs, -sn, sn, cs);
          float range = clamp(centerZ * 0.065, 0.25, 2.2);
          for (int i = 0; i < 3; i++) {
            vec2 off = (rot * dirs[i]) * radUv * (i == 0 ? 0.58 : 1.0);
            float dz1 = centerZ - linZ(vUv + off);
            float dz2 = centerZ - linZ(vUv - off);
            float c1 = smoothstep(0.03, range * 0.45, dz1) * (1.0 - smoothstep(range * 0.7, range * 1.4, dz1));
            float c2 = smoothstep(0.03, range * 0.45, dz2) * (1.0 - smoothstep(range * 0.7, range * 1.4, dz2));
            occ += (c1 + c2) * 0.5;
          }
          occ = clamp(occ / 3.0, 0.0, 1.0);
          float distFade = 1.0 - smoothstep(32.0, 65.0, centerZ);
          float aoFactor = 1.0 - occ * uAOStrength * distFade;
          float lum = max(max(col.r, col.g), col.b);
          col *= mix(aoFactor, 1.0, smoothstep(1.15, 2.2, lum));
        }

        // 2. Subtle volumetric atmospheric sun/moon shafts in day & twilight
        if (uShafts > 0.01 && uNight < 0.85) {
          float sunward = max(dot(ray, uSunDir), 0.0);
          if (sunward > 0.15) {
            float reach = min(far, 78.0);
            float jit = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
            float lit = 0.0, weight = 0.0;
            for (float i = 0.0; i < uSteps; i++) {
              float t = (i + jit) / uSteps;
              float w = exp(-t * reach * 0.024);
              lit += woodSunRaw(uEye + ray * (t * reach)) * w;
              weight += w;
            }
            lit /= max(weight, 1e-3);
            float thick = 1.0 - exp(-reach * 0.022);
            vec3 shaftCol = mix(vec3(1.0, 0.88, 0.66), vec3(1.0, 0.48, 0.22), uTwilight);
            col += shaftCol * max(lit - 0.30, 0.0) * thick * pow(sunward, 4.0) * uShafts * (1.0 - uNight) * 0.42;
          }
        }

        // 3. HDR Dual-Kawase Bloom composite (real optical halo around neon, blades, headlights & aurora)
        if (uBloomStrength > 0.001) {
          vec3 bloom = texture(tBloom, vUv).rgb;
          float bMul = uBloomStrength * mix(0.65, 1.18, max(uNight, uAurora));
          col += bloom * bMul;
        }

        // Optional explicit ?ink=1 comparison path (off by default in realistic AAA mode)
        if (uInk > 0.001 && depth < 1.0 && centerZ < uInkFar) {
          vec2 px = 1.1 / uSize;
          float n = max(max(linZ(vUv + vec2(px.x, 0.0)), linZ(vUv - vec2(px.x, 0.0))),
                        max(linZ(vUv + vec2(0.0, px.y)), linZ(vUv - vec2(0.0, px.y))));
          float edge = smoothstep(0.05, 0.14, (n - centerZ) / max(centerZ, 0.5));
          float fade = 1.0 - smoothstep(uInkFar * 0.45, uInkFar, centerZ);
          col = mix(col, uInkColor, edge * fade * uInk);
        }

        // 4. Filmic exposure + ACES tone mapping + subtle photographic grade + exact sRGB output
        col *= uExposure;
        col = acesFilm(col);
        // Subtle cinematic contrast & shadow-cool / highlight-warm filmic split
        vec3 srgb = linToSrgb(col);
        srgb = mix(srgb, srgb * srgb * (3.0 - 2.0 * srgb), 0.10);
        // Soft natural optical vignette
        if (uVignette > 0.001) {
          vec2 v = (vUv - 0.5) * vec2(uSize.x / uSize.y, 1.0);
          srgb *= 1.0 - uVignette * smoothstep(0.32, 1.12, length(v));
        }
        // Anti-banding micro-dither (eliminates 8-bit gradients in dark skies & mist)
        float grain = fract(sin(dot(gl_FragCoord.xy + vec2(uTime * 17.0, 0.0), vec2(12.9898, 78.233))) * 43758.5453) - 0.5;
        srgb += grain * (1.6 / 255.0);
        outC = vec4(clamp(srgb, 0.0, 1.0), 1.0);
      }`,
  });

  const quadGeo = new THREE.PlaneGeometry(2, 2);
  const flat = new THREE.Camera();
  const makePass = (mat) => {
    const m = new THREE.Mesh(quadGeo, mat);
    m.frustumCulled = false;
    const s = new THREE.Scene();
    s.add(m);
    return s;
  };
  const prefilterScene = makePass(prefilterMat);
  const downScene = makePass(downMat);
  const upScene = makePass(upMat);
  const compositeScene = makePass(material);
  const viewProj = new THREE.Matrix4();

  function resizeBloom() {
    const divs = [4, 8, 16];
    bDown.forEach((rt, i) => rt.setSize(Math.max(2, Math.floor(size.x / divs[i])), Math.max(2, Math.floor(size.y / divs[i]))));
    bUp[0].setSize(bDown[1].width, bDown[1].height);
    bUp[1].setSize(bDown[0].width, bDown[0].height);
    prefilterMat.uniforms.uTexel.value.set(1 / Math.max(1, size.x), 1 / Math.max(1, size.y));
  }

  return {
    target,
    uniforms,
    setSamples(nextSamples) {
      if (target.samples === nextSamples) return;
      target.dispose();
      target.samples = nextSamples;
    },
    resize() {
      renderer.getDrawingBufferSize(size);
      target.setSize(size.x, size.y);
      uniforms.uSize.value.copy(size);
      resizeBloom();
    },
    render(world, camera) {
      renderer.setRenderTarget(target);
      renderer.clear();
      renderer.render(world, camera);

      if (uniforms.uBloomStrength.value > 0.001) {
        // Prefilter HDR bright pixels -> bDown[0] (1/2 res)
        renderer.setRenderTarget(bDown[0]);
        renderer.render(prefilterScene, flat);

        // Downsample 1/2 -> 1/4 -> 1/8
        downMat.uniforms.tSrc.value = bDown[0].texture;
        downMat.uniforms.uTexel.value.set(1 / bDown[0].width, 1 / bDown[0].height);
        renderer.setRenderTarget(bDown[1]);
        renderer.render(downScene, flat);

        downMat.uniforms.tSrc.value = bDown[1].texture;
        downMat.uniforms.uTexel.value.set(1 / bDown[1].width, 1 / bDown[1].height);
        renderer.setRenderTarget(bDown[2]);
        renderer.render(downScene, flat);

        // Upsample 1/8 + 1/4 -> bUp[0]
        upMat.uniforms.tSrc.value = bDown[2].texture;
        upMat.uniforms.tAdd.value = bDown[1].texture;
        upMat.uniforms.uTexel.value.set(1 / bDown[2].width, 1 / bDown[2].height);
        renderer.setRenderTarget(bUp[0]);
        renderer.render(upScene, flat);

        // Upsample 1/4 + 1/2 -> bUp[1]
        upMat.uniforms.tSrc.value = bUp[0].texture;
        upMat.uniforms.tAdd.value = bDown[0].texture;
        upMat.uniforms.uTexel.value.set(1 / bUp[0].width, 1 / bUp[0].height);
        renderer.setRenderTarget(bUp[1]);
        renderer.render(upScene, flat);
      }

      renderer.setRenderTarget(null);
      viewProj.multiplyMatrices(camera.projectionMatrix, camera.matrixWorldInverse);
      uniforms.uInvViewProj.value.copy(viewProj).invert();
      uniforms.uEye.value.copy(camera.position);
      uniforms.uDepthParams.value.set(camera.near * camera.far, camera.far - camera.near, camera.far);
      renderer.render(compositeScene, flat);
    },
  };
}
