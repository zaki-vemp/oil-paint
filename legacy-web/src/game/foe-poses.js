// Extra actions on the authored Blender skeletons. Each pose is relative to the rest bones,
// so the same weapon and body meshes follow the new attack, guard and footwork animations.
import * as THREE from 'three';

const braced = {
  Chest: [0.18, 0.08, 0], Head: [-0.14, 0, 0], Thigh_L: [-0.20, 0, 0], Thigh_R: [-0.25, 0, 0],
  Shin_L: [0.36, 0, 0], Shin_R: [0.43, 0, 0],
  UpperArm_L: [-1.15, -0.15, -0.24], Forearm_L: [-1.12, 0, 0],
  UpperArm_R: [-0.89, 0.18, 0.23], Forearm_R: [-1.2, 0, 0],
};

const followThrough = {
  punk: { Chest: [0.17, -0.22, 0.55], UpperArm_R: [-1.45, 0.25, 0.7], Forearm_R: [-0.5, 0, 0.2], Thigh_L: [-0.35, 0, 0] },
  enforcer: { Chest: [0.29, 0.12, -0.28], UpperArm_L: [-1.52, -0.16, -0.32], Forearm_L: [-0.48, 0, 0], Thigh_R: [-0.34, 0, 0] },
  gunner: { Chest: [0.31, 0, 0.32], UpperArm_R: [-1.68, 0.04, 0.42], Forearm_R: [-0.32, 0, 0], UpperArm_L: [-1.18, 0, -0.18], Forearm_L: [-0.48, 0, 0] },
  blade: { Chest: [0.12, -0.3, -0.57], UpperArm_R: [-1.22, 0.28, -0.83], Forearm_R: [-0.32, 0, 0], Hand_R: [0.32, 0, 0.25], Thigh_L: [-0.32, 0, 0] },
  brute: { Chest: [0.31, 0, 0.58], UpperArm_L: [-1.05, -0.15, -0.65], UpperArm_R: [-1.13, 0.2, 0.65], Forearm_R: [-0.28, 0, 0], Thigh_R: [-0.32, 0, 0] },
};

export function foePoses(model, kind) {
  if (!followThrough[kind]) return {};
  const rotation = new THREE.Quaternion(), angles = new THREE.Euler();
  function clip(name, times, shapes) {
    const joints = new Set(shapes.flatMap(shape => Object.keys(shape)));
    const tracks = [];
    for (const joint of joints) {
      const bone = model.getObjectByName(joint);
      if (!bone?.isBone) continue;
      const values = shapes.flatMap(shape => {
        const [x, y, z] = shape[joint] ?? [0, 0, 0];
        return rotation.setFromEuler(angles.set(x, y, z)).premultiply(bone.quaternion).toArray();
      });
      tracks.push(new THREE.QuaternionKeyframeTrack(`${joint}.quaternion`, times, values));
    }
    return new THREE.AnimationClip(name, times.at(-1), tracks);
  }
  const sidestep = side => {
    const front = side > 0 ? 'L' : 'R', rear = side > 0 ? 'R' : 'L';
    const lean = { Chest: [0.24, 0.06, side * 0.28], Head: [-0.18, 0, side * -0.17],
      [`Thigh_${front}`]: [-0.42, 0, side * 0.25], [`Shin_${front}`]: [0.64, 0, 0],
      [`Thigh_${rear}`]: [0.13, 0, side * 0.11], [`Shin_${rear}`]: [0.19, 0, 0],
      UpperArm_R: [-0.54, 0, 0.21], Forearm_R: [-0.83, 0, 0], UpperArm_L: [-0.38, 0, -0.27], Forearm_L: [-0.77, 0, 0] };
    return clip(side > 0 ? 'EvadeLeft' : 'EvadeRight', [0, 0.10, 0.24, 0.36], [{}, lean, lean, {}]);
  };
  const strike = followThrough[kind];
  const reload = { ...braced, Chest: [-0.05, 0, -strike.Chest[2] * 0.55],
    UpperArm_R: [-0.70, 0.12, -0.25], Forearm_R: [-1.45, 0, 0], UpperArm_L: [-0.72, -0.16, 0.14] };
  return {
    Alternate: clip('Alternate', [0, 0.28, 0.5, 0.76, 1], [{}, reload, strike, { ...strike, Chest: [0.1, 0, 0.18] }, {}]),
    Guard: clip('Guard', [0, 0.12, 0.48, 0.68], [{}, braced, braced, {}]),
    EvadeLeft: sidestep(1), EvadeRight: sidestep(-1),
  };
}
