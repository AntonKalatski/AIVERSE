# ECS 2.0 — Agent handoff (living document)

Last updated: 2026-11-17  
Repository: https://github.com/chromealex/AIVERSE  
Feature branch: `feature/ecs-architecture-benchmarks`  
Pull request: https://github.com/chromealex/AIVERSE/pull/8  
Base: `main`

## Non-negotiable architectural decisions

- Retain production `engine/core/ECS.h` Sparse Set and its deterministic `World::each<First,...>` order until a separate compatibility/performance review.
- Do not change `Simulation::step()` while experimental scheduling is not fully tested against lockstep, snapshots, rollback, RTS and FPS.
- Components and world resources must declare all reads/writes before allowing concurrent system execution. Structural writes and order-sensitive effects must form barriers.
- Never change gameplay logic based on observed wall-clock timing. `Auto` chooses a lane policy from stable workload metadata and fixed configuration.
- Preserve entity generations, canonical command ordering, RNG state, snapshot reproducibility and checksums.

## Research and baseline

Compared AIVERSE with https://github.com/chromealex/ME.BECS and https://github.com/Felid-Force-Studios/StaticEcs. Tested Sparse Set, auto-smallest, bitmap indices, 4096/256/64 hierarchical bitmap, static slots and persistent workers. The data supports keeping Sparse Set for typical sparse/moderate queries, with parallel jobs for sufficiently large independent workloads.

Selected Mac Studio benchmarks (all times are 10 passes median, milliseconds):

| Entities | Density | Sparse dense | Sparse+hierbitmap | Static+hierbitmap |
| ---: | ---: | ---: | ---: | ---: |
| 1,000,000 | 1% | 0.147834 | 0.448333 | 0.502541 |
| 1,000,000 | 25% | 3.177 | 4.56212 | 8.27862 |
| 1,000,000 | 100% | 7.92646 | 9.11454 | 8.22242 |

Adaptive jobs benchmark on equal sparse storage/callback:

| Entities | Density | Sequential | Parallel 8 lanes | Auto 8 lanes |
| ---: | ---: | ---: | ---: | ---: |
| 1,000,000 | 1% | 0.489416 | 0.621333 | 0.485625 |
| 1,000,000 | 25% | 8.14237 | 2.67354 | 2.64929 |
| 1,000,000 | 100% | 20.5996 | 4.18721 | 4.25746 |

These are microbenchmarks, not guaranteed game-frame speedups. Checksums matched for all tested independent per-entity update workloads. Do not infer that arbitrary parallel systems are deterministic.

## Implemented experiments (PR #8)

| Path | Purpose |
| --- | --- |
| `tests/ecs_architecture_bench.cpp` | Sparse Set query, snapshot and destruction baselines |
| `tests/ecs_bitmap_query_bench.cpp` | Bitmap index, prepared pair, static-slot prototypes |
| `tests/ecs_hierarchical_bench.cpp` | Hierarchical mask and storage-density comparisons |
| `tests/ecs_persistent_jobs_bench.cpp` | Persistent 0/1/2/3/7 workers at several densities |
| `tests/ecs_adaptive_jobs_bench.cpp` | Identical callback across Sequential, Parallel, Auto |
| `engine/experiments/EcsExperiments.h` | PreparedAll, PersistentWorkers, eachParallel, eachAdaptive, DeterministicCommands, DirtyPageSnapshot |
| `engine/experiments/EcsDependencyPlan.h` | Read/write/resource/structural conflict graph and stable waves |
| `engine/experiments/EcsWaveExecutor.h` | **NEW:** opt-in execution of compatible system waves through PersistentWorkers, with barrier callback and exception propagation |
| `tests/ecs_experiments_test.cpp` | Component queries, adaptive jobs, exceptions, command buffer, dirty pages, graph tests |
| `tests/ecs_wave_executor_test.cpp` | **NEW:** sequential/parallel wave parity, dependency ordering, worker exception recovery |

### Latest step: opt-in wave execution

