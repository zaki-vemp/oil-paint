// Shared AAA PBR material loaders for Blender street assets (vehicles, props, landmarks, blades).
// Body meshes decode per-vertex PBR material classes from COLOR_0.a; Signal meshes emit HDR radiance
// directly into the Dual-Kawase bloom pipeline.
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
    const signal = paint(shared, [[1, 1, 1], [1, 1, 1], [1, 1, 1]], { vertexColors: true, surfaceKind: 6 });
    signal.side = THREE.FrontSide;
    palettes.set(shared, { body, signal });
  }
  return palettes.get(shared);
}

export function streetClone(source, shared) {
  const model = source.scene.clone(true), materials = streetMaterials(shared);
  model.traverse((o) => {
    if (!o.isMesh) return;
    o.material = o.material.name === 'Signal' ? materials.signal : materials.body;
    o.layers.enable(1);
  });
  return model;
}
