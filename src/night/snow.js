// Realistic AAA Snowbound mode: terrain PBR in night/land.js renders continuous snow,
// crystal glints and triplanar rock directly without painterly brush dabs.
// Retains the createSnow API so night/main.js works seamlessly.
import * as THREE from 'three';

export function createSnow() {
  const group = new THREE.Group();
  return { group, follow() {}, counts: { rock: 0, snow: 0 } };
}
