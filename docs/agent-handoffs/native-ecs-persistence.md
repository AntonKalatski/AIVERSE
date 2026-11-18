# Native ECS persistence — agent handoff (living log)

Working branch: `feature/universal-entity-inspector-fps`, PR #7.
Owner: whichever agent holds the **next** bounded stage. Do not force-push or
overwrite changes in the continuously updated FPS/editor branch.

## Architecture and compatibility

- `engine/sim/PersistentEcs.h`: per-`Simulation` registry with stable
  namespaced IDs (not runtime-dependent component type IDs), schema version,
  component-pool and singleton-resource codecs and checksum.
- `engine/sim/Simulation.h/.cpp`: setup-time registration, world snapshots,
  checksum, versioned byte persistence and rollback on malformed disk loads.
- `modules/rts/RtsSave.cpp`: counts engine's fixed pools plus **only** the
  registered native ECS pools. Unregistered pools still fail saves.
- `engine/core/Archive.h`: portable field-by-field encoding, streaming
  checksum and bounded binary blocks. No raw C++ structure dumps.
- No registered native components/resources: preserves previous v9 format and
  checksum contract. With registered native types: v10. Old v9 cannot silently
  load into a simulation requiring persistent native types.
- COW `World` snapshots already contain registered pools/resources; portable
  disk saves need the explicit registry. Module-specific private side effects
  remain the module's responsibility; derived caches rebuild on restore.
- Native schema migrations are **explicit**, with old-version codec readers.
  Validation must include the recorded source checksum before accepting a
  migrated payload. Never turn mismatches into silent defaults.

## Completed stage 1 — Native ECS registry and v10 (2026-11-18)

- Code SHA: `b416dd4c4ab9a72d43d7d9d805c44e49c4161d62`.
- Files: `AGENTS.md`, `CMakeLists.txt`,
  `docs/editor-unification-handoff.md`,
  `docs/native-ecs-persistence.md`, `docs/native-modules.md`,
  `engine/core/Archive.h`, `engine/core/ECS.h`,
  `engine/sim/PersistentEcs.h`, `engine/sim/Simulation.cpp/.h`,
  `modules/rts/RtsSave.cpp`, `tests/ecs_native_persistence_test.cpp`,
  `tests/run_ecs_experiment_suite.sh`.
- Implemented: registration, deterministic ordered encode/hash, v10
  native-specific chunk, legacy v9 switch, COW ownership, fixed pool whitelist.
- Tests: CMake and standalone test entry **added**, but native compile/CTest
  and complete RTS/FPS/network tests **NOT RUN**.
- Outstanding: explicit schema upgrades, failed-load transaction safety,
  actual native-module integration tests.

## Completed stage 2 — Schema migration and failed-load recovery (2026-11-18)

- Code SHA: `acba58bf9f3c4d7cea69c039540f203aa99abbc3`.
- Files: `AGENTS.md`, `docs/editor-unification-handoff.md`,
  `docs/native-ecs-persistence.md`, `engine/sim/PersistentEcs.h`,
  `engine/sim/Simulation.h/.cpp`,
  `tests/ecs_native_persistence_test.cpp`.
- Implemented: per-type old-version component/resource decoder registration;
  original encoded-data checksum verification during migration; backup and
  restore of Simulation-owned state for failed disk loads; test-source
  expansion for v1→v2, re-save, simulated peer checksum parity and COW.
- Native C++ compilation, CTest, actual network socket tests, Linux/MSVC/macOS
  parity **NOT RUN** in this session. Agent handoff is evidence of source
  changes, not proof those assertions pass.

## Completed stage 3 — Mandatory per-stage handoff policy (2026-11-18)

- This entry and an `AGENTS.md` rule make handoff **mandatory after every**
  implementation, verification, troubleshooting, or documentation stage.
- Source verification: read current GitHub branch, prior code commits,
  source docs and CI statuses. No native compilation was possible:
  `git ls-remote https://github.com/chromealex/AIVERSE.git HEAD` returned
  `Could not resolve host: github.com` in available container.
- CI at inspection: latest PR #7 runs were queued, not green.
- No production readiness claimed.

## Next stage — required validation

1. Check out latest shared PR #7 HEAD (not an old feature SHA); configure a
   regular CMake build and run the focused native persistence CTest.
2. Run `bash tests/run_ecs_experiment_suite.sh`, including its sanitized
   invocation when supported.
