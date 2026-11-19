# AIVERSE Stage 9 — full native CTest 127/129: temporary-filesystem hardening

Date: 2026-11-19
Repository: `chromealex/AIVERSE`
Branch: `feature/ecs-static-prefab-scene-integration`
Previous: `docs/agent-handoffs/ecs-static-scene-stage8-2026-11-19.md`

**Status: 127/129 tests passed in the user-provided full native
CTest run (268.20 seconds). Two failures remain to be rechecked.
The source correction is committed but NOT NATIVE-RETESTED.**

## User-supplied native validation — Mac Studio

The user ran the full CTest suite using
`~/Projects/AIVERSE-static-scene/build/static-scene`.
The reported result was:

```text
98% tests passed, 2 tests failed out of 129
Total Test time (real) = 268.20 sec
The following tests FAILED:
  57 - aiverse_gameplay_tools (Failed)
 116 - skill_library_isolation (Failed)
```

The previously failing `skill_catalog`/`skill_routing` tests are
NOT in this failure list after Stage 8's canonical catalog
registration fix. Among tests 117-129, the user showed passes for
`uiwindows_audio_importer`, `rules_files`,
`vfx_graph_compiler`, `vfx_ecs_presentation`,
`vfx_graph_assets`, `aiverse_grpc_tests`,
`aiverse_protoc`, `fps`, `fps_transport`,
`world_streaming`, `world_async`, `voxel_game`,
and `voxel_server`. The full CTest summary identifies exactly
two failing test names. The user excerpt does not show the RSS
JSONL output; the one-shot script is expected to abort before
RSS if CTest exits nonzero.

### Failure 1: skill_library_isolation — observed root cause

The traceback is from `tests/skills/isolation.py`, while executing
a nested `cmake -S <TemporaryDirectory> -B <TemporaryDirectory>/build`.
A temporary CMake configure attempted:

```text
/var/folders/.../T/aiverse-selection-.../build/
  CMakeFiles/3.27.8/CMakeSystem.cmake.tmp
CMake Error: System Error: Inappropriate ioctl for device
```

AppleClang 17 was identified, but the nested test project had
not finished configuring yet. This is a temporary filesystem /
host-environment error, not evidence of a failed meta symbol
isolation check. Do not weaken the symbol and archive assertions.

### Failure 2: aiverse_gameplay_tools — exact cause unknown

The user supplied the CTest failure *name* but not its own detailed
unittest traceback. The Python script `tests/libraries/gameplay_tools.py`
uses `tempfile.TemporaryDirectory` for project copies/fixtures and
can optionally select `AIVERSE_HEADLESS` or a legacy build/.temp
headless binary. The underlying failure **has not been identified**
from the provided excerpt. Do not assert that it was definitely
caused by the same macOS temporary filesystem issue. The correction
below removes two environmental sources of nondeterminism while
preserving all gameplay validation assertions.

## Stage 9 changes

`CMakeLists.txt` (one source commit):

- `file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/aiverse-test-tmp")`
  creates a stable writable test temporary root in the known-good
  native build volume rather than relying on macOS `/var/folders`.
- `aiverse_gameplay_tools` receives
  `TMPDIR=${CMAKE_CURRENT_BINARY_DIR}/aiverse-test-tmp`.
  If the real `aiverse_headless` target is defined in the same
  CMake configuration, its CTest environment also receives
  `AIVERSE_HEADLESS=$<TARGET_FILE:aiverse_headless>`.
  This forces the fixture to validate the **current** worktree's
  executable instead of a stale shell setting or legacy binary.
  The full-project build must have completed before running CTest,
  as it did in the reported run.
- `skill_library_isolation` receives the same
  `TMPDIR=...` environment, retaining its existing
  `TIMEOUT 600` and all original policy/symbol isolation checks.
  Python `tempfile` honors `TMPDIR` in the test process and
  subsequent nested CMake builds operate on the writable project
  build filesystem.

Commit:
`a6c3f2956e27e2903f3620b0002ba6e99403f190`
— redirect native Python fixture temporary projects and bind
gameplay validation to the current headless target.

CMake is reconfigured by the existing one-shot validation script,
so the CTest test properties will be refreshed after `git pull`.
No other source C++ code, simulation, ECS, SceneRuntime or static
collision APIs were modified in this step.

## Validation performed by this agent

- Inspected exact CMake definitions for both CTests.
- Inspected `tests/skills/isolation.py`, which creates nested CMake
  projects via `tempfile.TemporaryDirectory` and retains strict
  archive/link-symbol assertions.
- Inspected `tests/libraries/gameplay_tools.py` and
  `tools/libraries/gameplay.py`, which also use temporary
  project directories and optional headless executable discovery.
- Confirmed `aiverse_headless` is a CMake target on native desktop
  builds when `AIVERSE_BUILD_HEADLESS=ON`.
- Source correction committed to GitHub connector.
- **No native post-fix CMake configure, CTest, or RSS test has
  been executed in this agent session.** Do not claim 129/129.

## Repeat full native gate (existing isolated worktree)

```sh
cd ~/Projects/AIVERSE-static-scene
git fetch origin feature/ecs-static-prefab-scene-integration
git pull --ff-only origin feature/ecs-static-prefab-scene-integration
git rev-parse HEAD
bash tools/ecs-static-scene-validate.sh build/static-scene all 8 1000000
```

The script configures Release, builds the complete default target
set, runs all registered CTests once, then performs the 9 independent
RSS samples only if all CTests pass.

If `aiverse_gameplay_tools` still fails, capture its **specific**
CTest output from
`build/static-scene/verification/ecs-static-scene-all.log`,
or this targeted diagnostic:

```sh
ctest --test-dir build/static-scene \
  -R '^(aiverse_gameplay_tools|skill_library_isolation)$' \
  -VV --output-on-failure
```

Then fix the *actual* failure; do not skip or weaken a test merely to
make the full suite green. If the isolation test still receives ENOTTY
inside the build volume, inspect filesystem permissions/space and
CMake/Apple system calls before considering a controlled one-time
retry with explicit diagnostics.

## Remaining risks

- Unknown gameplay_tools traceback until a full failing-test detail
  is provided; this environment hardening may not solve its
  underlying problem.
- Full static-scene integration, COW, FPS/RTS and collision tests
  are only as verified as the reported CTest summary; platform
  variants, GPU/Metal, RSS and cross-scene terrain seam behavior
  remain independent test gates.
- Do NOT merge to main until full suite succeeds and RSS results
  and exact source SHA are recorded in this handoff.
