// Photorealistic AAA Neo-Kyoto city & alpine surroundings:
// - Skyscrapers with ray-traced 3D parallax interior rooms, Fresnel reflective glass, concrete/metal PBR & HDR neon signage
// - Realistic branching broadleaf trees (botanical alpha-tested leaf-cluster cards) & layered alpine spruce pines
// - Sculpted mossy granite boulders, swaying grass tufts, night rain streaks / sunlit petals
// - Physically-inspired Rayleigh+Mie sky dome with 3D-lit volumetric FBM clouds, cratered moon & crisp stars
import * as THREE from 'three';
import { COMMON, PERTURB_NORMAL, EXTENT } from '../oilpaint/common.js';
import { heightAt, wayDist, clearingAt, forestAt, mountainHeight, rng, way, ROAD } from './land.js';
import { CHAPTERS, chapterAt } from '../game/chapters.js';
import { LEVELS, GOAL } from '../game/levels.js';
import { DUELS } from '../game/duel.js';
import { EXTRA_KEEP } from '../game/story.js';
import { blockLots, AVENUES, STREETS, GRID_MIN, GRID_MAX, ZONES, ZONE_SPLIT, zoneAt } from './grid.js';

// Match the fixture palette in the shader and the street's baked light field.
const lightAccent = (x, z) => (Math.floor(x * 2 + 0.223) + Math.floor(z * 3 + 0.223)) & 1;

function streetLights(lights, signs) {
  const size = 512, data = new Uint8Array(size * size * 4), step = 2 * EXTENT / size;
  const zonePaint = ZONES.map((zone) => CHAPTERS[zone.chapter]);
  function stamp(p, radius, strength, accent) {
    const north = THREE.MathUtils.smoothstep(p.z - ZONE_SPLIT.downtown, -6, 6);
    const market = THREE.MathUtils.smoothstep(p.z - ZONE_SPLIT.market, -6, 6);
    const east = THREE.MathUtils.smoothstep(p.x - ZONE_SPLIT.east, -6, 6);
    const south = (1 - north) * (1 - market);
    const weights = [north, (1 - north) * market, south * east, south * (1 - east)];
    const key = accent ? 'leafLight' : 'foliage';
    const color = [0, 1, 2].map((ch) => zonePaint.reduce((sum, zone, i) => sum + zone[key][ch] * weights[i], 0));
    const minX = Math.max(1, Math.floor((p.x + EXTENT - radius) / step));
    const maxX = Math.min(size - 2, Math.floor((p.x + EXTENT + radius) / step));
    const minZ = Math.max(1, Math.floor((p.z + EXTENT - radius) / step));
    const maxZ = Math.min(size - 2, Math.floor((p.z + EXTENT + radius) / step));
    for (let j = minZ; j <= maxZ; j++) {
      const dz = (j + 0.5) * step - EXTENT - p.z;
      for (let i = minX; i <= maxX; i++) {
        const dx = (i + 0.5) * step - EXTENT - p.x;
        const falloff = Math.max(0, 1 - (dx * dx + dz * dz) / (radius * radius));
        const glow = 255 * strength * falloff * falloff;
        const at = (i + j * size) * 4;
        for (let ch = 0; ch < 3; ch++) data[at + ch] = Math.min(255, data[at + ch] + Math.round(glow * color[ch]));
      }
    }
  }
  for (const p of lights) {
    const h = p.y - heightAt(p.x, p.z);
    if (h > 3 && h < 8.5) stamp(p, 9.6, 0.78, lightAccent(p.x, p.z));
  }
  for (const p of signs) {
    const h = p.y - heightAt(p.x, p.z);
    if (h < 10) stamp(p, 6.8, 0.54, lightAccent(p.x, p.z) ^ 1);
  }
  const map = new THREE.DataTexture(data, size, size);
  map.magFilter = THREE.LinearFilter;
  map.minFilter = THREE.LinearMipmapLinearFilter;
  map.generateMipmaps = true;
  map.needsUpdate = true;
  return map;
}

// Merge plain indexed/non-indexed BufferGeometries into one instancing-ready BufferGeometry (position, normal, uv).
function mergeParts(parts) {
  let totalVerts = 0;
  const nonIndexed = parts.map((g) => {
    const out = g.index ? g.toNonIndexed() : g.clone();
    if (!out.getAttribute('normal')) out.computeVertexNormals();
    if (!out.getAttribute('uv')) {
      const count = out.getAttribute('position').count;
      out.setAttribute('uv', new THREE.Float32BufferAttribute(new Float32Array(count * 2), 2));
    }
    totalVerts += out.getAttribute('position').count;
    return out;
  });
  const pos = new Float32Array(totalVerts * 3);
  const nrm = new Float32Array(totalVerts * 3);
  const uv = new Float32Array(totalVerts * 2);
  let offset = 0;
  for (const g of nonIndexed) {
    const p = g.getAttribute('position').array;
    const n = g.getAttribute('normal').array;
    const u = g.getAttribute('uv').array;
    pos.set(p, offset * 3);
    nrm.set(n, offset * 3);
    uv.set(u, offset * 2);
    offset += g.getAttribute('position').count;
    g.dispose();
  }
  const merged = new THREE.BufferGeometry();
  merged.setAttribute('position', new THREE.BufferAttribute(pos, 3));
  merged.setAttribute('normal', new THREE.BufferAttribute(nrm, 3));
  merged.setAttribute('uv', new THREE.BufferAttribute(uv, 2));
  return merged;
}

// Organically tapered trunk with root flare + 4 diverging upper scaffold limbs (unit height 0..1).
function buildBroadleafTrunkGeo() {
  const parts = [];
  const main = new THREE.CylinderGeometry(0.42, 1.15, 0.72, 10, 4);
  main.translate(0, 0.34, 0);
  const mp = main.attributes.position;
  for (let i = 0; i < mp.count; i++) {
    const x = mp.getX(i), y = mp.getY(i), z = mp.getZ(i);
    const flare = y < 0.14 ? 1 + 0.35 * Math.pow(1 - y / 0.14, 2) * (0.7 + 0.3 * Math.cos(Math.atan2(z, x) * 5)) : 1;
    const bend = Math.sin(y * 4.2) * 0.12;
    mp.setXYZ(i, x * flare + bend, y, z * flare + Math.cos(y * 3.1) * 0.08);
  }
  main.computeVertexNormals();
  parts.push(main);
  for (let b = 0; b < 4; b++) {
    const a = b * (Math.PI * 0.5) + 0.45 + (b % 2) * 0.22;
    const len = 0.44 + (b % 2) * 0.08;
    const limb = new THREE.CylinderGeometry(0.16, 0.36, len, 7, 1);
    limb.translate(0, len * 0.5, 0);
    limb.rotateZ(0.58 + (b % 2) * 0.14);
    limb.rotateY(a);
    limb.translate(Math.cos(a) * 0.12, 0.56 + (b % 2) * 0.06, Math.sin(a) * 0.12);
    parts.push(limb);
  }
  return mergeParts(parts);
}

// Lush 14-card airy botanical canopy dome (outward crown normals for realistic volumetric sunlight & translucency).
function buildBroadleafCanopyGeo() {
  const parts = [];
  const rand = rng(4409);
  const clusters = [
    [0, 0.34, 0, 1.12],
    [0.54, 0.12, 0.24, 0.96], [-0.52, 0.16, 0.28, 0.94],
    [0.18, 0.10, -0.56, 0.96], [-0.36, 0.06, -0.44, 0.92],
    [0.42, -0.18, -0.34, 0.88], [-0.46, -0.16, -0.18, 0.86],
    [0.06, -0.14, 0.54, 0.92], [-0.14, 0.38, -0.22, 0.88],
    [0.26, 0.34, 0.22, 0.86], [0.62, -0.06, -0.04, 0.82],
    [-0.60, -0.04, 0.08, 0.82], [0.0, -0.26, -0.12, 0.88],
    [0.12, 0.14, 0.14, 1.05],
  ];
  for (let i = 0; i < clusters.length; i++) {
    const [cx, cy, cz, sc] = clusters[i];
    const card = new THREE.PlaneGeometry(sc * 1.38, sc * 1.38, 2, 2);
    const p = card.attributes.position;
    // Slight organic cup curvature on each foliage card
    for (let k = 0; k < p.count; k++) {
      const px = p.getX(k), py = p.getY(k);
      p.setZ(k, -(px * px + py * py) * 0.22);
    }
    card.rotateX((rand() - 0.5) * 1.45);
    card.rotateY(i * 1.12 + rand() * 0.6);
    card.rotateZ((rand() - 0.5) * 0.85);
    card.translate(cx, cy, cz);
    // Outward spherical crown normals make multi-card foliage shade smoothly like a dense organic canopy
    const nrm = card.attributes.normal;
    for (let k = 0; k < p.count; k++) {
      const vx = p.getX(k), vy = p.getY(k) + 0.15, vz = p.getZ(k);
      const len = Math.hypot(vx, vy, vz) || 1;
      nrm.setXYZ(k, vx / len, Math.max(-0.25, vy / len), vz / len);
    }
    parts.push(card);
  }
  return mergeParts(parts);
}

