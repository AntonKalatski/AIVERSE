# SpectraFX Stage 120 — merged main + SpectraFX final Mac Studio native acceptance PASS

**Date:** 2026-11-20
**Validated code SHA:** `70c9aa666f0bdaafc1f8169636b68fb7e4e437ba`
**Branch at test time:** `integration/spectrafx-main-sync-20261120`
**Hardware:** Apple M3 Ultra / Darwin
**Build:** `/Users/aleksandrfeer/Projects/AIVERSE-spectrafx-final-clean-20261120/build/spectrafx-main-clean`
**Acceptance logs:** `build/spectrafx-main-clean/spectrafx-acceptance/`

This commit descends from the genuine Stage 117 two-parent main merge:
`7ef385060956b73e131821e66e3a8b515da6ac32`, with 23 handoff
Markdown whitespace fixes and Stage 119 clean-worktree instructions.
The user executed the full strict native acceptance on this exact
integration SHA in a separate clean Mac Studio worktree, not on the old
pre-merge feature branch. This document records **observed evidence**
and changes no CMake, runtime, shader, UI, asset or test code.

## Actual verification from the Mac run (user-provided output)

| Acceptance stage | Exit/status |
|---|---:|
| configure | 0 |
| spectrafx_core_build | 0 |
| architecture | 0 |
| host_integration_build | 0 |
| full_build | 0 |
| ctest_full | 0 |
| ctest_spectrafx | 0 — **15/15 tests PASS**, 14.55 s |
| ctest_metal | 0 — **4/4 real MetalGPU PASS**, 5.02 s |
| metal_evidence | 0 — PASS=4/4 SKIP=0 MISSING=0 FAIL/UNKNOWN=0 |
| visual_review | 0 — **5/5 objective checks PASS** |

**Failed stages: 0. Skipped stages: 0. All 10/10 acceptance stages exit 0.**
The reported `ctest_full` stage exited 0, so the full enabled CTest
suite passed. Its **total test count was not present** in the excerpt;
do not falsely report it as the old pre-merge `128/128`.
`spectrafx_preview_policy` is covered by the focused SpectraFX suite
and the full test stage (15/15 focused PASS).

## Real MetalGPU evidence

The actual Metal RHI on Apple M3 Ultra printed:

```
device=Metal Apple M3 Ultra
A left/right=229/0 B left/right=0/228
gpu_frame_timing requested=512 scheduled=512 valid=512 invalid=0
p50_ms=0.1006 p95_ms=0.8632
metal_alloc_baseline_kib=1472 peak_kib=1728 after_kib=1664
churn_frames=512 capacity=256 peak_nominal_kib=36
retire_remaining=0 gpu_healthy=1
alpha_views_first=51/229 expected~51/230
impact_queued=1 impact_discarded=1 delivered=0
```

512 complete GPU timestamp samples, zero invalid samples and no
resources left in the 3-frame test retirement queue. Reported GPU
p50/p95 are **whole small 128×128 test command-buffer intervals**, not
the isolated SpectraFX shader cost or game frame time. Compare the
pre-merge snapshot p50=0.0800 ms / p95=0.3958 ms only as an exploratory
observation; multiple controlled runs would be required to call the
increased merged p95 a reproducible performance regression.

`MTLDevice.currentAllocatedSize` values are **device-wide** allocations,
not physical GPU residency or a conclusive leak detector. Reported
peak nominal 36 KiB is the test's dedicated GPU payload estimate.

The independent real MetalGPU test suite passed:
`spectrafx_metal_gpu`, `spectrafx_metal_billboard`,
`spectrafx_metal_rhi`, `spectrafx_metal_scene`.

## Renderer3D + PRISMA + visual

Real `spectrafx_metal_scene` reported:
`A pixels=168 center=63.50 B pixels=304 center=43.07
depth_center_delta=0`, reset without ghost effect,
`prisma_hdr=1 passes=10 fx_pixels=232
max_red_delta=185 depth_delta=-1`,
`failures=0`. Seven actual on-device PPM frames were saved to
`/tmp/spectrafx-final-scene-*.ppm`.

Automated `visual_review`: changed RGB pixels A/B/PRISMA
`192/322/232`, PASS for effect visibility in both views, camera
screen-position change, opaque center depth, reset restoration and
PRISMA effect contribution. Real on-device images from this new
integrated SHA were automatically checked, **but their exact PPM
contact sheet has not been separately viewed in this turn**.

## Integration result / promotion gate

This is the **first full successful native acceptance after merging
main into SpectraFX**. The integration branch can be fast-forwarded
into `feature/spectrafx-generic-cue-queue` because the tested merge
history contains the old feature head. Later documentation-only
commits do not constitute a new native test run.

Still open: actual live Editor GPU Assets Window mouse orbit, pause,
step, JSON hot-reload, safe reset, close/reopen and RT resize QA;
controlled long-run real Metal allocation measurements; independent
non-Metal Vulkan/GLES gameplay coverage and CI checks from other
parallel subsystems. **No automatic merge into `main`** is
authorized by this acceptance result alone; route through a reviewed
PR and separate platform gates.

## Next agent

1. Confirm current Git branch refs before promotion. Fast-forward only
   the SpectraFX feature ref, never force/rebase/overwrite.
2. Track GitHub Actions checks on the integration / new feature head;
   a still-queued check is not a PASS, and skipped optional jobs require
   policy-specific interpretation.
3. Run live Editor UX checklist on M3 Ultra. If live QA fails, fix the
   exact defect in a dedicated source commit and add new handoff.
4. Before any main merge, review final PR diff and rerun required checks
   if main advances.
