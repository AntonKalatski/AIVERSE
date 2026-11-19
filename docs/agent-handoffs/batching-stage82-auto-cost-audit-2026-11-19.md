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
