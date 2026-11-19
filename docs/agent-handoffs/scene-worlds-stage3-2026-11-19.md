# AIVERSE SceneResource Stage 3 — interrupted-stage handoff (2026-11-19)

Branch: `feature/scene-runtime-multiworld`  
Latest implementation commit at handoff: `918b3302ff99bf88804c4a53d13c93a61f615ee9`  
Status: **source committed, native compile/CTest not executed, NOT production ready**.  
Context: work on the scene manager was interrupted by a separate request to
implement shared/static immutable ECS configuration. That work is isolated in
`feature/ecs-shared-static-components`; it must not be silently merged into
the scene branch without native validation.

## Stage 3 implemented, source-only

- `engine/scene/SceneResourceBackend.h`: generic host-side dependency
  backend. Typed `SceneResourceKind` (Render, Collision, Terrain, Water,
  Weather, Vfx, Audio, Navmesh, Other), bounded staging bytes/resident bytes,
  worker concurrency, max catalog entries, owner-thread publication by
  operations and estimated bytes, pin/reference counts and callback hooks.
  Cancelling worker tasks tombstones them; `poll()` drains finished workers.
  The backend deliberately does **not** create a second GPU/content cache;
  hooks are adapter points for existing resource owners.
- `engine/scene/SceneResourcePipeline.h`: composes
  `SceneLoadCoordinator` with resource prewarm pins and owner-thread
  publications. Only `commit(ticket)` mutates scene membership at a
  deterministic structural/tick barrier; readiness is a host-only signal.
  Pins are released on cancel, commit, unload and restore.
- `engine/scene/SceneLoadCoordinator.h`: added read-only
  `prepared(ticket)` manifest access for dependency warmup, and corrected
  in-flight job accounting across cancel/rollback (tombstoned futures count
  towards the concurrency cap).
- `engine/scene/WorldSceneResourceBridge.h`: concrete adapter to
  existing `world::WorldScene`, `WorldAssets`, `ModelTextureCache`
  and NEREID presentation. Parses/validates world manifests asynchronously,
  publishes real WorldScene instances on graphics owner thread, delegates
  chunk streaming via existing WorldScene::update and releases through
  WorldScene::shutdown. Does **not** claim ready-to-enter physics/collision.
- `tests/ecs_scene_resource_pipeline_test.cpp`: scenarios for typed
  shared leases, staging/resident budgets, bounded publication, failed
  dependencies, additive/replace, COW restore and late worker cancellation.
  Test executable registered in `CMakeLists.txt` as
  `aiverse_ecs_scene_resource_pipeline_test`.

No compilation or runtime execution was available against the user's native
workspace. No actual benchmark or assertion-passing result is claimed.

## Known blocker: additive CollisionStream instances

Current `engine/world/CollisionStream.cpp` constructor rejects a target
`SpatialWorld` containing static collider IDs >= 0x80000000, and each
stream derives its IDs from a fixed 0x80000000-based chunk index. Thus two
independent CollisionStream instances must NOT be attached to one shared
SpatialWorld for additive scenes; their static collider IDs may alias or
initialization will fail.

**Safe next step:** Add a shared collision residency coordinator with
stable per-scene namespace allocation or a single aggregated catalog owning
all static colliders, then prove deterministic ID stability under additive
load/unload, terrain seams, COW rollback and network lockstep.
Do not simply bypass the high-ID check. Do not modify physics/WorldAssets
without coordinating with the world/physics agent.

## Stage 3 source commits

- `b3a82721745cb8c70127786f31fd268f5ade0a98` — resource backend.
- `535df70e5ed90389d9a353808590489e47d50028` — staging and cancellation.
- `eaf7474d5549c57cd0936b023a1be76ba9affb65` — read-only manifest.
- `db03cd1657511c32a4c43b8cbfcca7de9a205548` — scene resource pipeline.
- `7a0eb371c215d8ace45f42cb16870d92c89eb5fc` — regression fixture.
- `c099bee543bb77c12c032ec57335af73b699c531` — CMake test.
- `0ed27e1723eb34bd0e0b376463d69876e9f76d68` — canceled concurrency.
- `de1832fccbca482159c30ff85b399b41a24373e5` — WorldScene bridge.
- `ac33a749727d6cb2ae87b8d0fdd975ea7fe97d2d` — bridge headers.
- `918b3302ff99bf88804c4a53d13c93a61f615ee9` — canceled IO cap.

## First action for next scene agent

```sh
cd ~/Projects/AIVERSE
git fetch origin main feature/scene-runtime-multiworld
git worktree add -b verify-scene-stage3 \
  ../AIVERSE-scene-stage3 origin/feature/scene-runtime-multiworld
cd ../AIVERSE-scene-stage3
git pull --ff-only origin feature/scene-runtime-multiworld
cmake -S . -B build/scene-stage3 -DAIVERSE_BUILD_TESTS=ON
cmake --build build/scene-stage3 --target \
  aiverse_ecs_scene_runtime_test \
  aiverse_ecs_multiworld_test \
  aiverse_ecs_scene_load_coordinator_test \
  aiverse_ecs_scene_resource_pipeline_test --parallel 8
ctest --test-dir build/scene-stage3 --output-on-failure \
  -R '^(ecs_scene_runtime_test|ecs_multiworld_test|ecs_scene_load_coordinator_test|ecs_scene_resource_pipeline_test)$'
```

Then run hierarchy, world streaming and FPS/RTS parity tests before merging.
Record actual result, compiler, SHA and any remaining warnings here.

## Next implementation stages

1. Fix native compile/tests of all stage 1/2/3 scene headers and test suites.
2. Add safe additive physics/collision ownership as discussed above,
   with deterministic stable collider IDs and asset readiness barrier.
3. Add concrete TerraWeave, PHYSIS, NEREID, VFX, audio, navmesh adapters,
   coordinating ownership with their respective agents. Never duplicate
   their caches or simulation state.
4. Integrate `StaticRef<T>` prefab configuration from the independent
   shared/static branch after its native tests and save parity pass.
5. Add editor-level Scene tree, provenance, streaming status and Play
   preview World support.

Update this handoff with exact commits and tests after every new stage.
