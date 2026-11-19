// AAA physically-based (PBR) character & asset shader plus realistic MakeHuman protagonist setup.
// Renders skinned and rigid meshes with Cook-Torrance GGX specular, analytic HDR sky/city/aurora IBL,
// clearcoat, subsurface scattering, anisotropic hair, procedural micro-normals, and vertex-alpha
// PBR material classes (COLOR_0.a) for every Blender street asset, vehicle, citizen, and enemy.
import * as THREE from 'three';
import { COMMON, PERTURB_NORMAL } from '../oilpaint/common.js';
import { loadMan } from './man.js';
import { clone } from 'three/addons/utils/SkeletonUtils.js';
import { attachAvatar } from './avatar.js';

const PBR_FRAG = /* glsl */ `
${COMMON}
${PERTURB_NORMAL}
uniform vec3 uL; uniform vec3 uM; uniform vec3 uS;
uniform vec3 uL2; uniform vec3 uM2; uniform vec3 uS2;
uniform vec3 uOwn; uniform vec3 uOwn2;
uniform float uSplit;
uniform float uGrain;
uniform sampler2D tAlbedo; uniform float uAlbedo; uniform vec3 uTint;
uniform float uCutoff;
uniform float uRough;
uniform float uMetal;
uniform float uClearcoat;
uniform float uSSS;
uniform float uSurfaceKind; // 0=generic/vertex-class, 1=skin, 2=eyes, 3=hair, 4=techsuit, 5=boots, 6=emissive-signal
uniform vec3 uAccent;
in vec3 vWorld; in vec3 vN; in vec3 vBind; in vec2 vUv;
#if defined(USE_COLOR_ALPHA) || defined(USE_COLOR)
in vec4 vColorA;
#endif
layout(location = 0) out vec4 outC;

// Decode PBR parameters from Blender COLOR_0.a material class (0..1).
void decodeMaterialClass(float cls, vec3 linCol, out float rough, out float metal, out float coat, out float sss, out int detailMode) {
  detailMode = 0; // 0=cloth, 1=skin, 2=hair, 3=leather/rubber, 4=polymer/stone, 5=brushed-metal, 6=carpaint, 7=glass
  if (cls < 0.12) {
    // Matte woven cloth / fabric
    rough = 0.84; metal = 0.0; coat = 0.0; sss = 0.06; detailMode = 0;
  } else if (cls < 0.24) {
    // Realistic human skin
    rough = 0.44; metal = 0.0; coat = 0.08; sss = 0.62; detailMode = 1;
  } else if (cls < 0.36) {
    // Hair / fur
    rough = 0.36; metal = 0.0; coat = 0.14; sss = 0.08; detailMode = 2;
  } else if (cls < 0.48) {
    // Worn leather / tire rubber / timber
    rough = 0.62; metal = 0.0; coat = 0.05; sss = 0.0; detailMode = 3;
  } else if (cls < 0.60) {
    // Tactical matte polymer / painted armor / concrete / stone
    rough = 0.42; metal = 0.22; coat = 0.08; sss = 0.0; detailMode = 4;
  } else if (cls < 0.74) {
    // Brushed steel / titanium / alloy rims / gunmetal
    rough = 0.22; metal = 0.90; coat = 0.15; sss = 0.0; detailMode = 5;
  } else if (cls < 0.88) {
    // Glossy clearcoat automotive paint / lacquered shell
    rough = 0.17; metal = 0.48; coat = 1.0; sss = 0.0; detailMode = 6;
  } else if (cls < 0.985) {
    // Dark smoked reflective glass / visor / optic lens
    rough = 0.045; metal = 0.14; coat = 1.0; sss = 0.0; detailMode = 7;
  } else {
    // Fallback heuristic for unclassed legacy vertex colors (alpha == 1.0)
    float luma = dot(linCol, vec3(0.299, 0.587, 0.114));
    float mx = max(max(linCol.r, linCol.g), linCol.b);
    float mn = min(min(linCol.r, linCol.g), linCol.b);
    float sat = (mx - mn) / max(mx, 1e-4);
    if (luma < 0.025 && linCol.b > linCol.r * 1.35) {
      rough = 0.05; metal = 0.15; coat = 1.0; sss = 0.0; detailMode = 7; // glass
    } else if (luma < 0.045) {
      rough = 0.64; metal = 0.08; coat = 0.06; sss = 0.0; detailMode = 3; // rubber/carbon
    } else if (sat > 0.38 && linCol.r > linCol.g && linCol.g > linCol.b && luma > 0.14 && luma < 0.58) {
      rough = 0.45; metal = 0.0; coat = 0.06; sss = 0.48; detailMode = 1; // skin-like warm tone
    } else if (sat < 0.22 && luma > 0.14) {
      rough = 0.26; metal = 0.82; coat = 0.15; sss = 0.0; detailMode = 5; // metallic
    } else if (sat > 0.45) {
      rough = 0.24; metal = 0.32; coat = 0.65; sss = 0.0; detailMode = 6; // lacquered/painted
    } else {
      rough = 0.52; metal = 0.18; coat = 0.08; sss = 0.0; detailMode = 4; // tactical matte
    }
  }
}

void main() {
  vec3 N = normalize(vN);
  if (!gl_FrontFacing) N = -N;
  vec3 V = normalize(cameraPosition - vWorld);

  #if SURFACE_KIND == 6
  // HDR Emissive Signal role (headlights, taillights, neon armor strips, katana plasma edge, holograms)
  vec3 sig = uL;
  #if defined(USE_COLOR_ALPHA) || defined(USE_COLOR)
    sig = vColorA.rgb;
  #endif
  sig = toLinear(sig * uTint);
  float boost = mix(2.35, 3.65, max(uNight, uAurora));
  float core = 0.78 + 0.35 * pow(max(dot(N, V), 1e-4), 1.5);
  outC = vec4(hazed(sig * boost * core, vWorld), 1.0);
  return;
  #else

  vec3 albedo = toLinear(uL * uTint);
  float rough = uRough;
  float metal = uMetal;
  float coat = uClearcoat;
  float sss = uSSS;
  float ao = 1.0;
  vec3 emissive = vec3(0.0);
  int detailMode = 4;

  // Hit-flash detection (hunt.js sets uTint to [3, 3, 3] on damage)
  float flash = clamp((max(max(uTint.r, uTint.g), uTint.b) - 1.5) * 0.45, 0.0, 1.0);
  vec3 cleanTint = min(uTint, vec3(1.25));

  if (uAlbedo > 0.5) {
    vec4 texel = texture(tAlbedo, vUv);
    if (texel.a < uCutoff) discard;
    albedo = toLinear(texel.rgb * cleanTint);
  }

  #if defined(USE_COLOR_ALPHA) || defined(USE_COLOR)
    vec3 vcLin = toLinear(vColorA.rgb * cleanTint);
    albedo = vcLin;
    #if SURFACE_KIND == 0
      decodeMaterialClass(vColorA.a, vcLin, rough, metal, coat, sss, detailMode);
    #endif
  #endif

  // --- Bespoke AAA Character Shaders for Protagonist (Kael) & Mirror Boss (Ren) ---
  #if SURFACE_KIND == 1
    // 1. Realistic Human Skin + Fitted Tactical Long Sleeves & Fingerless Leather Gloves (MakeHuman male_generic)
    // Discard hidden torso/pelvis/thigh/upper-shoulder skin beneath male_casualsuit06 so zero mesh ever pokes through clothes.
    if (vBind.y > 0.12 && vBind.y < 1.565 && abs(vBind.x) < 0.315) discard;
    float armX = abs(vBind.x);
    bool isSleeve = armX >= 0.315 && armX < 0.67 && vBind.y > 1.30 && vBind.y < 1.64;
    bool isGlove = armX >= 0.67 && armX < 0.80 && vBind.y > 1.30 && vBind.y < 1.64;
    if (isSleeve) {
      // Matte dark tactical compression under-sleeve with woven carbon twill micro-normal
      detailMode = 0;
      vec4 sf = texture(tSurface, vUv * 22.0);
      vec3 sleeveCol = toLinear(uL2 * cleanTint) * (0.86 + 0.26 * sf.a);
      float cuffBand = smoothstep(0.012, 0.002, abs(armX - 0.655));
      albedo = mix(sleeveCol, vec3(0.024, 0.028, 0.036), cuffBand * 0.7);
      rough = 0.74 + (sf.b - 0.5) * 0.12;
      metal = cuffBand * 0.35;
      coat = 0.05;
      sss = 0.0;
      emissive = toLinear(uAccent) * smoothstep(0.005, 0.001, abs(armX - 0.648)) * mix(0.25, 0.85, uNight);
      N = perturbNormal(N, vWorld, vUv * 22.0, sf.rg, 0.24);
    } else if (isGlove) {
      // Dark tactical leather palm/knuckle glove (leaving realistic skin fingertips exposed)
      detailMode = 3;
      vec4 dt = texture(tDetail, vUv * 20.0);
      albedo = vec3(0.028, 0.026, 0.030) * (0.82 + 0.32 * dt.b);
      rough = 0.48 + 0.18 * dt.b;
      metal = 0.08;
      coat = 0.10;
      sss = 0.0;
      N = perturbNormal(N, vWorld, vUv * 20.0, dt.rg, 0.24);
    } else {
      detailMode = 1;
      vec3 baseSkin = toLinear(uL * cleanTint);
      float isFace = smoothstep(1.58, 1.66, vBind.y) * smoothstep(0.0, 0.05, vBind.z);
      vec2 cD = vec2((abs(vBind.x) - 0.042) / 0.028, (vBind.y - 1.705) / 0.024);
      vec2 nD = vec2(vBind.x / 0.015, (vBind.y - 1.712) / 0.022);
      vec2 lD = vec2(vBind.x / 0.023, (vBind.y - 1.674) / 0.0085);
      vec2 eD = vec2((abs(vBind.x) - 0.032) / 0.019, (vBind.y - 1.737) / 0.014);
      float cheeks = exp(-dot(cD, cD));
      float nose = exp(-dot(nD, nD));
      float lips = exp(-dot(lD, lD)) * smoothstep(0.06, 0.085, vBind.z);
      float eyeSocket = exp(-dot(eD, eD));
      float jawShade = smoothstep(1.705, 1.655, vBind.y) * smoothstep(0.03, 0.08, vBind.z) * (1.0 - lips);
      float micro = texture(tSurface, vUv * 26.0).a;
      albedo = baseSkin * (0.94 + 0.12 * micro);
      albedo = mix(albedo, albedo * vec3(1.12, 0.84, 0.78), (cheeks * 0.28 + nose * 0.18) * isFace);
      albedo = mix(albedo, albedo * vec3(0.95, 0.55, 0.52), lips * 0.55 * isFace);
      albedo *= 1.0 - eyeSocket * 0.22 * isFace;
      albedo *= 1.0 - jawShade * 0.08 * isFace;
      rough = mix(0.48, 0.36, (nose + cheeks * 0.5) * isFace) + (micro - 0.5) * 0.08;
      metal = 0.0;
      coat = 0.04 + 0.07 * lips;
      sss = 0.68;
      ao = mix(0.84, 1.0, smoothstep(1.54, 1.64, vBind.y));
      N = perturbNormal(N, vWorld, vUv * 24.0, texture(tSurface, vUv * 24.0).rg, 0.07);
    }
  #elif SURFACE_KIND == 2
    // 2. Realistic Human Eyes (sclera + fibrous iris + dark pupil + limbal ring + wet cornea clearcoat)
    vec2 eyeLocal = vec2(abs(vBind.x) - 0.0315, vBind.y - 1.7355);
    float rEye = length(eyeLocal);
    float front = smoothstep(0.065, 0.082, vBind.z);
    float irisMask = (1.0 - smoothstep(0.0056, 0.0066, rEye)) * front;
    float pupilMask = (1.0 - smoothstep(0.0022, 0.0029, rEye)) * front;
    float limbal = smoothstep(0.0048, 0.0061, rEye) * irisMask;
    float ang = atan(eyeLocal.y, eyeLocal.x);
    float fibers = 0.78 + 0.22 * sin(ang * 24.0 + rEye * 300.0) * cos(ang * 11.0);
    vec3 sclera = vec3(0.66, 0.62, 0.60) * mix(0.75, 1.0, front);
    vec3 irisCol = toLinear(uAccent) * fibers * (1.0 - 0.52 * limbal);
    albedo = mix(sclera, irisCol, irisMask);
    albedo = mix(albedo, vec3(0.004, 0.004, 0.006), pupilMask);
    rough = 0.04;
    metal = 0.0;
    coat = 1.0;
    sss = 0.12;
  #elif SURFACE_KIND == 3
    // 3. Realistic Natural Layered Hair (soft anisotropic sheen, zero plastic glare)
    detailMode = 2;
    float strand = texture(tSurface, vec2(vUv.x * 28.0, vUv.y * 5.0)).b;
    float rootAO = smoothstep(1.62, 1.80, vBind.y);
    albedo = toLinear(uL * cleanTint) * (0.70 + 0.42 * strand);
    rough = 0.48 + 0.12 * (1.0 - strand);
    metal = 0.0;
    coat = 0.04;
    sss = 0.06;
    ao = 0.62 + 0.38 * rootAO;
    N = perturbNormal(N, vWorld, vec2(vUv.x * 22.0, vUv.y * 5.0), texture(tSurface, vec2(vUv.x * 22.0, vUv.y * 5.0)).rg, 0.14);
  #elif SURFACE_KIND == 4
    // 4. Tailored Tactical Cyberpunk Field Jacket & Cargo Trousers (realistic matte weave + hardware)
    detailMode = 0;
    bool jacket = vBind.y >= 0.965;
    bool belt = vBind.y >= 0.925 && vBind.y < 0.975;
    vec3 upperCol = toLinear(uL * cleanTint);
    vec3 lowerCol = toLinear(uL2 * cleanTint);
    vec2 wUv = vUv * 22.0;
    vec4 surf = texture(tSurface, wUv);
    vec4 det = texture(tDetail, vUv * 12.0);
    float seamX = smoothstep(0.010, 0.002, abs(abs(vBind.x) - 0.135));
    float centerZip = smoothstep(0.012, 0.003, abs(vBind.x)) * step(0.02, vBind.z);
    float collar = smoothstep(1.48, 1.54, vBind.y);
    float kneePad = smoothstep(0.41, 0.45, vBind.y) * (1.0 - smoothstep(0.55, 0.59, vBind.y)) * step(0.01, vBind.z);
    float shoulderPanel = smoothstep(1.38, 1.46, vBind.y) * smoothstep(0.09, 0.16, abs(vBind.x));
    float chestYoke = smoothstep(1.34, 1.38, vBind.y) * (1.0 - smoothstep(1.46, 1.50, vBind.y));

    if (belt) {
      bool buckle = abs(vBind.x) < 0.042 && vBind.z > 0.06;
      albedo = buckle ? vec3(0.32, 0.35, 0.40) : vec3(0.022, 0.020, 0.022);
      rough = buckle ? 0.22 : 0.48;
      metal = buckle ? 0.92 : 0.0;
      coat = buckle ? 0.18 : 0.08;
      N = perturbNormal(N, vWorld, vUv * 18.0, det.rg, 0.22);
    } else if (jacket) {
      // Realistic dark technical canvas/leather tactical jacket with tailored shoulder yoke & subtle hardware
      albedo = upperCol * (0.86 + 0.24 * surf.a);
      albedo = mix(albedo, upperCol * 0.58, max(shoulderPanel * 0.72, max(collar * 0.50, chestYoke * 0.35)));
      rough = mix(0.78, 0.44, max(shoulderPanel, collar * 0.6)) + (surf.b - 0.5) * 0.12;
      metal = mix(0.02, 0.38, max(shoulderPanel * 0.55, centerZip * 0.85));
      coat = shoulderPanel * 0.12;
      albedo = mix(albedo, vec3(0.26, 0.29, 0.34), centerZip * 0.78);
      // Subtle understated micro-LED collar badge (no garish bright chest stripes!)
      float badge = smoothstep(0.008, 0.002, abs(vBind.y - 1.40)) * smoothstep(0.032, 0.010, abs(vBind.x + 0.095)) * step(0.06, vBind.z);
      emissive = toLinear(uAccent) * badge * mix(0.45, 1.4, uNight);
      ao = 0.76 + 0.24 * smoothstep(0.96, 1.34, vBind.y) * (1.0 - seamX * 0.22);
      N = perturbNormal(N, vWorld, wUv, surf.rg, 0.26);
    } else {
      albedo = lowerCol * (0.88 + 0.24 * surf.a);
      albedo = mix(albedo, vec3(0.030, 0.034, 0.042), kneePad * 0.78);
      rough = mix(0.84, 0.42, kneePad) + (surf.b - 0.5) * 0.10;
      metal = kneePad * 0.20;
      ao = 0.72 + 0.28 * smoothstep(0.10, 0.45, vBind.y);
      N = perturbNormal(N, vWorld, wUv, surf.rg, 0.28);
    }
  #elif SURFACE_KIND == 5
    // 5. Scuffed Tactical Leather Combat Boots
    detailMode = 3;
    vec4 det = texture(tDetail, vUv * 16.0);
    bool sole = vBind.y < 0.032;
    albedo = sole ? vec3(0.018, 0.020, 0.024) : toLinear(uL * cleanTint) * (0.82 + 0.34 * det.b);
    rough = sole ? 0.88 : (0.44 + 0.22 * det.b);
    metal = 0.04;
    coat = sole ? 0.0 : 0.12;
    ao = 0.68 + 0.32 * smoothstep(0.0, 0.18, vBind.y);
    N = perturbNormal(N, vWorld, vUv * 16.0, det.rg, 0.26);
  #else
    // Generic / Blender street assets: apply class-appropriate micro-normal & roughness breakup
    vec2 mUv = vUv * (detailMode == 5 ? 6.0 : detailMode == 0 ? 12.0 : 8.0);
    if (detailMode == 0) {
      vec4 sf = texture(tSurface, mUv);
      rough = clamp(rough + (sf.a - 0.5) * 0.14, 0.55, 0.96);
      N = perturbNormal(N, vWorld, mUv, sf.rg, 0.20);
    } else if (detailMode == 1) {
      float pore = texture(tSurface, mUv * 2.0).a;
      rough = clamp(rough + (pore - 0.5) * 0.10, 0.32, 0.62);
      N = perturbNormal(N, vWorld, mUv * 2.0, texture(tSurface, mUv * 2.0).rg, 0.07);
    } else if (detailMode == 3) {
      vec4 dt = texture(tDetail, mUv);
      rough = clamp(rough + (dt.b - 0.5) * 0.18, 0.42, 0.92);
      N = perturbNormal(N, vWorld, mUv, dt.rg, 0.22);
    } else if (detailMode == 4) {
      vec4 dt = texture(tDetail, mUv);
      rough = clamp(rough + (dt.b - 0.5) * 0.16, 0.24, 0.78);
      N = perturbNormal(N, vWorld, mUv, dt.rg, 0.15);
    } else if (detailMode == 5) {
      vec4 sf = texture(tSurface, vec2(mUv.x * 0.5, mUv.y * 2.2));
      rough = clamp(rough + (sf.b - 0.5) * 0.14, 0.12, 0.42);
      N = perturbNormal(N, vWorld, vec2(mUv.x * 0.5, mUv.y * 2.2), sf.rg, 0.09);
    } else if (detailMode == 6) {
      // Subtle metallic flake shimmer in automotive clearcoat paint
      float flake = texture(tDetail, vUv * 24.0).a;
      rough = clamp(rough + (flake - 0.5) * 0.04, 0.10, 0.26);
    }
  #endif

  float shadow = woodSunAt(vWorld + N * 0.06);
  vec3 c = pbrShade(vWorld, N, V, albedo, rough, metal, coat, sss, ao, shadow, emissive);

  // Subtle anisotropic Kajiya-Kay hair sheen
  if (detailMode == 2) {
    vec3 T = normalize(cross(N, vec3(1.0, 0.0, 0.0) + vec3(0.0, 0.0, 0.01)));
    vec3 H = normalize(V + uSunDir);
    float tdh = dot(T, H);
    float aniso = pow(sqrt(max(1.0 - tdh * tdh, 1e-4)), 32.0);
    c += keyLightCol() * aniso * 0.26 * (0.25 + 0.75 * shadow) * max(dot(N, uSunDir) * 0.5 + 0.5, 0.0);
  }

  // Hit-flash feedback (white-hot HDR burst on enemy/rival hit)
  if (flash > 0.001) c = mix(c, vec3(3.2, 2.8, 2.4), flash);

  outC = vec4(hazed(c, vWorld), uAlbedo > 0.5 ? texture(tAlbedo, vUv).a : 1.0);
  #endif
}
`;