// Layered alpine spruce bough whorls (5 tiers x 6 drooping needle fronds -> natural evergreen spire).
function buildPineBoughsGeo() {
  const parts = [];
  const tiers = 5;
  for (let t = 0; t < tiers; t++) {
    const frac = t / (tiers - 1); // 0 bottom .. 1 top
    const y = -0.44 + frac * 0.88;
    const rad = 1.05 * (1 - frac * 0.68);
    const boughs = t === tiers - 1 ? 5 : 6;
    for (let b = 0; b < boughs; b++) {
      const a = (b / boughs) * Math.PI * 2 + t * 0.55;
      const w = rad * 1.02;
      const h = rad * 1.24;
      const card = new THREE.PlaneGeometry(w, h, 1, 2);
      const p = card.attributes.position;
      // Droop outward bough tips gracefully downward (+Z maps to -Y after rotateX > PI/2)
      for (let k = 0; k < p.count; k++) {
        const py = p.getY(k);
        const u = (py / h) + 0.5; // 0 base .. 1 tip
        p.setZ(k, u * u * 0.26 * rad);
      }
      card.translate(0, h * 0.44, 0);
      // Lower boughs sweep outward & slightly down (112 deg), upper crown arches outward (~95 deg)
      card.rotateX(Math.PI * 0.63 - frac * 0.12);
      card.rotateY(a);
      card.translate(0, y, 0);
      // Outward + upward conical normals for natural moonlight/sunlight falloff
      const nrm = card.attributes.normal;
      for (let k = 0; k < p.count; k++) {
        const vx = p.getX(k), vz = p.getZ(k);
        const r = Math.hypot(vx, vz) || 1;
        nrm.setXYZ(k, (vx / r) * 0.72, 0.69, (vz / r) * 0.72);
      }
      parts.push(card);
    }
  }
  // Tight pointed evergreen apex fronds so the top finishes into a natural conifer spire
  for (let s = 0; s < 3; s++) {
    const tip = new THREE.PlaneGeometry(0.28, 0.30, 1, 1);
    tip.rotateY((s / 3) * Math.PI);
    tip.translate(0, 0.42, 0);
    parts.push(tip);
  }
  return mergeParts(parts);
}

// Organically sculpted displaced granite boulder (320 smooth-faceted tris).
function buildBoulderGeo() {
  const geo = new THREE.IcosahedronGeometry(1, 2);
  const p = geo.attributes.position;
  for (let i = 0; i < p.count; i++) {
    const x = p.getX(i), y = p.getY(i), z = p.getZ(i);
    const disp = 0.84 + 0.16 * Math.sin(x * 3.7 + y * 2.3) * Math.cos(z * 3.1 - x * 1.7)
      + 0.08 * Math.sin(x * 7.1 - z * 6.3 + y * 5.2);
    const flatBase = y < -0.25 ? 0.72 : 1.0;
    p.setXYZ(i, x * disp, y * disp * flatBase, z * disp);
  }
  geo.computeVertexNormals();
  return geo;
}