3. Build an **actual native RTS project** with custom registered component
   and resource types; verify v10 round-trip and rollback, compare across
   two hosts and verify the unregistered-pool failure.
4. Test corrupted data and migrations, including original checksum validation.
5. Run FPS/RTS save/replay parity with no new component registrations to prove
   v9 compatibility. Add actual network transport / desync regressions.
6. Fix all errors in the same working branch and add an exact new handoff entry
   **before** starting another stage.

## Required handoff entry template

```text
### YYYY-MM-DD — <bounded stage>
Branch / PR / code SHA:
Files changed:
Implemented:
Compatibility and authoritative state ownership:
Tests actually executed (exact commands and PASS/FAIL/NOT RUN):
CI result links and statuses:
Known limitations / blockers:
Next agent action (one concrete reproducible step):
Handoff documentation SHA:
```

If several agents commit concurrently, refresh the latest document and append
your entry preserving everyone else's notes. Never copy older documentation
over a newer version. No green CI logs means no passing-CI claim.

## Completed stage 4 — NativeModule + actual RTS persistence regression (2026-11-18)

- **Implementation SHA:** `a10a9a87e468465eeacaed113f61b58cea46f007`, branch `feature/universal-entity-inspector-fps`, PR #7.
- **Changed files:** `tests/ecs_native_rts_integration_test.cpp` (new), `CMakeLists.txt` (CTest registration).
- **Behavior/ownership:** an actual `game::NativeModule` is injected through `game::RtsModule(std::vector<NativeDefinition>)`, not a synthetic GameModule. `NativeModule::setup` registers a custom `Counter` pool and `Ledger` resource and persists entity identity in `NativeStore`; its real tick updates both fields.
- **Coverage added:** two real RTS simulation peers with matching checksum ticks; v10 byte save/load and checksum; COW snapshot restoration and post-reload replay; rejection of unregistered rogue pool; unchanged v9 save when no native registration; corrupt checksum must reject and preserve live simulation state.
- **Compatibility:** no production runtime behavior or network protocol changed; only a test target registered for `AIVERSE_BUILD_TESTS`. The new test calls existing `RtsModule::serializeState` and built-in `NativeModules::serialize/hash`. `NativeStore` remains the module's explicit state and custom component/resource data are owned by registered ECS.
- **Checks actually executed:** inspected GitHub source and recorded CMake target/linking; `git ls-remote https://github.com/chromealex/AIVERSE.git HEAD` attempted in available local container and failed with `Could not resolve host: github.com`. Native CMake compilation, CTest, AddressSanitizer, real network: **NOT RUN**.
- **Latest CI at handoff:** CI: queued (https://github.com/chromealex/AIVERSE/actions/runs/44263451051); AIVERSE Studio validation: queued (https://github.com/chromealex/AIVERSE/actions/runs/44263451024); ECS 2.0 experimental checks: queued (https://github.com/chromealex/AIVERSE/actions/runs/44263451070); GPU batching focused regression: queued (https://github.com/chromealex/AIVERSE/actions/runs/44263451007).
- **Risks/limits:** compilation/linkage on macOS/Windows/Linux is unproven. The new test uses repository `templates/mushrooms` files; assert those paths exist at test runtime. COW fixture covers in-process snapshots, not an actual UDP session or cross-CPU determinism.
- **Next action:** in a full checkout run `cmake --build <build> --target aiverse_ecs_native_rts_integration_test` and `ctest --test-dir <build> -R '^ecs_native_(persistence|rts_integration)_test$' --output-on-failure`. Fix native diagnostics, append actual result and only then proceed with persisted schema corruption and network session tests.


## Completed stage 5 — Validate entity allocator and real RTS pool rows (2026-11-18)
- **Implementation SHA:** `d8937196965a79c60d0832590f6a04b5afb9ec89` on PR #7 `feature/universal-entity-inspector-fps`.
- **Files:** `engine/core/ECS.h`, `engine/sim/Simulation.cpp`, `modules/rts/RtsSave.cpp`, `tests/ecs_native_persistence_test.cpp`.
- **Change:** added `World::validEntityState` guard for bounded entity allocator size, matching generation/alive arrays, binary alive values and exactly-once free-list membership of dead slots. `Simulation::deserialize` rejects malformed allocators before touching running state. `RtsSave::ioPool` now fails on invalid Entity generation, destroyed handles or duplicate component row indices rather than inserting inconsistent sparse-set entries.
- **Tests added:** five invalid allocator shapes (column count, duplicate free slot, out-of-range free slot, live/free collision, nonboolean alive value); existing disk round-trip and COW tests remain registered.
- **Compatibility:** valid v4–v10 existing save layout and v9 checksum are unchanged. Files with logically invalid allocator/pool membership may now be rejected, intentionally. Bound is 16,777,216 entity slots. `pendingDestroy` may contain stale handles intentionally and is not rejected.
- **Verification:** code committed and source reviewed through GitHub. Native Linux/macOS/Windows compile, CTest and runtime: **NOT RUN** (local container cannot resolve github.com); current GitHub Actions results not confirmed green. Do not assert validated production safety.
- **Next action:** compile `aiverse_ecs_native_persistence_test` and `aiverse_ecs_native_rts_integration_test`, run `ctest --test-dir <build> -R 'ecs_native_(persistence|rts_integration)_test' --output-on-failure`; check prior legacy save fixtures and fuzz malformed RTS pool bytes. Fix failures, then commit the next handoff.


## Completed stage 6 — Enforce native persistence in ECS full gameplay CI (2026-11-18)
- **Code SHA:** `2f5163f0655b348ccaa0822f4c92949669b64405` on `feature/universal-entity-inspector-fps` (PR #7).
- **Changed path:** `tests/run_ecs_gameplay_compare.sh`.
- **Behavior:** `--full` now builds the two registered ECS persistence targets (`aiverse_ecs_native_persistence_test`, `aiverse_ecs_native_rts_integration_test`) in both OFF/ON experimental ECS configurations and requires CTest cases `ecs_native_persistence_test` and `ecs_native_rts_integration_test` to pass alongside real RTS/network cases.
- **CI wiring:** `.github/workflows/ecs-experiments.yml` already calls `bash tests/run_ecs_gameplay_compare.sh --full` in its Ubuntu `ecs-gameplay` job, and its change filters include this script. No other workflow settings altered.
- **Compatibility:** no game runtime, save or network behavior changed; this is a CI test-gate improvement.
- **Verification:** inspected the existing workflow invocation, target names and CMake registrations via GitHub. `bash -n` on the updated script, actual CMake/CTest, RTS runtime and CI success: **NOT RUN** in the current container (GitHub checkout not mountable, DNS for github.com unavailable). Running CI remains the acceptance gate.
- **Next:** obtain a completed `ecs-gameplay` CI run on PR #7; fix source/test failures with real logs; record the checked commit and PASS/FAIL result before declaring native ECS persistence production-ready.


## Validation checkpoint — user-reported Mac Studio ECS/FPS subset (2026-11-18)
- **Reporter:** project owner, local `/Users/aleksandrfeer/Projects/AIVERSE/build/ecs-test` checkout. Exact checkout commit SHA and full invocation were not included; **do not attribute these outcomes to a specific branch SHA without checking `git rev-parse HEAD`**.
- **Observed CTest:** `ecs_projectile_composition_test` (#47) **PASS** 0.04s; `ecs_hitscan_selection_test` (#53) **PASS** 0.63s; `ecs_hitscan_resolution_test` (#54) **PASS** 0.04s; `fps` (#94) **PASS** 16.32s; total **4/4**, elapsed **17.04s**.
- **Verification source:** actual CTest output supplied by user (not an inferred test result). Scope is precisely the four listed tests; a successful full CI or all ECS tests has **not** been established.
- **Critical outstanding gate:** `ecs_native_persistence_test` and `ecs_native_rts_integration_test` **were not in this test selection**. Native component/resource serialization v10, migration v1→v2, corrupted state recovery and real RTS `NativeModule` round-trip still require executing these tests.
- **Next agent action:** verify local HEAD and test discovery:
  ```bash
  git rev-parse HEAD
  cmake --build build/ecs-test --target aiverse_ecs_native_persistence_test aiverse_ecs_native_rts_integration_test -j8
  ctest --test-dir build/ecs-test --no-tests=error --output-on-failure -R '^ecs_native_(persistence|rts_integration)_test$'
  ```
  Capture the full output and note whether the tested HEAD matches the PR. If a test is absent, reconfigure the existing CMake build with `AIVERSE_BUILD_TESTS=ON`; do not treat zero discovered tests as a pass. After actual passes, run `bash tests/run_ecs_experiment_suite.sh` and the real RTS/FPS parity suite.
- **CI on latest remote branch at this checkpoint:** queued; no observed successful workflow result. No new native build was executed by this agent.
