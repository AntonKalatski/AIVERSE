---
name: aiverse-voxel-nav
description: Move AIVERSE mobs through a changing voxel world with the `voxel-nav` package - A* over blocks with steps, drops, swimming and diagonals, node budgets, and spawn spot search by light level at night or in caves.
---

# voxel-nav

API: `libraries/include/aiverselib/bots/voxelnav/VoxelNav.h`; reference `docs/voxel.md`. Behaviour of a mob
(wander, hunt, flee) comes from `skills/genres/aiverse-survival-ecology` or `skills/ai/aiverse-behavior`.

1. Describe the body: `VoxelAgent{height, maxStep, maxDrop, swim, diagonal, maxNodes}`.
2. `voxelPath(world, registry, agent, startFeet, goalFeet, goalRadius)`; keep `maxNodes` small and spread
   queries over ticks; cache the path and re-plan when a block on it changes.
3. Follow the path with `voxelMoveBox` (voxel-world) and the locomotion modes of
   `skills/gameplay/aiverse-character-locomotion` (jump on step-up).
4. Spawning: `voxelSpawnSpots(world, registry, agent, rule, center, count, simulationRandom)` with `maxLight`,
   `skyDarkening` for night, allowed ground blocks and distance from players.
5. Verify: navigation tests in `tests/libraries/voxel_tests.cpp`; `aiverse_voxel` reports a real path time.

Not done: flying and climbing mobs, door use, hierarchical search for long distances.

Build selection: `AIVERSE::lib_voxel_nav`; native OBJECT modules call
`aiverse_native_use_libraries(target voxel-nav)`. Only this header library and its declared dependencies are
selected. See `skills/catalog.json` for dependency and responsibility routing.