// Roles: 0 tower (with 3D parallax interiors), 1 neon sign, 2 park grass, 3 crate, 4 lamp head / lane stud,
//        6 pole/spire, 7 bark trunk, 8 broadleaf foliage cards, 9 boulder, 10 alpine pine needle cards.
function pbrCityMaterial(shared, role, sway = 0) {
  const alphaCut = role === 8 || role === 10;
  const doubleSided = role === 2 || alphaCut;
  return new THREE.ShaderMaterial({
    glslVersion: THREE.GLSL3,
    side: doubleSided ? THREE.DoubleSide : THREE.FrontSide,
    uniforms: { ...shared, uRole: { value: role }, uSway: { value: sway } },
    vertexShader: /* glsl */ `
      ${COMMON}
      uniform float uSway;
      out vec3 vWorld; out vec3 vNormal; flat out float vSeed; out vec3 vObj; out vec3 vObjN; flat out vec3 vSize;
      flat out float vAccent; out vec2 vUv;
      void main() {
        vec4 w = instanceMatrix * vec4(position, 1.0);
        vec2 origin = instanceMatrix[3].xz;
        vSeed = fract(sin(origin.x * 17.3 + origin.y * 31.7) * 43758.5);
        vAccent = mod(floor(origin.x * 2.0 + 0.223) + floor(origin.y * 3.0 + 0.223), 2.0);
        vSize = vec3(length(instanceMatrix[0].xyz), length(instanceMatrix[1].xyz), length(instanceMatrix[2].xyz));
        vObj = position * vSize; vObjN = normal; vUv = uv;
        if (uSway > 0.0) {
          float wave = sin(uTime * 1.9 + w.x * 0.32 + w.z * 0.29 + vSeed * 6.28);
          float lift = max(position.y + 0.45, 0.0);
          w.x += wave * lift * uSway;
          w.z += cos(uTime * 1.5 + w.z * 0.26) * lift * uSway * 0.55;
        }
        vWorld = (modelMatrix * w).xyz;
        vNormal = normalize(mat3(modelMatrix) * mat3(instanceMatrix) * normal);
        gl_Position = projectionMatrix * viewMatrix * vec4(vWorld, 1.0);
      }`,
    fragmentShader: /* glsl */ `
      #define CITY_ROLE ${role}
      ${COMMON}
      ${role === 0 || role === 3 || role === 7 || role === 9 ? PERTURB_NORMAL : ''}
      uniform float uRole;
      in vec3 vWorld; in vec3 vNormal; flat in float vSeed; in vec3 vObj; in vec3 vObjN; flat in vec3 vSize;
      flat in float vAccent; in vec2 vUv;
      layout(location = 0) out vec4 outC;
      float hash(vec2 p) { return hash4(p, 0.0).x; }

      #if CITY_ROLE == 0
      // Ray-traced 3D parallax interior room behind glass panes (AAA architectural depth).
      vec3 parallaxRoom(vec2 cellF, vec3 viewObj, vec3 objN, float roomSeed, float litChance, vec3 fol, vec3 pale) {
        vec2 uv = cellF * 2.0 - 1.0; // -1..1 on pane
        // Build face-local view vector: (u along face, v up, w into room)
        float isX = step(0.5, abs(objN.x));
        float sgn = isX > 0.5 ? sign(objN.x) : sign(objN.z);
        vec3 ray = isX > 0.5
          ? vec3(-viewObj.z, -viewObj.y, -viewObj.x * sgn)
          : vec3(-viewObj.x, -viewObj.y, -viewObj.z * sgn);
        ray = normalize(ray + vec3(1e-4));
        float depth = 1.25;
        vec3 ro = vec3(uv.x, uv.y, 0.0);
        // Ray-box intersection against room walls x=+-1, y=+-1, back z=-depth
        vec3 invR = 1.0 / (ray + sign(ray + 1e-5) * 1e-4);
        vec3 tMin = (vec3(-1.0, -1.0, -depth) - ro) * invR;
        vec3 tMax = (vec3( 1.0,  1.0,  0.0) - ro) * invR;
        float tx = max(tMin.x, tMax.x);
        float ty = max(tMin.y, tMax.y);
        float tz = max(tMin.z, tMax.z);
        float tHit = min(min(tx, ty), tz);
        vec3 hit = ro + ray * tHit;

        // Warm office/apartment palette + occasional cool cyan/magenta Neo-Kyoto accent room
        vec3 warmWall = mix(vec3(0.95, 0.72, 0.42), vec3(0.85, 0.80, 0.72), fract(roomSeed * 7.3));
        vec3 roomTint = mix(warmWall, mix(pale, fol, step(0.5, fract(roomSeed * 3.1))), step(0.84, roomSeed));
        vec3 col;
        if (abs(tHit - tz) < 0.002) {
          // Back wall with subtle furniture/shelf silhouette band
          float shelf = smoothstep(-0.35, -0.25, hit.y) * (1.0 - smoothstep(0.15, 0.25, hit.y));
          col = roomTint * (0.55 + 0.30 * (hit.y * 0.5 + 0.5)) * mix(1.0, 0.68, shelf * step(0.45, fract(roomSeed * 5.7)));
        } else if (abs(tHit - ty) < 0.002) {
          if (hit.y > 0.0) {
            // Ceiling with luminous recessed LED panel
            float fixture = step(abs(hit.x), 0.55) * step(abs(hit.z + depth * 0.5), 0.35);
            col = mix(roomTint * 0.72, roomTint * 2.1 + vec3(0.5), fixture);
          } else {
            // Dark timber/slate floor
            col = roomTint * 0.26;
          }
        } else {
          // Side walls catching warm bounce
          col = roomTint * (0.42 + 0.25 * (hit.y * 0.5 + 0.5));
        }
        float depthAO = clamp(1.0 + hit.z * 0.32, 0.48, 1.0);
        return col * depthAO * litChance;
      }
      #endif

      void main() {
        #if CITY_ROLE == 0 || CITY_ROLE == 1 || CITY_ROLE == 4
        vec3 fol = toLinear(zoneFoliageAt(vWorld.xz));
        vec3 pale = toLinear(zoneLeafAt(vWorld.xz));
        #endif
        vec3 N = normalize(vNormal);
        if (!gl_FrontFacing) N = -N;
        vec3 V = normalize(cameraPosition - vWorld);

        vec3 albedo = vec3(0.16, 0.17, 0.20);
        float rough = 0.65;
        float metal = 0.08;
        float coat = 0.0;
        float sss = 0.0;
        float ao = 1.0;
        vec3 emissive = vec3(0.0);
        float nightOn = smoothstep(0.20, 0.76, uNight);

        #if CITY_ROLE == 0
          // --- 0. AAA Architectural Skyscrapers with 3D Parallax Interior Windows & PBR Cladding ---
          float side = step(abs(vObjN.y), 0.5);
          float isBldg = step(2.8, min(vSize.x, vSize.z)) * step(6.5, vSize.y);
          float u = abs(vObjN.x) > 0.5 ? vObj.z : vObj.x;
          float fw = abs(vObjN.x) > 0.5 ? vSize.z : vSize.x;
          float corner = step(fw * 0.5 - 0.26, abs(u)) * side;
          float ag = vObj.y + vSize.y * 0.5 - 3.0; // metres above pavement (SINK = 3)
          float bw = mix(1.12, 1.48, step(0.5, fract(vSeed * 7.3)));
          vec2 cell = vec2(u / bw, ag / 1.76), f = fract(cell);
          vec2 aa = min(fwidth(cell) * 0.5 + 0.005, vec2(0.25));
          float detail = 1.0 - smoothstep(0.12, 0.25, max(aa.x, aa.y));

          // Concrete / architectural stone / brushed steel cladding palette per tower
          vec4 wallDet = texture(tDetail, vec2(u, ag) * 0.28);
          vec4 wallSurf = texture(tSurface, vec2(u, ag) * 0.35);
          vec3 concrete = mix(vec3(0.14, 0.15, 0.17), vec3(0.32, 0.30, 0.28), vSeed);
          vec3 darkSteel = mix(vec3(0.06, 0.07, 0.09), vec3(0.14, 0.16, 0.20), fract(vSeed * 3.7));
          bool glassTower = fract(vSeed * 5.1) > 0.52;
          albedo = (glassTower ? darkSteel : concrete) * (0.86 + 0.24 * wallDet.b);
          rough = glassTower ? (0.36 + 0.16 * wallSurf.b) : (0.76 + 0.14 * wallDet.b);
          metal = glassTower ? 0.65 : 0.06;
          // Street-level damp weathering & ambient occlusion near pavement
          ao = 0.72 + 0.28 * smoothstep(0.0, 4.5, ag);
          N = perturbNormal(N, vWorld, vec2(u, ag) * 0.28, wallDet.rg, glassTower ? 0.08 : 0.22);

          float mech1 = step(16.0, vSize.y) * step(abs(ag - 10.6), 0.82);
          float mech2 = step(27.0, vSize.y) * step(abs(ag - 21.2), 0.82);
          float mech = clamp(mech1 + mech2, 0.0, 1.0) * step(ag, vSize.y - 5.2) * side * isBldg;
          float podium = step(ag, 3.35) * side * isBldg;
          float pier = step(0.84, abs(fract(u / (bw * 3.0)) - 0.5) * 2.0) * side * isBldg;
          float mullion = smoothstep(0.41 - aa.x, 0.41 + aa.x, abs(f.x - 0.5)) * (1.0 - pier) * side * isBldg;
          float spandrel = smoothstep(0.80 - aa.y, 0.80 + aa.y, f.y) * side * isBldg;
          albedo *= mix(1.0, 0.74, pier);
          albedo = mix(albedo, vec3(0.08, 0.09, 0.11), max(mullion, spandrel) * (1.0 - podium) * 0.65);
          metal = mix(metal, 0.82, max(mullion, spandrel) * (1.0 - podium) * 0.6);
          rough = mix(rough, 0.26, max(mullion, spandrel) * (1.0 - podium) * 0.6);

          // Window panes: Fresnel mirror glass + ray-traced 3D parallax interior rooms!
          vec2 inset = vec2(0.37, 0.31);
          vec2 glass = 1.0 - smoothstep(inset - aa, inset + aa, abs(f - 0.5));
          float pane = glass.x * glass.y * side * isBldg * detail * (1.0 - mech) * (1.0 - podium) * (1.0 - pier * 0.85) * (1.0 - corner);
          if (pane > 0.01) {
            vec2 cid = floor(cell);
            float h = hash(cid + vSeed * 91.7);
            float h2 = hash(cid * 1.73 + vSeed * 37.1);
            float blind = 1.0;
            if (h2 < 0.32) blind = mix(1.0, 0.28, step(0.38 + 0.34 * h, f.y));
            else if (h2 > 0.72) blind = mix(0.24, 1.0, step(abs(f.x - 0.5), 0.18 + 0.14 * h));
            float litOn = step(0.48, h) * nightOn;
            // Transform view direction into object space via inverse Y-rotation approximation from world vs obj normals
            vec3 viewToCam = V;
            vec3 room = parallaxRoom(f, viewToCam, vObjN, h, litOn * blind, fol, pale);
            // Architectural tinted glazing: crisp sky-reflective blue-steel tint by day, deep mirror obsidian by night
            vec3 glassTint = mix(vec3(0.08, 0.13, 0.19), vec3(0.016, 0.024, 0.036), nightOn);
            albedo = mix(albedo, glassTint, pane);
            rough = mix(rough, mix(0.07, 0.045, nightOn), pane);
            metal = mix(metal, mix(0.36, 0.14, nightOn), pane);
            coat = mix(coat, 1.0, pane);
            // Smooth glass normal (override wall concrete bump inside glass pane)
            N = normalize(mix(N, normalize(vNormal), pane));
            emissive += room * pane * mix(0.04, 1.75, nightOn);
          }

          // Mechanical louvered floor band + thin HDR neon trim (dusk/night only)
          float neonOn = smoothstep(0.20, 0.68, uNight);
          if (mech > 0.5) {
            float louver = step(0.45, fract(ag * 3.8));
            albedo = vec3(0.06, 0.07, 0.085) * mix(0.65, 1.0, louver);
            rough = 0.38; metal = 0.78;
            float mechPip = step(0.72, abs(ag - (mech1 > 0.5 ? 10.6 : 21.2)));
            emissive += fol * mechPip * neonOn * 2.1;
          }

          // Roof parapet & gravel membrane
          if (vObjN.y > 0.5 && isBldg > 0.5) {
            float parapet = max(step(vSize.x * 0.5 - 0.34, abs(vObj.x)), step(vSize.z * 0.5 - 0.34, abs(vObj.z)));
            albedo = mix(vec3(0.11, 0.115, 0.125) * (0.85 + 0.3 * wallDet.b), albedo * 1.05, parapet);
            rough = 0.85; metal = 0.05;
          }

          // Architectural cornice LED accent line (night/twilight only — clean metal trim by day)
          float trim = step(vSize.y * 0.5 - 0.28, vObj.y) * side;
          emissive += fol * trim * neonOn * 2.2;

          // Corner piers & warm depth-lit ground-floor Neo-Kyoto retail arcades
          albedo *= mix(1.0, 0.55, corner);
          if (podium > 0.5 && corner < 0.5) {
            float bayU = fract(u / 2.35);
            float bayId = hash(vec2(floor(u / 2.35), floor(vSeed * 43.0)));
            float shopWin = step(0.22, ag) * step(ag, 2.66) * step(0.10, bayU) * step(bayU, 0.90);
            float shopMul = max(step(0.46, abs(fract(bayU * 3.0) - 0.5)), step(abs(ag - 2.08), 0.04));
            float fascia = step(2.80, ag) * step(ag, 3.16) * step(0.05, bayU) * step(bayU, 0.95);
            if (shopWin > 0.5) {
              if (shopMul > 0.5) {
                albedo = vec3(0.05, 0.055, 0.068);
                rough = 0.32; metal = 0.82; coat = 0.0;
              } else {
                vec3 shopRoom = parallaxRoom(vec2(bayU, (ag - 0.22) / 2.44), V, vObjN, bayId, 1.0, fol, pale);
                albedo = mix(vec3(0.06, 0.09, 0.13), vec3(0.018, 0.022, 0.030), nightOn);
                rough = 0.06; metal = mix(0.28, 0.12, nightOn); coat = 1.0;
                N = normalize(vNormal);
                emissive += shopRoom * mix(0.14, 1.55, nightOn);
              }
            }
            if (fascia > 0.5) {
              vec3 fCol = mix(fol, pale, step(0.45, bayId));
              albedo = mix(fCol * 0.45, vec3(0.04, 0.045, 0.055), neonOn);
              emissive += fCol * mix(0.04, 2.25, neonOn);
            }
          }
        #elif CITY_ROLE == 1
          // --- 1. HDR Neon Glass-Tube Signboards with Brushed Aluminum Frame ---
          vec3 colA = mix(pale, fol, vAccent);
          vec3 colB = mix(fol, pale, vAccent);
          albedo = vec3(0.04, 0.045, 0.058);
          rough = 0.30; metal = 0.80;
          if (abs(vObjN.x) > 0.5) {
            float dx = vSize.z * 0.5 - abs(vObj.z);
            float dy = vSize.y * 0.5 - abs(vObj.y);
            float dEdge = min(dx, dy);
            float border = step(0.032, dEdge) * step(dEdge, 0.092);
            float inner = step(0.14, dx) * step(0.15, dy);
            bool vert = vSize.y >= vSize.z;
            float cellSpan = vert ? max(0.52, vSize.z - 0.30) : max(0.52, vSize.y - 0.32);
            vec2 gCoord = vert
              ? vec2((vObj.z / max(vSize.z - 0.28, 0.2)) + 0.5, (vObj.y + vSize.y * 0.5 - 0.15) / (cellSpan * 0.92))
              : vec2((vObj.z + vSize.z * 0.5 - 0.15) / (cellSpan * 0.95), (vObj.y / max(vSize.y - 0.30, 0.2)) + 0.5);
            float cid = floor(vert ? gCoord.y : gCoord.x);
            vec2 cf = vec2(vert ? gCoord.x : fract(gCoord.x), vert ? fract(gCoord.y) : gCoord.y);
            float inBox = inner * step(0.12, cf.x) * step(cf.x, 0.88) * step(0.14, cf.y) * step(cf.y, 0.86);
            vec2 guv = clamp((cf - vec2(0.12, 0.14)) / vec2(0.76, 0.72), 0.0, 1.0);
            float gh = hash(vec2(cid, floor(vSeed * 67.0)));
            float gh2 = fract(gh * 13.7 + 0.19);
            float sTop = step(0.78, guv.y) * step(0.06, guv.x) * step(guv.x, 0.94);
            float sMid = step(abs(guv.y - 0.50), 0.095) * step(0.12, guv.x) * step(guv.x, 0.88) * step(0.25, gh);
            float sBot = step(guv.y, 0.20) * step(0.10, guv.x) * step(guv.x, 0.90) * step(gh, 0.72);
            float sStem = step(abs(guv.x - mix(0.50, 0.34, step(0.65, gh2))), 0.10) * step(0.06, guv.y) * step(guv.y, 0.92);
            float sSide = step(abs(abs(guv.x - 0.50) - 0.33), 0.085) * step(0.22, guv.y) * step(guv.y, 0.82) * step(0.38, gh2);
            float sDiag = step(abs((guv.x - 0.18) - (guv.y - 0.14) * 0.82), 0.09) * step(0.14, guv.y) * step(guv.y, 0.76) * step(gh2, 0.48);
            float glyph = inBox * clamp(sTop + sMid + sBot + sStem + sSide + sDiag, 0.0, 1.0);
            float flick = 0.92 + 0.08 * step(0.06, fract(sin(floor(uTime * 8.0) + vSeed * 40.0) * 91.3));
            vec3 glyphCol = mix(colA, colB, step(0.75, fract(gh * 5.3)) * inBox * step(0.72, guv.y));
            float litMask = clamp(border + glyph, 0.0, 1.0);
            albedo = mix(vec3(0.03, 0.035, 0.048), glyphCol * 0.45, litMask);
            coat = 0.85; rough = 0.12;
            emissive = mix(colA * 0.06, mix(glyphCol, colB, border) * mix(0.65, 3.6, nightOn), litMask) * flick;
          } else {
            emissive = colB * step(abs(vObj.x), 0.025) * mix(0.2, 2.2, nightOn);
          }
        #elif CITY_ROLE == 2
          // --- 2. Realistic Park Grass Tufts ---
          float tip = clamp(vObj.y / max(vSize.y * 0.45, 0.1), 0.0, 1.0);
          albedo = mix(vec3(0.035, 0.085, 0.022), vec3(0.12, 0.24, 0.048), tip * (0.65 + 0.35 * vSeed));
          rough = 0.74; metal = 0.0; sss = 0.55; ao = 0.45 + 0.55 * tip;
        #elif CITY_ROLE == 3
          // --- 3. Industrial Concrete / Steel Utility Crates ---
          vec4 dt = texture(tDetail, (vObj.xy + vObj.z) * 0.65);
          albedo = mix(vec3(0.12, 0.13, 0.15), vec3(0.22, 0.21, 0.20), vSeed) * (0.82 + 0.32 * dt.b);
          rough = 0.52; metal = 0.45;
          N = perturbNormal(N, vWorld, (vObj.xy + vObj.z) * 0.65, dt.rg, 0.20);
          float hazard = step(abs(vObj.y - vSize.y * 0.12), vSize.y * 0.07) * step(0.5, fract((vObj.x + vObj.z) * 2.2));
          emissive = vec3(1.0, 0.76, 0.05) * hazard * mix(0.35, 2.4, uNight);
        #elif CITY_ROLE == 4
          // --- 4. Streetlamp LED Heads & Illuminated Road Lane Studs ---
          albedo = vec3(0.08, 0.09, 0.11);
          rough = 0.22; metal = 0.78;
          vec3 lampCol = mix(fol, pale, vAccent);
          emissive = lampCol * mix(0.45, 4.2, smoothstep(0.16, 0.68, uNight));
        #elif CITY_ROLE == 6
          // --- 6. Galvanized Steel Streetlamp Poles & Rooftop Comms Spires ---
          vec4 sf = texture(tSurface, vObj.xy * 2.0);
          albedo = vec3(0.14, 0.15, 0.18) * (0.85 + 0.25 * sf.b);
          rough = 0.28; metal = 0.88;
        #elif CITY_ROLE == 7
          // --- 7. Realistic Rough Tree Bark (Furrowed Cedar / Maple Trunk & Limbs) ---
          vec2 barkUv = vec2(vUv.x * 3.0, vUv.y * 5.0);
          vec4 dt = texture(tDetail, barkUv);
          albedo = mix(vec3(0.075, 0.052, 0.036), vec3(0.135, 0.096, 0.068), vSeed) * (0.70 + 0.55 * dt.b);
          rough = 0.88; metal = 0.0;
          ao = 0.60 + 0.40 * smoothstep(0.0, 0.55, vUv.y);
          N = perturbNormal(N, vWorld, barkUv, dt.rg, 0.45);
        #elif CITY_ROLE == 8
          // --- 8. Realistic Botanical Broadleaf Foliage Cards (Japanese Maple / Sakura / Zelkova) ---
          vec4 card = texture(tLeafCard, vUv);
          if (card.a < 0.38) discard;
          vec3 greenLeaf = mix(vec3(0.048, 0.145, 0.032), vec3(0.115, 0.245, 0.045), vSeed);
          vec3 sakuraLeaf = vec3(0.68, 0.26, 0.36);
          vec3 autumnGold = vec3(0.62, 0.34, 0.05);
          vec3 leafBase = vSeed > 0.82 ? mix(sakuraLeaf, autumnGold, step(0.91, vSeed)) : greenLeaf;
          vec3 twigCol = vec3(0.07, 0.05, 0.035);
          albedo = mix(leafBase * (0.58 + 0.72 * card.r), twigCol, card.b);
          rough = mix(0.46, 0.88, card.b);
          metal = 0.0;
          sss = (1.0 - card.b) * card.g * 0.78;
          // Natural crown interior AO: leaves lower & deeper inside the canopy sit in deeper shade
          float crownAO = clamp(0.42 + 0.58 * smoothstep(-0.65, 0.65, vObjN.y * 0.6 + length(vObj / max(vSize, vec3(0.1))) * 0.5), 0.32, 1.0);
          ao = crownAO;
        #elif CITY_ROLE == 9
          // --- 9. Sculpted Mossy Granite Boulders ---
          vec4 dt = texture(tDetail, (vObj.xz + vObj.y) * 0.55);
          vec3 rock = mix(vec3(0.12, 0.125, 0.135), vec3(0.22, 0.21, 0.20), vSeed) * (0.72 + 0.48 * dt.b);
          float moss = smoothstep(0.55, 0.85, N.y) * smoothstep(0.35, 0.75, dt.a);
          albedo = mix(rock, vec3(0.065, 0.125, 0.042), moss * 0.72);
          rough = mix(0.76, 0.90, moss);
          metal = 0.0;
          N = perturbNormal(N, vWorld, (vObj.xz + vObj.y) * 0.55, dt.rg, 0.42);
        #else
          // --- 10. Realistic Alpine Conifer / Spruce Needle Fronds ---
          vec4 card = texture(tPineCard, vUv);
          if (card.a < 0.34) discard;
          vec3 needle = mix(vec3(0.018, 0.088, 0.028), vec3(0.044, 0.155, 0.042), vSeed) * (0.62 + 0.68 * card.r);
          vec3 twigCol = vec3(0.065, 0.048, 0.034);
          albedo = mix(needle, twigCol, card.b);
          rough = mix(0.72, 0.90, card.b);
          metal = 0.0;
          sss = (1.0 - card.b) * card.g * 0.32;
          ao = 0.46 + 0.54 * smoothstep(-0.5, 0.55, vObj.y / max(vSize.y, 0.1));
        #endif

        float shadow = woodSunAt(vWorld + N * 0.14);
        vec3 col = pbrShade(vWorld, N, V, albedo, rough, metal, coat, sss, ao, shadow, emissive);
        outC = vec4(hazed(col, vWorld), 1.0);
      }`,
  });
}

