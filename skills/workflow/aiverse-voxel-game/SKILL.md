---
name: aiverse-voxel-game
description: Assemble a Minecraft-like AIVERSE game from the voxel packages (world, gen, light, mesh, ticks, nav, replication) plus existing FPS, locomotion, inventory, construction and survival libraries - host loop order, data files, checks and the parts still missing.
---

# Voxel sandbox game

Use when the request is a block-building / mining / survival game. Packages and their order in one tick:

| Step | Package / skill |
|---|---|
| Stream columns near players: unpark or generate, insert | `skills/world/aiverse-voxel-world`, `skills/world/aiverse-voxel-gen` |
| Light new chunks | `skills/world/aiverse-voxel-light` |
| Player commands: dig (break time from `breakTicks`), place (`voxelRaycastSolid` + `voxelOverlapsSolid`), relight, notify ticks | voxel-world, voxel-light, voxel-ticks |
| Inventory, drops, crafting, hotbar | `skills/gameplay/aiverse-inventory`, `skills/gameplay/aiverse-construction` |
| Player and mob movement | `voxelMoveBox` + `skills/gameplay/aiverse-character-locomotion` |
| Block updates (fluids, sand, growth) | `skills/world/aiverse-voxel-ticks` |
| Mobs: spawn by light, path, behaviour | `skills/ai/aiverse-voxel-nav`, `skills/genres/aiverse-survival-ecology` |
| Hunger, temperature, day and night | `skills/genres/aiverse-survival-ecology` |
| Server to clients | `skills/network/aiverse-voxel-replication` |
| Client meshes | `skills/visuals/aiverse-voxel-mesh` |
| Saves | voxel-world record 120, voxel-ticks record 121, plus the libraries' own records |

Steps:
1. Start from `examples/libraries/voxel` (blocks, worldgen, ticks) and measure with
   `aiverse_voxel --content <dir> --radius 6 --verify` before writing game code.
2. Write the game as a native module (`docs/native-modules.md`); keep all voxel state in the module's state and
   save it through NativeState (records above). Use the simulation Random only.
3. Multiplayer: an authoritative server (`docs/fps.md` authority path or `docs/city-server.md` style), not
   lockstep - clients do not simulate the whole world.
4. Check: library tests, `aiverse_voxel --verify`, headless restore of the game, screenshots on desktop and a
   low-end phone profile (`skills/verification/aiverse-mobile-perf`).

Still missing in the engine (track in `docs/genre-implementation.json` W1/W2): GPU upload and shaders for voxel
meshes in the renderer, an FPS-module host that uses voxel collision instead of static chunk collision, wiring
of replication messages into `engine/net`, hotbar / inventory drag-and-drop UI, texture array import for block
art, compression of snapshots, structures generation (W3).

Workflow recipe over the voxel packages and existing libraries; no runtime library of its own.
