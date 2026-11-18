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
