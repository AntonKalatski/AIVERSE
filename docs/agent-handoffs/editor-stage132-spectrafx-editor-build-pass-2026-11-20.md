# AIVERSE — Stage 132: SpectraFX editor compile confirmed on Mac Studio

**Date:** 2026-11-20
**Repository:** `chromealex/AIVERSE`
**Documentation branch:** `fix/spectrafx-preview-viewport-retirement-20261120`
**User's local checkout:** `~/Projects/AIVERSE-terraweave-stage70-local-qa`

## Owner-confirmed validation

Following Stage131's targeted migration of two SpectraFX preview windows to the new editor-owned GPU texture retirement API, the user confirmed: **"Собралось"** (the previously failing `aiverse_editor` target compiled).

Stage131 fixes:
- `apps/editor/windows/SpectraFxGraphWindow.cpp`: acquire `ed.gpuDeviceLease()` and pass `ed.viewportRetirement()` plus debug name to `previewViewport_.ensure`.
- `apps/editor/windows/SpectraFxGpuAssetsWindow.cpp`: same change for the GPU assets viewport.

The two patches in this hotfix branch match the implementation in `integration/terraweave-physis-stage70-20261120`. `Viewport::ensure` was **not** reverted to the unsafe three-argument form, and no changes were made to Metal/Vulkan/GLES backend.

Previous owner report also confirmed that `aiverse_spectrafx_metal_rhi_test` built and its named CTest `spectrafx_metal_rhi` passed **1/1 in 1.18s**. The earlier brace hotfix targeted remote main; the user's local `GfxMetal.mm` already contained the required brace and was not changed by that patch.

## Validation boundaries

**Actually performed by owner:** focused SpectraFX Metal RHI native build and GPU test PASS; subsequent local editor build success verbally confirmed.  
**Not yet performed/confirmed:** a successful *full* all-target native build after editor fix, full CTest suite, gameplay/editor GUI smoke tests, Vulkan/GLES runtime verification. Do not claim the integration is accepted or all tests pass yet.
**Changed in Stage132:** this separate handoff document only. The local editor source changes are Stage131 work and were performed on the user's machine; they were not merged to main by this stage.

## Next exact command

In the same local worktree:

```bash
cd ~/Projects/AIVERSE-terraweave-stage70-local-qa || exit 1
cmake --build build/terraweave-main-acceptance --parallel 8 || exit 1
ctest --test-dir build/terraweave-main-acceptance --output-on-failure --parallel 8
```

Record total pass/fail/skipped counts and failing tests if any. After complete acceptance, reconcile the local tree with the integration/main branches without `git reset --hard` or overwriting other agents' work. Retain feature-level isolation: CPU MDI PR #39 remains Draft and MDI default disabled; the editor viewport retirement hotfix is independent.
