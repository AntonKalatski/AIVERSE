# SpectraFX Stage 111 — PPM visual evidence review and Metal timing / allocation diagnostics

**Date:** 2026-11-20
**Branch:** `feature/spectrafx-generic-cue-queue`
**Code commits:**
- `3e56816e991f304bbe4ebe0fe1abc8505523e4d8`: real Metal RHI asynchronous GPU frame timing and device allocation instrumentation.
- `65426ea8b551e3a33713109a43c17e990375cd1f`: standard-library PPM contact sheet + JSON QA utility, unit tests and acceptance wiring.
- `e5aa28f843f7459afa8803a60e4c53d4b12cecc0`: correct synthetic camera-centroid test coordinates and exercise strict QA.
**Last known fully validated engine SHA:** `b06c6d0f4dc820455d9c7b25dc98aa33397adca7` on Apple M3 Ultra: user-reported 126/126 native CTests, 11/11 SpectraFX, 4/4 real MetalGPU, all nine acceptance stages exit 0, 512 x 256 GPU churn PASS. **Stage 111 edits are not yet CMake/CTest/native-validated.**

## RHI GPU diagnostics

Changed `tests/spectrafx_metal_rhi.mm` only — production RHI and core remain unchanged.

- Uses the existing `gfx::Device::timeGpuFrame(tag)` / `takeGpuTimings()` APIs, which on Metal read completed command-buffer GPUStartTime/GPUEndTime. Requests one tagged sample for each churn frame, drains completions every `beginFrame`, and collects a final sample batch after the diagnostic completion fence.
- Prints `gpu_frame_timing requested=... scheduled=... valid=... invalid=... p50_ms=... p95_ms=...`. Calculations use samples from finished command buffers only, exclude unsupported or invalid timing data, and use nearest-rank percentiles. No performance assertion/benchmark threshold depends on CPU scheduling or hardware temperatures.
- Prints `metal_alloc_baseline_kib=... peak_kib=... after_kib=...` using Apple `MTLDevice.currentAllocatedSize` under the availability guard. This property reports **all resources allocated by the Metal device**, not physically resident VRAM, not total system memory and not SpectraFX-exclusive bytes. Baseline is taken after common test resources have been prepared and before the churn sequence, peak is sampled while frames are encoded, and after is taken after a completed frame readback.
- The existing `peak_nominal_kib`, outstanding resource retirement/health, 3-frame lifespan, alpha index probe and impact callback checks are preserved.
- The optional timing samples may be unavailable on some Metal devices/drivers; print `valid=0` instead of claiming a pass or zero GPU cost. Actual RHI pixel acceptance remains independent.

## Seven-PPM visual contact sheet + strict objective pixel QA

New `tools/spectrafx/review_frames.py`, pure Python standard library:

- Reads exactly the seven authentic binary RGB `P6` files from `AIVERSE_SPECTRAFX_SCENE_CAPTURE` prefix (two camera baseline/FX pairs, after-reset and PRISMA HDR/Bloom baseline/FX).
- Refuses missing, invalid, truncated, wrong-size or unsupported maxval files; does not fabricate screenshots. The parser handles binary pixel bytes that happen to be ASCII whitespace correctly.
- Generates a 4-column by 3-row **PPM contact sheet**, including three per-channel absolute-difference heatmaps. Layout (no embedded labels) is included in JSON.
- Writes `<output-prefix>.json` with RGB changed-pixel counts, positive red intensity centroid, center pixel red difference, reset baseline difference and PRISMA affected pixels. Optional `--strict` fails when objectively established fixture invariants regress (FX visibility in both cameras, displacement, raw depth occlusion, reset convergence, postprocess FX contribution). These checks match existing Stage 104/107 fixture semantics but do not claim subjective artistic quality.
- Existing `tools/spectrafx/acceptance.sh` runs the optional **visual_review** stage after Metal evidence when `AIVERSE_SPECTRAFX_SCENE_CAPTURE` is set. Saves `spectrafx-visual-review.ppm`, `spectrafx-visual-review.json`, and `visual_review.log` under the existing acceptance report directory. When capture flag is absent, the extra stage is omitted (not treated as a skipped test).
- `CMakeLists.txt` registers cross-platform `spectrafx_visual_review_parser` CTest when Python3 exists, labelled `SpectraFX;TOOLS`, exercising the portable tool with synthetic PPMs.
- `tools/spectrafx/test_review_frames.py` has four hermetic unit tests: pixel data beginning with a whitespace byte is preserved; a truncated P6 frame is rejected; a seven-frame fixture generates contact sheet/JSON satisfying strict camera/restoration constraints; missing source files are rejected rather than invented. The synthetic camera positions were corrected in the third code commit.

