# AIVERSE render batching + GTA traffic-signal handoff — 2026-11-17

Branch: `fix/indirect-vulkan-base-vertex-safety` (PR #4, based on `feature/universal-model-batching-policy` PR #3). Starting commit: `a8bfb0db41f1611a81ec0548c896803ad79707ef`.

## User goals
1. All traffic-light housings should use identical GPU geometry/material state for batching. Switching green/yellow/red must affect only the illuminated lens, **not the base housing mesh colour or draw-batching identity**.
2. Improve overall render performance, benchmark before claiming a win.
3. **Save every completed step here** for another agent.

## Step 1 — measured baseline (DONE; user-provided macOS Metal A/B)
- Scene geometry pool: 95 pages, 884 models, 336.9 / 340.0 MiB, 99.1% utilization.
- MDI OFF vs ON: 110 -> 68 total GPU API submissions (-42); 73 -> 52 main-model submissions (-21).
- CPU Scene render OFF avg 0.738 ms, P95 0.788; ON avg 0.793 ms, P95 0.850; MDI CPU **+7.4% slower**.
- Full GPU frame OFF avg 0.354 ms, P95 0.513; ON avg 0.386 ms, P95 0.539 (GPU cost **+9.0%**, note timings are asynchronous and the benchmark sampled each mode for 900 frames).
- ON: ~4.0 indirect submissions / 46.0 commands per frame; CPU MDI available. OFF: candidate diagnostics all 0 by design.
- Previous screenshot MDI ON: 53 candidate scans, 47 ineligible scans, 5 material state splits, 1 geometry-buffer split, 0 flags/pose, 0 submission failures; sample counters may represent repeated passes, not distinct objects.
- Conclusion: **do not force MDI just for draw-call count**; adaptive selection or batching overhead reduction required, preserve instancing fallback.

## Step 2 — traffic light discovery (IN PROGRESS)
- GTA project has `assets_src/traffic-signals/README.md`, and generated three phase-specific models with the same geometry but different active emissive lens.
- `assets_src/_pipeline/kingston_traffic.py` and `assets_src/resident-traffic/source/install.py` define light_green, light_yellow, light_red types.
- Need inspect actual `assets_src/traffic-signals/source/install.py` and rendering/view path to select safe representation: ideally single base mesh and per-instance emissive state with stable pipeline, not three separate GLB GPU objects.

## Next
- Inspect GTA signal assets and type switching; identify stable material path and shared model geometry.
- Implement in correct repository/branch with tests, benchmark CPU/GPU and pixel-equivalence.
- Append new steps, exact commits and any known blockers after each meaningful change; avoid merging to main before validation.
