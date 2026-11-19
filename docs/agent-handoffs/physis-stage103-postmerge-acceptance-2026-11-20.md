# PHYSIS Stage 103 — post-merge acceptance checks and model batching safety regressions

Date: 2026-11-20
Repository: `chromealex/AIVERSE`
Branch: `integration/physis-terraweave-complete-20261119`
PR: https://github.com/chromealex/AIVERSE/pull/40 (Draft)
Previous handoff: `docs/agent-handoffs/physis-stage102-main-merge-2026-11-19.md`
Post-main merge + Stage102 handoff HEAD: `bc63e74cac870cc04f9faeb759a29d355247ff63`
Stage103 code commit: `71f07d42ee1e91cacd738745db4fe3be80ff80c9`.

## Stage103 code and rationale

`tests/render/model_batching_policy.cpp` extends the **existing** `model_batching_policy` CTest (no new target, no performance workload change):

1. GPU geometry pool safety: a CPU-authored model with `Model::Batching::allowGeometryPool=false` must not be accepted by `geometryPoolAdmission`, including after main introduced `Model::Batching::GeometryPoolPolicy`. When global pooling is enabled, the explicit result must be `DedicatedAsset`. Likewise a model with the new `GeometryPoolPolicy::Dedicated` must reject pooled shared buffers. The fixture uses the existing GPU-free MockDevice and three vertices/indices.
2. Static transform cache policy: a previously eligible rigid `ModelDraw` must be rejected by both `staticModelCacheCandidate` and `staticModelCacheGeometryCompatible` whenever `useVertexParameters=true` or `primitiveMaterialFilter>=0`, and must be eligible again when those flags are cleared. These were guards in the pre-merge PHYSIS renderer; after Stage102 they're factored into main's `StaticModelCachePolicy.h`.
3. Existing mesh/material/world/pose and GPU batching assertions in the target remain unchanged.

The changes are regression tests only. No extra `engine/physis`, TerraWeave, NEREID, Box3D, editor, renderer runtime or wire protocol modifications. All new checks use existing always-active `CHECK` rather than disappearing Release `assert`.

## Actual verification completed

- GitHub source read-back verified the new test snippets are committed in `tests/render/model_batching_policy.cpp`.
- CMake source checked: `aiverse_model_batching_policy_tests` still links the canonical `aiverse_models` and `aiverse_zstd_dec`; its existing registered CTest name is `model_batching_policy`.
- Stage102 Git tree/JSON/conflict-marker structural verification remains the previously recorded non-native evidence.
- GitHub Actions for Stage102 merge `bc63e74` were seen **queued**: full native macOS PHYSIS+TerraWeave one-pass, PHYSIS/Box3D focused, TerraWeave, Studio, GPU batching and CI. No PASS is inferred from queue status.
- Stage103 `71f07d42` triggered its own push and PR Actions; at inspection they were also **queued**, with NEREID conditional CPU regression `skipped`. Relevant run: https://github.com/chromealex/AIVERSE/actions/runs/44616122193.

**NOT RUN on Stage103:** C++/AppleClang compile, `model_batching_policy` CTest, full unfiltered 160+ AIVERSE CTest, Metal GPU, Place performance under merged code. Container cannot access user's Mac checkout or DNS to clone GitHub; the GitHub connector supports source reads and edits but cannot execute AppleClang on the user's Mac. Mark results pending, not successful.

## Next exact execution once runner is available

In a CLEAN checkout of the canonical integration ref:

```bash
cd ~/Projects/AIVERSE-physis-terra-final
git status --short
git fetch origin
git switch --detach origin/integration/physis-terraweave-complete-20261119
git rev-parse HEAD
git merge-base --is-ancestor d7d9b3f4180dcba98b9238fd56eedc7ccb420bb1 HEAD
AIVERSE_CTEST_VERBOSE=1 bash tools/physis-terraweave-final-validation.sh
```

The full acceptance script configures Debug with Box3D, editor, tools, headless and fast Debug Place producer, builds all enabled targets, verifies real PHYSIS/Terra fall bridge and Terra tests are registered, and executes ONE full UNFILTERED CTest while recording configure/build/JUnit and verbose Place phases in `build/physis-terraweave-onepass/onepass-reports/`. It includes the newly strengthened `model_batching_policy` target. Do not weaken the existing Place 5ms threshold, shrink clients/bodies or mark queued CI as PASS.

If full run fails, preserve original first failing compile or CTest log with tested SHA, make narrowly scoped fix on the same integration branch, add separate code commit and Stage104 handoff. If all green, independently confirm current `main` has not advanced and only then evaluate merging PR #40 into main; no premature merge.
