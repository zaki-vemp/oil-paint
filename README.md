---
kind: game
engine: three.js
---

# Komorebi — A spirit trail

An anime-inspired third-person adventure through five changing forests. Clear each chapter's beasts, open its shrine gate, learn a new ability and reach the sanctuary. The trail is the default world; **Snowbound** is a separate lantern-lit journey under the aurora.

## Play locally

```sh
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
| Pause / controls | Escape / P / H | — |

Touch devices have a movement stick, camera drag, jump and ability buttons. Snowbound also supports V for first person; the cabin switches to it automatically. F remains a normal strike in Snowbound.

## Chapters and progression

| Chapter | World palette | HUD | Earned ability |
|---|---|---|---|
| Sunleaf Trail | Sunlit green | Field journal | Spark |
| Petalwood | Rose blossoms | Field journal | Dash |
| Jade Highlands | Deep jade | Spirit watch | Heavy strike |
| Moonveil Grove | Blue moonlight | Spirit watch | Flare |
| Amber Sanctuary | Golden foliage | Field journal | Sanctuary victory |

The world palette blends as you cross chapter boundaries. HUD treatment follows the chapter automatically. The live trail map shows your position, checkpoints and nearby beasts; objectives and enemy health read actual game state.

Enemy strikes have a visible orange windup and commit to a direction. Step away, jump or dash to evade them. Sword hits stagger enemies, show impact sparks and restore a little spirit energy on defeat. Strikes are deliberate; proximity never attacks for you. Abilities unlock in order and show cooldowns or their required chapter.

Movement uses a 60 Hz simulation, responsive acceleration/braking, buffered jumps, a 120 ms ledge grace period and variable jump height. Dashes sweep the Rapier capsule continuously against terrain, trees, rocks and closed gates. The camera checks three rays across its near plane for obstacles. Respawns reset physics and visual history together.

Shrines heal and set the respawn point. Story progress remains compatible with `localStorage['oilpaint.levels.v1']`: `{ cleared, x, z }`; powers are derived from the cleared count. Free exploration unlocks all abilities, removes gates and respawns beasts after 20 seconds, without writing the story save.

## Rendering and assets

The trail now uses solid cel-shaded geometry: instanced clustered trees, tapered trunks, grass tufts, five-petal flowers, mossy rocks, clouds and shrine gates. Chapter palettes live in `src/game/chapters.js`, scenery in `src/world/anime.js`, and HUD/layout in `src/game/hud.js` and `src/ui.css`. Everything sits on the existing `heightAt` terrain; no new packages are required.

The existing animated MakeHuman traveler and Quaternius animals are retained. Animals keep their original atlas textures for eyes, muzzles and fur markings, with shared cel materials. Snowbound retains the original world geometry but uses flat marks and clean character surfaces, without impasto or canvas grain. The original painter remains available in `src/oilpaint/`; its implementation notes follow below.

## Build and verify

```sh
npm run build
npm run serve
node src/game/hunt.js
node src/game/levels.js
node src/game/chapters.js
CHROME=/path/to/chrome node tools/gameplay.mjs
CHROME=/path/to/chrome node tools/fps.mjs --seconds 8
```

`tools/gameplay.mjs` runs against the served build on port 5405 (pass another URL as its first argument). It exercises movement, short/held jumps, dash collision, enemy windup/dodging, deliberate attacks, all five chapter clears, victory, save restoration, pause and free-roam save isolation. The FPS tool chooses the native graphics backend on macOS/Windows.

Useful URLs: `?scene=autumn&fresh` ignores the existing save, `?scene=autumn&mode=roam` explores all chapters, `?scene=aurora` opens Snowbound, and `?quality=low` reduces scenery. `?capture` is deterministic and disables enemies; `?clean` hides the HUD without disabling gameplay.

The existing `window.lab` debug handle exposes `player`, `hunt`, `teleport(x,z,fx,fz)`, `step(dt,n)`, `setInput(fn)` and `view(eye,target)`. Headless scripts must use `tools/no-pointer-lock.mjs` to avoid capturing the real cursor.

## Credits

Code MIT; art CC0. Original scenery and UI geometry are made in code. The traveler is Patch's MakeHuman starter character; animations and animals are from Quaternius (CC0). Rapier is Apache-2.0 and three.js is MIT. No Pokémon characters or other franchise assets are included. See [LICENCE-NOTES.md](LICENCE-NOTES.md) and [public/CREDITS.md](public/CREDITS.md).

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
