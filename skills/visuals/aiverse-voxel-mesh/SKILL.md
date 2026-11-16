---
name: aiverse-voxel-mesh
description: Build AIVERSE voxel chunk meshes with the `voxel-mesh` package - face culling, greedy merging, ambient occlusion, smooth light, opaque / cutout / translucent layers, plants and fluids - on worker threads, for upload through the engine renderer.
---

# voxel-mesh

Presentation only. API: `libraries/include/aiverselib/systems/voxelmesh/VoxelMesh.h`; numbers in `docs/voxel.md`.

1. For each chunk in `VoxelWorld::renderDirty`: on the owner thread copy `voxelMeshInput(world, chunk)` (18^3
   neighbourhood), clear the entry, mesh with `meshChunk(input, registry, options)` on a worker, upload on the
   render thread. Drop a result whose `revision` is older than the chunk's current one.
2. Draw three passes: opaque, cutout (alpha test: leaves, plants, torches), translucent (blended, sorted with
   `sortTranslucent` when the camera moves).
3. Vertex data: chunk-local position, block UV (texture array with repeat, or fract() for an atlas), texture
   layer, normal, AO 0..3, sky and block light 0..15 - the shader mixes sky light with the time of day.
4. Positions: add the chunk origin relative to the camera origin in Fixed before converting to float
   (`engine/world/SpatialWorld.h` relativeTo).
5. Verify: mesh tests in `tests/libraries/voxel_tests.cpp` (winding, upright side textures, greedy, AO, layers),
   `aiverse_voxel` timings, and real screenshots (`skills/verification/aiverse-visual-qa`).

Not done: packed vertex format (28 bytes now), GPU upload and shaders in the engine renderer, LOD for distant chunks.

Build selection: `AIVERSE::lib_voxel_mesh`; native OBJECT modules call
`aiverse_native_use_libraries(target voxel-mesh)`. Only this header library and its declared dependencies are
selected. See `skills/catalog.json` for dependency and responsibility routing.
