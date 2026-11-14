# HANDOFF — state of the work as of 2026-11-14

**Continuation:** sections 1–6 record the earlier run. Section 7 below is the current state and supersedes
their vehicle limitations, pedestrian provenance and verification results.

For the next agent. Read `AGENTS.md` first (rules, module map, gotchas); this file is the *state*: what was
changed in this run, what is committed and what is not, how it was verified, and what is still open.
Delete this file once its open items are closed; nothing in the game reads it.

## 1. Repository state

| | |
|---|---|
| Branch | `main`, in sync with `origin/main` |
| Last commit | `fb8e10c` "Turn the trail into a cyberpunk night city with human enemies" |
| Uncommitted | 17 modified files, 4 new files (section 4). Not committed, not pushed |
| Side branch | `cyberpunk-city` points at the same commit as `main`; it can be deleted |
| Ignored output | `dist/` (build), `shots/*.png` (six screenshots from this run), `.firebase/` |

Git LFS: `.gitattributes` marks `*.glb`, `*.vrm`, `*.png`, `*.jpg` for LFS, but the LFS filter is not installed
in this clone, so every binary in history is a plain blob. Do not run `git lfs install` without deciding what to
do about the existing blobs; they would all show as modified.

## 2. How to run and verify

```bash
npm install
npm run build                 # node build.mjs → dist/
node src/game/hunt.js         # balance and rule invariants (also runs levels, duels, combat demos)
node src/game/levels.js
node src/game/ride.js         # vehicle handling demo
node src/game/chapters.js
```

Browser checks are headless Chrome through the project's own tools. They need the build served:

```bash
node tools/serve.mjs dist 5405 &
export CHROME="/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
node tools/gameplay.mjs       # full six-level run, save/restore, pause, roam
node tools/street.mjs         # car, bike, ramming, crash, citizens, mob, thief, jogger
node tools/fps.mjs --seconds 8
```

Last results (all on the uncommitted tree):

- `tools/gameplay.mjs`: PASS (38 crew members and 6 rivals defeated in order, save restored).
- `tools/street.mjs`: PASS. Measured: car at 18.8 m/s after 2.5 s; ram dealt 97 damage and threw a punk 1.22 m;
  a 20 m/s wall hit rebounded at 5.4 m/s and cost the driver 14 health; bike lean 0.6 rad; citizen hurt −15,
  killed −30, mob blow −7, thief 0, jogger −15.
- `tools/fps.mjs`, back to back on the same machine state: commit `fb8e10c` 93–94 fps, this tree 97–101 fps
  (headless, 1920×1080). The same build measured 213 fps hours earlier on a quieter machine: never compare fps
  across time. To compare, build the old commit in a `git worktree` and pass `--dist <path>/dist`.
- No browser console errors or warnings in any run.

Not done: nobody has played it by hand. The in-app preview (`preview_start` with `.claude/launch.json`) stayed in
"starting" and never spawned vite in this run, so every check above is headless.

## 3. What is committed (`fb8e10c`)

That commit also carries the user's earlier uncommitted work (Seed-san VRM traveler, six mirror-rival duels,
sword poses, chapter palettes, anime finish, Firebase hosting). On top of it:

### 3.1 Enemies: animals replaced by people and a mech

- `src/game/hunt.js`: `HIT` (hp / sword / bolt per kind), `KIND` (AI and body numbers), `CAST` (kind → model file,
  the one prop kept in hand, the attack clip), `NEON` (repaint by source material name), `HEFT` (how far a blow
  moves a body).
- Kinds: `punk`, `enforcer` (melee), `gunner` (ranged), `blade` (lunges), `brute` (siege mech).
- `beastMesh(kind, variant)`: clones the rig, removes every spare weapon prop (`PROPS` regex), normalises height
  to 1.5 m × `KIND.scale`, and gives each body its own materials. Every material uses the shared paint shader
  with a 1×1 white map and `uTint` set to the source colour, so a hit can flash one body (`lay()` scales `uTint`).