- Commit `fac3d130`: added `EcsWaveExecutor.h`.
- Commit `f1d6534`: added `ecs_wave_executor_test.cpp`.
- `WaveExecutor` accepts a `DependencyPlan` and ordered array of callable systems. It executes independent systems within each wave concurrently if at least two systems are present, and waits before the next wave.
- A caller-provided `afterWave` callback supports barriers such as flushing *already joined* deterministic structural commands.
- Worker exceptions propagate after remaining tasks join; the simulation state may be partially updated after failure, so the caller must discard and rebuild from a valid snapshot.
- This is deliberately **not** wired to `Simulation::step()`: access declarations are trusted and not yet automatically verified/enforced.

### Validation status

- User previously reported `PASS prepared, persistent parallel, deterministic commands, dirty pages` from the older test version.
- User ran and reported full adaptive benchmark CSV with matching checksums, as shown above.
- **NEW files have been committed through the GitHub connector but have not yet been compiled on Mac Studio.** This session's container cannot resolve GitHub hosts, so no claim of native compilation is made.
- Previous test changes to adaptive/exception/dependency behavior also require current-branch revalidation.

## Build/run from repository root

```sh
git switch feature/ecs-architecture-benchmarks
git pull --ff-only

c++ -std=c++20 -O2 -pthread -I. tests/ecs_wave_executor_test.cpp -o /tmp/aiverse-wave-test
/tmp/aiverse-wave-test

c++ -std=c++20 -O3 -pthread -I. tests/ecs_experiments_test.cpp -o /tmp/aiverse-ecs-experiments-test
/tmp/aiverse-ecs-experiments-test

c++ -std=c++20 -O3 -DNDEBUG -pthread -I. tests/ecs_adaptive_jobs_bench.cpp -o /tmp/aiverse-ecs-adaptive
/tmp/aiverse-ecs-adaptive
```

## 2026-11-17 — Step: checked access scopes (latest)

User Mac Studio validation of prior step (reported in conversation):
- `PASS dependency wave execution, sequential/parallel checksum, worker recovery`
- `PASS prepared, adaptive jobs, exceptions, dependency waves, deterministic commands, dirty pages`

Current code changes in PR #8:
- Commit `932ebe0`: `engine/experiments/EcsGuardedWorld.h`. Added opt-in `GuardedWorld` facade with declared per-component `read<T>/write<T>/has<T>`, resource access, guarded structural calls, `eachRead<T>/eachWrite<T>`, and thread-local `AccessScope` checking the executing system's `SystemAccess`. `runChecked` wraps each `WaveExecutor` callback in an `AccessScope`, restoring scope on exceptions.
- Commit `1091e10`: `tests/ecs_guarded_world_test.cpp`. Covers sequential/parallel parity of independent system wave, resource barrier, rejecting undeclared writes/resources/structural operations, and worker pool reuse after access violation.

**Validation status:** these two new commits were pushed via GitHub connector, but the new test has **not** yet been compiled or run on Mac Studio. No claim of production memory safety. `GuardedWorld` is only an opt-in facade; raw `World`, previously retained references/pointers, global state, external resources, and misdeclared indirect effects can bypass the checks. Access IDs are per-process component type IDs and not stable serialized identifiers. This is a prototype, not full scheduler enforcement.

Run:
```sh
git switch feature/ecs-architecture-benchmarks
git pull --ff-only
c++ -std=c++20 -O2 -pthread -I. tests/ecs_guarded_world_test.cpp -o /tmp/aiverse-ecs-guarded
/tmp/aiverse-ecs-guarded
```

Next:
1. Validate this test; correct build/runtime errors.
2. Introduce a stronger restricted access API to prevent direct raw `World` escape in scheduled call sites; assess thread-safety of concurrent pool creation and access.
3. Add a deterministic rollback/replay test with the existing `SimSnapshot` API and wave jobs; execute with different worker counts, compare canonical checksums.
4. Then design conservative opt-in `Simulation` integration, preserving the sequential path.
5. Continue updating this handoff after each code step and do not merge PR #8 until complete regression tests pass.

