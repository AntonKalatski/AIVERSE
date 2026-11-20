# AIVERSE Stage 115 — exact-source Release tests PASS; main CI still compiling

Date: 2026-11-20
Repository: chromealex/AIVERSE
Base main SHA: `ad175f624ef6f25e7c6dc19cabe199cb30623201`
Handoff branch: `docs/aiverse-stage115-release-retirement-validation-20261120`
Previous handoff: docs/handoffs/aiverse-stage-114-release-retirement-tests.md

## Context
PR #76 merged Stage 114 on 2026-11-20:
https://github.com/chromealex/AIVERSE/pull/76
Replaced 14 + 24 standard assertions in retirement queue regression
tests with always-executed `AIVERSE_TEST_CHECK` under `-DNDEBUG`.
The first Linux ARM64 compile failure on preceding SHA was
`-Werror=unused-but-set-variable` in these two test files.

## What was verified in Stage 115
Fetched each of the following exact files from main through the
connected GitHub repository source and compared the local materialized
source against its Git blob SHA via `git hash-object`, **all five match**:

| File | Git blob SHA |
|---|---|
| tests/AlwaysCheck.h | c2dda60e186ad62be400ee6949fcbaab88abecdd |
| tests/render/resource_retirement.cpp | c6066baac88365ac47460d6a3bddf41a67259102 |
| tests/render/async_resource_retirement.cpp | 043f378f0d19e74047d388c4b6f8f19b202894f1 |
| engine/render/ResourceRetirementQueue.h | 197ba1233d57f037a4b302fc87856054882adfd8 |
| engine/render/AsyncResourceRetirementQueue.h | df8398cec5e0ad36346c4c8f1cb10fe4960deaca |

On local Debian GCC 14.2, both `main` source tests were built
with flags matching key Linux Release CI flags:
```bash
c++ -std=c++20 -O3 -DNDEBUG -Wall -Wextra -Wpedantic -Werror \
    -I. tests/render/resource_retirement.cpp -o /tmp/resource_retirement_stage115
/tmp/resource_retirement_stage115
c++ -std=c++20 -O3 -DNDEBUG -Wall -Wextra -Wpedantic -Werror \
    -I. tests/render/async_resource_retirement.cpp -o /tmp/async_resource_retirement_stage115
/tmp/async_resource_retirement_stage115
```
Both compilation and execution: **PASS (exit code 0)**.

## Main CI / remaining acceptance
- Actual GitHub main CI run:
  https://github.com/chromealex/AIVERSE/actions/runs/44725283646
- At Stage 115 inspection Linux ARM64 GCC had completed checkout,
  dependencies and Configure; Build was still in progress.
- 11 other main CI jobs still queued.
- An old full CTest run #44709805590 was still queued but targets
  `fc42156b41129f15b48c8b9e8626579eb2c70db0`, **not current main**.
- Full AIVERSE build, complete CTest, Metal GPU, Vulkan and GLES
  runtime parity **NOT VERIFIED** at Stage 115.

## Next steps
1. Wait for exact CI result on main `ad175f624ef6f25e7c6dc19cabe199cb30623201` and inspect first
   failing job/line, if any. Do not invent success.
2. If further errors arise, use a narrow feature branch, include
   next detailed stage handoff and merge after verifying head/base.
3. Once native compilation unblocked, run entire CTest on current
   main, including non-skipped Metal GPU cases on supported Mac and
   Vulkan/GLES targeted backend tests.
4. This is a documentation-only handoff branch so that publishing
   the Stage 115 record does NOT change main and needlessly cancel
   its in-progress CI (the main CI workflow cancels previous main
   runs on every push).
