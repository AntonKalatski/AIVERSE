# SceneAsset / SceneLoadCoordinator — Stage 2 handoff (2026-11-19)

Repository: `chromealex/AIVERSE`  
Working branch: `feature/scene-runtime-multiworld`  
Parent: Stage 1 at `155dfcf31a19077a7cb415b9269dae4468355803`  
Status: **Stage 2 source committed; native compile/CTest not executed here**.

## Scope and contract

Read `docs/scene-worlds.md`, `engine/scene/AGENTS.md`, and
`docs/agent-handoffs/scene-worlds-stage1-2026-11-19.md`.
This stage adds a stable scene file format, asynchronous CPU preparation
and deterministic main-thread activation. It does **not** edit core ECS
storage, `Simulation::step()`, RTS/FPS canonical state, or any renderer,
water, terrain, weather or physics implementation. The canonical scene
registry/entity ownership remains `SceneState`/`SceneOwner` in the ECS
World. The coordinator is **host-only** and must not be hashed or saved.

## Changed files

1. **`engine/scene/SceneAsset.h`** — v1 manifest API:
   - `SceneObjectAsset{id,parent,prefab,properties}` / `SceneAsset`.
   - Strict `SceneAsset::decode(Json,...)` for `version:1`, ID,
     dependencies and prefab objects.
   - Validated relative logical paths (no absolute, `..`, `.`, empty
     segments, backslashes, colon).
   - Max 10,000 objects, max 512 dependency keys, max 512-character asset
     keys, and 4 MiB per manifest (enforced after Reader returns).
   - Reject duplicated object IDs/deps, missing parents, self-parenting,
     parent cycles, wrong types. Normalize objects/deps by stable sorted IDs.

2. **`engine/scene/SceneLoadCoordinator.h`** — host-side loader:
   - `SceneTicket{SceneId,epoch,serial}` avoids accepting a stale worker
     result after an ECS rollback restores `SceneId` allocation.
   - `SceneResourceAdapter{acquire,release}`: dedupes resource residency
     and releases shared assets only at last user. Both callbacks execute
     only on the owner thread; release must not throw.
   - `ReadAsset` callback is copied into a `std::async` worker; this
     worker only reads/parses JSON and validates manifest. It never touches
     ECS, SceneHierarchy or GPU.
   - `SpawnPrefab` callback instantiates each prefab on a new scene-owned
     Entity on the owner thread. LocalTransform produced by the callback
     is preserved; Parent linking occurs afterward via `SceneHierarchy`.
   - `request` / `poll` / `status` / `ready` / `error` /
     `commit` / `cancel` / `unload` are the main lifecycle API.
     A load is **not** activated on future completion: only an explicit
     caller-owned deterministic tick/structural barrier invokes commit.
   - Replace acquires assets and instantiates new scene before deleting
     nonpersistent old scenes. Failures unload partial new entities and
     release staged leases; old scene is unaffected.
   - Cancel of a pending worker returns without waiting; its future is
     tombstoned and drained in `poll`. Coordinator destruction can still
     block waiting for pending `std::async` jobs.
   - After rollback/cold load: call `invalidateAfterRestore()` then
     `queueRestoredResidency()`, wait and commit residency-only tickets
     for Active scenes. No ECS entities are spawned by residency rehydrate.
   - `maxPreparing` defaults to 4. The coordinator does not perform
     actual GPU upload budgeting or dependency graph traversal.
   - `poll()` **must not mutate ECS**, including on an asynchronous read
     failure; the host cancels/commits at its own structural barrier.

3. **`tests/ecs_scene_load_coordinator_test.cpp`** — new native fixture:
   - Strict manifest validation, parent cycle and ID checks.
   - Worker thread != owner thread, resource acquisition on owner.
   - Additive persistent global+city loading; stable parent transforms.
   - Deduplicated resource acquisitions and release on last reference.
   - Missing dependency/prefab failures keep previous active scenes.
   - Replace old scenes and retain persistent scenes.
   - World COW rollback + invalidate tickets + rehydrate resources without
     respawning ECS objects.
   - Cancel a worker blocked on a promise, ignore its late completion.
   - Note: test source **exists but has not been compiled/run here**.

4. **`CMakeLists.txt`** — registers CTest
   `aiverse_ecs_scene_load_coordinator_test` under the existing native
   `aiverse_engine` + Threads test group.

5. **`docs/scene-worlds.md`** — Stage 2 C++ usage, manifest JSON,
   deterministic activation, resource and rollback contracts, limitations.

## Stage 2 Git commits

- `e19faf8de5dab0c70663d9cd9d8d49fded9763b6`: scene manifest.
- `bc8ea4108ba3cce7e7681f9f6b90588a727022b5`: async coordinator.
- `166b1efc494a87363b530d29ca6e4a6bad1df1fb`: coordinator fixes:
  poll is non-authoritative, preserve prefab LocalTransform, release leases.
- `d82f1016073b80ce80c67aa7e3a07f1b0d8e7108`: native loader tests.
- `5a7379ff3cde498e7eb5b105582c40522af959a9`: CMake target.
- `4d5024e492ee7d1dbeaf431336e0c2145918ab6d`: living architecture doc.

