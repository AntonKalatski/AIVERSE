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

## Stage 6 — official product identity: AIVERSE SpectraFX (COMMITTED, TESTS NOT RUN)

**Date:** 2026-11-18. **Branch:** `feature/universal-entity-inspector-fps`.  
**User request:** invent a distinctive VFX-system name, actually use it in the engine's code and leave a durable handoff. **Decision: AIVERSE SpectraFX** (public namespace `aiverse::spectra`; skill `aiverse-spectrafx`). Unreal Niagara remains an inspiration/reference, **not** the product name.

### Commits and files (in implementation order)

| Commit | File | Change |
|---|---|---|
| `b2e684492661b14d79efba33276659d10a4a5d67` | `engine/fx/VfxGraph.h` | Real `compileSpectraGraph` functions, compatibility `compileVfxGraph` wrappers |
| `6c1d503dcbd23359b04703f280c47e0f7d7aaf5e` | `engine/fx/VfxGraphAssets.h` | Canonical `SpectraGraphAssets`/`SpectraGraphLoadResult` with old `Vfx*` type aliases |
| `0ee7a5a2dad956e38b63d67d94cfc2a7f043f427` | `libraries/include/aiverselib/genre/EcsVfxPresentation.h` | Canonical `SpectraSourceComponent`, `SpectraEventBinding`, `SpectraPresentationSystem`; aliases preserve component identity |
| `92eb8d3375b8963d6a7696e31d8c72810028796d` | `engine/fx/SpectraFX.h` | Public `aiverse::spectra::{Runtime,Effect,Handle,Quality,GraphAssets,GraphLoadResult,compileGraph,kName}` |
| `7cfca2c6e3a7f430a86f6bb20a6037a7459ed119` | `libraries/include/aiverselib/genre/SpectraFX.h` | Native-project `aiverse::spectra::{SourceComponent,EventBinding,PresentationSystem}` |
| `78e57322ab1e668bc585e3ef31c9ac64911a81e0` | `tests/vfx_graph_compiler_test.cpp` | Uses branded public `spectra::compileGraph` |
| `b88671bdd26c7170839b162e00e96186ac92f148` | `tests/vfx_graph_assets_test.cpp` | Uses `spectra::Runtime`/`spectra::GraphAssets` |
| `7e2e47c277978d8f50beca1775f8b9b8251c4d67` | `tests/vfx_ecs_presentation_test.cpp` | Exercises `spectra::SourceComponent`/`PresentationSystem` |
| `e3b4d7e0e6b78f05f2f51016b24981d1d1c17294` | `CMakeLists.txt` | Adds `AIVERSE::spectrafx` alias for `aiverse_fx` and labels regression tests `SpectraFX` |
| `770fa8ec42dc4a9669f16e8d2d88eafb31215444` | `docs/spectrafx.md` | Official architecture/API/graph schema, integration and roadmap |
| `44918a3d9de7321645a67808c7db7a2ece938dec` | `skills/visuals/aiverse-spectrafx/SKILL.md` | Canonical AI skill |
| `a3b3b590a659527338382a76675307cc37483a73` | `skills/visuals/aiverse-niagara-vfx/SKILL.md` | Legacy skill redirects to official branding |
| `1cab6922a806d305fbe88e9a5fafe7dcb7490fb9` | `tools/skills/catalog.py` | Adds official `aiverse-spectrafx` workflow/search and keeps Niagara alias |
| `bb734cb476f1732751fb6a1003082c5720e93700` | `skills/catalog.json` | New canonical skill entry + legacy entry updated |
| `a327028b6e9350601b25951836995fc3b394ff45` | `docs/particles.md` | Explains SpectraFX overlay on existing CPU effects, incomplete future work |
| `6f19737e427ee6281fe482b5d00139144d398d53` | `AGENTS.md` | Official name, headers, CMake target, skill, handoff contract |

### New canonical native API

```cpp
#include <aiverselib/genre/SpectraFX.h>
#include "engine/fx/SpectraFX.h"

aiverse::spectra::Runtime particles;
aiverse::spectra::GraphAssets graphAssets(particles, readContent);
auto compiled = graphAssets.load("rocket_exhaust");
if (compiled.effect >= 0)
    particles.play(compiled.effect, {0, 1, 0});

aiverse::spectra::SourceComponent component;
component.effect = compiled.runtimeName;
world.add<aiverse::spectra::SourceComponent>(entity, component);

aiverse::spectra::PresentationSystem presenter(particles);
// presenter.sync(world, nativePoseResolver);
// presenter.consumeTickEvents(tick, sim.events(), gameEventBindings);
```

