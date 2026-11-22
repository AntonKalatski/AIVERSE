# SpectraFX Stage 117 — integrate main into isolated SpectraFX validation branch

Date: 2026-11-20
Integration branch: `integration/spectrafx-main-sync-20261120`
**Merge commit:** `7ef385060956b73e131821e66e3a8b515da6ac32`
First parent, fully native-validated SpectraFX: `ba4b126fd533cdbcb8443f46a68c6d8afe76867f` (Stage 116: 128/128 full CTest PASS).
Second parent, main: `e4129c5062a6ab47e7c70f3fdd483392fde1f679`.
Common merge base: `d71c6d47e532b25fb816a8bb1f96c41e5275a165`.

## Why an isolated branch

The two lineages diverged by **359 SpectraFX commits ahead** and **1957 main commits behind** when inspected. An initial GitHub PR from main to an integration branch reported mergeability `dirty`. The previously accepted SpectraFX source must not be overwritten or called validated after integrating a very large volume of changes from other agents.

Created `integration/spectrafx-main-sync-20261120` from the green SpectraFX SHA, then produced a true **two-parent Git merge commit** using a three-way file merge against the shared base. Neither `main` nor the original `feature/spectrafx-generic-cue-queue` branch was modified. This is **not** an integration of SpectraFX into main.

## Exact file-level resolution

Against current main the result differs in exactly **88 expected SpectraFX files** (checked through GitHub compare), with no unrelated paths:

- **77 files added:** independent SpectraFX engine, Editor Windows, tests, CI workflow, utilities and agent handoffs.
- **5 modified files on paths unchanged in main since the common ancestor:** `apps/client/ClientFx.cpp`, `apps/client/ClientFx.h`, `engine/fx/ParticleSystem.cpp`, `engine/fx/ParticleSystem.h`, `engine/actor/CMakeLists.txt`.
- **6 files changed by both main and SpectraFX:** `CMakeLists.txt`, `apps/client/GameClient.cpp`, `apps/fps/client.cpp`, `engine/gfx/Gfx.h`, `engine/gfx/metal/GfxMetal.mm`, `skills/visuals/aiverse-spectrafx/SKILL.md`.

For each concurrent file, the exact original main and feature contents were reconstructed independently from base-to-branch line edits before applying combined changes. **Five of these six files had no line-overlapping edits**, so both sets of changes were preserved by line-wise three-way merging.

The only overlapping change was an identical `shutdown(gfx::Device&)` line in `apps/fps/client.cpp`. It was resolved deliberately, preserving both operations **before scene and device teardown**:

```cpp
void shutdown(gfx::Device& device) {
    horizonFrameTiming.detach(device); // main: TerraWeave timing bridge
    fxScene.shutdown();                // SpectraFX: release visual effects
    // Existing stream/server/client/audio/renderer/device cleanup retained
}
```

The actual resolved source retains the existing compact one-line style, with no cleanup code dropped.

## Static verification actually completed

- GitHub confirms the resulting SHA is a commit with exactly the **two intended parents** and expected merged tree.
- GitHub compare main-to-merge lists **exactly 88 files**, all from the expected SpectraFX paths, and no unrelated edits.
- Main `Renderer3D` interface remains present. New SpectraFX CMake targets, `spectrafx_preview_policy`, real Metal tests, PRISMA bridge, Vulkan gfx target and main Studio editor wiring are all present in the merged files.
- `apps/fps/client.cpp` contains both `horizonFrameTiming.detach(device)` and `fxScene.shutdown()`. The merge-related source files have no Git conflict markers.
- The existing GitHub PR #54 (main → integration) automatically appeared merged after the two-parent merge commit became the integration branch head.

**Not run on this merged SHA:** native Mac Clang/Metal build, CTest, GPU pixel tests, Editor GUI interaction, Vulkan/GLES builds, production performance or actual leak verification. Static/source checks are **not** substitutes for native acceptance.

## Final native QA — use a dedicated clean Mac worktree

Do not disturb the already-tested SpectraFX worktree or concurrent branches. Run from an existing checked-out AIVERSE repository:

```bash
cd ~/Projects/AIVERSE-spectrafx-validation
git fetch origin main feature/spectrafx-generic-cue-queue integration/spectrafx-main-sync-20261120
git worktree add --detach ~/Projects/AIVERSE-spectrafx-main-qa origin/integration/spectrafx-main-sync-20261120
cd ~/Projects/AIVERSE-spectrafx-main-qa
git submodule update --init --recursive

git merge-base --is-ancestor origin/main HEAD
git merge-base --is-ancestor origin/feature/spectrafx-generic-cue-queue HEAD
git diff --check origin/main HEAD

# First isolate integration errors before a potentially lengthy full CTest.
cmake -S . -B build/spectrafx-main-qa \
    -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_EDITOR=ON \
    -DAIVERSE_BUILD_CLIENT=ON -DAIVERSE_BUILD_HEADLESS=ON \
    -DAIVERSE_BUILD_TOOLS=ON
cmake --build build/spectrafx-main-qa --parallel 4 \
    --target aiverse_spectrafx_header_smoke \
    aiverse_spectrafx_preview_policy_tests \
    aiverse_editor aiverse_fps_client aiverse_client

# Only if focused build passes, run the full one-command acceptance.
AIVERSE_ACCEPTANCE_REQUIRE_METAL=1 \
AIVERSE_SPECTRAFX_RHI_STRESS_FRAMES=512 \
AIVERSE_SPECTRAFX_RHI_STRESS_CAPACITY=256 \
AIVERSE_SPECTRAFX_SCENE_CAPTURE=/tmp/spectrafx-merged-scene \
    bash tools/spectrafx/acceptance.sh build/spectrafx-main-qa
```

With main merged, the **full CTest count can differ from the previously green 128**. Require zero failed and zero skipped acceptance stages, the native Metal **4/4 real PASS**, SpectraFX-focused tests including preview policy, and 5/5 real screenshot checks. Inspect the first compiler/linker failure with its exact path rather than guessing.

## Promotion / rollback

Keep `feature/spectrafx-generic-cue-queue` and `main` unchanged until the merged commit passes hardware acceptance and any new conflicts are resolved in the integration branch. After validation, the feature branch can be advanced by fast-forward merge (no rebase and no force-push). The integration commit retains both historical parents, so rollback is a matter of switching away from the integration branch, not reverting main.

Next agent: own native final QA and any merge-only compile fixes in the integration branch. Record test SHA, logs, GPU metrics, source changes and exact next step in a fresh handoff. Live Editor UI QA and isolated GPU resource lifetime remain independent gates.