export function createAnimeWorld(shared) {
  ZONES.forEach((zone, i) => {
    const c = CHAPTERS[zone.chapter];
    shared.uZoneGrass.value[i].set(...c.day.grass);
    shared.uZoneFoliage.value[i].set(...c.foliage);
    shared.uZoneLeaf.value[i].set(...c.leafLight);
    shared.uZonePath.value[i].set(...c.day.path);
  });
  shared.uZoneSplit.value.set(ZONE_SPLIT.downtown, ZONE_SPLIT.market, ZONE_SPLIT.east);
  const rand = rng(71029), group = new THREE.Group(), shade = new THREE.Group(), colliders = [];
  const dummy = new THREE.Object3D();
  const record = (x, y, z, sx, sy, sz, ry = 0, rz = 0) => ({ x, y, z, sx, sy, sz, ry, rz });
  function instances(geometry, material, items, parent = group) {
    const mesh = new THREE.InstancedMesh(geometry, material, items.length);
    items.forEach((p, i) => {
      dummy.position.set(p.x, p.y, p.z);
      dummy.scale.set(p.sx, p.sy, p.sz);
      dummy.rotation.set(0, p.ry, p.rz);
      dummy.updateMatrix();
      mesh.setMatrixAt(i, dummy.matrix);
    });
    mesh.instanceMatrix.needsUpdate = true;
    mesh.computeBoundingSphere();
    mesh.frustumCulled = parent === group;
    parent.add(mesh);
    return mesh;
  }
  function scatter(geometry, material, items) {
    const bins = [[], [], [], []];
    for (const p of items) bins[zoneAt(p.x, p.z)].push(p);
    return bins.filter((bin) => bin.length).map((bin) => {
      const mesh = instances(geometry, material, bin);
      mesh.userData.full = bin.length;
      return mesh;
    });
  }

  const keep = [GOAL, ...DUELS, ...EXTRA_KEEP, ...LEVELS.flatMap((L) => [L.checkpoint, ...L.spots, ...(L.gate ? [L.gate] : [])])];
  const SINK = 3;
  const towers = [], signs = [], poles = [], lights = [];
  const crownRand = rng(91837);
  const capH = [28, 16, 18, 9];
  for (const lot of blockLots()) {
    const x = lot.x + (rand() - 0.5) * 2, z = lot.z + (rand() - 0.5) * 2;
    const w = 4.2 + rand() * 2.4, d = 4.2 + rand() * 2.4, reach = Math.max(w, d) * 0.5;
    if (forestAt(x, z) > 0.25 || mountainHeight(x, z) > 0.25) continue;
    if (wayDist(x, z) < ROAD + 3.2 + reach || clearingAt(x, z) > 0.35) continue;
    if (keep.some((p) => Math.hypot(p.x - x, p.z - z) < 16 + reach)) continue;
    if (LEVELS.some((L) => L.gate && Math.abs(L.gate.z - z) < reach + 4)) continue;
    const y = heightAt(x, z);
    let h = capH[lot.zone] * (0.5 + rand() * 0.55);
    if (lot.heart) h *= 1.35;
    const ry = (rand() - 0.5) * 0.1;
    towers.push({ ...record(x, y + (h - SINK) / 2, z, w, h + SINK, d, ry), wd: wayDist(x, z), zone: lot.zone });
    colliders.push({ x, y: y + (h - SINK) / 2, z, box: [w / 2, (h + SINK) / 2, d / 2], ry });
    const k = Math.floor(rand() * 4), alongX = k % 2 === 0, out = (k < 2 ? 1 : -1) * ((alongX ? w : d) / 2 + 0.16);
    const nx = alongX ? Math.cos(ry) : Math.sin(ry), nz = alongX ? -Math.sin(ry) : Math.cos(ry);
    const sh = Math.min(h * 0.45, 2.2 + rand() * 3.2), sw = Math.min((alongX ? d : w) * 0.4, 0.8 + rand() * 1.1);
    signs.push({ ...record(x + nx * out, y + sh / 2 + 2.2 + rand() * Math.max(0, h - sh - 3), z + nz * out, 0.2, sh, sw, alongX ? ry : ry + Math.PI / 2), zone: lot.zone });
    if (lot.heart || h > 15.5) {
      const roofY = y + h, cr = Math.cos(ry), sr = Math.sin(ry);
      const cw = Math.min(w, d) * (0.30 + crownRand() * 0.10), ch = 1.1 + crownRand() * 1.1;
      poles.push(record(x, roofY + ch * 0.5, z, cw, ch, cw, ry));
      const rimW = w * 0.72, rimD = d * 0.72, rimY = roofY + ch * 0.88;
      for (const s of [-1, 1]) {
        lights.push(record(x + sr * (rimD * 0.5) * s, rimY, z + cr * (rimD * 0.5) * s, rimW, 0.16, 0.14, ry));
        lights.push(record(x + cr * (rimW * 0.5) * s, rimY, z - sr * (rimW * 0.5) * s, 0.14, 0.16, rimD, ry));
      }
      const spireH = (lot.heart ? 6.5 : 4.2) + crownRand() * 4.5;
      const ox = (crownRand() - 0.5) * w * 0.28, oz = (crownRand() - 0.5) * d * 0.28;
      const sx = x + cr * ox + sr * oz, sz = z - sr * ox + cr * oz;
      poles.push(record(sx, roofY + ch + spireH * 0.28, sz, 0.11, spireH * 0.56, 0.11));
      poles.push(record(sx, roofY + ch + spireH * 0.76, sz, 0.048, spireH * 0.48, 0.048));
      poles.push(record(sx, roofY + ch + spireH * 0.58, sz, 0.038, 1.35, 0.038, ry, Math.PI / 2));
      lights.push(record(sx, roofY + ch + spireH + 0.14, sz, 0.22, 0.28, 0.22, ry));
      lights.push(record(sx, roofY + ch + spireH * 0.58, sz, 0.26, 0.14, 0.26, ry));
      if (h > 21) {
        const s2 = spireH * 0.62, sx2 = x - cr * ox * 0.85, sz2 = z + sr * ox * 0.85;
        poles.push(record(sx2, roofY + ch + s2 * 0.5, sz2, 0.055, s2, 0.055));
        lights.push(record(sx2, roofY + ch + s2 + 0.12, sz2, 0.18, 0.22, 0.18, ry));
      }
    }
  }
  const toriiPts = [[-120, 180], [120, 180], [-160, 60], [160, 60], [-120, -60], [120, -60], [-160, -140], [120, -220]];
  for (const [gx, gz] of toriiPts) {
    const gy = heightAt(gx, gz);
    for (const s of [-1, 1]) {
      const px = gx + s * 5.2;
      towers.push({ ...record(px, gy + 3.6, gz, 0.85, 8.2, 0.85, 0), wd: 6 });
      colliders.push({ x: px, y: gy + 3.6, z: gz, box: [0.45, 4.1, 0.45], ry: 0 });
      lights.push(record(px, gy + 4.1, gz + 0.46, 0.22, 6.2, 0.08));
    }
    towers.push({ ...record(gx, gy + 7.6, gz, 13.6, 0.72, 1.05, 0), wd: 6 });
    towers.push({ ...record(gx, gy + 6.4, gz, 11.4, 0.44, 0.7, 0), wd: 6 });
    lights.push(record(gx, gy + 8.05, gz, 13.2, 0.2, 1.15));
    signs.push(record(gx, gy + 6.95, gz + 0.52, 0.16, 1.1, 4.2, Math.PI / 2));
  }
  for (const [bx, bz, bh] of [[-40, 180, 16], [40, 180, 19], [-80, 180, 17], [80, 180, 18], [-40, -60, 15], [80, -140, 17]]) {
    const by = heightAt(bx, bz) + bh;
    towers.push({ ...record(bx, by, bz, 18, 2.1, 3.4, 0), wd: 14 });
    lights.push(record(bx, by - 0.8, bz + 1.75, 17.4, 0.2, 0.16));
    lights.push(record(bx, by - 0.8, bz - 1.75, 17.4, 0.2, 0.16));
  }
  {
    const sy = heightAt(GOAL.x, GOAL.z);
    for (let i = 0; i < 4; i++) {
      const a = i * (Math.PI / 2) + Math.PI / 4;
      const px = GOAL.x + Math.cos(a) * 16.5, pz = GOAL.z + Math.sin(a) * 16.5, py = heightAt(px, pz);
      towers.push({ ...record(px, py + 9.5, pz, 1.6, 20, 1.6, a), wd: 2 });
      colliders.push({ x: px, y: py + 9.5, z: pz, box: [0.85, 10, 0.85], ry: a });
      lights.push(record(px - Math.cos(a) * 0.85, py + 10, pz - Math.sin(a) * 0.85, 0.25, 16, 0.25, a));
    }
    towers.push({ ...record(GOAL.x, sy + 18.5, GOAL.z, 25, 1.1, 1.8, Math.PI / 4), wd: 2 });
    towers.push({ ...record(GOAL.x, sy + 18.5, GOAL.z, 25, 1.1, 1.8, -Math.PI / 4), wd: 2 });
    lights.push(record(GOAL.x, sy + 19.3, GOAL.z, 24, 0.3, 1.9, Math.PI / 4));
    lights.push(record(GOAL.x, sy + 19.3, GOAL.z, 24, 0.3, 1.9, -Math.PI / 4));
  }
  const boxGeo = new THREE.BoxGeometry(1, 1, 1);
  towers.sort((a, b) => a.wd - b.wd);
  scatter(boxGeo, pbrCityMaterial(shared, 0), towers);
  scatter(boxGeo, pbrCityMaterial(shared, 1), signs);
  if (towers.length) instances(boxGeo, new THREE.MeshBasicMaterial(), towers, shade);

  // Streetlamps & lane studs
  for (let i = 2; i < way.length - 1; i += 2) {
    const [ax, az] = way[i], [bx, bz] = way[i + 1], l = Math.hypot(bx - ax, bz - az) || 1, nx = (bz - az) / l, nz = -(bx - ax) / l;
    for (const s of [-1, 1]) {
      const x = ax + nx * (ROAD - 0.5) * s, z = az + nz * (ROAD - 0.5) * s;
      lights.push(record(x, heightAt(x, z) + 0.03, z, 0.12, 0.05, 0.6, Math.atan2(bx - ax, bz - az)));
    }
    if (i % 8 !== 2) continue;
    const s = i % 16 === 2 ? 1 : -1, x = ax + nx * (ROAD + 3) * s, z = az + nz * (ROAD + 3) * s, y = heightAt(x, z);
    poles.push(record(x, y + 2.1, z, 0.07, 4.2, 0.07));
    lights.push(record(x - nx * 0.7 * s, y + 4.2, z - nz * 0.7 * s, 1.5, 0.12, 0.3, Math.atan2(-nz, nx)));
  }
  const crossing = (x, z, avenues) => (avenues ? STREETS : AVENUES).some((v) => Math.abs((avenues ? z : x) - v) < 6);
  for (const x of AVENUES) {
    for (let z = GRID_MIN, n = 0; z <= GRID_MAX; z += 12, n++) {
      if (crossing(x, z, true) || mountainHeight(x, z) > 0.6) continue;
      for (const side of [-1, 1]) {
        const lx = x + (ROAD - 0.55) * side;
        if (mountainHeight(lx, z) > 0.6) continue;
        lights.push(record(lx, heightAt(lx, z) + 0.045, z, 0.1, 0.04, 0.55, 0));
      }
      if (n % 3 !== 0) continue;
      const side = n % 6 === 0 ? 1 : -1, lx = x + (ROAD + 2.4) * side;
      if (mountainHeight(lx, z) > 0.6) continue;
      const y = heightAt(lx, z);
      poles.push(record(lx, y + 2.1, z, 0.07, 4.2, 0.07));
      lights.push(record(lx - 0.7 * side, y + 4.2, z, 1.5, 0.12, 0.3, Math.PI / 2));
    }
  }
  for (const z of STREETS) {
    for (let x = GRID_MIN, n = 0; x <= GRID_MAX; x += 12, n++) {
      if (crossing(x, z, false) || mountainHeight(x, z) > 0.6) continue;
      for (const side of [-1, 1]) {
        const lz = z + (ROAD - 0.55) * side;
        if (mountainHeight(x, lz) > 0.6) continue;
        lights.push(record(x, heightAt(x, lz) + 0.045, lz, 0.55, 0.04, 0.1, 0));
      }
      if (n % 3 !== 0) continue;
      const side = n % 6 === 0 ? 1 : -1, lz = z + (ROAD + 2.4) * side;
      if (mountainHeight(x, lz) > 0.6) continue;
      const y = heightAt(x, lz);
      poles.push(record(x, y + 2.1, lz, 0.07, 4.2, 0.07));
      lights.push(record(x, y + 4.2, lz - 0.7 * side, 0.3, 0.12, 1.5, 0));
    }
  }
  scatter(new THREE.CylinderGeometry(1, 1, 1, 8), pbrCityMaterial(shared, 6), poles);
  scatter(boxGeo, pbrCityMaterial(shared, 4), lights);
  shared.tStreetLights.value = streetLights(lights, signs);

  // --- Realistic Branching Broadleaf Trees, Layered Alpine Pines & Sculpted Granite Boulders ---
  const placedTrees = [], broadleafTrunks = [], pineTrunks = [], canopies = [], pines = [], boulders = [];
  for (let i = 0; i < 3200 && placedTrees.length < 420; i++) {
    const x = -325 + rand() * 650, z = -325 + rand() * 650;
    const f = forestAt(x, z), mh = mountainHeight(x, z);
    if (f <= 0.35 && (mh <= 0.3 || mh >= 24)) continue;
    if (wayDist(x, z) < ROAD + 3.4 || clearingAt(x, z) > 0.25) continue;
    if (Math.hypot(x + 20, z - 305) < 28) continue;
    if (keep.some((p) => Math.hypot(p.x - x, p.z - z) < 12)) continue;
    if (toriiPts.some(([tx, tz]) => Math.hypot(tx - x, tz - z) < 9)) continue;
    if (LEVELS.some((L) => L.gate && Math.abs(L.gate.z - z) < 4.5)) continue;
    const y = heightAt(x, z);
    const sx = (heightAt(x + 1.5, z) - heightAt(x - 1.5, z)) / 3;
    const sz = (heightAt(x, z + 1.5) - heightAt(x, z - 1.5)) / 3;
    if (Math.hypot(sx, sz) > 0.75) continue;
    if (placedTrees.some((t) => Math.hypot(t.x - x, t.z - z) < 5.5)) continue;

    placedTrees.push({ x, z });
    const broadleaf = mh < 3.5 && rand() < 0.72;
    const th = broadleaf ? 5.2 + rand() * 4.2 : 6.2 + rand() * 5.2;
    const tr = broadleaf ? 0.30 + rand() * 0.18 : 0.22 + rand() * 0.15;
    const yaw = rand() * 6.28;
    if (broadleaf) {
      broadleafTrunks.push(record(x, y - 0.15, z, tr, th * 0.85, tr, yaw));
      const cs = 2.6 + rand() * 1.7;
      canopies.push(record(x, y + th * 0.84, z, cs, cs * 0.84, cs, yaw));
    } else {
      pineTrunks.push(record(x, y + th * 0.44 - 0.15, z, tr, th * 0.88, tr, yaw));
      const pr = 2.15 + rand() * 0.95;
      pines.push(record(x, y + th * 0.56, z, pr, th * 0.86, pr, yaw));
    }
    if (Math.max(Math.abs(x), Math.abs(z)) < 305 && y < 30) {
      colliders.push({ x, y, z, r: tr * 1.05, h: 4.5 });
    }
  }
  for (let i = 0; i < 650 && boulders.length < 110; i++) {
    const x = -335 + rand() * 670, z = -335 + rand() * 670;
    const f = forestAt(x, z), mh = mountainHeight(x, z);
    if ((mh <= 0.4 || mh >= 32) && f <= 0.45) continue;
    if (wayDist(x, z) < ROAD + 3.2 || clearingAt(x, z) > 0.25) continue;
    if (Math.hypot(x + 20, z - 305) < 28) continue;
    if (keep.some((p) => Math.hypot(p.x - x, p.z - z) < 10)) continue;
    if (LEVELS.some((L) => L.gate && Math.abs(L.gate.z - z) < 4.5)) continue;
    if (placedTrees.some((t) => Math.hypot(t.x - x, t.z - z) < 3.2)) continue;
    const s = 0.9 + rand() * 1.9, y = heightAt(x, z);
    boulders.push(record(x, y + s * 0.30, z, s * 1.18, s * 0.76, s * 1.02, rand() * 6.28));
    if (Math.max(Math.abs(x), Math.abs(z)) < 305 && y < 30) {
      colliders.push({ x, y: y + s * 0.2, z, r: s * 0.85, rock: true });
    }
  }
  const broadleafTrunkGeo = buildBroadleafTrunkGeo();
  const pineTrunkGeo = new THREE.CylinderGeometry(0.18, 1.0, 1, 8);
  const canopyGeo = buildBroadleafCanopyGeo();
  const pineGeo = buildPineBoughsGeo();
  const boulderGeo = buildBoulderGeo();
  const shadowSphereGeo = new THREE.IcosahedronGeometry(0.95, 1);
  const shadowConeGeo = new THREE.ConeGeometry(0.95, 1, 7);
  const shadeMat = new THREE.MeshBasicMaterial();
  const barkMat = pbrCityMaterial(shared, 7);
  if (broadleafTrunks.length) { scatter(broadleafTrunkGeo, barkMat, broadleafTrunks); instances(broadleafTrunkGeo, shadeMat, broadleafTrunks, shade); }
  if (pineTrunks.length) { scatter(pineTrunkGeo, barkMat, pineTrunks); instances(pineTrunkGeo, shadeMat, pineTrunks, shade); }
  if (canopies.length) { scatter(canopyGeo, pbrCityMaterial(shared, 8, 0.032), canopies); instances(shadowSphereGeo, shadeMat, canopies, shade); }
  if (pines.length) { scatter(pineGeo, pbrCityMaterial(shared, 10, 0.022), pines); instances(shadowConeGeo, shadeMat, pines, shade); }
  if (boulders.length) { scatter(boulderGeo, pbrCityMaterial(shared, 9), boulders); instances(boulderGeo, shadeMat, boulders, shade); }

  // Soft tapered grass tufts & industrial crates
  const tuft = new THREE.BufferGeometry(), vertices = [];
  for (let i = 0; i < 6; i++) {
    const a = i * 2.39996, x = Math.sin(a) * 0.14, z = Math.cos(a) * 0.14, h = 0.34 + (i % 3) * 0.11;
    vertices.push(x - 0.045, 0, z, x + 0.045, 0, z, x + Math.sin(a) * 0.15, h, z + Math.cos(a) * 0.13);
  }
  tuft.setAttribute('position', new THREE.Float32BufferAttribute(vertices, 3));
  tuft.computeVertexNormals();
  const weeds = [], crates = [];
  for (let i = 0; i < 8500 && weeds.length < 6500; i++) {
    const x = -325 + rand() * 650, z = -325 + rand() * 650, d = wayDist(x, z), f = forestAt(x, z), mh = mountainHeight(x, z);
    if (d < ROAD + 1.2 || mh > 14 || f <= 0.38) continue;
    const s = 0.5 + rand() * 0.7;
    weeds.push(record(x, heightAt(x, z) - 0.015, z, s, s, s, rand() * 6.28));
  }
  for (let i = 0; i < 700 && crates.length < 280; i++) {
    const x = GRID_MIN + rand() * (GRID_MAX - GRID_MIN), z = GRID_MIN + rand() * (GRID_MAX - GRID_MIN);
    if (forestAt(x, z) > 0.3 || mountainHeight(x, z) > 0.25) continue;
    if (wayDist(x, z) < ROAD + 2.5) continue;
    if (keep.some((p) => Math.hypot(p.x - x, p.z - z) < 8)) continue;
    const s = 0.5 + rand() * 1.0, y = heightAt(x, z);
    crates.push(record(x, y + s * 0.4, z, s * 1.5, s, s * 1.2, rand() * 6.28));
    colliders.push({ x, y: y + s * 0.15, z, r: s * 0.8, rock: true });
  }
  const grasses = scatter(tuft, pbrCityMaterial(shared, 2, 0.11), weeds);
  scatter(boxGeo, pbrCityMaterial(shared, 3), crates);

  // Atmospheric weather: sleek slanting rain streaks at night, soft sunlit blossom petals / dust by day.
  const PETAL_COUNT = 360;
  const petalGeo = new THREE.PlaneGeometry(1, 1);
  const petals = new THREE.InstancedMesh(petalGeo, new THREE.ShaderMaterial({
    glslVersion: THREE.GLSL3,
    side: THREE.DoubleSide,
    transparent: true,
    depthWrite: false,
    uniforms: shared,
    vertexShader: /* glsl */ `
      ${COMMON}
      out vec3 vWorld;
      out vec2 vUv;
      out float vId;
      out float vRain;
      const vec3 PBOX = vec3(58.0, 16.0, 58.0);
      void main() {
        float id = float(gl_InstanceID);
        vId = id;
        vUv = uv;
        vRain = smoothstep(0.35, 0.72, uNight);
        vec4 r = hash4(id * 1.37 + 5.0);
        vec4 q = hash4(id * 2.93 + 17.0);
        vec3 p = r.xyz * PBOX;
        // Rain falls fast and diagonally; daytime dust/petals drift softly on the breeze
        float vy = mix(-(0.35 + 0.32 * r.w), -(9.5 + 4.5 * r.w), vRain);
        p.xz += uWind.xy * uTime * mix(0.75 + 1.0 * q.y, 1.8 + 0.9 * q.y, vRain);
        p.y += uTime * vy;
        vec3 origin = vec3(uFocus.x, uFocus.y + 6.0, uFocus.z);
        p = mod(p - origin + PBOX * 0.5, PBOX) - PBOX * 0.5 + origin;
        if (vRain < 0.5) {
          float ph = uTime * (1.0 + 0.8 * q.x) + r.w * TAU;
          p.xz += vec2(sin(ph), cos(ph * 0.8 + q.z * 6.0)) * (0.35 + 0.35 * q.y);
        }
        float ground = heightAt(p.xz);
        float vis = smoothstep(ground + 0.15, ground + 0.85, p.y) * (1.0 - smoothstep(22.0, 29.0, distance(p.xz, uFocus.xz)));
        vec3 toEye = normalize(cameraPosition - p);
        vec3 camR = normalize(cross(vec3(0.0, 1.0, 0.0), toEye + vec3(1e-4, 0.0, 1e-4)));
        vec3 camU = cross(toEye, camR);
        vec3 side, up;
        vec2 sz;
        if (vRain > 0.5) {
          vec3 fallDir = normalize(vec3(uWind.x * 0.18, -1.0, uWind.y * 0.18));
          side = normalize(cross(fallDir, toEye));
          up = fallDir;
          sz = vec2(0.011, 0.42 + 0.32 * q.z) * vis;
        } else {
          float spin = uTime * (1.1 + 1.2 * q.x) + r.x * TAU;
          side = cos(spin) * camR + sin(spin) * camU;
          up = -sin(spin) * camR + cos(spin) * camU;
          sz = vec2(0.055 + 0.045 * q.z, 0.032 + 0.025 * q.y) * vis;
        }
        vWorld = p + side * (position.x * sz.x) + up * (position.y * sz.y);
        gl_Position = vis > 0.001 ? projectionMatrix * viewMatrix * vec4(vWorld, 1.0) : vec4(2.0, 2.0, 2.0, 1.0);
      }`,
    fragmentShader: /* glsl */ `
      ${COMMON}
      in vec3 vWorld; in vec2 vUv; in float vId; in float vRain;
      layout(location = 0) out vec4 outC;
      void main() {
        vec2 c = vUv * 2.0 - 1.0;
        if (vRain > 0.5) {
          float streak = (1.0 - c.x * c.x) * smoothstep(1.0, 0.1, abs(c.y));
          vec3 pool = toLinear(streetLightAt(vWorld.xz));
          vec3 col = (vec3(0.32, 0.42, 0.58) + pool * 2.4) * streak;
          outC = vec4(hazed(col, vWorld), streak * 0.32 * vRain);
          return;
        }
        float d = length(c * vec2(1.0, 1.35));
        float shape = 1.0 - smoothstep(0.55, 0.98, d);
        if (shape < 0.05) discard;
        vec4 q = hash4(vId * 2.93 + 17.0);
        vec3 col = q.z < 0.55 ? vec3(0.95, 0.58, 0.68) : vec3(0.95, 0.85, 0.58);
        col *= 0.75 + 0.55 * woodSunAt(vWorld);
        outC = vec4(hazed(col, vWorld), shape * 0.78);
      }`,
  }), PETAL_COUNT);
  dummy.position.set(0, 0, 0); dummy.rotation.set(0, 0, 0); dummy.scale.setScalar(1); dummy.updateMatrix();
  for (let i = 0; i < PETAL_COUNT; i++) petals.setMatrixAt(i, dummy.matrix);
  petals.instanceMatrix.needsUpdate = true;
  petals.frustumCulled = false;
  group.add(petals);

  // --- Photorealistic Physically-Inspired Rayleigh+Mie Sky Dome & 3D-Lit Volumetric FBM Clouds ---
  const sky = new THREE.Mesh(new THREE.SphereGeometry(950, 32, 20), new THREE.ShaderMaterial({
    glslVersion: THREE.GLSL3, uniforms: shared, side: THREE.BackSide, depthWrite: false,
    vertexShader: `out vec3 vDir;
      void main() {
        vDir = position;
        gl_Position = projectionMatrix * viewMatrix * vec4(position + cameraPosition, 1.0);
        gl_Position.z = gl_Position.w;
      }`,
    fragmentShader: `${COMMON}
      in vec3 vDir; layout(location = 0) out vec4 outC;
      float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }
      float cloudDensity(vec2 p) {
        vec2 n1 = texture(tNoise, p).rg;
        float n2 = texture(tNoise, p * 2.45 + vec2(n1.y * 0.08, -n1.x * 0.06)).b;
        float n3 = texture(tNoise, p * 5.8 - vec2(uTime * 0.0005, 0.0)).a;
        return n1.x * 0.54 + n1.y * 0.22 + n2 * 0.16 + n3 * 0.08;
      }
      void main() {
        vec3 v = normalize(vDir);
        float up = max(v.y, 0.0);
        float day = 1.0 - uNight;
        float sunDot = max(dot(v, uSunDir), 0.0);

        // 1. Rayleigh + Mie Daytime / Twilight Atmosphere
        vec3 zenithDay = mix(vec3(0.08, 0.28, 0.78), vec3(0.06, 0.10, 0.28), uTwilight);
        vec3 midDay = mix(vec3(0.24, 0.52, 0.92), vec3(0.42, 0.24, 0.34), uTwilight);
        vec3 horizDay = mix(vec3(0.64, 0.78, 0.92), vec3(1.15, 0.44, 0.18), uTwilight);
        vec3 daySky = mix(horizDay, midDay, smoothstep(0.0, 0.24, up));
        daySky = mix(daySky, zenithDay, smoothstep(0.18, 0.85, up));
        // Solar Mie forward scatter + crisp HDR sun disc
        vec3 sunGlowCol = mix(vec3(1.0, 0.90, 0.68), vec3(1.4, 0.46, 0.16), uTwilight);
        daySky += sunGlowCol * (pow(sunDot, 8.0) * 0.24 + pow(sunDot, 48.0) * 0.55);
        daySky += vec3(6.5, 5.8, 4.4) * smoothstep(0.9985, 0.9994, dot(v, uSunDir));

        // 2. Deep Atmospheric Night Sky + Urban Light-Pollution Horizon + Cratered Moon + Twinkling Stars
        vec3 fol = toLinear(uFoliage);
        vec3 nightZenith = vec3(0.007, 0.013, 0.032);
        vec3 nightHoriz = vec3(0.028, 0.042, 0.082) + fol * 0.085 + toLinear(uHaze) * 0.16;
        vec3 nightSky = mix(nightHoriz, nightZenith, pow(up, 0.48));
        // Subtle atmospheric moon halo + realistic shaded moon disc
        nightSky += vec3(0.16, 0.24, 0.38) * pow(sunDot, 22.0) * 0.42;
        float moonEdge = smoothstep(0.9987, 0.9992, dot(v, uSunDir));
        if (moonEdge > 0.001 && uNight > 0.1) {
          vec3 mRight = normalize(cross(vec3(0.0, 1.0, 0.0), uSunDir));
          vec3 mUp = cross(uSunDir, mRight);
          vec2 mUv = vec2(dot(v, mRight), dot(v, mUp)) * 18.0;
          float maria = 0.72 + 0.28 * texture(tNoise, mUv * 0.35 + 0.5).r;
          nightSky = mix(nightSky, vec3(1.85, 1.92, 2.05) * maria, moonEdge);
        }
        // Crisp pinpoint multi-temperature twinkling starfield
        if (uNight > 0.25 && up > 0.06) {
          vec2 sg = v.xz / (up + 0.18) * 145.0;
          vec2 sid = floor(sg);
          vec2 sf = fract(sg) - 0.5;
          vec4 sr = hash4(sid, 41.7);
          if (sr.x > 0.986) {
            float starD = length(sf - (sr.yz - 0.5) * 0.55);
            float pt = exp(-starD * starD * 48.0);
            float tw = 0.65 + 0.35 * sin(uTime * (2.0 + 5.0 * sr.w) + sr.x * 120.0);
            vec3 starCol = mix(vec3(0.75, 0.86, 1.0), vec3(1.0, 0.88, 0.70), sr.w);
            nightSky += starCol * pt * tw * smoothstep(0.06, 0.28, up) * smoothstep(0.25, 0.82, uNight) * 1.45;
          }
        }

        vec3 c = mix(daySky, nightSky, uNight);

        // 3. 3D-Lit Volumetric-Style Cumulus / Stratus Cloud Layer
        if (v.y > 0.015) {
          vec2 q = (v.xz + cameraPosition.xz * 0.0015) / (v.y + 0.24) * vec2(0.14, 0.18);
          q -= uWind.xy * uTime * 0.00055;
          float d0 = cloudDensity(q);
          // Second directional sample toward sun/moon gives authentic 3D cloud self-shadowing & silver lining!
          vec2 sunOff = uSunDir.xz / max(v.y + 0.35, 0.25) * 0.022;
          float dSun = cloudDensity(q + sunOff);
          float cover = smoothstep(0.50, 0.74, d0) * smoothstep(0.015, 0.15, v.y);
          float lightStep = clamp((d0 - dSun) * 3.6 + 0.45, 0.0, 1.0);
          float silverLining = pow(sunDot, 6.0) * smoothstep(0.46, 0.68, d0) * (1.0 - smoothstep(0.68, 0.84, d0)) * 1.1;

          vec3 dayCloudDark = mix(vec3(0.32, 0.40, 0.54), vec3(0.28, 0.18, 0.28), uTwilight);
          vec3 dayCloudLit = mix(vec3(1.08, 1.04, 0.98), vec3(1.35, 0.56, 0.26), uTwilight);
          vec3 dayCloud = mix(dayCloudDark, dayCloudLit, lightStep) + sunGlowCol * silverLining * 0.55;

          // Dramatic dark night clouds underlit subtly by Neo-Kyoto's horizon neon & silver-rimmed by moon
          vec3 nightCloudDark = vec3(0.014, 0.020, 0.038);
          vec3 nightCloudUnder = (vec3(0.045, 0.062, 0.105) + fol * 0.08) * (1.0 - 0.55 * up);
          vec3 nightCloud = mix(nightCloudDark, nightCloudUnder, lightStep * 0.75 + 0.25) + vec3(0.22, 0.32, 0.48) * silverLining * 0.38;
          c = mix(c, mix(dayCloud, nightCloud, uNight), cover * 0.92);
        }

        // Distant hazy skyline silhouette below horizon crest
        float a = atan(v.x, v.z) * 46.0;
        float top = 0.014 + 0.072 * hash(vec2(floor(a), 3.0)) * hash(vec2(floor(a * 0.31), 9.0));
        if (v.y < top && v.y > -0.04) {
          vec2 room = vec2(fract(a) * 5.0, v.y * 420.0);
          float on = step(0.86, hash(floor(room) + floor(a))) * step(0.25, fract(room.x)) * step(0.3, fract(room.y)) * smoothstep(0.3, 0.8, uNight);
          vec3 sil = mix(airColour(v) * 0.78, vec3(0.014, 0.020, 0.036), uNight);
          vec3 winCol = mix(toLinear(uLeafLight), fol, step(0.7, hash(floor(room)))) * 1.15;
          c = mix(sil, winCol, on);
        }
        outC = vec4(c, 1.0);
      }`,
  }));
  sky.renderOrder = 10;
  sky.frustumCulled = false;
  group.add(sky);

  let active = -1;
  const targetColor = new THREE.Color(), nightMix = new THREE.Color();
  function update(dt, z, cleared, roam) {
    const index = chapterAt(z, cleared, roam), c = CHAPTERS[index];
    if (index !== active) {
      active = index;
      document.documentElement.dataset.chapter = String(index);
      document.documentElement.dataset.style = c.style;
      document.documentElement.style.setProperty('--chapter-accent', c.color);
      dispatchEvent(new CustomEvent('chapterchange', { detail: { index, ...c } }));
    }
    const n = shared.uNight.value, tw = shared.uTwilight.value;
    ZONES.forEach((zone, i) => {
      const zc = CHAPTERS[zone.chapter];
      for (const [uArr, key] of [[shared.uZoneGrass.value, 'grass'], [shared.uZonePath.value, 'path']]) {
        const d = zc.day[key], nt = zc[key];
        uArr[i].set(d[0] + (nt[0] - d[0]) * n, d[1] + (nt[1] - d[1]) * n, d[2] + (nt[2] - d[2]) * n);
      }
    });
    const blend = 1 - Math.exp(-dt * 2);
    for (const [uniform, key] of [['uGrass', 'grass'], ['uPath', 'path'], ['uSkyHigh', 'sky'], ['uHaze', 'haze']]) {
      const d = c.day[key], nt = c[key];
      targetColor.setRGB(d[0] + (nt[0] - d[0]) * n, d[1] + (nt[1] - d[1]) * n, d[2] + (nt[2] - d[2]) * n);
      if (key === 'haze' && tw > 0.001) {
        targetColor.r = Math.min(1, targetColor.r + 0.32 * tw);
        targetColor.g = Math.min(1, targetColor.g + 0.16 * tw);
        targetColor.b = Math.min(1, targetColor.b + 0.08 * tw);
      }
      shared[uniform].value.lerp(targetColor, blend);
    }
    for (const [uniform, key] of [['uFoliage', 'foliage'], ['uLeafLight', 'leafLight']]) {
      shared[uniform].value.lerp(targetColor.setRGB(...c[key]), blend);
    }
    nightMix.copy(shared.uHaze.value).lerp(targetColor.setRGB(0.7, 0.8, 1.0), 0.18);
    shared.uHazeSun.value.setRGB(1.0, 0.93, 0.76).lerp(nightMix, n);
    shared.uHazeDensity.value = 0.0018 + 0.0024 * n;
    shared.uGlazeMul.value.setRGB(0.58, 0.64, 0.75).lerp(targetColor.setRGB(0.65, 0.72, 0.75), n);
    nightMix.copy(shared.uFoliage.value).multiplyScalar(0.42);
    shared.uRim.value.setRGB(0.22, 0.21, 0.16).lerp(nightMix, n);
    nightMix.copy(shared.uHaze.value).multiplyScalar(0.3);
    shared.uBounce.value.setRGB(0.11, 0.11, 0.08).lerp(nightMix, n);
  }
  return {
    group, shade, colliders,
    counts: {
      trees: towers.length, forestTrees: placedTrees.length, boulders: boulders.length,
      grass: weeds.length, flowers: lights.length, rocks: crates.length, petals: PETAL_COUNT,
    },
    update,
    setQuality(detail) {
      for (const g of grasses) g.count = Math.floor(g.userData.full * detail);
      petals.count = Math.floor(PETAL_COUNT * detail);
    },
  };
}