The working head may be newer than these commit IDs due to this handoff
and subsequent fixes. Use `git rev-parse HEAD` after pulling; never infer
a passing test from a committed source file.

## Verification: native execution remains pending

No remote GitHub Actions run was observed when Stage 1 was reviewed, and
the current assistant environment cannot clone GitHub with git or compile
against the full repository sources. Thus these are source changes, not
certified production code. **First task for next agent is to compile and
fix any compiler errors or actual test failures, then record command,
hardware/compiler, outcome and exact SHA here.**

Always fetch the branch before build, and use a separate worktree:

```sh
cd ~/Projects/AIVERSE
git fetch origin main feature/scene-runtime-multiworld
git worktree add -b verify-scene-stage2 ../AIVERSE-scene-stage2 origin/feature/scene-runtime-multiworld
cd ../AIVERSE-scene-stage2
git pull --ff-only origin feature/scene-runtime-multiworld
cmake -S . -B build/scene-worlds -DAIVERSE_BUILD_TESTS=ON
cmake --build build/scene-worlds --target \
  aiverse_ecs_scene_runtime_test \
  aiverse_ecs_multiworld_test \
  aiverse_ecs_scene_load_coordinator_test \
  aiverse_ecs_scene_hierarchy_test \
  aiverse_ecs_scene_hierarchy_simulation_test \
  --parallel 8
ctest --test-dir build/scene-worlds --output-on-failure \
  -R '^(ecs_scene_runtime_test|ecs_multiworld_test|ecs_scene_load_coordinator_test|ecs_scene_hierarchy_test|ecs_scene_hierarchy_simulation_test)$'
```

Once targeted tests compile/pass, run wider ECS/RTS/FPS save/rollback,
`aiverse_tests`, and relevant world streaming tests before requesting
a merge to main. Update this file **after each code stage and test run**.

## Known gaps and safety conditions

- Manifest v1 does not yet bundle prefab definitions or serialize arbitrary
  `properties` to native typed components; the host's `SpawnPrefab` callback
  is intentionally explicit and must be supplied by the native project.
- The callback must affect only the newly-created scene Entity and avoid
  non-reversible global side effects; otherwise a failure cannot provide
  strong transaction rollback for all external systems.
- Only the authoritative `SceneRuntime` State is saved/snapshotted.
  Do not add `std::future`, GPU handles, caches or asset leases to ECS
  authoritative resources.
- Host must coordinate activation at a deterministic multiplayer tick.
  Disk readiness may stall wall time, never influence gameplay tick order.
  `poll()` may expose Failed but does not delete Loading scene metadata;
  call `cancel(ticket)` at a barrier to release it.
- `queueRestoredResidency()` only prepares Active scene dependencies.
  Restored Loading scenes require an explicit host cancellation/re-request
  policy (or a later dedicated resume API); do not replay stale worker jobs.
- No implicit tracking of `SceneRuntime::unload` outside coordinator.
  Such an unload can leave coordinator presentation resource leases until
  `SceneLoadCoordinator::unload` or reset; migrate all scene lifecycle
  operations to one owner.
- `Reader` may run on worker threads; its captured references must remain
  alive until in-flight futures complete. It must impose a bounded read size
  if reading untrusted streams; only post-read validation is implemented.
- Coordinator is not wired into platform IO, RHI/GPU, NEREID, TerraWeave,
  PHYSIS, Audio, Prisma or SpectraFX yet. Resource adapter is a host seam;
  do not label these integrations complete.
- No editor scene tree / scene serializer / prefab overrides / UI.
- Reentrant scene unload during ECS each() / parallel job is forbidden.
  Place commit/cancel/unload at a game-owned structural barrier.
- ResourceAdapter acquire/release runs on the owner thread, so a slow GPU
  upload can still hitch; Stage 3 should support incremental publication.

## Next implementation step (Stage 3)

1. Add a concrete cross-system `SceneResourceBackend` supporting bounded
   async decode + render-thread publication budgets, with lease identity
   stable across scene instances and worlds, and cancellation/rollback.
   Reuse `ModelTextureCache`, `world::ChunkStreamer` and existing
   `WorldScene` rather than creating duplicate caches.
2. Add per-scene physics/collision/navmesh resources and explicit prepare,
   publish, retire barriers, including TerraWeave and NEREID ownership.
3. Add typed deterministic `SceneTransitionCommand` policy / integration
   with `GameModule::readyToStep`, authoritative load barrier, network
   parity and replay/rollback test.
4. Add editor scene tree (persistent/additive/streaming), scene asset
   authoring and in-editor Play clone as an independent ECS World.
5. Prove the entire lifecycle in two independent native `GameModule`
   consumers and a headless FPS/RTS compatibility fixture, then update
   `docs/scene-worlds.md`, `engine/scene/AGENTS.md` and this handoff.

Ownership: `engine/scene/` is the scene-runtime area. Avoid interfering
with parallel agents changing `engine/core/ECS.h`, `Simulation.cpp`,
`modules/fps`, `modules/rts`, `engine/world` or RHI without explicit
coordination and test parity.
