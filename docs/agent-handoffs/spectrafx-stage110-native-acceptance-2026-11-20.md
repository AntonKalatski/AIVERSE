# SpectraFX Stage 110 — strict full Apple M3 Ultra acceptance for Stages 106–109

**Recorded:** 2026-11-20
**Branch:** `feature/spectrafx-generic-cue-queue`
**Validated engine source SHA:** `b06c6d0f4dc820455d9c7b25dc98aa33397adca7`
**Changes in Stage 110:** documentation/evidence only; no engine source, shaders, CMake or acceptance script modified.

## Actual native verification (user-provided Mac Studio log)

`Darwin`, `Apple M3 Ultra`, build at
`/Users/aleksandrfeer/Projects/AIVERSE-spectrafx-validation/build/spectrafx-validation`.

| Acceptance stage | Exit status |
|---|---:|
| configure | 0 |
| spectrafx_core_build | 0 |
| architecture | 0 |
| host_integration_build | 0 |
| full_build | 0 |
| ctest_full | 0 |
| ctest_spectrafx | 0 |
| ctest_metal | 0 |
| metal_evidence | 0 |

**Failed stages: 0, skipped stages: 0.**
`ctest_full`: **126/126 PASS** (wall 858.65 s).
`ctest_spectrafx`: **11/11 PASS** (wall 10.12 s), including `spectrafx_retirement` (0.27 s), `spectrafx_metal_rhi` (4.83 s) and `spectrafx_metal_scene` (2.04 s).
`ctest_metal`: **4/4 PASS** (wall 5.15 s); strict verifier:
`MetalGPU real PASS=4/4 SKIP=0 MISSING=0 FAIL/UNKNOWN=0`.

### Real RHI, GPU load and multi-camera sorting

From the verbose Metal run:

```
[spectrafx-metal-rhi] device=Metal Apple M3 Ultra
[spectrafx-metal-rhi] A left/right=229/0 B left/right=0/228
[spectrafx-metal-rhi] churn_frames=512 capacity=256 peak_nominal_kib=36 retire_remaining=0 gpu_healthy=1
[spectrafx-metal-rhi] alpha_views_first=51/229 expected~51/230
[spectrafx-metal-rhi] impact_queued=1 impact_discarded=1 delivered=0
PASS [spectrafx-metal-rhi] production RHI offscreen cameras, pixel parity, compute->draw and 3-frame resource retirement
```

The 512-frame/256-capacity stress mode really executed successfully on hardware. The `36 KiB` reading is **nominal outstanding payload under the test's accounting**, not real GPU-resident memory or Metal heap usage. The index probe demonstrates opposite view forward directions produce different GPU-sort orders; it is not a per-particle-colored transparency correctness comparison.

### Real Renderer3D, PRISMA postprocessing and depth

```
[spectrafx-metal-scene] device=Metal Apple M3 Ultra
[spectrafx-metal-scene] A pixels=168 center=63.50 B pixels=304 center=43.07 depth_center_delta=0
[spectrafx-metal-scene] prisma_hdr=1 passes=10 fx_pixels=232 max_red_delta=185 depth_delta=-1
[spectrafx-metal-scene] failures=0
PASS [spectrafx-metal-scene] production Renderer3D depth, two camera RTs, SceneHost and reset retirement
```

Real device output confirms that particles were rendered in two camera render targets, physically occluded in the non-postprocessed scene, and contributed to a ten-pass PRISMA HDR/Bloom effect chain. Small bloom energy at the center after postprocessing is expected and does not imply depth-test failure.

### Render image evidence (not visually reviewed)

Seven top-down PPM artifacts were generated **locally on the user's Mac**, not uploaded to the conversation:

- `/tmp/spectrafx-scene-baseline-a.ppm`
- `/tmp/spectrafx-scene-effect-a.ppm`
- `/tmp/spectrafx-scene-baseline-b.ppm`
- `/tmp/spectrafx-scene-effect-b.ppm`
- `/tmp/spectrafx-scene-after-reset-a.ppm`
- `/tmp/spectrafx-scene-prisma-baseline-a.ppm`
- `/tmp/spectrafx-scene-prisma-effect-a.ppm`

Automated visual pixels passed, but no independent human/agent visual inspection or screenshot diff of those files has been done here.

## Production API and ownership

No source changes in this stage. Verified tested source includes:
- `SpectraFxSceneHost::prepareGpu(dt, view)` + `prepareCpu(dt, view)` once/frame, `prepareRenderView(view)` before each camera's render pass, no genre-specific dependencies in `engine/fx`.
- Editor preview camera+RT packet authored in Stage 106 (compiled as part of `host_integration_build` and `full_build` but its actual GUI interaction was **not** exercised by the pasted tests).
- PRISMA owns HDR/Bloom/post chain, SpectraFX draws in the open HDR scene pass prior to `Renderer3D::resolvePostFx()`.
- Metal actual alpha sort/render-to-texture and GPU impact feedback generation invalidation remain presentation-only.
- `spectrafx_retirement` deterministic production HybridPlayback regression passed as part of the focused 11/11 suite. The pasted verbose report does not show its `peak_rhi_handles`, `peak_slots` or CPU `wall_ms` values; retrieve `ctest_full.log`/verbose run to report those quantities.

## Remaining steps

1. Inspect the seven PPM screenshots and carry out real Editor Preview mouse orbit, pause/step/reset, authored asset hot reload and resize interactions (outside native unit tests).
2. Measure GPU residency and heap/driver allocation lifetime via Xcode Instruments/Metal API Validation, and record peak RSS/Metal memory and GPU time across the extended stress configuration.
3. Compare correct view-dependent overlapping **alpha blending** (not just index sorting) and PRISMA composition under camera changes and real editor UI.
4. Verify non-Metal GLES/Vulkan CPU fallback with platform builds when those platform workstreams are ready.
5. Review conflicts with highly divergent `main` and other AIVERSE branches before merging. Do not claim fully production-ready solely from these tests.

The full run's log directory on the user machine:
`build/spectrafx-validation/spectrafx-acceptance/`.
