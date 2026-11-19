---
name: aiverse-ecs-shared-static
description: Use AIVERSE SharedRef and StaticRef immutable component storage to deduplicate gameplay config, preserve native save/rollback, and benchmark memory/read cost without rewriting ECS core.
---

# AIVERSE shared/static immutable component storage

**Branch:** `feature/ecs-shared-static-components` (opt-in feature).
Read `docs/ecs-shared-static-components.md` and
`docs/agent-handoffs/ecs-shared-static-stage1-2026-11-19.md`
before integrating it. Also read `docs/ecs-agent-handoff.md`,
`docs/ecs-mechanic-composition.md` and root `AGENTS.md`.

## Use the right storage

- Keep frequent mutable data as ordinary `World::add<T>` components.
- Repeated configuration values that can change *per Entity* through
  assignment/reinterning: `SharedRef<T>` + `SharedStore<T>`.
- Prefab/asset-defined immutable configuration: `StaticRef<T>` +
  `StaticTableBuilder<T>::seal()` / `StaticTable<T>`.
- Do not confuse static configuration with a static/static-mobility entity:
  the latter may have dynamic components but read immutable config.
- Shared values and static tables always return **const T**. Never add a
  mutable pointer or reference accessor. Do not store native pointers or
  external scene-asset handles in serializable ECS config.

## Contract

- Public headers: `engine/sim/SharedStaticComponents.h` and
  `engine/sim/SharedStaticPersistence.h`. Do not change `ECS.h` without
  separate ECS branch coordination/parity testing.
- `T` defines a stable field-wise `io(Archive&,T&)` overload. Deterministic
  contents, no raw pointer serialization, no unordered container iteration.
- `SharedRef<T>` and `StaticRef<T>` are 4-byte World-local indices.
  SharedStore is a World resource, copied in snapshots with immutable
  payloads shared; StaticTable is external immutable content shared across
  Worlds. Persistent saves store the static table's fingerprint/binding,
  NOT its full content.
- Register schemas during `GameModule::setup` using
  `registerSharedPersistence<T>` and
  `registerStaticPersistence<T>` with stable type IDs.
  Check `validateShared<T>(world)` and
  `validateStatic<T>(world,table)` after load.
- `setStatic` binds once. A new static config requires a new sealed table
  and an explicit content/version/world migration.
- Use `SharedReadView<T>`/`StaticReadView<T>` to amortize store lookups
  in hot loops. Do not retain views across World copy/rollback/compaction.
- `compactShared<T>` may run only at a deterministic structural/tick
  barrier after flush, without worker jobs or external cached SharedRef IDs;
  previous snapshots keep their own ref mapping.
- Performance is O(1), but latency may differ from regular components.
  Never claim faster/same speed without Release per-platform benchmarks.
- Run CTest `ecs_shared_static_components_test` and target
  `aiverse_ecs_shared_static_bench` on target hardware, then capture actual
  result, compiler, git SHA and any warnings in the stage handoff.
- Keep RTS/FPS behavior and save protocols unchanged until a migration
  has verified checksum/replay and serial format parity.

## Follow-up integrations

- Prefer StaticRef for types/prototypes from shared asset definitions.
  Use SharedRef for large repeated runtime config settings. Both work in
  any genre: FPS, RTS, RPG, racing, city builders, server simulation.
- When SceneAsset/SceneLoadCoordinator creates many instances of a prefab,
  bind a single static table once per ECS World and attach 4-byte refs.
  Do not duplicate config objects during additive level loading.
- Keep skill, architecture doc, AGENTS rules and stage handoff updated
  after each implementation step. Never mark unrun tests as passing.

## Prefab config catalog (Stage 3)

Use `engine/sim/PrefabStaticCatalog.h` for stable reusable configs
assigned by prefab ID. The builder accepts prefab keys in any order
and seals sorted key-to-`StaticRef<T>` bindings plus an immutable
`StaticTable<T>`. Identical config values are deduplicated.

`PrefabStaticCatalog<T>::bindWorld(world)` ensures two versioned
fingerprints match: unique config table content and prefab key-to-slot
mapping. `attach(world,entity,prefabKey)` works at entity creation;
`read(world,entity)` is const. On save/load, the native host must
use `registerPrefabStaticPersistence<T>(simulation,stableId,version)`
during GameModule setup, and call `catalog.validateWorld(world)`
after load/restore before stepping the World. Do **not** also
register static persistence for the same T a second time.

The scene loader has a typed `SceneObjectAsset::prefab` string that
an integration adapter can forward to `attach`. Keep that bridge
an opt-in native callback until both scene and ECS feature branches
have passed their native tests. Do not merge the scene branch
implicitly or duplicate immutable T values per spawned instance.

New native fixture: `ecs_prefab_static_catalog_test` (source only;
native execution still pending). Its test cases cover 10k cross-scene
entities, a separate preview ECS World, deterministic importer order,
alias mapping revision conflicts, snapshot and v10 save/restore.
Record the actual compiler and CTest output in
`docs/agent-handoffs/ecs-shared-static-stage3-2026-11-19.md`.

## Real SceneLoadCoordinator integration (Stage 4, 2026-11-19)

Use `engine/scene/PrefabStaticSceneBinding.h` from the isolated
`feature/ecs-static-prefab-scene-integration` branch. The bridge
converts `PrefabStaticCatalog<T>` into a
`SceneLoadCoordinator::SpawnPrefab` callback. Bind the immutable
catalog and register both Scene and prefab schemas in
`GameModule::setup`, then use the callback for deterministic
scene-activation entity creation. Immutable table is external to ECS,
and survives additive scene unload while other scenes/Worlds reference it.

