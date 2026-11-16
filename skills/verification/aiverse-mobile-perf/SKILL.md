---
name: aiverse-mobile-perf
description: Keep AIVERSE games fast on weak phones - frame and tick budgets, post-effect cost, LOD and geometry, Lua and simulation cost, measured benchmarks and honest reporting.
---

# Mobile performance

Budgets: simulation tick at 20 Hz has 50 ms for everything on phones (sim + render share a thread); Lua <= 3-5 ms per tick; frame target 33.3 ms (30 FPS) on mid-range Android.

1. Post effects: `postfx_estimate` and the `warnings` (ms per frame) of `postfx_*` answers; `postfx_preset mobile` as the base for phones.
2. Geometry: autolod and HLOD (`docs/autolod.md`, `docs/geometry.md`, `docs/geometry-performance.md`); large worlds `docs/large-worlds.md`.
3. Lighting: baked or probe GI instead of many realtime lights (`skills/visuals/aiverse-shader-graph`); shadow quality per device.
4. Simulation: unit counts, pathing load, Lua work every tick (`docs/lua-coverage.md` table); measure with headless matches and CPU time, not wall time on a busy machine.
5. Budgets in code: `skills/verification/aiverse-performance-budgets` (p95, peaks, baseline regressions).
6. Rendering benchmarks: `docs/render-benchmarks.md`, results in `docs/benchmarks/`.
6a. Start-up time: the client defers unit models and sprite sheets until a map is chosen
   (`docs/startup-loading.md`, test `aiverse_deferred_loading_tests`); Metal shader precompile with
   `tools/metal/precompile.py`.
7. Report only measured numbers with device, build and scene; a target is not a result.

Runtime library: none. Workflow over engine budgets and benchmarks.
