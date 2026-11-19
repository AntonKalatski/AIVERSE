# AIVERSE — multiple scenes and multiple ECS worlds

Status: **Stage 1 implementation, native tests pending**, branch
`feature/scene-runtime-multiworld`. See
`docs/agent-handoffs/scene-worlds-stage1-2026-11-19.md`.

## Key decision: a Scene is not an ECS World

- **ECS World** (`engine/core/ECS.h`) is a container of Entity IDs, component
  pools and World resources.
- **Simulation** (`engine/sim/Simulation.h`) owns exactly one World plus a
  deterministic tick, RNG, ordered commands and snapshot/replay state.
- **Scene** (`engine/scene/SceneRuntime.h`) is a named, independently
  unloadable **set of entities in one ECS World**. Additive levels share the
  same Entity allocator, systems, physics context and tick.
- **SimulationHost** (`engine/scene/SimulationHost.h`) owns several completely
  independent Simulations. The host does not allow raw Entity handles to cross
  World boundaries.
- **world::WorldScene** (`engine/world/WorldScene.h`) streams geometry and
  presentation chunks. It is **not** an ECS World or the SceneRuntime. Reuse it
  for physical data/residency when a future SceneAsset loader is integrated.
- **SceneHierarchy** (`engine/sim/SceneHierarchy.h`) is the existing
  Parent/LocalTransform/WorldTransform graph **within one ECS World**, not a
  scene manager. Cross-scene parent links are allowed within a World.
  Cross-world Parent pointers are forbidden.

### Recommended ownership layout

```text
Process / Engine
  Shared immutable asset cache; Device / Renderer
  SimulationHost
    WorldHandle A -> Simulation A -> ECS World A
      persistent scene (audio, game systems, globals)
      city scene (additive)
      house interior scene (additive)
      streamed chunks (WorldScene / world.async)
    WorldHandle B -> Simulation B -> ECS World B
      independently stepped preview / replay / match
```

**Normal game:** one ECS World plus many scenes. Creating a second ECS World
for every room or streamed chunk wastes state, breaks ordinary Entity links
and complicates simulation/network/physics. Use a second World for an
independent simulation (editor play clone, separate multiplayer match,
replay/preview, isolated minigame, authoritative/client simulation pair).

## Implemented API (Stage 1)

`engine/scene/SceneRuntime.h`:

- `SceneId`: nonzero, monotonically increasing ID scoped to one ECS World.
- `SceneOwner`: authoritative ECS component identifying entity membership.
  Entities without it are host-owned and will **not** be unloaded by scenes.
- `SceneState`: authoritative World resource containing asset identifiers,
  Loading/Active phases, persistent bits, next ID and pending replacement.
  `std::map` gives canonical deterministic traversal order.
- `SceneRuntime::beginLoad(asset, mode, persistent)`: register scene intent.
  Does **not** read assets. Modes: `Additive`, `Replace`.
- `SceneRuntime::createEntity`, `adopt`, `transfer`, `ownerOf`: scene
  membership API. `adopt` rejects an entity already owned by any scene;
  `transfer` is explicit.
- `SceneRuntime::activate(id, hierarchy)`: publish a ready scene, optionally
  removing prior nonpersistent scenes for Replace. The old scene is kept until
  activation. Persistent scenes survive Replace.
- `SceneRuntime::cancel`: abort an unfinished scene and discard its entities.
- `SceneRuntime::unload`: remove all scene-owned entities and metadata. When
  passed the active SceneHierarchy, it detaches nodes **before** destruction;
  children from other scenes become scene hierarchy roots.
- `registerScenePersistence(sim)`: register the version 1 component/resource
  schemas used by native binary save and checksum. Call exactly once **inside
  GameModule::setup**, before persistence registration is sealed.

Example from a native project's `GameModule::setup`:

```cpp
#include "engine/scene/SceneRuntime.h"

void MyModule::setup(aiverse::Simulation& sim,
                     const aiverse::Json& rules,
                     const aiverse::Json& initialMap) {
    aiverse::scene::registerScenePersistence(sim);
    aiverse::scene::SceneRuntime scenes(sim.world());
    auto shared = scenes.beginLoad("levels/global", aiverse::scene::LoadMode::Additive, true);
    auto city = scenes.beginLoad("levels/city");
    // Populate each scene with owned entities in your asset-instantiation layer.
    auto sun = scenes.createEntity(shared);
    auto npc = scenes.createEntity(city);
    (void)sun; (void)npc; (void)rules; (void)initialMap;
    scenes.activate(shared);
    scenes.activate(city);
}
```

Asynchronous loader/presentation must not call ECS structural APIs from a
worker thread: perform CPU preparation in workers, then instantiate and
activate at a deterministic tick barrier on the owning simulation thread.
Do not invoke `unload` during `World::each` or concurrent scheduler lanes.
Pass `SceneHierarchy*` when a hierarchy is installed. The host is responsible
for releasing scene-specific physics, navmesh and GPU resources at a matching
lifecycle barrier; **this stage does not yet do that automatically**.

