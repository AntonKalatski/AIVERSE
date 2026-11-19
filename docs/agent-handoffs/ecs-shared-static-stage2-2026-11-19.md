# AIVERSE Shared/Static ECS — Stage 2 COW Release validation handoff

Date: 2026-11-19  
Repository: `chromealex/AIVERSE`  
Branch: `feature/ecs-shared-static-components`  
Status: **warning/assert correctness fix committed; corrected native COW tests NOT rerun**.

## Why this stage exists

A Mac Studio Release build (AppleClang 17.0.0.17000604) reported four
CTest passes, including two COW tests that emitted eight warnings.
Inspection confirmed ordinary `assert()` was used throughout:
`NDEBUG` removes those checks in Release. Most critically
`for(unsigned workers:{1u,3u,7u})assert(exercise(workers)==expected)`
never called `exercise` for those workers, so the parallel parity
cases were not tested at all. These earlier green COW results are
**provisional**. No core engine or gameplay behaviour was modified.

## User-provided results BEFORE fixing tests

- Working tree: `~/Projects/AIVERSE-shared-static`.
- Git SHA: `730d22cf80a9d4f8729080328b0ab8be69705464`.
- Compiler: AppleClang 17.0.0.17000604, Release.
- `ecs_scene_hierarchy_test`: Passed 0.91 s (valid custom CHECK).
- `ecs_scene_hierarchy_simulation_test`: Passed 0.37 s
  (valid AIVERSE_TEST_CHECK).
- `ecs_cow_snapshot_test`: CTest Passed 0.32 s, **not credible for
  invariants** under `NDEBUG`; 5 unused-argument warnings.
- `ecs_cow_parallel_test`: CTest Passed 0.33 s, **not credible for
  parallel parity** under `NDEBUG`; 3 unused-variable warnings,
  worker cases 1/3/7 skipped.
- Aggregate: 4/4 reported Passed, 1.94 s total. No actual failures
  were reported, but Release COW correctness remains unproven.
- The separate new `ecs_shared_static_components_test` had already
  passed its 1/1 target on `11568cd16` with valid always-on checks,
  and its performance microbenchmark is unaffected.

## Changed files and commits

1. `tests/ecs_cow_snapshot_test.cpp`: switched `#include <cassert>`
   to `#include "tests/AlwaysCheck.h"`, replaced 28 occurrences of
   `assert(...)` with `AIVERSE_TEST_CHECK(...)`. All checks execute
   in Debug/Release and cannot suppress test side effects.
   Commit `f858af26b90939fc484adf50cdfe3d28e5325330`.
2. `tests/ecs_cow_parallel_test.cpp`: same change for 5 `assert`
   occurrences, including the loop over worker counts. Worker
   configurations are now actually executed in Release.
   Commit `8b89d5841ea5e7630a86c243134da9b451ddd5ea`.
3. `docs/agent-handoffs/ecs-shared-static-stage1-2026-11-19.md`:
   repaired an earlier duplicated/broken Markdown validation block;
   clearly distinguished valid test results from provisional COW passes.
   Commit `50c4de7dec7788d0841f923b578499bb6c10e709`.
4. `docs/ecs-shared-static-components.md`: recorded COW warning cause
   and native revalidation requirement.
   Commit `ad4e41e2ee9ba2814cdcb43046ccf6da6a79fcf8`.

## First action for any next agent

Use the **existing** isolated worktree. Do not create another worktree or
modify the user's main AIVERSE checkout:

```sh
cd ~/Projects/AIVERSE-shared-static
git fetch origin feature/ecs-shared-static-components
git pull --ff-only origin feature/ecs-shared-static-components
cmake --build build/shared-static --target \
  aiverse_ecs_cow_snapshot_test \
  aiverse_ecs_cow_parallel_test \
  aiverse_ecs_scene_hierarchy_test \
  aiverse_ecs_scene_hierarchy_simulation_test --parallel 8
ctest --test-dir build/shared-static --output-on-failure \
  -R '^(ecs_cow_snapshot_test|ecs_cow_parallel_test|ecs_scene_hierarchy_test|ecs_scene_hierarchy_simulation_test)$'
```

All four should compile without the earlier 5+3 unused warnings; the
Release COW assertions are now real, so an actual failing invariant
must be investigated/fixed, **not** reverted to `assert`. Record the
actual CTest results, warnings, compiler, exact git SHA, and next step
in this document when logs are available. Then run broader native
COW/ECS replay/snapshot, FPS/RTS parity and RSS tests before main.

## Architecture preserved

- Production `engine/core/ECS.h` Sparse Set and
  `engine/sim/Simulation.cpp` are unchanged.
- Shared/Static feature implementation in
  `engine/sim/SharedStaticComponents.h` and its v10 persistence
  helper are unchanged.
- No scene systems, renderer or genre modules were modified.
- The next feature milestone remains StaticRef configuration reuse
  across SceneAsset prefab instances, coordinated with
  `feature/scene-runtime-multiworld` only after both branches pass
  their native regression suites.
