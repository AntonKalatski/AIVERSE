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


---

## Stage 83 — owner Mac Studio validates Stage 82 AUTO calibration; traced run slows down (2026-11-19)

**Branch:** `perf/auto-batch-cost-metrics-20261119` (PR #39 Draft). **Source tested:** Stage 82 head `ad20f0b569064d29090f9716fedb016c02bf8b9c`. **This stage reports actual user-provided native test results; no further runtime changes are verified yet.** Target worktree was `/Users/aleksandrfeer/Projects/AIVERSE-batching-stage82`, detached at the correct head; primary checkout remained untouched.

### Actual owner-provided Mac Studio build/test evidence

- AppleClang 17.0.0: configured the separate worktree successfully and built the real `aiverse_material_batches` including `Renderer3DModels.cpp` and `GfxMetal.mm` without a compile/link error.
- `material palette CPU checks=38 result=passed`.
- Synthetic geometry: 1024 authored unique primitives, one ModelDraw.
- Confirmed **actual MDI-submitting frame 101** `changed_channels=0` versus Direct baseline; final AUTO frame also `changed_channels=0`.
- `stress benchmark policy=auto measured_mdi_frames=80 measured_direct_frames=1 measured_mdi_submissions=730`; terminal `batch=1 shadow=5 opaque=5 total=12 error=`, 10 real CPU MDI submissions and 2048 indirect commands. Cumulative MDI scans=attempts=submitted=860, 0 failed/ineligible/geometry/material/state break rejections.
- `Stage 82 exit=0`.
- `AUTO CPU learning phase=0 bucket=2 direct=18 0.92662ms mdi=88 0.79663ms opportunities=106`; opaque `direct=22 0.90724ms mdi=84 0.71809ms opportunities=106`. **The CPU selector now chooses MDI on the test and is no longer stuck on Direct.**

CPU render timing recorded from this specific highly traced run:

| Sample | Count | Median (ms) | P95 (ms) |
| --- | ---: | ---: | ---: |
| Direct baseline | 66 | 1.1740 | 2.5262 |
| mixed AUTO | 81 | 1.4177 | 2.1721 |
| AUTO Direct fallback | 8 | 1.1988 | 1.2741 |
| AUTO MDI-submitted | 88 | 1.3492 | 2.1721 |
| AUTO late frames | 36 | 1.7112 | 2.2683 |

Sampled successful-MDI CPU stage mean per pass: shadow `total=0.62334ms submit=0.39444ms` (n=83 frames, 415 submissions); opaque `total=0.51661ms submit=0.38189ms` (n=79, 395 submissions). Last frame `submit=0.50567ms` shadow and `0.55725ms` opaque.

### What the detailed diagnostic actually demonstrates

At cold startup, first MDI passes were **extremely expensive** (phase0 39.80017ms, phase1 444.91758ms). The next MDI pair measured 1.73/1.61ms. By opportunities ~8–36 the completed MDI passes largely reached ~0.3–0.4ms against initial Direct ~0.5ms, and MDI EWMA dropped below Direct; 32+ MDI calibration samples allowed the policy to overcome the cold EWMA. Late in the run there were substantial >1ms pass-time spikes for both paths, so the overall AUTO wall time was slower than previous untraced six-run fixed-scene CPU MDI runs, whose forced MDI medians were 0.8846ms and 0.8772ms. **The trace run does not establish that AUTO itself causes slowdown**: debug `fprintf(stderr)` on many early frames, cold caches, GPU submission/synchronization and time-varying external contention can affect wall timing. No GPU timestamps, frame thermal/frequency metrics, or randomized per-frame crossover were collected. Do not use this diagnostic sample to tune the 8% policy threshold or claim default-on is ready.

### Next exact step: retrace-free counterbalanced test

From the isolated worktree, use its retained Stage 80 rotated runner **without** `AIVERSE_BATCH_AUTO_COST_TRACE` or `AIVERSE_ICB_PROFILE`. The mode-specific runner already verifies forced CPU MDI raster parity and actual submissions. A fresh six-run log under `build/batching-stage82/stage83-clean` is needed to measure the genuinely learned AUTO after Stage 81 policy changes under balanced process order. Compare both Direct and forced MDI results before drawing conclusions; if all three modes slow simultaneously, suspect device/environment rather than AUTO alone.

```bash
cd /Users/aleksandrfeer/Projects/AIVERSE-batching-stage82 || exit 1
unset AIVERSE_BATCH_AUTO_COST_TRACE AIVERSE_ICB_PROFILE AIVERSE_BATCH_AUTO_TRACE
bash tools/batching-stage80-ab.sh 1024 build/batching-stage82/stage83-clean
```

The script uses `AIVERSE_BATCH_BENCH_BIN` default `build/batching-gpu-verify/aiverse_material_batches`; **set `AIVERSE_BATCH_BENCH_BIN=build/batching-stage82/aiverse_material_batches`** for this isolated Stage 82 build (correct invocation below).

```bash
AIVERSE_BATCH_BENCH_BIN=build/batching-stage82/aiverse_material_batches \
  bash tools/batching-stage80-ab.sh 1024 build/batching-stage82/stage83-clean
```

Require 6/6 exits, 38 checks each, real-MDI raster parity, and actual submissions for forced modes. If clean-run AUTO remains slower than forced MDI despite choosing MDI on 80+ frames, compare per-pass and whole-render cost, GPU queue inflight stalls and sampled cadence. Do not conflate CPU wall with GPU time. If clean AUTO is competitive, proceed to 10k and GPU timestamp instrumentation, keeping experimental flags OFF by default and PR #39 Draft.

**Actual work performed for Stage 83:** analyzed owner-provided build and trace log; GitHub handoff updated. No assistant-run native GPU/CTest, no CI or Vulkan validation.