- Clip roles are resolved once: `Idle`, `Walk`, `Run`, `Attack`, `Hit`, `Death` (`Walk` falls back to `Run`,
  `Hit` may be missing).
- Models in `public/enemies/` with `SOURCE.md` (Quaternius, CC0).

### 3.2 Combat and physics

All in `src/game/hunt.js` unless noted.

| Mechanic | Where | Rule |
|---|---|---|
| Parry | melee hit check in `update()` | Blow lands while `0 < e.windup < PARRY` (0.28 s): damage × `PARRY_MUL` (1.5), stagger 1.2 s, +0.5 spark |
| Launch | `hurt()` | Source `heavy` or `whirl` sets `e.vy = LAUNCH * HEFT[kind]`; airborne bodies are staggered and cannot wind up |
| Thrown bodies | `hurt()` on death, `shove()` | Knockback × 1.7 and an upward kick; `shove()` runs for the dead too |
| Plunge | attack start in `update()` + `Player.slam()` | Strike while airborne and > 0.7 m up: slam down at 26 m/s; landing hurts everything within 3.4 m for `PLUNGE` (1.4) × sword |
| Gunner | chase branch, `fire()` | Holds `reach` 11 m, backs off inside `keep` 5 m, tracks until `AIM_LOCK` (0.3 s) before firing a 15 m/s bolt |
| Deflect | bolt loop | A swing in its hit window within 2.4 m reverses the bolt (× 1.5) and makes it the player's |
| Blade lunge | windup branch | In the last 0.2 s of windup it sweeps `lunge` 2.4 m forward |
| Swept moves | `sweep()` | One helper for knockback and lunges; stopped by static geometry |

`Player.slam(speed)` is in `src/figure/player.js`.

### 3.3 HUD

- `src/ui.css`: complete rewrite. Dark cut-corner plates, cell health bars (CSS mask), `--hot` (yellow, or red in
  the `spirit` treatment) and `--chapter-accent` (per district), glitch chapter banner, no backdrop blur.
- `src/game/hud.js`: `FOES` (name and threat tier), grid minimap with diamond hostiles, `--cd` custom property
  drives the cooldown drain on ability slots, `critical` class under 30 health.
- `index.html`: menu, loading, hint and controls copy.

### 3.4 Night city

- `src/world/anime.js`: the `cel(shared, role, sway)` material draws seven roles (0 tower, 1 sign, 2 weeds,
  3 crate, 4 lamp head / lane dash, 5 smog, 6 pole). Windows, roof trim and sign bars are drawn in object metres
  (`vObj`, `vSize`), so they hold on rotated and scaled instances. Sky shader has stars, a moon and a painted far
  skyline.
- Towers are cuboid colliders (`{ box, ry }`, consumed in `src/main.js`) and keep clear of every spawn, duel,
  cairn and gate (`keep` list).
- `src/world/land.js` (anime branch): slab joints, wet-road neon streaks, a soft neon wash at the road edge. A hard
  kerb line was tried and removed: the way mask is ragged and every tooth showed.
- `src/game/chapters.js`: six districts. Palette keys kept their old names: `grass` = pavement, `path` = road,
  `foliage` / `leafLight` = the two neons, `color` = HUD accent.
- `update()` in `anime.js` also sets `uRim` and `uBounce` from the district colours.

## 4. What is uncommitted

Modified: `AGENTS.md`, `README.md`, `index.html`, `public/CREDITS.md`, `public/enemies/SOURCE.md`,
`src/figure/input.js`, `src/figure/player.js`, `src/game/chapters.js`, `src/game/duel.js`, `src/game/hud.js`,
`src/game/hunt.js`, `src/game/levels.js`, `src/main.js`, `src/ui.css`, `src/world/anime.js`, `src/world/land.js`,
`tools/gameplay.mjs`.
New: `src/game/ride.js`, `tools/street.mjs`, `public/enemies/civ.glb`, `public/enemies/runner.glb`, this file.

