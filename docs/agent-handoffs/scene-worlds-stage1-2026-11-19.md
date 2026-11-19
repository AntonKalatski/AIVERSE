# Scene runtime / multiworld — Stage 1 agent handoff

Date: 2026-11-19  
Repository: `chromealex/AIVERSE`  
Branch: `feature/scene-runtime-multiworld`  
Base: `main` (branch created directly from current remote main)  
Most recent implementation/docs commit before this handoff: `7b09ca5d406b95c716c1eec0ca6d75a85eef0c13`  
Status: **code committed, native build and CTest NOT run**.

## Intent and ownership

Provide genre-independent support for multiple additive or replacement scenes
inside a single ECS World and multiple independently stepped ECS simulations
in the same process. No RTS/FPS/renderer engine rewrites, no changes to
`ECS.h` or `Simulation::step()`. New shared APIs live in
`engine/scene/`. Read `engine/scene/AGENTS.md` and `docs/scene-worlds.md`.

**Three distinct meanings of world already exist:**
`aiverse::World` = ECS data; `Simulation` = World + tick/RNG/commands;
`world::WorldScene` = streaming/presentation chunks. The new system does
not collapse those concepts. `SceneHierarchy` remains per ECS World.

## Implementation completed

1. `engine/scene/SceneRuntime.h`
   - `SceneId`, `SceneOwner`, `SceneState`, `SceneRecord`.
   - `SceneRuntime::beginLoad`, `createEntity`, `adopt`, `transfer`,
     `ownerOf`, `activate`, `cancel`, `unload`, `record`, `count`.
   - Additive and Replace modes; old scene retained until replacement is
     ready to activate. Persistent scenes survive replacement. Only one
     pending Replace at a time. Stale IDs never recycled within one rollback timeline.
   - Unload gathers scene-owned entities before mutation, sorts by Entity
     index/generation, detaches SceneHierarchy nodes before destruction.
     Foreign children are promoted to hierarchy roots.
   - Registry stored as a copyable ECS World resource, member ownership as
     a component, so SimSnapshot / COW contains all authoritative scene
     state. Versioned Archive IO and optional native ECS v10 registration
     through `registerScenePersistence`.
   - `beginLoad` is intentionally *not* a file loader; it creates a
     lifecycle record into which an external asset loader can instantiate.
     No unverified async IO claims.

2. `engine/scene/SimulationHost.h`
   - `WorldHandle` (index,generation), `WorldEntityRef`.
   - `SimulationHost::insert/get/contains/alive/erase/setPaused/isPaused/
     size/stepAll/advanceAll`.
   - Independent Simulations, safe stale-handle rejection after slot
     reuse, deterministic slot-order stepping, reentrant lifecycle mutation
     rejected during steps (RAII guard resets on exception).
   - Does not automatically parallelize worlds or provide cross-world
     message transport.

3. Native CMake tests added (not run):
   - `tests/ecs_scene_runtime_test.cpp`: additive/replace/cancel, persistent
     scenes, cross-scene Parent unlink, rollback, COW and binary native ECS
     persistence with checksum.
   - `tests/ecs_multiworld_test.cpp`: two separate Simulations, pause,
     tick advancement, stale handles, cross-world entity identity.
   - Both targets added to the top-level CMake test foreach.

4. Documentation:
   - `docs/scene-worlds.md`: architecture, API example, save/rollback,
     execution barriers, roadmap and validation commands.
   - `engine/scene/AGENTS.md`: scoped rules for subsequent contributors.
   - This handoff: status, verified/unverified and concrete continuation.

## Git commit trail

- `c843518d3294cf3703929db35bf61341ac8cd9ac` — scene ownership/lifecycle.
- `f0235c6fa1aa28e3d5d941ebfd13fec1247a3dcd` — multi-Simulation host.
- `4b0f592663c5b06a874c89a9535c769028045a06` — scene regression test.
- `36a7eda8bbb5a02a453a848ec2789e6feab12436` — multiworld regression test.
- `0f43013b81c5a2131d2e2f62e709f2f8629ffacf` — CMake test registration.
- `2e9d10cfa44934a70e0d2ee3cefb0318443bae0a` — host reentrancy guard.
- `27f6609396a73e74e6a9e71a54147a46b787b676` — architecture/API doc.
- `7b09ca5d406b95c716c1eec0ca6d75a85eef0c13` — scoped AGENTS.