A scene record in `Loading` is *not* an automatic ECS query filter: it is
the host's responsibility to make the prepared entities eligible at
activation, or to defer instantiation until ready. Thus `beginLoad` is
a **scene lifecycle primitive**, not yet full asynchronous level streaming.

There may be only one pending `Replace` request per ECS World; a concurrent
replacement request is rejected until `activate` or `cancel`. Scene IDs are
never recycled even after cancel/unload, including after rollback to an older
snapshot (where IDs are restored as part of the rollback contract).

## Multiple independent Simulations

`engine/scene/SimulationHost.h`:

```cpp
aiverse::scene::SimulationHost host;
auto first = host.insert(std::move(firstSimulation));   // set up before insertion
auto second = host.insert(std::move(secondSimulation));
host.setPaused(second, true);
host.stepAll();             // only first steps
host.setPaused(second, false);
host.advanceAll(0.05);      // each runs its own fixed tick accumulation
auto* simulation = host.get(first);
host.erase(first);          // invalidates first's WorldHandle
```

`WorldHandle` is `(slot index, generation)`; `WorldEntityRef` is
`(WorldHandle, Entity)`. Do not store or resolve raw `Entity` across worlds.
IDs are checked on every host lookup and stale handles fail after removal
and slot reuse. Slot generation wrap retires the slot permanently.

The host is a single-owner sequential dispatcher, not parallel game-world
simulation. It rejects insert/erase/pause operations during a tick callback.
Worlds keep separate RNG, command queues, tick counters, rollback rings and
gameplay data. The render/backend asset cache may be shared, but mutable ECS
resources must not be shared between worlds without an explicitly designed
message/snapshot bridge.

## Save / rollback contract

A `SimSnapshot` copies the ECS World, so `SceneOwner` and `SceneState`
participate automatically (including COW). To persist them to binary disk
saves, opt-in projects **must** call `registerScenePersistence` before setup
is sealed. This contributes `aiverse.scene.owner` and
`aiverse.scene.state` schemas to native ECS v10 saves/checksum. Legacy
RTS/FPS projects that do not opt in keep their existing formats unchanged.

Deterministic *gameplay* scene transitions must execute as canonical scheduled
commands on the same tick for every lockstep peer. Local asset readiness must
only block wall-clock progress through `readyToStep()`, not select an
activation tick based on worker completion speed. After deserializing:
rebuild `SceneHierarchy` as usual; rebuild/publish renderer/physics caches
from resident scene assets, and do not serialize GPU objects or background
jobs.

## Stage 1 verification

CTest targets registered in the top-level CMake test group:

- `ecs_scene_runtime_test`: additive/replace/cancel, persistent scene
  protection, cross-scene hierarchy unlink, ID generations, COW, and native
  save/load/checksum.
- `ecs_multiworld_test`: separate Simulation ticks, per-world pause,
  cross-world identity and stale WorldHandle rejection.

To run on the user's native workstation (from this branch):

```sh
git fetch origin feature/scene-runtime-multiworld
git switch feature/scene-runtime-multiworld
git pull --ff-only origin feature/scene-runtime-multiworld
cmake -S . -B build/scene-worlds -DAIVERSE_BUILD_TESTS=ON
cmake --build build/scene-worlds --target aiverse_ecs_scene_runtime_test aiverse_ecs_multiworld_test -j8
ctest --test-dir build/scene-worlds --output-on-failure -R '^(ecs_scene_runtime_test|ecs_multiworld_test)$'
```

Tests were **added but not executed natively** in this agent environment.
Do not mark Stage 1 production-ready until compiler/CTest results are
recorded in its handoff, and the full regression suite has passed.

## Future stages (not yet implemented)

1. SceneAsset format/registry with stable asset keys, validated prefab IDs,
   dependency graph and references.
2. Async CPU preparation and main-thread ECS instantiation; unpublish hooks
   for WorldScene, collision, PHYSIS/TerraWeave, water/NEREID, SpectraFX and
   audio. Shared immutable assets referenced rather than duplicated.
3. Explicit activation/loading barriers, preloading, cancellation,
   deterministic scene transition commands and scene-specific world events.
4. Scene hierarchy/editor tree and additive/unload UI, prefab overrides,
   scene validation and serialization/migration.
5. Network replication interest across scene regions, persistent-entity
   transfer, scene-travel save/load and multiworld tooling.
6. Cross-genre RTS/FPS/native integration & profiling; large-world streaming
   stays chunk-based rather than creating a new ECS World for every chunk.

Until a verified migration, do not rewrite RTS/FPS map loading, clone a second
canonical gameplay state or change the meaning of the legacy world-space
`Transform`. Follow `docs/ecs-agent-handoff.md` and
`docs/ecs-scene-hierarchy.md` for concurrent ECS work.
