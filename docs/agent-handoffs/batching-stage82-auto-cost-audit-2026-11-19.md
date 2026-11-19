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


---

## Stage 84 — clean crossover outcome and buffered per-frame CPU timeline (2026-11-19)

**Branch:** `perf/auto-batch-cost-metrics-20261119`, PR #39 (Draft). **Source change:** `adfe9a34d37bb5816f91a85d33e7ac790bd3f6d0` in `tests/render/material_batches.cpp`. Documentation appended as distinct commit. Owner's primary conflicted `AIVERSE` checkout stays untouched.

### Owner Mac Studio native Stage 83 clean six-run crossover

Executed in detached `AIVERSE-batching-stage82` using the prebuilt Stage 82 fixture **without** `AIVERSE_BATCH_AUTO_COST_TRACE`. Runner completed all six runs, every run printed `material palette CPU checks=38 result=passed`, screenshot equality `changed_channels=0`, and zero pipeline failures. Forced CPU MDI runs also printed `actual MDI pixel parity frame=100 changed_channels=0`. CPU synchronous `renderer.render()` medians (ms):

| Mode | Cycle 1 | Cycle 2 |
|---|---:|---:|
| Direct | 1.1802 | 1.1677 |
| CPU MDI | 0.8303 | 0.8201 |
| AUTO | 0.8488 | 1.4638 |

Cycle 1 AUTO (0.8488ms) came close to forced MDI (0.8303ms): approximately 28.1% faster than standalone Direct and ~2.2% slower than forced MDI. In Cycle 2 AUTO was substantially slower at 1.4638ms even though it **still used MDI** in 79/81 measured frames (755 MDI submissions), versus 80/81 and 800 submissions in Cycle 1. Forced MDI was consistently fast across runs, so the performance regression is not explained by simply choosing Direct. During Cycle 2 AUTO, shadow stage on final frame had 0.88979ms total / 0.54708ms backend submit, while opaque remained 0.34933ms total / 0.26158ms backend; both phases' cost varied, with unexplained large spikes. CPU time is not GPU time/FPS; thermal contention, GPU/CPU waits or interactive scheduling remain hypotheses, not confirmed causes. Do not turn AUTO on by default based on these six process runs.

### Stage 84 code change: opt-in deferred CSV

`AIVERSE_BATCH_FRAME_TIMELINE=1` in the `auto_stress` fixture records an in-memory sample **after** every `renderer.render()` call on batched frames 90..195, and writes the entire CSV only at frame 195. This avoids per-frame file I/O and `fprintf` logging overhead in the measured hot path. Output file: `<prefix>-frames.csv` with columns:

```text
frame,render_cpu_ms,mdi_submissions,mdi_commands,shadow_draws,opaque_draws,shadow_cpu_ms,opaque_cpu_ms,shadow_backend_ms,opaque_backend_ms
```

Model phase CPU and backend stage timings come from the existing `ModelRenderStats::mdiCpuStages` measured within the same frame; draw counts are renderer profile shadow/opaque. The timer brackets synchronous `renderer.render()` only and precedes timeline vector append. Writes/flushes at frame 195 are not included in the CPU sampled render time. A failed CSV file write marks the fixture failed. **This is not a GPU timestamp implementation**, and data only covers the scene's second phase, not warm-up Direct frames before frame 90. The CSV may expose successive frame spikes and whether they affect both passes or one pass, but cannot directly identify OS scheduling or Metal queue waits.

### Validation truth

Stage 83 six-run test evidence was provided by the owner and is described above. Stage 84 CSV source has been committed via GitHub, but **was not built or run on Mac Studio yet**; do not claim native CSV output, parity, performance or CI. No rendering/RHI/auto-policy production logic changed in Stage 84; production flags remain OFF by default.

### Exact next Mac command, safe detached worktree

```bash
ROOT=/Users/aleksandrfeer/Projects/AIVERSE
WT=/Users/aleksandrfeer/Projects/AIVERSE-batching-stage82
git -C "$ROOT" fetch origin perf/auto-batch-cost-metrics-20261119 || exit 1
git -C "$WT" status --short
git -C "$WT" switch --detach origin/perf/auto-batch-cost-metrics-20261119 || exit 1
cmake --build "$WT/build/batching-stage82" --target aiverse_material_batches --parallel 8 || exit 1
cd "$WT" || exit 1
set -o pipefail
AIVERSE_PALETTE_TEST_CASE=auto_stress \
AIVERSE_BATCH_STRESS_ITEMS=1024 \
AIVERSE_BATCH_AUTO=1 \
AIVERSE_BATCH_CPU_TIMING=1 \
AIVERSE_BATCH_FRAME_TIMELINE=1 \
AIVERSE_CAPTURE_SUCCESSFUL_MDI=1 \
build/batching-stage82/aiverse_material_batches \
  unused.obj build/batching-stage82/captures/stage84-auto-1k \
  2>&1 | tee build/batching-stage82/stage84-auto.log
echo "Stage 84 exit=$?"
```

Require `material palette CPU checks=38 result=passed`, actual MDI pixel parity `changed_channels=0`, final `changed_channels=0`, `stress frame timeline samples=106`, and exit=0. The CSV path is `build/batching-stage82/captures/stage84-auto-1k-frames.csv`. Inspect spikes/contiguous windows with the same MDI mode. If no spikes appear in one run, repeat the isolated AUTO process 3+ times with unique prefixes to characterize variability without I/O trace. Next agent should use the actual CSV to decide whether the problem is in selection CPU time, backend submit, or external load. Keep PR #39 Draft and original main worktree untouched.


---

## Stage 85 — actual Stage 84 CSV analysis: steady-state AUTO MDI is stable in this run (2026-11-19)

**Repository/branch:** `chromealex/AIVERSE` / `perf/auto-batch-cost-metrics-20261119`, Draft PR #39. **Owner evidence:** user-uploaded `stage84-auto-1k-frames.csv`, 106 rows with integer frames 90..195 and columns `frame,render_cpu_ms,mdi_submissions,mdi_commands,shadow_draws,opaque_draws,shadow_cpu_ms,opaque_cpu_ms,shadow_backend_ms,opaque_backend_ms`. Analyzed the actual file using Python pandas, not inferred from prior logs. This stage documents results only; no production source changes or Mac tests run by assistant.

### Quantified observations

