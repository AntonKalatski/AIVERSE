# AIVERSE — immutable prefab configurations in real SceneRuntime

**Integration status:** isolated branch
`feature/ecs-static-prefab-scene-integration`; code and tests committed,
**targeted native integration build and CTest verified on Mac Studio: 7/7 passed, 0 failed (2026-11-19)**. Broader RTS/FPS, GPU and RSS validation remains pending.

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
was changed by the merge. The combined source compiled and the targeted seven-test CTest selection
passed natively on Mac Studio (2026-11-19). Full engine and genre integration
regressions remain pending.

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

## Mac Studio native result (2026-11-19)

The user built all seven selected integration targets. The initial run
was 6/7 because the resource-pipeline test treated two scene references
as two backend leases. The corrected test distinguishes two
SceneLoadCoordinator references from one backend lease. After rebuilding,
`ecs_scene_resource_pipeline_test` passed **1/1** (0.60 s test), and the
complete seven-test group passed **7/7** (0 failures, 0.24 s total).
The two Clang `Pending` initializer warnings were not present in the supplied
rebuild excerpt. Exact local HEAD and full build warning status were not
provided. See `docs/agent-handoffs/ecs-static-scene-stage4-2026-11-19.md`.

This validates the targeted integration, not production FPS/RTS replay,
actual GPU/Metal, additive collision ownership, complete game save formats
or process RSS. Preserve those as explicit pre-merge gates.


## Stage 5: multiple immutable config domains per prefab (source committed)

The opt-in header `engine/scene/PrefabStaticDomainRegistry.h` now lets
one SceneAsset object attach **multiple independent StaticRef<T> types**
such as weapon, AI/brain and inventory. It uses existing
`PrefabStaticCatalog<T>` and `SceneLoadCoordinator`; no rewrite of the
production ECS Sparse Set or `Simulation::step()`.

```cpp
#include "engine/scene/PrefabStaticDomainRegistry.h"
namespace sc = aiverse::scene;

auto definitions = std::make_shared<sc::PrefabStaticDomainRegistry>();
definitions->addDomain<WeaponConfig>(
    "combat/weapon", "mygame.prefab.weapon", weaponCatalog,
    sc::MissingPrefabConfig::Reject);
definitions->addDomain<AIConfig>(
    "ai/brain", "mygame.prefab.ai", aiCatalog,
    sc::MissingPrefabConfig::Reject);
definitions->addDomain<InventoryConfig>(
    "items/inventory", "mygame.prefab.inventory", inventoryCatalog,
    sc::MissingPrefabConfig::Skip);  // Optional for a projectile/ambient entity
if(!definitions->seal())throw std::runtime_error("no prefab domains");

// In GameModule::setup before Simulation seals native persistence schemas:
sc::registerScenePersistence(sim);
definitions->registerPersistence(sim);
if(!definitions->bindWorld(sim.world()))
    throw std::runtime_error("immutable prefab domain mismatch");

// Host, at an owned tick/structural barrier:
sc::SceneLoadCoordinator coordinator(
    sim.world(), readScene, sceneResources,
    definitions->callback(
        [](aiverse::World& world, aiverse::Entity entity,
           const sc::SceneObjectAsset& instance, std::string& error) {
            // Only ordinary per-instance ECS state is mutable.
            world.add<MutableHealth>(entity, ...);
            return true;
        }));
```

The registry sorts domain keys (e.g. `ai/brain` before
`combat/weapon`) regardless of registration order. `seal()` prevents
further registration, rejects duplicate domain names, native schema
identifiers and duplicate `StaticRef<T>` C++ types, because one World
can store only one component of a given T per entity. Native persistence
IDs are validated for allowed ASCII characters and length before
registration.

**Two-pass spawn:** all domains first check that the World is bound to
the correct immutable catalog revision, every required prefab is
supported and each StaticRef is not already assigned. Only then does
the registry attach 4-byte references and invoke the optional mutable
instance initializer. If an initializer fails, SceneLoadCoordinator
cancels the new scene and removes its partially instantiated entities;
previous committed scenes are untouched.