## 2026-11-17 — Step: deterministic World replay (latest)

New test `tests/ecs_wave_replay_test.cpp`:
- Commit `5a93c66`: replay World snapshot through opt-in checked dependency waves and lane-ordered structural commands.
- Commit `327e0bc`: route scheduled component reads through `GuardedWorld` rather than raw `World::pool`.
- Executes ten deterministic ticks starting from the same deep-copied World with worker counts 0, 3 and 7; compares a fingerprint that includes dense Entity handles, component values and Entity allocator generations/free list.
- Movement reads Velocity and writes Position, acceleration writes Velocity, and structural command application occurs after the appropriate wave barrier.
- Confirms original seed state remains unchanged, compares repeated replay with the same and different worker counts.
- This test is not `Simulation::saveState/loadState` and does not verify network commands, RNG or full module snapshots.

**Validation status:** commits present on feature branch; Mac Studio has **not yet compiled or run this new replay test**. Prior wave/experiments tests passed as reported by user.

Run:
```sh
git pull --ff-only
c++ -std=c++20 -O2 -pthread -I. tests/ecs_wave_replay_test.cpp -o /tmp/aiverse-ecs-replay
/tmp/aiverse-ecs-replay
```

Next: validate new guarded/replay tests, address any failures, add simulation-level snapshot/rollback/lockstep regression coverage and enforce a no-raw-World contract before enabling production scheduled systems.

## 2026-11-17 — Bulk step: opt-in Simulation integration and consolidated test suite

User requested all outstanding work in one bundle, then will execute tests. Implemented several commits on the existing PR #8:

- `ad8979f` — `engine/core/ECS.h`: added `World::eachExisting<T>(fn)`, which does **not register a missing pool**. This avoids a known concurrent mutation hazard from calling the previous `World::each<T>()` on previously unused types. Structural operations must still be excluded while jobs are active.
- `54b4897` — `engine/experiments/EcsGuardedWorld.h`: `eachRead/eachWrite` now use `eachExisting`.
- `b0ca3cf` and `91e2ce3` — `engine/experiments/EcsSimulationBridge.h`: opt-in `addGuardedGroup(Simulation&, name, vector<GuardedSystem>, workerCount)`. A group registers as **one ordinary system** through the existing `Simulation::addSystem` API. Internally executes compatible declared systems with `WaveExecutor` and `runChecked`. The bridge disallows structural and ordered-side-effect systems. All legacy systems retain their original sequential path and a single `World::flush()` after the group.
- `689cd0f` — `tests/ecs_simulation_bridge_test.cpp`: exercises actual `Simulation::step()`, canonical player-command ordering, `SimSnapshot` save/load/replay, and matching checksums across 0/1/3/7 worker counts. Uses a small isolated `ToyModule` rather than RTS/FPS gameplay.
- `3863b7d` — `engine/experiments/EcsExperiments.h`: command buffer `flush` swaps queued lane batches before replay to avoid callback invalidation; `discard()` for cancelled batches. Commands enqueued during flush remain for the next flush; a partial mutation on exception remains nontransactional.
- `7eb318b` — `tests/ecs_experiments_test.cpp`: new command-buffer reentrant enqueue/discard tests.
- `d083de2` — `tests/run_ecs_experiment_suite.sh`: builds and executes all five correctness/integration tests with one command, optionally runs all performance benchmarks via `--bench`, optionally enables ASan+UBSan via `--sanitize`.

### Commands — run everything after pulling

```bash
git switch feature/ecs-architecture-benchmarks
git pull --ff-only
bash tests/run_ecs_experiment_suite.sh
bash tests/run_ecs_experiment_suite.sh --sanitize
bash tests/run_ecs_experiment_suite.sh --bench
```

**Verification status:** all new code is pushed through GitHub but not locally compiled by the assistant. Mac Studio previously passed wave and experiment tests *before these modifications*. User will run the full suite now. Do not claim this batch is verified yet.

