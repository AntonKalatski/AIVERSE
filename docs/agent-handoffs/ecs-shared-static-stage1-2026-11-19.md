# AIVERSE — Shared/Static ECS stage 1 handoff

**Date:** 2026-11-19  
**Repo:** `chromealex/AIVERSE`  
**Branch:** `feature/ecs-shared-static-components` (created directly from `main`)  
**Status:** AppleClang Release compilation and the targeted native CTest passed on Mac Studio, and the single 200k/64-unique read microbenchmark was run (user-provided log, 2026-11-19). Full ECS/RTS/FPS integration and actual RSS remain unverified.

This branch is independent of `feature/scene-runtime-multiworld`, currently
being developed by a separate scene-world agent. Do not mix their commits
until tests and the shared/static prefab binding API are verified. Do not
rewrite `engine/core/ECS.h` or default `Simulation::step()`: those are
owned by ongoing ECS architecture/FPS migration branches.

## Goal

Deduplicate identical large config data without destroying per-entity query
performance. Add a different static (immutable lifetime) component type to
reuse one prefab/unit config across any number of entities and across
independent ECS Worlds. Both ECS ref payloads are 4 bytes, indexed read O(1).
Use a sparse set of reference components so existing ECS queries and COW
snapshots remain intact. No genre-specific implementation.

## Source/API implemented

1. `engine/sim/SharedStaticComponents.h`
   - `SharedRef<T>{uint32_t slot}` and `StaticRef<T>{uint32_t slot}`,
     slot 0 invalid. Compile-time sizeof checks.
   - Deterministic `SharedValueCodec<T>` uses Archive IO for hashing and
     equality verification (hash collisions fully compared).
   - `SharedStore<T>::intern(value)` deduplicates, stores immutable
     `shared_ptr<const T>` values and maps hashes to ordered bucket IDs.
     `SharedStore` is copyable/COW-friendly and an ECS World resource.
   - `setShared(world,e,T)`, `updateShared(world,e,fn)`,
     `readShared<T>(world,e)`, `validateShared<T>(world)`.
     Mutations are by copying the value and reassigning one Entity ref.
   - `compactShared<T>(world)`: at a structural barrier remaps live
     references by ascending old slot, rebuilds the store without unused
     values; works with COW snapshots that keep old store and refs.
     Invalidates cached refs/views. No automatic per-Entity refcount/GC.
   - `StaticTableBuilder<T>::define(T)` deduplicates authoring definitions;
     `seal()` produces `shared_ptr<const StaticTable<T>>` with a
     contiguous immutable config array and deterministic fingerprint.
   - `StaticTableBinding<T>{fingerprint}` as an authoritative World
     resource; `bindStaticTable(world,table)` and `setStatic` only
     allow one initial binding per Entity and exact table identity.
   - `readStatic`, `validateStatic`, `SharedReadView<T>`,
     `StaticReadView<T>`: cached registry references for hot loops,
     indexed read only. No per-read hashing or per-entity allocations.
   - `io(Archive&,SharedStore<T>&)`, `io` for refs/binding:
     native persistent ECS v10 save/load/checksum uses one unique entry
     per shared value and per-Entity refs. Static table data is external
     immutable content and not duplicated per save/snapshot.

2. `engine/sim/SharedStaticPersistence.h`:
   - `registerSharedPersistence<T>(Simulation&,stableTypeId,version)`
     registers ref Component + store Resource as two versioned schemas.
   - `registerStaticPersistence<T>(Simulation&,stableTypeId,version)`
     registers ref Component + binding Resource. Call in
     `GameModule::setup` before registration seals.

3. Tests implemented; **targeted native CTest passed** on the verified commit:
   - `tests/ecs_shared_static_components_test.cpp`: stable intern IDs,
     per-Entity replacement without modifying other shared values,
     static bind-once const access, COW snapshot, deterministic compaction,
     rollback, native v10 serialize/deserialize checksum and wrong-table
     rejection. **Native result: 1/1 Passed, 0.39 sec test time.**
   - `tests/ecs_shared_static_bench.cpp`: 200k Entities, 64 unique
     128-byte configs, normal vs SharedRef/StaticRef median O(1) read
     loops with equivalent checksums; estimates payload bytes. **Native
     Release result:** ordinary 0.309 ms, shared 0.089 ms, static 0.096 ms.
     These are read microbenchmarks, not full-frame timings.
   - `CMakeLists.txt`: registers `ecs_shared_static_components_test`
     in the core CTest loop; optional `aiverse_ecs_shared_static_bench`
     executable in the same test-enabled native configuration.

