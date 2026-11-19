# AIVERSE scene runtime — local agent instructions

Scope: `engine/scene/`. Read `docs/scene-worlds.md`,
`docs/agent-handoffs/scene-worlds-stage1-2026-11-19.md`,
`docs/agent-handoffs/scene-worlds-stage2-2026-11-19.md`,
`docs/ecs-agent-handoff.md` and `docs/ecs-scene-hierarchy.md`
before modifying scene/world lifecycle code.

- One Scene = independently unloadable set of entities in ONE
  `aiverse::World`. Do not implement a new ECS allocator per level or
  reinterpret `world::WorldScene` (it streams visual/spatial chunks).
- One `Simulation` owns one `World` and one deterministic tick/RNG/command
  stream. `SimulationHost` holds multiple independent Simulations.
- Authoritative scene membership is the `SceneOwner` component. Registry,
  pending replacement, ID generator and states are the `SceneState` World
  resource. Register both through `registerScenePersistence` in native
  modules opting in to serialized state. Never store source-of-truth scene
  membership only in a renderer, IO manager or global map.
- Scene IDs are World-scoped and monotonic; cross-world Entity refs MUST use
  `WorldEntityRef` with generation-safe `WorldHandle`.
- `beginLoad` is only lifecycle registration. CPU asset preparation happens
  off-thread; ECS structural changes and `activate/unload` only at an owned
  deterministic structural/tick barrier, not directly in job callbacks.
- When unloading SceneHierarchy-tracked entities pass the hierarchy. Its
  `detach` implementation repairs foreign child links before destruction.
- Immutable render assets may be shared; no mutable gameplay World resources,
  physics bodies, hierarchy caches or RNG shared accidentally across Worlds.
- Keep migration opt-in; preserve existing RTS/FPS map/save/network semantics
  until native parity tests pass. Do not edit `engine/core/ECS.h`,
  `engine/sim/Simulation.cpp` or RTS/FPS gameplay without explicit
  coordination with the ongoing ECS agent.
- Maintain COW/rollback and native binary schema versioning. Update
  `docs/scene-worlds.md` and the stage handoff after EVERY implementation
  stage, recording branch/commit, changed files, tests actually run (or
  explicitly unverified), limitations and concrete next steps.
- Required regression checks: `ecs_scene_runtime_test`,
  `ecs_multiworld_test`, existing `ecs_scene_hierarchy_test`,
  `ecs_scene_hierarchy_simulation_test`, plus FPS/RTS save/checksum tests
  before production integration.

Do not claim asset/physics/navmesh/render scene streaming is implemented in
Stage 1; this stage supplies deterministic ECS ownership and lifecycle APIs.

## Stage 2 coordinator ownership (2026-11-19)

- `SceneAsset.h` is the strict canonical v1 scene manifest. Do not accept
  `Json::integer()` for IDs: it converts via Fixed and can truncate 1.5.
  `decodeId` validates the exact integer literal. Future schema changes
  need explicit version migrations and malformed-input tests.
- `SceneLoadCoordinator.h` is a **host-only bridge**, not an ECS resource.
  `ReadAsset` may execute on a worker; callbacks for resource acquire,
  release, prefab spawn, scene activation and hierarchy changes run only on
  the host simulation owner thread. `poll` never changes authoritative ECS.
- Load at request time, but instantiate/publish only from an explicit
  deterministic tick-barrier commit. A future finishing does NOT decide the
  gameplay tick. Use `GameModule::readyToStep` for required-content gating.
- Use `SceneLoadCoordinator::unload` to coordinate resource lease release
  with ECS scene ownership. Do not bypass it with direct `SceneRuntime`
  calls for managed scenes.
- On rollback/cold restore, discard all old async tickets via
  `invalidateAfterRestore`, rebuild SceneHierarchy, and use
  `queueRestoredResidency` to reacquire non-authoritative dependencies of
  Active scenes. Never respawn the restored gameplay entities.
- SceneResourceAdapter keys are immutable dependency identities. Refcounting
  is per coordinator; cross-world shared GPU assets require an external
  cache. Resource `release` must not throw. Bound the incoming byte stream
  in the Reader before accepting untrusted files.
- Native validation target: `aiverse_ecs_scene_load_coordinator_test`.
  Keep it green together with Stage 1 and hierarchy tests; actual native
  results are still outstanding. Do not describe Stage 2 as tested.
