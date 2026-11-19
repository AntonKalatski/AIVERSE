# SpectraFX Stage 102 — strict evidence for full native Metal acceptance

**Date:** 2026-11-19  
**Branch:** `feature/spectrafx-generic-cue-queue`  
**Code commits:** `a6b8617d472b104e660a254d2ed228abc4c81905` (MetalGPU evidence verifier and CTest), `e711c2a955e54a12c6b3aa8136724744cc99726a` (skip report and shell indentation).  
**Prior native validation:** Stage 97, `4e363db3a6fc0d1631a574165febb7d26e08c3b8` (Apple M3 Ultra 8/8 acceptance steps, 2/2 older Metal tests PASS). The newly authored Stage 98–102 source remains **unverified by native CMake/Metal/CTest**.

## Motivation

`ctest -L MetalGPU` may return process exit 0 when all tests are skipped by `SKIP_RETURN_CODE 77`. A summary that only checks CTest exit 0 can therefore misleadingly suggest hardware GPU validation even when there was none. Stage 98 introduced a third real MetalGPU test, `spectrafx_metal_rhi`, in addition to `spectrafx_metal_gpu` and `spectrafx_metal_billboard`; real 3/3 execution must be distinguished from 2/3 + SKIP.

## Changes / API

- New `tools/spectrafx/verify_metal_ctest.py` parses verbose CTest results for **exactly three** named tests: `spectrafx_metal_gpu`, `spectrafx_metal_billboard`, `spectrafx_metal_rhi`. It emits explicit `PASS / SKIP / FAIL / MISSING` statuses. Missing or failed test is nonzero in every mode. `--require-all` also treats skips as failure.
- New `tools/spectrafx/test_verify_metal_ctest.py`: four hermetic `unittest` checks for 3/3 PASS, a skipped RHI test, an absent RHI test, and a failed billboard test. No Metal hardware or native engine required.
- `CMakeLists.txt` registers `spectrafx_metal_evidence_parser` with `SpectraFX;Architecture` labels when Python3 Interpreter exists, so the standard full CTest includes the verifier tests.
- `tools/spectrafx/acceptance.sh`: after verbose `ctest_metal`, executes a separate `metal_evidence` stage; logs `metal_evidence.log` plus its exit code in the acceptance summary. Environment `AIVERSE_ACCEPTANCE_REQUIRE_METAL=1` enables strict all-three-hardware-tests gating on Apple Silicon. On Linux/hosted macOS without a GPU, non-strict mode reports skips rather than pretending hardware passed. The absence of a new expected test is still a failure. Corrected the non-Apple and targeted-build-failure skip reporting.

## Actual checks

- GitHub branch/source inspection, exact-anchor edits, branch commits and fast-forward pushes were completed successfully.
- **Local hermetic parser check executed in the current container** against copies of the committed parser and unittest logic: `python3 -m unittest discover -v -s /mnt/data/spectrafx_parser_check -p 'test_verify_metal_ctest.py'` → **4 tests run, 4 passed, no failures**. It exercised PASS, skipped, missing and failed line parsing. This is parser-only evidence, not AIVERSE binary or macOS hardware verification.
- **NOT RUN:** native AIVERSE configure/build, on-device `spectrafx_metal_rhi`, `spectrafx_retirement`, end-to-end Renderer3D/editor GPU preview, full Mac Studio acceptance, ASan/leak/fence stress on the current branch. GitHub Actions associated with an earlier SpectraFX workflow run were still queued at inspection; no CI PASS is asserted.

## Next command (Mac Studio, no further feature work required before validation)

Verify that the existing validation worktree is clean before changing its detached HEAD:

```bash
cd ~/Projects/AIVERSE-spectrafx-validation
git status --short
git fetch origin
git switch --detach origin/feature/spectrafx-generic-cue-queue

AIVERSE_ACCEPTANCE_REQUIRE_METAL=1 \
  bash tools/spectrafx/acceptance.sh build/spectrafx-validation
```

Expected: `ctest_metal` and `metal_evidence` both exit 0 and explicitly list **3/3 real PASS** on Apple M3 Ultra; no skips. All other CMake/build/architecture/full CTest stages must exit 0. A nonzero failure requires inspecting `spectrafx-acceptance/{configure,spectrafx_core_build,full_build,ctest_full,ctest_metal,metal_evidence}.log` before changing source or test expectations. The exact source SHA from `summary.md` must be recorded with results.

## Remaining integration work

Hardware 3/3 acceptance is a prerequisite, not the final GPU release gate. Stage 98 tests real gfx::Device offscreen rendering, Stage 100 exercises delayed impact callbacks and Stage 101 checks deterministic HybridPlayback retirement, but actual Renderer3D Scene/Game viewport screenshots and production GPU load/pacing, frame-in-flight lifetime, editor reset and high-volume concurrency still require focused visual/stress tests. Preserve the genre-independent `SpectraFxView` boundary, do not move game/editor logic into engine/fx and do not merge the highly divergent `main` automatically.