4. Documentation:
   - `docs/ecs-shared-static-components.md`: architecture, examples,
     memory model, boundaries, save/rollback/benchmark commands.
   - `skills/gameplay/aiverse-ecs-shared-static/SKILL.md`:
     reusable guidance for agents generating games and ECS mechanics.
   - This handoff.

## Commits (in order)

- `fadd16698d64c28d271d29627338749a74f59529` — interned shared and
  sealed static table core.
- `9f027523ff62f85c8f417b7baaaeb24eff44045d` — native versioned
  persistence helpers.
- `4967be0a2b9fe191c3c9439ab7caff90e7522328` — deterministic
  shared store compaction, sealed table builder memory trim.
- `b86af2d84347c7db015ad5964a28ce27f544f294` — regression test.
- `da1a1396b0c4d2cb322c8a7bfda7aa668b169f70` — microbenchmark.
- `5c3b61817fee667558624e3d50a6762628265fb1` — CMake registration.
- `29c296a7f1fbb7466f7fd9d526b102c73c026c37` — architecture/API doc.
- `2ebb71e8fc21a6db8d3cfc3eed30577a44a64926` — agent skill.
- `77f79689d6b8cd89533d787cfc1e689e3e27719f` — initial stage-1 handoff.
- `08d0227c540fed21a9a9b875f343db3951a0c746` — benchmark accepts entity count, uses atomic checksum sink.
- `11568cd16d7ad410421bfc018f630a426edf2f03` — finalize initial handoff, **exact commit tested on Mac Studio**.
- `e3d2002e236e9f7e2df3d986e19d36e0b2b9b67b` — document verified Mac Studio native CTest/Release microbenchmark.
- `8701e40ceec45c7dbd9104bb788d7b56a05023a2` — record measured numbers and limitations in architecture doc.

The latest handoff update comes after these commits; use `git rev-parse HEAD`
for the exact branch head. Documentation-only commits after `11568cd16`
do not alter the tested C++ sources.

## Native validation evidence — 2026-11-19

The user ran the following on **Mac Studio / macOS**, on the exact source
commit `11568cd16d7ad410421bfc018f630a426edf2f03`:

- Worktree `../AIVERSE-shared-static` tracking `origin/feature/ecs-shared-static-components`.
- CMake configuration: `-DAIVERSE_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release`.
- Compiler: **AppleClang 17.0.0.17000604** (C and C++).
- Both targets built and linked: `aiverse_ecs_shared_static_components_test` and `aiverse_ecs_shared_static_bench`.
- `ctest -R '^ecs_shared_static_components_test

Keep the user's active AIVERSE checkout untouched: separate worktree.

```sh
cd ~/Projects/AIVERSE
git fetch origin main feature/ecs-shared-static-components

git worktree add -b verify-ecs-shared-static \
  ../AIVERSE-shared-static origin/feature/ecs-shared-static-components

cd ../AIVERSE-shared-static
git pull --ff-only origin feature/ecs-shared-static-components

cmake -S . -B build/shared-static \
  -DAIVERSE_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release

cmake --build build/shared-static --target \
  aiverse_ecs_shared_static_components_test \
  aiverse_ecs_shared_static_bench --parallel 8

ctest --test-dir build/shared-static --output-on-failure \
  -R '^ecs_shared_static_components_test$'

