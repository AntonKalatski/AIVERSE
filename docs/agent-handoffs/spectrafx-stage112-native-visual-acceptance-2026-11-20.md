# SpectraFX Stage 112 — Stage 111 full native and visual acceptance PASS

Recorded: 2026-11-20  
Branch: `feature/spectrafx-generic-cue-queue`  
**Validated source SHA:** `441d69e08836888604e0aee6ec71765a51fddddc`  
This stage: **documentation-only** record of user-provided hardware report; no engine, shader or test code changed.

## Reported full native acceptance — Apple M3 Ultra, Darwin

User supplied `tools/spectrafx/acceptance.sh` summary from:
`/Users/aleksandrfeer/Projects/AIVERSE-spectrafx-validation/build/spectrafx-validation`.

| Stage | Exit |
| --- | ---: |
| configure | 0 |
| spectrafx_core_build | 0 |
| architecture | 0 |
| host_integration_build | 0 |
| full_build | 0 |
| ctest_full | 0 |
| ctest_spectrafx | 0 |
| ctest_metal | 0 |
| metal_evidence | 0 |
| visual_review | 0 |

**Failed stages 0, skipped stages 0**. Explicit `metal_evidence`:
`MetalGPU real PASS=4/4 SKIP=0 MISSING=0 FAIL/UNKNOWN=0`.
Tests listed PASS:
`spectrafx_metal_gpu`,
`spectrafx_metal_billboard`,
`spectrafx_metal_rhi`,
`spectrafx_metal_scene`.

The pasted report shows `ctest_full`, `ctest_spectrafx` and
`ctest_metal` as successful stages, but **does not include the count
for full CTest nor the full list of individual focused tests**.
Do not attribute the earlier 126/126 count to this SHA without inspecting
the current full log.

## Stage 111 seven-frame objective visual analysis

```
[spectrafx-visual] contact_sheet=.../spectrafx-visual-review.ppm
[spectrafx-visual] report=.../spectrafx-visual-review.json
[spectrafx-visual] changed A/B/PRISMA=192/322/232
PASS effect_visible_in_both_views
PASS camera_changes_screen_position
PASS opaque_center_depth_test
PASS reset_restores_baseline_red
PASS prisma_effect_visible
[spectrafx] PASS visual_review
```

The local screenshot suite and the new standard-library visual analyzer
passed. It verifies measurable camera offset, depth occlusion, reset
state and PRISMA effect contribution, but not subjective appearance or
human visual inspection. The actual PPM contact sheet and JSON files
remain on the user's Mac and have not been seen by this agent.

## What this demonstrates

- Stage 111 code was built on a production Metal machine and the full
  native acceptance ran without any failed or skipped stages.
- All four hardware MetalGPU regression tests and strict evidence checker pass.
- Seven real PPM captures were found, decoded and compared by the new
  tool; all five objective checks passed.
- Existing generic camera-independent FX interface, Renderer3D + PRISMA
  integration and GPU resource/impact safety remain accepted under
  the tested build.
- The user excerpt **does not show the new `gpu_frame_timing`
  p50/p95 or `metal_alloc_baseline_kib/peak_kib/after_kib` line**,
  even though `ctest_metal` passed. Do not invent those GPU metrics
  or imply precise physical VRAM telemetry.

## Handoff / next action

1. Read `build/spectrafx-validation/spectrafx-acceptance/ctest_metal.log`
   around `[spectrafx-metal-rhi] gpu_frame_timing`, and collect the
   scheduled/valid/invalid sample counts, GPU p50/p95 and device-wide
   Metal allocation samples. These are whole-device timelines, not
   SpectraFX-exclusive GPU cost.
2. Inspect/upload the existing `spectrafx-visual-review.ppm` and
   `spectrafx-visual-review.json` to verify color, bloom and edges
   visually as well as via objective metrics.
3. Interactively test Editor GPU Assets Preview: Restart,
   Pause/Resume, Step, camera orbit, Reset view, resizing, and
   in-memory/hot reload of the authored GPU emitter asset.
4. Compare Metal timing and allocated resource results across stable
   baseline/long-run frames and check Metal API Validation/Instruments
   before declaring leak-free; verify Vulkan/GLES CPU fallback in
   compatible platform builds.
5. Do not merge the heavily divergent `main` or other developers'
   camera/PRISMA branches without conflict review.

**Tested SHA belongs to the code release**:
`441d69e08836888604e0aee6ec71765a51fddddc`.
Later documentation commits do not imply rerunning tests.
