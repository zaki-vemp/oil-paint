// Story script, voiced radio comms, six sector field missions, and eight repeatable street
// contract kiosks across the expanded 720m city grid. Imported by missions.js and anime.js.
import { SCALE } from './chapters.js';
import { GRID_MIN, GRID_MAX } from '../world/grid.js';
import { SPEAKERS } from './audio.js';

export const COMMS = {
  intro: {
    speaker: 'sora',
    line: 'Courier Zero-Seven, live link online. Ren locked the six sector gates behind our neural tags. Overload both jammers in Sunleaf Row, clear the street crew, then defeat his Specter.',
  },
  sector1: {
    speaker: 'sora',
    line: 'Petal Arcade ahead. A Syndicate runner stole your cipher key. Catch the marked courier on the strip, then upload at the Arcade Terminal.',
  },
  sector2: {
    speaker: 'sora',
    line: 'Jade Heights is flooding the grid with static. Shatter three Corp Beacons along the avenue so we can punch through.',
  },
  sector3: {
    speaker: 'sora',
    line: 'Moonveil Stacks. Grab a bike or coupe and carve through all three cyan Relay Rings before the sector locks down.',
  },
  sector4: {
    speaker: 'sora',
    line: 'Ashen Hollow. Ren is staging weapons caches for a city purge. Smash three Syndicate Supply Caches down this block.',
  },
  sector5: {
    speaker: 'sora',
    line: 'Final stretch to Amber Sanctuary. Overload both Shield Pylons guarding the approach, then break Ren for good.',
  },
  field0: { speaker: 'sora', line: 'Both jammers fried! Clear the remaining punks and face Ren’s first Specter.' },
  field1: { speaker: 'sora', line: 'Cipher uploaded! Finish the Arcade crew and take down the Specter.' },
  field2: { speaker: 'sora', line: 'Corp beacons are dark! Sweep the Heights crew and challenge Ren’s mirror.' },
  field3: { speaker: 'sora', line: 'All relay rings cleared! Break the Stacks defenders and defeat the Specter.' },
  field4: { speaker: 'sora', line: 'Supply caches wrecked! Clear the Hollow crew and cross the final Specter.' },
  field5: { speaker: 'sora', line: 'Sanctuary shields are down! Finish the guard and meet Ren at the spire.' },
  duel0: { speaker: 'ren', line: 'Still running errands for ghosts, Zero-Seven? Show me if your blade remembers the first lesson.' },
  duel1: { speaker: 'ren', line: 'Fast wheels cannot outrun your own telemetry. Sidestep the thrust—if you can.' },
  duel2: { speaker: 'ren', line: 'Two cuts, one shadow. Let’s see if you learned to dash through the storm.' },
  duel3: { speaker: 'ren', line: 'Iron holds where pride snaps. Crack my guard with heavy steel.' },
  duel4: { speaker: 'ren', line: 'Almost at the spire, courier. Spin through the ember ring.' },
  duel5: { speaker: 'ren', line: 'Every street, every mirror, every parcel led you here. Show me what remains when the mask drops.' },
  clear0: { speaker: 'sora', line: 'Sector one clear! Spark bolt online—press Q to fire an energy arc.' },
  clear1: { speaker: 'sora', line: 'Sector two clear! Neural Dash unlocked—press E to phase through strikes.' },
  clear2: { speaker: 'sora', line: 'Sector three clear! Heavy Cleave unlocked—press F to shatter guards and launch foes.' },
  clear3: { speaker: 'sora', line: 'Sector four clear! Lantern Flare unlocked—press R to stagger every enemy around you.' },
  clear4: { speaker: 'sora', line: 'Sector five clear! Whirl Slash unlocked—press C to spin-cut the whole pack.' },
  clear5: { speaker: 'sora', line: 'Final gate open! Step into Amber Sanctuary Terminal ahead to stabilize the city core.' },
  firstRide: { speaker: 'sora', line: 'Wheels online. W and S throttle, Space handbrakes, and G steps out once you slow down.' },
  heat: { speaker: 'sora', line: 'Careful—citizens saw that! Break line of sight or step into a Mission Relay to clear your heat.' },
  contractStart: { speaker: 'dispatch', line: 'Street contract accepted. Follow the green diamond to complete the run for bonus credits.' },
  contractDone: { speaker: 'sora', line: 'Contract paid in full! Credits wired and suit vitals restored.' },
  win: { speaker: 'sora', line: 'Sanctuary core stabilized. Ren’s lock on the city is broken—every district and side contract is yours to roam, Kael.' },
};