Expected test count can grow from 126 to **127** (CMake condition for Python3 interpreter). Do **not** assert this new CTest or the visual-review tool passed on macOS until a new run exists.

## Actual checks

- Fetched production GitHub source, inspected current RHI timing API and real Metal backend, applied deterministic uniquely matched changes, committed and fast-forward pushed all changes to the feature branch.
- Cross-checked `MTLDevice.currentAllocatedSize` against current Apple Metal API documentation; it is the total allocated size of device resources, not residence or an engine-subsystem ownership breakdown.
- Checked the synthetic centroid arithmetic in the local container: the initial test fixture failed its intended 6px shift, the updated source inputs give inner-region centroids **21.0** (A) and **13.0** (B), satisfying B + 6px < A.
- **NOT RUN against actual committed Python files** in this environment (network/filesystem cannot materialize the GitHub connector blobs directly), and **NOT RUN** native CMake/Clang, CTest, GPU pixel capture, real Metal profiling, live Editor GUI or on-device memory timing at the Stage 111 SHA. Older Stage 110 126/126 green results remain tied to their exact SHA.

## One-command Mac Studio acceptance and visual review

Worktree must be clean before detaching the feature ref; never overwrite parallel agents' worktrees.

```bash
cd ~/Projects/AIVERSE-spectrafx-validation
git status --short
git fetch origin
git switch --detach origin/feature/spectrafx-generic-cue-queue

AIVERSE_ACCEPTANCE_REQUIRE_METAL=1 \
AIVERSE_SPECTRAFX_RHI_STRESS_FRAMES=512 \
AIVERSE_SPECTRAFX_RHI_STRESS_CAPACITY=256 \
AIVERSE_SPECTRAFX_SCENE_CAPTURE=/tmp/spectrafx-scene \
  bash tools/spectrafx/acceptance.sh build/spectrafx-validation
```

Inspect `build/spectrafx-validation/spectrafx-acceptance/summary.md`, `ctest_metal.log`, `metal_evidence.log`, `visual_review.log` and `spectrafx-visual-review.json`. For visual review open `spectrafx-visual-review.ppm` in Preview, or upload it and the JSON back to the assistant. Note that the optional new `visual_review` stage makes ten acceptance stages with capture enabled.

## Remaining work for next agent

Fix any native compiler/CTest regressions first, and record exact SHA and logs in a new handoff. After acceptance, visually inspect the contact sheet and live Editor GPU Preview (pause/step/orbit/asset hot reload), profile Metal resource allocation after repeated scene resets to distinguish stable caches from leaks, and compare GPU p50/p95 under comparable thermal frame pacing against baseline. Verify non-Metal Vulkan/GLES CPU fallback when integration branches are ready. Do not rebase/merge `main` blindly (parallel camera/PRISMA/PHYSIS agents own overlapping files).

## User-reported native result: Stage 111 PASS (2026-11-20)

Source `441d69e08836888604e0aee6ec71765a51fddddc`
passed all ten acceptance stages (exit 0; failed 0; skipped 0) on
Darwin/Apple M3 Ultra. The strict checker confirmed all four real
MetalGPU tests PASS with no missing or skipped tests.
The new `visual_review` PASS analyzed seven local PPM frames:
five objective checks PASS, changed pixel counts A=192, B=322,
PRISMA=232. Actual contact sheet has not been visually reviewed.
The provided excerpt does not print whole-GPU p50/p95/allocation
figures or full CTest count. See Stage 112 handoff for details.