- Next stage should integrate existing world streaming and presentation
  facilities via adapters, *not* fork a second renderer/physics/terrain cache.


## Integrated Static prefab catalog (Stage 4, 2026-11-19)

- `engine/scene/PrefabStaticSceneBinding.h` is the actual native bridge
  between `SceneLoadCoordinator::SpawnPrefab` and
  `storage::PrefabStaticCatalog<T>` from `engine/sim/PrefabStaticCatalog.h`.
  A new SceneAsset instance stores only a 4-byte `StaticRef<T>`; the
  sealed config table is shared through `shared_ptr<const Catalog>` across
  additive scenes and independent ECS Worlds.
- Call `registerScenePersistence` and
  `registerPrefabStaticPersistence<T>` inside `GameModule::setup`, then
  `catalog->bindWorld(sim.world())`. Do not double-register native
  `StaticRef<T>` schemas.
- Supply `StaticPrefabSceneBinding<T>(catalog, optionalInitializer).callback()`
  to the actual `SceneLoadCoordinator`; `MissingPrefabConfig::Reject`
  rejects unrecognized prefabs and rolls back the *new* scene while old
  scenes survive. Optional domains can use `Skip` and
  `PrefabSceneSpawnChain` in a deterministic registration order.
- A scene unload destroys only scene-owned Entity refs; the immutable
  prefab catalog stays alive while owned by the game/preview/module
  or any other legitimate `shared_ptr` holder. Never mutate static
  config values or register a second authoritatively writable copy.
- After cold native state restore, validate
  `catalog->validateWorld(sim.world())`, rebuild `SceneHierarchy` and
  restore scene resource residency via `invalidateAfterRestore` /
  `queueRestoredResidency`. Instance Parent/LocalTransform persistence
  is still host-owned and must be explicitly registered; this adapter
  only installs the static prefab/scene schemas.
- Native opt-in CTest: `ecs_static_scene_integration_test`; run this
  plus all relevant source-branch ECS, scene, COW, save and lockstep tests
  before proposing a main merge. **Combined native targeted integration CTest passed 7/7 on Mac Studio
  (2026-11-19). Full ECS/COW/FPS/RTS and GPU checks remain outstanding.**
- See `docs/ecs-static-prefab-scene-integration.md` and
  `docs/agent-handoffs/ecs-static-scene-stage4-2026-11-19.md` for the
  exact integration contract/commit history, verification and next step.
- The additive `CollisionStream` static collider ID namespace is still
  unresolved; do NOT attach two independent collision streams to one
  `SpatialWorld` or disable its existing guard to make tests green.


## Stage 5 — multi-domain immutable prefab configs (2026-11-19)

- `PrefabStaticDomainRegistry` in
  `engine/scene/PrefabStaticDomainRegistry.h` composes one sealed catalog
  per distinct `StaticRef<T>` type. Domain/persistence IDs are explicitly
  stable and unique. Domain order is `std::map` sorted key order, never
  import order or unordered hash-map iteration.
- `addDomain<T>(domainKey,persistentSchemaId,catalog,Reject|Skip,version)`
  runs during configuration. `seal()` freezes the domain registry.
  `registerPersistence(sim)` and `bindWorld(sim.world())` run during
  GameModule::setup; do not register the same StaticRef<T> separately.
- Before creating per-entity StaticRefs, the callback preflights ALL domain
  bindings and required prefab keys. A missing required domain causes
  rejection, leaving the just-created Entity without partial static refs.
  After successful preflight, attach 4-byte refs, then create ordinary
  mutable instance components in an optional initializer.
- `PrefabStaticCatalog<T>::canBindWorld` validates existing content
  fingerprints without mutating ECS; registry bind preflights all domains
  before inserting any. Content values remain immutable across Worlds.
- New CTest: `ecs_static_scene_multidomain_test` tests 3 typed domains,
  optional inventory, deterministic registration-order persistence,
  rollback/scene unload and changed-alias rejection. Its native result is
  **pending**. Previous Stage 4 seven-test suite passed but is not a
  substitute for this new test.
- Use `bash tools/ecs-static-scene-validate.sh build/static-scene extended 8`
  after pulling branch in an isolated worktree. It logs every CMake
  build and selected ECS/COW/FPS/RTS CTest result. See Stage 5 handoff.
