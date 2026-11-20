# PHYSIS Stage 102 — Merge latest main into PHYSIS + TerraWeave integration (staged Git-data merge)

Date: 2026-11-19
Repository: `chromealex/AIVERSE`
Canonical integration branch: `integration/physis-terraweave-complete-20261119`
Isolated staging branch: `integration/physis-main-merge-stage102-20261119`
Canonical draft PR: https://github.com/chromealex/AIVERSE/pull/40
Staging merge-preview PR: https://github.com/chromealex/AIVERSE/pull/48
Previous handoff: `docs/agent-handoffs/physis-stage101-place-perf-native-green-2026-11-19.md`

## Source revisions and merge

- Source PHYSIS + TerraWeave integration: `658285a6ec22054ef9d382ef324ddd01ebe9d9b8` (Stage101).
- Pinned current `main`: `d7d9b3f4180dcba98b9238fd56eedc7ccb420bb1` (ECS scene/static integration).
- Git merge base: `d71c6d47e532b25fb816a8bb1f96c41e5275a165`.
- Main was diverged by 467 commits; PHYSIS integration ahead 1483 commits.
- Normal GitHub PR-based auto-merge was not mergeable. Rather than overwriting either history, an explicit three-way *content* merge was built using base/main/integration Git trees and the Git data API. The new true two-parent merge commit is **`a3a665d39d44a75fdb68656050583d352ebd2df5`**, parents ordered (integration, main); tree `28affaee3373c514841a0f6d224d076ad67b4953`. No force-push, `main` mutation, source-branch reset, or local agent-worktree manipulation.

## Files/architecture combined

- 94 changed *leaf* paths from `main` relative to the merge base, including new ECS scenes, prefab/shared/static components, scene collision namespaces, render batch policies, Vulkan/Metal support changes, skill catalog and tests. Full integration tree contains 4981 leaves.
- 11 leaf paths were changed on **both** parents: `AGENTS.md`, `CMakeLists.txt`, `apps/editor/Editor.cpp`, `apps/editor/windows/SceneWindow.cpp`, `engine/gfx/Gfx.h`, `engine/render/GlobalGeometryPool.cpp`, `engine/render/Model.h`, `engine/render/Renderer3D.h`, `engine/render/Renderer3DModels.cpp`, `skills/catalog.json`, `tools/skills/catalog.py`.
- `AGENTS.md`: preserved BOTH append-only instruction sections (PHYSIS/TerraWeave ownership and incoming main policies). `CMakeLists.txt`: retained PHYSIS/TerraWeave/Box3D/Place targets AND incoming ECS scene/collision targets, including both insertions after `modules/fps`.
- Editor, Gfx, model header, renderer header, skill catalog: three-way nonoverlapping line changes preserved from both parents.
- `GlobalGeometryPool.cpp`: main replaces the old `canPool` logic with new `geometryPoolAdmission` / per-model `geometryPoolPolicy`. To retain PHYSIS branch's dynamic CPU-vertex safeguard, reject `!model.batching.allowGeometryPool` as `DedicatedAsset`, alongside incoming main's new policy.
- `Renderer3DModels.cpp`: retained incoming shared-material caching and new `staticModelCacheGeometryCompatible` path. The PHYSIS branch's guard against reusing static geometry for `useVertexParameters` and `primitiveMaterialFilter` is re-expressed in `engine/render/StaticModelCachePolicy.h` in both `staticModelCacheDrawCandidate` and `staticModelCacheGeometryCompatible`; the latter is main-added and is adjusted as part of this integration. Retained feature branch's partial-baker `capture_` filter.

## Actual verification performed

**GitHub static checks completed only**:
- Compared all three recursively expanded Git trees with the merged tree: **4981 leaf paths retained**; **0 lost main-only changes**, **0 lost PHYSIS-only changes**; all 11 shared-path conflicts explicitly resolved.
- `skills/catalog.json` parsed successfully as JSON.
- Checked 8 critical merged files for Git conflict markers: none.
- Confirmed combined CMake source retains PHYSIS/TerraWeave fall bridge and incoming scene collision CTest target registration; original duplicated conditional target names `release_tools` and `aiverse_place_app` occur in each pre-merge parent and were **not** introduced by this merge.
- Confirmed combined render policy retains both `allowGeometryPool` and `geometryPoolPolicy`, and guards for dynamic vertices, material-filtered static cache, shared material snapshots, and baker captures.

**NOT RUN on Stage102 merge**: CMake configure, AppleClang/Metal/Box3D native compilation, Vulkan/GLES compilation, PHYSIS CTTests, combined engine full CTest, GPU/visual validation. Passing Stage101 focused Place 0.789ms does not prove Stage102 acceptance.

## Known limitations and next action

- Reconciled C++ can still have compile or runtime integration issues because outgoing and incoming feature changes were developed on divergent 467/1483-commit histories; **do not call full acceptance green** until actual macOS logs are recorded.
- Some policies are semantically merged from distinct generations; validate model geometry pooling, static packet cache, editor Scene, ECS scene collision namespace and PHYSIS/Terra/Box3D fall behavior together.
- Do NOT merge PR #40 into `main` while it remains Draft or before full acceptance; don't disable/relax Place thresholds.
- Any later `main` movement requires a separate pinned merge, not a claim this merge includes future commits.

### Next exact Mac Studio acceptance

```bash
cd ~/Projects/AIVERSE-physis-terra-final
git status --short
git fetch origin
git switch --detach origin/integration/physis-terraweave-complete-20261119
git show -s --format='%H%n%P' HEAD
# Check the source is descended from the pinned main revision:
git merge-base --is-ancestor d7d9b3f4180dcba98b9238fd56eedc7ccb420bb1 HEAD
AIVERSE_CTEST_VERBOSE=1 bash tools/physis-terraweave-final-validation.sh
```

Use the existing dedicated worktree only if it is clean; do not destroy other agents' changes. The script builds the complete Debug/Box3D/Place acceptance configuration and runs one **unfiltered** CTest. If failure, provide first compiler/CTest failure, fix integration without weakening checks, then write the separate Stage103 implementation and handoff with exact Mac evidence.