- **MDI mode**: 93/106 rows have nonzero `mdi_submissions`, median synchronous `renderer.render()` **0.850792ms**, mean 1.005803ms inflated by early warmup outliers, min 0.751875ms, max 8.87179ms.
- **Direct mode**: 13/106 rows, median **1.190000ms**, mean 1.182105ms, min 1.113380ms, max 1.24629ms.
- Full MDI mode consistently submits 10 CPU indirect groups with 2048 commands and 5 shadow + 5 opaque draw calls. Direct mode has 0 MDI submissions and 1024 + 1024 draw calls. Those are distinct paths, so no claim that MDI is always faster in a real scene or on GPU.
- First cold/slow MDI frames:
  - Frame 91: render 8.871790ms, shadowCPU 4.036960ms, opaqueCPU 4.648370ms, backend submissions 2.762210ms + 1.500000ms.
  - Frame 93: render 3.901250ms, shadowCPU 2.001460ms, opaqueCPU 1.742420ms, backend submissions 1.655630ms + 1.556080ms.
  - Frame 95: render 3.563420ms, shadowCPU 1.778540ms, opaqueCPU 1.618130ms, backend submissions 1.471290ms + 1.442540ms.
  - Frame 97: MDI returns to 0.898250ms; frame 99: 0.899417ms.
- **After frame 97** (frames 98..195), no MDI-submitted row exceeds 1.2ms CPU render duration. MDI medians by further window: frames 100..115 ~0.842ms, 116..139 ~0.848ms, 140..159 ~0.844ms, 160..179 ~0.852ms, 180..195 ~0.859ms. This *one* recorded process has no sustained late-run MDI slowdown; the high Cycle 2 AUTO medians in the preceding six-run clean benchmark are not reproduced here. One healthy run cannot disprove intermittent slowdowns on another process run.
- Medians are unpaired samples from one sequential AUTO workload, not independent randomized A/B/A, and **all values are CPU synchronous wall time only**. GPU timestamps, completion latency, FPS, CPU frequency, scheduling and external contention are unmeasured. The earlier Mac GPU raster parity of this fixture was confirmed independently, but a CSV cannot itself verify pixel parity.

### Interpretation and next work

The immediate high-confidence conclusion is that the cold first 3 MDI samples heavily increase backend CPU submission cost; a warmed run can be stable around 0.85ms while Direct is around 1.19ms in this fixed scene. The prior apparently slow second AUTO process might reflect GPU queue stalls/CPU scheduler/thermal contention, but there is **no evidence yet** to attribute it to any single mechanism.

**Next tasks:** collect three independent Stage 84 CSV files using distinct `<prefix>` values in the same isolated worktree, preferably logging hardware/thermal state separately, and compare timelines by frame, especially whether spikes occur in `shadow_backend_ms`, `opaque_backend_ms` or the remainder. Collect GPU command-buffer or timestamp timing separately. Before changing the policy or declaring performance ready, repeat forced MDI alongside AUTO under different process order and capture actual GPU parity and no-submission guards. Keep experimental flags OFF by default and Draft PR #39 open. Handoff must report any new code commits and Mac test outcomes.

**Verification truth:** Python-loaded uploaded CSV successfully parsed, shape (106,10). This is analysis/handoff only; no native compilation, CTest, CI or GPU run was performed in this stage.


---

## Stage 86 — independent AUTO frame-time replication with forced CPU MDI controls (2026-11-19)

**Repository:** `chromealex/AIVERSE`; **branch:** `perf/auto-batch-cost-metrics-20261119`; **PR:** #39 Draft. **New files:** `tools/batching-stage86-trials.sh` and `tools/batching-stage86-analyze.py`. **Commits:** `af30bc0cfe32283b5ab3d0bb19fef60260564084` (runner), `c340235033f437602bd7fa2861818fceed4cf4d6` (analyzer), `abb0bc55fc85e3651323fce5d1e36400a0fcd315` (shell output-variable initialization fix). This handoff is a separate subsequent commit. **No runtime renderer/RHI/policy change.** Existing flags OFF by default.

### Prior measurements motivating this work

Stage 83 clean Mac Studio rotated 1k-primitive benchmark showed:
- Direct `renderer.render()` CPU median: 1.1802 / 1.1677 ms.
- Forced CPU MDI median: 0.8303 / 0.8201 ms, real MDI screenshot byte equality in both trials.
- AUTO median: 0.8488 / 1.4638 ms; while AUTO selected MDI for 80/81 and 79/81 measured frames in successive runs. Performance is therefore variable and cannot be explained by frequent Direct fallback.
- A subsequent owner-provided Stage 84 CSV, 106 frames (90..195), had 93 MDI frames and MDI median ~0.850792ms; after initial cold MDI frames 91,93,95, no successful-MDI frame after 97 exceeded 1.2ms. This single healthy run does not explain why another process had sustained late high times.

### New benchmark implementation

- `bash tools/batching-stage86-trials.sh [ITEMS=1024] [OUTPUT=build/batching-stage82/stage86-trials]` runs **five separate processes** in order `AUTO1, forced-MDI1, AUTO2, forced-MDI2, AUTO3`. Each has isolated process-local shader/Metal caches, baseline warmup and capture prefix. This is not randomized per-frame A/B/A, but interleaving provides a contemporary control for environmental performance drift.
- Every mode sets `AIVERSE_PALETTE_TEST_CASE=auto_stress`, `AIVERSE_BATCH_CPU_TIMING=1`, `AIVERSE_BATCH_FRAME_TIMELINE=1`, `AIVERSE_CAPTURE_SUCCESSFUL_MDI=1` and its explicit policy. Inherited GPU ICB, AUTO trace, GPU authored path and other conflicting benchmark flags are removed from the child environment. AUTO alone enables `AIVERSE_BATCH_AUTO=1`; forced-MDI alone enables `AIVERSE_METAL_CPU_MDI=1`.
- The runner requires successful executable exit via `set -euo pipefail`, 38-or-current-count CPU `result=passed`, an actual-MDI pixel-equality message, final byte equality, 106 CSV rows logged, CSV file existence, and real forced-MDI samples. Log and CSV files are unique per trial.
- New Python 3 standard-library analyzer reads the five CSVs, strictly validates header, numeric fields and frames 90..195, and writes `summary.csv` with measured-frame CPU median, P95, last-36-frame median, MDI/Direct frame counts, actual submissions, warmed MDI frames exceeding 1.2ms, longest consecutive slow run, per-pass CPU-stage and backend-submit median, and the worst frame's stage costs. Also prints a compact run comparison. The 1.2ms threshold is a diagnostic reference from prior 1k results, **not** a production latency requirement or an adaptive decision threshold.
- Bash issue found during static review: `local label=... prefix=... ` expanded prefix before label assignment with `set -u`. It was corrected in `abb0bc55`. No native execution reported.

### Validation status

**Implemented and committed** via GitHub connector. Inspected the live runner/CSV output contract. **NOT RUN** after these new Stage 86 changes: Mac shell, Python analyzer against generated five-run files, CMake/CTest, Metal raster, Vulkan or GPU timers. Prior Stage 83/84 owner results remain the only actual native evidence. No claim of 3 independent runs or stability verification should be made until owner logs arrive.

### Exact next owner Mac Studio command (isolated worktree)