**Two-pass World bind:** each catalog's new read-only
`canBindWorld(world)` checks existing fingerprints before any catalog
is inserted into the World, so a stale second domain never partially
binds the first. Allocation errors can still throw at setup, so do this
only during a structural barrier. The immutable catalogs and table
values may be shared by the game World, a second multiplayer World,
and the editor preview World.

The new `tests/ecs_static_scene_multidomain_test.cpp` checks: three
domains, an optional inventory domain only on soldiers, invalid
mandatory prefab preflight without writes, no partial World binding,
stable native checksum/serialization under **different domain
registration order**, instances with mutable ammo overrides, additive
global+field scenes, persistent objects, replacement and failed
replacement, rollback, native serialization, and rejection of changed
prefab-to-config aliases without a changed underlying unique-value
table. CTest target:
`aiverse_ecs_static_scene_multidomain_test`.

**Native status: new Stage 5 CTest has not yet been compiled or run.**
The preceding Stage 4 target selection was verified 7/7 on Mac Studio,
which is not evidence for this newly added code.

### Expanded native regression runner

Use the script `tools/ecs-static-scene-validate.sh` in an isolated
worktree. It does not fetch/change Git refs, and logs build and CTest
output under `build/static-scene/verification/`.

```sh
cd ~/Projects/AIVERSE-static-scene
git fetch origin feature/ecs-static-prefab-scene-integration
git pull --ff-only origin feature/ecs-static-prefab-scene-integration
bash tools/ecs-static-scene-validate.sh build/static-scene smoke 8
bash tools/ecs-static-scene-validate.sh build/static-scene extended 8
```

`smoke` builds/runs 12 scene/Shared/Static/COW/hierarchy tests.
`extended` includes 9 additional regressions, notably real RTS/FPS
GameModule replay, native persistence, COW rollback session, guarded
parallelism and ECS wave replay (21 tests total). The tool configures
CMake with `AIVERSE_BUILD_TESTS=ON` and
`CMAKE_BUILD_TYPE=Release` automatically. Every test must pass before
considering a production main merge. Platform-specific graphics/Metal,
scene-editor and collision namespaces remain separate verification
requirements.


## Stage 6 — typed prefab authoring, immutable Inspector and measured RSS

Stage 6 adds the remaining **opt-in authoring and inspection seams** without
changing the deterministic ECS, production editor writeback, RTS or FPS
components. Its source was committed after Stage 5, whose new multi-domain
test has still **not** been run natively. Stage 4's prior 7/7 does not prove
the new Stage 5/6 sources work.

### Editor/import pipeline: `PrefabStaticAuthoring<T>`

`engine/scene/PrefabStaticAuthoring.h` parses an authored JSON v1
document into an immutable `PrefabStaticCatalog<T>` using a
project-supplied typed decoder. The document is bounded to 4 MiB,
the number of entries is limited, duplicate IDs are rejected,
relative logical prefab keys are validated, and the decoded catalog
is sorted/interened regardless of JSON source order.

```json
{
  "version": 1,
  "domain": "combat/weapon",
  "prefabs": [
    {"id": "prefabs/soldier",
     "config": {"damage": 60, "range": 5}},
    {"id": "prefabs/turret",
     "config": {"damage": 60, "range": 5}}
  ]
}
```

The *game's* decoder must strictly validate field names, exact numeric
syntax, bounds and version migrations; do not silently coerce unknown
types via `Json::getInt` or floats in deterministic configs. Once
loaded, both soldier and turret reuse ONE `WeaponConfig` value in
the sealed table. The source document can be edited/rewritten by
ordinary editor content tools; changing its revision requires an
explicit authoring-to-runtime rebuild and immutable catalog revision
rebind, **not a write into a live StaticTable**.

### Runtime editor Inspector: `PrefabStaticInspector<T>`

