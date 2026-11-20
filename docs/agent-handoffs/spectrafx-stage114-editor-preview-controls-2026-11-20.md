# SpectraFX Stage 114 — Editor Preview timeline, revision retries and deterministic QA

Date: 2026-11-20  
Branch: `feature/spectrafx-generic-cue-queue`  
Parent SHA: `76a7d222579abd8017f0740a9adace23b7f56744`  
**Last natively validated source:** `441d69e08836888604e0aee6ec71765a51fddddc` (Stage 113; 10/10 acceptance, 4/4 MetalGPU, 5/5 visual pixel checks). Source changes here require new compilation and interactive Editor QA.

## Reason for the change

The existing GPU Assets Editor Preview set `previewHash_=hash` and cleared its restart request even when `previewHost_.dispatch(cue)` failed. That made an unsupported GPU effect / missing CPU fallback draft look logically applied, masked retries and potentially hid the error until the user modified unrelated properties. Preview timing was scattered among UI booleans and wall-clock state and had no standalone tests. Switching two assets with byte-identical JSON did not explicitly request a restart.

## Implemented

- New `apps/editor/windows/SpectraFxGpuPreviewPolicy.h`: pure, editor-local state policy with no ImGui/Gfx/ECS/genre dependency. Tracks pause/resume, single pending 1/60 s step, monotonic finite wall-clock samples clamped to 50 ms, applied revision, failed revision, manual restart and invalid-draft invalidation.
- `apps/editor/windows/SpectraFxGpuAssetsWindow.cpp` now delegates timeline and revision decisions to that policy. A failed `dispatch` is recorded as **failed**, not **applied**, and is not resent every UI frame. A new text revision or explicit Restart allows retry. When an invalid JSON draft is observed, old GPU effects reset once at safe point and a pending step is preserved rather than consumed. Camera-orbit/Reset view, per-RT `SpectraFxView`, `prepareRenderView`, and rendering to the editor RT remain intact.
- Loading or creating a different asset explicitly requests a restart even when two assets have identical JSON bytes.
- New `tests/spectrafx_preview_policy.cpp` checks one-step semantics, pause without catch-up, resume, invalid/uninitialized frame preservation, non-finite clock safety, successful and failed revision gating, retry on edit/restart, invalid draft, project reset and zero hash.
- `CMakeLists.txt` registers desktop `aiverse_spectrafx_preview_policy_tests` with CTest `spectrafx_preview_policy`, labels `SpectraFX;EDITOR;Architecture`. Runs in the full and SpectraFX-labelled suites.

## Ownership/API

Only the Editor owns its new preview policy and UI state; generic `engine/fx`, `gfx::Device`, PRISMA, physics, camera subsystem and ECS authority are untouched. No changes to effect serialization or save-file formats.

Live `SceneHost::resetScene` remains an outside-pass safe-point operation that retains in-flight Metal buffers; `shutdownPreview` remains an Editor teardown operation. A failed RHI safe-point reset leaves a pending restart for a later attempt.

## Actual verification and limitations

Changed source was fetched from GitHub, edit anchors matched uniquely, tests and docs were authored in one repository patch. **No Apple native build / CTest, live ImGui rendering, screenshot or interactive mouse input has been executed on this new SHA.** A deterministic standalone policy test is authored but its native execution must still be checked. Do not use Stage 113 hardware PASS as proof that Stage 114 compiled.

Remaining: on-device interactive Editor Preview QA for pause/step, orbit, parameter edits, asset switch, valid→invalid→valid drafts, forced GPU rejection and restart, window close/reopen and RT resize. Then long-run Metal API Validation/Instruments leak/resource studies, frame pacing and merge review with independent UI migration/camera branches.

## Next Mac Studio acceptance

Use the clean detached validation worktree and run the strict one-pass acceptance with actual image capture:

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

Expected additional desktop `spectrafx_preview_policy` CTest (total test count may increase by 1); all 4 MetalGPU tests remain required. Record exact compiled SHA, CTest summary, Editor Preview observations, hard errors and next agent task. Never claim an unrun test passed.
