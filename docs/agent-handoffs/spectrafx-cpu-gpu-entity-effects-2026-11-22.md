# SpectraFX handoff 2026-11-22: one CPU/GPU path, effects declared in data

Branch `agent-spectra-next` (worktree on KINGSTON `/Volumes/KINGSTON/AIVERSE-builds/worktrees/agent-spectra-next`).
Reference: `docs/spectrafx-entity-effects.md`.

## Done

1. **CPU twin of the GPU kernel.** `engine/fx/SpectraFxEvolutionKernel.h` mirrors `kSpectraFxComputeMetal` line for
   line. `SpectraFxGpuEvolution` has a `cpuTwin` mode (CPU arrays, packed live-only upload into a 3-texture RGBA32F
   ring, back-to-front for alpha, ground-hit events without readback). `SpectraFxHybridEmitter` runs the same
   `fx/gpu/*.gpu.json` asset on the twin when the GPU is not selected (no compute, `"preference":"cpu"`, failed pool,
   `AIVERSE_SPECTRAFX_CPU=1` via `HybridPlayback::setForceCpuTwin`). `cpuEffect` became optional
   (`"cpuFallback": "twin"` default, `"effect"` legacy). The billboard renderer no longer requires compute.
   Metal rendering stays readback-free (compute writes the texture the vertex shader samples).
2. **Shared data-driven entity effects.** `engine/fx/SpectraFxEntityEffects.h` (table + presenter). Hosts:
   - RTS client `apps/client/ClientFx` (`configureEntityEffects` called from `GameClient::startMatch`): scene objects +
     sim events (died/attack/hit/ability) -> presenter; inline rules on rules `types`, their `weapons`, `abilities`.
   - FPS client `apps/fps/client.cpp` via `SpectraFxSceneHost::configureEntityEffects` + `presentEntityEffects`:
     characters, vehicles, projectiles; inline rules on `fps.weapons[i]`.
   - `SpectraFxSceneHost` now enables itself when a project declares bindings even without `fx/gpu/routes.json`.
3. **Templates (data only):** `templates/skirmish` (rules `spectraFx`, inline on townhall/catapult/tower weapon,
   `fx/gpu/*.gpu.json`, `fx/wheel_dust.json`, new map `maps/fx_showcase.json`), `templates/fps_arena`
   (rules `spectraFx`, inline on rifle, `fx/gpu/*`, `fx/death_burst.json`, `fx/dust_kick.json`).

## Verified on the Mac (Metal, shared GPU)

- CTest: `spectrafx_cpu_gpu_parity`, `spectrafx_entity_effects`, `spectrafx_retirement`, `spectrafx_contract`,
  `spectrafx_metal_rhi`, `spectrafx_metal_scene`, `spectrafx_metal_gpu`, `spectrafx_metal_billboard`,
  `vfx_gpu_backend` passed.
- Kernel parity 4096 particles x 180 frames: identical alive sets, pos error p50 3e-8 m, p99 6e-7 m, max 1.6e-6 m.
  Emitter parity (same asset GPU vs forced twin, 90 frames): identical live counts every frame, images max delta 0.
- Perf (median of 110 frames, 3 reps, two separate runs; GPU = whole-frame GPU time of a frame that only runs the
  dispatch): 65536 particles CPU twin 0.39-1.0 ms (66k-168k particles/ms), GPU 0.022-0.024 ms (2.7M-3.0M
  particles/ms); 4096: CPU 0.029 ms vs GPU 0.014 ms.
- Hosts with `AIVERSE_FX_DEBUG=1`: RTS client and FPS client run the presenter from template data; with
  `AIVERSE_SPECTRAFX_CPU=1` the same emitters report `cpuTwin` instead of `gpu`.

## Not verified / open

- Editor in-process RTS play (`apps/editor`, `gameFxBridge_`) does not feed the presenter yet; only the clients do.
- Legacy RTS `fx/fx.json` (`RtsFx`) still runs beside the new mechanism (genre-specific debt; migrate its cue table
  onto `SpectraFxEntityEffects` and remove `RtsFx`).
- Planet RTS maps skip entity effects (GPU billboards use flat world space).
- CPU twin covers the GPU emitter subset (billboards, gravity, damping, ground plane); graph-v2 / mesh / ribbon
  effects still use `ParticleSystem` (`fx/<name>.json`).
- GL/GLES/WebGL/Vulkan twin rendering is not run on device (Metal with forced twin only); the GLSL billboard
  shader existed before and is unchanged.