`engine/scene/PrefabStaticInspector.h` emits the actual
`NativeModule::inspectComponents` API-compatible
`components`/`componentSchemas` JSON for a selected real ECS entity.
It reads `StaticRef<T>` and the bound catalog, using a typed, read-only
project callback that renders `const T&` as JSON. The emitted schema
has `editable:false` for every exposed field and displays slot,
unique config count, 16-digit exact hexadecimal table/catalog
fingerprints and typed config properties.

```cpp
// In a native game's module inspectComponents(World&,Entity,Json&,Json&):
PrefabStaticInspector<WeaponConfig> inspector(
    weaponCatalog,"StaticWeapon","Weapon config (read-only)",
    [](const WeaponConfig& config) {
        Json value=Json::object();
        value.set("damage",Json::number(int64_t(config.damage)));
        return value;
    });
inspector.append(world,entity,components,schemas);
```

`engine/scene/PrefabStaticInspector.h` now additionally provides
`PrefabStaticInspectorRegistry`, which lets one NativeModule hook
render several typed immutable StaticRef domains in stable, sorted
component-ID order. Native GameModules register each typed
`const T& -> Json` callback once, call `seal()`, then invoke
`inspectorRegistry.append(world,entity,components,schemas)` from the
existing read-only inspector hook. This does not change the editor's
runtime writeback permissions.

The existing editor Inspector consumes this JSON through its native
module's `inspectComponents` callback. This bridge **does not itself
install an existing game module hook**, nor change global editor
registration/writeback or introduce a generic C++ reflection system.
Each native game opts in and supplies its own typed converter.

The existing `EditorEcsWriteback.h` restricts edits to explicit
authoring bindings; this feature does **not** enable raw StaticRef or
shared StaticTable mutation. Editing configs means editing the
**source JSON asset**, reimporting a new sealed catalog, then
performing an explicit asset migration/reload barrier.

Test source:
`tests/ecs_static_scene_editor_contract_test.cpp` checks deterministic
authoring order, duplicate/path/numeric validation, immutability
and read-only native Inspector JSON schema. CMake target is
`aiverse_ecs_static_scene_editor_contract_test`. Native execution
remains pending until the single requested final verification run.

### Memory benchmark: actual resident bytes per isolated process

`tests/ecs_shared_static_rss_bench.cpp` reports an OS-backed
process resident-set measurement using Mach task information
on macOS, `/proc/self/statm` on Linux and zero if unsupported.
It also prints the canonical read checksum and median CPU read time.

**Ordinary**, **Shared**, and **Static** each run in a separate process,
so allocations/heap fragmentation from another storage mode cannot
contaminate RSS. The benchmark creates 128-byte config values in
one ECS World and reports both baseline and post-build RSS, plus
the delta. The full-run script checks matching read checksums for all
three modes at each unique-value count.

`tools/ecs-static-scene-validate.sh` mode `all` runs:
- one CMake Release configure;
- one **full project native build**;
- one complete serial CTest pass (not just the targeted suite);
- process-isolated RSS/CPU read benchmarks for 1,000,000 entities with
  16, 64 and 1,024 distinct 128-byte configuration values;
- a JSONL log and a Python standard-library summary that checks
  read checksum parity and reports **measured** memory deltas.

It logs the Git SHA and outputs under
`build/static-scene/verification/`. No backend/GPU cache or
production compiler assumptions are hidden. RSS is **not** the same
as payload-only savings, and one RSS number is not a cross-platform
performance claim.

### One-shot final native validation

No Stage 6 native compilation or tests have been executed here.
After finishing all feature changes, use the existing isolated worktree
and run **one** final command:

```sh
cd ~/Projects/AIVERSE-static-scene
git fetch origin feature/ecs-static-prefab-scene-integration
git pull --ff-only origin feature/ecs-static-prefab-scene-integration
bash tools/ecs-static-scene-validate.sh build/static-scene all 8 1000000
```

The `all` mode builds all targets and runs the entire registered CTest
suite, including scene/Static, COW, RTS/FPS, gameplay and editor tests
under the current default CMake options. It does not test disabled
optional platforms/feature flags or Metal GPU scenarios that require
special configuration. Any failing test should be fixed and the
whole gate rerun before a main merge. The script will not declare
success until all stages complete.

