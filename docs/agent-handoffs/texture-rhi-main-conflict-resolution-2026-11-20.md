# Texture RHI integration — seven content conflicts resolved for local merge

Date: 2026-11-20

## Branch, current revisions, ownership

- Local target worktree: `~/Projects/AIVERSE-texture-main-merge`
- Target branch: `integration/texture-rhi-main-20261120`, at pre-merge `7355ea69dbce62168e5339de1512075e473d3d31`.
- Merged parent `main`: `e4129c5062a6ab47e7c70f3fdd483392fde1f679`, to be validated against `MERGE_HEAD`.
- Original verified texture branch: `feature/texture-rhi-unification` at `9145e6efe72a31306f7197f32f080e29908beb3b`.
- This document and seven resolved code files were prepared as a ZIP for application in a **still-uncommitted** Git merge. **No merge commit SHA exists until the local merge is committed.** No remote branch was mutated by generating these files.
- Conflict-marked source and all three stages came from the user-supplied `aiverse-texture-merge-conflicts.zip`.

## Files / architecture / decisions

1. `AGENTS.md`: retain Texture RHI streaming/gfx handoff and main's renderer/CPU caching, geometry, PHYSIS/TerraWeave/NEREID and other instructions by concatenating both nonoverlapping sections.
2. `CMakeLists.txt`: keep Texture RHI's conditional libktx/OpenEXR runtime linkage and main's NEREID water transitive link requirement. All prior automatic nonconflicting CMake merges are retained.
3. `engine/gfx/vulkan/VkDevice.h`: preserve texture device fragment slot limit and main's serial/completed-frame RHI methods.
4. `engine/render/Model.cpp`: keep reduced per-image virtual fallback texture upload/fail-closed retries; preserve main's material revision invalidation whenever texture binding changes, including fallback upload.
5. `engine/render/Renderer3D.cpp`: keep reflection-plane state reset plus `customFeedback_` per-scene clearing.
6. `engine/render/Renderer3D.h`: preserve VT feedback adapters/texture resolver/public binding APIs, include texture material resources, and main's NEREID ripple interface and static CPU MDI/model/cache policies.
7. `engine/render/Renderer3DModels.cpp`: preserve both GPU material tickets/reset and main's cache/rolling-history cleanup; include VT group compatibility checks and main's CPU MDI profiler. For per-command GPU ICB, preserve VT/PBR snapshots, pipeline/material checks, resource tickets and encoded variants **while reusing main's persistent** `gpuIcbSourceSlotsScratch_` and `gpuIcbInstancesScratch_` instead of the old removed `sourceSlots`/`instanceRecords` local arrays.

`skills/catalog.json` was not in the supplied archive: the user had already staged a regenerated catalog before exporting these seven remaining unresolved files. Leave it untouched, and run `python3 tools/skills/catalog.py check` after applying.

## Verification that actually occurred

- All seven conflict-marker blocks were parsed from the provided exact `merged` file snapshots and resolved using the corresponding `stage 2` / `stage 3` content.
- Confirmed no `<<<<<<<` / `>>>>>>>` markers remain in generated files.
- Performed static name/structure checks, including retention of both Vulkan API methods, NEREID + VT renderer entrypoints, main's scratch arrays and Texture RHI material resource tickets.
- **No CMake, compiler, CTest, native Metal GPU, Vulkan GPU, WebGL browser or complete source tree build was run.** Full compile compatibility and runtime semantics remain unverified. In particular the huge Renderer3D model pipeline received automatically merged changes outside the conflict hunks that should be reviewed on a full build.
- Previous Mac Studio `22 portable / 7 native Metal / full_engine PASS` manifest applies only to the **pre-merge SHA** `9145e6ef...`.

## Next step

1. Apply the resolved seven files and this handoff in the isolated local worktree with `apply_resolved.sh`, which refuses wrong branch/no MERGE_HEAD/unmerged input mismatch, backs up originals and stages files.
2. Confirm `git diff --name-only --diff-filter=U` empty, `git diff --cached --check` succeeds, and `python3 tools/skills/catalog.py check` / `python3 tools/skills/audit.py --strict` succeed.
3. Configure and build actual merged source: `cmake -S . -B build/merge-texture -DAIVERSE_BUILD_TESTS=ON`, `cmake --build build/merge-texture --parallel 4`; fix compile errors **in the integration worktree**, especially renderer and Vulkan interfaces. Run `ctest --test-dir build/merge-texture --output-on-failure`.
4. When code and tests pass, commit the local merge and push only `integration/texture-rhi-main-20261120`; do not alter main or original Texture RHI branch.
5. Rerun the complete Mac Studio Texture RHI Metal acceptance on the final merged SHA, plus appropriate Vulkan/GLES/WebGL gates. Record exact test logs/SHA and create a subsequent handoff with any corrections and actual results.
