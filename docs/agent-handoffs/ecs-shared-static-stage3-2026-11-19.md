# AIVERSE — Shared/Static ECS Stage 3: prefab config catalog

Date: 2026-11-19  
Repo: `chromealex/AIVERSE`  
Branch: `feature/ecs-shared-static-components`  
Base Stage 2 handoff: `docs/agent-handoffs/ecs-shared-static-stage2-2026-11-19.md`  
Status: **catalog source, CMake test and docs committed; new native test NOT run**.

## Proven baseline (user-provided Mac Studio logs)

1. New `ecs_shared_static_components_test` compiled and passed **1/1** on
   AppleClang 17 / Release; 200k/64-unique read microbenchmark ordinary
   0.309 ms, shared 0.089 ms, static 0.096 ms. Payload comparison
   25,600,000 bytes vs 808,192 bytes. Not real process RSS.
2. After the previous agent changed legacy COW tests to
   `AIVERSE_TEST_CHECK` to keep assertions active under `NDEBUG`,
   user posted a Mac Studio CTest run showing
   `ecs_scene_hierarchy_test` Passed 0.51 sec,
   `ecs_scene_hierarchy_simulation_test` Passed 0.16 sec,
   `ecs_cow_snapshot_test` Passed 0.42 sec and
   `ecs_cow_parallel_test` Passed 0.40 sec;
   **4/4 passed, 0 failed, real total 1.50 sec**.
   These results are from the prior source revision, not from the
   newly committed prefab catalog. The exact HEAD of this CTest
   invocation was not printed, so do not invent it.

## Stage 3 implemented code

### New: `engine/sim/PrefabStaticCatalog.h`

- `PrefabStaticCatalogBuilder<T>::add(prefabKey, config)`: validates
  normalized project-relative stable prefab key; refuses duplicates
  and invalid `../`, absolute, empty and malformed paths.
- `seal()`: deterministic canonical ordering via `std::map`;
  identical configs are deduplicated using the existing
  `StaticTableBuilder<T>::define`, then sealed into one immutable,
  contiguous `StaticTable<T>`. Import registration order is irrelevant.
  The catalog is returned via `std::shared_ptr<const ...>`.
- `PrefabStaticCatalog<T>::find(prefabKey)`: spawn-time key lookup,
  returns World-independent 32-bit `StaticRef<T>`; do not invoke per
  entity in hot-tick queries.
- `bindWorld(world)`: binds the existing `StaticTableBinding<T>`
  plus an extra `PrefabCatalogBinding<T>` World resource.
  The catalog fingerprint hashes the immutable table fingerprint
  AND canonical prefab-key-to-slot pairs. Without the second stamp,
  changing an alias from config A to B while leaving the unique
  table unchanged would be undetected after save/load.
- `attach(world,entity,prefabKey)`: one-time per-entity StaticRef
  assignment, rejecting stale entity, unknown prefab, wrong
  catalog revision or already-bound StaticRef. No per-entity config
  data copies.
- `read(world,entity)`, `validateWorld(world)`: const data access,
  table + mapping fingerprint and stale ref validation.
- `registerPrefabStaticPersistence<T>(simulation,stableTypeId,version)`:
  registers three versioned native v10 schemas inside GameModule::setup:
  `StaticRef<T>`, `StaticTableBinding<T>` and
  `PrefabCatalogBinding<T>`. Do not also register the same
  `StaticRef<T>` independently under a duplicate schema ID.
- The catalog and its `StaticTable<T>` live outside World and are
  reusable by multiple Simulations; only the refs/bindings are
  authoritative ECS state. No heap copy of T per entity.

### Test source and wiring (not yet natively executed)

- `tests/ecs_prefab_static_catalog_test.cpp`:
  sorted mapping independent of importer input order; deduplication
  of four prefab keys into two unique 128-byte configs; 10,000
  instances in two simulated additive scenes in one ECS World;
  separate preview World reading the same immutable table pointer;
  frozen const reads; one-time binding; unknown prefab rejection;
  World snapshot/rollback; two independent native GameModule hosts;
  v10 persistence and checksum replay; swapped alias rejection
  with an unchanged underlying StaticTable fingerprint.
