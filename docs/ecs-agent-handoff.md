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

## Outstanding work and risks (priority order)

1. **Validate latest changes**: compile and execute new tests; fix compiler warnings or test failures in PR #8.
2. **Enforce access contracts**: declared read/write component and resource accesses are currently advisory only. Need guarded world views, safe lifecycle and explicit access validation before production scheduling.
3. **Deterministic job commands**: current `DeterministicCommands` merges lane-local commands, but order-dependent operations must use stable semantic lane IDs independent of OS scheduling; validate against replay and rollback.
4. **Wave executor safety**: a rejected/misdeclared dependency can race. Add robust instrumentation and integration tests for write/write, resource/RNG and structural conflicts; avoid concurrent writes to shared event queues.
5. **Production integration**: add explicit opt-in system registration and keep existing sequential path as fallback; do not blanket-parallelize RTS modules.
6. **Snapshots**: `DirtyPageSnapshot` only works for explicitly-marked trivially copyable vector pages and is not a full rollback replacement. Require tracking of all mutations, entity allocator state and nontrivial components.
7. **Perf**: evaluate scheduling overhead and real gameplay work, not only independent microbenchmarks. Tune fixed `ParallelPolicy{8192,2048}` for system costs and platform worker topology.
8. **Regression**: run existing unit tests, RTS/FPS simulations, replay/lockstep and cross-worker checksum comparisons before merging.
9. **Docs discipline**: update this document in the same PR after each meaningful code step with commit, exact changed files, verification status and next actions.

## Operating rule for the next agent

Work on the current feature branch and existing PR #8. Do not merge to main without explicit approval and full verification. Every implementation step must update `docs/ecs-agent-handoff.md` in the same branch. Report specific confirmed outcomes separately from assumptions.
