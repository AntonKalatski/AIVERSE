# AIVERSE Universal Niagara-style VFX — agent handoff

**Date:** 2026-11-18  
**Working branch:** `feature/universal-entity-inspector-fps`  
**Goal:** universal, composable VFX for RTS, FPS, racing, RPG, strategy and native games. Never create an FPS-only or RTS-only particle system. Extend existing `engine/fx/ParticleSystem`; keep VFX presentation-only and authoritative ECS/rollback deterministic.

## Stage 1 — versioned graph frontend (COMMITTED, TESTS NOT RUN)

**Implementation SHA:** `5afe37d4ee6418897b49ee5ac3e27cbe32362c40`  
**Standalone test source SHA:** `b17ba98f0675be2f628b2177b83d8b210f9250eb`  
**CTest registration SHA:** `43f0090c681f5826746a62e51df55fa33c553b50`

### Files

- `engine/fx/VfxGraph.h`: new header-only, version-1 Niagara-like graph authoring IR and compiler.
- `tests/vfx_graph_compiler_test.cpp`: standalone headless compile, branch fan-out, parameter override and invalid-graph checks.
- `CMakeLists.txt`: `aiverse_vfx_graph_tests` / CTest `vfx_graph_compiler` target.

### Architecture and API

```cpp
#include "engine/fx/VfxGraph.h"
aiverse::fx::EffectDef compiled;
std::vector<std::string> errors;
bool ok = aiverse::fx::compileVfxGraph(graphJson, parameterOverrides, compiled, errors);
if (ok) {
    int effect = particleSystem.define("projectile_impact", compiled);
    particleSystem.play(effect, worldPosition);
}
```

Graph data is a JSON object with `version:1`, optional `parameters`, `nodes`, `links`, `outputs`, optional `forceFields`. Nodes: `Spawn`, `Initialize`, zero or more `Update`, and an output `SpriteOutput` / `MeshOutput` / `RibbonOutput`. One path per output: Spawn → Initialize → Update* → Output. Output fan-out is supported; node merges and cycles are rejected. Each node holds `properties` from the existing `FxEffect.h` emitter schema; nested properties merge. Values can refer to a graph parameter using `{"$param":"name"}`, with optional literal overrides. The existing strict `parseEffect` validates resulting emitters.

The resulting `EffectDef` uses the CURRENT CPU simulation and rendering code; it does not execute a graph per particle and does not add compute/GPU particle support. `MeshOutput` requires `meshes`; `RibbonOutput` forces hidden particle ribbon trails.

### Tests and limitations

- **NOT RUN**: no local AIVERSE checkout/build environment was available in this connector-backed stage. `CMakeLists.txt` registration is committed, but neither compilation, CTest, rendering nor CI has been verified. Do not claim the test passes.
- Commands for the next agent:
  ```bash
  cmake -S . -B build/vfx-test -DAIVERSE_BUILD_TESTS=ON
  cmake --build build/vfx-test --target aiverse_vfx_graph_tests -j4
  ctest --test-dir build/vfx-test --output-on-failure -R '^vfx_graph_compiler$'
  ```
- **Critical caveat:** Graph v1 is a data IR compiled to the old effect schema, not a complete programmable Niagara graph. No arithmetic/dataflow nodes, custom attributes, multi-input merge, function graphs, runtime GPU kernels, visual graph editor, depth-based collisions or soft particles yet.
- Handle branch concurrency carefully: other agents are changing editor/ECS/Metal code. Never force-push or overwrite unrelated work.
- Existing RTS FX bridge: `modules/rts/fx/RtsFx.*` and `apps/client/ClientFx.*`. Existing particle renderer: `engine/fx/{ParticleSystem,FxEmit,FxRender,FxInternal}.*`; CPU dynamic vertex expansion, batch-by-texture/blend, capped quality tiers.
- Stability: `fx/<name>.json` assets and `fx/fx.json` RTS bindings are unchanged. New graph frontend opt-in; no migration of existing assets.

## Next stages (DO NOT claim completed)

