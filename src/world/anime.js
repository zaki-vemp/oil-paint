// Solid, reusable scenery for the city, forests and mountains: blocks on a road grid, towers with
// day glass / night windows, neon signs, street lamps, woodland trees, alpine pines, boulders, grass
// tufts, clouds and a dynamic day/night sky. Every placement uses heightAt and keeps roads/combat clear.
import * as THREE from 'three';
import { COMMON } from '../oilpaint/common.js';
import { heightAt, wayDist, clearingAt, forestAt, mountainHeight, rng, way, ROAD } from './land.js';
import { CHAPTERS, chapterAt } from '../game/chapters.js';
import { LEVELS, GOAL } from '../game/levels.js';
import { DUELS } from '../game/duel.js';
import { EXTRA_KEEP } from '../game/story.js';
import { blockLots, AVENUES, STREETS, GRID_MIN, GRID_MAX, ZONES, ZONE_SPLIT, zoneAt } from './grid.js';

// Roles: 0 tower, 1 sign, 2 weeds, 3 crate, 4 lamp head / lane stud, 5 clouds, 6 pole,
//        7 tree trunk, 8 tree canopy / pine boughs, 9 boulder.
function cel(shared, role, sway = 0) {
  return new THREE.ShaderMaterial({
    // closed solids render FrontSide; grass blades are single triangles and show both faces
    glslVersion: THREE.GLSL3, side: role === 2 ? THREE.DoubleSide : THREE.FrontSide,
    uniforms: { ...shared, uRole: { value: role }, uSway: { value: sway } },
    vertexShader: /* glsl */ `
      ${COMMON}
      uniform float uSway;
      out vec3 vWorld; out vec3 vNormal; flat out float vSeed; out vec3 vObj; out vec3 vObjN; flat out vec3 vSize;
      void main() {
        vec4 w = instanceMatrix * vec4(position, 1.0);
        vSeed = fract(sin(instanceMatrix[3].x * 17.3 + instanceMatrix[3].z * 31.7) * 43758.5);
        // the thing's own metres, whichever way it is turned: windows and stripes are drawn in these
        vSize = vec3(length(instanceMatrix[0].xyz), length(instanceMatrix[1].xyz), length(instanceMatrix[2].xyz));
        vObj = position * vSize; vObjN = normal;
        float wave = sin(uTime * 1.8 + w.x * 0.28 + w.z * 0.31);
        w.x += wave * max(position.y, 0.0) * uSway;
        w.z += cos(uTime * 1.4 + w.z * 0.24) * max(position.y, 0.0) * uSway * 0.4;
        vWorld = (modelMatrix * w).xyz;
        vNormal = normalize(mat3(modelMatrix) * mat3(instanceMatrix) * normal);
        gl_Position = projectionMatrix * viewMatrix * vec4(vWorld, 1.0);
      }`,
    fragmentShader: /* glsl */ `
      ${COMMON}
      uniform float uRole;
      in vec3 vWorld; in vec3 vNormal; flat in float vSeed; in vec3 vObj; in vec3 vObjN; flat in vec3 vSize;
      layout(location = 0) out vec4 outC;
      float hash(vec2 p) { return hash4(p, 0.0).x; }
      void main() {
        vec3 fol = zoneFoliageAt(vWorld.xz);
        vec3 pale = zoneLeafAt(vWorld.xz);
        vec3 pave = zoneGrassAt(vWorld.xz);
        vec3 n = normalize(vNormal);
        vec3 base = vec3(0.20, 0.22, 0.26), glow = vec3(0.0);
        float own = 0.0; // how much of this spot is its own light rather than a lit surface
        float nightOn = smoothstep(0.22, 0.78, uNight);
        if (uRole < 0.5) {
          vec3 dayBase = mix(vec3(0.54, 0.57, 0.63), vec3(0.74, 0.72, 0.68), vSeed);
          vec3 nightBase = mix(vec3(0.065, 0.075, 0.11), vec3(0.13, 0.14, 0.19), vSeed);
          base = mix(dayBase, nightBase, uNight);
          float side = step(abs(vObjN.y), 0.5);
          float isBldg = step(2.8, min(vSize.x, vSize.z)) * step(6.5, vSize.y);
          float u = abs(vObjN.x) > 0.5 ? vObj.z : vObj.x;
          float fw = abs(vObjN.x) > 0.5 ? vSize.z : vSize.x;
          float corner = step(fw * 0.5 - 0.26, abs(u)) * side;
          float ag = vObj.y + vSize.y * 0.5 - 3.0; // metres above pavement (SINK = 3)
          float bw = mix(1.08, 1.42, step(0.5, fract(vSeed * 7.3)));
          vec2 cell = vec2(u / bw, ag / 1.72), f = fract(cell);

          // Mid-tower mechanical floor bands on medium and tall high-rises
          float mech1 = step(16.0, vSize.y) * step(abs(ag - 10.6), 0.82);
          float mech2 = step(27.0, vSize.y) * step(abs(ag - 21.2), 0.82);
          float mech = clamp(mech1 + mech2, 0.0, 1.0) * step(ag, vSize.y - 5.2) * side * isBldg;
          float podium = step(ag, 3.35) * side * isBldg;

          // Architectural vertical piers, window mullions & horizontal spandrels
          float pier = step(0.84, abs(fract(u / (bw * 3.0)) - 0.5) * 2.0) * side * isBldg;
          float mullion = step(0.42, abs(f.x - 0.5)) * (1.0 - pier) * side * isBldg;
          float spandrel = step(0.82, f.y) * side * isBldg;
          base *= mix(1.0, 0.76, pier);
          base *= mix(1.0, 0.88, max(mullion, spandrel) * (1.0 - podium));

          // Day sky-glass and night apartments/offices with clean graphic blinds, curtains & shoji screens
          float pane = step(0.14, f.x) * step(f.x, 0.86) * step(0.20, f.y) * step(f.y, 0.80) * side;
          pane *= (1.0 - mech) * (1.0 - podium) * (1.0 - pier * 0.85) * (1.0 - corner);
          vec2 cid = floor(cell);
          float h = hash(cid + vSeed * 91.7);
          float h2 = hash(cid * 1.73 + vSeed * 37.1);
          float blind = 1.0;
          if (h2 < 0.34) {
            // Half-drawn roller shade: warm bright lower half, soft shaded upper half
            blind = mix(1.0, 0.36, step(0.38 + 0.34 * h, f.y));
          } else if (h2 > 0.70) {
            // Parted side curtains framing the lit interior
            blind = mix(0.30, 1.0, step(abs(f.x - 0.5), 0.18 + 0.14 * h));
          } else if (h2 > 0.52) {
            // Sliding shoji / partition panel across half the window
            blind = mix(0.52, 1.0, step(0.46, f.x));
          }
          float skyDiag = smoothstep(-0.25, 0.35, sin((u + vObj.y * 0.85) * 0.26 + vSeed * 6.0));
          vec3 dayGlass = mix(vec3(0.28, 0.44, 0.62), vec3(0.50, 0.68, 0.82), 0.55 * h + 0.45 * skyDiag);
          dayGlass = mix(dayGlass * 0.78, mix(dayGlass, vec3(0.76, 0.78, 0.80), 0.32), blind);
          vec3 warmRoom = mix(vec3(1.0, 0.82, 0.48), pale, 0.50);
          vec3 roomCol = mix(warmRoom, fol, step(0.84, h)) * (0.72 + 0.32 * f.y) * blind;
          float litRoom = pane * step(0.54, h) * nightOn;
          glow = roomCol;
          own = litRoom * mix(0.62, 1.0, step(0.40, blind));
          base = mix(base, mix(dayGlass, base * 1.65 + vec3(0.015, 0.02, 0.035) * blind, uNight), pane * (1.0 - litRoom));

          // Louvered mechanical band with neon accent pinstripe
          if (mech > 0.5) {
            float louver = step(0.45, fract(ag * 3.5));
            base = mix(dayBase * 0.62, nightBase * 0.72, uNight) * mix(0.74, 1.0, louver);
            float mechPip = step(0.70, abs(ag - (mech1 > 0.5 ? 10.6 : 21.2)));
            glow = mix(glow, fol * 0.85, mechPip);
            own = max(own, mechPip * mix(0.10, 0.85, nightOn));
          }

          // Roof parapet framing for elevated views
          if (vObjN.y > 0.5 && isBldg > 0.5) {
            float parapet = max(step(vSize.x * 0.5 - 0.32, abs(vObj.x)), step(vSize.z * 0.5 - 0.32, abs(vObj.z)));
            base *= mix(0.82, 1.08, parapet);
          }

          // Neon cornice line under the roof
          float trim = step(vSize.y * 0.5 - 0.30, vObj.y) * side;
          glow = mix(glow, fol, trim);
          own = max(own, trim * mix(0.14, 1.0, nightOn));

          // Dark corner piers and warm ground-floor Neo-Kyoto shopfront arcades
          base *= mix(1.0, 0.62, corner);
          if (podium > 0.5 && corner < 0.5) {
            float bayU = fract(u / 2.25);
            float bayId = hash(vec2(floor(u / 2.25), floor(vSeed * 43.0)));
            float shopWin = step(0.18, ag) * step(ag, 2.68) * step(0.11, bayU) * step(bayU, 0.89);
            float mull = 0.86 + 0.14 * step(0.10, fract(bayU * 3.0));
            float fascia = step(2.80, ag) * step(ag, 3.14) * step(0.06, bayU) * step(bayU, 0.94);
            vec3 shopWarm = mix(vec3(1.0, 0.78, 0.40), pale, 0.32);
            vec3 shopCol = mix(shopWarm, fol, step(0.84, bayId)) * mull * mix(0.74, 1.06, clamp((ag - 0.18) / 2.50, 0.0, 1.0));
            vec3 dayShop = mix(vec3(0.24, 0.32, 0.40), shopWarm * 0.60, 0.38) * mull;
            base = mix(base * 0.80, dayShop, shopWin * (1.0 - uNight));
            glow = mix(glow, shopCol, shopWin);
            own = max(own, shopWin * mix(0.14, 0.94, nightOn));
            vec3 fasciaCol = mix(fol, pale, step(0.45, bayId));
            glow = mix(glow, fasciaCol, fascia);
            own = max(own, fascia * mix(0.24, 0.96, nightOn));
          }
        } else if (uRole < 1.5) {
          // Segmented Neo-Kyoto signboard: outer neon border + multi-cell cyber-glyph blocks with scanline pulse
          vec3 colA = mix(fol, pale, step(0.5, vSeed));
          vec3 colB = mix(pale, fol, step(0.5, vSeed));
          base = mix(vec3(0.16, 0.18, 0.24), vec3(0.04, 0.05, 0.09), uNight);
          if (abs(vObjN.x) > 0.5) {
            float dx = vSize.z * 0.5 - abs(vObj.z);
            float dy = vSize.y * 0.5 - abs(vObj.y);
            float dEdge = min(dx, dy);
            float border = step(0.035, dEdge) * step(dEdge, 0.095);
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
            float scan = 0.88 + 0.12 * sin(vObj.y * 36.0 - uTime * 3.4 + vSeed * 12.0);
            float flick = 0.90 + 0.10 * step(0.07, fract(sin(floor(uTime * 8.0) + vSeed * 40.0) * 91.3));
            vec3 glyphCol = mix(colA, colB, step(0.75, fract(gh * 5.3)) * inBox * step(0.72, guv.y));
            float litMask = clamp(border + glyph, 0.0, 1.0);
            base = mix(base, glyphCol * 0.82, litMask * (1.0 - uNight));
            glow = mix(colA * 0.12, mix(glyphCol * 1.12, colB * 1.05, border), litMask) * scan * flick;
            own = mix(0.28 * litMask, mix(0.16, 1.0, litMask), smoothstep(0.16, 0.72, uNight));
          } else {
            glow = colB * 0.9;
            own = step(abs(vObj.x), 0.025) * smoothstep(0.2, 0.75, uNight);
          }
        } else if (uRole < 2.5) {
          float isNat = step(0.3, texture(tLand, landUv(vWorld.xz)).a);
          vec3 dayGrass = mix(vec3(0.24, 0.56, 0.20), vec3(0.48, 0.72, 0.24), vSeed);
          vec3 nightGrass = mix(vec3(0.05, 0.15, 0.13), vec3(0.09, 0.24, 0.19), vSeed);
          vec3 natBase = mix(dayGrass, nightGrass, uNight);
          vec3 urbBase = mix(pave * 1.3, mix(dayGrass * 0.85, fol * 0.3, uNight), vSeed * 0.6);
          base = mix(urbBase, natBase, isNat);
        } else if (uRole < 3.5) {
          vec3 dayCrate = mix(vec3(0.38, 0.40, 0.44), vec3(0.52, 0.50, 0.48), vSeed);
          vec3 nightCrate = mix(vec3(0.10, 0.11, 0.15), vec3(0.17, 0.18, 0.22), vSeed);
          base = mix(dayCrate, nightCrate, uNight);
          glow = vec3(0.99, 0.87, 0.08);
          own = step(abs(vObj.y - vSize.y * 0.12), vSize.y * 0.07) * step(0.5, fract((vObj.x + vObj.z) * 2.2)) * mix(0.35, 1.0, uNight);
        } else if (uRole < 4.5) {
          base = vec3(0.24, 0.27, 0.31);
          glow = mix(fol, pale, step(0.5, vSeed));
          own = mix(0.12, 1.0, smoothstep(0.18, 0.7, uNight));
        } else if (uRole < 5.5) {
          vec3 dayCloud = mix(vec3(0.70, 0.77, 0.88), vec3(0.97, 0.96, 0.93), smoothstep(-0.3, 0.45, n.y)) + vec3(1.0, 0.58, 0.36) * uTwilight * 0.35;
          vec3 nightCloud = mix(uSkyHigh * 2.2 + 0.02, uHaze * 1.25, max(-n.y, 0.0));
          base = mix(dayCloud, nightCloud, uNight);
        } else if (uRole < 6.5) {
          base = mix(vec3(0.32, 0.35, 0.40), vec3(0.09, 0.10, 0.13), uNight);
        } else if (uRole < 7.5) {
          base = mix(mix(vec3(0.34, 0.24, 0.17), vec3(0.48, 0.36, 0.25), vSeed), mix(vec3(0.07, 0.06, 0.08), vec3(0.12, 0.10, 0.12), vSeed), uNight);
        } else if (uRole < 8.5) {
          vec3 dayCanopy = vSeed > 0.85
            ? mix(vec3(0.92, 0.58, 0.68), vec3(0.94, 0.74, 0.24), step(0.92, vSeed))
            : mix(vec3(0.20, 0.52, 0.22), vec3(0.46, 0.74, 0.25), vSeed);
          vec3 nightCanopy = mix(vec3(0.04, 0.14, 0.14), vec3(0.09, 0.26, 0.22), vSeed);
          base = mix(dayCanopy, nightCanopy, uNight);
        } else {
          base = mix(mix(vec3(0.40, 0.42, 0.46), vec3(0.52, 0.50, 0.49), vSeed), mix(vec3(0.08, 0.11, 0.17), vec3(0.13, 0.15, 0.22), vSeed), uNight);
        }
        float light = dot(n, uSunDir) * 0.5 + 0.5;
        float shade = uRole > 4.5 && uRole < 5.5 ? 1.0 : woodSunAt(vWorld + n * 0.1);
        float band = mix(0.65, 0.85, smoothstep(0.28, 0.31, light));
        band = mix(band, 1.12, smoothstep(0.65, 0.68, light));
        vec3 c = base * band * mix(0.79, 1.0, shade);
        float rim = abs(dot(n, normalize(cameraPosition - vWorld)));
        if (uRole < 0.5 || (uRole > 2.5 && uRole < 3.5) || (uRole > 5.5 && uRole < 7.5) || uRole > 8.5) {
          c *= mix(0.72, 1.0, smoothstep(0.02, 0.16, rim));
        }
        c = mix(c, glow, own);
        outC = vec4(hazed(c, vWorld), 1.0);
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
    // The sun's pass draws the shade group from a camera that must see every tower and tree.
    mesh.frustumCulled = parent === group;
    parent.add(mesh);
    return mesh;
  }
  // One mesh per district, culled by the camera.
  function scatter(geometry, material, items) {
    const bins = [[], [], [], []];
    for (const p of items) bins[zoneAt(p.x, p.z)].push(p);
    return bins.filter((bin) => bin.length).map((bin) => {
      const mesh = instances(geometry, material, bin);
      mesh.userData.full = bin.length;
      return mesh;
    });
  }

  // Nothing is built where a fight happens: every spawn, duel, mission target, kiosk, relay and gate keeps its room.
  const keep = [GOAL, ...DUELS, ...EXTRA_KEEP, ...LEVELS.flatMap((L) => [L.checkpoint, ...L.spots, ...(L.gate ? [L.gate] : [])])];
  const SINK = 3; // metres a tower goes into the ground, so a slope never shows under it
  const towers = [], signs = [], poles = [], lights = [];
  const crownRand = rng(91837); // isolated RNG so existing tower/prop coordinates and colliders never shift
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
    // Penthouse crowns & rooftop comms spires on tall high-rises / heart lots (batched into poles & lights: 0 extra draw calls)
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
  // Painted 3D landmarks: Avenue Neon Torii gateways, elevated skybridges, and the Amber Sanctuary spire.
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
  // Drawn nearest the street first: seen from it, the towers behind are mostly hidden and their pixels are never shaded.
  towers.sort((a, b) => a.wd - b.wd);
  scatter(boxGeo, cel(shared, 0), towers);
  scatter(boxGeo, cel(shared, 1), signs);
  if (towers.length) instances(boxGeo, new THREE.MeshBasicMaterial(), towers, shade);

  // Lamps lean over the way from alternate sides; lit dashes mark both of its edges.
  for (let i = 2; i < way.length - 1; i += 2) {
    const [ax, az] = way[i], [bx, bz] = way[i + 1], l = Math.hypot(bx - ax, bz - az) || 1, nx = (bz - az) / l, nz = -(bx - ax) / l;
    for (const s of [-1, 1]) {
      const x = ax + nx * (ROAD - 0.5) * s, z = az + nz * (ROAD - 0.5) * s;
      lights.push(record(x, heightAt(x, z) + 0.03, z, 0.12, 0.05, 0.6, Math.atan2(bx - ax, bz - az))); // a dash along the lane
    }
    if (i % 8 !== 2) continue;
    const s = i % 16 === 2 ? 1 : -1, x = ax + nx * (ROAD + 3) * s, z = az + nz * (ROAD + 3) * s, y = heightAt(x, z);
    poles.push(record(x, y + 2.1, z, 0.07, 4.2, 0.07));
    lights.push(record(x - nx * 0.7 * s, y + 4.2, z - nz * 0.7 * s, 1.5, 0.12, 0.3, Math.atan2(-nz, nx)));
  }
  // The same dashes and lamps along every avenue and street. Skip crossings and mountain slopes.
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
  scatter(new THREE.CylinderGeometry(1, 1, 1, 6), cel(shared, 6), poles);
  scatter(boxGeo, cel(shared, 4), lights);

  // --- Woodland trees, alpine pines, boulders & grass tufts ---
  const placedTrees = [], trunks = [], canopies = [], pines = [], boulders = [];
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
    const th = broadleaf ? 4.5 + rand() * 4.0 : 5.5 + rand() * 5.0;
    const tr = broadleaf ? 0.28 + rand() * 0.20 : 0.22 + rand() * 0.16;
    trunks.push(record(x, y + th * 0.45 - 0.3, z, tr, th, tr, rand() * 6.28));
    if (broadleaf) {
      const cs = 2.4 + rand() * 1.8;
      for (let k = 0; k < 3; k++) {
        const a = rand() * 6.28, rOff = k === 0 ? 0 : cs * 0.42;
        const ls = cs * (k === 0 ? 1.0 : 0.74);
        canopies.push(record(x + Math.cos(a) * rOff, y + th * (0.78 + k * 0.12), z + Math.sin(a) * rOff, ls, ls * 0.78, ls, rand() * 6.28));
      }
    } else {
      for (let k = 0; k < 3; k++) {
        const cr = (2.1 - k * 0.45) * (0.85 + rand() * 0.3);
        const ch = (2.8 - k * 0.4) * (0.85 + rand() * 0.3);
        pines.push(record(x, y + th * (0.48 + k * 0.24), z, cr, ch, cr, rand() * 6.28));
      }
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
    boulders.push(record(x, y + s * 0.32, z, s * 1.15, s * 0.78, s * 1.0, rand() * 6.28));
    if (Math.max(Math.abs(x), Math.abs(z)) < 305 && y < 30) {
      colliders.push({ x, y: y + s * 0.2, z, r: s * 0.85, rock: true });
    }
  }
  const trunkGeo = new THREE.CylinderGeometry(0.65, 1, 1, 6);
  const canopyGeo = new THREE.IcosahedronGeometry(1, 1);
  const pineGeo = new THREE.ConeGeometry(1, 1, 7);
  const boulderGeo = new THREE.IcosahedronGeometry(1, 0);
  const shadeMat = new THREE.MeshBasicMaterial();
  if (trunks.length) { scatter(trunkGeo, cel(shared, 7), trunks); instances(trunkGeo, shadeMat, trunks, shade); }
  if (canopies.length) { scatter(canopyGeo, cel(shared, 8, 0.035), canopies); instances(canopyGeo, shadeMat, canopies, shade); }
  if (pines.length) { scatter(pineGeo, cel(shared, 8, 0.025), pines); instances(pineGeo, shadeMat, pines, shade); }
  if (boulders.length) { scatter(boulderGeo, cel(shared, 9), boulders); instances(boulderGeo, shadeMat, boulders, shade); }

  // A clump is six broad blades in one geometry, with a stable seed and a bend at its tip.
  const tuft = new THREE.BufferGeometry(), vertices = [];
  for (let i = 0; i < 6; i++) {
    const a = i * 2.39996, x = Math.sin(a) * 0.15, z = Math.cos(a) * 0.15, h = 0.36 + (i % 3) * 0.11;
    vertices.push(x - 0.06, 0, z, x + 0.06, 0, z, x + Math.sin(a) * 0.16, h, z + Math.cos(a) * 0.14);
  }
  tuft.setAttribute('position', new THREE.Float32BufferAttribute(vertices, 3));
  tuft.computeVertexNormals();
  const weeds = [], crates = [];
  for (let i = 0; i < 8500 && weeds.length < 6500; i++) {
    const x = -325 + rand() * 650, z = -325 + rand() * 650, d = wayDist(x, z), f = forestAt(x, z), mh = mountainHeight(x, z);
    if (d < ROAD + 1.2 || mh > 14) continue;
    if (f <= 0.35 && (d > 16 || Math.max(Math.abs(x), Math.abs(z)) > 280)) continue;
    const s = 0.5 + rand() * 0.7;
    weeds.push(record(x, heightAt(x, z) - 0.015, z, s, s, s, rand() * 6.28));
  }
  for (let i = 0; i < 700 && crates.length < 280; i++) {
    const x = GRID_MIN + rand() * (GRID_MAX - GRID_MIN), z = GRID_MIN + rand() * (GRID_MAX - GRID_MIN);
    if (forestAt(x, z) > 0.3 || mountainHeight(x, z) > 0.25) continue;
    if (wayDist(x, z) < ROAD + 2.5) continue; // the road itself stays clear for wheels
    if (keep.some((p) => Math.hypot(p.x - x, p.z - z) < 8)) continue;
    const s = 0.5 + rand() * 1.0, y = heightAt(x, z);
    crates.push(record(x, y + s * 0.4, z, s * 1.5, s, s * 1.2, rand() * 6.28));
    colliders.push({ x, y: y + s * 0.15, z, r: s * 0.8, rock: true });
  }
  const grasses = scatter(tuft, cel(shared, 2, 0.12), weeds);
  scatter(boxGeo, cel(shared, 3), crates);

  // Drifting Komorebi Spirit Petals / Neon Embers: 1 InstancedMesh wrapping in a 90m volume around uFocus (0 CPU updates).
  const PETAL_COUNT = 320;
  const petalGeo = new THREE.BufferGeometry();
  petalGeo.setAttribute('position', new THREE.Float32BufferAttribute([
    0, -0.52, 0,   0.36, 0.04, 0.04,   0, 0.52, 0,
    0, -0.52, 0,   0, 0.52, 0,        -0.36, 0.04, -0.04,
  ], 3));
  petalGeo.setAttribute('uv', new THREE.Float32BufferAttribute([
    0.5, 0,   1, 0.54,   0.5, 1,
    0.5, 0,   0.5, 1,    0, 0.54,
  ], 2));
  const petals = new THREE.InstancedMesh(petalGeo, new THREE.ShaderMaterial({
    glslVersion: THREE.GLSL3,
    side: THREE.DoubleSide,
    uniforms: shared,
    vertexShader: /* glsl */ `
      ${COMMON}
      out vec3 vWorld;
      out vec2 vUv;
      out float vId;
      out float vTwinkle;
      const vec3 PBOX = vec3(90.0, 18.0, 90.0);
      void main() {
        float id = float(gl_InstanceID);
        vId = id;
        vUv = uv;
        vec4 r = hash4(id * 1.37 + 5.0);
        vec4 q = hash4(id * 2.93 + 17.0);
        vec3 p = r.xyz * PBOX;
        float vy = q.w > 0.38 ? -(0.38 + 0.34 * r.w) : (0.24 + 0.26 * r.w);
        p.xz += uWind.xy * uTime * (0.85 + 1.25 * q.y);
        p.y += uTime * vy;
        vec3 origin = vec3(uFocus.x, uFocus.y + 5.2, uFocus.z);
        p = mod(p - origin + PBOX * 0.5, PBOX) - PBOX * 0.5 + origin;
        float ph = uTime * (1.1 + 0.9 * q.x) + r.w * TAU;
        p.xz += vec2(sin(ph), cos(ph * 0.83 + q.z * 6.0)) * (0.45 + 0.45 * q.y);
        p.y += sin(ph * 1.3 + q.w * 5.0) * 0.22;

        float ground = heightAt(p.xz);
        float vis = smoothstep(ground + 0.25, ground + 0.95, p.y) * (1.0 - smoothstep(34.0, 44.5, distance(p.xz, uFocus.xz)));
        vec3 toEye = normalize(cameraPosition - p);
        vec3 camR = normalize(cross(vec3(0.0, 1.0, 0.0), toEye + vec3(1e-4, 0.0, 1e-4)));
        vec3 camU = cross(toEye, camR);
        float spin = uTime * (1.1 + 1.3 * q.x) + r.x * TAU;
        float tilt = 0.45 * sin(uTime * (1.6 + 1.3 * q.y) + r.y * TAU);
        vec3 side = cos(spin) * camR + sin(spin) * camU;
        vec3 up = (-sin(spin) * camR + cos(spin) * camU) * cos(tilt) + toEye * sin(tilt);
        float sz = (0.14 + 0.12 * q.z) * vis;
        sz = max(sz, min(distance(p, cameraPosition) * uPixel * 2.8, 0.30) * vis);
        vWorld = p + side * (position.x * sz) + up * (position.y * sz) + toEye * (position.z * sz);
        vTwinkle = 0.84 + 0.16 * sin(uTime * (2.4 + 2.2 * r.w) + id * 1.7);
        gl_Position = vis > 0.001 ? projectionMatrix * viewMatrix * vec4(vWorld, 1.0) : vec4(2.0, 2.0, 2.0, 1.0);
      }`,
    fragmentShader: /* glsl */ `
      ${COMMON}
      in vec3 vWorld;
      in vec2 vUv;
      in float vId;
      in float vTwinkle;
      layout(location = 0) out vec4 outC;
      void main() {
        vec4 q = hash4(vId * 2.93 + 17.0);
        vec3 fol = zoneFoliageAt(vWorld.xz);
        vec3 pale = zoneLeafAt(vWorld.xz);
        vec3 warmGold = vec3(1.0, 0.84, 0.42);
        vec3 sakura = vec3(1.0, 0.58, 0.76);
        vec3 cyanSpirit = mix(vec3(0.24, 0.90, 1.0), fol, 0.65);
        vec3 dayTint = q.z < 0.48 ? sakura : mix(warmGold, pale, 0.45);
        vec3 nightTint = mix(q.z < 0.36 ? warmGold : (q.z < 0.68 ? sakura : cyanSpirit), mix(fol, pale, step(0.5, q.x)), 0.38);
        float core = 1.0 - smoothstep(0.0, 0.48, length(vUv - 0.5));
        float sunLit = 0.62 + 0.46 * woodSunAt(vWorld);
        vec3 dayCol = mix(dayTint * 0.88, vec3(1.0, 0.96, 0.90), core * 0.42) * sunLit;
        vec3 nightCol = mix(nightTint * 0.92, vec3(1.0, 0.97, 0.92), core * 0.50) * vTwinkle;
        outC = vec4(hazed(mix(dayCol, nightCol, smoothstep(0.15, 0.75, uNight)), vWorld), 1.0);
      }`,
  }), PETAL_COUNT);
  dummy.position.set(0, 0, 0); dummy.rotation.set(0, 0, 0); dummy.scale.setScalar(1); dummy.updateMatrix();
  for (let i = 0; i < PETAL_COUNT; i++) petals.setMatrixAt(i, dummy.matrix);
  petals.instanceMatrix.needsUpdate = true;
  petals.frustumCulled = false;
  group.add(petals);

  // Cloud banks lit cleanly in daylight, warm coral at twilight, and neon-lit from below at night.
  const clouds = [];
  for (let i = 0; i < 28; i++) {
    const x = (rand() - 0.5) * 1100, z = (rand() - 0.5) * 1100, y = 75 + rand() * 55;
    for (let j = 0; j < 4; j++) clouds.push(record(x + j * 12, y + Math.sin(j * 1.8) * 3, z, 16 + rand() * 8, 5 + rand() * 4, 9 + rand() * 6));
  }
  instances(new THREE.IcosahedronGeometry(1, 2), cel(shared, 5), clouds);
  const sky = new THREE.Mesh(new THREE.SphereGeometry(950, 24, 16), new THREE.ShaderMaterial({
    glslVersion: THREE.GLSL3, uniforms: shared, side: THREE.BackSide, depthWrite: false,
    vertexShader: `out vec3 vDir; void main() { vDir = position; gl_Position = projectionMatrix * viewMatrix * vec4(position + cameraPosition, 1.0); }`,
    fragmentShader: `${COMMON}
      in vec3 vDir; layout(location = 0) out vec4 outC;
      float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }
      void main() {
        vec3 v = normalize(vDir);
        vec3 c = mix(uHaze * 1.25, uSkyHigh, smoothstep(-0.02, 0.5, v.y));
        float sunward = max(dot(v, uSunDir), 0.0);
        // Warm sunset/sunrise glow during the 10-minute phase transition.
        c += vec3(1.0, 0.46, 0.22) * uTwilight * pow(sunward, 3.0) * (1.0 - smoothstep(0.0, 0.55, abs(v.y))) * 0.55;
        float day = 1.0 - uNight;
        c += vec3(1.0, 0.94, 0.74) * pow(sunward, 18.0) * 0.28 * day;
        c = mix(c, vec3(1.0, 0.97, 0.86), smoothstep(0.9975, 0.9988, dot(v, uSunDir)) * day);
        vec2 star = floor(v.xz / max(v.y, 0.05) * 90.0);
        c += vec3(0.75, 0.85, 1.0) * step(0.9975, hash(star)) * smoothstep(0.12, 0.4, v.y) * smoothstep(0.35, 0.85, uNight);
        c = mix(c, vec3(0.86, 0.92, 1.0), smoothstep(0.9990, 0.9992, dot(v, uSunDir)) * uNight);
        // a far skyline: blocks against the horizon glow, rooms lit up at night
        float a = atan(v.x, v.z) * 46.0, top = 0.015 + 0.085 * hash(vec2(floor(a), 3.0)) * hash(vec2(floor(a * 0.31), 9.0));
        if (v.y < top) {
          vec2 room = vec2(fract(a) * 5.0, v.y * 420.0);
          float on = step(0.86, hash(floor(room) + floor(a))) * step(0.25, fract(room.x)) * step(0.3, fract(room.y)) * smoothstep(0.3, 0.8, uNight);
          vec3 daySil = mix(uHaze * 0.72, uSkyHigh * 0.85, 0.35);
          vec3 nightSil = mix(uSkyHigh * 0.7, uHaze, 0.45);
          c = mix(mix(daySil, nightSil, uNight), mix(uLeafLight, uFoliage, step(0.7, hash(floor(room)))) * 0.8, on);
        }
        outC = vec4(c, 1.0);
      }`,
  }));
  sky.renderOrder = -10;
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
    shared.uHazeDensity.value = 0.0034 + 0.0044 * n;
    shared.uGlazeMul.value.setRGB(0.58, 0.64, 0.75).lerp(targetColor.setRGB(0.65, 0.72, 0.75), n);
    nightMix.copy(shared.uFoliage.value).multiplyScalar(0.42);
    shared.uRim.value.setRGB(0.22, 0.21, 0.16).lerp(nightMix, n);
    nightMix.copy(shared.uHaze.value).multiplyScalar(0.3);
    shared.uBounce.value.setRGB(0.11, 0.11, 0.08).lerp(nightMix, n);
  }
  return { group, shade, colliders, counts: { trees: towers.length, forestTrees: placedTrees.length, boulders: boulders.length, grass: weeds.length, flowers: lights.length, rocks: crates.length, petals: PETAL_COUNT }, update,
    setQuality(detail) {
      for (const g of grasses) g.count = Math.floor(g.userData.full * detail);
      petals.count = Math.floor(PETAL_COUNT * detail);
    } };
}
