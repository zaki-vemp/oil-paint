// Validate imported GLBs before shipping: finite geometry/animation data, embedded buffers, real rigs and clips.
// This catches failed legacy Blender conversions (including a hidden character exporting as headgear alone).
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const publicDir = fileURLToPath(new URL('../public/', import.meta.url));
const streetDir = process.argv[2] ?? resolve(publicDir, 'street');
const street = [
  'terminal', 'supply-crate', 'power-core', 'beacon', 'barricade', 'noodle-stall', 'echo-shrine',
  'cyber-katana', 'ren-odachi', 'spirit-lantern',
  'coupe', 'sports', 'van', 'motorbike', 'scooter', 'rider',
  'resident', 'mechanic', 'courier', 'executive', 'vendor',
  'foe-punk', 'foe-enforcer', 'foe-gunner', 'foe-blade', 'foe-mech',
];
for (const file of ['people/citizen-female.glb', 'people/citizen-male.glb', 'people/runner.glb', 'vehicles/eclipse.glb', ...street.map(name => `street/${name}.glb`)]) {
  const authored = file.startsWith('street/');
  const data = readFileSync(authored ? resolve(streetDir, file.slice('street/'.length)) : resolve(publicDir, file));
  assert.equal(data.readUInt32LE(0), 0x46546c67, file + ' GLB');
  assert.equal(data.readUInt32LE(4), 2, file + ' glTF 2');
  assert.equal(data.readUInt32LE(8), data.length, file + ' length');
  const size = data.readUInt32LE(12), g = JSON.parse(data.subarray(20, 20 + size));
  const bin = data.subarray(28 + size);
  assert(g.asset.copyright, file + ' embeds credit');
  assert(g.buffers.every(b => !b.uri), file + ' has embedded buffers');
  let triangles = 0;
  for (const mesh of g.meshes) for (const p of mesh.primitives) {
    const a = g.accessors[p.attributes.POSITION];
    assert.equal(a.type, 'VEC3');
    assert(a.min.every(Number.isFinite) && a.max.every(Number.isFinite), file + ' bounds');
    triangles += g.accessors[p.indices].count / 3;
  }
  assert(triangles > 500, file + ' contains a complete model');
  for (const a of g.accessors) {
    if (a.componentType !== 5126) continue;
    const v = g.bufferViews[a.bufferView], components = { SCALAR: 1, VEC2: 2, VEC3: 3, VEC4: 4, MAT4: 16 }[a.type];
    const offset = (v.byteOffset ?? 0) + (a.byteOffset ?? 0), stride = v.byteStride ?? components * 4;
    for (let i = 0; i < a.count; i++) for (let j = 0; j < components; j++) {
      assert(Number.isFinite(bin.readFloatLE(offset + i * stride + j * 4)), file + ' finite vertex/skin/animation data');
    }
  }
  const authoredRig = /^street\/(resident|mechanic|courier|executive|vendor|foe-punk|foe-enforcer|foe-gunner|foe-blade|foe-mech)\.glb$/.test(file);
  if (file.startsWith('people/') || authoredRig) {
    const minJoints = authoredRig ? 18 : 20;
    assert(g.skins?.some(s => s.joints.length >= minJoints), file + ' has a humanoid skeleton');
    const extraClips = {
      'street/foe-gunner.glb': ['Idle_Shoot'],
      'street/foe-blade.glb': ['Sword_Slash'],
      'street/foe-mech.glb': ['Attack'],
    }[file] ?? [];
    for (const name of ['Idle', 'Walk', 'Run', 'Punch', 'HitReact', 'Death', ...extraClips]) {
      const clip = g.animations?.find(a => a.name === name);
      assert(clip, file + ' clip ' + name);
      assert(clip.channels.filter(c => c.target.path === 'rotation').length >= 6, file + ' animated joints in ' + name);
    }
    assert(g.meshes.some(m => m.primitives.some(p => p.attributes.JOINTS_0 !== undefined)), file + ' has skinned geometry');
  } else if (file.startsWith('vehicles/')) assert.equal(g.meshes.length, 6, 'car surfaces are merged by material role');
  if (authored) {
    for (const mesh of g.meshes) for (const p of mesh.primitives) assert(p.attributes.COLOR_0 !== undefined, file + ' retains authored vertex colours');
    assert(g.materials.some(m => m.name === 'VertexPaint'), file + ' retains the VertexPaint role');
    assert(g.materials.some(m => m.name === 'Signal' && m.emissiveFactor?.some(c => c > 0)), file + ' retains the emissive Signal role');
    assert(g.meshes.length <= 6, file + ' is batched for the game');
    if (/\/(coupe|sports|van|motorbike|scooter)\.glb$/.test(file)) {
      const wheels = (g.nodes ?? []).filter(n => n.name?.startsWith('Wheel_'));
      assert.equal(wheels.length, /\/(coupe|sports|van)\.glb$/.test(file) ? 4 : 2, file + ' keeps its wheel pivots');
    }
  }
  console.log(`${file}: ${triangles} triangles, ${Math.round(data.length / 1024)} KB`);
}
