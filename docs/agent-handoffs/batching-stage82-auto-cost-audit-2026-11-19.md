# AIVERSE batching — Stage 82: AUTO wall-time cost audit

**Date:** 2026-11-19
**Owner branch:** `perf/auto-batch-cost-metrics-20261119` (created from `main` after merged PR #34)
**Worktree safety:** the owner's main checkout `/Users/aleksandrfeer/Projects/AIVERSE` has unresolved `UU AGENTS.md` and local changes to Vulkan/docs. Do not use `pull`, `reset --hard`, `rebase --abort`, or merge into that checkout. Use a **separate detached worktree** as directed below.
**Code commits:** `695ffceae4f8be475ba1e526a0dd1ad73dbf49ad` and `cd05c38fd6efb7441a5d72b4dac0785cb0fe1176` in `engine/render/Renderer3DModels.cpp`.
**Validation:** GitHub commit creation/source examination only; native Mac compile, raster and GPU tests **not performed** for Stage 82. Production flags/default behavior unchanged.

## Background and evidence

PR #34 was successfully merged into `main` at `58e29ab052458fffcdebebffe27022b53adb9367`. Earlier owner Mac Studio Stage 80 runs (2 process-order-rotated cycles) passed all six fixed-camera trials with 35 original + 3 scheduler CPU checks expected under Stage 81. The **observed** `renderer.render()` CPU medians on 1,024 unique primitives were:

| Path | Cycle 1 (ms) | Cycle 2 (ms) |
| --- | ---: | ---: |
| Direct | 1.1545 | 1.1685 |
| Forced CPU MDI | 0.8846 | 0.8772 |
| AUTO | 1.1464 | 1.1479 |

Both forced CPU MDI runs made 810 CPU MDI submissions during 81 measured batched frames, with `actual MDI pixel parity frame=100 changed_channels=0` and terminal `changed_channels=0`; both forced Direct runs had zero MDI submissions. AUTO still selected Direct at end; its per-shadow/opaque MDI EWMA values were inexplicably higher than corresponding Direct EWMAs despite the lower overall forced-MDI `renderer.render()` median. CPU pass metrics and complete render wall time have different measurement scope, so these numbers do not yet prove that the scheduler makes a mathematically incorrect choice. Stage 81 increased MDI calibration probes but its new native AUTO outcome has **not yet been provided**.

## Scope and architecture

`Renderer3D::renderModels(phase)` already measures one CPU phase from `passBegin`; after completion, `AutoCpuBatchPolicy::observe` stores the raw synchronous wall duration in a pass/size bucket. Stage 82 adds a flag-gated trace **at the exact observation point**, printing the sampled phase, mode selected by real MDI submissions, number of items, raw CPU pass duration, per-pass completed MDI submissions, current Direct/MDI EWMA, sample counts, and opportunities.

- Flag: `AIVERSE_BATCH_AUTO_COST_TRACE=1`. Default OFF. Prints at most 24 samples per mode and phase per process (96 maximum) to avoid spamming every frame.
- Format: `[auto-cpu-cost] phase=0 mode=mdi items=1024 wall_ms=... submissions=... direct_ewma_ms=... mdi_ewma_ms=... direct_samples=... mdi_samples=... opportunities=...`.
- The instrumentation does **not** change the selector, timing boundaries, geometry compatibility, the Metal RHI, or GPU synchronization.
- This is **CPU wall time**, not GPU completion latency. Trace I/O can distort timing on subsequent frames; compare this diagnostic run with an untraced run, not as an authoritative performance benchmark.

## Exact Mac commands (no modifications to conflicted checkout)

```bash
ROOT=/Users/aleksandrfeer/Projects/AIVERSE
WT=/Users/aleksandrfeer/Projects/AIVERSE-batching-stage82

git -C "$ROOT" fetch origin perf/auto-batch-cost-metrics-20261119 || exit 1
git -C "$ROOT" worktree add --detach "$WT" origin/perf/auto-batch-cost-metrics-20261119 || exit 1
cmake -S "$WT" -B "$WT/build/batching-stage82" \
  -DAIVERSE_BUILD_HDR_TESTS=ON -DAIVERSE_BUILD_TESTS=OFF \
  -DAIVERSE_BUILD_EDITOR=OFF -DCMAKE_BUILD_TYPE=Release || exit 1
cmake --build "$WT/build/batching-stage82" --target aiverse_material_batches --parallel 8 || exit 1
mkdir -p "$WT/build/batching-stage82/captures"
(
  cd "$WT" || exit 1
  AIVERSE_PALETTE_TEST_CASE=auto_stress \
  AIVERSE_BATCH_STRESS_ITEMS=1024 \
  AIVERSE_BATCH_AUTO=1 \
  AIVERSE_BATCH_CPU_TIMING=1 \
  AIVERSE_BATCH_AUTO_COST_TRACE=1 \
  AIVERSE_CAPTURE_SUCCESSFUL_MDI=1 \
  build/batching-stage82/aiverse_material_batches \
    unused.obj build/batching-stage82/captures/auto-stage82-1k \
    2>&1 | tee build/batching-stage82/auto-stage82.log
)
```

If detached-worktree path already exists, inspect it rather than overwriting it. Do not suppress compile/test errors. For this run require CPU checks PASS, a verified actually-MDI capture `changed_channels=0`, final Direct-vs-AUTO screenshot parity, and exit code 0. Because `tee` can hide program exit codes without pipefail, check the log's final result and consider `set -o pipefail`.

## Next agent tasks

1. Compare the 24 raw MDI and Direct per-pass times with `successful MDI CPU stage` and complete `renderer.render()` distributions. Investigate any gap, including phase invocations per frame, early GPU waits, and omitted CPU initialization.
2. Only if a measurement boundary or cost-model bias is demonstrated, correct it with a targeted regression fixture. Do not introduce another unsubstantiated preference threshold.
3. Rerun six 1k trials (rotated Direct/MDI/AUTO); capture real CPU MDI pixel parity and GPU timestamps when supported. Revisit 10k only afterward.
4. Record native results, new commits, limitations and next step in **another handoff**. Keep automated flags OFF by default and create a new Draft PR after passing tests. Preserve the owner's unrelated worktrees.