Use `MissingPrefabConfig::Reject` for required config domains, or
`Skip` for optional typed domains and compose spawners through
`PrefabSceneSpawnChain` in stable order. Only write ordinary
per-entity components during instance initialization; never edit the
shared/static T payload. On rollback, invalidate scene tickets and
rehydrate resource leases WITHOUT re-spawning static refs.

The actual CTest integration source is
`tests/ecs_static_scene_integration_test.cpp`, target
`aiverse_ecs_static_scene_integration_test`. It spans real
`SceneRuntime`, `SceneLoadCoordinator`, two ECS Worlds, persistent
additive scenes, unload, replace, failure/cancel, native save/COW
and static alias mismatch. **The combined targeted integration was compiled and passed 7/7 CTests
on Mac Studio (2026-11-19)**; full engine/FPS/RTS and GPU validation
remain outstanding. Read handoff
`docs/agent-handoffs/ecs-static-scene-stage4-2026-11-19.md` before
continuation. Keep the original feature branches intact.


## Multi-domain prefab configuration recipe (Stage 5)

For a single prefab with separate Weapon, AI and Inventory immutable
configuration, prefer `engine/scene/PrefabStaticDomainRegistry.h`:

```cpp
auto registry = std::make_shared<aiverse::scene::PrefabStaticDomainRegistry>();
registry->addDomain<WeaponConfig>(
    "combat/weapon", "game.prefab.weapon", weapons);
registry->addDomain<AIConfig>(
    "ai/brain", "game.prefab.ai", brains);
registry->addDomain<InventoryConfig>(
    "items/inventory", "game.prefab.inventory", inventories,
    aiverse::scene::MissingPrefabConfig::Skip);
registry->seal();
// During GameModule setup, before native ECS registry seal:
registry->registerPersistence(sim);
registry->bindWorld(sim.world());
// SceneLoadCoordinator owns entity creation at deterministic tick barrier:
auto spawn = registry->callback(mutableInstanceInitializer);
```

Each catalog has its own shared sealed table and 4-byte Entity
`StaticRef<T>`. The registry validates all required domains before
attaching any of them, supports optional types, rejects duplicate
`StaticRef<T>` types and binds every table against saved fingerprints.
Never store mutable instance HP/ammo inside StaticTable; give each
Entity an ordinary ECS component instead. Do not use scene I/O
completion to choose deterministic gameplay tick.

New native fixture: `ecs_static_scene_multidomain_test` (not yet
executed); extended host validation:
`bash tools/ecs-static-scene-validate.sh build/static-scene extended 8`.
Keep the Stage 5 handoff in sync with actual results.


## Stage 6 — editor/source assets, static-only Inspector and measured RSS

Use `engine/scene/PrefabStaticAuthoring.h` to load/validate editor
authored immutable config definitions. Source JSON has
`version:1`, a stable `domain`, and `prefabs` entries carrying
`id` and typed `config` object. Native game decoder must reject
invalid types/fractional integral data and perform fieldwise conversion;
seal once and share the catalog across scenes and Worlds.

`engine/scene/PrefabStaticInspector.h` generates real
`NativeModule::inspectComponents` compatible JSON with `editable:false`
for the selected entity's static configuration. Owning game module
must call the helper inside its native inspector hook. Never enable
raw `StaticRef` or sealed table runtime writeback. Editing settings
belongs to project source JSON and a controlled reimport/migration.

`ecs_static_scene_editor_contract_test` validates the editor-facing
contract. `aiverse_ecs_shared_static_rss_bench` measures OS resident
bytes and read time in separate processes for ordinary, shared, static
modes. One final validation command after *all feature work*:
`bash tools/ecs-static-scene-validate.sh build/static-scene all 8 1000000`.
Do not report native Stage 6 tests or RSS results before that run.


## Additive collision ID ownership (Stage 7)

For multiple simultaneously loaded scenes sharing one physical SpatialWorld,
use `engine/scene/SceneCollisionNamespaces.h` to allocate deterministic,
non-overlapping static collider ID ranges by sorted stable *instance* key.
Always retain a move-only range Lease while the real
`world::CollisionStream` is alive. The standard legacy 3-argument
CollisionStream still rejects any prior static high ID; the optional
4th `CollisionIdRange` parameter supports additive scenes safely.

`engine/scene/SceneCollisionBridge.h` wraps this exact existing
CollisionStream as a `SceneLoadCoordinator` dependency adapter.
On scene unload it shuts down its own chunk stream and releases only
its collider IDs; another scene's collider IDs remain live.
Gate simulation with `bridge.prepareRegions(regions)` before
consuming any authoritative tick in a region requiring static
collision. The bridge's per-stream publication limits are not a
global scheduler budget. Do not rely on wall-clock worker completion
to decide lockstep command/tick order.

The host must use one namespace catalog per physical SpatialWorld,
and should verify its fingerprint during network content negotiation
and cold load. See `docs/scene-collision-namespaces.md` and
`docs/agent-handoffs/ecs-static-scene-stage7-2026-11-19.md`.
Native CTests `ecs_scene_collision_namespaces_test` and
`ecs_scene_collision_bridge_test` are added but not yet run.
The agreed final gate remains
`bash tools/ecs-static-scene-validate.sh build/static-scene all 8 1000000`.
