// Directional sun/moon shadows: static world baked once into a high-res depth map,
// plus a 512x512 dynamic shadow frustum tracking the protagonist, nearby vehicles and combatants.
import * as THREE from 'three';
import { SUN } from './common.js';

const BIAS = new THREE.Matrix4().set(0.5, 0, 0, 0.5, 0, 0.5, 0, 0.5, 0, 0, 0.5, 0.5, 0, 0, 0, 1);

function depthTarget(w, h) {
  const target = new THREE.WebGLRenderTarget(w, h, { depthBuffer: true, generateMipmaps: false });
  target.depthTexture = new THREE.DepthTexture(w, h, THREE.UnsignedIntType);
  target.depthTexture.compareFunction = THREE.LessEqualCompare;
  target.depthTexture.minFilter = target.depthTexture.magFilter = THREE.LinearFilter;
  return target;
}

export function createShade(renderer, shared, still, { sun = SUN, size = [4096, 2560], box = [165, 104], centre: at = [0, 11, -8], back = 400, near = 10, far = 800 } = {}) {
  const wood = depthTarget(size[0], size[1]);
  const sunCam = new THREE.OrthographicCamera(-box[0], box[0], box[1], -box[1], near, far);
  const centre = new THREE.Vector3(...at);
  sunCam.position.copy(centre).addScaledVector(sun, back);
  sunCam.lookAt(centre);
  sunCam.updateMatrixWorld(true);
  sunCam.updateProjectionMatrix();
  renderer.setRenderTarget(wood);
  renderer.clear();
  renderer.render(still, sunCam);
  renderer.setRenderTarget(null);
  shared.tTreeShade.value = wood.depthTexture;
  shared.uTreeShadeMat.value.copy(BIAS).multiply(sunCam.projectionMatrix).multiply(sunCam.matrixWorldInverse);

  // Higher-res dynamic shadow map (512x512, 5.5m half-extent) covering the player, foes and nearby rides.
  const fig = depthTarget(512, 512);
  const figCam = new THREE.OrthographicCamera(-5.5, 5.5, 5.5, -5.5, 1, 65);
  figCam.layers.set(1);
  shared.tFigShade.value = fig.depthTexture;
  const depthOnly = new THREE.MeshBasicMaterial({ side: THREE.DoubleSide });
  return {
    update(scene, at) {
      figCam.position.set(at.x, at.y + 1.0, at.z).addScaledVector(sun, 30);
      figCam.lookAt(at.x, at.y + 1.0, at.z);
      figCam.updateMatrixWorld(true);
      shared.uFigShadeMat.value.copy(BIAS).multiply(figCam.projectionMatrix).multiply(figCam.matrixWorldInverse);
      const before = scene.overrideMaterial;
      scene.overrideMaterial = depthOnly;
      renderer.setRenderTarget(fig);
      renderer.clear();
      renderer.render(scene, figCam);
      renderer.setRenderTarget(null);
      scene.overrideMaterial = before;
    },
  };
}