Native CMake: `target_link_libraries(my_native_target PRIVATE AIVERSE::spectrafx)` (include dirs already follow the existing aiverse_fx target; `libraries/include` must also be in the native project's include paths for the `aiverselib/genre` header).

**Migration/compatibility:** no change to legacy `fx/<name>.json`, `fx/fx.json`, old effect runtime IDs, serialized simulation state or `RtsFx`; no duplicate renderer. Old names `compileVfxGraph`, `VfxGraphAssets`, `VfxSourceComponent`, `VfxPresentationSystem`, `VfxEventBinding` are source-level aliases/wrappers. Because the ECS component type aliases resolve to the exact same types, existing ECS type IDs are preserved (do not replace aliases with wrappers or derived types). Graph asset path stays `fx/graphs/*.vfx.json` for compatibility.

### Test record — NOT RUN

No local checkout/build/CTest runner was available while making the connector-backed edits, so compilation, `ctest`, skill catalog check and visual QA have **NOT** been executed; do not claim success. Next agent must run:

```bash
cmake -S . -B build/vfx-test -DAIVERSE_BUILD_TESTS=ON
cmake --build build/vfx-test --target aiverse_vfx_graph_tests aiverse_vfx_ecs_tests aiverse_vfx_assets_tests -j4
ctest --test-dir build/vfx-test --output-on-failure -L SpectraFX
python3 tools/skills/catalog.py check
```

### Known limitations and exact next task

1. Confirm that the new renamed C++ types and aliases compile/link, that all three tests pass, and that generated `skills/catalog.json` equals the catalog generator's output. Fix immediately if not; report actual outputs.
2. Hook `SpectraFX` graph asset CRUD, preview and runtime loading into `apps/editor/core/FxOps.*` / `Workspace` / the existing Effects window and MCP registration, without breaking legacy effect operations; then create a visual graph editor.
3. Add GPU compute particle backend with explicit RHI capability checks and CPU fallback; never call existing graph v1 a full Niagara dataflow/GPU implementation.
4. Validate genre-neutral actual host usage for FPS and RTS (not only header-level fixtures), entity attachment, effect IDs, rollback and visuals, then migration/parity.
5. Continue appending handoff entries **in this same file**, separately committed after each implementation stage; preserve all other agents' changes to this active branch.

## Stage 7 — SpectraFX MCP graph authoring and CPU preview bake (COMMITTED, TESTS NOT RUN)

**Date:** 2026-11-18. **Branch:** `feature/universal-entity-inspector-fps`.  
**Goal:** make SpectraFX graph-v1 frontend available through both editor Workspace and headless MCP without genre-specific simulation dependencies.

### Implementation and documentation commits

| SHA | File | Change |
|---|---|---|
| `cceac6c08471785a19b3556f474ad276616c23f3` | `apps/editor/core/SpectraGraphOps.cpp` | Six universal `fx_graph_*` operations |
| `b71ad7b6dd00c4b44b09312b430c272089011968` | `apps/editor/core/FxOps.cpp` | Register graph operations with existing FX tools |
| `13ffbf60776c71681603bde2381c9b1ddc40b3f5` | `tests/vfx_graph_ops_test.cpp` | Workspace graph CRUD, validation, bake, safety regression |
| `a8609363e7eed3ee7d53b64af4d2884a8d350f65` | `CMakeLists.txt` | Build and CTest target `aiverse_vfx_ops_tests` / `vfx_graph_ops` |
| `510bd7e11fbf6c8269334f94c7abc3cd04245f35` | `docs/spectrafx.md` | Document MCP graph API |
| `4d7856a46d4e47130d7cb56718bb3227bffeeb4b` | `skills/visuals/aiverse-spectrafx/SKILL.md` | Agent graph-edit workflow |

### API, architecture and compatibility

Registered in `registerFxOps` through `registerSpectraGraphOps`: `fx_graph_list`, `fx_graph_get`, `fx_graph_validate`, `fx_graph_set`, `fx_graph_delete`, `fx_graph_bake`. Graph validation uses `aiverse::spectra::compileGraph` and is CPU-only. Graphs live in `fx/graphs/<name>.vfx.json` and are limited to 1 MiB. `fx_graph_set` rejects invalid compilation without overwriting the source; valid changes call `Workspace::note(Change::Fx)`. `fx_graph_bake {name,effect,overrides?,overwrite?}` materializes a separate `fx/<effect>.json` compatible with existing `fx_preview` and legacy clients; old effect files are not overwritten unless `overwrite:true`. Bake is a snapshot: editing source graphs does not automatically refresh baked effects.

Game simulation, deterministic ECS authority, rollback state and old `RtsFx` are untouched. There is no node-canvas editor and no GPU simulation in this stage.

### Verification status — NOT RUN

GitHub connector files and commits were created. No local AIVERSE checkout, build, CTest result or screenshot was available in this stage. **No test is claimed passing.** New `vfx_graph_ops` test was added but has not executed.

Commands for the next agent:

```bash
cmake -S . -B build/vfx-test -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_EDITOR=ON
cmake --build build/vfx-test --target aiverse_vfx_graph_tests aiverse_vfx_ecs_tests aiverse_vfx_assets_tests aiverse_vfx_ops_tests -j4
ctest --test-dir build/vfx-test --output-on-failure -L SpectraFX
python3 tools/skills/catalog.py check
```

Review the new `SpectraGraphOps.cpp` and `vfx_graph_ops_test.cpp` for actual build/runtime errors before production usage. This must be done independently of GitHub source presence.

**Next bounded stage:** editor node-canvas for the exact graph-v1 schema (select/add/move/link/delete nodes, exposed parameters, compile diagnostics, preview through `fx_graph_bake`/existing CPU renderer); then standalone GPU RHI capability and compute-stage design. After each implementation stage append a **separate handoff commit** to this same file, listing SHAs, tests actually executed and the next action.

## Stage 8 — visual SpectraFX node canvas and in-memory preview (COMMITTED, TESTS NOT RUN)

**Date:** 2026-11-18. **Branch:** `feature/universal-entity-inspector-fps`.  
**Scope:** Basic authoring/preview of the existing graph-v1 format for **all genres**; no additional gameplay authority, no forked FPS/RTS renderer and no GPU compute claims.

### Implementation commits / paths

| SHA | Path | Change |
|---|---|---|
| `87e2c5647d1502514807664c1785e8858c51ce16` | `engine/fx/VfxGraph.h` | Optional `nodes[].position:[x,y]` authoring metadata, validated and ignored by CPU compiler |
| `458d2794a09fda86f8d39494ee393b82fcca6638` | `apps/editor/windows/SpectraGraphWindow.cpp` | Optional ImGui SpectraFX window: load/new/save, graph canvas, panning, add/drag/link/delete, inspector JSON, parameters, undo/redo, compile messages |
| `a58becfbde9d1bdd03c04d30aa10a16f2515617e` | `apps/editor/EditorFxOps.cpp` | GUI-only `fx_graph_preview`: compile graph in memory into `gameFx_` and play in Scene/Game |
| `5f08de8ed7c931e4bff460973fe71362eb88ad51` | `apps/editor/EditorFxOps.cpp` | Correct escaped JSON screenshot hint and newlines |
| `95357caa81f68bd6cd4709a783e97dc82daf0d20` | `apps/editor/windows/SpectraGraphWindow.cpp` | Include `FxOps.h` for canonical graph asset file-name validation |
| `868c077aa9288f9de77d624944525308d48f3bb9` | `tests/vfx_graph_compiler_test.cpp` | Authoring metadata accepts valid positions, rejects malformed/out-of-range positions, has no effect on CPU emitter output |
| `3aa17d390fae819458cc3f89127f25804e3b2877` | `engine/fx/VfxGraphAssets.h` | Include `ParticleSystem.h` explicitly so SpectraFX public headers are self-contained |
| `ed968dda74724abbad926709944dd6a6a278db24` | `docs/spectrafx.md` | Basic canvas, graph preview, saved node metadata, limitations |
| `08fda8e982eb494a089e02d8a49336b04453a397` | `skills/visuals/aiverse-spectrafx/SKILL.md` | Agent GUI workflow and preview tool |
| `2acb0cd9d22b30c6136d8dfafa7a71a2615fc0a5` | `docs/particles.md` | Update capability/limitation status |
| `b191861b09eca4050f41c820276be4ee977cbf3f` | `AGENTS.md` | Official universal VFX canvas/preview integration rules |
| `594e4793fbe079bdc2340f2be7aa992eca8fc48e` | `engine/fx/SpectraFX.h` | Update public header capability comment |

### API, authoring data and compatibility

- The optional editor window is registered as `id()=="spectrafx_graph"`, title **SpectraFX**, window group `look`. Existing CMake `file(GLOB ... apps/editor/windows/*.cpp)` picks it up without a CMake registration edit.
- Graph nodes retain existing `{id,type,properties}` and may optionally add `"position":[x,y]` in **the same** `fx/graphs/<name>.vfx.json`. Compiler validates two numeric coordinates, absolute bound <=100000 and then ignores them when constructing `EffectDef`. Older graph sources remain compatible, with computed initial visual layout. This is source-file-only metadata and does **not** affect simulation save formats, ECS checksums or rollback.
- Basic canvas: click a node header to select; left-drag to move; middle-drag pans; click the output pin and then another node's input to connect (single parent / cycle guard); add existing v1 node types through combo; edit the selected node's `properties` as JSON; edit root `parameters` JSON in a modal; undo/redo the full document.
- Save invokes `Workspace::call("fx_graph_set",...)` (strict validation, `Change::Fx`). Invalid/incomplete graph edits can be drafted but not saved as valid assets. Existing `fx/*.json` unaffected.
- New GUI-only `fx_graph_preview {graph, name?, overrides?, x?,y?,z?, advance?,loop?,freeze?}` compiles the **unsaved** graph and calls `ParticleSystem::define("__spectrafx_editor_preview",...)` then `play` with the Scene/Game camera. Unlike `fx_graph_bake`, this command writes no FX files; `view_screenshot` remains the visual verification tool.
- Currently the property inspector edits raw per-node JSON rather than typed Niagara-style individual module pin values. The graph is an ordered module-overlay IR, not an arbitrary runnable dataflow graph.
- `SpectraGraphAssets` now explicitly includes `ParticleSystem.h` for correct standalone header inclusion; no public symbol changed.

### Verification record — NOT RUN

GitHub file creation and per-file commits succeeded. **No CMake build, CTest execution, editor launch, screenshot, Metal/Android validation or runtime FPS/RTS parity test was performed.** This environment has no local AIVERSE checkout, and source review is **not** proof of successful compilation.

The next local agent MUST execute:

```bash
cmake -S . -B build/vfx-test -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_EDITOR=ON
cmake --build build/vfx-test --target aiverse_editor aiverse_vfx_graph_tests aiverse_vfx_ecs_tests aiverse_vfx_assets_tests aiverse_vfx_ops_tests -j4
ctest --test-dir build/vfx-test --output-on-failure -L SpectraFX
python3 tools/skills/catalog.py check
```

Then launch AIVERSE Editor, open **Графика → SpectraFX**, verify create/save/load, node drag/link/relink/cycle prevention, undo/redo, JSON property edits, legacy file preservation, preview and `view_screenshot`. Also test opening an older graph without `position` and re-saving it.

### Risks and exact next stage

1. **Build/UX unverified:** Inspect `SpectraGraphWindow.cpp` and `EditorFxOps.cpp` for compiler/platform errors and ImGui interactions; fix after a real build. Test the editor only when its existing unrelated branch gates are green.
2. **Graph behavior:** Raw property JSON and graph-v1 single-parent paths are an initial authoring UI only. Empty/disconnected output graphs fail validation by design; add a typed parameter/module inspector and UX for incomplete graph drafts, plus limits on high-density canvas interactions.
3. **Preview lifecycle:** The ephemeral effect uses a reserved runtime name inside the editor's existing CPU particle system. Reloading the particle registry from another window invalidates this preview; add a dedicated visual preview resource or reload-aware integration, and validate multi-window interactions.
4. **Engine GPU work still absent:** Design explicit RHI compute capability checks and GPU particle buffers (spawn/update/compact, emitters, alive counters, sorting, events), CPU fallback for GLES3/WebGL2 and per-device benchmarks; do not silently enable GPU path before parity tests.
5. **Next bounded implementation:** First run/repair native builds and vfx tests, then extend SpectraFX v1 module editor and production genre-neutral rendering adapters. Record the next implementation SHA/test outcomes in a separate handoff commit **before** beginning another stage.

## Stage 9 — typed SpectraFX module inspector and source preservation (COMMITTED, TESTS NOT RUN)

**Date:** 2026-11-18. **Branch:** `feature/universal-entity-inspector-fps`.  
**Goal:** replace blind raw-JSON edits for common graph-v1 fields with shared, validated field metadata, without losing unrecognized/complex emitter properties or binding logic to one game genre.

### Exact committed changes

| Commit SHA | File | Change |
|---|---|---|
| `fef79683d36a7bf1d707bf2023f1700eccb7359a` | `engine/fx/SpectraModuleSchema.h` | New pure C++ authoring field specifications and safe typed field validation/update/remove |
| `ecce17c209acd851f9051ad0fe9ae940900e3869` | `apps/editor/windows/SpectraGraphWindow.cpp` | Generic typed UI controls in SpectraFX graph-v1 inspector with advanced JSON fallback |
| `c8baa6bf043984dbcd4ef4d2a54a1dedc0b025e6` | `tests/vfx_graph_compiler_test.cpp` | Added schema/validation/no-mutation/unknown-property retention/CPU-compile checks |
| `de2d28d41348c9d5865d0c080223955dbad7ff06` | `engine/fx/SpectraFX.h` | Exposes schema through official public native C++ header |
| `d2d5f69bd192fe537e5f77292cd34b080ea1596c` | `docs/spectrafx.md` | Typed authoring contract, supported common field list and limitations |
| `c97c5272b8fbf4d8414c3f0410f7c29d7eadbc8d` | `skills/visuals/aiverse-spectrafx/SKILL.md` | Agent workflow for typed fields and complex module fallback |

### Architecture, APIs, serialization

The header `engine/fx/SpectraModuleSchema.h` defines `aiverse::spectra::FieldKind`, `FieldSpec`, `fieldsForNode(type)`, `fieldForNode(type,key)`, `validateTypedField(spec,value,error)`, `applyTypedField(node,key,value,error)`, `removeTypedField(node,key,error)`. The canonical include `engine/fx/SpectraFX.h` exports these declarations. This metadata is pure authoring logic and is **not** a replacement for the existing strict emitter loader `fx::parseEffect`.

Typed fields cover common `Spawn`, `Initialize`, `Update`, `SpriteOutput`, `MeshOutput` and `RibbonOutput` properties. Widgets support bounded number/integer, boolean, nonempty string, validated enum choice and finite 3D vectors. An edit is performed on a copied node and committed only if its own field validation succeeds. Updating or removing a typed field leaves unknown and unrelated JSON properties untouched. The GUI shows complex existing values (curves/ranges) as JSON-only rather than converting them to scalar values. Selected node's raw JSON inspector stays available for all advanced/unsupported data.

The graph format is unchanged (version 1, `nodes[].properties`, `nodes[].position`), so legacy `fx/*.json`, graph assets, old C++ aliases and simulation save files remain compatible. Nothing writes renderer handles to deterministic ECS state, and no GPU compute code was added. Runtime authoring, CPU effect, ECS event and client modules were not modified.

### Tests actually executed

**NOT RUN:** There is no local AIVERSE checkout in this environment and external GitHub access from the container was unavailable (DNS failure). GitHub connector writes and reads succeeded, but do not establish compiler or runtime correctness. `tests/vfx_graph_compiler_test.cpp` was extended; neither CMake/CTest nor GUI interaction was run. No CI status was verified. Therefore **do not mark stage 9 production-ready**.

Local QA commands for the next agent:

```bash
git switch feature/universal-entity-inspector-fps
git pull --ff-only origin feature/universal-entity-inspector-fps
cmake -S . -B build/vfx-test -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_EDITOR=ON
cmake --build build/vfx-test --target aiverse_editor aiverse_vfx_graph_tests aiverse_vfx_ecs_tests aiverse_vfx_assets_tests aiverse_vfx_ops_tests -j4
ctest --test-dir build/vfx-test --output-on-failure -L SpectraFX
python3 tools/skills/catalog.py check
```

Manual editor QA: open **Графика → SpectraFX**; test each typed control, non-destructive curve/gradient handling, undo/redo for sliders and text fields, JSON inspector synchronization, project switching, save/reload, bad enum handling and Scene preview. Check native includes and CMake public target `AIVERSE::spectrafx` compile on macOS.

### Risks, known limitations and exact next stage

1. GUI code has not been compiled or interacted with on a real ImGui renderer. In particular, validate active-widget undo grouping, String text editing, default field additions and multi-window preview lifecycle. Fix actual build/UI failures before calling this complete.
2. The schema covers only frequent scalar/vector fields, not a full Niagara module library. Nested `noise`, `collision`, `trail`, `light`, `subEmitters`, curves, gradients and complex shader data still require the existing JSON/particle tools.
3. The typed schema is intentionally a convenience layer; `parseEffect` is the final authority when saving an entire graph. Typed edits can leave an incomplete graph until connected.
4. No GPU backend or GPU execution is present. The next independent stage should **first verify and repair stage 7–9 builds/tests**, then design and implement RHI compute capabilities, GPU simulation buffers and a CPU/GPU execution selector with GLES3/WebGL fallback. Do not merge genre-specific VFX engines, alter deterministic ECS ownership, or make unverified performance/parity claims.
5. After the next bounded code stage, append another **separate handoff commit** to this file with implementation SHA, actual test results, risks and precise continuation steps. Preserve all concurrent agents' work.

## Stage 10 — Metal GPU compute particle subset with CPU fallback (COMMITTED, TESTS NOT RUN)

**Date:** 2026-11-18. **Branch:** `feature/universal-entity-inspector-fps`.  
**Goal:** take the first bounded step from CPU-only SpectraFX to a GPU compute particle backend using the existing RHI, preserving CPU compatibility and all RTS/FPS gameplay/event behavior.

### Verified source findings before design

- The RHI `engine/gfx/Gfx.h` already defines an optional compute contract: `supportsCompute`, `createCompute`, `beginCompute`, `setComputeBuffer`, `setComputeUniforms`, `dispatch`, `endCompute`, storage buffers and debug readback.
- Current `engine/gfx/metal/GfxMetal.mm` implements compute and guarantees per-dispatch barriers; storage `MTLBuffer` can be bound through `setVertexBuffer` in the render encoder. The existing `engine/cloth/ClothGpu.cpp` uses this API.
- Vulkan, GL33 and GLES3 in this branch do **not** expose a functional general RHI compute implementation. Do not activate the GPU path there on the basis of GLSL source alone.
- Existing CPU emitter contract is in `engine/fx/ParticleSystem.*`; `modules/rts/fx/RtsFx.*` remains production.

### Implementation commits

| SHA | File | Change |
|---|---|---|
| `c0e79b7b77c61898f34512cae431ac65c8deacc3` | `engine/fx/SpectraGpuShaders.h` | Metal compute kernel for spawn, integration, GPU billboard expansion; matching GLSL 430 source held for future Vulkan backend (NOT USED) |
| `397cd205d7a84e9555d96cb9d4c68188807d0395` | `engine/fx/SpectraGpuParticles.h` | Public opt-in CPU/Metal selection, config, stats, lifecycle and emitter API |
| `4f1c84f436979130bbc187d4df5f1630d2542a3e` | `engine/fx/SpectraGpuParticles.cpp` | GPU buffer allocation, compute queue, additive GPU render, CPU `fx::ParticleSystem` fallback |
| `c9cfd28ad1d98dc2c17a646332d7eb02176eed3f` | `tests/vfx_gpu_backend_test.cpp` | Pure backend-selection and headless CPU fallback regression |
| `a67b0194c9b4bd5c1c53bc661ede89bf92b9fd4b` | `CMakeLists.txt` | Link new .cpp into `aiverse_fx`, register CTest `vfx_gpu_backend` with SpectraFX label |
| `c340bbf90479744b71940263a14183d2e2ac3a04` | `engine/fx/SpectraFX.h` | Expose the optional GPU emitter through the existing public native header |
| `c791f75567aae2e3456163272d990164e062cac4` | `tests/vfx_gpu_backend_test.cpp` | Add recording Metal RHI mock checking dispatch order and draw command contract |
| `1a0521ba3a4df37f58bd49ff614becb35889541c` | `docs/spectrafx.md` | Native usage, supported subset, RHI requirements and explicit limitations |
| `6f595a58643c95e57248e4945837797d3e9e7517` | `docs/spectrafx.md` | Document mock RHI coverage without claiming actual GPU execution |
| `2714f57d6be439f88aba36e5c19194ec18eb6461` | `skills/visuals/aiverse-spectrafx/SKILL.md` | Agent instructions for GPU prototype, fallback, invocation and testing |
| `368a5522c2d42d957d1a3ca6c11fe90bfc45e18f` | `AGENTS.md` | Preserve shared ECS/CPU contract and warn against unverified production GPU enablement |

### Public API, layout and platform behavior

```cpp
#include "engine/fx/SpectraFX.h"
aiverse::spectra::ComputeParticles emitter;
aiverse::spectra::ComputeEmitterDesc cfg; // capacity 1..4096, burst 1..64
cfg.backend = aiverse::spectra::ComputePreference::Automatic;
cfg.lifetime = 0.8f;
cfg.speed = 3.0f;
cfg.size = 0.35f;
cfg.gravityY = -9.81f;
if (!emitter.init(device, cfg)) { /* emitter.reason() */ }
// Per presentation frame: AFTER Device::beginFrame and BEFORE render pass:
emitter.setView(camera, viewProj, width, height);
emitter.burstAt({0,2,0}, {0,1,0}, {1,0.4f,0.1f,1});
if (!emitter.step(1.f / 60)) { /* timing / resource error */ }
// INSIDE open render pass:
emitter.draw();
// Shutdown before gfx::Device destruction.
emitter.shutdown();
```

- `ComputePreference`: `Automatic`, `CpuOnly`, `PreferGpu`, `RequireGpu`. `RequireGpu` returns an error on unsupported GPU or shader initialization failure; `Automatic` and `PreferGpu` fall back to the original CPU ParticleSystem. Current GPU choice is Metal only when `Device::supportsCompute()` and quality is at least Medium.
- `ComputeBackend`: `Cpu`, `Metal`; pure `chooseComputeBackend` available for native use/tests. Device pointer may be null for CPU-only headless tests.
- `ComputeEmitterDesc` supports the **narrow** additive billboard effect subset: bounded capacity/burst count, lifetime, speed, size, Y gravity, drag, quality, backend policy. `burstAt` supplies world-space position/direction/tint; this is not general graph-v1 automatic GPU execution.
- Persistent Metal storage buffers: 64-byte particle `positionAge/velocityLife/tint/size` state, 64-byte seed records, uint32 spawn slots, and 40-byte expanded GPU vertex per quad corner; static 16-bit indices and a soft RGBA8 texture. The renderer reuses existing vertex/fragment particle shaders and RHI draw/pass bindings.
- Each `step`: enqueue CPU-written seed+slot arrays as Metal blits, dispatch compute mode 0 (birth), mode 1 (age, speed, gravity/drag, position), mode 2 (expand billboard geometry), then draw without GPU→CPU readback. A CPU-side time-to-live/free-slot pool is bookkeeping only (particle motion is computed on Metal).
- GPU render currently draws the full allocated slot index array; inactive slots emit degenerate zero-alpha quads. There is **NO GPU compaction, indirect count or per-particle occlusion/culling/sorting**. Maximum 4096 slots prevents unbounded GPU work.
- All existing `fx/*.json`, `fx/graphs/*.vfx.json`, `RtsFx`, native API aliases and ECS save/rollback formats are unchanged. There is **no production RTS/FPS integration** in this stage.

### Actual verification (DO NOT OVERSTATE)

**NOT RUN:** No local AIVERSE checkout/compiler was available from the GitHub connector environment. GitHub commits succeeded, but neither native CMake build nor CTest nor Apple Metal shader compilation/Apple Silicon frame output was performed. A recording RHI mock was authored, not executed; it does not stand in for a real GPU test.

CI checked on commit `a67b0194c9b4bd5c1c53bc661ede89bf92b9fd4b`: GPU batching regression, ECS experimental checks and Studio validation were **queued**, the general CI run was **cancelled**, and the commit status endpoint contained no success statuses. No passing CI claim is authorized.

Local commands for next agent:

```bash
git switch feature/universal-entity-inspector-fps
git pull --ff-only origin feature/universal-entity-inspector-fps
cmake -S . -B build/vfx-test -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_EDITOR=ON
cmake --build build/vfx-test --target aiverse_editor aiverse_vfx_graph_tests aiverse_vfx_ecs_tests aiverse_vfx_assets_tests aiverse_vfx_ops_tests aiverse_vfx_gpu_tests -j4
ctest --test-dir build/vfx-test --output-on-failure -L SpectraFX
python3 tools/skills/catalog.py check
```

Then run a **real Metal** offscreen smoke: execute `step()` in a frame *before* `beginPass`, inspect MSL compilation output, render several consecutive bursts in one scene, capture frames and compare with CPU for correct birth/death, alpha fade, position and depth. Run the same native example with `CpuOnly` and on GLES3/Vulkan fallback. Stress repeated `clear`, resize/reinit, GPU allocation failure, GPU encoder failure, multiple frames in flight, quality tiers, and owner thread.

### Limitations and exact next stage

1. **Compilation/Metal output unverified.** Resolve real build errors, RHI shader errors and cross-frame buffer hazards before enabling the GPU path in any production project. The mock verifies only host API ordering if it compiles/runs; no GPU execution.
2. **No GPU graph execution.** `CompileParticles` (correct spelling: **`ComputeParticles`**) is a manual opt-in simple billboard emitter. Complex FX remain the CPU backend. Do not claim Niagara parity or convert graph-v1 assets to GPU automatically.
3. **Single pool and material.** Implement a reusable emitter scheduler, per-emitter storage ranges, batching by material, dead-particle compaction, scalable indirect draw, alpha sort and transparent rendering before targeting large particle counts.
4. **CPU-only backends.** Vulkan compute is not wired through the current RHI; GLSL 430 kernel text is only a placeholder. Do not pretend it runs under GL33/GLES3/WebGL2.
5. **Next bounded stage:** native compile/CTest/Apple Metal validation and fixes, then opt-in integration of `ComputeParticles` into native presentation/ECS bridge with explicit GPU eligibility/fallback for graph-v1 effects. Record actual benchmark numbers only after using GPU timing and representative scenes; update this same handoff with a separate commit after each bounded stage.

## Stage 11 — capability-routed graph runtime with lossless CPU fallback (COMMITTED, TESTS NOT RUN)

**Date:** 2026-11-18. **Branch:** `feature/universal-entity-inspector-fps`.  
**Aim:** provide a **genre-independent, opt-in** bridge from compiled SpectraFX graph-v1 effects to the Stage-10 Metal compute prototype **only** when the complete graph effect can run without silently dropping any modules. For all other graphs retain the original CPU `EffectDef`. No change to authoritative gameplay ECS, save/rollback, existing `RtsFx`, or production FPS/RTS clients.

### Implementation commits and affected paths

| SHA | Path | Change |
|---|---|---|
| `2f0365999a3423155db79573d461e91c3ea03c98` | `engine/fx/SpectraGraphRuntime.h` | Public `GraphRuntime`, options and schema eligibility |
| `22eb34d5c90e3b6954b3e8527b77fa972889e7f7` | `engine/fx/SpectraGraphRuntime.h` | Preserve wrapper ownership after GPU health recovery |
| `86ea5f3d76e53753c97c1600b131b2c4df8f996e` | `engine/fx/SpectraGraphRuntime.cpp` | Runtime selection, exact CPU graph fallback, startup/burst/update/render/clear lifecycle |
| `591030a4684782f3559c232c038f352154db1f71` | `CMakeLists.txt` | Link adapter to `aiverse_fx`, add `aiverse_vfx_graph_runtime_tests` / `vfx_graph_runtime` CTest |
| `cb059574a1a1cc576dfee90bdef409054d3d30f0` | `tests/vfx_graph_runtime_test.cpp` | Test GPU eligibility, unsupported module rejection, headless CPU mode, RequireGpu errors |
| `3b9a6bf70c6ef8b5404f2aabc619facea7a4638d` | `engine/fx/SpectraFX.h` | Expose native `GraphRuntime` API |
| `fe13539788dfa1fcab809f76c069d0068c981b56` | `engine/fx/SpectraGraphRuntime.cpp` | Explicit rejection of advanced modules and MinMaxCurve variants |
| `d2f0fe01adb36aef4140ab46b3180f1355414d4a` | `engine/fx/SpectraGraphRuntime.cpp` | Exact default white alpha fade required for GPU |
| `d4ac32e37ac8c1942a8316165df49fdbe7e502b8` | `engine/fx/SpectraGpuParticles.cpp` | Guard GPU clear against active render/compute encoder to prevent CPU-slot/desynced GPU buffer state |
| `c8c299e2e6507ac4399c2a131950f2fbffb8fa01` | `apps/editor/core/SpectraGraphOps.cpp` | `metalGpuEligible` / `metalGpuReason` in graph get/list/validate |
| `144c66d36702b6b4d8144021aac58c7faeeec072` | `apps/editor/windows/SpectraGraphWindow.cpp` | Show GPU schema eligibility and CPU fallback explanation |
| `eb036eb2e4a7f3a5a8cac8b1a0c7b209ee62ec17` | `tests/vfx_graph_ops_test.cpp` | Assert new MCP GPU eligibility diagnostics |
| `1c3399e779bca3423033b458202c7826a4344112` | `docs/spectrafx.md` | Graph runtime usage, strict support policy, RHI limitations |
| `6bb91ec35831143c780ebdcf35a7ad32be8b7c98` | `skills/visuals/aiverse-spectrafx/SKILL.md` | Agent native C++ usage and cross-genre fallback rules |
| `b7042eedd06d0a79fe824b5a9201b22f4c6b5625` | `AGENTS.md` | Add stage-11 contracts and no-unsupported-GPU claims |

### Architecture / native C++ API

```cpp
#include "engine/fx/SpectraFX.h"
aiverse::spectra::GraphRuntime view;
aiverse::spectra::GraphRuntimeOptions opts;
opts.preference = aiverse::spectra::ComputePreference::Automatic;
opts.capacity = 1024;
opts.quality = aiverse::fx::FxQuality::High;

if(!view.initGraph(device, graphJson, aiverse::Json::object(), opts)) {
    // reason() and errors(); RequireGpu never silently falls back.
}
// After gfx::Device::beginFrame, before render pass:
view.setView(camera, viewProjection, width, height);
view.burstAt(worldPosition, direction, tint);
view.step(1.f/60.f);
// Inside the existing depth-tested render pass:
view.draw(fog);
// Before gfx::Device destruction, after matching encoder scopes:
view.shutdown();
```

- `inspectGraphGpuEligibility(const fx::EffectDef&, GraphRuntimeOptions)` is a **pure schema check**, not a GPU capability probe. `GraphGpuEligibility {eligible, gpu, reason}` exposes an option-independent evaluation of the compiled graph's actual emitter modules. GPU v1 allows exactly one additive `builtin:soft` camera-facing point burst (1..64 particles), constant life/speed/size/drag, zero spread, only Y-gravity, default white alpha fade, and no other modules. The canonical `effectToJson` comparison against a known-supported template plus explicit complex-module/colour checks prevents dropping authoring data. Any additional emitter/output, shader graph, texture, gradient, noise, force field, collisions, sub-emitter, trail/light, random range, size curve, etc. rejects GPU.
- `GraphRuntime::initGraph(device,graph,overrides,options)` calls existing graph compiler; `initEffect(device,compiled,options)` accepts native-compiled `EffectDef` directly. For **eligible + Metal** graphs, the class attempts `ComputeParticles` and only reports `Metal` if its GPU init succeeds. For unsupported device, unsupported graph, shader-creation failure or low quality, `Automatic`/`PreferGpu` use the **original full** `fx::ParticleSystem`, not the Stage-10 simplified fallback emitter. `CpuOnly` explicitly uses CPU. `RequireGpu` fails with diagnostics.
- `burstAt/setView/step/draw/clear/shutdown` own one presentation runtime instance; production clients and editor preview are **not** transparently replaced. The internal Stage-10 GPU wrapper may fall back to its simplified CPU equivalent after a GPU health failure only for an already-compatible graph; `GraphRuntime` preserves the wrapper's lifetime and reports its actual backend.
- No authoritative ECS `World` changes; no new deterministic simulation component or cross-genre fork. Legacy `fx/*.json`, `fx/graphs/*.vfx.json`, `RtsFx` and existing C++ aliases are untouched.
- The graph editor and MCP `fx_graph_validate/get/list` now expose **schema** `metalGpuEligible` and `metalGpuReason`, which **do not confirm a real Metal device or successful GPU pipeline**. GUI preview deliberately stays CPU-only.

### Verification status (actual)

**NOT RUN:** the current container has no local AIVERSE checkout (`/mnt/data/AIVERSE` does not exist). A direct `git ls-remote https://github.com/chromealex/AIVERSE.git HEAD` attempt failed with `Could not resolve host: github.com`. GitHub connector writes/reads succeeded. No CMake/CTest, clang compilation, Metal shader compile, GPU render readback, FPS/RTS native-client integration, benchmarks or screenshots were run. Tests are committed **test sources only**, not passing results.

On the stage-11 AGENTS SHA `b7042eedd06d0a79fe824b5a9201b22f4c6b5625`, the GitHub combined status query returned an empty status list; four workflows (GPU batching regression, Studio validation, ECS experimental checks, general CI) were **queued**, not passed.

**Next agent must run:**

```bash
git switch feature/universal-entity-inspector-fps
git pull --ff-only origin feature/universal-entity-inspector-fps
cmake -S . -B build/vfx-test -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_EDITOR=ON
cmake --build build/vfx-test --target aiverse_editor aiverse_vfx_graph_tests aiverse_vfx_ecs_tests aiverse_vfx_assets_tests aiverse_vfx_ops_tests aiverse_vfx_gpu_tests aiverse_vfx_graph_runtime_tests -j4
ctest --test-dir build/vfx-test --output-on-failure -L SpectraFX
python3 tools/skills/catalog.py check
```

Then run actual Apple Silicon Metal shaders on a real GPU frame and capture images. Compare full CPU effects against all rejected graph features and the supported Metal subset, including alpha/depth and multiple frames-in-flight; exercise burst/clear/reinit, malformed graph/override, RequireGpu failure, RHI allocation/encoder failure and performance/quality tiers. The recording fake-RHI test from stage 10 does **not** execute real MSL.

### Known limitations and immediate next action

1. **Zero build proof.** Run/fix the native targets before asserting that the source compiles. Specifically inspect the new `effectToJson` equivalence gate, public includes, editor registrations and MSL compute/storage binding. CI queued is not CI passing.
2. **GPU v1 deliberately narrow.** No GPU compaction/indirect count/alpha sort, mesh/ribbon trails, collision, events, advanced graph dataflow or per-emitter materials. Current GPU draw submits all allocated slots (dead slots degenerate), capped at 4096. No benchmark or battery/mobile cost claim.
3. **Opt-in only.** GraphRuntime is not wired to production FPS/RTS host loops or native ECS presenter yet. The host must decide tick-boundary event delivery, attached transform resolution, GPU update scheduling before render passes, resource lifecycle and rollback presentation reset. Preserve existing CPU VFX parity until migration tests.
4. **Next bounded stage:** build and test stages 7–11 on macOS + real Metal, fix compiler and runtime defects with a **separate stage-12 implementation and handoff**, then opt-in one real native cross-genre presentation host using graph instances. Only after successful GPU stress/visual QA consider allocator compaction, indirect drawing and richer GPU graph execution.
5. Append to **this same living handoff** after each next code stage in a separate commit. Never replace or lose other agents' progress in `feature/universal-entity-inspector-fps`.

## Stage 12 — GPU sparse-range and device-failure hardening (COMMITTED, TESTS NOT RUN)

**Date:** 2026-11-18. **Branch:** `feature/universal-entity-inspector-fps`.  
**Goal:** correct two known high-risk behaviors in the Stage 10–11 Metal GPU subset — a misleading `RequireGpu` fallback after device loss and unnecessary full-capacity compute/draw work — while preserving all genre-neutral ECS/presentation and CPU paths. Add a dedicated CI compilation/regression lane. **No real Metal execution was conducted.**

### Exact implementation and validation-source commits

| Implementation SHA | Changed file | What changed |
|---|---|---|
| `ef7fa1fa969d1fb7c636105661e1ce4510b7857f` | `engine/fx/SpectraGpuParticles.h` | Stats for submitted GPU slot prefix, GPU fallback count and discarded particles; private draw-prefix state |
| `174bc522f6835ab6606b39a4e76c46dcdf2808f2` | `engine/fx/SpectraGpuParticles.cpp` | Strict `RequireGpu` failure after unhealthy device, non-seamless automatic fallback, compute/draw occupied prefix rather than full capacity |
| `edde3b227434f94616c24966675329cf0ae6fbf3` | `engine/fx/SpectraGpuParticles.h` | `ComputeParticles::ready()` exposing actual runtime-ready state |
| `279c88a770c73838d56e55bf448040dd1284f888` | `engine/fx/SpectraGraphRuntime.h` | `GraphRuntime::gpuActive()` now checks underlying emitter health |
| `e887d6b36a68b39d1495685abc096e5721bef00c` | `engine/fx/SpectraGraphRuntime.cpp` | Propagate real backend/readiness/reason after runtime failure |
| `439f8528b53cb54821fd7598a10c5e46519e00f6` | `engine/fx/SpectraGpuParticles.cpp` | Skip all GPU compute when pool empty |
| `d4c4ab4829921303daa9d1045fb5e969849184ea` | `tests/vfx_gpu_backend_test.cpp` | RHI mock: RequireGpu after device loss, explicit CPU fallback, encoder/clear safety, occupied-prefix draw count |
| `79d9e227d9482486a25cac2e33ecb1c0b7a15f25` | `tests/vfx_gpu_backend_test.cpp` | Additional staggered births/expiry across multiple frames with holes in the GPU pool |
| `cd24d96603d27807129975428bfc5443575b9760` | `.github/workflows/spectrafx-validation.yml` | PR/manual Linux+macOS focused CTest, Linux editor compile + MCP graph CTest |
| `d14f2c496fc91cf07f44c4f6d54b2da6788d3362` | `docs/spectrafx.md` | Stage-12 public API/telemetry/lifecycle/CI documentation |
| `87915882fc95bfde2f2683bdf6be1722bd736c29` | `skills/visuals/aiverse-spectrafx/SKILL.md` | Agent protocol for sparse GPU draws, RequireGpu and failure reporting |
| `af80404c23e056debe7a8b07853770b8eedcfd13` | `AGENTS.md` | Stage-12 genre-neutral GPU lifecycle and no-false-claims rules |

### API, data flow, platform and compatibility

- `ComputeStats` adds `submittedSlots` (last draw-prefix capacity, including dead holes), `gpuFallbacks` and `lostOnFallback` (discarded GPU-resident particles if the GPU fails and the wrapper attempts an automatic CPU migration). Previous stats remain source compatible. `ComputeParticles::ready()` exposes the readiness bit; `gpuActive()` still requires Metal.
- Prior GPU integration/update/draw dispatches across the entire allocated `capacity`, even with one active particle. The new implementation computes the greatest occupied slot index from bounded CPU-side ages and uses that **prefix** for integrate/expand and indexed draw calls. New births are queued as before. Empty pool + no births skips all dispatches and draws.
- **Not stream compaction**: inactive slots inside the prefix still produce degenerate zero-alpha billboards; when three live particles occupy slots 3–5, `alive == 3` while `submittedSlots == 6`. This bounded optimization does not change shader, storage-buffer layout, CMake symbol, graph JSON or native ECS identity.
- `RequireGpu`: if `Device::gpuHealthy()` reports failure, `step` returns **false**, the emitter is marked not ready, its backend is not misreported as CPU, and `reason()` explains the forbidden fallback. The host must recreate/reinitialize the device as appropriate.
- `Automatic` / `PreferGpu`: runtime GPU-health failure attempts to create a CPU emitter, increments `gpuFallbacks`, increments `lostOnFallback` by the affected live count, and returns **false** for the transition frame, rather than falsely declaring seamless rendering. **An unhealthy graphics device can also make a CPU renderer unusable**: CPU fallback initialization alone is *not* proof of recovered visuals.
- `GraphRuntime::step` now copies the contained emitter's actual backend, readiness and error; `GraphRuntime::gpuActive()` also checks contained `ComputeParticles::gpuActive()`. There is no automatic FPS/RTS client replacement or mutation of authoritative ECS state.
- `clear()` inside an active render/compute encoder remains refused without altering slot bookkeeping, and outside the encoder clears `submittedSlots`. `shutdown()` resets these fields.
- Added `.github/workflows/spectrafx-validation.yml`, triggered on relevant PR changes and manual dispatch. Linux/macOS build the five standalone SpectraFX C++/RHI mock regression targets and run `ctest -L SpectraFX`; Linux also compiles the editor and runs `vfx_graph_ops`. This CI workflow **does not** execute Metal shaders on a GPU or validate Apple Silicon visuals.

### Actual tests / CI record — NOT RUN

No native AIVERSE checkout exists in this tool container (`/mnt/data/AIVERSE` absent), so there was **no actual CMake configure, build, CTest, Metal shader compilation, screenshot capture or performance benchmark**. GitHub API/connector writes succeeded and remote workflow metadata was queried.

On latest Stage-12 AGENTS commit `af80404c23e056debe7a8b07853770b8eedcfd13`, the GitHub workflow query showed **five queued** jobs/workflows: `GPU batching focused regression`, `ECS 2.0 experimental checks`, `SpectraFX validation`, `CI` and `AIVERSE Studio validation`. The combined status endpoint returned **no successful statuses**. Therefore mark nothing as PASS.

Commands for local AIVERSE agent:

```bash
git switch feature/universal-entity-inspector-fps
git pull --ff-only origin feature/universal-entity-inspector-fps
cmake -S . -B build/vfx-test -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_EDITOR=ON
cmake --build build/vfx-test --target aiverse_editor aiverse_vfx_graph_tests aiverse_vfx_ecs_tests aiverse_vfx_assets_tests aiverse_vfx_ops_tests aiverse_vfx_gpu_tests aiverse_vfx_graph_runtime_tests -j4
ctest --test-dir build/vfx-test --output-on-failure -L SpectraFX
python3 tools/skills/catalog.py check
```

**Real Metal QA must follow:** start device frame, initialize an eligible SpectraFX graph, spawn from many locations across multiple frames, simulate, begin render pass, draw, inspect output pixels/screenshot, verify shader compile logs and GPU command ordering, test draw range and sparse holes, `RequireGpu` failure without CPU transition, automatic fallback and full device recovery, and compare with CPU quality/visuals. Run on an actual Apple Silicon GPU and track upload bytes/draw calls/GPU timings.

### Known limitations and exact next action

1. **Build status still unknown.** The new CI lane exists and was queued, but this stage did not see a completion. Its source changes are not compile-verified; next agent should fetch actual CI logs and fix real compiler/test failures first.
2. **GPU prototype remains narrow:** one additive soft-sprite emitter, no true GPU compaction, indirect count, scene depth collision, shader graph/dataflow nodes, multiple textures, ribbon/mesh effects or GPU emitter event channels. Vulkan/GL/GLES/WebGL remain CPU fallback. The prefix optimization is O(capacity) CPU age bookkeeping and can still draw holes.
3. **Runtime recovery is not seamless.** GPU-resident particles are lost on an automatic transition and `step` reports `false`; if `gpuHealthy` is false, a separate graphics-device reinitialization may still be necessary. Do not treat fallback as proof that graphics recovered.
4. **Next bounded stage (Stage 13):** observe/fix CMake+CTest and actual Metal diagnostics on Apple Silicon; implement a real offscreen Metal compute+draw smoke test and safe graphics-device recovery before considering default GPU effect routing. Once verified, design GPU compaction/indirect buffers and a genre-neutral ECS presentation scheduler that can route multiple graph emitters in native projects. Each implementation stage must end with another **separate** handoff commit here with actual SHAs/test results and explicit limits.

## Stage 13 — dense Metal particle gather with CPU-owned live index map (COMMITTED, TESTS NOT RUN)

**Date:** 2026-11-18. **Branch:** `feature/universal-entity-inspector-fps`.  
**Goal:** eliminate dead-slot billboards and sparse-index draw overhead in the existing opt-in SpectraFX Metal compute subset. Use a CPU-published live-index map and GPU-resident vertex gather; do not overstate this as fully GPU-autonomous compaction. No ECS/gameplay/save/rollback or RTS/FPS production client changes.

### Implementation commits

| Exact implementation SHA | File | Changes |
|---|---|---|
| `a1e8a6b2c9718c7360fc633b91a2143d1fd99a60` | `engine/fx/SpectraGpuShaders.h` | Both MSL and future GLSL compute sources gather only live physical slots into dense billboard vertices using storage buffer 4 and `meta.w` alive count |
| `09cf4b2c4cdbe8603a476e49def8df2fac7ad44e` | `engine/fx/SpectraGpuParticles.h` | Persistent `liveSlots_` GPU buffer, CPU-side `aliveSlots_` list and dense `drawCount_`; update stats semantics |
| `fb8d3a97a65cb700c21fab2a44e0aeecc979f687` | `engine/fx/SpectraGpuParticles.cpp` | Create/destroy new storage buffer, reserve live-slot list, include allocation in GPU-memory budget, reset on clear/shutdown |
| `f1aed98bd0ab40655a62ee8605e7d143260cafce` | `engine/fx/SpectraGpuParticles.cpp` | Update particle ages after compute submission; use CPU predicted survivors to upload slot map, dense compute expand and exact-live indexed draw |
| `e5816eeadc5e801cde330b2f4723b042eaae79cd` | `tests/vfx_gpu_backend_test.cpp` | Recording-RHI captures live-slot map and asserts dense draw of the 3 surviving physical slots [3,4,5] |
| `b04c9c3436caa88b1c2e084334c51473f5a152d7` | `docs/spectrafx.md` | Stage-13 design, platform limitations, test scope and latency/bandwidth caveats |
| `d30d1c217e6a5f475d945890fce77da501f9d546` | `skills/visuals/aiverse-spectrafx/SKILL.md` | Canonical AI agent implementation/usage/validation guidance |
| `6f53180d965f2388884ab842053c7891f66d480c` | `AGENTS.md` | Stage-13 no-false-claims and CPU-preservation engineering rule |

### Architecture and source/ABI contracts

- Existing stable physical GPU state slots remain `ParticleState` (64 bytes). GPU quad `Vertex` remains 40 bytes and retains its existing sprite shader/pipeline layout. Capacity still capped at **4096** slots (16-bit indices). One new `BufferType::Storage` holds `uint32_t liveSlots[capacity]`, bound at **compute slot 4**.
- At each presentation frame, CPU scans its bounded lifespan/free-slot records, predicts the post-step survivors (`ages[i] > dt`), builds an ascending physical-slot map and uploads that map (4 bytes per live particle) along with any new birth data **before beginCompute**. Do not mutate CPU ages or free slots if the compute encoder cannot begin.
- GPU pass 0 writes births into physical state slots; pass 1 integrates physical slots through the previously-live high-water index; pass 2 reads `liveSlots[id]`, gathers resident GPU position/velocity/tint and writes to **dense** `vertices[id*4..id*4+3]`. Uniform `meta.w` supplies the live count; both Metal and GLSL source agree. No GPU→CPU position readback is required.
- The host then draws `drawCount * 6` indices from the preexisting 16-bit static index buffer; `drawCount == alive` after a successful GPU step. Dead holes in the physical pool are **never** submitted as vertex quads. The Stage-12 `submittedSlots` field now reports actual densely emitted quads (previously it was a high-water prefix, possibly including holes). Update consumers accordingly.
- `ComputeStats::uploadedBytes` now additionally accounts for the 4-byte-per-survivor per-frame map. The GPU buffer memory budget includes the added 4-byte-per-capacity buffer. Idle pools still skip compute/draw. Normal graphic output remains one additive `builtin:soft` particle material with no alpha sort and no cross-emitter batching.
- Legacy `fx::ParticleSystem` CPU backend remains the default for production RTS/FPS clients. `GraphRuntime` still only opts eligible graphs into this Metal subset. Other graphs, platforms and quality tiers preserve the full original CPU effect. ECS deterministic state and old graph assets are not touched.

### Test and CI status — NOT RUN

The recording-RHI test was **updated, not executed**: it asserts that after two groups of three births, when physical slots 0–2 expire and 3–5 live, the GPU slot map equals `{3,4,5}` and the renderer issues exactly **18 indices**, not 36. Other scenarios still cover empty-pool no-dispatch, clear safety, `RequireGpu` device health, auto fallback and detailed diagnostics. The `.github/workflows/spectrafx-validation.yml` CI workflow still exists but at the last query its jobs were **queued**; no successful build/CTest result was available for this stage. No MSL compiler or actual Metal device was used. Therefore **source committed ≠ tested** and no performance measurements are claimed.

To validate in a full native checkout:

```bash
git switch feature/universal-entity-inspector-fps
git pull --ff-only origin feature/universal-entity-inspector-fps
cmake -S . -B build/vfx-test -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_EDITOR=ON
cmake --build build/vfx-test --target aiverse_editor aiverse_vfx_gpu_tests aiverse_vfx_graph_runtime_tests aiverse_vfx_graph_tests aiverse_vfx_assets_tests aiverse_vfx_ecs_tests aiverse_vfx_ops_tests -j4
ctest --test-dir build/vfx-test --output-on-failure -L SpectraFX
python3 tools/skills/catalog.py check
```

Then perform real Apple Silicon validation: use a Metal `gfx::Device` to compile `cs_main`; render after an initial burst, spawn a second group later, let the first expire, and verify exactly the expected visible second group (no holes/ghost quads). Inspect actual GPU buffer(4) values and `meta.w` in a GPU frame capture; measure uploads, dispatches, draw counts and frame time at 128/1024/4096 live particles. Compare visual FPS/RTS-agnostic example effects with CPU fallback. Test multiple in-flight frames and device reset before considering default enablement.

### Known limitations and next bounded stage

1. **No CI/real device proof** yet: fix any actual compiler, CTest and Metal shader issues in these source changes before extending the compute path. CI queued does not mean passing.
2. **CPU-published gather is not autonomous GPU compaction.** The CPU still scans lifetime/slots O(capacity), uploads O(alive) 32-bit indices, computes draw count and submits `drawIndexed` with an alive CPU value. GPU owns particle motion and vertex expansion only. Real GPU scan/atomic compaction + GPU indirect arguments is a separate, higher-risk stage.
3. The single Metal soft additive sprite emitter remains restricted; no GPU ribbons/meshes, depth collisions, sorting/alpha blending, multi-material/render-queue batching, sub-emitter events, scalable pooled effects or generic Niagara dataflow. Vulkan/GL/GLES/WebGL remain CPU fallback.
4. **Next stage (Stage 14):** first obtain real Linux/macOS CI and Mac Metal render/test logs. If green, design an opt-in **GPU-authored alive counter + indirect indexed draw** behind an explicit RHI capability gate and preserve the CPU list path as fallback; then build a genre-neutral multi-emitter presentation scheduler with correct rollback reset, budgeting and visual parity. Do not silently alter existing clients.
5. After every next code stage, append a separately committed handoff to **this same file** with exact SHA, tests actually executed and next work. Concurrent agents continue working in this branch; use blob-SHA-safe updates.
