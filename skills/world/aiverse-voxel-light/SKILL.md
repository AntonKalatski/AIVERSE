---
name: aiverse-voxel-light
description: Compute Minecraft-style sky and block light (0..15) in an AIVERSE voxel world with the `voxel-light` package - full light for new chunks, incremental relight after each edit, light levels for spawning and growth.
---

# voxel-light

Use with `skills/world/aiverse-voxel-world`. API: `libraries/include/aiverselib/systems/voxellight/VoxelLight.h`;
behaviour and numbers: `docs/voxel.md`.

1. After inserting chunks call `lightPending(world, registry, maxChunks)` (drains `VoxelWorld::lightDirty`);
   spread it over frames on the client. Stream whole columns: sky enters from the top of the world or of the
   highest resident chunk.
2. After every `VoxelWorld::set` call `relightBlock(world, registry, pos, oldBlock)` (also for edits reported by
   voxel-ticks and replicated edits on clients).
3. `voxelLightLevel(world, pos, skyDarkening)` for spawn rules (night: darkening about 11) and plant growth.
4. Verify: the light tests in `tests/libraries/voxel_tests.cpp` (incremental equals full recompute, roofs,
   torches, holes, glass, leaves, columns shaded from above).

Invariants: derived state - deterministic, never hashed or saved; recompute after load. Changed light marks the
meshes that sample it dirty. Opacity 15 blocks light; spread loses max(1, opacity) per block; sky falls straight
down through opacity 0 without loss.

Build selection: `AIVERSE::lib_voxel_light`; native OBJECT modules call
`aiverse_native_use_libraries(target voxel-light)`. Only this header library and its declared dependencies are
selected. See `skills/catalog.json` for dependency and responsibility routing.
