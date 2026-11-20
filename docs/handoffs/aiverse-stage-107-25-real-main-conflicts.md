# AIVERSE Stage 107 — actual conflict list from isolated merge worktree

Date: 2026-11-20
Repository: chromealex/AIVERSE
Worktree (user Mac): `~/Projects/AIVERSE-prisma-main-resolution`
Local branch: `integration/prisma-main-resolution-20261120`
Base at worktree creation: main b80601eb3fdcafddedbe9a9387f78a2fea0e499f
Other side: integration/prisma-motionweave-view 1a5bac1e9e04aa56f90e134450aadbcdeec0168d
Previous handoff: docs/handoffs/aiverse-stage-106-main-merge-conflict-triage.md

## What user actually ran
```sh
git fetch origin
git worktree add -b integration/prisma-main-resolution-20261120 \
    ~/Projects/AIVERSE-prisma-main-resolution origin/main
cd ~/Projects/AIVERSE-prisma-main-resolution
git merge --no-ff --no-commit origin/integration/prisma-motionweave-view
git diff --name-only --diff-filter=U
```
Git reported 25 textual conflicts, while many other files auto-merged.
`main` was at b80601eb3fdcafddedbe9a9387f78a2fea0e499f (including merged SpectraFX PR #60).
**There is an active MERGE_HEAD and unmerged index on the user's Mac**.
No merge commit/push has been made. Do not switch branches, run
rebase/checkout --ours/theirs en masse, or discard the merge.

## Exact unmerged paths (25)
1. `AGENTS.md`
2. `CMakeLists.txt`
3. `apps/client/GameClient.cpp`
4. `apps/editor/Editor.cpp`
5. `apps/editor/Editor.h`
6. `apps/fps/client.cpp`
7. `docs/agent-handoffs/universal-niagara-vfx-2026-11-18.md`
8. `docs/editor-unification-handoff.md`
9. `docs/models.md`
10. `engine/gfx/Gfx.h`
11. `engine/gfx/metal/GfxMetal.mm`
12. `engine/gfx/vulkan/GfxVulkan.cpp`
13. `engine/gfx/vulkan/VkDevice.h`
14. `engine/render/GlobalGeometryPool.h`
15. `engine/render/Model.cpp`
16. `engine/render/Renderer3D.cpp`
17. `engine/render/Renderer3D.h`
18. `engine/world/WorldScene.cpp`
19. `engine/world/WorldScene.h`
20. `modules/fps/CMakeLists.txt`
21. `modules/rts/view/RtsViewDraw.cpp`
22. `tests/ecs_cow_exception_test.cpp`
23. `tests/ecs_cow_parallel_test.cpp`
24. `tests/ecs_cow_snapshot_test.cpp`
25. `tests/ecs_guarded_parallel_test.cpp`

## Required preservation
- Renderer3D, model, geometry, RHI Metal/Vulkan and generic view
  must retain both recent main innovations and Prisma/MotionWeave.
- FPS game/client, editor and world scene APIs must reconcile both
  definitions and corresponding implementations; no whole-file
  replacements for engine or game files.
- Four ECS tests should preserve always-on Release checks and any new
  main test logic.
- Generated skills/catalog.json must be reconciled with its generator.
- Diff/checks before committing; slow native build/CTest explicitly
  deferred until after merge at user request.

## Safe three-way data handoff
To resolve these conflicts faithfully another agent needs current
Git index stages 1 (base), 2 (main), 3 (Prisma/MotionWeave) and
conflict-marked working copies. An independent read-only Python
collector has been generated and validated on a one-conflict local
Git fixture. The current user's worktree is **not accessible** from
the cloud; do not guess its state. Export stages to a ZIP, upload
it for file-by-file semantic resolution, and deliver a SHA256-guarded
apply script to minimize any user editing. Only apply to matching
MERGE_HEAD / conflict content, then `git add` each reviewed file,
`git diff --check`, `git diff --name-only --diff-filter=U`.
No native main merge verification has been run.

## State of PR #59
https://github.com/chromealex/AIVERSE/pull/59
OPEN / GitHub mergeable_state=dirty as of Stage 106.
Do not claim main merged until a merge commit is actually pushed and
verified from GitHub. User requested merging before lengthy final tests,
but this does not authorize dropping code from either side.
