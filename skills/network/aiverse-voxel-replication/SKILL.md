---
name: aiverse-voxel-replication
description: Stream an AIVERSE voxel world from an authoritative server to clients with the `voxel-replication` package - view radius, nearest-first chunk snapshots in a byte budget, unloads, edit batches by revision, automatic resend and a client mirror.
---

# voxel-replication

Host side, never inside simulation ticks. API: `libraries/include/aiverselib/net/voxel/VoxelReplication.h`;
reference `docs/voxel.md`. Session, admission and transports: `skills/network/aiverse-network-session`,
`skills/network/aiverse-session-admission`; the authoritative FPS path is described in `docs/fps.md`.

1. Server: per client a `VoxelPeer` and a `VoxelView` (center chunk, radius, vertical range) from the player's
   position. Each network tick call `pumpVoxelPeer(config, world, view, peer, world.edits)` for every client,
   send the returned messages on a reliable ordered channel, then clear `world.edits`.
2. Client: `applyVoxelMessage(mirror, registry, bytes, &oldBlocks)`; light inserted chunks (`lightPending`) and
   relight applied edits, then re-mesh. The mirror is presentation; the client predicts its own digging and
   accepts the server's result.
3. Budgets: `maxBytes` / `maxChunks` per pump; `pendingChunks` shows the backlog.
4. Verify: replication tests in `tests/libraries/voxel_tests.cpp` (budget, edits, lost edit resend, moving view,
   malformed bytes) and `aiverse_voxel` (mirror equals server).

Not done: compression of snapshots, wiring into `engine/net` authority messages, interest by line of sight.

Build selection: `AIVERSE::lib_voxel_replication`; native OBJECT modules call
`aiverse_native_use_libraries(target voxel-replication)`. Only this header library and its declared dependencies
are selected. See `skills/catalog.json` for dependency and responsibility routing.
