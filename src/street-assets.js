// Reviewed Blender models share two city materials: vertex-painted surfaces and flat signal lights.
// Geometry and materials are reused across clones; no imported physical lighting or tone mapping is added.
import * as THREE from 'three';
import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';
import { paint } from './figure/figure.js';

const sources = new Map(), palettes = new WeakMap();
export function loadStreetAsset(name) {
  if (!sources.has(name)) sources.set(name, new GLTFLoader().loadAsync(`./street/${name}.glb`));
  return sources.get(name);
}

export function streetMaterials(shared) {
  if (!palettes.has(shared)) {
    const body = paint(shared, [[1, 1, 1], [0.85, 0.85, 0.85], [0.5, 0.55, 0.6]], { vertexColors: true });
    body.side = THREE.FrontSide;
    const signal = new THREE.MeshBasicMaterial({ vertexColors: true, toneMapped: false });
    palettes.set(shared, { body, signal });
  }
  return palettes.get(shared);
}

export function streetClone(source, shared) {
  const model = source.scene.clone(true), materials = streetMaterials(shared);
  model.traverse(o => {
    if (!o.isMesh) return;
    o.material = o.material.name === 'Signal' ? materials.signal : materials.body;
    o.layers.enable(1);
  });
  return model;
}
