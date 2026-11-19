# AIVERSE — immutable prefab configurations in real SceneRuntime

**Integration status:** isolated branch
`feature/ecs-static-prefab-scene-integration`; code and tests committed,
**combined native compile/CTest NOT yet performed**.

This is a real source-level integration of the two previously separate
branches, not a simulation that only imitates scene grouping.

## Source merge

The integration branch contains the actual Git merge of:

- `feature/scene-runtime-multiworld` at
  `61c57988289f0aad157c2f0384cb19ac561077db` — SceneRuntime,
  SceneAsset, SceneLoadCoordinator and scene resource pipeline.
- `feature/ecs-shared-static-components` at
  `58a3680f658a502a378ab4bf4bf5790818a66d5f` — immutable prefab
  catalogs and SharedRef/StaticRef storage.

Merge commit:
`f6bd6fc56ca66f8674f0c1f5dee23d11d17a82cb`.

Only `CMakeLists.txt` overlapped. The integration branch retains test
targets from both branches; neither source feature branch nor `main`
was changed by the merge. The compatibility of the combined code is still
unverified until the full native build runs.

## Integrated architecture

```text
GameModule::setup
  registerScenePersistence(sim)
  registerPrefabStaticPersistence<UnitConfig>(sim, "game.unit")
  catalog->bindWorld(sim.world())
  installSceneHierarchy(sim, hierarchy)

SceneLoadCoordinator
  -> async read/validate SceneAsset (no ECS mutations)
  -> explicit main-thread / deterministic tick commit
     -> SceneRuntime::createEntity(sceneId)
     -> StaticPrefabSceneBinding<UnitConfig>::callback()
        -> static catalog lookup by object.prefab
        -> attach StaticRef<UnitConfig> (4 bytes per entity)
        -> optional mutable per-instance initialization
     -> SceneHierarchy::attach / setParent
  -> additive / replace / unload
     -> remove only entities owned by removed scene
     -> keep immutable table while owner holds shared_ptr<const Catalog>
     -> release scene asset leases through SceneLoadCoordinator

ECS World A (game)   --references--> shared immutable Catalog
ECS World B (preview) --references--> SAME catalog/table, no duplicate T
```

### Public API

`engine/scene/PrefabStaticSceneBinding.h`:

- `StaticPrefabSceneBinding<T>(shared_ptr<const PrefabStaticCatalog<T>>,
  afterAttach?, MissingPrefabConfig::Reject|Skip)`.
- `binding.bindWorld(world)` — call once from GameModule::setup; returns
  false on stale prefab-to-configuration mapping or content revision.
- `binding.callback()` — returns the exact
  `SceneLoadCoordinator::SpawnPrefab` callable. Captures the immutable
  catalog by shared_ptr so the binding object may be destroyed.
- `MissingPrefabConfig::Reject` default fails a scene atomically on
  unknown prefab; `Skip` allows optional configuration domains when
  combined with `PrefabSceneSpawnChain`. An unknown prefab under Skip
  receives no `StaticRef<T>` for that domain.
- `PrefabSceneSpawnChain::add(callback)` — deterministic ordered
  composition of independent prefab configuration domains and per-instance
  initialization; any failing stage cancels the in-progress new scene.

Example native game setup:

```cpp
#include "engine/scene/PrefabStaticSceneBinding.h"
namespace sc=aiverse::scene;
namespace st=aiverse::storage;

// catalog built/sealed from content before GameModule setup.
// Each instance contains only a StaticRef<UnitConfig>, not another
// copy of UnitConfig.
void MyModule::setup(Simulation& sim,const Json&,const Json&) {
    sc::registerScenePersistence(sim);
    st::registerPrefabStaticPersistence<UnitConfig>(
        sim,"my-game.unit-static",1);
    if(!catalog->bindWorld(sim.world()))
        throw std::runtime_error("invalid prefab catalog revision");
    installSceneHierarchy(sim,hierarchy);
}

// Host creates coordinator on the Simulation-owner thread:
sc::StaticPrefabSceneBinding<UnitConfig> binding(
    catalog,
    [](World& world,Entity entity,const sc::SceneObjectAsset& object,
       std::string& error) -> bool {
        // Author mutable per-instance components only.
        // Do NOT mutate UnitConfig or global/other gameplay Entities.
        LocalTransform local{};
        local.position.x=Fixed::fromInt(object.properties.getInt("x",0));
        world.add<LocalTransform>(entity,local);
        return true;
    });

sc::SceneLoadCoordinator coordinator(
    sim.world(), readSceneManifest, sceneResourceAdapter,
    binding.callback(), hierarchy.get());

auto ticket=coordinator.request("scenes/town",sc::LoadMode::Additive);
// coordinator.poll() updates host readiness only.
// A validated tick/transition command commits at the agreed barrier.
if(coordinator.ready(ticket))
    coordinator.commit(ticket);
```