1. **Stage 2:** generic read-only ECS VFX presentation bridge. Add components describing asset + attachment + binding, and a presenter that only *reads* gameplay state and consumes view events, never writes render handles into authoritative World. Test two unrelated genre hosts and entity generations; preserve RTS legacy bridge during migration. Commit a separate stage-2 handoff with actual SHA/tests.
2. **Stage 3:** graph asset file format, loading and caching/hot reload, exposed parameters, reusable typed graph modules, editor preview integration and MCP graph commands; add a dedicated visual graph editor instead of only JSON.
3. **Stage 4:** GPU particle simulation backend (Metal compute first, Vulkan/desktop as supported, fallback to CPU GLES3/WebGL2); data layout, queue/barriers, spawn/update/compact, compute/graphics sync, prefix sum and culling. Do not claim parity on untested backends.
4. **Stage 5:** advanced GPU features: particle collision/depth sampling, scene geometry/physics interface, camera-facing and mesh instancing, sorting, ribbons, lights, GPU event channels, persistent pools, LOD/scalability.
5. **Stage 6:** convert existing RTS/FPS/native integrations to generic VFX contract, cross-genre performance, screenshot and platform CI regression gates. Retire the RTS-only event binding path only after parity.

Every completed stage must follow `AGENTS.md`: implementation commit(s), then a **separate handoff commit in this document** with SHA, paths, actual tests/CI statuses, caveats and next step. Do not promise Unreal source compatibility or a fully working Niagara editor before those stages are implemented.

## Stage 2 — cross-genre ECS presentation system (COMMITTED, TESTS NOT RUN)

**Implementation SHA:** `60657dded40c58bd07b701df7c7ea1bd34b94120`  
**Standalone test SHA:** `7c743f48a6c92112a9756392d1965bf964be1620`  
**CTest registration SHA:** `e19a0ac9074bd3c4aa714396e748f68eedb95b63`

### Files

- `libraries/include/aiverselib/genre/EcsVfxPresentation.h`: data-only `VfxSourceComponent`, `VfxEventBinding` and read-only `VfxPresentationSystem`.
- `tests/vfx_ecs_presentation_test.cpp`: two distinct ECS component types (space unit, racing vehicle), emitter attach/move, event playback, generation reuse, reset.
- `CMakeLists.txt`: `aiverse_vfx_ecs_tests` and CTest `vfx_ecs_presentation`.

### Public integration

```cpp
#include <aiverselib/genre/EcsVfxPresentation.h>
using namespace aiverse::lib::genre;
auto& source = world.add<VfxSourceComponent>(entity);
source.effect = "engine_fire";  // existing fx asset or compiled graph output
VfxPresentationSystem presenter(particles);
presenter.sync(world, [](const World& w, Entity e,
                         aiverse::fx::float3& pos, aiverse::fx::float3& dir) {
    // Resolve render transform from an arbitrary native project component.
    return resolveYourPresentationPose(w, e, pos, dir);
});
// After each committed tick, not after each render frame:
presenter.consumeTickEvents(tick, sim.events(), {{hitEventId, {"impact_flash", 1_fx}}});
// On new match, rollback/resimulation or preview reset:
presenter.reset();
```

`VfxSourceComponent` has effect name, fixed-point scale, revision, enabled flag, and kill/fade on detach. Generation-packed Entity keys prevent handles leaking to recycled entities. If an attachment is out of view / budget rejected, the bridge retries when it becomes visible. Do not attach one-shot assets through this component (the bridge will restart non-looped effects); use `consumeTickEvents` for one-shots. The game-specific host provides pose resolution and the mapping from module-defined `SimEvent.type` IDs to effect assets; this package contains **no RTS/FPS includes** and writes **nothing** to authoritative ECS.

### Verification

**NOT RUN:** sources and standalone test target committed, but no C++ compilation, CTest, CI or screenshot tests have completed in this environment. Required commands:

```bash
cmake -S . -B build/vfx-test -DAIVERSE_BUILD_TESTS=ON
cmake --build build/vfx-test --target aiverse_vfx_graph_tests aiverse_vfx_ecs_tests -j4
ctest --test-dir build/vfx-test --output-on-failure -R '^vfx_(graph_compiler|ecs_presentation)$'
```

### Constraints / next step

