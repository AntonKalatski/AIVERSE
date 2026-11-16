---
name: aiverse-voxel-ticks
description: Run block updates in an AIVERSE voxel world with the `voxel-ticks` package - flowing water and lava with levels, infinite sources, lava-water reactions, falling sand, random-tick growth - deterministic, budgeted and saved.
---

# voxel-ticks

API: `libraries/include/aiverselib/systems/voxelticks/VoxelTicks.h`, `VoxelTicksData.h`, `VoxelTicksPersistence.h`;
example `examples/libraries/voxel/ticks.json`; reference `docs/voxel.md`.

1. Block behaviour comes from block data (`fluid`, `fluidSpread`, `fluidTicks`, `infiniteSource`, `falls`,
   `growInto`, `growChance`); reactions from `voxelTicks.reactions`.
2. After each host edit call `notifyVoxelChange(state, world, registry, rules, pos, tick)`.
3. Once per tick: `stepVoxelTicks(rules, state, world, registry, tick, simulationRandom, &activeChunks)`; relight
   every reported change with its old block. `budgetHit` means work carried to the next tick.
4. Save with `storeVoxelTicks` / `loadVoxelTicks` (record 121) together with the world; hash with `voxelTicksHash`.
5. Verify: tick tests in `tests/libraries/voxel_tests.cpp` (spread, drain, falls, sources, sand, reactions,
   determinism, save, budget, growth).

Not done: redstone-like signals, fire spread in 3D (2D grid fields are `docs/fields.md`), pressure.

Build selection: `AIVERSE::lib_voxel_ticks`; native OBJECT modules call
`aiverse_native_use_libraries(target voxel-ticks)`. Only this header library and its declared dependencies are
selected. See `skills/catalog.json` for dependency and responsibility routing.
