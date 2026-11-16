---
name: aiverse-voxel-gen
description: Generate infinite AIVERSE voxel worlds chunk by chunk with the `voxel-gen` package - height noise, biomes, caves, ore veins, trees and plants across chunk borders - deterministic from a seed and configured in JSON.
---

# voxel-gen

API: `libraries/include/aiverselib/systems/voxelgen/VoxelGen.h` and `VoxelGenData.h`; example config
`examples/libraries/voxel/worldgen.json`; reference `docs/voxel.md`.

1. Configure `voxelGen` by block names (stone, water, bedrock, biomes with surface / filler / shore / trees /
   plants, ores with Y ranges); `readVoxelGen(json, registry)` validates with path errors.
2. `VoxelGenerator(config).generate(chunk)` for every chunk that is not parked; `heightAt(x, z)` for spawn points
   and surface queries. Any order and any thread gives identical chunks.
3. Tune with `aiverse_voxel --content <dir>` (timings, chunk counts) and screenshots of a real view.
4. Verify: generation tests (determinism, golden hash, biomes, trees, caves, ores) in
   `tests/libraries/voxel_tests.cpp`. Changing the algorithm changes the golden hash - only on purpose, noted in
   `docs/voxel.md`, because saves keep only edited chunks.

Not done: structures (villages, dungeons; W3 in `docs/genre-implementation.json`), rivers, overhang terrain.

Build selection: `AIVERSE::lib_voxel_gen`; native OBJECT modules call
`aiverse_native_use_libraries(target voxel-gen)`. Only this header library and its declared dependencies are
selected. See `skills/catalog.json` for dependency and responsibility routing.
