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

## Step 2 — GTA traffic light diagnosis (DONE)
- GTA installer previously wrote three phase-specific files (`red.glb`, `yellow.glb`, `green.glb`) with identical mesh geometry but different active material colors. This caused distinct GPU VB/IB handles and prevented cross-phase model batching despite identical housings.
- AIVERSE already supports per-type `view.materials` named overrides and shared model caching (RtsLook::configureChoice), so we can share one source model without modifying ECS/simulation phase logic.

## Step 3 — shared-model traffic-light fix (IMPLEMENTED, awaiting runtime tests)
- Separate GTA branch: `fix/shared-traffic-signal-model-batching` and draft PR https://github.com/chromealex/gta/pull/4
- GTA commits: `99b792d` shared GLB generator & phase-only emissive material overrides; `c091dd2` geometry/material alignment regression tests; `e5ee4f2` migration docs.
- New `models/traffic-signals/signal.glb` has shared pole/head/lens meshes. Each traffic-signal rules type overrides only its active `red_lens`, `yellow_lens`, or `green_lens` material and retains per-lens spotlight offset.
- Installer updates `streamedWorld.assets` and removes obsolete three old GLB hash entries, but must be re-run locally to generate binary asset and update rules; pre-existing unused binary files may remain.
- NOT YET verified with native render, installed model, traffic light simulation, or macOS benchmark. Keep PR draft until tests and visual check.

## Step 4 — Metal CPU-MDI performance and diagnostics (IMPLEMENTED, awaiting benchmarks)
- Commit `6cbc40a`: validated CPU indirect-layout cache is now enabled by default, opt out using `AIVERSE_MDI_LAYOUT_CACHE=off` or `0`. Metal CPU MDI command threshold raised from 8 to 16, override using `AIVERSE_METAL_MDI_MIN_COMMANDS=2..32`. Motivation: user measured ~7.4% CPU/9% full GPU regression even with fewer draw calls; this is a conservative performance hypothesis, NOT a measured win.
- Commits `58e3057` and `04f4b32`: count and display mutually exclusive first-failure reasons for ineligible MDI candidates: geometry clusters, GI/baked, material batch/shadow roles, deform/skin/morph, alpha/fade.
- TODO validate with fixed-camera Metal A/B before treating these as optimisations; confirm no regressions on Vulkan/GL.

## Step 5 — viewport camera triangle counter (IMPLEMENTED, awaiting build and UI test)
- User requested triangle count inside Scene → Batching window.
- Commit `db65aee`: added three 64-bit RenderProfile counters: `cameraModelTriangles`, `cameraMeshTriangles`, `cameraOverlayTriangles`.
- Commit `527b5b1`: in `Renderer3D::render`, count only first opaque main-camera model pass (not reflection or repeated SSR capture) and blended pass using per-phase `profile_.triangles` deltas; also count regular immediate Mesh draw indices and triangle overlays. `beginScene()` already resets profile each scene.
- Commit `42d2548`: Scene Batching shows `Triangles in camera (tracked)`, broken down into Models / Meshes / Overlays and explains limitations.
- Semantics: **submitted triangle indices / 3, including instances** for currently drawn camera geometry. Not the number of rasterized/visible fragments or GPU-late culled primitives. Excludes shadows, reflections, repeated SSR colour pass, outlines/depth-only model passes, custom renderers such as terrain/sprites. Custom draw APIs do not expose universal triangle counts currently. This is a precise scoped tracked counter, NOT a universal total GPU triangles figure.
- Tests pending: compile `aiverse_editor` on Apple Silicon, run Scene, verify changing camera updates counts, check SSR/reflection doesn't double count and MDI OFF/ON yields matching numbers for same view. Run native tests/CI if available.

## Next
- Build editor and test newest branch on Apple Silicon; verify camera triangle counters with movable camera + fixed camera A/B.
- Re-run GTA signal installer and tests, then native client and actual Metal lighting/traffic signal changes; keep GTA draft PR #4.
- Benchmark Metal CPU MDI threshold 8 vs 16 (and cache off/on) with controlled camera; update findings before merging.
- Append exact commit/test results for another agent after **each step**.

- Inspect GTA signal assets and type switching; identify stable material path and shared model geometry.
- Implement in correct repository/branch with tests, benchmark CPU/GPU and pixel-equivalence.
- Append new steps, exact commits and any known blockers after each meaningful change; avoid merging to main before validation.
