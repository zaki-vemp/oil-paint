// Solid, reusable scenery for the spirit trail: clustered tree crowns, tapered trunks, grass tufts,
// wildflowers and mossy rocks. Instancing keeps the whole wood to a handful of draws. Every placement
// uses the land's heightAt, and every colour comes from the active chapter's shared palette.
import * as THREE from 'three';
import { mergeGeometries } from 'three/addons/utils/BufferGeometryUtils.js';
import { COMMON } from '../oilpaint/common.js';
import { heightAt, wayDist, clearingAt, rng } from './land.js';
import { CHAPTERS, chapterAt } from '../game/chapters.js';

function cel(shared, role, sway = 0) {
  return new THREE.ShaderMaterial({
    glslVersion: THREE.GLSL3, side: THREE.DoubleSide,
    uniforms: { ...shared, uRole: { value: role }, uSway: { value: sway } },
    vertexShader: /* glsl */ `
      ${COMMON}
      uniform float uSway;
      out vec3 vWorld; out vec3 vNormal; out float vSeed;
      void main() {
        vec4 w = instanceMatrix * vec4(position, 1.0);
        vSeed = fract(sin(instanceMatrix[3].x * 17.3 + instanceMatrix[3].z * 31.7) * 43758.5);
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
      in vec3 vWorld; in vec3 vNormal; in float vSeed;
      layout(location = 0) out vec4 outC;
      void main() {
        vec3 n = normalize(vNormal);
        vec3 base = uFoliage;
        if (uRole < 0.5) base = mix(vec3(0.27, 0.23, 0.18), vec3(0.48, 0.40, 0.28), vSeed);
        else if (uRole < 1.5) base = mix(uFoliage, uLeafLight, vSeed * 0.45);
        else if (uRole < 2.5) base = mix(uGrass * 0.8, uLeafLight, vSeed * 0.65);
        else if (uRole < 3.5) base = mix(vec3(0.38, 0.47, 0.43), uGrass * 0.8, max(n.y, 0.0) * 0.7);
        else if (uRole < 4.5) base = mix(vec3(1.0, 0.92, 0.64), vec3(1.0, 0.63, 0.51), step(0.65, vSeed));
        else base = mix(vec3(0.84, 0.90, 0.87), vec3(1.0, 0.98, 0.87), max(n.y, 0.0));
        float light = dot(n, uSunDir) * 0.5 + 0.5;
        float shade = uRole > 4.5 ? 1.0 : woodSunAt(vWorld + n * 0.1);
        float band = mix(0.65, 0.85, smoothstep(0.28, 0.31, light));
        band = mix(band, 1.12, smoothstep(0.65, 0.68, light));
        vec3 c = base * band * mix(0.79, 1.0, shade);
        float rim = abs(dot(n, normalize(cameraPosition - vWorld)));
        if (uRole < 4.5 && uRole != 2.0) c *= mix(0.72, 1.0, smoothstep(0.02, 0.16, rim));
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

  const trunks = [], crowns = [], branches = [], roots = [];
  // Keep the trail open to the sky. Trees frame its bends instead of hiding the next encounter.
  for (let i = 0; i < 1900; i++) {
    const x = (rand() - 0.5) * 205, z = (rand() - 0.5) * 300;
    if (wayDist(x, z) < 5.6 || clearingAt(x, z) > 0.4) continue;
    if (trunks.some(p => Math.hypot(p.x - x, p.z - z) < 5.5)) continue;
    const y = heightAt(x, z), h = 6.8 + rand() * 5.4, r = 0.26 + rand() * 0.23, a = rand() * Math.PI * 2;
    trunks.push(record(x, y + h / 2, z, r, h, r, a));
    colliders.push({ x, y, z, r: r * 1.1, h });
    for (let j = 0; j < 3; j++) {
      const t = a + j * Math.PI * 2 / 3;
      const spread = 1.1 + rand() * 0.8;
      crowns.push(record(x + Math.sin(t) * spread, y + h - 0.2 + rand() * 1.4, z + Math.cos(t) * spread,
        2.5 + rand() * 1.3, 1.7 + rand() * 1.2, 2.4 + rand() * 1.2, t));
      branches.push(record(x + Math.sin(t) * 0.65, y + h * 0.71, z + Math.cos(t) * 0.65, r * 0.45, h * 0.4, r * 0.45, t, 0.7));
      roots.push(record(x + Math.sin(t) * 0.3, y + 0.12, z + Math.cos(t) * 0.3, r * 0.7, 0.9, r * 0.7, t, 0.95));
    }
  }
  const trunkGeo = new THREE.CylinderGeometry(0.65, 1.1, 1, 7);
  const crownGeo = new THREE.IcosahedronGeometry(1, 1);
  const bark = cel(shared, 0);
  instances(trunkGeo, bark, [...trunks, ...branches, ...roots]);
  instances(crownGeo, cel(shared, 1, 0.022), crowns);
  const casterMat = new THREE.MeshBasicMaterial();
  instances(trunkGeo, casterMat, trunks, shade);
  instances(crownGeo, casterMat, crowns, shade);

  // A clump is six broad blades in one geometry, with a stable seed and a bend at its tip.
  const tuft = new THREE.BufferGeometry(), vertices = [];
  for (let i = 0; i < 6; i++) {
    const a = i * 2.39996, x = Math.sin(a) * 0.15, z = Math.cos(a) * 0.15, h = 0.36 + (i % 3) * 0.11;
    vertices.push(x - 0.06, 0, z, x + 0.06, 0, z, x + Math.sin(a) * 0.16, h, z + Math.cos(a) * 0.14);
  }
  tuft.setAttribute('position', new THREE.Float32BufferAttribute(vertices, 3));
  tuft.computeVertexNormals();
  const grasses = [], flowers = [], rocks = [];
  for (let i = 0; i < 25000; i++) {
    const x = (rand() - 0.5) * 100, z = -115 + rand() * 220, d = wayDist(x, z);
    if (d < 1.9 || d > 46) continue;
    const y = heightAt(x, z), s = 0.65 + rand() * 0.9;
    grasses.push(record(x, y - 0.015, z, s, s, s, rand() * 6.28));
    if (i % 19 === 0 && d < 20) flowers.push(record(x, y + 0.28, z, 0.15, 0.15, 0.15, rand() * 6.28));
  }
  for (let i = 0; i < 160; i++) {
    const x = (rand() - 0.5) * 90, z = -110 + rand() * 215;
    if (wayDist(x, z) < 4) continue;
    const s = 0.5 + rand() * 1.0, y = heightAt(x, z);
    rocks.push(record(x, y + s * 0.15, z, s, s * 0.7, s * 0.8, rand() * 6.28));
    colliders.push({ x, y: y + s * 0.15, z, r: s * 0.8, rock: true });
  }
  const grass = instances(tuft, cel(shared, 2, 0.2), grasses);
  instances(new THREE.IcosahedronGeometry(1, 0), cel(shared, 3), rocks);
  const petals = [];
  for (let i = 0; i < 5; i++) {
    const a = i * Math.PI * 2 / 5;
    const p = new THREE.SphereGeometry(1, 5, 3);
    p.scale(0.62, 0.22, 0.45);
    p.rotateY(-a);
    p.translate(Math.cos(a) * 0.5, 0, Math.sin(a) * 0.5);
    petals.push(p);
  }
  instances(mergeGeometries(petals), cel(shared, 4), flowers);
  for (const p of petals) p.dispose();

  // Clouds are real silhouettes, so the canopy always has a calm, legible backdrop.
  const clouds = [];
  for (let i = 0; i < 28; i++) {
    const x = (rand() - 0.5) * 1100, z = (rand() - 0.5) * 1100, y = 65 + rand() * 55;
    for (let j = 0; j < 4; j++) clouds.push(record(x + j * 12, y + Math.sin(j * 1.8) * 3, z, 16 + rand() * 8, 5 + rand() * 4, 9 + rand() * 6));
  }
  instances(new THREE.IcosahedronGeometry(1, 2), cel(shared, 5), clouds);
  const sky = new THREE.Mesh(new THREE.SphereGeometry(950, 24, 16), new THREE.ShaderMaterial({
    glslVersion: THREE.GLSL3, uniforms: shared, side: THREE.BackSide, depthWrite: false,
    vertexShader: `out vec3 vDir; void main() { vDir = position; gl_Position = projectionMatrix * viewMatrix * vec4(position + cameraPosition, 1.0); }`,
    fragmentShader: `${COMMON}
      in vec3 vDir; layout(location = 0) out vec4 outC;
      void main() {
        vec3 v = normalize(vDir);
        vec3 c = mix(uHaze, uSkyHigh, smoothstep(-0.02, 0.65, v.y));
        float sun = dot(v, uSunDir);
        c = mix(c, vec3(1.0, 0.96, 0.76), smoothstep(0.9990, 0.9992, sun));
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
    shared.uHazeSun.value.copy(shared.uHaze.value).lerp(targetColor.setRGB(1.0, 0.94, 0.75), 0.18);
  }
  return { group, shade, colliders, counts: { trees: trunks.length, grass: grasses.length, flowers: flowers.length, rocks: rocks.length }, update,
    setQuality(low) { grass.count = low ? Math.floor(grasses.length * 0.4) : grasses.length; } };
}