The immutable catalog is an external asset identity; its prefab-key map
and unique StaticTable values are shared among scenes AND Worlds. Its
two authoritative World bindings (table content fingerprint and sorted
prefab-to-slot mapping fingerprint) plus each per-Entity StaticRef
are included in optional native ECS v10 save/checksum. The table itself
is not written into every ECS snapshot.

After a successful cold deserialize/rollback, the host must reattach
the exact sealed catalog/content revision, validate
`catalog->validateWorld(sim.world())`, rebuild SceneHierarchy, call
`SceneLoadCoordinator::invalidateAfterRestore()`, and rehydrate
non-authoritative scene resources with `queueRestoredResidency()`.
Never respawn saved StaticRefs during residency-only rehydration.

## Native integration test

Registered CTest target: `ecs_static_scene_integration_test`,
source `tests/ecs_static_scene_integration_test.cpp`.

The test uses REAL `Simulation`, `SceneRuntime`,
`SceneLoadCoordinator` and `StaticPrefabSceneBinding`. It covers:

- persistent global scene and additive town, with parent/child local
  transforms evaluated by SceneHierarchy;
- separate preview World using the same immutable catalog object;
- 4-byte refs, two unique configs and pointer identity across Worlds;
- shared asset leases across additive scenes;
- unload town while retaining global and preview entities and the table;
- replace with cave, missing-prefab replacement failure preserving
  previously committed scenes;
- COW rollback and resource-only residency rehydration without spawning
  new StaticRefs;
- native Simulation v10 save/deserialize/checksum with sealed catalog,
  and rejection of same-table but changed prefab alias mapping.

**Scope caveat:** the binary save test focuses on `SceneOwner`,
`SceneState`, `StaticRef`, `StaticTableBinding` and
`PrefabCatalogBinding`. Save/load of instance `LocalTransform`
and `Parent` must still be explicitly registered by the hosting
game's persistence schema; the integration doesn't silently replace
existing RTS/FPS transform semantics.

No real GPU resource lifecycle or Box3D/SpatialWorld static collider
ownership is claimed. The separate scene branch has an unresolved
`CollisionStream` static ID conflict for two additive stream instances;
this merge does not bypass the collider ID guard.

## Validation commands

Use an isolated worktree, not the user's main checkout:

```sh
cd ~/Projects/AIVERSE
git fetch origin main feature/ecs-static-prefab-scene-integration

git worktree add -b verify-static-scene \
  ../AIVERSE-static-scene \
  origin/feature/ecs-static-prefab-scene-integration
cd ../AIVERSE-static-scene
git pull --ff-only origin feature/ecs-static-prefab-scene-integration

cmake -S . -B build/static-scene \
  -DAIVERSE_BUILD_TESTS=ON \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build/static-scene --parallel 8 --target \
  aiverse_ecs_static_scene_integration_test \
  aiverse_ecs_prefab_static_catalog_test \
  aiverse_ecs_shared_static_components_test \
  aiverse_ecs_scene_runtime_test \
  aiverse_ecs_scene_load_coordinator_test \
  aiverse_ecs_multiworld_test \
  aiverse_ecs_scene_resource_pipeline_test

ctest --test-dir build/static-scene --output-on-failure \
  -R '^(ecs_static_scene_integration_test|ecs_prefab_static_catalog_test|ecs_shared_static_components_test|ecs_scene_runtime_test|ecs_scene_load_coordinator_test|ecs_multiworld_test|ecs_scene_resource_pipeline_test)$'
```

After these pass, run the hierarchy/COW suites and full native FPS/RTS
save/rollback/replay suites on the integration branch before proposing
a merge to `main`.

## Next stages

- Native compiler/CTest fix cycle for all combined headers and test
  targets; update the stage handoff after each code change.
- Multi-config prefab composition (weapons, inventory, health presets,
  AI settings) through the ordered spawn chain, with per-domain static
  table validation and optional configuration types.
- Add explicit typed immutable asset revision bindings in SceneAsset
  manifests; reject incompatible content before entering multiplayer
  scene transitions.
- Create safe additive collision residency/ID namespacing with the
  world-physics agent, not a second colliding `CollisionStream`.
- Real integration tests with renderer, PHYSIS, TerraWeave, NEREID and
  SpectraFX adapters, including frame/budget profiling.
