# AIVERSE — additive scene collision namespaces

Branch: `feature/ecs-static-prefab-scene-integration`  
Status: **optional source implementation; native build and CTest pending**.

## Problem solved

The existing `world::CollisionStream` used static collider IDs
`0x80000000 + chunkIndex*65536 + localColliderIndex` in every
independent stream. It rejected construction whenever a
`SpatialWorld` already contained any high static ID. That was correct
for single-world streaming, but prevented additive levels with
independent collision streams in one physical World.

The new API preserves the legacy behavior and adds **explicit
deterministic, disjoint ID ranges**. It does not create a second physics
engine, collision cache or triangle decoder.

## Core API

`engine/world/CollisionStream.h` now accepts an optional last argument:

```cpp
world::CollisionStream stream(manifest, reader, spatialWorld);
    // Legacy 3-arg mode: unchanged high-ID guard

world::CollisionStream additive(
    manifest, reader, spatialWorld,
    world::CollisionIdRange{firstSlot, reservedChunkSlots});
    // Explicit namespace: only rejects colliders occupying this range
```

Each namespace slot reserves 65,536 IDs, allowing up to 65,535
colliders per chunk. Up to 32,768 such slots fit in the upper half of
32-bit collider IDs. A scene must reserve enough slots for its entire
manifest chunk catalog **before any worker starts**; unused reserved
slots are intentionally unavailable to other scene instances until
the namespace catalog itself is regenerated at a safe content barrier.

`CollisionStream` retains its existing SHA-256-verified chunk loading,
`AsyncChunkStream` worker jobs, bounded staging, terrain seam checks
within one stream, `SpatialWorld` publication and shutdown. The
`WorldAssets` manifest is unchanged.

## Deterministic host namespace catalog

New `engine/scene/SceneCollisionNamespaces.h`:

```cpp
scene::SceneCollisionNamespaces namespaces;
namespaces.define("instances/global", 4); // max 4 terrain/collision chunks
namespaces.define("instances/town", 12);
namespaces.define("instances/cave_01", 3);
namespaces.seal();                        // sort by stable instance key

auto lease = namespaces.acquire("instances/town");
if(!lease) throw std::runtime_error("collision range already leased");

// Declare lease BEFORE stream so stream destructs first:
auto stream = std::make_unique<world::CollisionStream>(
    worldManifest, reader, spatialWorld, lease->range());
```

`define` validates stable relative instance names, rejects duplicates,
rejects zero or exceeding the total 32,768-slot capacity.
`seal` allocates ranges in **lexically sorted instance-key order**
regardless of scene arrival/worker completion order, and calculates a
deterministic fingerprint of the full layout. A move-only RAII lease
prevents a second active stream from owning the same range even when
no colliders have been uploaded yet. The lease must outlive the stream.

The catalog is **per physical `SpatialWorld`**. The same canonical
layout may be constructed separately in two independent ECS Worlds,
but each host must use its own leases. Content revisions changing
the catalog require an explicit reset/migration, not an opportunistic
allocation based on load completion timing.

Scene keys must identify **instances**, not only scene asset files:
multiple simultaneously active copies of one cave asset require
distinct stable instance keys and disjoint namespaces.

## SceneLoadCoordinator bridge

New `engine/scene/SceneCollisionBridge.h`:

```cpp
scene::SceneCollisionBridge collision(spatialWorld, namespaces);
collision.define("collision/global","instances/global",
                 worldCollisionManifest, readChunkBytes);
collision.define("collision/town","instances/town",
                 townCollisionManifest, readChunkBytes);

scene::SceneLoadCoordinator loader(
    sim.world(), readSceneJson, collision.adapter(),
    prefabSpawnCallback, hierarchy.get());
```

A `SceneAsset` may list `collision/town` among its `dependencies`.
When the scene is explicitly committed at its deterministic tick
barrier, `SceneLoadCoordinator` leases that resource from
`SceneCollisionBridge`. The bridge creates a real
`CollisionStream` in its assigned, currently unoccupied namespace.
When the scene unloads, `SceneLoadCoordinator` releases the resource;
the stream shuts down and removes **only its own collider IDs**, and
then the namespace lease is returned.

A physical scene cannot be treated as collision-ready immediately
after `SceneLoadCoordinator::commit`: its chunks may still be
preparing asynchronously.

**Before advancing a simulation tick** that can touch required
collision areas, call `collision.prepareRegions(bounds,requests,
publications)` and do not step until it returns true:

