# NEREID Stage 45 — authored CPU regression suite, not yet executed

**Date:** 2026-11-19.
**Branch:** `feature/nereid-water-core`, Draft PR #23.
**Base/parent:** `50f887b6a7508a09cb323f135901bbb7b79e866b`; main remains an ancestor.
**Stage commit:** code, CMake registration, CI runner and handoff share one atomic commit.

## Implemented

Created `tests/ecs_nereid_confluence_contract_test.cpp`, an independent CPU-only executable that exercises:

1. Static JSON water + `nereidConfluences` manifest. Valid IDs and mouth footprint, two upstream flows mixed into an outlet at expected Fixed coordinates; out-of-radius and non-outlet samples unchanged.
2. Authoring validation: duplicate junction ID; duplicate/fractional inlet water ID; self-link; missing/unreferenced water; impossible mouth; invalid radius; nonboolean enabled; disabled authoring entries suppressed.
3. Real `World` with three `WaterBodyComponent` entities and a `WaterConfluenceComponent`: `sampleWater` and `WaterQueryIndex::sample` match selected body, submersion, exact deterministic flow.
4. Read-only ECS snapshot `snapshotNereidEcsWater` + `snapshotNereidEcsConfluences` projected through `NereidConfluenceScene::flowForWater`: expected to reproduce authoritative query flow. Disabling junction must leave base body flow intact.
5. Stage 43 material key policy: foam quality/color/refraction are draw-only and keep the upload key stable; vertex-baked `foamStrength` invalidates the mesh upload key.

Updated `CMakeLists.txt` with `aiverse_ecs_nereid_confluence_contract_test` and CTest `ecs_nereid_confluence_contract_test`. Updated `tests/run_ecs_experiment_suite.sh` so the ECS experimental CI shell runner invokes this target with the existing `NEREID_CORE` sources. The new test does not create a graphics device or alter FPS/RTS/PHYSIS semantics.

## How to verify later

```sh
cmake -S . -B build/ecs-test -DAIVERSE_BUILD_TESTS=ON
cmake --build build/ecs-test --target aiverse_ecs_nereid_confluence_contract_test -j 4
ctest --test-dir build/ecs-test --output-on-failure -R '^ecs_nereid_confluence_contract_test$'
# Standalone experimental CI runner:
bash tests/run_ecs_experiment_suite.sh
```

Run GLSL/Metal compilation separately: this CPU regression only checks rendering key semantics, not shader GPU output.

## Actual verification and constraints

**Performed:** GitHub source inspection of CMake, core ECS examples, JSON mutability and renderer contract; authoring and CI registration committed to the remote branch. **NOT performed:** native C++ compile, CTest execution, GitHub Actions successful run, real graphics device, shader compiler, save/load and platform regression. As of stage start, observed actions for this branch were queued/stale on an older SHA; those do not prove this test passes.

If C++ compiler/CI exposes a failure, fix the actual issue before marking the PR ready. Prioritize include/standalone symbol problems, stable ID parity, Fixed rounding, manifest rejection behaviour and stale async mesh states.

**Next agent:** run this new contract test and the rest of `ecs_nereid_.*_test`, fix actual build/runtime failures, then run native ECS persistence+rollback, FPS/RTS/PHYSIS regression, GLSL/MSL compiler checks and GPU residency profiling. Append actual command, platform, exit status and commit to the handoff. Keep PR Draft until verified.


### Stage 45.1 — source-level link dependency repair

Inspected exact definitions: `worldInteger` and `worldVector` are implemented (not inline) in `engine/world/WorldAssets.cpp`. Both renderer contract and confluence contract include NEREID manifest parsing and therefore need those symbols. The existing CMake targets linked only `aiverse_engine` plus the NEREID header-only package, missing `aiverse_world` (which owns `WorldAssets.cpp`). Updated BOTH CMake test link libraries to include `aiverse_world`. The standalone `tests/run_ecs_experiment_suite.sh` now compiles `engine/world/WorldAssets.cpp` and `engine/pack/Sha256.cpp` for both tests; the latter resolves references from `WorldAssets::verifiedReader` even though the fixture itself does not call it.

**Actually performed:** read CMake targets, `modules/fps/CMakeLists.txt` world library ownership and WorldAssets/Sha256 source via GitHub connector; applied guarded source patch/atomic commit. **Not performed:** native compile/link, CTest, shell execution or green CI. Git clone of repo from the local container failed (GitHub hostname DNS resolution unavailable); observed GitHub Actions for old SHAs remained queued/cancelled. Do not claim test PASS. **Next:** run the two CPU contracts on a checkout with network/source, resolve any compiler/logic failures, then full suite.