```bash
ROOT=/Users/aleksandrfeer/Projects/AIVERSE
WT=/Users/aleksandrfeer/Projects/AIVERSE-batching-stage82

git -C "$ROOT" fetch origin perf/auto-batch-cost-metrics-20261119 || exit 1
git -C "$WT" status --short
git -C "$WT" switch --detach origin/perf/auto-batch-cost-metrics-20261119 || exit 1

# Only the Python/shell scripts changed since Stage 84; reuse Stage 82 binary.
cd "$WT" || exit 1
AIVERSE_BATCH_BENCH_BIN=build/batching-stage82/aiverse_material_batches \
  bash tools/batching-stage86-trials.sh \
  1024 build/batching-stage82/stage86-trials
```

The scripts print `Stage 86 PASS` only after all five child executables and CSV summary succeed. Share `build/batching-stage82/stage86-trials/summary.csv` and optionally the three AUTO CSVs if there are slow frames. Review outlier provenance against interleaved forced-MDI CPU controls before retuning the CPU AUTO policy or touching Metal. Keep Draft PR #39 until native verification. The main user's conflicted worktree remains untouched.

### Following tasks

If all three AUTO runs are stable near forced MDI and only first-cold MDI frames spike, quantify cold-start versus settled performance and consider GPU-timestamp instrumentation next; do not unconditionally enable AUTO. If AUTO sometimes runs slowly but forced MDI controls do not, use the per-pass backend timings in CSVs to isolate which phase changes. If both are slow together, prioritize system/device contention and native GPU completion instrumentation over policy changes. Verify shader/geometry permutations, Vulkan cross-platform parity, and full CI before release.


---

## Stage 87 — five-process Stage 86 shows backend/environment variability, not only AUTO (2026-11-19)

**Branch:** `perf/auto-batch-cost-metrics-20261119`, Draft PR #39. **Owner-tested previous script:** `tools/batching-stage86-trials.sh` and `tools/batching-stage86-analyze.py` from Stage 86. **New analyzer change:** `7af48913c4dea101aee080282a6a5b4c71f069df` in `tools/batching-stage86-analyze.py`. Documentation in this follow-up commit. **Native execution of the new analyzer revision has NOT yet occurred.** No renderer/Metal RHI/CPU policy changes.

### Actual owner Mac Studio Stage 86 results

The owner ran all five processes and the runner printed `Stage 86` summary and `changed_channels=0` at the end. The complete printed summary:

```text
Trial    MDI/81    median       p95      late   MDI>1.2  streak  worst frame
auto1        76    0.8063    1.7736    0.7775        12       6          141
mdi1         81    1.5965    1.9701    1.6193        59       8          178
auto2        80    0.8393    1.8285    1.5731        26       5          162
mdi2         81    0.8620    0.9219    0.8726         0       0          159
auto3        80    1.3900    1.8787    1.4392        48       8          173
```

**Key finding:** forced MDI without AUTO changes CPU median from `1.5965ms` (MDI1) to `0.8620ms` (MDI2), a ~85% difference, and MDI1 has 59 sampled MDI frames above 1.2ms while MDI2 has 0. Thus the slowdown is **not only caused by AUTO mode selection**; CPU backend work, queue synchronization, scheduling/thermal and concurrent system load remain possible explanations. AUTO still uses MDI in 76–80 of 81 measured frames, confirming mode selection is not the main cause of this run's differences. The healthy low-latency runs indicate the engine is capable of a ~0.8–0.9ms CPU render on this fixed synthetic scene, but neither sustained GPU improvement nor general FPS gains have been proven.

All five runs returned to the runner and generated a summary; detailed CSVs have **not** yet been provided to this agent for Stage 86. Do not assert exact per-frame backend costs or identify an individual OS/GPU cause from only the printed summary. The final `changed_channels=0` appears in the owner log; the runner's own assertions require actual-MDI screenshot parity and CPU invariants in every mode before completing successfully.

### Stage 87 analyzer implementation

Without changing the original CSV input format, the analyzer now:
- Identifies contiguous **measured-frame** slow intervals above the previous diagnostic 1.2ms CPU threshold, including first/last frame indices of the longest slow streak.
- Separates each slow interval's **median CPU time in two Metal submit sub-stages** (`shadow_backend_ms + opaque_backend_ms`) from the rest of synchronous render CPU time (`render_cpu_ms - backend_sum`). The remainder includes all other CPU work; it **must not be labelled a GPU wait**.
- Reports summed median backend costs for slow MDI frames, longest slow interval and worst frame to `summary.csv`, and prints concise per-run attribution lines.
- Preserves existing per-run medians, P95, MDI submission counts, raster parity gates, run order and all data sources. No GPU timers and no production flags changed.

### Validation performed and next precise instructions

**Performed:** analyzed actual owner-reported five-run summary and modified GitHub analyzer source. **Not performed:** running changed Python analyzer, verifying all per-frame values from owner CSVs, native CMake, CTest, new Metal tests, GPU timestamps, Vulkan and CI. PR remains Draft.

Re-run the analyzer only against the already generated five CSV files — no GPU re-render or rebuild needed:

```bash
cd /Users/aleksandrfeer/Projects/AIVERSE-batching-stage82 || exit 1
git fetch origin perf/auto-batch-cost-metrics-20261119 || exit 1
git switch --detach origin/perf/auto-batch-cost-metrics-20261119 || exit 1
python3 tools/batching-stage86-analyze.py \
  build/batching-stage82/stage86-trials 1024
```

The output is `build/batching-stage82/stage86-trials/summary.csv`; share that CSV plus any slow-run `auto3-frames.csv`, `mdi1-frames.csv`, `auto2-frames.csv` to diagnose the stages. A direct read of the raw CSV is needed before concluding whether backend encoding, render setup or process scheduling dominates. Keep primary AIVERSE conflict worktree untouched.

**Next engineering action:** if the slow intervals clearly correlate with Metal backend submit CPU stages, add independent opt-in timing around the Metal CPU ICB encoding loop/argument-buffer stage and/or GPU command buffer waits; if they correlate with other CPU work, inspect command submission cadence and renderer preprocessing. Run more clean A/B rotated trials only after locating the dominant cost. Do not enable AUTO by default or merge PR #39 without native checks.


---

## Stage 88 — separate slow Direct vs MDI streaks and use row-paired CPU attribution (2026-11-19)