### 4.1 Bigger map

- `SCALE = 2.2` in `src/game/chapters.js` is the single stretch factor. It is applied to: chapter `BOUNDARIES`,
  `DUELS` (`duel.js`), `LEVELS` and `GOAL` (`levels.js`), `WAY`, `START`, `CLEARING` and the climb in `land.js`,
  and the shade box in `main.js`.
- Ceiling: the land map is `EXTENT` = 256 m each way (`src/oilpaint/common.js`, shared with the night scene). At
  2.2 the clearing's far edge is at z ≈ −232. A larger `SCALE` needs a larger `EXTENT`, which changes the night
  scene too.
- `levels.js`: the hand-written list is `BASE` at the old small scale. `LEVELS` is `BASE` scaled, with every crew
  doubled: each spot gets a twin 7 m back up the way and 7 m across it (a mech's twin is an enforcer). 38 crew.
  `demoLevels()` now throws if any spawn lies outside its own gates (it would be unreachable and the level could
  never clear).
- `loadProgress()` ignores the saved `x`, `z` and returns the cleared level's own cairn, so saves written before
  the map grew still land correctly. The save format is unchanged.
- `land.js`: `ROAD = 3.6` (half-width). The road mask, the worn dip, tower setback, lamp and dash offsets and
  crate clearance all read it. `heightField()` now defaults to 512 cells over `EXTENT`.
- `anime.js`: 847 towers within 120 m of the way, 10 240 weeds, 344 crates, lane dashes every second way sample,
  a lamp every eighth.
- Cairns (`hunt.js`, level mode only): scaled 2.3×, moved 8 m off the way, a 26 m light beam, trigger radius 7 m.
  The night scene keeps the old size.
- Gate collider widened to 260 m half-extent.

### 4.2 Vehicles (`src/game/ride.js`, new)

- `RIDES`: `car` (top 25 m/s, accel 12, brake 24, grip 7, drift 1.3, slip 6, steer 0.62) and `bike` (top 31,
  accel 17, brake 22, grip 9, drift 1.7, slip 7.5, steer 0.5, leans).
- `drive(v, spec, { throttle, steer, hand }, dt, grounded)` is pure and is the whole handling model: engine force
  fades with (v/top)², the same pedal brakes when it opposes motion, steering lock halves at `calm` m/s, yaw
  follows a bicycle model, sideways speed is scrubbed at `grip` until it passes `slip` or the handbrake is on,
  then at `drift`. Positive `steer` is the player's right, which is *decreasing* yaw in this world.
- `createRides({ scene, shared, player, heightAt, hunt, route, checkpoints, inBounds, settle })` parks a car and
  a bike past every checkpoint and returns `{ update, leave, rides, driven }`.
- `step()`: three rays ahead (both corners and the middle) against static colliders only
  (`QueryFilterFlags.EXCLUDE_KINEMATIC`), velocity reflected off the hit normal with 0.3 restitution, crash damage
  to the driver above 11 m/s into the wall, ground from `heightAt` under both axles, free flight under
  `GRAVITY` 20 when the ground falls away, pitch and roll from the land, bike lean from yaw rate × speed.
- Ramming: any hunt body inside the vehicle's oriented box and ahead of its travel is passed to `hunt.ram()`.
- Seat rules: `G` enters within 3.4 m, leaves below `BAIL` 9 m/s to whichever side is free. A fall or death calls
  `leave(true)` through `respawn()` in `main.js`.
- Each vehicle has a kinematic body and cuboid collider so the player and crews walk round it.
- A vehicle left more than 60 m from home and 110 m from the player is put back.
- Models are built in code from painted solids (`profile()` extrudes a side silhouette). No model files.

### 4.3 Wiring

- `src/figure/input.js`: `ride` (key `G`, pad button 8).
- `src/main.js`: in `tick()`, `const driving = rides?.update(dt, inp, follow) ?? false`. While driving,
  `player.update` is skipped, the out-of-bounds respawn is skipped, combat inputs are masked before
  `hunt.update`, and `man.object.visible` is false. `settle()` is the shared "start animation from standing"
  helper used by `respawn()` and by `ride.js` on leaving.
- `src/figure/player.js`: `castRay(origin, dir, max, exclude, still)`; `still = true` ignores kinematic bodies.
  The follow camera passes `still = driving`, otherwise it clips into the car it follows.
- `hunt` now returns `hud`, `ram(e, speed, ux, uz)`, `jolt(k)` (camera kick) and `crash(n)` (driver damage).
- `createHunt` takes an optional `camera`.
- `window.lab.rides` is the test handle.

### 4.4 Citizens and thieves (`src/game/hunt.js`)

- Two extra kinds outside `KINDS`: `civ` and `thief`, both `civ: true`, `aggro: 0`. They are created only when
  `levels` and `route` are given (autumn), four per level: three citizens walking a two-point beat on the pavement
  (`KERB` 5.4 m from the middle of the way) and one hooded runner doing the level's whole length.
- They are ordinary hunt bodies with `seg: -1`, so every weapon and the bumper hits them, and they never count
  toward clearing a level. They return 45 s after dying, in level mode too.
- Entity fields added: `walk` (two points), `leg`, `angry` (seconds left hostile), `thief` (boolean), `rammed`
  (ram cooldown).
- `blame(e, killed, provoked)` is the whole rule:
  - thief: nothing, and one spark back on a kill;
  - already hostile (`provoked`): nothing;
  - otherwise −`GUILT` (15) health, or −30 on a kill, never below 1; a kill sets every citizen within
    `MOB.radius` (34 m) to `angry = MOB.time` (30 s), `aggro = 18`.
- Two runners in three are thieves (`i % 3 !== 2` over the level index). A thief has a small pink box at the hip;
  the third runner is a jogger and counts as a citizen.
- Dying calms the street. The swing's aim assist ignores calm citizens.
- `seen(e, pd)`: a body is drawn only within `SEEN` 85 m and inside the camera frustum (last frame's), or within
  9 m. Animation mixers are not advanced for unseen or frozen bodies.

### 4.5 HUD additions

- `hud.ride(state)`: speed read-out (`.ride`), a clickable prompt (`.ride-prompt`, sends `KeyG`), sliding and
  airborne states.
- `.heat` banner with the mob's remaining seconds (`heat` is passed into `hud.update`).
- Minimap: calm citizens are small pale squares; hostile ones are diamonds like any enemy.
- Target panel: `Citizen` / `Hooded runner`, with a line on what hurting them costs.

### 4.6 Tests and tools

- `tools/street.mjs` (new): the browser check for sections 4.2 and 4.4. `--shots <dir>` saves two screenshots.
- `tools/gameplay.mjs`: every fixed place is measured from `plan[0].checkpoint.z` (`Z0`); it moves the street's
  people out of the way before measuring combat.
- `demo()` in `hunt.js` and `demoRide()` in `ride.js` assert the new numbers; extend them when tuning.

## 5. Open items, in rough order of value

1. **Play it by hand.** Feel of the car, the bike, the camera at the wheel, and pedestrian density are untested
   by a person.
2. **Ramming trivialises levels.** One hit at speed kills a punk; two kill an enforcer. Options: vehicle health,
   crews that dodge, or gates that close the level to wheels.
3. **No moving traffic.** Vehicles move only when driven.
4. **Vehicles do not collide with each other and cannot roll over.** The model is a tyre model on the height
   map, not a rigid body. The vendored Rapier build does export `DynamicRayCastVehicleController` if a real
   chassis is wanted; crews are kinematic capsules, so a dynamic chassis would treat them as walls unless that
   is handled.
5. **Citizens freeze beyond 36 m** (the same AI cut-off as crews) and never flee. A fleeing reaction and a
   longer walking range would read better.
6. **Provenance of `civ.glb` and `runner.glb`.** They are three.js re-exports from a mirror, not the author's
   files. `public/enemies/SOURCE.md` says so. Replace with originals from quaternius.com if it matters.
7. **Stale names.** The scene key is still `autumn`; `forest` in `main.js` is the city; some toasts still say
   "clearing"; `AGENTS.md`'s mindmap still lists `forest.js`, `floor.js` and "5 levels".
8. **Unused assets still ship**: `public/creatures/` (fox, demons) and `public/animals/` (about 3 MB of the
   build). They were left because they are the user's files.
9. **Gunner shots only threaten a standing or mid-swing player.** Running sideways always dodges. Leading the
   shot would fix it.
10. **No per-level stat scaling.** Difficulty rises through crew mix only.
11. **Touch**: the stick steers and the Jump button is the handbrake, the prompt button enters, but there is no
    on-screen control to leave a vehicle.
12. **Night scene** got the new kinds (`punk`, `enforcer`, `gunner`, `blade`) but no city, vehicles or citizens.

## 6. Things that cost time, so they do not cost it again

- A vehicle's own collider is kinematic. Anything that raycasts *from* a vehicle or *for* its camera must pass
  `EXCLUDE_KINEMATIC`, or the car stops on itself.
- Yaw convention: forward is `(sin yaw, 0, cos yaw)` and screen-right is `(−cos yaw, 0, sin yaw)`. Turning right
  lowers yaw. Local +x of a model is the player's left.
- KIND field names must not collide with per-entity state. `windup`, `body` and `tell` are state, so the KIND
  fields are `wind`, `cap` and so on.
- Quaternius rigs come with several materials and no textures; the paint shader takes one palette. The white-map
  plus `uTint` route gives per-material colour with no shader change. Textured rigs (the runner) pass their own
  map with tint 1.2.
- Body meshes have `frustumCulled = false` because their bounds are wrong; visibility is decided in `seen()`.
- Tower fill was a third of the frame until solids were drawn front-face only and towers were ordered nearest the
  street first.

## 7. Continuation — vehicles, imported assets and verification (2026-11-14)

The user asked to finish the larger-map/vehicle/street work, then supplied Unity, OpenGameArt, itch.io,
Convai and Fab links for better assets. Existing uncommitted work was preserved. Nothing committed or pushed.

### Gameplay and physics

- Kept the existing 2.2× map, 2.3× checkpoint meshes, 7 m checkpoint trigger, 26 m beams and doubled crews.
- `ride.js`: 12 parked vehicles plus 6 traffic vehicles, one per district. Traffic follows a lane, brakes for
  people/vehicles, stops inside its district, and only recycles when both ends are out of sight and the starting
  lane is clear. G takes a traffic vehicle below 2 m/s and removes its AI.
- Vehicle physics uses substeps of at most 1/120 s, Rapier cuboid sweeps against static solids, oriented-box
  chassis contacts, mass-weighted impulses, yaw impulses and impact-driven roll. Heightfield triangles are
  explicitly excluded from horizontal sweeps: `heightAt` supplies terrain support. Otherwise triangle edges
  catch the bumper. Pair separation is swept too, so it cannot push a parked car through a wall.
- Vehicles can settle overturned and cannot drive upside down. Leave with G, then G beside the stopped wreck
  rights it. Exits check space for the player's capsule; the clickable G button remains visible while driving.
- This remains **arcade tyre/chassis physics**, not a dynamic Rapier chassis or a full suspension simulation.
- Ramming armed crews is capped at `RAM.crew = 42`; a 60 HP punk survives one hit. A hit also takes more vehicle
  momentum. Bystanders still take speed-based damage up to 130. Only driven/coasting/player-shunted vehicles
  earn guilt; ambient traffic yields and does not blame the player. Innocent joggers can now hurt the player
  when they join a mob (the runner kind previously had zero damage).

### Imported art

- `public/people/citizen-female.glb`: pistachio's original Human Basemeshes female, CC0.
- `public/people/runner.glb`: the same author's male, CC0, with runner headgear.
- `public/people/citizen-male.glb`: Clint Bellanger's original Base Human Models male, CC BY 3.0.
- All three have clothing materials, headgear/eyes and six authored clips: Idle, Walk, Run, Punch, HitReact,
  Death. Gender alternates within citizen beats; outfit colours vary through the existing paint shader.
  `CAST.civ.alternate` is the second source; sources are loaded as arrays and chosen per body.
- `public/vehicles/eclipse.glb`: Eclipse SRC body by shotman_16 from Jreo's VitaVehicle, CC BY 4.0, reached through
  the supplied itch.io physics listing. Its Godot packed mesh arrays were decoded as data (no scripts run),
  converted to the existing chassis dimensions and merged into 6 material roles. Wheels/bike remain original.
- `createRides()` is now async to load the car. `main.js` awaits it before warming shaders.
- Original download links, attribution, licenses and checksums live in `public/people/SOURCE.md` and
  `public/vehicles/SOURCE.md`; `public/CREDITS.md` is updated. The old mirrored `enemies/civ.glb` and `runner.glb`
  remain on disk but are no longer loaded.
- `tools/import-street-assets.py` reproduces the conversions with Blender 4.5. Sources remain under ignored
  `.test/import-source/`. Blender was run with auto-execution disabled; the temporary app image was unmounted
  and removed. The 2019 source contains a broken legacy shape-key pointer; Blender discards it, and we export
  its original cage/weights without shape keys. This is handled explicitly in the converter.
- Unity's suggested vehicle publisher disallows runtime GLB distribution in browser games. That pack was not
  imported. Unity/Unreal engine packages and the Convai SDK were not installed into this Three.js game.

### Verification

- `node tools/assets.mjs`: all 4 GLBs pass finite geometry/skin/animation checks, embedded buffers, full meshes,
  credit metadata, humanoid rigs and required clips. Models: female 1302 tris / 399 KB; male 960 / 212 KB;
  runner 1308 / 401 KB; car 2912 / 208 KB.
- Node demos (`hunt`, `levels`, `ride`, `chapters`), syntax checks, production build and `git diff --check` pass.
  Built payload is 33.0 MB. No runtime dependencies added.
- Final `tools/gameplay.mjs`: PASS, all six levels/rival lessons, save/restore, pause and free roam.
- Extended `tools/street.mjs` passes: car 18.8 m/s after 2.5 s; ram 42 damage / punk alive / 1.22 m throw;
  wall rebound −5.6 m/s and 14 health loss; bike lean 0.58 rad; traffic travels 10 m without a driver and stops
  5.55 m from a pedestrian; rear impact pushes parked car 4.95 m; broadside rolls bike and G recovers it;
  actual car rams cost 30 health + hostile witnesses for a citizen, zero + calm witnesses for a thief.
- Handling/combat checks park traffic away; traffic is measured separately in a fresh world. The gameplay tool
  also disables other attackers during its single-attacker dodge check, preventing random second-crew hits.
- Browser asset screenshots: `shots/asset-people.png`, `shots/asset-car.png`. Aurora smoke loads without errors.
- Back-to-back `tools/fps.mjs --seconds 8`: original pre-continuation tree **218 fps**, final imported-assets
  tree **224 fps** on the same machine state. Original build snapshot: `/tmp/oil-paint-before.155Aja/dist`.
- In-app browser used for bike and imported Eclipse boarding, driving, braking and clickable exit; zero console
  warnings/errors in the imported-car run. Preview left paused. Screenshot: `shots/imported-preview.png`.

Remaining: human playtesting for feel; civilians still freeze past the AI cut-off and do not flee. Traffic is
local district trips with out-of-sight recycling, not a city-wide road network. No Convai voice/dialogue integration.
Other historical open items (gunner leading, unused retired assets, touch polish) remain outside this work.
- In headless tests, a calm citizen is not aim-assisted. A test that swings at one must face it itself.
- The save stores `x`, `z` but they are no longer read.