- `CMakeLists.txt`: `aiverse_ecs_prefab_static_catalog_test`
  registered in the native `aiverse_engine` CTest group.
- `docs/ecs-shared-static-components.md`: new Stage 3 architecture
  and sample SceneLoadCoordinator::SpawnPrefab adapter.
- `skills/gameplay/aiverse-ecs-shared-static/SKILL.md`: Stage 3
  instructions for future AI agents.

## Stage 3 commits (source + docs)

- `cf3b255194b9ec61cdaeb9f562dee88b7a3e879c` — catalog with
  immutable prefab refs and save mapping fingerprint.
- `915a740c49238dd54e75a124f36cc75915eb81ca` — native fixture
  across worlds, scene-like hosts, snapshots and save/load.
- `b88b1187e9a7d4ea28a05a8590ce3214e75ebffd` — CMake registration.
- `c40bf8fa57a75ad7f5dd94f395da89132324bc00` — architecture doc.
- `4a92c1c385159e74f908f7fb9d42495dee5c03a1` — agent skill update.

Previous stage result documentation:
`41d4b46f9dd3e8d3e3f5035bcf4b558e5f986323`
recorded the user-provided 4/4 native results.

## Immediate validation task — existing worktree

Do not create a new worktree or mutate the main checkout:

```sh
cd ~/Projects/AIVERSE-shared-static
git fetch origin feature/ecs-shared-static-components
git pull --ff-only origin feature/ecs-shared-static-components

cmake --build build/shared-static --target \
  aiverse_ecs_prefab_static_catalog_test \
  aiverse_ecs_shared_static_components_test \
  aiverse_ecs_cow_snapshot_test \
  aiverse_ecs_cow_parallel_test \
  --parallel 8

ctest --test-dir build/shared-static --output-on-failure \
  -R '^(ecs_prefab_static_catalog_test|ecs_shared_static_components_test|ecs_cow_snapshot_test|ecs_cow_parallel_test)$'
```

**No native compile/CTest result exists yet for the new prefab catalog.**
If the compiler or CTest detects an error, fix source on this branch and
record the command/output, compiler, exact SHA, changed files, limitations
and next actions in this handoff. Later run whole ECS suite and genre
FPS/RTS save/rollback parity before production merge.

## Architectural integration boundary and limitations

- The `feature/scene-runtime-multiworld` branch is **not merged** here.
  It contains `SceneAsset::SceneObjectAsset{prefab}` and
  `SceneLoadCoordinator::SpawnPrefab(World&,Entity,...)`; a future
  verified integration should forward `object.prefab` to
  `catalog->attach`. No production scene loader change was made.
- These tests emulate two additive scene instance groups but do not
  prove actual SceneLoadCoordinator lifecycle or GPU residency.
- `StaticTable<T>` and catalog expose no mutable T accessor. Raw
  `World::add<StaticRef<T>>` remains a low-level escape hatch and
  can bypass the once-only wrapper; do not claim the ECS core prevents
  all raw mutations without a guarded World API migration.
- The prefab key map lookup allocates a temporary std::string for a
  string_view and costs O(log number of prefabs), but runs at spawn-time,
  NOT per tick. Per-tick `StaticReadView<T>` is O(1).
- Changing a catalog's table or alias mapping produces a different
  version stamp. The host must rehydrate the exact content revision
  and call `validateWorld` after cold save/rollback before stepping.
  Versioned content migrations are not yet implemented.
- Source code changes after this handoff commit must receive a
  new handoff entry with actual tests and exact SHA. Do not mark this
  stage complete solely because earlier COW tests passed.

## Proposed next development stage

After Stage 3 native checks pass, connect catalog binding to a
verified scene-runtime integration adapter, using one immutable
catalog per config domain and refcounted scene assets. Include test
for unloading scene A while scene B retains entities referencing
the same table, save/load across scene replacement, and FPS/RTS
native host usage. The scene work has an unresolved additive
CollisionStream ID ownership issue; do not pull in or "fix" physics
by bypassing its static collider namespace validation.
