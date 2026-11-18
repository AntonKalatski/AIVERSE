# Unified test runner — living agent handoff

Working branch: `feature/universal-entity-inspector-fps`, PR #7. This handoff is additive and must
not overwrite other agents' entries in the shared branch.

## Stage 1 — Unified desktop runner and extras registry (2026-11-18)

- **Implementation commit:** `a2d9a428c5c01e177473962eb534ab654bc75a64`.
- **Files:** `run-tests.sh` (executable, repository-root entry point),
  `tests/extra-test-suites.txt` (registration manifest),
  `docs/testing.md` (usage/agent contract).
- **Behavior:** `./run-tests.sh` creates/reuses `build/all-tests`, configures
  a desktop CMake build with testing/editor/tools/server/headless enabled,
  builds all default targets (not just `aiverse_engine`), runs every CTest
  without a hand-maintained `-R` selector, and then runs explicitly
  registered standalone checks in deterministic manifest order.
- **Registered extras at this stage:** standalone ECS experiment suite,
  ECS ASan/UBSan suite, full real RTS/FPS ON/OFF parity script,
  third-party notices, README translations and skill-catalog verification.
  Future additions belong in CMake `add_test` or the manifest.
- **Failure reporting:** configure/build failures stop before CTest; test
  failures are collected and reported with a nonzero exit. Each stage writes
  its own full log to a timestamped `build/all-tests/test-reports/` folder,
  with `summary.txt` and CTest JUnit XML.
- **Compatibility:** no game runtime, ECS, editor, network protocol or save
  format changes. User's existing `build/ecs-test` is untouched unless
  explicitly supplied through `--build-dir`. Requires Bash 3.2+, CMake/CTest
  and Python 3 for registered Python suites.
- **Validation actually executed:** static manifest structure checked at
  creation (6 records, four columns), source pushed to GitHub. Attempted
  `curl` of the published runner for a local Bash syntax run, but container
  reported `Could not resolve host: raw.githubusercontent.com`.
  Therefore native Bash syntax, CMake, full build, CTest, sanitizer and parity:
  **NOT RUN** in the current environment. Do not claim passing results.
- **Next agent:** verify `bash -n run-tests.sh` on the local checkout and
  run `./run-tests.sh --quick` first; then `./run-tests.sh` for the full
  manifest. Investigate errors using the new build/test logs and update this
  handoff with the actual tested SHA, failure/passing stages, and next fix.

## Mandatory continuation template

```text
### YYYY-MM-DD — <one bounded change>
Branch / implementation SHA:
Files changed:
Test registration (CTest or extras manifest):
Actual commands and PASS/FAIL/NOT RUN:
Report directory and first error:
Compatibility / limitations:
Next agent action:
Handoff commit SHA:
```

Each implementation stage is followed by a separate handoff commit. If
another agent updates the document concurrently, fetch the latest shared
HEAD and append; do not force-push or replace their entries.
