// Kael's carried Spirit Lantern: detailed Blender metallic lantern (public/street/spirit-lantern.glb)
// swinging dynamically from his right fist, paired with a smooth volumetric optical firelight halo
// that feeds into HDR Bloom and drives the dynamic inverse-square uLamp point light across the snow.
import * as THREE from 'three';
import { loadStreetAsset, streetClone } from '../street-assets.js';

const TOP = 0.18;

export function createLantern(shared) {
  const object = new THREE.Group();
  object.name = 'Old Kyoto lantern';
  object.rotation.order = 'YXZ';
  const dark = new THREE.MeshBasicMaterial({ color: new THREE.Color(0.04, 0.04, 0.05) });
  const glass = new THREE.MeshBasicMaterial({ color: new THREE.Color(2.6, 1.6, 0.55) });
  const body = new THREE.Mesh(new THREE.CylinderGeometry(0.05, 0.058, 0.15, 8), glass);
  const cap = new THREE.Mesh(new THREE.ConeGeometry(0.075, 0.06, 8), dark);
  cap.position.y = 0.105;
  const base = new THREE.Mesh(new THREE.CylinderGeometry(0.066, 0.066, 0.025, 8), dark);
  base.position.y = -0.085;
  object.add(body, cap, base);

  loadStreetAsset('spirit-lantern').then((asset) => {
    const model = streetClone(asset, shared);
    model.name = 'Spirit lantern';
    model.scale.setScalar(TOP / 0.14);
    for (const mesh of [...object.children]) mesh.geometry.dispose();
    dark.dispose();
    glass.dispose();
    object.clear();
    object.add(model);
  }).catch(() => {});

  // Smooth billboarded optical volumetric glow halo (zero chunky brush petals!)
  const glow = new THREE.Mesh(
    new THREE.PlaneGeometry(1.15, 1.15),
    new THREE.ShaderMaterial({
      glslVersion: THREE.GLSL3,
      uniforms: { uLamp: shared.uLamp, uLampCol: shared.uLampCol, uTime: shared.uTime },
      transparent: true,
      depthWrite: false,
      blending: THREE.AdditiveBlending,
      vertexShader: /* glsl */ `
        uniform vec4 uLamp;
        out vec2 vUv;
        void main() {
          vUv = uv;
          vec3 camR = vec3(viewMatrix[0][0], viewMatrix[1][0], viewMatrix[2][0]);
          vec3 camU = vec3(viewMatrix[0][1], viewMatrix[1][1], viewMatrix[2][1]);
          vec3 world = uLamp.xyz + camR * position.x + camU * position.y;
          gl_Position = projectionMatrix * viewMatrix * vec4(world, 1.0);
        }`,
      fragmentShader: /* glsl */ `
        precision highp float;
        uniform vec4 uLamp;
        uniform vec3 uLampCol;
        uniform float uTime;
        in vec2 vUv;
        layout(location = 0) out vec4 outC;
        void main() {
          float r = length(vUv * 2.0 - 1.0);
          if (r > 1.0) discard;
          float flick = 0.90 + 0.10 * sin(uTime * 11.0) * cos(uTime * 7.3);
          float core = exp(-r * r * 14.0) * 1.4;
          float halo = exp(-r * 3.8) * 0.42 * (1.0 - smoothstep(0.65, 1.0, r));
          vec3 col = mix(vec3(1.4, 0.65, 0.18), vec3(2.8, 2.2, 1.2), exp(-r * 7.0));
          outC = vec4(col * (core + halo) * flick * uLamp.w, clamp(core + halo, 0.0, 1.0));
        }`,
    }),
  );
  glow.frustumCulled = false;
  glow.renderOrder = 21;

  let swing = 0, swingV = 0, lastX = null, lastZ = 0;
  return {
    object,
    glow,
    update(dt, grip, yaw) {
      if (lastX == null) { lastX = grip.x; lastZ = grip.z; }
      const vx = (grip.x - lastX) / Math.max(dt, 1e-3), vz = (grip.z - lastZ) / Math.max(dt, 1e-3);
      lastX = grip.x; lastZ = grip.z;
      const along = Math.max(-6, Math.min(6, vx * Math.sin(yaw) + vz * Math.cos(yaw)));
      swingV += (-swing * 30 - swingV * 3.5 - along * 0.9) * dt;
      swing = Math.max(-0.6, Math.min(0.6, swing + swingV * dt));
      object.position.set(grip.x + Math.sin(yaw) * Math.sin(swing) * TOP, grip.y - Math.cos(swing) * TOP, grip.z + Math.cos(yaw) * Math.sin(swing) * TOP);
      object.rotation.set(-swing, yaw, 0);
      shared.uLamp.value.set(object.position.x, object.position.y + 0.02, object.position.z, 1);
    },
  };
}
