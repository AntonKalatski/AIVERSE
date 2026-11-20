# AIVERSE Stage 9 — full native CTest 127/129: temporary-filesystem hardening

Date: 2026-11-19
Repository: `chromealex/AIVERSE`
Branch: `feature/ecs-static-prefab-scene-integration`
Previous: `docs/agent-handoffs/ecs-static-scene-stage8-2026-11-19.md`

**Status: FULL native build/CTest and all 9 process-isolated RSS
benchmarks passed on Mac Studio after the Stage 9 environment fix.**
User-reported full CTest time: 517.25 seconds; final script status:
`SUCCESS: complete all run`. The rerun excerpt does not print the
CTest aggregate test-count line or exact `git rev-parse HEAD`; no source
commit after `2f119fefdadd06c5053c5f2a2e3eb68e5353f3e0` was required.
See measured results below.

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

## Successful full native rerun and RSS measurements — 2026-11-19

After pulling the Stage 9 fixes, the user supplied the end of the
one-shot validation output. It shows:

```text
Label Time Summary:
SpectraFX = 0.62 sec*proc (3 tests)
Total Test time (real) = 517.25 sec
...
RSS samples validated: 9
SUCCESS: complete all run
```

Since `tools/ecs-static-scene-validate.sh` runs the full native build
and the complete registered CTest under `set -euo pipefail`, and only
executes RSS after successful CTest, the final SUCCESS is evidence
that the complete **default build and CTest completed with exit 0**.
The prior run contained 129 tests. The rerun excerpt does not display
its own aggregate `100% tests passed` line, so avoid independently
asserting an exact rerun count without viewing its full log.

- Native project / platform: user Mac Studio, source worktree
  `~/Projects/AIVERSE-static-scene`, Release script default.
- Full CTest elapsed: **517.25 sec**, zero failures inferred from
  the success-gated script.
- All 9 RSS samples: JSON lines validated, no checksum differences.
- Full build/test log in user's worktree:
  `build/static-scene/verification/ecs-static-scene-all.log`.
- RSS output in user's worktree:
  `build/static-scene/verification/ecs-static-scene-rss.jsonl`.
- The tool output does not contain current `git rev-parse HEAD`;
  last confirmed branch source HEAD before this documentation
  update was `2f119fefdadd06c5053c5f2a2e3eb68e5353f3e0`.

### Original user-provided process measurements

Each row represents a separate process constructing **1,000,000
Entities** with a **128-byte configuration type**. Delta RSS is
`rss_after_build_bytes - rss_baseline_bytes`, which includes ECS
allocation overhead, entity bookkeeping and process allocator effects;
it is not simply the payload memory footprint.

| Unique | Mode | Baseline RSS bytes | After build RSS bytes | RSS delta bytes | Median read ms | Checksum |
| ---: | --- | ---: | ---: | ---: | ---: | ---: |
| 16 | ordinary | 4296704 | 290320384 | 286023680 | 1.733709 | 991000000 |
| 16 | shared | 4296704 | 42299392 | 38002688 | 0.716334 | 991000000 |
| 16 | static | 4337664 | 42303488 | 37965824 | 0.671584 | 991000000 |
| 64 | ordinary | 4296704 | 294334464 | 290037760 | 1.666042 | 4063000000 |
| 64 | shared | 4296704 | 44969984 | 40673280 | 0.724584 | 4063000000 |
| 64 | static | 4296704 | 44949504 | 40652800 | 0.669459 | 4063000000 |
| 1024 | ordinary | 4296704 | 298401792 | 294105088 | 1.694500 | 65486484928 |
| 1024 | shared | 4296704 | 46493696 | 42196992 | 1.288292 | 65486484928 |
| 1024 | static | 4325376 | 45273088 | 40947712 | 0.807083 | 65486484928 |

All user-reported `rss_after_reads_bytes` values were also measured.
They were only 4–12 KiB above after-build RSS; see the original
`rss.jsonl` on the user's machine for all exact values.

### Derived comparisons to ordinary storage

| Unique | Ordinary delta MiB | Shared delta MiB | Static delta MiB | Static RSS saved | Static read speedup |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 16 | 272.77 | 36.24 | 36.21 | 86.73% | 2.58× |
| 64 | 276.60 | 38.79 | 38.77 | 85.98% | 2.49× |
| 1024 | 280.48 | 40.24 | 39.05 | 86.08% | 2.10× |

Shared's measured RSS savings relative to ordinary were
86.71%, 85.98% and 85.65% respectively. Its read speedups were
2.42×, 2.30× and 1.32×.

**Conclusion bounded to the benchmark:** retaining one large
immutable configuration table and per-Entity 4-byte references
reduces **measured process RSS growth by approximately 86%** and
improves this two-field read pass by ~2.1–2.6× for Static.
The comparison is a single observed Mac run, without confidence
intervals or multi-run variance control, and does not directly
represent an entire FPS/RTS level's working set or GPU memory.

### Follow-up after successful gate

The original failure in `skill_library_isolation` (macOS CMake ENOTTY
under system temporary folder) and the previously uncharacterized
`aiverse_gameplay_tools` failure were **not present as blockers in
this successful gated rerun**. The exact initial gameplay-tools
traceback is still unknown, so credit the reproducible rerun rather
than overclaiming proof that its previous cause was solely TMPDIR.

Before any main merge, check current `main` divergence, ensure
no concurrent agents modified these sources, and run any additional
configuration-specific GPU/physics/integration tests required by the
target platform. The default CTest gate does not automatically cover
all optional Metal GPU settings, production scenes and multi-process
network parity.

## Remaining risks

- The successful rerun confirms the default full gate now passes.
  The initial gameplay_tools traceback was not supplied, so the exact
  root cause of its earlier failure is not established separately.
- Full static-scene integration, COW, FPS/RTS and collision tests
  are only as verified as the reported CTest summary; platform
  variants, GPU/Metal, RSS and cross-scene terrain seam behavior
  remain independent test gates.
- The default full gate and measured RSS have now succeeded;
  exact checked-out SHA on the user's Mac was not printed in the
  supplied excerpt. Source review, any recent main divergence and
  production-specific optional test gates still precede main merge.
