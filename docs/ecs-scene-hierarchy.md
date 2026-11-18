# ECS Parent hierarchy — deterministic nested transforms

## Why a separate hierarchy

AIVERSE already has two different transform usages:

- `engine/sim/CommonComponents.h::Transform`: RTS **world-space** `pos`, `prevPos`, yaw. RTS movement, spatial hashing and legacy saves depend on this meaning.
- `modules/rts/view/EntityHierarchy.h`: presentation-only `EntityParent` links evaluated into `render::mat4` using floats. This is appropriate for effect attachments and rendering but not authoritative physics / lockstep.

Do **not** silently redefine the existing RTS `Transform` or use view-only float matrices for simulation.

New opt-in generic API:

- `engine/sim/SceneHierarchy.h`
- `engine/sim/SceneHierarchySystem.h`
- `engine/render/SceneHierarchyRender.h`

### Components

| Component | Responsibility |
| --- | --- |
| `Parent` | Generation-safe `Entity value` link |
| `LocalTransform` | Local fixed-point position, unit quaternion, scale |
| `WorldTransform` | Derived cached world position, quaternion/scale convenience fields, **authoritative fixed-point 3x4 affine matrix** |

`WorldTransform::affine` preserves correct combined transforms when a child rotation follows nonuniform parent scaling (shear). Use it for rendering, collision proxy updates and transformed points. Quaternion/scale convenience decomposition is not exact under shear; do not rebuild the affine matrix from them.

Gameplay uses `Fixed` (Q47.16) only. Rendering converts after the tick using `render::hierarchyMatrix(worldTransform, renderOrigin)`. Render origin subtraction occurs in `Fixed` before conversion to float, preserving large-world precision. Imported quaternions should be normalized once at authoring/import; the runtime assumes unit quaternions.

### Runtime usage

```cpp
#include "engine/sim/SceneHierarchySystem.h"
#include "engine/render/SceneHierarchyRender.h"

auto hierarchy=std::make_shared<aiverse::SceneHierarchy>();

// During GameModule::setup, or after initial Entity creation:
hierarchy->attach(sim.world(),car,aiverse::LocalTransform{});
hierarchy->attach(sim.world(),wheel,wheelLocal);
hierarchy->setParent(sim.world(),wheel,car);

// Register once after movement, before dependent gameplay/view systems:
aiverse::installSceneHierarchy(sim,hierarchy);

// Change local transform through API; this marks its subtree dirty:
hierarchy->setLocal(sim.world(),car,newCarLocal);

// Parent can be changed without destroying children:
hierarchy->setParent(sim.world(),wheel,newParent);

// Before destroy()/destroyLater()/flush(), detach hierarchy node:
hierarchy->detach(sim.world(),wheel);
sim.world().destroyLater(wheel);
```

To register the hierarchy system after a specific engine system, call `installSceneHierarchy` at that point in module setup. It uses ordinary `Simulation::addSystem`, so existing system execution remains unchanged. `Simulation::loadState` and successful `deserialize` call registered state-restored hooks; `installSceneHierarchy` rebuilds derived indices and world-transform caches then.

For a native project that does not instantiate `Simulation`, hold `SceneHierarchy` yourself, use `attach/setParent/setLocal/detach`, and call `update(world)` at your chosen barrier. After restoring ECS data from disk, call `rebuild(world); update(world);`.

**Important lifecycle/API contract:** Do not directly mutate `Parent` / `LocalTransform` or destroy registered entities without notifying `SceneHierarchy`. If external code performs those changes, call `rebuild` after structural changes or `invalidate(entity)` after local changes. The runtime does not intercept arbitrary writes via raw `World::get<T>`. `WorldTransform` is a cache; never treat it as authoritative serialized hierarchy topology.

### Performance

- Children are held in sorted indices, traversal is deterministic and iterative (no recursion stack limit).
- Each node tracks `dirty` and `subtreeDirty`.
- Each mutation marks its subtree and the ancestor path. `pendingRoots_` stores only roots with dirty descendants.
- `update()` processes scheduled roots and skips clean branches; if nothing changed, it returns immediately and visits **zero** nodes.
- `lastUpdated()` counts recalculated transforms, `lastVisited()` counts traversed nodes. Use these counters for diagnostics, not simulation decisions.
- Parent-cycle checks are bounded and generation-aware.
- For very large hierarchies, further optimizations may include index-addressed node tables or level-based job scheduling; these are not yet implemented or benchmarked.

Example benchmark (10k/100k entities, ten entities per hierarchy, 1% changed leaves):

```sh
bash tests/run_ecs_experiment_suite.sh --bench
```

### Rollback and concurrency

Only one thread should perform structural hierarchy changes. Update happens at a Simulation system barrier, after movement writes finish. For parallel future updates, process disjoint branches or depth levels and prepare writable `WorldTransform` COW pages on the caller first. The current hierarchy evaluator is sequential; dirty-branch pruning usually wins for predominantly static trees.

A cached `SceneHierarchy` instance is not included in `SimSnapshot`; `Parent` and `LocalTransform` are part of the authoritative `World`. The post-load hook rebuilds derived indices. Modules with custom binary serialization must explicitly serialize these new component types. AIVERSE does not yet serialize arbitrary components automatically.

### Tests

- `tests/ecs_scene_hierarchy_test.cpp`: nested composition, dirty branches, cycle rejection, stale generations, reparent, detach, deep 3000-level chain, snapshot `World` rollback, affine shear.
- `tests/ecs_scene_hierarchy_simulation_test.cpp`: opt-in system integration, render matrix and `SimSnapshot` rebuild.
- `tests/ecs_scene_hierarchy_bench.cpp`: full vs clean vs sparse dirty workloads.

The new tests must pass on Mac Studio and CI before this opt-in feature is considered ready for production.
