---
name: aiverse-voxel-world
description: Store and edit a mutable AIVERSE block world (Minecraft-like) - 16^3 chunks, block types from JSON, simulated edits with revisions, parking and region files for saves, Fixed ray picking and box movement against blocks - with the `voxel-world` package.
---

# voxel-world

Use for any game whose world is made of blocks that players and rules change: sandbox, survival, colony
digging, destructible cover. Heightfield maps stay with `skills/world/aiverse-terrain`.

Read `docs/voxel.md` first; API: `libraries/include/aiverselib/systems/voxel/VoxelWorld.h`, `VoxelTypes.h`,
`VoxelCodec.h`, `VoxelQuery.h`, `VoxelData.h`, `VoxelPersistence.h`. Example data: `examples/libraries/voxel/blocks.json`.

1. Blocks: `readBlockRegistry(Json)`; air is id 0, ids follow list order - never reorder a shipped list.
2. World: `VoxelWorld(VoxelBounds)` with a vertical chunk range; stream **whole columns** (light needs it).
   Insert generated chunks (`skills/world/aiverse-voxel-gen`) or `unpark` edited ones; `unload` parks edits.
3. Edits: only `set(pos, id, meta, registry)` in the simulation (player commands validated first). It returns the
   old block: pass it to voxel-light `relightBlock` and voxel-ticks `notifyVoxelChange`.
4. Queries: `voxelRaycastSolid` (picking: `block` to break, `before` to place), `voxelMoveBox` for players,
   mobs and items (Y then X then Z, `grounded`), `voxelOverlapsSolid` before placing a block into a body.
5. Saves: `storeVoxelWorld` / `loadVoxelWorld` (record 120); after loading make the saved resident chunks
   resident again before the next tick (unpark edited, regenerate the rest) - then `hash()` matches. Disk:
   `exportRegions` / `importRegion` (checksummed region files of 32x32 columns).
6. Verify: `aiverse_library_voxel_tests`, `aiverse_voxel --verify` (generate, edit, save, restore, compare hash).

Invariants: integers and Fixed only; std::map order; unloaded chunks are solid for queries; light, meshes and
dirty sets are derived and never hashed or saved; `edits` is drained by the host after replication.

Build selection: `AIVERSE::lib_voxel_world`; native OBJECT modules call
`aiverse_native_use_libraries(target voxel-world)`. Only this header library and its declared dependencies are
selected. See `skills/catalog.json` for dependency and responsibility routing.