export function paint(
  shared,
  [L, M, S, A = L],
  {
    skinned = false,
    vertexColors = false,
    lower = null,
    split = -10,
    grain = 2.2,
    map = null,
    tint = [1, 1, 1],
    roughness = 0.48,
    metalness = 0.15,
    clearcoat = 0.0,
    sss = 0.0,
    surfaceKind = 0,
    accent = [0.0, 0.92, 1.0],
  } = {},
) {
  const col = (c) => ({ value: new THREE.Color(...c) });
  const [L2, M2, S2, A2 = L2] = lower ?? [L, M, S, A];
  return new THREE.ShaderMaterial({
    glslVersion: THREE.GLSL3,
    side: THREE.FrontSide,
    vertexColors,
    uniforms: {
      ...shared,
      uL: col(L), uM: col(M), uS: col(S),
      uL2: col(L2), uM2: col(M2), uS2: col(S2),
      uOwn: col(A), uOwn2: col(A2),
      uSplit: { value: split },
      uGrain: { value: grain },
      tAlbedo: { value: map ?? shared.tNoise.value },
      uAlbedo: { value: map ? 1 : 0 },
      uTint: col(tint),
      uCutoff: { value: 0 },
      uRough: { value: roughness },
      uMetal: { value: metalness },
      uClearcoat: { value: clearcoat },
      uSSS: { value: sss },
      uSurfaceKind: { value: surfaceKind },
      uAccent: col(accent),
    },
    vertexShader: skinned ? /* glsl */ `
      #include <common>
      #include <skinning_pars_vertex>
      out vec3 vWorld; out vec3 vN; out vec3 vBind; out vec2 vUv;
      #if defined(USE_COLOR_ALPHA) || defined(USE_COLOR)
      out vec4 vColorA;
      #endif
      void main() {
        #include <beginnormal_vertex>
        #include <skinbase_vertex>
        #include <skinnormal_vertex>
        #include <begin_vertex>
        #include <skinning_vertex>
        vec4 w = modelMatrix * vec4(transformed, 1.0);
        vWorld = w.xyz;
        vN = normalize(mat3(modelMatrix) * objectNormal);
        vBind = position;
        vUv = uv;
        #if defined(USE_COLOR_ALPHA)
        vColorA = color;
        #elif defined(USE_COLOR)
        vColorA = vec4(color, 1.0);
        #endif
        gl_Position = projectionMatrix * viewMatrix * w;
      }` : /* glsl */ `
      out vec3 vWorld; out vec3 vN; out vec3 vBind; out vec2 vUv;
      #if defined(USE_COLOR_ALPHA) || defined(USE_COLOR)
      out vec4 vColorA;
      #endif
      void main() {
        vec3 p = position, nrm = normal;
        #ifdef USE_INSTANCING
        p = (instanceMatrix * vec4(p, 1.0)).xyz;
        nrm = mat3(instanceMatrix) * nrm;
        #endif
        vec4 w = modelMatrix * vec4(p, 1.0);
        vWorld = w.xyz;
        vN = normalize(mat3(modelMatrix) * nrm);
        vBind = position;
        vUv = uv;
        #if defined(USE_COLOR_ALPHA)
        vColorA = color;
        #elif defined(USE_COLOR)
        vColorA = vec4(color, 1.0);
        #endif
        gl_Position = projectionMatrix * viewMatrix * w;
      }`,
    fragmentShader: `#define SURFACE_KIND ${Math.round(surfaceKind)}\n${PBR_FRAG}`,
  });
}