- Neither RTS nor FPS game loops have been migrated to call this adapter yet. The old `RtsFx` bindings must remain until event parity is verified; do not claim integration done.
- The bridge consumes `SimEvent` and `World` **only** on the presentation side, not via an ECS gameplay `Simulation::addSystem`; timing remains under the host's render loop.
- Duplicate-tick suppression is global per presenter. On rollback or replay, call `reset()` before replay; split-screen/multiple worlds need separate presenters. Future work should generalize stable presentation event IDs across snapshot/resimulation.
- Next: Stage 3 — load and hot-reload versioned graph assets in a reusable registry; integrate graph asset edits with the existing FX editor/MCP and add graph-aware tests. Then GPU simulation and visual node editor are separate stages, not implicit in the current work.

## Stage 3 — graph assets, exposed-parameter variants and hot reload (COMMITTED, TESTS NOT RUN)

**Implementation SHA:** `582763839d818e04287723cba6143453648680d0`  
**Standalone test SHA:** `26a3acb7f2d6511ef9a070539e56ed7528ca1214`  
**CTest registration SHA:** `cd5f25b784a1beab1af2c5177376e239338fcf57`

### Files

- `engine/fx/VfxGraphAssets.h`: public header-only `VfxGraphAssets` and `VfxGraphLoadResult` (CPU graph compilation registry).
- `tests/vfx_graph_assets_test.cpp`: graph load, unchanged-content cache, parameter variants, reload after source change, last-good fallback on malformed JSON, path traversal rejection and invalidate.
- `CMakeLists.txt`: `aiverse_vfx_assets_tests`, CTest `vfx_graph_assets`.

### Stable public C++ API

```cpp
#include "engine/fx/VfxGraphAssets.h"
aiverse::fx::VfxGraphAssets assets(particles,
    [](const std::string& path, std::string& text) {
        return readGameContent(path, text); // provided by the native game
    });
auto result = assets.load("engine_fire"); // fx/graphs/engine_fire.vfx.json
auto variant = assets.load("engine_fire", aiverse::Json::parse(R"({"size":2})"));
if (result.effect >= 0) {
    particles.play(result.effect, {0,0,0});
    // A VfxSourceComponent may reference result.runtimeName once loaded.
}
// The next assets.load() checks the source content hash and recompiles on changes.
// Invalid reloads return the previous good compiled effect plus errors.
```

The registry hashes source content using stable FNV-1a 64-bit and builds parameter variant names from stable serialized override objects. Graph output materializes into an existing `ParticleSystem::define` effect, with separate effect identities for each exposed-parameter variant. `fx/graphs/<name>.vfx.json` names are validated (A-Z/a-z/0-9/_/-). The renderer's CPU effect ID is retained for an in-place valid hot reload. Failed reloads do not overwrite a working effect. `invalidate()` forces recompilation on the next load. The registry intentionally does not perform filesystem IO itself; native games can use archives or platform readers.

**Compatibility:** old `fx/<name>.json` and old `fx/fx.json` remain unchanged. Native games can opt in through public headers; no render-server or gameplay simulation dependency was added.

**NOT RUN:** no local compiler, CTest, CI status, Metal device or screenshot verification was available in this execution context. Required compile/run commands:

```bash
cmake -S . -B build/vfx-test -DAIVERSE_BUILD_TESTS=ON
cmake --build build/vfx-test --target aiverse_vfx_graph_tests aiverse_vfx_ecs_tests aiverse_vfx_assets_tests -j4
ctest --test-dir build/vfx-test --output-on-failure -R '^vfx_(graph_compiler|ecs_presentation|graph_assets)$'
```

### Limitations and immediate next action

- Graph loading is **not yet wired** into the existing FX editor or agent MCP commands; no visual node editor, shader/compute nodes or GPU simulation exists. The runtime registry is callable by native projects today once compiled, but the existing RTS and FPS production game loops do not consume it automatically.
- A fallback on bad asset reload keeps the previous in-memory effect; an explicit `ParticleSystem::reloadAll()` invalidates stored effect IDs, so the host must also clear/invalidate its `VfxGraphAssets` registry before reloading. A future integrated hot-reload service should make this automatic.
- Next bounded stage: editor/MCP integration of graph resources, graph node canvas, parameter editing and graph-to-effect preview; **then** introduce GPU compute backend with CPU fallback and platform-specific capability checks.
- After every next implementation stage, append exact implementation SHA, branch, paths, actual tests and handoff commit here; do not mark stage validated until the test commands pass.

