# AIVERSE — Metal GfxMetal.mm build break, 2026-11-20

**Repository:** chromealex/AIVERSE
**Isolated hotfix branch:** `fix/metal-completed-frame-serial-brace-20261120`
**Source:** current `main` at hotfix-branch creation
**Fix commit:** `813d49fd12c0005b09682088de3cb7ad12180adc`
**Changed file:** `engine/gfx/metal/GfxMetal.mm` (one missing closing brace restored)

## Actual user report and error chain

The user pasted a failed Mac Studio compile of the older local worktree `AIVERSE-terraweave-stage70-local-qa` building target `aiverse_spectrafx_metal_rhi_test`:

- `warning: abstract class is marked 'final'`
- `Gfx.h` reports many unimplemented Device pure-virtual methods
- `error: member initializer 'layer_' does not name a non-static data member`
- `use of undeclared identifier 'device_'`, `queue_`, `frameSemaphore_`, `info_`
- Compilation fails inside `engine/gfx/metal/GfxMetal.mm` at ~line 85 onward.

The accompanying Stage127 CPU MDI pipeline experiment was a **separate worktree** and ran all eight tests PASS. Corrected `batching-stage127-analyze.py` successfully produced:
- OFF: GPU CB Direct 0.570219ms, CPU MDI 0.819625ms, +43.74% GPU difference, CPU -40.79%.
- ON: GPU CB Direct 0.569854ms, CPU MDI 0.782240ms, +37.27% GPU difference, CPU -40.36%.
- Reports written as `stage127-runs.csv` and `stage127-comparison.csv`. The experiment remains opt-in, not default enabled.

## Root-cause review

GitHub inspection found a deterministic C++ syntax defect in **current remote main** `GfxMetal.mm`: the method `uint64_t completedFrameSerial() const override` lacked the closing `}` after its return expression, so the next `uint64_t gpuSubmissionFence() const override` started inside the previous method. That causes scope/parse cascades including the class appearing abstract and its trailing members inaccessible. The same method is **properly closed** on remote `integration/terraweave-physis-stage70-20261120` and the dedicated batching feature branch. This means it would be unsafe to blindly replace one branch's entire Metal source with another; the fix is the exact missing brace only.

The hotfix restores this single `}` between `completedFrameSerial()` and `gpuSubmissionFence()`. The repaired `GfxMetal.mm` GitHub blob SHA equals the version already present in remote `integration/terraweave-physis-stage70-20261120` (8b68f32f92c9b2ba84f8cfd0fc3842a4039f7a24).

**Important caution:** the user's local TerraWeave worktree is not directly accessible to this agent; its path and errors match this type of failure, but its actual file bytes were **not inspected**. Determine whether the exact missing-brace sequence exists locally before applying the hotfix, since another local merge conflict could cause the same compiler cascade.

## Mac Studio validation, in place, no hard reset

First inspect the local file:

```bash
cd /Users/aleksandrfeer/Projects/AIVERSE-terraweave-stage70-local-qa
sed -n '94,122p' engine/gfx/metal/GfxMetal.mm
```

If `completedFrameSerial()` lacks the brace, add exactly one `}` after its `return ... : 0;` and before `gpuSubmissionFence()`. Alternatively pull just this correction from the isolated hotfix commit with a carefully inspected `git show` / `git apply`, preserving local merges and other agent changes:

```bash
git fetch origin fix/metal-completed-frame-serial-brace-20261120
git show 813d49fd12c0005b09682088de3cb7ad12180adc -- engine/gfx/metal/GfxMetal.mm
# Only after confirming local file matches the hunk:
git show 813d49fd12c0005b09682088de3cb7ad12180adc --format= -- engine/gfx/metal/GfxMetal.mm | git apply --check
git show 813d49fd12c0005b09682088de3cb7ad12180adc --format= -- engine/gfx/metal/GfxMetal.mm | git apply
```

Then rerun the previously failing local build target:

```bash
cmake --build build/terraweave-main-acceptance --target aiverse_spectrafx_metal_rhi_test --parallel 8
```

Adjust the build directory to the actual configured local build; do not assume the above path exists. Do not re-merge large feature branches for a one-character syntax error.

## Tests actually performed and pending

**Performed:** inspected current remote main and two feature branches through GitHub connector, verified missing closing brace and correctly balanced version, committed a one-character hotfix to isolated branch, confirmed resulting GitHub blob matches the known-good Stage70 feature branch. Separately, read user's pasted native build failure and Stage127 corrected GPU analysis output.

**Not performed:** local AppleClang compile, actual local TerraWeave worktree byte check, CI run, full CTest, cross-platform Vulkan/GLES build, hotfix PR/merge into main. This is a source-level correction with a high-confidence root cause, not a verified build. Main was **not** modified by this hotfix.

**Next:** user checks local method and selectively applies one brace if matching, then rebuilds failing target. After verified native build, coordinate merging hotfix to main without overwriting other agents' changes. Keep CPU MDI PR #39 Draft and opt-in.
