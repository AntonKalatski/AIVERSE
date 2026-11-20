# NEREID Stage 50 — proven macOS full green run; fix disabled Release checks

**Date:** 2026-11-19
**Working branch:** `feature/nereid-water-core`
**Confirmed user-tested commit:** `65346f91c675b7a7bdd8313fcce03c829a336db2`
**Implementation:** `8f426376ecb095017cefdd313cca5dd6c2d70186`
**Handoff:** this file is in a separate documentation commit; inspect `git log -1` for SHA.
**PR:** [#23](https://github.com/chromealex/AIVERSE/pull/23) (Draft)



## Stage 50 — observed full macOS green run, and Release-test warning repair

**Date:** 2026-11-19. **Branch:** `feature/nereid-water-core`; PR #23 remains Draft. **Tested and passing SHA:** `65346f91c675b7a7bdd8313fcce03c829a336db2`. **Test-correctness implementation SHA:** `8f426376ecb095017cefdd313cca5dd6c2d70186` (requires rerun). See independent handoff `docs/nereid/stage-50-release-assertion-correctness.md`.

**ACTUAL user-provided macOS test evidence (on SHA `65346f91`):** `context.txt` confirms full-mode Release run on Mac Studio. `configure(5).log`: CMake configure/generate succeeded. `build-all.log`: entire build succeeded, ending `[100%] Built target aiverse_editor`. `ctest-focused.log`: **16/16 PASS**, including NEREID ECS, surface, renderer, confluence, SpectraFX and skills. `ctest-full.log`: **118/118 PASS, 0 failed, 268.32 sec**. `skill-catalog.log`: validated **219 skills**. `skill-routing.log`: passed. `skill-audit.log`: **0 errors / 0 warnings**. `summary(5).txt`: every one of seven stages returned exit=0. The logs were supplied by the owner; no need to infer CI success from pending GitHub jobs.

**Warning/NOTE review:** `build-all.log` contains exactly **18 Clang warnings** (`-Wunused-variable`, `-Wunused-parameter`, `-Wunused-but-set-variable`) and **3 Clang notes** (template/lambda instantiation context, not independent defects) across six **test** translation units: `ecs_scene_hierarchy_simulation_test.cpp`, `ecs_guarded_failure_test.cpp`, `ecs_simulation_bridge_test.cpp`, `ecs_cross_genre_mechanics_test.cpp`, `ecs_projectile_composition_test.cpp` and `ecs_cow_rollback_session_test.cpp`. Cause: these tests used standard `assert(...)`, which is compiled away under `NDEBUG`/Release; variables only inspected inside assertions then trigger unused warnings and important test contracts were **not actually checked in Release**. One projectile fixture was constructed inside `assert(collision_.add(...))` and also ceased to execute in Release. These diagnostics should not be hidden by suppressing compiler warnings.

**Correctness repair (SHA `8f426376ecb095017cefdd313cca5dd6c2d70186`):** added `tests/AlwaysCheck.h` with `AIVERSE_TEST_CHECK(expression)` that evaluates exactly once and throws a diagnostic `std::runtime_error` containing source file, line and failed expression, regardless of `NDEBUG`. Converted all **44** standard `assert()` calls in the six above-mentioned test files; eliminated each `<cassert>` include in those files. This now executes formerly-elided collision fixture creation and verifies cross-genre checksum, rollback and ECS invariants in real Release builds. **No engine, ECS, NEREID, VFX or shipping runtime code changed.**

**Actually run after code change:** an isolated smoke program reproducing the `AIVERSE_TEST_CHECK` helper was built and executed with GCC 14.2 `-std=c++20 -Wall -Wextra -Werror -pedantic` in both **`-DNDEBUG -O2` and `-O0`** modes: **both PASS**. This verifies the helper's basic Release/Debug semantics, **not** the six edited AIVERSE test files. Source review verified 44 conversions and their executable expressions. **NOT RUN after SHA `8f426376ecb095017cefdd313cca5dd6c2d70186`:** AIVERSE's macOS/CMake compilation, CTest, GPU shaders, real Metal quality/performance or platform matrix. The 118/118 run applies to the **previous** SHA, not this follow-up commit.

**Next:** pull the new branch SHA, run `AIVERSE_NEREID_BUILD_DIR=build/nereid-test AIVERSE_NEREID_JOBS=4 bash tools/nereid/validate.sh --full` again to prove that all formerly-disabled checks pass. Revisit any failures as genuine Release test regressions instead of weakening/removing checks. Once CPU suite is green on the new head, gather real Metal/GLSL/GPU evidence before PR Ready.


## Changed files at implementation stage

- Added `tests/AlwaysCheck.h` (always-on test helper).
- Updated `tests/ecs_scene_hierarchy_simulation_test.cpp` (6 checks).
- Updated `tests/ecs_guarded_failure_test.cpp` (4 checks).
- Updated `tests/ecs_simulation_bridge_test.cpp` (10 checks).
- Updated `tests/ecs_cross_genre_mechanics_test.cpp` (9 checks).
- Updated `tests/ecs_projectile_composition_test.cpp` (10 checks).
- Updated `tests/ecs_cow_rollback_session_test.cpp` (5 checks).
- Documentation: `AGENTS.md`, `docs/nereid/HANDOFF.md`, `docs/nereid/ARCHITECTURE.md`, `skills/gameplay/aiverse-nereid-water/SKILL.md`, this handoff.

## Exact reproduction / next owner action

```sh
cd ~/Projects/AIVERSE
git switch feature/nereid-water-core
git pull --ff-only origin feature/nereid-water-core
AIVERSE_NEREID_BUILD_DIR=build/nereid-test AIVERSE_NEREID_JOBS=4 \
  bash tools/nereid/validate.sh --full
```

Read `build/nereid-test/nereid-logs/{summary.txt,build-all.log,ctest-focused.log,ctest-full.log}`. Release-mode assertions now execute, so a newly failing test may expose a previously hidden real bug; fix the behavior or expectation based on evidence. Do not suppress warnings or put back `assert()`. If CPU tests pass, proceed to GPU optical and performance acceptance.

## Isolation

This stage does not change binary serialization, runtime C++ libraries, gameplay ownership, public NEREID components or render API. It only strengthens regression tests and publishes the observed macOS results. Existing smoke/CTest are not substitutes for graphics measurements.