### Remaining production limitations

- No automatic mutation of arbitrary third-party native module
  components; authoring and Inspector type conversion use project hooks.
- An immutable `StaticTable` still requires explicit content revision
  migration. Editor authoring changes don't silently rewrite live Worlds.
- Scene streaming collision namespace issues and GPU/Metal/TerraWeave/
  PHYSIS/NEREID integration remain outside this feature's verified
  scope. They must be coordinated with their owners before main.

## Stage 7 — safe additive collision streams

Added a deterministic per-`SpatialWorld` ID namespace catalog,
`SceneCollisionNamespaces`, and a real `CollisionStream` dependency
adapter `SceneCollisionBridge`. They permit simultaneously active
scene collision streams without sharing or overwriting 32-bit static
collider IDs. Existing `CollisionStream` high-ID validation remains
strict in legacy mode; opt-in explicit ranges validate reservation
sizes and already occupied IDs.

The actual SceneLoadCoordinator may now use collision dependency keys
alongside static prefab references; per-scene unload releases only
owned collision resources, while other worlds/scenes retain theirs.
Host `GameModule::readyToStep` must gate gameplay on
`collision.prepareRegions(...)`.

New CTests:
`ecs_scene_collision_namespaces_test` and
`ecs_scene_collision_bridge_test`, linked to the existing
`aiverse_world` library. Their source is committed, but native
build/CTest remain unverified until the user's final single run.
See `docs/scene-collision-namespaces.md` and Stage 7 handoff.


### Additive collision namespace save identity

Opt-in `SceneCollisionPersistence.h` registers and saves the
canonical collision namespace layout fingerprint in the ECS World.
The host validates it on rollback/cold deserialize before physics
ticks. Actual collision lease/stream residency stays host-only and
must be independently rebuilt after restore. Native source test
covers layout save/checksum and mismatch rejection. Native
execution is pending the final full CTest.

## Verified Mac Studio native gate and measured RSS (2026-11-19)

**Latest default-configuration full validation is green.** The user
completed the one-shot `tools/ecs-static-scene-validate.sh ... all`
run, which first builds all native default targets, then executes
every registered CTest and, only on success, runs process-isolated
RSS benchmarks. The output ended with
`RSS samples validated: 9` and `SUCCESS: complete all run`.
The reported full CTest elapsed time was **517.25 seconds**.
The previous full run had 129 CTests. The successful excerpt did not
include the aggregate test count or exact checkout SHA; the success
gate confirms no CTest failure but not a separately verified count.

For one million ECS entities and 128-byte configs (OS resident-set
**delta from each process baseline**, not the entire game's RSS):

| Unique configs | Ordinary RSS delta | Shared RSS delta | Static RSS delta | Static memory saving |
| ---: | ---: | ---: | ---: | ---: |
| 16 | 272.77 MiB | 36.24 MiB | 36.21 MiB | 86.73% |
| 64 | 276.60 MiB | 38.79 MiB | 38.77 MiB | 85.98% |
| 1024 | 280.48 MiB | 40.24 MiB | 39.05 MiB | 86.08% |

Static read pass speedups were 2.58×, 2.49× and 2.10× respectively
compared to ordinary inline values. Shared was 2.42×, 2.30× and
1.32× faster. All read checksums matched in all nine samples:
991000000 (16 unique), 4063000000 (64), and 65486484928
(1024). There were no checksum errors.

These results measure one source configuration and one Mac Studio
measurement pass; they do **not** measure GPU memory, production
game scene residency, cold-load frame spikes, or cross-platform
variance. They validate the default source path and the synthetic
ECS benchmark, not every optional graphics/physics build flag.

For full raw numbers, test-trace provenance and remaining limitations,
see `docs/agent-handoffs/ecs-static-scene-stage9-2026-11-19.md`.

**Merge readiness:** The default full native gate has passed.
Review latest `main` and parallel feature agents before merging;
production-specific Metal GPU modes, network replay across hosts,
and real authored-scene/collision cases may need separate gates.