**Branch:** `perf/auto-batch-cost-metrics-20261119` (Draft PR #39). **Change:** `75500fbf58ff1fde862d77af6863d857ceb09d79` in `tools/batching-stage86-analyze.py`. This documentation is a subsequent commit. **No renderer/Metal/CPU policy changes**; all production paths/flags unchanged.

### Actual owner-provided Stage 87 output (updated Stage 86 CSV analyzer)

The owner successfully executed the Stage 87 analyzer in their Mac Studio worktree and pasted:

```text
Trial    MDI/81    median       p95      late   MDI>1.2  streak  worst frame
auto1        76    0.8063    1.7736    0.7775        12       6          141
mdi1         81    1.5965    1.9701    1.6193        59       8          178
auto2        80    0.8393    1.8285    1.5731        26       5          162
mdi2         81    0.8620    0.9219    0.8726         0       0          159
auto3        80    1.3900    1.8787    1.4392        48       8          173

Slow intervals (>1.2ms CPU render; diagnostic threshold, not SLA):
auto1: longest=134..139, backend_median=0.0000ms non_backend_cpu_median=1.2608ms, worst_backend=0.4898ms worst_non_backend_cpu=1.4717ms
mdi1: longest=165..172, backend_median=1.0396ms non_backend_cpu_median=0.6619ms, worst_backend=1.2015ms worst_non_backend_cpu=0.8278ms
auto2: longest=173..177, backend_median=0.8687ms non_backend_cpu_median=0.7193ms, worst_backend=0.6110ms worst_non_backend_cpu=1.7796ms
mdi2: longest=-1..-1, backend_median=nanms non_backend_cpu_median=nanms, worst_backend=0.6148ms worst_non_backend_cpu=0.3290ms
auto3: longest=148..155, backend_median=0.8040ms non_backend_cpu_median=0.8042ms, worst_backend=0.4861ms worst_non_backend_cpu=1.7400ms
```

**Conservative interpretation:** in the forced MDI1 slow interval, ~1.04ms is attributable to two CPU backend submission sub-stages and ~0.66ms to other synchronous CPU work. In AUTO2 and AUTO3 both backend and other CPU work are material. AUTO1's longest slow interval has zero median measured backend CPU submit time: likely the policy chose Direct on many or all those frames, but the previous analyzer did **not** label frame mode in contiguous intervals, so that remains unverified. The worst frame of a run is also not necessarily within its longest interval. Neither the zero backend metric nor the remainder proves GPU idling or waiting. Only the printed derived summary was inspected; raw five CSVs were not available in this agent environment. All values are CPU wall, **not** GPU execution or FPS.

### Root cause of ambiguous diagnostic attribution

Previous `longest_slow_backend_median_ms` used the sum of two independently computed phase medians, which need not equal a median of per-frame total backend CPU time. It also combined contiguous slow Direct and MDI frames into one interval, making `backend=0` ambiguous. The classification needed to be improved before modifying renderer decisions.

### New code

- `mode_name(row)`: classify by `mdi_submissions>0` on **each actual completed frame**.
- `mode_slow_runs(rows, mode)`: return contiguous slow frames matching exactly that mode; a Direct frame breaks an MDI slow run, and vice versa. The existing overall slow interval and summary columns remain.
- `median_cost(group)`: computes backend = shadow + opaque on each *same* frame, then takes the median of those row-level sums, plus a separate median of remaining synchronous CPU time. The costs are explicitly **not** GPU stalls and the median of backend plus the median of other need not equal the total frame median.
- Extended `summary.csv` fields: mode composition of the longest overall interval; longest MDI-only and Direct-only slow intervals with start/end/count and CPU backend/other medians; paired backend median for all MDI frames and for slow MDI frames.
- Prints a new section `Mode-separated slow intervals (MDI and Direct never share a streak):` after the original diagnostic report.
- No new benchmark run is required; reuse existing `auto1`, `mdi1`, `auto2`, `mdi2`, `auto3` CSV files.

### Validation truth and exact next Mac command

**Performed:** inspected output pasted by owner, used GitHub connector to read and update analyzer. **NOT PERFORMED:** execution of Stage 88 analyzer on actual 5 CSVs, Python unit tests, native CMake/CTest, Vulkan, GPU timestamp probes. Keep PR #39 Draft.

Avoid changing the checkout because its worktree can have an in-progress rebase: fetch and extract the single Python file from the remote Git object to `/tmp`, preserving all worktrees:

```bash
cd /Users/aleksandrfeer/Projects/AIVERSE-batching-stage82 || exit 1
git fetch origin perf/auto-batch-cost-metrics-20261119 || exit 1
git show origin/perf/auto-batch-cost-metrics-20261119:tools/batching-stage86-analyze.py \
  > /tmp/aiverse-stage88-analyze.py || exit 1
python3 /tmp/aiverse-stage88-analyze.py build/batching-stage82/stage86-trials 1024
```

Share `Mode-separated slow intervals` and/or regenerated `summary.csv`; this is the next evidence needed to decide whether to instrument Metal ICB CPU encode vs rendering preparation vs OS scheduling. Once mode-specific CPU costs are established, test any hypothesis in a targeted A/B, do not blindly change AUTO thresholds. Maintain per-stage handoff and preserve default-off feature flags.


---

## Stage 89 — owner validates mode-separated slow CPU episodes; opt-in Metal ICB slow-stage trace (2026-11-19)

**Repository/branch:** `chromealex/AIVERSE` / `perf/auto-batch-cost-metrics-20261119`, **PR #39 Draft**.
**Production code touched:** `engine/gfx/metal/GfxMetal.mm`, commit `a3347861a3c75dccf677c1f1bbdcf890cb0fc4ea`.
**Agent documentation:** this section is in a separate subsequent commit.
**Testing status:** owner's Stage 88 Python analysis ran successfully using the existing five Stage 86 CSVs. **New Metal C++/ObjC++ slow trace has not yet been compiled or executed on Mac Studio**. No GPU timestamps, Vulkan, full CTest, or CI results established for this stage.

### Verified owner evidence (Stage 88 reanalysis)

Five original CPU render() medians/p95, all ms:
```text
auto1  0.8063 / 1.7736   MDI 76/81
mdi1   1.5965 / 1.9701   MDI 81/81
auto2  0.8393 / 1.8285   MDI 80/81
mdi2   0.8620 / 0.9219   MDI 81/81
auto3  1.3900 / 1.8787   MDI 80/81
```

Actual `Mode-separated slow intervals` output from Mac:
```text
auto1: MDI 127..129 (3 frames), backend=0.9812ms, other_cpu=0.7001ms; Direct 136..138 (3 frames), other_cpu=1.2872ms
mdi1:  MDI 165..172 (8 frames), backend=1.0469ms, other_cpu=0.6619ms; Direct -1..-1 (0 frames)
auto2: MDI 173..177 (5 frames), backend=0.8694ms, other_cpu=0.7193ms; Direct 153..153 (1 frame), other_cpu=1.2445ms
mdi2:  MDI -1..-1 (0 frames); Direct -1..-1 (0 frames)
auto3: MDI 175..180 (6 frames), backend=0.9579ms, other_cpu=0.6678ms; Direct 153..153 (1 frame), other_cpu=1.2449ms
```

Interpretation: slow MDI-only episodes occur in both forced-MDI (mdi1) and AUTO. Two CPU backend submission sub-stage totals are significant (~0.87..1.05 ms median during longest slow MDI episodes), and other synchronous CPU work is significant (~0.66..0.72 ms). AUTO1 additionally has slow Direct-only frames. Therefore **not a pure policy-selection bug**; do not change AUTO EWMA or threshold on this evidence. CPU wall-time, not actual GPU duration/FPS; no OS scheduling or GPU-stall cause yet established. The analyzer's per-frame paired backend/other medians do not add linearly to the total CPU frame median.

### Code implementation

Previously `AIVERSE_ICB_PROFILE=1` reported averages across a large submission window, hiding individual slow submission stage breakdown. Added an **explicit opt-in** `AIVERSE_ICB_SLOW_TRACE=1` within the existing CPU Metal `drawIndexedIndirect` backend. After actual `executeCommandsInBuffer` submission, compute the same four measured CPU intervals already used by the aggregate trace:
- `args`: validation/pool/PBR argument setup up to `afterArgs`
- `alloc`: ICB reset + uniform snapshots through `afterAlloc`
- `encode`: C++/Objective-C per-command ICB construction through `afterEncode`
- `residency_execute`: resource declarations plus CPU-side `executeCommandsInBuffer` encoding, through `afterExecute`

If total exceeds configurable `AIVERSE_ICB_SLOW_TRACE_MS` (default `0.15` ms, valid 0.01..100), print `[metal-icb-slow] frame=... slot=... commands=... total=... args=... alloc=... encode=... residency_execute=... threshold=... sample=.../24`. Log **at most 24 events per process**. No clock in the inner draw loop; the no-flag path has only a constant conditional and no per-command cost. This diagnostic does not change ICB buffer allocation, pipeline, encoder bindings, residency, synchronization, fallback, selection or GPU authoring. Logs can still perturb the immediately following frames; **do not use traced run as throughput proof**.

### Next Mac Studio build and targeted validation

Use the existing standalone worktree, but note it has previously been observed with an in-progress rebase. **Do not call `git switch` / `git reset --hard` / `rebase --abort` there.** Create a fresh **detached** dedicated worktree at the remote tip:

```bash
ROOT=/Users/aleksandrfeer/Projects/AIVERSE
WT=/Users/aleksandrfeer/Projects/AIVERSE-batching-stage89
git -C "$ROOT" fetch origin perf/auto-batch-cost-metrics-20261119 || exit 1
git -C "$ROOT" worktree add --detach "$WT" origin/perf/auto-batch-cost-metrics-20261119 || exit 1
cmake -S "$WT" -B "$WT/build/batching-stage89" -DAIVERSE_BUILD_HDR_TESTS=ON -DAIVERSE_BUILD_TESTS=OFF -DAIVERSE_BUILD_EDITOR=OFF -DCMAKE_BUILD_TYPE=Release || exit 1
cmake --build "$WT/build/batching-stage89" --target aiverse_material_batches --parallel 8 || exit 1
cd "$WT" || exit 1
mkdir -p build/batching-stage89/captures
set -o pipefail
AIVERSE_PALETTE_TEST_CASE=auto_stress \
AIVERSE_BATCH_STRESS_ITEMS=1024 \
AIVERSE_BATCH_STRESS_POLICY=mdi \
AIVERSE_METAL_CPU_MDI=1 \
AIVERSE_BATCH_CPU_TIMING=1 \
AIVERSE_BATCH_FRAME_TIMELINE=1 \
AIVERSE_CAPTURE_SUCCESSFUL_MDI=1 \
AIVERSE_ICB_SLOW_TRACE=1 \
AIVERSE_ICB_SLOW_TRACE_MS=0.15 \
build/batching-stage89/aiverse_material_batches unused.obj \
  build/batching-stage89/captures/stage89-mdi-1k \
  2>&1 | tee build/batching-stage89/stage89-mdi.log
echo "Stage 89 exit=$?"
```

A single forced MDI run is a focused probe, not a five-trial performance comparison. Require existing `material palette CPU checks=38 result=passed`, `actual MDI pixel parity ... changed_channels=0`, final `changed_channels=0`, real `measured_mdi_frames=81`, expected CSV, and exit 0. `[metal-icb-slow]` lines may be absent if all submissions stay under 0.15ms. If absent and CPU slowdown does occur, lower the diagnostic threshold to 0.08ms and rerun only if appropriate. If slowdown is unobserved, do not claim backend fixed; compare against a healthy control run and repeat measurements. Capture log plus per-frame CSV.

### Following stage

Correlate specific expensive Metal ICB samples (frame, slot, draw count, four sub-stages) with frame CSV's `shadow_backend_ms` and `opaque_backend_ms`; identify whether slow time comes from actual encode loop, uniform/ICB allocations, or residency/command issuance. Distinguish slow *CPU* path from GPU runtime with independent native Metal command buffer / GPU timestamps when viable. Keep PR Draft and experiment flags OFF by default; no engine performance claim until repeatability.


---

## Stage 90 — Mac Metal CPU MDI cold alloc diagnosed; filtered warm slow-ICB trace (2026-11-19)

**Repo/branch:** `chromealex/AIVERSE` / `perf/auto-batch-cost-metrics-20261119` (Draft PR #39). **Code change:** `f1dcfbb7079fb4a25b738c0bef0e7cca8f18ecc3` in `engine/gfx/metal/GfxMetal.mm`. This handoff is a separate commit. **Stage 90 code not yet compiled/run on Mac.** GPU/CPU experimentation remains explicitly opt-in, all defaults unchanged.

### Real owner-provided Stage 89 Metal evidence

Owner built the real Mac Objective-C++ renderer and ran forced `mdi` with `AIVERSE_ICB_SLOW_TRACE=1`, threshold 0.15ms. Actual output:
- `material palette CPU checks=38 result=passed`; actual MDI raster parity `frame=100 changed_channels=0`; final `changed_channels=0`; `Stage 89 exit=0`; 81/81 measured frames submitted real MDI (810 submissions).
- Forced-MDI `renderer.render()` sampled median 1.6058ms, P95 1.9307ms; late 36-frame median 1.6127ms. It was a **slow run**, matching previous intermittent slow forced MDI results.
- First 24 `[metal-icb-slow]` lines all came from **frames 91..93**, because the diagnostic printed only the first 24 submissions exceeding 0.15ms and the first CPU ICB builds were very expensive.
- Frame 91 slot 0 (227 commands): total 0.92938ms, **alloc 0.81671ms**, encode 0.07067ms, residency/execute 0.03979ms, args 0.00221ms. The cold `alloc` includes ICB allocation/reset, command-buffer and uniform-resource preparation; exact OS/driver cause is not established.
- Most other cold frame 91..93 227-command submissions: total ~0.31–0.36ms, `alloc` ~0.25–0.30ms, `encode` ~0.045–0.055ms. The selected subset therefore proves **cold ICB setup dominated initial slow submissions**. It does **NOT** explain late-frame slowdown: no trace lines for frames after 93 and late median was still 1.6127ms.
- Later final-frame renderer stage: shadow backend submit 0.24667ms and opaque backend submit 0.23925ms, indicating at least some warmed frame could be quick even in slow experiment; no raw per-frame CSV for Stage 89 was provided.

### Stage 90 implementation

Enhances the existing `AIVERSE_ICB_SLOW_TRACE=1` diagnostic with two **optional static parsed flags**:
- `AIVERSE_ICB_SLOW_TRACE_FROM_FRAME=120`: ignore CPU ICB submit events before frame 120; valid 0..1,000,000, default 0. Preserves 24-event budget for warmed frames and intermittent late slow periods.
- `AIVERSE_ICB_SLOW_TRACE_SLOT=0`: optional per-frame submission index filter (valid 0..64); default all slots. Choosing 0 lets us compare similarly sized 227-command groups once per MDI-submitting frame and reduces stdout/trace distortion. `slot` refers to the index of the command submission in the current Metal frame slot's pool; prior logs indicated 0..9 per frame.
- Existing `AIVERSE_ICB_SLOW_TRACE_MS` default 0.15ms and max 24 printed messages stay unchanged. No extra timing inside the per-draw loop, no resource, pipeline, command or GPU behavior modification. The filtering is diagnostic only.

### Actual status and next exact Mac Studio validation

**Verified:** owner Stage 89 native build/correctness and 24 cold trace samples. GitHub source updated for Stage 90. **Unverified:** new Stage 90 compilation, real late-frame filtered trace, distinct CPU vs GPU wait mechanism, GPU duration/timestamps, Vulkan/full CI. Do not conflate CPU wall with GPU runtime.

Because the owner's primary worktree and an older batching worktree have unresolved rebase conflicts, create a separate **new** worktree for Stage 90 (check its path first; do not overwrite existing work):

```bash
ROOT=/Users/aleksandrfeer/Projects/AIVERSE
WT=/Users/aleksandrfeer/Projects/AIVERSE-batching-stage90

git -C "$ROOT" fetch origin perf/auto-batch-cost-metrics-20261119 || exit 1
git -C "$ROOT" worktree add --detach "$WT" origin/perf/auto-batch-cost-metrics-20261119 || exit 1
cmake -S "$WT" -B "$WT/build/batching-stage90" \
  -DAIVERSE_BUILD_HDR_TESTS=ON -DAIVERSE_BUILD_TESTS=OFF \
  -DAIVERSE_BUILD_EDITOR=OFF -DCMAKE_BUILD_TYPE=Release || exit 1
cmake --build "$WT/build/batching-stage90" --target aiverse_material_batches --parallel 8 || exit 1

cd "$WT" || exit 1
mkdir -p build/batching-stage90/captures
set -o pipefail
AIVERSE_PALETTE_TEST_CASE=auto_stress \
AIVERSE_BATCH_STRESS_ITEMS=1024 \
AIVERSE_BATCH_STRESS_POLICY=mdi \
AIVERSE_METAL_CPU_MDI=1 \
AIVERSE_BATCH_CPU_TIMING=1 \
AIVERSE_BATCH_FRAME_TIMELINE=1 \
AIVERSE_CAPTURE_SUCCESSFUL_MDI=1 \
AIVERSE_ICB_SLOW_TRACE=1 \
AIVERSE_ICB_SLOW_TRACE_FROM_FRAME=120 \
AIVERSE_ICB_SLOW_TRACE_SLOT=0 \
AIVERSE_ICB_SLOW_TRACE_MS=0.08 \
build/batching-stage90/aiverse_material_batches unused.obj \
  build/batching-stage90/captures/stage90-mdi-1k \
  2>&1 | tee build/batching-stage90/stage90-mdi.log
echo "Stage 90 exit=$?"
```

Require native build, 38 CPU checks, actual MDI `changed_channels=0`, final pixel parity, real 81/81 measured submissions, CSV and exit=0. Trace threshold `0.08` ms is intentionally below the 0.15ms initial cutoff to catch moderate slow slot-0 submissions; no log entries would mean that slot remained below threshold for all allowed frames, not that every Metal ICB submission was fast. The 24-line budget and optional slot filter intentionally sample, not exhaustively profile.

### Next

Analyze `[metal-icb-slow]` lines from frames >=120 together with Stage 90 `-frames.csv` for frame-level CPU backend/other costs. If warmed `alloc` dominates during slow MDI intervals, investigate pool growth, uniform allocation/reuse, `resetWithRange` cost or driver scheduling without removing explicit bindings. If `encode` dominates, investigate CPU command setup overhead and validated alternatives. If measured backend is fast but overall CPU slow, profile renderer setup/visibility/grouping and OS scheduler. Only make changes with clear evidence, confirm pixel parity and repeated low-variance performance afterward. Keep PR #39 Draft/default opt-ins OFF.


---

## Stage 91 — warm Metal ICB encode dominates; isolate reset vs uniforms (2026-11-19)

**Repo/branch:** `chromealex/AIVERSE` / `perf/auto-batch-cost-metrics-20261119`, Draft PR #39.
**Source commit:** `de9892f6ac6bb33ea1b37f10427697d8e0da4b1f` in `engine/gfx/metal/GfxMetal.mm`. Handoff recorded in a separate commit.
**Correctness:** No change to Metal commands, pipeline, buffer reuse, synchronization, engine defaults or the AUTO selector. Added only one clock reading per *submitted CPU ICB* and two diagnostic values on a slow-trace opt-in. Stage 91 code has NOT been compiled or executed on Mac.

### Owner's actual Stage 90 Mac Studio evidence

The owner successfully compiled the prior Stage 90 code and executed real Metal forced CPU MDI for 1,024 unique primitives with `AIVERSE_ICB_SLOW_TRACE_FROM_FRAME=120`, `_SLOT=0`, `_MS=0.08`. All 38 CPU checks PASS, real MDI pixel parity on frame 100 `changed_channels=0`, final capture `changed_channels=0`, exit 0, 81/81 measured forced MDI frames and 810 actual MDI submissions. Full batched phase `renderer.render()` median **1.4087ms**, P95 **1.9296ms**, late-frame median **1.5407ms**. This is another slow forced-MDI run, showing that mode selection is not the root cause.

24 filtered warm `slot=0`, `commands=227` samples from frames 120..162 had `encode` generally ~0.085..0.113ms, a high sample 0.13658ms on frame 140, `alloc` generally 0.009..0.016ms, `residency_execute` ~0.010..0.027ms, and negligible `args`. E.g. frame 120 total 0.12400ms, encode 0.09287ms, alloc 0.01333ms, residency-execute 0.01771ms, args 0.00008ms. This *selected submission* has ~75% encode CPU cost. The first cold frames from Stage 89 had been alloc/reset-dominated. **Do not attribute the entire ~1.5ms per-frame CPU render duration solely to ICB encoding:** a frame has ten submissions and model prep, and the trace sampled only slot 0. CPU time is not GPU completion/FPS.

### Stage 91 diagnostic refinement

The existing Metal CPU ICB `alloc` metric spans:
(a) creation/resize/reset of `MTLIndirectCommandBuffer` and
(b) private-to-submission uniform-buffer allocations, copy and active binding-table preparation. Those are distinct sources of cost. Stage 91 inserts a timestamp `afterIcbReset` immediately after `[icb resetWithRange:...]` and **only in the opt-in diagnostic output** splits the existing `alloc` interval into `icb_reset` and `uniforms`, both ms. The output remains capped at 24 slow submissions, and the preceding aggregate `AIVERSE_ICB_PROFILE` remains unchanged. The exact same ICB resource binding and lifetime logic remains. This helps determine whether allocation/reset or CPU uniforms work dominates when reproducing slow frames. There is still no per-frame cross-slot aggregate and no GPU timestamps; those require separate work.

### Next owner Mac run

Prefer a new isolated worktree because previous project worktrees had unresolved rebase state. Do not remove or reset them. Clone just a detached worktree from the new branch tip, then compile a focused fixture:

```bash
ROOT=/Users/aleksandrfeer/Projects/AIVERSE
WT=/Users/aleksandrfeer/Projects/AIVERSE-batching-stage91
git -C "$ROOT" fetch origin perf/auto-batch-cost-metrics-20261119 || exit 1
git -C "$ROOT" worktree add --detach "$WT" origin/perf/auto-batch-cost-metrics-20261119 || exit 1
cmake -S "$WT" -B "$WT/build/batching-stage91" -DAIVERSE_BUILD_HDR_TESTS=ON -DAIVERSE_BUILD_TESTS=OFF -DAIVERSE_BUILD_EDITOR=OFF -DCMAKE_BUILD_TYPE=Release || exit 1
cmake --build "$WT/build/batching-stage91" --target aiverse_material_batches --parallel 8 || exit 1
cd "$WT" || exit 1
mkdir -p build/batching-stage91/captures
set -o pipefail
AIVERSE_PALETTE_TEST_CASE=auto_stress \
AIVERSE_BATCH_STRESS_ITEMS=1024 \
AIVERSE_BATCH_STRESS_POLICY=mdi \
AIVERSE_METAL_CPU_MDI=1 \
AIVERSE_BATCH_CPU_TIMING=1 \
AIVERSE_BATCH_FRAME_TIMELINE=1 \
AIVERSE_CAPTURE_SUCCESSFUL_MDI=1 \
AIVERSE_ICB_SLOW_TRACE=1 \
AIVERSE_ICB_SLOW_TRACE_FROM_FRAME=120 \
AIVERSE_ICB_SLOW_TRACE_SLOT=0 \
AIVERSE_ICB_SLOW_TRACE_MS=0.08 \
build/batching-stage91/aiverse_material_batches unused.obj \
  build/batching-stage91/captures/stage91-mdi-1k 2>&1 | tee build/batching-stage91/stage91-mdi.log
echo "Stage 91 exit=$?"
```

Pass criteria: native build succeeds; `material palette CPU checks=38 result=passed`; `actual MDI pixel parity ... changed_channels=0`; final `changed_channels=0`; 81 MDI measured frames with nonzero commands, no backend failure, and exit 0. Inspect `[metal-icb-slow]` lines for **`icb_reset` vs `uniforms`**, alongside `encode`; collect per-frame CSV to correlate late CPU times. Slow-trace output is sampled and potentially perturbs later frames: performance decisions require separate untraced repeated controls. Preserve Draft PR #39 until native verification and external cross-platform tests.


---

## Stage 92 — owner Mac fast forced-MDI run passes; intermittent slowdown not reproduced (2026-11-19)

**Branch:** `perf/auto-batch-cost-metrics-20261119` (Draft PR #39). **Tested code:** Stage 91 `de9892f6ac6bb33ea1b37f10427697d8e0da4b1f`, handoff `77ab4bb94cf848f8cd3327c22f8a60e2e38a5c2a`. This Stage 92 update documents owner-provided **native Mac Studio evidence**; no renderer/RHI/policy change or assistant-run native test.

### Actual results from owner Mac Studio

```text
[100%] Built target aiverse_material_batches
material palette CPU checks=38 result=passed
stress requested_items=1024 authored_unique_primitives=1024 submitted_model_draws=1
batch=0 shadow=1024 opaque=1024 total=2050 error=
actual MDI pixel parity frame=100 changed_channels=0
CPU render benchmark direct: samples=66 median_ms=1.2233 p95_ms=2.3969
CPU render benchmark batched: samples=81 median_ms=0.7739 p95_ms=0.8303 min_ms=0.6899 max_ms=0.9340
stress benchmark policy=mdi measured_mdi_frames=81 measured_direct_frames=0 measured_mdi_submissions=810
CPU render benchmark auto_mdi_submitted_frames: samples=96 median_ms=0.7742 p95_ms=0.8416
CPU render benchmark auto_late_frames: samples=36 median_ms=0.7860 p95_ms=0.8549
successful MDI CPU stage pass=0 samples=96 submissions=480 mean_total_ms=0.34075 grouping_ms=0.01701 submit_ms=0.23464
successful MDI CPU stage pass=1 samples=96 submissions=480 mean_total_ms=0.30895 grouping_ms=0.00729 submit_ms=0.24805
stress frame timeline samples=106 path=build/batching-stage91/captures/stage91-mdi-1k-frames.csv
batch=1 shadow=5 opaque=5 total=12 error=
stress phase=auto model_draw_calls=10 cpu_mdi_submissions=10 cpu_mdi_commands=2048 gpu_icb_submissions=0 gpu_icb_commands=0
changed_channels=0
Stage 91 exit=0
```

The test used `AIVERSE_ICB_SLOW_TRACE=1`, `AIVERSE_ICB_SLOW_TRACE_FROM_FRAME=120`, `AIVERSE_ICB_SLOW_TRACE_SLOT=0`, `AIVERSE_ICB_SLOW_TRACE_MS=0.08`. **No `[metal-icb-slow]` output** appeared, meaning no eligible sampled slot-0 submission crossed the threshold in this process (subject to trace instrumentation/sampling), NOT that other slots or the GPU were fast. The dedicated `icb_reset` vs `uniforms` diagnostic therefore did not produce observations in this run; its compilation and raster behavior still passed.

### Interpretation and next steps

The forced CPU MDI median 0.7739ms vs sequential Direct baseline 1.2233ms implies an observed ~36.7% lower CPU `renderer.render()` median for this specific synthetic 1k-primitive scene. The late 36-frame MDI median 0.7860ms and P95 0.8549ms do not reproduce the intermittent late slowdown in Stage 89/90, where forced MDI medians were 1.6058/1.4087ms respectively. No code optimization took place between those runs, only opt-in diagnostics differed; do not credit the Stage 91 diagnostic timestamp with a speedup. The variation may be influenced by Metal driver/CPU scheduling/contention but no root cause has been proven. GPU timing/FPS and cross-platform validation are still absent.

**Next experiment without changing renderer:** repeat the *same* compiled Stage 91 forced-MDI process at least three times with `AIVERSE_ICB_SLOW_TRACE_FROM_FRAME=160`, no SLOT filter, `AIVERSE_ICB_SLOW_TRACE_MS=0.08`, each with a unique output capture prefix and `AIVERSE_BATCH_FRAME_TIMELINE=1`. A trace capturing all late submission slots will reveal whether a slow recurrence arises in slot0 or another slot, and the generated per-frame CSV can establish whether slow `renderer.render()` coincides with CPU submission stages. Compare against an untraced control to exclude logging effects; preserve submitted count and screenshot parity. Do not rename/rewrite any existing worktree or remove an unfinished rebase.

If repeated runs remain consistently fast, defer further micro-optimizations and proceed to representative 2k/4k load, then controlled 10k with GPU timestamps/memory metrics. Keep CPU MDI/AUTO default-off and PR #39 Draft until validated.


---

## Stage 93 — owner three late forced-MDI runs: two slow, one fast; stage timing evidence (2026-11-19)

**Repo/branch:** `chromealex/AIVERSE` / `perf/auto-batch-cost-metrics-20261119` (Draft PR #39). **Tested code:** Stage 91 through `77ab4bb94cf848f8cd3327c22f8a60e2e38a5c2a`. Owner ran three forced CPU-MDI processes with `AIVERSE_ICB_SLOW_TRACE_FROM_FRAME=160`, all slots and 0.08ms threshold. **This stage is evidence/handoff only; no production or diagnostic C++ changes.**

### Actual native Mac Studio evidence

All three forced MDI runs executed `aiverse_material_batches` and produced `material palette CPU checks=38 result=passed`, real MDI raster `actual MDI pixel parity frame=100 changed_channels=0`, final image parity `changed_channels=0`, 81/81 measured frames submitted 810 CPU MDI groups, 0 GPU ICB, and saved 106-row per-frame CSVs:
- `build/batching-stage91/late-probes/mdi-1-frames.csv`
- `build/batching-stage91/late-probes/mdi-2-frames.csv`
- `build/batching-stage91/late-probes/mdi-3-frames.csv`

CPU `renderer.render()` medians / P95 / late-36-frame medians (ms):

| Trial | Median | P95 | Late median |
|---|---:|---:|---:|
| Forced MDI1 | 1.3936 | 1.8735 | 1.3254 |
| Forced MDI2 | 1.3699 | 1.9353 | 1.2674 |
| Forced MDI3 | 0.8277 | 0.9210 | 0.8297 |

The 24-line `[metal-icb-slow]` output limit was consumed on frames 160..164 for slow MDI1/MDI2, so later intervals **were not observed** by the detailed trace, despite the slow median being sustained. Fast MDI3 reported only three >0.08ms slow samples from frames 190, 193 and 196 (frame 196 is beyond the CSV capture window ending frame 195, so do not attempt to correlate it with the sampled 81-frame benchmark).

For 227-command warm samples from slow MDI1 and MDI2, `encode` frequently ~0.08–0.10ms, with occasional ~0.13–0.15ms. `icb_reset` normally ~0.005–0.008ms and `uniforms` ~0.003–0.005ms; `residency_execute` usually ~0.005–0.02ms. This indicates **sampled warm ICB calls are primarily CPU encoding rather than allocations**, contrary to initial frames 91..93, where cold allocations dominate. It does **not** prove whether all per-frame slowdowns come from encoding because nine or ten submissions and renderer CPU preparation add up, and the trace contains only the first 24 events.

### Interpretation

The intermittent slowdown occurs in forced CPU MDI without AUTO, so further adjusting the AUTO selector is unjustified. There are at least two CPU cost profiles: fast process `renderer.render()` median ~0.83ms and slow processes ~1.37–1.39ms. Causes such as process scheduling, GPU queue waits/CPU state, GPU and CPU thermal/frequency conditions or driver overhead remain **hypotheses**; no GPU timing, system utilization or OS waiting instrumentation collected. Both slow and fast processes pass pixel parity on this fixed 1,024-unique-primitive scene.

### Next exact diagnostic procedure — no source change required

Existing `AIVERSE_ICB_SLOW_TRACE_FROM_FRAME` flag already supports beginning output after frame 175; it does not require another source commit. Use the existing Stage 91 executable and a fresh prefix; either trace just `slot=0` over 175..195 or all slots (24 messages cap can still fill in ~3 frames). Prefer slot 0 to compare same group across frames, but if full backend attribution is required use the existing per-frame CSV that accumulates both passes without the 24-line cap.

```bash
cd /Users/aleksandrfeer/Projects/AIVERSE-batching-stage91 || exit 1
mkdir -p build/batching-stage91/late175
set -o pipefail
AIVERSE_PALETTE_TEST_CASE=auto_stress \
AIVERSE_BATCH_STRESS_ITEMS=1024 AIVERSE_BATCH_STRESS_POLICY=mdi \
AIVERSE_METAL_CPU_MDI=1 AIVERSE_BATCH_CPU_TIMING=1 \
AIVERSE_BATCH_FRAME_TIMELINE=1 AIVERSE_CAPTURE_SUCCESSFUL_MDI=1 \
AIVERSE_ICB_SLOW_TRACE=1 AIVERSE_ICB_SLOW_TRACE_FROM_FRAME=175 \
AIVERSE_ICB_SLOW_TRACE_SLOT=0 AIVERSE_ICB_SLOW_TRACE_MS=0.08 \
build/batching-stage91/aiverse_material_batches unused.obj \
  build/batching-stage91/late175/mdi-175 \
  2>&1 | tee build/batching-stage91/late175/mdi-175.log
```

Require `checks=38 passed`, actual MDI pixel parity 0, final pixel parity 0, real MDI frame count 81/81 and process exit 0. For a more informative answer with **no further GPU runs**, request the three already created `mdi-1-frames.csv`, `mdi-2-frames.csv`, `mdi-3-frames.csv` for the CPU stage time series. In particular, compare same frame ranges and whether both shadow and opaque backend submit costs increase together. Do not equate backend CPU wall with GPU execution. PR #39 stays Draft; experimental flags remain OFF by default.

**Validation truth:** owner-provided Mac native logs were analyzed; GitHub handoff updated. Assistant did not compile/run Mac renderer or perform GPU timing.
