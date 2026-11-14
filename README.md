---
kind: game
engine: three.js
---

# Komorebi — A spirit trail

An anime-inspired third-person adventure through six changing forests. Defeat each chapter’s creatures and mirror rival to open its shrine, learn a new ability, and reach the sanctuary. The trail is the default world; **Snowbound** is a separate lantern-lit journey under the aurora.

## Play locally

```sh
git lfs pull
npm install
npm run dev
```

Open [localhost:5406](http://127.0.0.1:5406). Click the world to look, then click again or press J to strike. Escape opens the pause menu, including story/free exploration, world travel and scenery quality. A first click only captures the mouse. The game is silent.

| Action | Keyboard / mouse | Gamepad |
|---|---|---|
| Move | WASD / arrows | Left stick |
| Look / zoom | Mouse / wheel | Right stick / d-pad |
| Sprint | Shift | L3 toggle / RB |
| Jump / short hop | Hold / tap Space | Hold / tap A |
| Strike | J / left click | X |
| Spark | Q | Y |
| Dash | E | B |
| Heavy strike | F | LB |
| Flare | R | Start |
| Whirl | C | R3 |
| Pause / controls | Escape / P / H | — |

Touch devices have a movement stick, camera drag, jump and ability buttons. Snowbound also supports V for first person; the cabin switches to it automatically. F remains a normal strike in Snowbound.

## Chapters and progression

| Chapter | World palette | HUD | Earned ability |
|---|---|---|---|
| Sunleaf Trail | Sunlit green | Field journal | Spark |
| Petalwood | Rose blossoms | Field journal | Dash |
| Jade Highlands | Deep jade | Spirit watch | Heavy strike |
| Moonveil Grove | Blue moonlight | Spirit watch | Flare |
| Ashen Hollow | Smoky rose | Spirit watch | Whirl |
| Amber Sanctuary | Golden foliage | Field journal | Sanctuary victory |

The world palette blends as you cross chapter boundaries. HUD treatment follows the chapter automatically. The live trail map shows your position, checkpoints and nearby beasts; objectives and enemy health read actual game state.

Each chapter ends in a **mirror duel against Ren**, a swordsman from the same anime character family as the traveler. Defeating the creatures reveals Ren at the shrine; the gate stays closed until the duel is won. The six lessons build in order:

1. Read an orange sweep, jump or backstep, and strike during the green recovery window.
2. Sidestep a committed thrust and punish it with Spark.
3. Dash to evade the two-cut pattern; wait for the second cut before attacking.
4. Evade the overhead slam or break the guard with two heavy strikes.
5. Use the earlier lessons to win Whirl from the Ember Spiral rival.
6. Combine Whirl guard breaks and Flare openings against your final mirror.

The boss HUD shows the current cue, the lesson, health and the real “read / evade / punish” milestones. Guarded attacks deal chip damage; recovery hits deal extra damage. Duels start with full health and spirit energy and set a nearby retry point. Leaving the duel resets it. Losing resets the living rival, while cleared creatures stay defeated for that session. Existing cleared-chapter saves remain valid.

The traveler has an authored three-strike sword chain (cut, backhand, thrust), an overhead heavy, casting and hit recoil. Sword grips use a two-bone arm solve; blade trails follow the actual weapon. Brief hit pauses, bounded sparks, impact rings and camera feedback make hits readable; reduced-motion mode disables camera shake. Projectiles sweep against world colliders. Flare is an expanding ring rather than an opaque sphere.

Enemy strikes have a visible orange windup and commit to a direction. Step away, jump or dash to evade them. Sword hits stagger enemies, show impact sparks and restore a little spirit energy on defeat. Strikes are deliberate; proximity never attacks for you. Abilities unlock in order and show cooldowns or their required chapter.

Movement uses a 60 Hz simulation, responsive acceleration/braking, buffered jumps, a 120 ms ledge grace period and variable jump height. Dashes sweep the Rapier capsule continuously against terrain, trees, rocks and closed gates. The camera checks three rays across its near plane for obstacles. Respawns reset physics and visual history together.

Shrines heal and set the respawn point. Story progress remains compatible with `localStorage['oilpaint.levels.v1']`: `{ cleared, x, z }`; powers are derived from the cleared count. Free exploration unlocks all abilities, removes gates and respawns beasts after 20 seconds, without writing the story save.

## Rendering and assets

The trail is now a night city in solid cel-shaded geometry: instanced towers with lit windows, neon signs, street lamps, lane dashes, crates, smog banks and shrine gates, in six neon districts. Chapter palettes live in `src/game/chapters.js`, scenery in `src/world/anime.js`, and HUD/layout in `src/game/hud.js` and `src/ui.css`. Everything sits on the existing `heightAt` terrain; no new packages are required.

The visible traveler is now **Seed-san by VirtualCast, Inc.**, a detailed textured anime model from the VRM Consortium samples. Rivals reuse it with a contrasting outfit. Its humanoid joints follow the existing hidden MakeHuman animation rig with rest-pose corrections, preserving locomotion and foot IK. Boots, scarf and katana are original geometry. No VRM runtime dependency was added; the model loads through the existing GLTFLoader.

The enemies are now Quaternius CC0 people and a mech (`public/enemies/`, see its `SOURCE.md`): street punks, corp enforcers, hazmat gunners, razor ronin and a siege mech. Earlier builds used a textured animated Fox (PixelMannen/tomkranis/Asobo Studio/scurest, via Khronos) and Demon/BlueDemon from Quaternius Ultimate Monsters. They use their own locomotion, punch, hit-reaction and death clips where supplied. The original dog/wolf files remain for compatibility, but are no longer the active creature models. Snowbound retains the original world geometry but uses flat marks and clean character surfaces, without impasto or canvas grain. The original painter remains available in `src/oilpaint/`; its implementation notes follow below.

## Build and verify

```sh
npm run build
npm run serve
node src/game/hunt.js
node src/game/levels.js
node src/game/chapters.js
node src/game/combat.js
node src/game/duel.js
CHROME=/path/to/chrome node tools/gameplay.mjs
CHROME=/path/to/chrome node tools/fps.mjs --seconds 8
```

`tools/gameplay.mjs` runs against the served build on port 5405 (pass another URL as its first argument). It exercises movement, short/held jumps, dash collision, enemy windup/dodging, deliberate attacks, all rival lessons and chapter clears, victory, save restoration, pause and free-roam save isolation. The FPS tool chooses the native graphics backend on macOS/Windows.

Useful URLs: `?scene=autumn&fresh` ignores the existing save, `?scene=autumn&mode=roam` explores all chapters, `?scene=aurora` opens Snowbound, and `?quality=low` reduces scenery. `?capture` is deterministic and disables enemies; `?clean` hides the HUD without disabling gameplay.

The existing `window.lab` debug handle exposes `player`, `hunt`, `teleport(x,z,fx,fz)`, `step(dt,n)`, `setInput(fn)` and `view(eye,target)`. Headless scripts must use `tools/no-pointer-lock.mjs` to avoid capturing the real cursor.

## Credits

Code MIT; original scenery and procedural combat artwork CC0. Seed-san uses the VRM Public License 1.0 (required credit in the game menu); the Fox model is CC0 with CC BY 4.0 animation/conversion; Quaternius monsters and the hidden locomotion driver are CC0. Rapier is Apache-2.0 and three.js is MIT. No Pokémon characters or other franchise assets are included. See [LICENCE-NOTES.md](LICENCE-NOTES.md) and [public/CREDITS.md](public/CREDITS.md).

## Model upgrade performance

The detailed anime avatar, creature rigs and chapter rivals increase the complete build from about 9 MB to 22 MB. On the same local Chrome run at 1920×1080 over 8 seconds, the earlier models measured 139 fps and the upgraded models 105 fps. This is an intentional art-quality cost of the model replacement. Rivals share geometry/textures, appear only after their creature pack clears, and skip rendering/animation outside 36 m; there are no per-enemy render targets or shader variants. Low detail still reduces the grass budget and pixel ratio remains capped at 1.5.

## Original painter reference

The sections below document the retained paint engine. The active trail uses the cel renderer described above.

### Paint engine concepts

Nothing is filtered after the picture is drawn. Everything is a **dab**: one touch of a loaded brush, in one mixed colour, pinned to a point of the world and turned to lie along a direction of the world (the grain of a trunk, the fall of a cloak).

- **Colour is paint, not light.** A thing has three mixes (light, half-tone, shade). Each dab picks one from how lit its spot is, then is mixed a little differently from its neighbours. Nothing is tone-mapped, so you choose colours as they should look on the canvas.
- **The brush** is a sheet of eight marks made in code: where there is paint, how thick it stands, and a streak of a second colour. A light from the upper left of the canvas rakes across the thickness.
- **Distance simplifies.** Dabs keep between a least and a most size on the canvas, so far things are painted with fewer, larger touches.
- **The ground stays put.** Its dabs lie flat on it, real patches of the surface, each at its own small height, so they overlap in one fixed order and never turn with the eye. Every dab belongs to one small square of ground for good. With distance, one in four is drawn larger to stand for its neighbours; as you come nearer it shrinks back where it lies and the others grow in round it. Nothing is swapped for something else.
- **Nothing is put in order by the eye.** Which of two overlapping strokes is on top must never depend on where the camera is, or the paint crawls as you breathe. Near things are given a depth each. Far things (ranges, water, sky) are too far apart for a depth to tell them from each other, so they are laid in one fixed order, far to near.
- **Shade.** The still world's shade is drawn once from the sun (or the moon), and a dab takes it whole, so a shadow's edge is a ragged line of whole dabs. A moving thing's shadow would make dabs flick as it passed, so it goes over the ground as a soft glaze, pixel by pixel.
- **Light that is light.** A few things are painted in strokes that add up instead of covering: the aurora, a flame, the glow round a candle. A carried lamp (`uLamp`) turns what is near it toward that thing's own warm colour.
- **Mist** puts distance between one row of things and the next. In the wood, the one thing that is not a dab is the sun standing in that mist, found by walking each line of sight through the shade.

### Using the paint engine

It owns the picture: a three.js `WebGLRenderer` (WebGL2), its own render target and final pass, and every material. Your scene, camera and game loop stay yours. Built on three 0.186.

```js
import * as THREE from 'three';
import { createShared, createBrushSheet, dabMaterial, dabGeometry, dabMesh, groundRings, pinDabs, createShade, createFrame } from './src/oilpaint/index.js';

const renderer = new THREE.WebGLRenderer({ antialias: false });
renderer.autoClear = false;
renderer.outputColorSpace = THREE.LinearSRGBColorSpace; // colours are paint: written as they are

const shared = createShared();              // sun, wind, mist, maps: every material reads these
shared.tBrush.value = createBrushSheet();
// shared.tHeight / shared.tLand: your ground's height and a map of what lies where (see src/world/land.js)

// 1. dabs of your own: positions in buffers, colour and direction decided in GLSL
const mesh = dabMesh(dabGeometry(count, { aPos: [positions, 3] }), dabMaterial(shared, {
  head: `in vec3 aPos;`,
  body: `void makeDab(inout Dab d) {
    float lit = sunAt(aPos);
    vec4 r = hash4(float(gl_InstanceID));
    d.pos = aPos;
    d.dir = vec3(0.0, 1.0, 0.0);                       // the way the stroke runs
    d.size = vec2(0.2, 0.5);                           // across and along, metres
    d.c1 = hazed(jitter(paint3(LIGHT, HALF, SHADE, lit, r.x), r.yzw, 0.5), aPos);
    d.c2 = d.c1 * 1.15;                                // the colour streaked into it
  }`,
}));

// 2. a ground with no end (see src/world/floor.js for the leaf carpet, src/night/snow.js for snow)
const carpet = groundRings(shared, { body: carpetGLSL, per: 3, n: 192, cell0: 0.14, levels: 6, minPx: [5, 9], maxPx: [4000, 4000], cells: [0, 8] });

// 3. paint on any mesh, rigged or not: pins spread over its surface and carried with it
const paint = pinDabs(shared, [[light, half, shade]], [{ mesh: model, count: 500, style: (p, n) => ({ pal: 0, size: [0.06, 0.12], dir: UP }) }]);
scene.add(mesh, carpet.group, paint.mesh);

// 4. shade and the canvas
const shade = createShade(renderer, shared, stillThingsScene);   // the still world, once
const frame = createFrame(renderer, shared);

function tick() {
  shared.uTime.value = time;
  shared.uPixel.value = (2 * Math.tan(THREE.MathUtils.degToRad(camera.fov) / 2)) / drawingBufferHeight;
  carpet.follow(camera.position.x, camera.position.z);
  paint.update();                    // after the model has moved
  shade.update(scene, heroPosition); // the moving thing's shadow: draws the meshes on layer 1
  frame.render(scene, camera);
}
```

In `makeDab` you have `paint3` (the three mixes), `jitter` (broken colour), `hazed` (mist), `sunAt` / `woodSunAt` (shade), `lampAt` (a carried light), `gustAt` (wind), `heightAt` / `normalAt` (the ground) and `hash4`. Set `d.lie = 1.0` with `d.nrm` and `d.height` for a dab that lies on a surface, `d.glaze = 1.0` for one the moving shadow passes over, and `d.base = 1.0` for one pinned by its foot (a blade of grass, a ray of the aurora). `createShade` takes the sun's direction and how much of the world its picture of the shade must hold.

For **made things** (a cabin, furniture, pots) there is a kit in `src/night/kit.js`: give it flat faces or any geometry with the name of a palette and it keeps the faces as an underpainting and lays strokes on them along the grain, with logs and planks in courses. `src/night/cabin.js`, `roof.js` and `details.js` are a whole house built with it.

### Original module map

| | |
|---|---|
| `src/oilpaint/` | The painter: `dabs.js` (the dab), `brush.js` (the marks), `common.js` (shared uniforms and GLSL), `ground.js` (the steady ground), `pins.js` (paint on a mesh), `shade.js`, `finish.js` (the canvas) |
| `src/night/` | The aurora: `land.js` (the ridge, the horns, the fjord), `paint.js` (the night's palette), `snow.js` (snow and rock), `pines.js`, `sky.js` (stars, moon, the aurora), `far.js` (water and ranges), `cabin.js` with `kit.js`, `roof.js`, `details.js`, `lantern.js`, `tracks.js` (his footprints), `drift.js` (snow in the air), `main.js` |
| `src/world/` | The wood: `land.js` (ground and the way through), `floor.js` (leaf carpet, undergrowth), `forest.js` (about 3,100 trees: trunks, bark, about 570,000 leaf dabs), `sky.js`, `leaves.js` (falling), `kick.js` (kicked up) |
| `src/figure/` | The wanderer: Patch's starter man and his movement (`man.js`, `animator.js`, `player.js`, `input.js`), his hat, and `cloak.js`, a cloth of points on threads kept off his body and under the hat's brim |
| `src/main.js`, `src/scenes.js`, `src/boot.js` | The wood's demo; which picture is shown; the page's start |
| `tools/` | Headless stills (`shots.mjs`), a film (`clip.mjs`), frame time (`shots.mjs --time`, `fps.mjs`), steadiness (`steady.mjs` for the ground, `jitter.mjs` for anything), the scene switch (`switch.mjs`), a first visit (`cold.mjs`), a clip file rewritten with only what its clips use (`compact-glb.mjs`) |

### Paint engine limits

- three.js on WebGL2 only. It is not a node material and does not run on WebGPU.
- Almost everything is opaque: there is no glass, and water is strokes that give back the sky, not a mirror of the world.
- Paint materials use three-colour palettes; the anime character shader can additionally retain an albedo atlas.
- Historical paint-engine measurements on an RTX 4090 at 1080p: about 4.5 ms a frame in the aurora, about 4.6 in the wood. `?quality=low`, or Patch's performance mode, draws less (in the aurora: fewer pixels, no veil behind the curtains, less snow in the air; in the wood: no sun in the mist, fewer falling leaves).
- The demo downloads about 9 MB, 5 of it code and the rest the wanderer and his clips. A first visit is ready in about 4 seconds on a fast machine and a 30 Mbit/s line.
- Each scene is one hour of one day. Walk off the ridge into the fjord, or more than 78 m from the way in the wood, and you are put back at the start.
- Through his own eyes you see no body or hands.
