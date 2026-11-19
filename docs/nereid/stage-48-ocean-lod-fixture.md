# NEREID Stage 48 — large-world ocean LOD assertion consistency

**Date:** 2026-11-19
**Parent:** `caba95e19f5bc9d9b29dbe0f4393e2cfe511ed33`
**Branch:** `feature/nereid-water-core`, Draft PR #23
**Implementation commit:** this file is committed atomically with the test fix; see `git log -1`.



## Stage 48 — large-world ocean LOD fixture corrected (macOS CTest failure)

**Date:** 2026-11-19; branch `feature/nereid-water-core`; Draft PR #23; parent `caba95e19f5bc9d9b29dbe0f4393e2cfe511ed33`. **Actual report:** `ecs_nereid_renderer_contract_test` aborts with `large-world ocean patch vertices must be local and stable` during native CTest. Earlier JSON fixture errors are resolved far enough for the test to reach the large-world section.

**Root cause:** the assertion expected an unsubdivided four-vertex ocean patch; its `WaterSurfaceMeshOptions seaOptions` left `lod` at the default **Medium**. In `buildWaterSurfaceMesh`, Medium uses one round of triangle subdivision; only **Far** is the unsubdivided four-vertex path. The patch's precise double-precision coordinates, origins and GPU key are not implicated by this test's misleading message.

**Changes:** explicitly set `seaOptions.lod=WaterSurfaceLod::Far` before the four-vertex assertion. Added a separate regression case for `Medium`: CPU mesh must contain more than four vertices while retaining the expected -1 local X/Z and double-precision snapped key. No water mesh generation, ECS, RHI, shader, gameplay or resident-cache algorithms changed. Changed only `tests/ecs_nereid_renderer_contract_test.cpp`, this handoff and the NEREID documentation pointers.

**Verification actually performed:** inspected GitHub source of the test, `NereidSurfaceResident::prepare`, the shared water mesh LOD defaults, quad generation and `splitMesh`; guarded source edit and commit to connected GitHub. **NOT RUN:** native C++ compile, CTest rerun, Metal/GLSL shaders, GPU capture, performance or FPS/RTS/PHYSIS regressions. **Next:** pull the branch, rebuild `aiverse_ecs_nereid_renderer_contract_test`, execute targeted CTest, then full suite and investigate any subsequent failures. PR remains Draft.


## Exact changes

- `tests/ecs_nereid_renderer_contract_test.cpp`: Far-lod 4-vertex assertion + Medium-lod detailed/basis assertion
- `docs/nereid/HANDOFF.md`, `docs/nereid/ARCHITECTURE.md`, `skills/gameplay/aiverse-nereid-water/SKILL.md`: stage handoff, test contract, unverified state
- `docs/nereid/stage-48-ocean-lod-fixture.md`: this standalone handoff

## Re-run on Mac

```bash
cd ~/Projects/AIVERSE
git switch feature/nereid-water-core
git pull --ff-only origin feature/nereid-water-core
cmake --build build/nereid-test --target aiverse_ecs_nereid_renderer_contract_test --parallel 4
ctest --test-dir build/nereid-test --output-on-failure -R '^ecs_nereid_renderer_contract_test$'
```

Do not claim it passes until CTest output is observed. If green, rebuild changed tests and run the complete 118+ test suite.