./build/shared-static/aiverse_ecs_shared_static_bench
```

Compiler, selected CTest and one Release read benchmark have now been
recorded above; RSS and broader validation remain outstanding. Run existing COW
`ecs_cow_snapshot_test`, `ecs_cow_parallel_test`, native
`ecs_scene_hierarchy_test`, `ecs_scene_hierarchy_simulation_test`
and broader RTS/FPS parity before any production migration.

## Known limits / careful integration

- `SharedRef<T>` / `StaticRef<T>` have 4-byte payloads **plus**
  existing Sparse Set sparse/dense Entity index costs. SharedStore hash
  maps, allocations, `shared_ptr` control blocks and dynamic heap fields
  in T also consume memory. Real RSS must be measured.
- Indexed read is O(1) but may be slower or faster than an inlined
  ordinary component. `SharedReadView` and `StaticReadView` avoid a
  repeated World resource/binding lookup. Never claim identical latency
  before Release target benchmarks.
- SharedStore does not automatically delete individual unused slots.
  `compactShared<T>` is a deterministic operation changing ref IDs;
  perform only at a canonical barrier after World.flush, no jobs or stale
  external SharedRef cache. A snapshot before compaction stays valid.
- Compaction can change checksums/save bytes even if logical live values
  remain identical, because store slot topology changes. It must execute
  identically on every peer and be represented by a deterministic command
  or explicit save-format migration if used in networked lockstep.
- Default hash codec encodes T by field-wise Archive IO. T must be
  default-constructible/copyable for v10 persistence. Configs with
  pointers or transient OS state must not be registered. Any schema change
  requires an explicit versioned migration; changing a field order
  silently is forbidden.
- The static table is content-defined outside ECS World. Runtime API
  cannot mutate table values: no non-const getter. The public ECS
  `StaticRef<T>` component remains structurally replaceable by
  external raw World APIs, so engine policy has to avoid bypassing
  `setStatic` for ordinary gameplay (or use a guarded mutation interface).
- `StaticTableBinding` 64-bit fingerprint is a deterministic checksum,
  not a cryptographic trust root. Validate table identity on save load
  and bind the exact versioned asset from the content registry.
- Native `PersistentEcsRegistry` imposes max 32 MiB per schema payload.
  For extremely large unique SharedStore tables, split by content domain
  or compact at deterministic barriers.
- No production RTT/fps modules were automatically migrated; no
  fabricated native build or benchmark measurements.

## Proposed next steps

1. **Priority:** extend native validation beyond the passing targeted test:
   COW, replay and multi-module parity; collect process RSS and more Release
   read benchmarks across low/high unique counts on target platforms.
2. Integrate static prefab archetype config binding with `SceneAsset` and
   `SceneLoadCoordinator` from the separate scene branch after it has
   passed its own tests, so additive scenes share one immutable config.
3. Add an opt-in query facade for `World::each<T,SharedRef<Config>>`
   with typed automatic dereference and pre-resolved registries.
4. Consider a compact 16-bit ref for tightly bounded game-specific tables,
   and validate vs 32-bit default, with schema versioning.
5. Investigate GC thresholds and fingerprinting/content-index serialization
   for large mutable shared tables; preserve deterministic intern order.
6. Migrate known repeated config structures from RTS/FPS incrementally
   with real native host parity / save/checksum replay.

**Do not merge to main until compilation, CTest, and relevant replay
checksums are verified and documented.**
 --output-on-failure`:
  **1/1 Passed**, test 0.39 sec, total 0.40 sec, **0 failures**.
- Benchmark: `entities=200000`, `uniques=64`, `config_bytes=128`.
  Payload-only estimates: ordinary **25,600,000 bytes**, shared/static
  **808,192 bytes** (about **96.84% less**).
  Read-only median CPU timings: ordinary **0.309 ms**,
  Shared **0.089 ms** (about **3.47x faster**),
  Static **0.096 ms** (about **3.22x faster**).
  Ratios reported directly by executable: Shared/ordinary **0.289**,
  Static/ordinary **0.310**.

**Interpretation / limitations:** The code was genuinely compiled and the
one targeted test genuinely passed, so Stage 1 now has native evidence.
Only this selected CTest was run, not the full CTest suite; no RTS/FPS
integration or cross-platform parity has been demonstrated. The memory
figures exclude Sparse Set, hash metadata, allocations and OS overhead;
no process RSS was measured. The benchmark uses one fixed high-reuse
workload (64 unique configurations). Repeated runs, larger unique
counts, random access, writes/compaction and cold-cache cases are still
needed. Do not generalize the read speedup to all real gameplay systems.