### Safety limits / remaining production work

- This is **opt-in**, not default Simulation parallelism. It schedules groups of *independent systems*, not jobs within a single expensive system. For parallel entity iteration use separately profiled `eachAdaptive`, but it is not integrated with the guarded bridge.
- `GuardedWorld` is a **runtime-checked facade only**. A job can escape via external captures, raw `World`, retained pointers, static globals, RNG or events; access declarations are not statically enforced. It is still unsafe to enable arbitrary gameplay modules.
- No structural changes are allowed inside guarded groups. Use existing regular sequential systems before/after groups for spawning/deletion, or extend the staged command API with proper lifecycle semantics.
- Snapshot rollback in the test uses `Simulation::saveState/loadState` full snapshots. `DirtyPageSnapshot` is still only a standalone prototype and cannot replace `SimSnapshot` without full dirty tracking of components, allocator, resources and nontrivial data.
- There is no performance claim from the real grouped systems, and no production RTS/FPS integration yet. Benchmarking and regression suite still need a successful Mac Studio run.
- Do not merge PR #8 to `main` until assertions, sanitizers and real-game replay/lockstep regressions pass.

### Next agent actions

1. Run suite on Mac Studio; fix compile errors and any checksum mismatches, updating this handoff in each fix.
2. Expand opt-in safety with a restricted registration API and strict no-raw-World policy; instrument runtime access validation.
3. Add RTS and FPS integration checks under an explicit opt-in flag, compare checksums with baseline.
4. Design a safe per-entity parallel guarded job API that propagates `AccessScope` to workers without nested worker-pool deadlocks.
5. Evaluate real-world performance and snapshot optimizations, ensuring a sequential fallback.

## 2026-11-17 — Agent discoverability

- Commit `c873f52`: `AGENTS.md` now links this living ECS handoff in its introduction and instructs every agent to update it per implementation step.
- Current production world remains Sparse Set. The new bridge is **opt-in only** through `addGuardedGroup`; existing RTS/FPS system registration and execution remain unchanged.
- The bulk suite still awaits user execution on Mac Studio. Treat all fresh commits after `PASS dependency wave execution` as unverified until the new combined suite passes.

## 2026-11-17 — macOS test runner CRLF fix

Mac Studio reported before any C++ compilation:

```
: invalid option nameent_suite.sh: line 3: set: pipefail
```

This is consistent with the local shell script containing CRLF (`\r\n`) line endings, making Bash parse `pipefail\r`.

- Commit `e7e66fb`: added root `.gitattributes` with `*.sh text eol=lf` and `*.bash text eol=lf`.
- Commit `a1bff78`: rewrote `tests/run_ecs_experiment_suite.sh` with LF line endings and a header note.
- This addresses the script transport/checkout problem only; all C++ suite results remain unverified until Mac Studio can run it.

Recovery if an existing checkout retains CRLF despite pull:

```sh
git pull --ff-only
perl -pi -e 's/\r\n/\n/g' tests/run_ecs_experiment_suite.sh
bash tests/run_ecs_experiment_suite.sh
```

Check line endings with `file tests/run_ecs_experiment_suite.sh` or `git check-attr text eol -- tests/run_ecs_experiment_suite.sh`. The local recovery edits the working tree; after successful verification, restore the file to the committed version if needed.

## 2026-11-17 — Full Mac Studio validation (confirmed)

User ran the updated `tests/run_ecs_experiment_suite.sh` with normal, `--sanitize`, and `--bench` modes. Full pasted output is retained in conversation. **All five correctness suites passed in both normal and sanitizer modes**:

- `PASS prepared, adaptive jobs, exceptions, dependency waves, deterministic commands, dirty pages`
- `PASS dependency wave execution, sequential/parallel checksum, worker recovery`
- `PASS guarded access, dependency barriers, parallel parity, exception recovery`
- `PASS deterministic world snapshot replay across 0/3/7 workers`
- `PASS Simulation step, canonical commands, snapshot rollback, 0/1/3/7-worker checksums`
- `PASS ECS experiment suite` printed after normal, sanitizer, and benchmark runs.

