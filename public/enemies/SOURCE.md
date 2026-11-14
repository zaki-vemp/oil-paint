# Enemy cast — Quaternius, CC0 1.0

All five files are unmodified models by Quaternius, released CC0 1.0
(https://creativecommons.org/publicdomain/zero/1.0/). Only the file names changed.

| File | Original | Pack |
|---|---|---|
| `punk.gltf` | `Character_Enemy.gltf` | [Toon Shooter Game Kit](https://quaternius.com/packs/toonshootergamekit.html) |
| `enforcer.gltf` | `Character_Soldier.gltf` | Toon Shooter Game Kit |
| `gunner.gltf` | `Character_Hazmat.gltf` | Toon Shooter Game Kit |
| `blade.glb` | `Character.glb` | [Cyberpunk Game Kit](https://quaternius.com/packs/cyberpunkgamekit.html) |
| `mech.glb` | `Enemy_Large.glb` | Cyberpunk Game Kit |

| `civ.glb` | `v3/runner_Casual_Female.glb` | Ultimate Animated Character Pack (re-export, see below) |
| `runner.glb` | `v2/b_Ninja.glb` | Ultimate Monsters (re-export, see below) |

These two pedestrian files are now **retired and not loaded**; active citizens/runners use original OpenGameArt meshes (`../people/SOURCE.md`).

`civ.glb` and `runner.glb` are not the author's original files: they are copies re-exported with three.js by the
mirror repository below (its README states CC0 Quaternius assets). The models are Quaternius designs released CC0;
if provenance has to be airtight, replace both with the originals from quaternius.com.

Download mirrors (retrieved 2026-11-14):
- https://github.com/jwh781101-ship-it/nantu-arena-assets (Toon Shooter Game Kit)
- https://github.com/trebeljahr/quaternius-showcase/tree/main/public/glb/cyberpunk_pack (Cyberpunk Game Kit)

In game the spare weapon props are removed, height is normalized, and each source
material colour is repainted through the shared cel shader (src/game/hunt.js, `CAST` / `NEON`).
