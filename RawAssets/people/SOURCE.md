# Street people — original OpenGameArt meshes

Retained legacy assets. The active street uses the reviewed Blender set in [`../street/SOURCE.md`](../street/SOURCE.md).

Retrieved 2026-11-14 directly from the authors' OpenGameArt uploads; these replace the mirrored Quaternius
pedestrians in the active city. The old files in `enemies/` are retained but are not loaded.

| Game file | Original mesh / author | License |
|---|---|---|
| `citizen-female.glb` | Female from [Human Basemeshes](https://opengameart.org/content/human-basemeshes), **pistachio** | [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/) |
| `runner.glb` | Male from Human Basemeshes, **pistachio** | CC0 1.0 |
| `citizen-male.glb` | Male from [Base Human Models (Low Poly)](https://opengameart.org/content/base-human-models-low-poly), **Clint Bellanger** | [CC BY 3.0](https://creativecommons.org/licenses/by/3.0/) |

Adaptations by Komorebi: remove subdivision, assign clothing/shoe/skin materials, add a cap/eyes/runner visor,
normalise the relaxed stance, and author six in-place animation clips (Idle, Walk, Run, Punch, HitReact, Death).
The base meshes and skin weights come from the credited artists. No source scripts are executed.

To reproduce with Blender 4.5, download/extract into `.test/import-source/`:

- [Human Basemeshes.zip](https://opengameart.org/sites/default/files/Human%20Basemeshes.zip) → `pistachio/Human Basemeshes.blend`.
- [human.zip](https://opengameart.org/sites/default/files/human.zip) → `bellanger/human.blend`.
- The car source described in [`../vehicles/SOURCE.md`](../vehicles/SOURCE.md) → `car.tscn`.

Run `blender --background --disable-autoexec --python tools/import-street-assets.py`, then `node tools/assets.mjs`.
Blender reports and discards a broken legacy shape-key pointer in the 2019 source; the export uses its original
low-poly cage and armature, without shape keys. Original downloads remain untouched.

SHA-256 of the downloaded archives:

```text
pistachio.zip  b873af7825484ad2a58da186650045f55883589109a2ba9f2c440c7a7fa96fb7
bellanger.zip cc08de6b1a594132f6e82ad017e33edb2cfa525334ee39725275ef3803dd74d7
```