During `--sanitize`, macOS emitted `malloc: nano zone abandoned due to inability to reserve vm space.` This is a sanitizer/allocator VM warning, **not a failed test**. No ASan/UBSan violations were shown in the supplied output. This does not amount to a proof of absence of all races; ThreadSanitizer has not been run.

Selected **latest** benchmark timings at 1,000,000 entities, 10 passes median (ms):

| Density | Adaptive Sequential | Adaptive Parallel (8 lanes) | Adaptive Auto (8 lanes) | Auto/Sequential speedup |
| ---: | ---: | ---: | ---: | ---: |
| 1% | 0.766041 | 0.706875 | 0.766166 | ~1.00x |
| 25% | 10.9338 | 2.99763 | 2.96833 | 3.68x |
| 100% | 27.1653 | 5.10267 | 4.82267 | 5.63x |

Latest hierarchical benchmark at 1m (ms):

| Density | Sparse dense | Sparse hierarchical | Static hierarchical | Static 2 threads |
| ---: | ---: | ---: | ---: | ---: |
| 1% | 0.140041 | 0.476584 | 0.458209 | 0.7465 |
| 25% | 3.36696 | 4.83896 | 8.24113 | 5.88079 |
| 100% | 8.07446 | 9.03925 | 8.45104 | 5.72075 |

Selected latest storage-oriented query benchmark (1m): sparse broad 16.8395ms; narrow 8.317ms; auto-smallest 8.38842ms; sparse-bitmap-prepared 8.24079ms; static-slot-bitmap 4.88879ms. Note: these are different microbenchmark workloads and MUST NOT be directly compared with hierarchical or adaptive numbers.

All compared checksum columns matched across variants in the user-provided CSV for each dataset. The entire suite executes, including `Simulation::step()` + `SimSnapshot` test on a toy module, but **no existing RTS/FPS gameplay module has been migrated or subjected to full network lockstep integration testing**.

**Updated handoff decision:** prior blocker "latest tests not compiled" is resolved for these five suites. Remaining blockers: runtime guarded access only checks calls using the facade, arbitrary external captures/raw World bypass it; complete production dependency contracts, deterministic external effect handling, realistic RTS/FPS replay tests, worker race detection/TSan, and complete ECS-wide incremental snapshots are not implemented/verified. Retain opt-in bridge and sequential default; do NOT merge by default.

## 2026-11-17 — Gameplay integration hardening and real RTS/FPS compare (CURRENT)

After the full prior test suite passed, new code was committed in PR #8:

- `9d84ca7` — `PersistentWorkers::parallelFor` now rejects nested calls on the **same** pool from its worker or caller lanes rather than deadlocking. This is defensive only; calling two distinct pools recursively is not yet subjected to a general concurrency audit.
- `12ad169` — `tests/ecs_experiments_test.cpp` verifies nested parallel submissions are rejected and worker pool remains usable.
- `942ff3b` — `modules/rts/RtsUnits.cpp` gains an **off-by-default** `AIVERSE_ECS_EXPERIMENTAL_RTS_TRANSFORM_JOBS` path for the independent `Transform.prevPos = Transform.pos` work in `sysMovement`. The spatial-hash insertion stays **sequential in original Unit dense order**. It processes at most one Transform per Unit and does not change the rest of RTS movement.
- `ed7ed90` — `CMakeLists.txt` defines the default-OFF option `AIVERSE_ECS_EXPERIMENTAL_RTS_TRANSFORM_JOBS` and applies it privately to `aiverse_game`. It **must not** be enabled for every game without validation.
- `ac8dfa3` — `tests/run_ecs_gameplay_compare.sh` builds a baseline and opt-in version of the **real RTS** headless runner and FPS tests. Runs RTS at fixed seed with `--verify-restore-at` and compares `--checksum-log` checkpoint files. FPS tests run in each build; FPS gameplay is still sequential and its authoritative State/actors/RNG must not be parallelized without a separate dependency redesign.
- `c06c48a`, `df98896` — registered `ecs_experiments_test`, `ecs_wave_executor_test`, `ecs_guarded_world_test`, `ecs_wave_replay_test`, `ecs_simulation_bridge_test` in CMake/CTest, linked to `aiverse_engine` and `Threads::Threads`.

