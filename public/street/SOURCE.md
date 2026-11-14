# Reviewed neon street art

These thirteen models are original Blender assets authored for this project and reviewed
as separate preview renders before integration. They have no downloaded mesh or texture dependencies.

- Terminal, supply crate, portable power core, uplink beacon and district barricade.
- Kestrel coupe, Parcel delivery van, Razor motorcycle and courier scooter.
- The seated motorcyclist, separated from the reviewed bike/rider assembly so the
  game can animate the same bike's wheels and hide the rider when it is parked.
- Market resident, street mechanic and hooded courier. The game versions have a
  seventeen-bone skeleton and Idle, Walk, Run, Punch, HitReact and Death clips.

`tools/prepare-neon-assets.py` prepares the reviewed GLBs through Blender: it batches
surfaces into authored vertex colours, keeps wheel pivots, reduces geometry and
builds the street characters' skinning and in-place animations. The runtime uses
the existing cel painter, vehicle physics and NPC AI. Original preview sources are
kept separately from the game, in the `oil-paint-asset-previews` review folder.

To reproduce with Blender 4.5:

```sh
blender --background --factory-startup --python tools/prepare-neon-assets.py -- \
  --source /path/to/oil-paint-asset-previews
node tools/assets.mjs
```

Do not edit GLB bytes by hand. Rebuild from the reviewed sources with this script.
The earlier people and Eclipse files remain in `people/` and `vehicles/`, with their
original credits, but are no longer the active street models.
