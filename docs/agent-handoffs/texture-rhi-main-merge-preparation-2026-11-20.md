# AIVERSE Texture RHI — main merge preparation and unresolved conflicts
Date: 2026-11-20

## Status: NOT MERGED
User requested merge of current main into the Texture RHI feature, without changing main. There is no local repository checkout accessible in the assistant container (GitHub host DNS resolution fails), and the available GitHub connector cannot execute git merge or resolve the conflicting index. To avoid overwriting concurrent agents' changes, no synthetic Git tree merge was attempted.

## Branches and exact source revisions
- Original verified Texture RHI branch: `feature/texture-rhi-unification` at `9145e6efe72a31306f7197f32f080e29908beb3b`.
- Main (at preparation): `e4129c5062a6ab47e7c70f3fdd483392fde1f679`.
- Isolated integration branch created: `integration/texture-rhi-main-20261120`, initially from the exact Texture RHI verified SHA.
- Temporary draft integration PR #56: https://github.com/chromealex/AIVERSE/pull/56 ; base integration branch, head main. GitHub reports `mergeable=false` (do not force).
- Original Texture RHI PR #21 remains Draft. Original main and feature branches are unchanged by this preparation.
- GitHub compare reports **diverged** with ~2366 main-only and ~944 feature-only commits from common base `9ee6ff6fbf390bd45363bc92c838f492dfe92eee`. Compare file list is truncated at 300; it cannot be treated as complete conflict enumeration.

## Preserve these changes
Texture RHI passes its macOS final one-shot at SHA `9145e6efe72a31306f7197f32f080e29908beb3b`: user-supplied acceptance manifest `safe_texture_tests=22`, `native_metal_gpu_tests=7`, `full_engine=1`, `result=PASS`. Merging main changes resulting SHA and invalidates the final manifest for new HEAD until revalidation. Do not discard Texture RHI's core gfx and VT changes, GLSL/MSL sampling, tests, CI or handoffs. Do not discard main's concurrent rendering, animations, editor, ECS, Physis, TerraWeave and other agent changes.

## Local integration process on Mac (with clean independent worktree)
```sh
cd ~/Projects/AIVERSE-texture-verify-20261119-150819
git fetch origin main feature/texture-rhi-unification integration/texture-rhi-main-20261120
# Use a fresh directory for integration, NOT the verified Texture RHI worktree.
git worktree add ../AIVERSE-texture-main-merge integration/texture-rhi-main-20261120
cd ../AIVERSE-texture-main-merge
git status --short
git merge --no-ff --no-commit origin/main
# Resolve any conflicts by preserving both feature sets in each combined source.
git diff --name-only --diff-filter=U
git status
git add <resolved-paths>
git commit -m 'merge: integrate main into Texture RHI preserving both systems'
git push -u origin integration/texture-rhi-main-20261120
```
If conflicts exist, a failed merge is an intermediate state; do not use `git checkout --ours/--theirs` wholesale for CMake, renderer or editor files. Do not `git rebase --skip` any feature commit. If there are unresolved files, keep changes uncommitted in worktree and provide their conflict hunks, resolve one at a time. If the integration worktree is not clean, save changes before merging.

## Gates after merge
Compile focused texture tests, run 22 portable checks, full-engine CTest, 7 native Metal GPU passes, and record the exact merged SHA and new `acceptance.txt`. Also run Vulkan/GLES/WebGL2 acceptance on appropriate platforms. Then fast-forward the original Texture RHI feature to reviewed merged integration commit (only if explicitly authorized and non-conflicting) and rebase PR #21 onto the intended target main after the branch is validated. Keep `main` unchanged.

## Tests actually performed in this preparation
GitHub branch creation and draft PR #56 succeeded; GitHub compare/metadata queries performed. **No git merge, no conflict resolution, no CMake build, and no CTest was executed by the assistant this turn.** The only verified macOS test result is the pre-merge user-provided acceptance manifest.

## Next agent step
Resolve the merge conflicts locally in the isolated worktree (or via a full git-capable CI runner), preserve both sets of code, commit merge to integration branch, rerun native tests at merged HEAD, then update handoff and PR. Do not claim main already merged.