### Validation status of this step

**Not yet compiled or run on Mac Studio**. The **previous** complete ECS suite (including sanitizers and benchmarks) passed; this new code requires fresh validation.

### Execute after git pull

```sh
# Existing standalone suites; now includes nested worker-pool test
bash tests/run_ecs_experiment_suite.sh
bash tests/run_ecs_experiment_suite.sh --sanitize

# Real modules: build two engine variants, run RTS snapshot/checksum comparison,
# FPS regression in both, optionally aiverse_tests in both:
bash tests/run_ecs_gameplay_compare.sh
bash tests/run_ecs_gameplay_compare.sh --full

# Alternative via existing CMake build tree:
cmake -S . -B build -DAIVERSE_BUILD_TESTS=ON
cmake --build build --target aiverse_ecs_experiments_test aiverse_ecs_simulation_bridge_test
ctest --test-dir build -R '^ecs_' --output-on-failure
```

### Known limitations

- No general parallel execution of RTS/FPS systems; only opt-in RTS previous-transform preprocessing is experimental.
- The number of copied Unit transforms must be large enough to justify 3 worker threads; performance and cross-platform checks are outstanding. Source option is OFF by default; no change to existing users.
- The guarded facade detects only accesses routed through it; raw `World` and external/global captures remain unguarded, so any such scheduled system must be audited.
- `DirtyPageSnapshot` does not integrate with `SimSnapshot` (no complete dirty tracking for all pools/resources/Entity state); do not replace the existing rollback mechanism.
- The compare script assumes CMake toolchain and example template availability; any configure/build failures must be fixed and recorded.
- Avoid calling this feature complete until `run_ecs_gameplay_compare.sh` and relevant RTS/FPS lockstep suites pass.

## Outstanding work and risks (priority order)

1. **Validation accomplished:** five correctness suites and normal/sanitizer/benchmark runs passed on Mac Studio; rerun after any further change.
2. **Enforce access contracts:** GuardedWorld now validates calls routed through it, but raw World references, retained pointers and external captures bypass checks; harden safe lifecycle and declarations before production scheduling.
3. **Deterministic job commands**: current `DeterministicCommands` merges lane-local commands, but order-dependent operations must use stable semantic lane IDs independent of OS scheduling; validate against replay and rollback.
4. **Wave executor safety**: a rejected/misdeclared dependency can race. Add robust instrumentation and integration tests for write/write, resource/RNG and structural conflicts; avoid concurrent writes to shared event queues.
5. **Production integration**: add explicit opt-in system registration and keep existing sequential path as fallback; do not blanket-parallelize RTS modules.
6. **Snapshots**: `DirtyPageSnapshot` only works for explicitly-marked trivially copyable vector pages and is not a full rollback replacement. Require tracking of all mutations, entity allocator state and nontrivial components.
7. **Perf**: evaluate scheduling overhead and real gameplay work, not only independent microbenchmarks. Tune fixed `ParallelPolicy{8192,2048}` for system costs and platform worker topology.
8. **Regression**: run existing unit tests, RTS/FPS simulations, replay/lockstep and cross-worker checksum comparisons before merging.
9. **Docs discipline**: update this document in the same PR after each meaningful code step with commit, exact changed files, verification status and next actions.

## Operating rule for the next agent

Work on the current feature branch and existing PR #8. Do not merge to main without explicit approval and full verification. Every implementation step must update `docs/ecs-agent-handoff.md` in the same branch. Report specific confirmed outcomes separately from assumptions.
