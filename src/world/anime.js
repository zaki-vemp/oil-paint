// Solid, reusable scenery for the trail, now a night city: towers with lit windows, neon signs on their
// walls, street lamps and lane studs along the way, crates, weeds in the cracks and a smog-lit sky.
// Instancing keeps the whole district to a handful of draws. Every placement uses the land's heightAt,
// and every colour comes from the active chapter's shared palette: `foliage` and `leafLight` are the
// district's two neons, `grass` its pavement, `path` its road. Light is paint here too: a lit window or a
// sign is simply drawn in its own colour, never added as lighting.
import * as THREE from 'three';
import { COMMON } from '../oilpaint/common.js';
import { heightAt, wayDist, clearingAt, rng, way } from './land.js';
import { CHAPTERS, chapterAt } from '../game/chapters.js';
import { LEVELS, GOAL } from '../game/levels.js';
import { DUELS } from '../game/duel.js';

// Roles: 0 tower, 1 sign, 2 weeds, 3 crate, 4 lamp head / lane stud, 5 smog, 6 pole.
function cel(shared, role, sway = 0) {
  return new THREE.ShaderMaterial({
    glslVersion: THREE.GLSL3, side: THREE.DoubleSide,
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
          glow = mix(uLeafLight, uFoliage, step(0.9, h)) * (0.7 + 0.3 * h);
          own = pane * step(0.64, h);
          base = mix(base, base * 1.7 + 0.02, pane * (1.0 - own));
          // a line of neon under the roof
          float trim = step(vSize.y * 0.5 - 0.28, vObj.y) * side;
          glow = mix(glow, uFoliage, trim); own = max(own, trim);
        } else if (uRole < 1.5) {
          // a sign: bars of its colour that read as characters, with a tired flicker
          float bars = step(0.42, fract(vObj.y * 1.25 + vSeed * 7.0)) * step(abs(vObj.z), vSize.z * 0.38);
          float flick = 0.86 + 0.14 * step(0.12, fract(sin(floor(uTime * 9.0) + vSeed * 40.0) * 91.3));
          glow = mix(uFoliage, uLeafLight, step(0.5, vSeed)) * mix(0.42, 1.0, bars) * flick; own = 1.0;
        } else if (uRole < 2.5) {
          base = mix(uGrass * 1.5, uFoliage * 0.3, vSeed * 0.6);
        } else if (uRole < 3.5) {
          base = mix(vec3(0.10, 0.11, 0.15), vec3(0.17, 0.18, 0.22), vSeed);
          glow = vec3(0.99, 0.87, 0.08);
          own = step(abs(vObj.y - vSize.y * 0.12), vSize.y * 0.07) * step(0.5, fract((vObj.x + vObj.z) * 2.2));
        } else if (uRole < 4.5) {
          glow = mix(uFoliage, uLeafLight, step(0.5, vSeed)); own = 1.0;
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
    parent.add(mesh);
    return mesh;
  }

  // Nothing is built where a fight happens: every spawn, duel, cairn and gate keeps its room.
  const keep = [GOAL, ...DUELS, ...LEVELS.flatMap((L) => [L.checkpoint, ...L.spots, ...(L.gate ? [L.gate] : [])])];
  const SINK = 3; // metres a tower goes into the ground, so a slope never shows under it
  const towers = [], signs = [];
  // The street stays open to the sky: low blocks front it and the towers rise behind them.
  for (let i = 0; i < 2600; i++) {
    const x = (rand() - 0.5) * 230, z = (rand() - 0.5) * 320, wd = wayDist(x, z);
    const w = 3.5 + rand() * 3.5, d = 3.5 + rand() * 3.5, reach = Math.max(w, d) * 0.75;
    if (wd < 7 + reach || clearingAt(x, z) > 0.4) continue;
    if (keep.some((p) => Math.hypot(p.x - x, p.z - z) < 7.5 + reach)) continue;
    if (towers.some((p) => Math.hypot(p.x - x, p.z - z) < 8.5)) continue;
    const y = heightAt(x, z), h = 6 + rand() * 8 + Math.min(24, Math.max(0, wd - 10) * 0.8) * rand(), ry = (rand() - 0.5) * 0.6;
    towers.push(record(x, y + (h - SINK) / 2, z, w, h + SINK, d, ry));
    colliders.push({ x, y: y + (h - SINK) / 2, z, box: [w / 2, (h + SINK) / 2, d / 2], ry });
    for (let j = rand() < 0.45 ? 2 : 1; j > 0; j--) {
      // a wall of the four, and a slab standing just off it
      const k = Math.floor(rand() * 4), alongX = k % 2 === 0, out = (k < 2 ? 1 : -1) * ((alongX ? w : d) / 2 + 0.16);
      const nx = alongX ? Math.cos(ry) : Math.sin(ry), nz = alongX ? -Math.sin(ry) : Math.cos(ry);
      const sh = Math.min(h * 0.5, 2.4 + rand() * 4), sw = Math.min((alongX ? d : w) * 0.4, 0.8 + rand() * 1.2);
      signs.push(record(x + nx * out, y + sh / 2 + 2.4 + rand() * Math.max(0, h - sh - 3.2), z + nz * out, 0.2, sh, sw, alongX ? ry : ry + Math.PI / 2));
    }
  }
  const boxGeo = new THREE.BoxGeometry(1, 1, 1);
  instances(boxGeo, cel(shared, 0), towers);
  instances(boxGeo, cel(shared, 1), signs);
  instances(boxGeo, new THREE.MeshBasicMaterial(), towers, shade);

  // Lamps lean over the way from alternate sides; lit dashes mark both of its edges.
  const poles = [], lights = [];
  for (let i = 2; i < way.length - 1; i += 3) {
    const [ax, az] = way[i], [bx, bz] = way[i + 1], l = Math.hypot(bx - ax, bz - az) || 1, nx = (bz - az) / l, nz = -(bx - ax) / l;
    for (const s of [-1, 1]) {
      const x = ax + nx * 2.5 * s, z = az + nz * 2.5 * s;
      lights.push(record(x, heightAt(x, z) + 0.03, z, 0.12, 0.05, 0.6, Math.atan2(bx - ax, bz - az))); // a dash along the lane
    }
    if (i % 12 !== 2) continue;
    const s = i % 24 === 2 ? 1 : -1, x = ax + nx * 4 * s, z = az + nz * 4 * s, y = heightAt(x, z);
    poles.push(record(x, y + 2.1, z, 0.07, 4.2, 0.07));
    lights.push(record(x - nx * 0.45 * s, y + 4.2, z - nz * 0.45 * s, 1.0, 0.12, 0.3, Math.atan2(-nz, nx)));
  }
  instances(new THREE.CylinderGeometry(1, 1, 1, 6), cel(shared, 6), poles);
  instances(boxGeo, cel(shared, 4), lights);

  // A clump is six broad blades in one geometry, with a stable seed and a bend at its tip.
  const tuft = new THREE.BufferGeometry(), vertices = [];
  for (let i = 0; i < 6; i++) {
    const a = i * 2.39996, x = Math.sin(a) * 0.15, z = Math.cos(a) * 0.15, h = 0.36 + (i % 3) * 0.11;
    vertices.push(x - 0.06, 0, z, x + 0.06, 0, z, x + Math.sin(a) * 0.16, h, z + Math.cos(a) * 0.14);
  }
  tuft.setAttribute('position', new THREE.Float32BufferAttribute(vertices, 3));
  tuft.computeVertexNormals();
  const weeds = [], crates = [];
  for (let i = 0; i < 7000; i++) {
    const x = (rand() - 0.5) * 100, z = -115 + rand() * 220, d = wayDist(x, z);
    if (d < 3.2 || d > 46) continue;
    const s = 0.5 + rand() * 0.7;
    weeds.push(record(x, heightAt(x, z) - 0.015, z, s, s, s, rand() * 6.28));
  }
  for (let i = 0; i < 160; i++) {
    const x = (rand() - 0.5) * 90, z = -110 + rand() * 215;
    if (wayDist(x, z) < 4) continue;
    const s = 0.5 + rand() * 1.0, y = heightAt(x, z);
    crates.push(record(x, y + s * 0.4, z, s * 1.5, s, s * 1.2, rand() * 6.28));
    colliders.push({ x, y: y + s * 0.15, z, r: s * 0.8, rock: true });
  }
  const grass = instances(tuft, cel(shared, 2, 0.12), weeds);
  instances(boxGeo, cel(shared, 3), crates);

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
    setQuality(low) { grass.count = low ? Math.floor(weeds.length * 0.4) : weeds.length; } };
}