## Next validation actions (existing isolated worktree)

Keep the user's active AIVERSE checkout untouched: separate worktree.

```sh
cd ~/Projects/AIVERSE
git fetch origin main feature/ecs-shared-static-components

git worktree add -b verify-ecs-shared-static \
  ../AIVERSE-shared-static origin/feature/ecs-shared-static-components

cd ../AIVERSE-shared-static
git pull --ff-only origin feature/ecs-shared-static-components

cmake -S . -B build/shared-static \
  -DAIVERSE_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release

cmake --build build/shared-static --target \
  aiverse_ecs_shared_static_components_test \
  aiverse_ecs_shared_static_bench --parallel 8

ctest --test-dir build/shared-static --output-on-failure \
  -R '^ecs_shared_static_components_test$'

./build/shared-static/aiverse_ecs_shared_static_bench
```

Record compiler version, platform, actual CTest pass/fail count,
Release benchmark medians, Resident Set Size measurements, current
branch SHA, warnings and any fixes here. Then run existing COW
`ecs_cow_snapshot_test`, `ecs_cow_parallel_test`, native
`ecs_scene_hierarchy_test`, `ecs_scene_hierarchy_simulation_test`
and broader RTS/FPS parity before any production migration.

## Known limits / careful integration

- `SharedRef<T>` / `StaticRef<T>` have 4-byte payloads **plus**
  existing Sparse Set sparse/dense Entity index costs. SharedStore hash
  maps, allocations, `shared_ptr` control blocks and dynamic heap fields
  in T also consume memory. Real RSS must be measured.
- Indexed read is O(1) but may be slower or faster than an inlined
  ordinary component. `SharedReadView` and `StaticReadView` avoid a
  repeated World resource/binding lookup. Never claim identical latency
  before Release target benchmarks.
- SharedStore does not automatically delete individual unused slots.
  `compactShared<T>` is a deterministic operation changing ref IDs;
  perform only at a canonical barrier after World.flush, no jobs or stale
  external SharedRef cache. A snapshot before compaction stays valid.
- Compaction can change checksums/save bytes even if logical live values
  remain identical, because store slot topology changes. It must execute
  identically on every peer and be represented by a deterministic command
  or explicit save-format migration if used in networked lockstep.
- Default hash codec encodes T by field-wise Archive IO. T must be
  default-constructible/copyable for v10 persistence. Configs with
  pointers or transient OS state must not be registered. Any schema change
  requires an explicit versioned migration; changing a field order
  silently is forbidden.
- The static table is content-defined outside ECS World. Runtime API
  cannot mutate table values: no non-const getter. The public ECS
  `StaticRef<T>` component remains structurally replaceable by
  external raw World APIs, so engine policy has to avoid bypassing
  `setStatic` for ordinary gameplay (or use a guarded mutation interface).
- `StaticTableBinding` 64-bit fingerprint is a deterministic checksum,
  not a cryptographic trust root. Validate table identity on save load
  and bind the exact versioned asset from the content registry.
- Native `PersistentEcsRegistry` imposes max 32 MiB per schema payload.
  For extremely large unique SharedStore tables, split by content domain
  or compact at deterministic barriers.
- No production RTT/fps modules were automatically migrated; no
  fabricated native build or benchmark measurements.

## Proposed next steps

1. **Priority:** run native tests, fix compile diagnostics/behaviour, repeat
   on multiple compilers and collect Release read/performance baselines.
2. Integrate static prefab archetype config binding with `SceneAsset` and
   `SceneLoadCoordinator` from the separate scene branch after it has
   passed its own tests, so additive scenes share one immutable config.
3. Add an opt-in query facade for `World::each<T,SharedRef<Config>>`
   with typed automatic dereference and pre-resolved registries.
4. Consider a compact 16-bit ref for tightly bounded game-specific tables,
   and validate vs 32-bit default, with schema versioning.
5. Investigate GC thresholds and fingerprinting/content-index serialization
   for large mutable shared tables; preserve deterministic intern order.
6. Migrate known repeated config structures from RTS/FPS incrementally
   with real native host parity / save/checksum replay.

**Do not merge to main until compilation, CTest, and relevant replay
checksums are verified and documented.**
