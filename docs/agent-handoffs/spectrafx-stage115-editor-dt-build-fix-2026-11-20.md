# SpectraFX Stage 115 — Editor Preview C++ compile scope fix after Stage 114 hardware regression

**Date:** 2026-11-20  
**Branch:** `feature/spectrafx-generic-cue-queue`  
**Failing exact source:** `c5dd5914edac64f8071f4bd6a5ffd8eb5e6353e3`  
**Implementation fix commit:** `fc08834e1744a0a50d272043167515a635b70565`  
**Stage 115 handoff:** documentation commit after the fix (this document).

## User-reported Mac Studio acceptance

Mac Studio Darwin/Apple M3 Ultra, build
`build/spectrafx-validation`, tested source SHA `c5dd5914edac64f8071f4bd6a5ffd8eb5e6353e3`.

| Stage | Result |
|---|---|
| configure | 0 |
| spectrafx_core_build | 0 |
| architecture | 0 |
| **host_integration_build** | **2 (FAILED)** |
| **full_build** | **2 (FAILED)** |
| **ctest_full** | **SKIP (prevent execution of potentially stale tests)** |
| ctest_spectrafx | 0 |
| ctest_metal | 0 |
| metal_evidence | 0 |
| visual_review | 0 |

**Reported totals: failed stages 2; skipped stages 1.** The focused SpectraFX CTest and real four MetalGPU tests were passing, but **the complete native acceptance FAILED** and cannot be marked all-green.

The user did not supply the compiler diagnostics from `host_integration_build.log` or `full_build.log`; consequently exact failure provenance is not fully proven from logs. Source inspection does independently reveal a deterministic C++ compilation error in the Editor translation unit.

## Concrete defect found in source

`apps/editor/windows/SpectraFxGpuAssetsWindow.cpp`, function `drawPreview`:

Before the fix, `const float dt=previewPolicy_.advance(now,canDraw);`
was declared inside the `else { ... if(previewInitialized_) { ... } }`
scope. Later, outside that scope, `if(canDraw) previewHost_.prepareCpu(dt,view)`
used `dt` where it was **not declared**. This is a compile-time undeclared identifier in the Editor target, but it is invisible to the newly added standalone `spectrafx_preview_policy` and Metal RHI tests (which do not compile the whole Editor window). It matches the pattern of the user's Editor+full build failure; the exact compiler line was not uploaded.

## Fix implemented and pushed

In `apps/editor/windows/SpectraFxGpuAssetsWindow.cpp`:

1. Define `float dt=0.0f;` beside `bool canDraw=false;` outside the valid/invalid JSON branch.
2. When a valid revision is ready, assign `dt=previewPolicy_.advance(now,canDraw)` rather than declare a new block-local variable.
3. The once-per-frame GPU `previewHost_.prepareGpu(dt,view)` and CPU `previewHost_.prepareCpu(dt,view)` now use the same visible time step, while invalid/unavailable frames keep their existing `previewPolicy_.advance(now,false)` timestamp/Step-preservation behavior.

No modifications to `engine/fx`, RHI, PRISMA, effect assets, game modules or native Metal shaders. The previous tested Stage 113 source remains intact in history.

## Verification status

GitHub source was inspected at failing SHA and the lexical scope error identified. The replacement was applied with exact source-anchor checks and committed/pushed as `fc08834e1744a0a50d272043167515a635b70565`. Verified in current branch source that the enclosing-scope `dt` declaration exists and the branch-local `dt` declaration is gone.

**NOT RUN after this fix:** native macOS Clang/Editor build, full build, full CTest, four Metal GPU tests, visual review, interactive Editor Preview. Do not attribute the failing SHA's passing Metal tests to the new fix SHA without rerunning.

## Next focused verification on Mac Studio

In clean validation worktree only:

```bash
cd ~/Projects/AIVERSE-spectrafx-validation
git status --short
git fetch origin
git switch --detach origin/feature/spectrafx-generic-cue-queue

# Quickly confirm the previously failing native host compilation before
# spending ~14 minutes on the full CTest:
cmake --build build/spectrafx-validation --config Debug \
  --parallel 4 --target aiverse_editor aiverse_fps_client aiverse_client \
  2>&1 | tee /tmp/spectrafx-editor-rebuild.log
```

Only after the focused build exits zero, run the strict one-pass acceptance:

```bash
AIVERSE_ACCEPTANCE_REQUIRE_METAL=1 \
AIVERSE_SPECTRAFX_RHI_STRESS_FRAMES=512 \
AIVERSE_SPECTRAFX_RHI_STRESS_CAPACITY=256 \
AIVERSE_SPECTRAFX_SCENE_CAPTURE=/tmp/spectrafx-scene \
  bash tools/spectrafx/acceptance.sh build/spectrafx-validation
```

If the focused host build still fails, use the **first actual error** from `/tmp/spectrafx-editor-rebuild.log` or `build/spectrafx-validation/spectrafx-acceptance/host_integration_build.log`; do not guess a second cause. Then patch code on feature branch, add the next handoff and rerun.

Remaining Editor GUI acceptance (Pause/Step, camera orbit/Reset view, JSON invalid/retry, hot reload, close/reopen) requires interactive on-device QA after native builds pass. Avoid blind merge of highly divergent `main`.