No runtime build or execution has occurred in this environment; there
is no native test output to report. **Do not label Stage 1 tested.**

## First action for next agent / user test

Use a separate worktree so other active AIVERSE work stays intact:

```sh
cd ~/Projects/AIVERSE
git fetch origin main feature/scene-runtime-multiworld
git worktree add -b verify-scene-runtime ../AIVERSE-scene-runtime origin/feature/scene-runtime-multiworld
cd ../AIVERSE-scene-runtime
git pull --ff-only origin feature/scene-runtime-multiworld

cmake -S . -B build/scene-worlds -DAIVERSE_BUILD_TESTS=ON
cmake --build build/scene-worlds --target \
  aiverse_ecs_scene_runtime_test aiverse_ecs_multiworld_test \
  aiverse_ecs_scene_hierarchy_test aiverse_ecs_scene_hierarchy_simulation_test \
  --parallel 8
ctest --test-dir build/scene-worlds --output-on-failure \
  -R '^(ecs_scene_runtime_test|ecs_multiworld_test|ecs_scene_hierarchy_test|ecs_scene_hierarchy_simulation_test)$'
```

If CMake configuration requires other repository-specific options, use
those documented in `AGENTS.md`. Record actual compiler diagnostics,
CTest names/count, branch HEAD and machine/platform in this handoff. Fix
failures in the feature branch, never by overwriting a separate agent's
working copy. Run the wider ECS/RTS/FPS save/replay suite before integration.

## Limitations / design decisions for next stage

- **Not implemented:** asset dependency resolution, scene file formats,
  actual asynchronous loading, incremental entity instantiation budget,
  renderer/physics/navmesh/audio unpublish hooks, editor scene UI, global
  replicated scene-travel commands and interworld messaging.
- `Loading` is a lifecycle flag, not an automatic query filter. For now,
  only instantiate authoritative scene entities at a deterministic commit
  barrier, or prevent their gameplay update until activation.
- Pass active `SceneHierarchy*` to unload/activate/cancel for graph-tracked
  scenes. `SceneHierarchy` is a derived cache; rebuild after rollback/load.
- Direct `SceneRuntime` calls on multiplayer peers MUST execute at the
  same validated command tick. Worker completion order must not influence
  simulation outcomes. Use `readyToStep()` to gate immutable content
  availability.
- Rollback restores `SceneState::nextId`, so asynchronously prepared jobs
  from the discarded future must be invalidated with a host-owned timeline
  epoch in Stage 2, not only a World-local SceneId.
- World-local `SceneId` is not unique across independent simulations;
  transport scene IDs with WorldHandle (or canonical scene asset key).
- Entity references from other scene entities can outlive an unload;
  generation safety detects stale targets, but gameplay systems must
  validate them before use.
- Persistent `SceneState` requires explicit registration; avoid applying
  this automatically to legacy RTS/FPS save v9 without a versioned upgrade.
- No scene-specific memory or GPU residency budget enforcement yet.
  Reuse `world::ChunkStreamer`, `world::WorldScene` and existing model
  caches rather than duplicating them.

## Concrete next implementation step: Stage 2

Create a typed `SceneAsset` manifest and a genre-independent
`SceneLoadCoordinator` which connects asynchronous preparation to the
`SceneRuntime` activation barrier. Start with an in-memory mock asset
reader and fake resource adapter, so tests can prove:

1. Two additive scenes load/unload independently; repeated assets dedupe
   immutable resource handles while preserving separate scene instances.
2. Replace is atomic from the simulation's perspective (old scene survives
   until ready), cancellation ignores late worker completions, and failures
   preserve the old scene.
3. Fixed-tick activation is driven by host commands, never worker timing;
   after cold save/rollback, world scene state and hierarchy are rebuilt.
4. Resource release invokes presentation/collision callbacks once per
   scene instance, with reference counting across additive scenes.
5. Native, genre-independent project code can instantiate the loader via
   public `AIVERSE` targets without editing core or linking RTS/FPS.

After Stage 2, update this document, `docs/scene-worlds.md`,
`engine/scene/AGENTS.md`, and record exact code commits and **actual**
test output before proceeding.
