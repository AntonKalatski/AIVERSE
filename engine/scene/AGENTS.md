# AIVERSE scene runtime — local agent instructions

Scope: `engine/scene/`. Read `docs/scene-worlds.md`,
`docs/agent-handoffs/scene-worlds-stage1-2026-11-19.md`,
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
