// Solid, reusable scenery for the night city: blocks on a road grid, towers with lit windows, neon
// signs, street lamps and lane studs, crates, weeds and a smog-lit sky. Each district is its own
// instanced draw so a district behind the camera is skipped. Every placement uses heightAt.
// Building colour comes from where it stands (four districts). The story chapter still tints the
// sky and the figures. Light is paint: a lit window is drawn in its own colour, never added as lighting.
import * as THREE from 'three';
import { COMMON } from '../oilpaint/common.js';
import { heightAt, wayDist, clearingAt, rng, way, ROAD } from './land.js';
import { CHAPTERS, chapterAt } from '../game/chapters.js';
import { LEVELS, GOAL } from '../game/levels.js';
import { DUELS } from '../game/duel.js';
import { blockLots, AVENUES, STREETS, GRID_MIN, GRID_MAX, ZONES, ZONE_SPLIT, zoneAt } from './grid.js';

// Roles: 0 tower, 1 sign, 2 weeds, 3 crate, 4 lamp head / lane stud, 5 smog, 6 pole.
function cel(shared, role, sway = 0) {
  return new THREE.ShaderMaterial({
    // every solid here is closed, so only its outside is drawn; the weeds are single blades and show both faces
    glslVersion: THREE.GLSL3, side: role === 2 ? THREE.DoubleSide : THREE.FrontSide,
    uniforms: { ...shared, uRole: { value: role }, uSway: { value: sway } },
    vertexShader: /* glsl */ `
      ${COMMON}
      uniform float uSway;
      out vec3 vWorld; out vec3 vNormal; out float vSeed; out vec3 vObj; out vec3 vObjN; flat out vec3 vSize;
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
      in vec3 vWorld; in vec3 vNormal; in float vSeed; in vec3 vObj; in vec3 vObjN; flat in vec3 vSize;
      layout(location = 0) out vec4 outC;
      float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }
      void main() {
        vec3 fol = zonePick(vWorld.xz, uZoneFoliage[0], uZoneFoliage[1], uZoneFoliage[2], uZoneFoliage[3]);
        vec3 pale = zonePick(vWorld.xz, uZoneLeaf[0], uZoneLeaf[1], uZoneLeaf[2], uZoneLeaf[3]);
        vec3 pave = zonePick(vWorld.xz, uZoneGrass[0], uZoneGrass[1], uZoneGrass[2], uZoneGrass[3]);
        vec3 n = normalize(vNormal);
        vec3 base = vec3(0.09, 0.10, 0.13), glow = vec3(0.0);
        float own = 0.0; // how much of this spot is its own light rather than a lit surface
        if (uRole < 0.5) {
          base = mix(vec3(0.07, 0.08, 0.11), vec3(0.14, 0.15, 0.20), vSeed);
          float side = step(abs(vObjN.y), 0.5);
          float u = abs(vObjN.x) > 0.5 ? vObj.z : vObj.x;
          vec2 cell = vec2(u / 1.15, vObj.y / 1.7), f = fract(cell);
          float pane = step(0.16, f.x) * step(f.x, 0.84) * step(0.24, f.y) * step(f.y, 0.80) * side;
          float h = hash(floor(cell) + vSeed * 91.7);
          // most lit rooms are the district's pale light, a few its neon; the rest are dark glass
          glow = mix(pale, fol, step(0.9, h)) * (0.7 + 0.3 * h);
          own = pane * step(0.64, h);
          base = mix(base, base * 1.7 + 0.02, pane * (1.0 - own));
          // a line of neon under the roof
          float trim = step(vSize.y * 0.5 - 0.28, vObj.y) * side;
          glow = mix(glow, fol, trim); own = max(own, trim);
          // Dark corner piers and lit ground-floor bays give the blocks a human scale.
          float edge = step(vSize.x * 0.5 - 0.18, abs(vObj.x)) + step(vSize.z * 0.5 - 0.18, abs(vObj.z));
          base *= mix(1.0, 0.62, min(1.0, edge) * side);
          float shop = side * step(-vSize.y * 0.5 + 3.7, vObj.y) * step(vObj.y, -vSize.y * 0.5 + 5.8);
          shop *= step(0.12, fract(u / 2.2)) * step(fract(u / 2.2), 0.88);
          glow = mix(glow, pale * 0.72, shop); own = max(own, shop * 0.78);
        } else if (uRole < 1.5) {
          // a sign: bars of its colour that read as characters, with a tired flicker
          float bars = step(0.42, fract(vObj.y * 1.25 + vSeed * 7.0)) * step(abs(vObj.z), vSize.z * 0.38);
          float flick = 0.86 + 0.14 * step(0.12, fract(sin(floor(uTime * 9.0) + vSeed * 40.0) * 91.3));
          glow = mix(fol, pale, step(0.5, vSeed)) * mix(0.42, 1.0, bars) * flick; own = 1.0;
        } else if (uRole < 2.5) {
          base = mix(pave * 1.5, fol * 0.3, vSeed * 0.6);
        } else if (uRole < 3.5) {
          base = mix(vec3(0.10, 0.11, 0.15), vec3(0.17, 0.18, 0.22), vSeed);
          glow = vec3(0.99, 0.87, 0.08);
          own = step(abs(vObj.y - vSize.y * 0.12), vSize.y * 0.07) * step(0.5, fract((vObj.x + vObj.z) * 2.2));
        } else if (uRole < 4.5) {
          glow = mix(fol, pale, step(0.5, vSeed)); own = 1.0;
        } else if (uRole < 5.5) {
          base = mix(uSkyHigh * 2.2 + 0.02, uHaze * 1.25, max(-n.y, 0.0)); // smog, lit from the streets below
        }
        float light = dot(n, uSunDir) * 0.5 + 0.5;
        float shade = uRole > 4.5 && uRole < 5.5 ? 1.0 : woodSunAt(vWorld + n * 0.1);
        float band = mix(0.65, 0.85, smoothstep(0.28, 0.31, light));
        band = mix(band, 1.12, smoothstep(0.65, 0.68, light));
        vec3 c = base * band * mix(0.79, 1.0, shade);
        float rim = abs(dot(n, normalize(cameraPosition - vWorld)));
        if (uRole < 0.5 || uRole > 2.5 && uRole < 3.5 || uRole > 5.5) c *= mix(0.72, 1.0, smoothstep(0.02, 0.16, rim));
        c = mix(c, glow, own);
        outC = vec4(hazed(c, vWorld), 1.0);
      }`,
  });
}

