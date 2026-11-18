# Authored AA cyberpunk street art (`public/street/`)

All twenty-six binary GLB 2.0 models in `public/street/` are original headless Blender 4.2 LTS
assets authored programmatically for Komorebi (`Original AA Blender art authored for Komorebi (CC0/MIT).`).
They have no external mesh or texture dependencies: every surface is closed-manifold geometry (`FrontSide`
culling safe) batched by material role (`VertexPaint` with baked vertical and cavity/ridge ambient
occlusion in `COLOR_0`, plus flat `Signal` emissive accents) so the game renders them directly through
the shared `paint3` cel/oil shader (`toneMapped: false`, at most 6 meshes per asset).

## Asset roster

- **Mission & service props (`tools/build_aa_mission_props.py`):**
  `terminal.glb` (Holo-Uplink Relay Terminal), `supply-crate.glb` (Syndicate Munitions Cache),
  `power-core.glb` (Aethelgard Signal Jammer / Reactor Core), `beacon.glb` (Corp Broadcast Spire)
  and `barricade.glb` (Hardlight Traffic & Sector Barrier).
- **Neo-Kyoto street landmarks (`tools/build_aa_landmarks.py`):**
  `noodle-stall.glb` (Night Ramen & Tea Stall) and `echo-shrine.glb` (Old Kyoto Memory Echo Pedestal).
- **Hero blades & spirit lantern (`tools/build_aa_blades.py`):**
  `cyber-katana.glb` (Komorebi Edge), `ren-odachi.glb` (Ren's Architect Odachi) and
  `spirit-lantern.glb` (Komorebi Seed Lantern).
- **Vehicles & seated rider (`tools/build_aa_vehicles.py`):**
  `coupe.glb` (Kestrel Cyber-Sedan, 4 axle pivots `Wheel_FL`..`Wheel_RR`),
  `sports.glb` (Kestrel GT-X Hypercar, 4 axle pivots),
  `van.glb` (Aethelgard Armored Transport Van, 4 axle pivots),
  `motorbike.glb` (Razor Kaneda Superbike, 2 axle pivots `Wheel_F`/`Wheel_R`),
  `scooter.glb` (Komorebi Neo-Scooter, 2 axle pivots) and
  `rider.glb` (Seated Cyber-Rider, separated from the two-wheelers so wheels spin independently
  and parked bikes hide the rider).
- **Civilians & street specialists (`tools/build_aa_civilians.py`, `tools/build_aa_specialists.py`):**
  `resident.glb` (Neo-Kyoto Streetwear Citizen), `mechanic.glb` (District Cyber-Artisan),
  `vendor.glb` (Night-Market Vendor / Field Medic), `courier.glb` (Syndicate Hooded Data-Runner)
  and `executive.glb` (Aethelgard Corporate Executive) — each skinned to a 21-bone humanoid skeleton
  with 24 fps `Idle`, `Walk`, `Run`, `Punch`, `HitReact` and `Death` clips.
- **Enemy infantry & elites (`tools/build_aa_infantry.py`, `tools/build_aa_elites.py`):**
  `foe-punk.glb` (Neon Viper Street Punk), `foe-enforcer.glb` (Aethelgard Riot Enforcer),
  `foe-gunner.glb` (Hazmat Pulse Gunner, adds `Idle_Shoot`) and `foe-blade.glb` (Razor Ronin
  Cyber-Assassin, adds `Sword_Slash`) on 21-bone humanoid skeletons, plus `foe-mech.glb`
  (Aethelgard Titan Siege Mech, adds `Attack`) on a 25-bone walker rig.

## Reproducing with Blender 4.2 LTS

All eight builders share `tools/blender_aa_lib.py` (chamfered manifold primitives, vertex-colour AO,
role batching, skinning, animation baking and GLB export). Rebuild from the repository root and
validate every asset with `tools/assets.mjs`:

```sh
BLENDER=${BLENDER:-.test/blender/blender}
for script in \
  tools/build_aa_mission_props.py \
  tools/build_aa_landmarks.py \
  tools/build_aa_blades.py \
  tools/build_aa_vehicles.py \
  tools/build_aa_civilians.py \
  tools/build_aa_specialists.py \
  tools/build_aa_infantry.py \
  tools/build_aa_elites.py
do
  "$BLENDER" --background --disable-autoexec --python "$script"
done
node tools/assets.mjs
```

Do not edit GLB bytes by hand; regenerate from the Python sculptors above.

## Historical pipeline note

The initial thirteen-model review pass prepared external Blender previews through
`tools/prepare-neon-assets.py` (retained in `tools/` for reference). Those thirteen models and
thirteen additional landmarks, blades, vehicles, specialists and enemy rigs are now generated
directly from source by the `tools/build_aa_*.py` suite above. Earlier character, vehicle and
enemy files in `people/`, `vehicles/` and `enemies/` remain on disk intact with their original
credits (`people/SOURCE.md`, `vehicles/SOURCE.md`, `enemies/SOURCE.md`).