// Realistic PBR palettes for the protagonist (kept exported for night/main.js compatibility)
export const DAY = {
  CLOAK: [[0.14, 0.24, 0.34], [0.09, 0.16, 0.24], [0.04, 0.08, 0.14]],
  HAT: [[0.22, 0.24, 0.28], [0.15, 0.17, 0.20], [0.08, 0.09, 0.12]],
  SHIRT: [[0.17, 0.23, 0.30], [0.12, 0.17, 0.23], [0.06, 0.09, 0.14]],
  TROUSERS: [[0.13, 0.14, 0.17], [0.09, 0.10, 0.12], [0.05, 0.06, 0.08]],
  SKIN: [[0.84, 0.65, 0.54], [0.68, 0.49, 0.40], [0.42, 0.29, 0.25]],
  BOOTS: [[0.14, 0.13, 0.14], [0.09, 0.08, 0.09], [0.04, 0.04, 0.05]],
  HAIR: [[0.10, 0.09, 0.10], [0.06, 0.05, 0.06], [0.03, 0.03, 0.04]],
  EYES: [[0.42, 0.28, 0.14], [0.25, 0.16, 0.09], [0.10, 0.07, 0.05]],
};

export async function createFigure(shared, onProgress, palettes = {}) {
  const man = await loadMan({ base: './figure/', onProgress });
  man.template = clone(man.object);
  const avatar = await attachAvatar(man, shared, { palettes });
  man.appearance = avatar;
  const cloakMesh = new THREE.Group();
  const dabs = new THREE.Group();
  return { man, avatar, cloakMesh, dabs, updateCloak: () => avatar.update() };
}
