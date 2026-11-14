# Eclipse SRC — VitaVehicle model

The car body is **Eclipse SRC by shotman_16**, distributed in **Jreo's VitaVehicle** project, reached through
the user's [itch.io physics assets](https://itch.io/game-assets/free/tag-physics) link.
The [author's VitaVehicle page](https://jreo.itch.io/rcp4) lists assets under
[CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) and credits the car in the
[project README](https://github.com/jreo03/g-rcp2).

Original data: [`base car.tscn`](https://github.com/jreo03/g-rcp2/blob/beta/g-rcp2/base%20car.tscn), downloaded
2026-11-14. SHA-256: `5cfe2b7c2cd171e396262513056acaa664e01efd4a4261e6566409861ecc1ca3`.

`tools/import-street-assets.py` decodes only the car's packed position/index arrays (no Godot scripts), converts
axis/winding/scale to a 4.2 m × 1.9 m chassis, and merges surfaces into six material roles. Komorebi repaints
the body, glass and lamps through its existing materials and adds its original animated wheels/neon trim.
`eclipse.glb` embeds this attribution. The bike and the handling code remain original Komorebi work.

To reproduce, save the original TSCN as `.test/import-source/car.tscn`, obtain the people sources as described
in `../people/SOURCE.md`, then run the Blender import script and `node tools/assets.mjs`.

The suggested Unity Free Stylized Vehicle package was reviewed but not included. Its publisher explicitly
disallows runtime GLB distribution in browser games in their [published response](https://itch.io/profile/awbmecreations).
Unity/Unreal/Convai engine plugins are not installed in this vanilla Three.js project.