```cpp
if(!collision.prepareRegions(requiredRegions)) {
    // Return false from GameModule::readyToStep(sim).
    // No tick/commands/RNG may be consumed while collision is missing.
    return false;
}
return true;
```

A host with mixed asset types (render, terrain, collision, audio)
should combine callbacks from `SceneCollisionBridge::adapter()`
with its other dependency adapters through a stable routing layer.
The standalone bridge only handles its own defined collision keys.
`prepareRegions` requests/publications budgets apply **per resident
CollisionStream**; a global cross-stream fixed CPU work budget is a
future optimization.

## Scope and non-goals

- No change to a legacy three-argument `CollisionStream` call.
  Its high-ID guard remains strict.
- Existing (SpatialWorld) collider queries and triangle/terrain
  collision code are reused.
- This is an opt-in host layer; no silent conversion of production
  FPS/RTS physics modules or editor terrain to additive scenes.
- The allocator prevents ID aliasing only when all additive streams
  follow a single per-World `SceneCollisionNamespaces` catalog.
  Direct manually assigned overlapping ranges are guarded when the
  target already contains colliders, but no API can detect a future
  as-yet-unpublished collision stream unless the host holds leases.
- WorldAssets validates and hashes *each* source manifest/chunk.
  No static terrain seam validation across **different scene
  instances** is performed: overlapping/adjacent authored terrain
  must be checked in the editor and its gameplay behavior remains
  project-owned. Within one stream the original seam guard remains.
- Collision residency, active leases and worker progress are derived
  host state, **not authoritative ECS resources**. The stable
  namespace layout fingerprint should be validated alongside
  content versions in network sessions and cold save loading.
- GPU/Metal/NEREID/PHYSIS/TerraWeave physics coupling and editor
  terrain authoring still require separate integration tests.
- Full collision namespace code and tests have **not** been compiled
  or executed natively yet.

## Validation

`tests/ecs_scene_collision_namespaces_test.cpp`:
- stable namespace assignments independent of registration order,
  duplicate keys/capacity limit, move-only lease protection;
- two real hashed `CollisionStream` workers in the same
  `SpatialWorld` with different upper-32-bit IDs;
- legacy high-ID check and duplicate explicitly overlapping range
  rejection;
- unloading/reloading one stream preserves the other's collisions.

`tests/ecs_scene_collision_bridge_test.cpp`:
- actual `SceneLoadCoordinator` with persistent global and additive
  town scenes using collision dependencies;
- per-scene collision residency, region-ready barrier, independent
  unload, replacement with cave while persistent global survives,
  final cleanup of all ID leases and collider objects.

CMake targets:
`aiverse_ecs_scene_collision_namespaces_test` and
`aiverse_ecs_scene_collision_bridge_test`, linked against the
existing `aiverse_world` target.

Both are included in the `smoke/extended` suite and in the complete
default CTest `all` gate. The user requested **one final full
native test pass**, so don't run isolated tests prematurely.
Final command in the existing Mac Studio worktree:

```sh
cd ~/Projects/AIVERSE-static-scene
git fetch origin feature/ecs-static-prefab-scene-integration
git pull --ff-only origin feature/ecs-static-prefab-scene-integration
bash tools/ecs-static-scene-validate.sh build/static-scene all 8 1000000
```


## Persistence binding for lockstep and cold loads

New optional `engine/scene/SceneCollisionPersistence.h` provides
`CollisionNamespaceBinding{uint64_t fingerprint}` plus
`registerCollisionNamespacePersistence`,
`bindCollisionNamespaces(World&,catalog)` and
`validateCollisionNamespaces(const World&,catalog)`.

During `GameModule::setup`, before native registry sealing:

```cpp
scene::registerCollisionNamespacePersistence(
    simulation, "aiverse.scene.collision-namespaces", 1);
if(!scene::bindCollisionNamespaces(simulation.world(),namespaces))
    throw std::runtime_error("collision namespace layout mismatch");
```

After COW rollback/cold deserialize, require
`validateCollisionNamespaces(sim.world(),namespaces)` before
advancing physics. The only authoritative data stored is the
deterministic fingerprint; collision worker progress and leases
remain **host-only**, never serialized. One independent physical
SpatialWorld per Simulation is the intended use. Multiple distinct
physical spaces inside one Simulation require separate typed/ID
registrations and a larger host integration.

`ecs_scene_collision_bridge_test` additionally covers native v10
serialization, rollback, same-layout restoration and rejection of
a layout whose scene names match but reserved capacities differ.
**Native execution remains pending.**
