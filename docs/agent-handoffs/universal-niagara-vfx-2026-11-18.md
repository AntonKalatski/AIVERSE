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