export function createAnimeWorld(shared) {
  ZONES.forEach((zone, i) => {
    const c = CHAPTERS[zone.chapter];
    shared.uZoneGrass.value[i].set(...c.grass);
    shared.uZoneFoliage.value[i].set(...c.foliage);
    shared.uZoneLeaf.value[i].set(...c.leafLight);
    shared.uZonePath.value[i].set(...c.path);
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
    // The sun's pass draws the shade group from a camera that must see every tower.
    mesh.frustumCulled = parent === group;
    parent.add(mesh);
    return mesh;
  }
  // ponytail: one mesh per district, culled by the camera. Stream buffers if a district shows up in the frame profile.
  function scatter(geometry, material, items) {
    const bins = [[], [], [], []];
    for (const p of items) bins[zoneAt(p.x, p.z)].push(p);
    return bins.filter((bin) => bin.length).map((bin) => {
      const mesh = instances(geometry, material, bin);
      mesh.userData.full = bin.length;
      return mesh;
    });
  }

  // Nothing is built where a fight happens: every spawn, duel, cairn and gate keeps its room.
  const keep = [GOAL, ...DUELS, ...LEVELS.flatMap((L) => [L.checkpoint, ...L.spots, ...(L.gate ? [L.gate] : [])])];
  const SINK = 3; // metres a tower goes into the ground, so a slope never shows under it
  const towers = [], signs = [];
  const capH = [28, 16, 18, 9];
  for (const lot of blockLots()) {
    const x = lot.x + (rand() - 0.5) * 2, z = lot.z + (rand() - 0.5) * 2;
    const w = 4.2 + rand() * 2.4, d = 4.2 + rand() * 2.4, reach = Math.max(w, d) * 0.5;
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
  }
  const boxGeo = new THREE.BoxGeometry(1, 1, 1);
  // Drawn nearest the street first: seen from it, the towers behind are mostly hidden and their pixels are never shaded.
  towers.sort((a, b) => a.wd - b.wd);
  scatter(boxGeo, cel(shared, 0), towers);
  scatter(boxGeo, cel(shared, 1), signs);
  if (towers.length) instances(boxGeo, new THREE.MeshBasicMaterial(), towers, shade);

  // Lamps lean over the way from alternate sides; lit dashes mark both of its edges.
  const poles = [], lights = [];
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
  // The same dashes and lamps along every avenue and street. Skip the crossing itself; the trail covers its own.
  const crossing = (x, z, avenues) => (avenues ? STREETS : AVENUES).some((v) => Math.abs((avenues ? z : x) - v) < 6);
  for (const x of AVENUES) {
    for (let z = GRID_MIN, n = 0; z <= GRID_MAX; z += 8, n++) {
      if (crossing(x, z, true)) continue;
      for (const side of [-1, 1]) {
        const lx = x + (ROAD - 0.55) * side;
        lights.push(record(lx, heightAt(lx, z) + 0.045, z, 0.1, 0.04, 0.55, 0));
      }
      if (n % 4 !== 0) continue;
      const side = n % 8 === 0 ? 1 : -1, lx = x + (ROAD + 2.4) * side, y = heightAt(lx, z);
      poles.push(record(lx, y + 2.1, z, 0.07, 4.2, 0.07));
      lights.push(record(lx - 0.7 * side, y + 4.2, z, 1.5, 0.12, 0.3, Math.PI / 2));
    }
  }
  for (const z of STREETS) {
    for (let x = GRID_MIN, n = 0; x <= GRID_MAX; x += 8, n++) {
      if (crossing(x, z, false)) continue;
      for (const side of [-1, 1]) {
        const lz = z + (ROAD - 0.55) * side;
        lights.push(record(x, heightAt(x, lz) + 0.045, lz, 0.55, 0.04, 0.1, 0));
      }
      if (n % 4 !== 0) continue;
      const side = n % 8 === 0 ? 1 : -1, lz = z + (ROAD + 2.4) * side, y = heightAt(x, lz);
      poles.push(record(x, y + 2.1, lz, 0.07, 4.2, 0.07));
      lights.push(record(x, y + 4.2, lz - 0.7 * side, 0.3, 0.12, 1.5, 0));
    }
  }
  scatter(new THREE.CylinderGeometry(1, 1, 1, 6), cel(shared, 6), poles);
  scatter(boxGeo, cel(shared, 4), lights);

  // A clump is six broad blades in one geometry, with a stable seed and a bend at its tip.
  const tuft = new THREE.BufferGeometry(), vertices = [];
  for (let i = 0; i < 6; i++) {
    const a = i * 2.39996, x = Math.sin(a) * 0.15, z = Math.cos(a) * 0.15, h = 0.36 + (i % 3) * 0.11;
    vertices.push(x - 0.06, 0, z, x + 0.06, 0, z, x + Math.sin(a) * 0.16, h, z + Math.cos(a) * 0.14);
  }
  tuft.setAttribute('position', new THREE.Float32BufferAttribute(vertices, 3));
  tuft.computeVertexNormals();
  const weeds = [], crates = [];
  for (let i = 0; i < 8000; i++) {
    const x = GRID_MIN + rand() * (GRID_MAX - GRID_MIN), z = GRID_MIN + rand() * (GRID_MAX - GRID_MIN), d = wayDist(x, z);
    if (d < ROAD + 1.2 || d > 16) continue;
    const s = 0.5 + rand() * 0.7;
    weeds.push(record(x, heightAt(x, z) - 0.015, z, s, s, s, rand() * 6.28));
  }
  for (let i = 0; i < 700 && crates.length < 280; i++) {
    const x = GRID_MIN + rand() * (GRID_MAX - GRID_MIN), z = GRID_MIN + rand() * (GRID_MAX - GRID_MIN);
    if (wayDist(x, z) < ROAD + 2.5) continue; // the road itself stays clear for wheels
    if (keep.some((p) => Math.hypot(p.x - x, p.z - z) < 8)) continue;
    const s = 0.5 + rand() * 1.0, y = heightAt(x, z);
    crates.push(record(x, y + s * 0.4, z, s * 1.5, s, s * 1.2, rand() * 6.28));
    colliders.push({ x, y: y + s * 0.15, z, r: s * 0.8, rock: true });
  }
  const grasses = scatter(tuft, cel(shared, 2, 0.12), weeds);
  scatter(boxGeo, cel(shared, 3), crates);

  // Smog banks are real silhouettes, lit from below, so the skyline always has a legible backdrop.
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
        // the city's glow low down, night above it
        vec3 c = mix(uHaze * 1.25, uSkyHigh, smoothstep(-0.02, 0.5, v.y));
        vec2 star = floor(v.xz / max(v.y, 0.05) * 90.0);
        c += vec3(0.75, 0.85, 1.0) * step(0.9975, hash(star)) * smoothstep(0.12, 0.4, v.y);
        c = mix(c, vec3(0.86, 0.92, 1.0), smoothstep(0.9990, 0.9992, dot(v, uSunDir))); // the moon
        // a far skyline: blocks against the glow, a few rooms still lit
        float a = atan(v.x, v.z) * 46.0, top = 0.015 + 0.085 * hash(vec2(floor(a), 3.0)) * hash(vec2(floor(a * 0.31), 9.0));
        if (v.y < top) {
          vec2 room = vec2(fract(a) * 5.0, v.y * 420.0);
          float on = step(0.86, hash(floor(room) + floor(a))) * step(0.25, fract(room.x)) * step(0.3, fract(room.y));
          c = mix(mix(uSkyHigh * 0.7, uHaze, 0.45), mix(uLeafLight, uFoliage, step(0.7, hash(floor(room)))) * 0.8, on);
        }
        outC = vec4(c, 1.0);
      }`,
  }));
  sky.renderOrder = -10;
  sky.frustumCulled = false;
  group.add(sky);

  let active = -1;
  const targetColor = new THREE.Color();
  function update(dt, z, cleared, roam) {
    const index = chapterAt(z, cleared, roam), c = CHAPTERS[index];
    if (index !== active) {
      active = index;
      document.documentElement.dataset.chapter = String(index);
      document.documentElement.dataset.style = c.style;
      document.documentElement.style.setProperty('--chapter-accent', c.color);
      dispatchEvent(new CustomEvent('chapterchange', { detail: { index, ...c } }));
    }
    const blend = 1 - Math.exp(-dt * 2);
    for (const [uniform, key] of [['uGrass', 'grass'], ['uFoliage', 'foliage'], ['uLeafLight', 'leafLight'], ['uPath', 'path'], ['uSkyHigh', 'sky'], ['uHaze', 'haze']]) {
      shared[uniform].value.lerp(targetColor.setRGB(...c[key]), blend);
    }
    shared.uHazeSun.value.copy(shared.uHaze.value).lerp(targetColor.setRGB(0.7, 0.8, 1.0), 0.18);
    // figures take the district's neon as their back light, and its glow into their shade
    shared.uRim.value.copy(shared.uFoliage.value).multiplyScalar(0.42);
    shared.uBounce.value.copy(shared.uHaze.value).multiplyScalar(0.3);
  }
  return { group, shade, colliders, counts: { trees: towers.length, grass: weeds.length, flowers: lights.length, rocks: crates.length }, update,
    setQuality(detail) { for (const g of grasses) g.count = Math.floor(g.userData.full * detail); } };
}