// Six field missions, strictly inside their sector's gates at SCALE = 3.0.
export const SECTOR_MISSIONS = [
  {
    seg: 0,
    type: 'jammer',
    opTitle: 'OP 01 // Dead Drop',
    stepLabel: 'Overload Signal Jammers',
    tip: 'Move WASD · Click/J Slash · G Ride bike · Smash the glowing Signal Jammers',
    targets: [
      { x: -7, z: 214, label: 'Jammer Alpha' },
      { x: 7, z: 184, label: 'Jammer Beta' },
    ],
  },
  {
    seg: 1,
    type: 'intercept',
    opTitle: 'OP 02 // Stolen Cipher',
    stepLabel: 'Catch Marked Courier & Sync Uplink',
    tip: 'Defeat the hooded Syndicate Courier (magenta diamond), then stand by the Terminal',
    targets: [
      { x: -6, z: 126, label: 'Marked Courier', role: 'courier' },
      { x: 8, z: 112, label: 'Arcade Uplink', role: 'terminal' },
    ],
  },
  {
    seg: 2,
    type: 'beacon',
    opTitle: 'OP 03 // Blackout Towers',
    stepLabel: 'Shatter Corp Beacons',
    tip: 'Use Q Spark or sword combos to shatter all 3 Corp Beacons',
    targets: [
      { x: -10, z: 68, label: 'West Beacon' },
      { x: 26, z: 46, label: 'East Beacon' },
      { x: 14, z: 24, label: 'South Beacon' },
    ],
  },
  {
    seg: 3,
    type: 'rings',
    opTitle: 'OP 04 // Hotwire Express',
    stepLabel: 'Pass Through Relay Rings',
    tip: 'Ride a car or bike (G) or dash (E) through the 3 cyan Relay Rings',
    targets: [
      { x: 16, z: -18, label: 'Relay Ring 1' },
      { x: 4, z: -46, label: 'Relay Ring 2' },
      { x: -8, z: -72, label: 'Relay Ring 3' },
    ],
  },
  {
    seg: 4,
    type: 'cache',
    opTitle: 'OP 05 // Cache Raid',
    stepLabel: 'Destroy Supply Caches',
    tip: 'F Heavy Strike or ram at speed to wreck the 3 Syndicate Supply Caches',
    targets: [
      { x: -14, z: -116, label: 'North Cache' },
      { x: -20, z: -142, label: 'Mid Cache' },
      { x: 12, z: -166, label: 'Gate Cache' },
    ],
  },
  {
    seg: 5,
    type: 'shield',
    opTitle: 'OP 06 // Ghost Protocol',
    stepLabel: 'Overload Shield Pylons',
    tip: 'Destroy both Sanctuary Shield Pylons, then defeat Ren at the Spire',
    targets: [
      { x: 16, z: -222, label: 'West Shield Pylon' },
      { x: 30, z: -252, label: 'East Shield Pylon' },
    ],
  },
];

// Eight repeatable street contract terminals placed on avenue corners across the expanded city grid.
// Horizontal dirs keep side contracts inside their sector's z band so closed gates never block them.
export const KIOSK_SPOTS = [
  { id: 'k0', name: 'Sunleaf West Contract', x: -120 + 6.8, z: 200 + 6.8, kind: 'rings', dir: [-1, 0] },
  { id: 'k1', name: 'Downtown East Contract', x: 120 - 6.8, z: 200 + 6.8, kind: 'jammers', dir: [1, 0] },
  { id: 'k2', name: 'Arcade Plaza Contract', x: -120 + 6.8, z: 120 - 6.8, kind: 'uplink', dir: [-1, 0] },
  { id: 'k3', name: 'Heights Overlook Contract', x: 120 - 6.8, z: 40 + 6.8, kind: 'rings', dir: [1, 0] },
  { id: 'k4', name: 'Market Cross Contract', x: -120 + 6.8, z: -40 - 6.8, kind: 'jammers', dir: [-1, 0] },
  { id: 'k5', name: 'Docks Freight Contract', x: 120 - 6.8, z: -40 + 6.8, kind: 'uplink', dir: [1, 0] },
  { id: 'k6', name: 'Suburbs Relay Contract', x: -120 + 6.8, z: -120 - 6.8, kind: 'rings', dir: [-1, 0] },
  { id: 'k7', name: 'Sanctuary Outer Contract', x: 120 - 6.8, z: -240 - 6.8, kind: 'jammers', dir: [1, 0] },
];

export const EXTRA_KEEP = [
  ...SECTOR_MISSIONS.flatMap((m) => m.targets.map((t) => ({ x: t.x, z: t.z }))),
  ...KIOSK_SPOTS.map((k) => ({ x: k.x, z: k.z })),
];

export function demoStory() {
  if (SECTOR_MISSIONS.length !== 6) throw new Error('expected 6 sector missions');
  for (const [i, m] of SECTOR_MISSIONS.entries()) {
    if (m.seg !== i || !m.opTitle || !m.targets.length) throw new Error('bad sector mission ' + i);
  }
  for (const [key, entry] of Object.entries(COMMS)) {
    if (!SPEAKERS[entry.speaker] || !entry.line) throw new Error('bad comms entry ' + key);
  }
  for (const k of KIOSK_SPOTS) {
    if (k.x <= GRID_MIN || k.x >= GRID_MAX || k.z <= GRID_MIN || k.z >= GRID_MAX) throw new Error('kiosk outside grid ' + k.id);
  }
  if (SCALE < 2.8) throw new Error('expected expanded SCALE');
  console.log('story.js: ok');
}

if (globalThis.process?.argv?.[1]?.endsWith('story.js')) demoStory();
