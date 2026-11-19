# NEREID Stage 45.2 — compile repair for dependent World::get<T> calls

**Date:** 2026-11-19
**Branch:** `feature/nereid-water-core`, Draft PR #23
**Parent commit:** `f22b9242c66a6dc4994d169da4f6f6951e5e8fb4`
**Implementation commit:** this handoff is committed atomically alongside the code change; use `git log -1` to identify its SHA.

## Actual build failure supplied by user

On macOS, Clang fails to compile `tests/ecs_nereid_native_pipeline_test.cpp` during the complete NEREID branch build. Eight diagnostics on lines 61, 62, 77, 78, 80, 81, 86 and 87 report: `use 'template' keyword to treat 'get' as a dependent template name`.

## Fix and reasoning

The template function `exercise<Tag>()` invokes `get<T>()` through an expression involving a dependent caller. Clang requires explicit parsing of `get` as a member template. All eight instances are changed from `linear->world().get<T>(...)` and `indexed->world().get<T>(...)` to `linear->world().template get<T>(...)` and `indexed->world().template get<T>(...)`.

Changed file: `tests/ecs_nereid_native_pipeline_test.cpp` (eight disambiguators only). The ECS component types, test data, assertions, save/rollback semantics, water APIs and shipping engine remain unmodified.

## Verification

- **Actual observed before fix:** user-reported macOS Clang compile error in eight locations.
- **Source verification performed:** fetched head file, counted exactly eight dependent-member calls, replaced and verified none of the old forms remain; committed through connected GitHub.
- **NOT run after fix:** macOS/CMake compile, CTest, GPU shader checks or FPS/RTS regression. Passing behavior is not yet established.

## Next action

```sh
cd ~/Projects/AIVERSE
git fetch origin feature/nereid-water-core
git switch feature/nereid-water-core
git pull --ff-only origin feature/nereid-water-core
cmake --build build/nereid-test --parallel 4
ctest --test-dir build/nereid-test --output-on-failure --parallel 4
```

Record any new first compiler error and append actual test results to a subsequent handoff. Keep PR #23 Draft until native builds and regressions are green.