## Stage 2 corrective checkpoint — rollback one-shot cleanup

**Implementation SHA:** `9cc82382781e852f307b7cf7ca083aff36ad07a4`  
**File:** `libraries/include/aiverselib/genre/EcsVfxPresentation.h`  
The presenter now retains up to 65,536 recent event-spawned `FxHandle` values and kills those handles during `reset()`; this prevents recent one-shot remnants being left in the scene after rollback. Beyond this bounded history, very old handles cannot be retracted by the presenter: the cap keeps bookkeeping bounded. For effects older than the cap that must disappear, the host may reset its entire particle world at new-match boundaries.

**Tests:** NOT RUN. The stage-2 test currently exercises event playback and reset cursor, but does not assert old one-shot particle removal; extend `tests/vfx_ecs_presentation_test.cpp` before declaring rollback fully verified. The `FxHandle` cleanup also needs a high-volume profiling pass.

**Next:** finish stage-3 editor/MCP integration or introduce the graph GPU backend only after compiling all three new tests and verifying platform support.

## Stage 4 — AI skill, AGENTS rule and discovery registration (COMMITTED, TESTS NOT RUN)

**Skill SHA:** `040a283087f3d28a2939d260909dccdc480a6292`  
**AGENTS.md SHA:** `14230d7c1e7dbbfb5c4f8f0e70bdcac252faad76`  
**Skill generator SHA:** `b054038ef1bd1c51b02a1a74cc5245a204d9cdf8`  
**Generated catalog SHA:** `d48037587ec193a2e46133a8e629caa641d0df50`

Files: `skills/visuals/aiverse-niagara-vfx/SKILL.md` (new), `AGENTS.md`, `tools/skills/catalog.py`, `skills/catalog.json`. AI agents now have a durable skill with versioned graph JSON example, current CPU backend and ECS API, cross-genre integration rules, outstanding tasks and reproducible test commands. AGENTS.md links this handoff and forbids genre forks or false claims that the GPU/editor portion is done. The skill generator and catalog both list `aiverse-niagara-vfx` with explicit search aliases.

**Tests NOT RUN.** Run `python3 tools/skills/catalog.py check` as well as the three new VFX CTest targets. The catalog JSON skill list is in lexical ID order, but this is not equivalent to a generator validation. As with all stages, code presence does not prove build correctness.

**Next stage:** implement editor/MCP graph asset CRUD and a visual node canvas; validate tool bindings and cross-project native import. GPU backend needs a separate device capability/RHI design, shader compilation and performance testing before enabling in production. Do not modify authoritative ECS or replace legacy FX until equivalent presentation behavior is measured.

## Stage 5 — bounded O(1) event handle tracking and rollback test (COMMITTED, TESTS NOT RUN)

**Implementation SHA:** `d1766010fffc405578c2b41c5d0e4e812fb9b3e0`  
**Test patch SHA:** `ce19be88d787ee26ce851bf1fc43d99b4734de27`

- `libraries/include/aiverselib/genre/EcsVfxPresentation.h`: replaced `erase(begin)` in recent one-shot handle history with a bounded ring overwrite; each event capture is now O(1) even after the 65,536-handle limit. Rollback reset still kills all handles retained in the ring.
- `tests/vfx_ecs_presentation_test.cpp`: added an assertion that `reset()` clears both attached particles and recent event-spawned one-shots from the headless CPU particle snapshot.
- **TESTS NOT RUN.** The updated source and test were committed, not compiled or executed. Run `ctest --test-dir build/vfx-test --output-on-failure -R '^vfx_(graph_compiler|ecs_presentation|graph_assets)$'` after building targets.
- **Risk:** Very old one-shot effects whose handles have been overwritten by the bounded ring cannot be killed by a presenter-only reset. Their natural expiration is expected for normal short-lived effects. A production full-world reset can instead clear all particles, if the host owns the entire particle world.
- **Next:** integrate graph assets into the editor and MCP, then implement an actual graph canvas and GPU backend; user-facing Niagara parity is NOT delivered by these foundational stages.
