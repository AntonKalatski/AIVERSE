# TerraWeave Stage70 — current main ancestry + Mac 181/181 native acceptance

**Date:** 2026-11-20.
**Branch:** `integration/terraweave-physis-stage70-20261120`.
**Working PR:** [Draft #53](https://github.com/chromealex/AIVERSE/pull/53).
**Verified stage base HEAD (before this docs-only commit):** `f55d8d4e8622d297c4c27266f9289f82cc59baed`.
**Current main:** `e4129c5062a6ab47e7c70f3fdd483392fde1f679`.

## Branch-integrity check — NO MERGE REQUIRED

GitHub compare results directly against both refs:

```text
base = main; head = integration/terraweave-physis-stage70-20261120:
  status=ahead; ahead_by=126; behind_by=0
  merge_base=e4129c5062a6ab47e7c70f3fdd483392fde1f679

base = integration/terraweave-physis-stage70-20261120; head = main:
  status=behind; ahead_by=0; behind_by=126
  merge_base=e4129c5062a6ab47e7c70f3fdd483392fde1f679
```

**Conclusion:** Main's exact latest commit is already a Git ancestor of
the TerraWeave integration HEAD. No missing main commit, merge conflict
or source synchronization remains. A synthetic merge would not bring
new changes and was intentionally NOT created. Do not touch main or
feature branches. Prior genuine two-parent PHYSIS/Stage70 merge is
`97b4e165cae1ab74b907ba2b934665a530715432`.

## Real native Mac Studio acceptance reported by user

The isolated checkout was
`~/Projects/AIVERSE-terraweave-stage70-local-qa`.
The user's latest complete terminal summary says:

```text
179/181 ... meta_liveops_schema ...... Passed
180/181 Test #34: aiverse_tests ...... Passed 858.94 sec
181/181 Test #110: place ............ Passed 27.51 sec
100% tests passed, 0 tests failed out of 181
Total Test time (real) = 886.46 sec
PASS: merged native/editor/Box3D CTest
```

The acceptance script output points to
`build/terraweave-main-acceptance/acceptance-reports`.
The previously observed 180/181 Mac failure was
`aiverse_tests` aborting on an FBX fixture with
`AIVERSE_FBX=OFF`; source commit
`92758e07a8edbb6d14f47787b3729f6cff1390a1`
corrected that contract. The post-fix monolithic test was also
independently reported PASS (851.30 sec). The full user-reported
**181/181** run proves this registration set executed without CTest
failures in that native Mac build. It does not prove GPU correctness.

**Provenance limitation:** The supplied terminal excerpt does NOT
include `provenance.txt` or the printed `git rev-parse HEAD`, so
an exact source SHA for the full 181/181 result is not independently
established. The repository's source at the stage HEAD has been
re-read on GitHub for the FBX-off, micro nutrient, 65k grid guard and
native test registration. There has been no new code fetched from main.

## Read-only source contract review at stage HEAD

- `CMakeLists.txt` registers both
  `aiverse_terraweave_micro_nutrient_test` and
  `aiverse_terraweave_native_micro_compile_test`.
- `tests/terraweave_micro_nutrient_test.cpp` contains bounded
  65,536-cell regression (`capacitySettings`).
- `engine/world/TerraWeaveNereidMicroNutrients.h` checks bounded
  native NEREID micro search settings.
- `tests/tests.cpp` registers
  `models_fbx_disabled_gives_actionable_error`, while CMake
  propagates `AIVERSE_NO_FBX=1` to `aiverse_tests` when disabled.
- `.github/workflows/terraweave-main-integration-qa.yml`
  performs a complete unfiltered native CTest as a separate
  GitHub CI acceptance gate.
This is a source review, not independent native execution.

## Remaining gates

1. From the local QA worktree collect exact
   `acceptance-reports/provenance.txt`, `cmake-cache-flags.txt`,
   and `ctest.xml`; compare their SHA/flags with the PR source.
2. Validate cross-system **live** TerraWeave + NEREID + PHYSIS +
   Box3D behavior under one ECS World: forest death, wind/fall,
   sediment and nutrient movement, NEREID body identity,
   save/replay/rollback and sustained resource accounting.
3. Run Metal/Vulkan/GLES RHI-specific integration and screenshot/
   GPU parity checks. Their validation is not implied by 181 CPU CTests.
4. Check latest GitHub Actions against current HEAD, including
   Linux GCC/Clang, editors and GPU where available; don't equate
   QUEUED or CANCELLED with PASSED.
5. Merge Draft PR #53 into main only after those checks and
   explicit user approval. Keep per-stage agent handoffs complete.

**Actions in this stage:** GitHub ancestry and source checks plus a
docs-only QA handoff. **No source merge or gameplay code edits.**
