# SpectraFX Stage 108 — two-camera GPU alpha depth sort index parity

Date: 2026-11-20  
Branch: `feature/spectrafx-generic-cue-queue`  
Implementation commit: `9468a46297abbdd846e8a98ccf1df371bba95f17`  
Native accepted baseline: Stage 105 source `c4fc948535557f1cc1a00459d1a3cd48257ad6ca`, 126/126 full CTest and 4/4 MetalGPU PASS on Apple M3 Ultra. **Stage 106–108 not yet native tested.**

## New real Metal acceptance

Extended `tests/spectrafx_metal_rhi.mm` with a real compute + RHI graphics probe for two opposite camera directions, without any CPU readback of live particle positions:

1. Instantiate two GPU-owned particles at z=+0.5 and z=-0.5 and initialize `SpectraFxGpuAlphaSort(2)`, output RGBA32F position map after `SpectraFxGpuEvolution::step(0)`.
2. Call `sort.prepare(particles,eye,{0,0,1})` and `sort.preparedFor`, then execute a *test-only* fullscreen GPU shader that samples pixel (0,0) of `sortedIndexTexture()` into offscreen RGBA8 color A (slot 0 encoded ~51 red).
3. End pass, repeat `sort.prepare` for the same particles with camera forward `{0,0,-1}`, then encode and draw color B (slot 1 encoded ~230 red). This deliberately reuses sorted texture scratch within one Metal frame and depends on correct ordering of compute and render encoders.
4. Complete frame; use only the already-defined diagnostic `gfx::Device::readColorRGBA8` **after** the frame to assert first-index reversal (A red in 35..85, B in 195..255). Prints `[spectrafx-metal-rhi] alpha_views_first=A/B`.
5. Fence output through completed frame readback, release scratch, shaders, buffers, pipelines and GPU evolution resources before the shared compute program.

The shader is a **diagnostic index visualizer only**; the actual sort compute pipeline, sorted-index texture, RHI command ordering, GPU evolution and offscreen draw path are the production APIs. No genre modules or Editor runtime are linked.

## Coverage boundaries

- This verifies the **sorted-index map** changes per camera direction; it does **not** prove perceptual alpha-blend correctness for overlapping particles with differing per-particle colors, since the production GPU billboard renderer currently has uniform emitter color.
- The test uses one GPU frame with sequential camera render passes, exactly as required by `HybridPlayback::prepareRenderView`. Camera B sorting must occur **after** pass A ends.
- Does not modify `engine/fx` contracts or serialization; Vulkan/GLES GPU compute remain unavailable/fallback.
- No performance claims. The dedicated stress work remains next.

## Actual verification

Fetched GitHub production `SpectraFxGpuAlphaSort.h`, renderer and current real Metal RHI test, authored a uniquely anchored test addition and committed/pushed. **No native macOS build, CTest, Metal render, pixel extraction or GPU health verification has occurred on the new source.** Stage 105 PASS is a previous SHA only.

## Next action

Complete GPU-safe resource/load stress tests and perform one final strict all-target acceptance using `AIVERSE_ACCEPTANCE_REQUIRE_METAL=1`. Review console lines `alpha_views_first`, `prisma_hdr`, stage summary and optional images. If native Metal shader compilation or index-order fails, fix the actual code/diagnostic with evidence; preserve handoff and tested SHA. Do not merge to `main` before tested and conflict-reviewed.
