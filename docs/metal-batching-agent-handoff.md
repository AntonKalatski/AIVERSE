# Metal batching / editor performance — agent handoff (2026-11-17)

> **Read this before touching rendering or frame pacing.** This document describes work on branch `fix/indirect-vulkan-base-vertex-safety`, proposed into `feature/universal-model-batching-policy` via [PR #4](https://github.com/chromealex/AIVERSE/pull/4). It is a handoff, **not** a claim that all performance work is complete.

## Goal and local reproduction

Owner builds AIVERSE Editor on Apple Silicon/macOS at `~/Projects/AIVERSE`. A separate GTA project is at `/Volumes/KINGSTON/GTA2`, but **the measured scene and draw-call diagnostic are inside AIVERSE Editor**; do not redirect this investigation to the GTA executable.

```sh
cd ~/Projects/AIVERSE
git fetch origin
git switch fix/indirect-vulkan-base-vertex-safety
git pull --ff-only
cmake --build build-metal -j8
ctest --test-dir build-metal -R aiverse_model_batching_policy_tests --output-on-failure
AIVERSE_EXPERIMENTAL_METAL_ICB=1 AIVERSE_BENCH_UNCAPPED=1 AIVERSE_FRAME_TRACE=1 \
  ./build-metal/AiverseEditor.app/Contents/MacOS/AiverseEditor 2>&1 | tee /tmp/aiverse-frame-profile.log
```

Use Scene > Batching > `Compare A/B same frame`, `Profile Scene renderer`, and Draw Calls visualization. To disable MDI for comparison use `AIVERSE_MULTI_DRAW_INDIRECT=off`. Experimental Metal ICB is intentionally opt-in due to a previously observed Apple AGX driver crash. Do **not** enable it globally without a device stability test. `AIVERSE_MAC_FRAME_HZ` adjusts NSTimer cadence (15–240 Hz), not a guaranteed FPS cap; `AIVERSE_BENCH_UNCAPPED=1` disables Metal display sync for diagnostic runs.

## Implemented in this PR

- `engine/gfx/metal/GfxMetal.mm`: store `MTBuffer::type` (a missing field initially broke compilation), validate indirect buffer type, CPU-readable contents, and model instance-uniform bounds before encoding ICB commands.
- `engine/render/Renderer3DModels.cpp`: Vulkan fallback for nonzero pooled base vertex, because direct and indirect addressing conventions differ. Existing multi-draw grouping, instancing, and geometry pool already existed on the feature base branch.
- `engine/render/GlobalGeometryPool.cpp`: default page size reduced from 8 MiB to 2 MiB, overflow guards; `tests/render/model_batching_policy.cpp` asserts the new small-page allocation. **This does not reduce the occupied vertex/index data itself.**
- `apps/editor/windows/SceneWindow.cpp`: geometry pool utilization/slack, Scene profiling toggle, last completed GPU command-buffer time, and `[scene-frame]` viewport/renderScene breakdown.
- `engine/platform/macos/PlatformMac.mm`: `AIVERSE_MAC_FRAME_HZ`, `[mac-frame]` stage timing every 120 frames, and frame cadence diagnostics.
- `apps/editor/Editor.cpp`: `[editor-frame]`, `[editor-window]`, and newest `[scene-stages]` breakdown for `renderScene()`.

## User-measured results (not synthetic benchmarks)

Same-frame Scene A/B: total CPU/API draw submissions 452 -> 262 (about -42%); model pass draws 276 -> 175 (about -37%). Another capture: 7 indirect submissions for 73 GPU draw commands. Shared pool: 986 models, 118 pages, about 382.9 MiB occupied / 386 MiB allocated (~99.2% utilization). This is **not** proof of proportional FPS improvement: an indirect submission can contain multiple actual GPU draw commands.

Metal GPU command-buffer time was about 1.1–1.2 ms while editor CPU frame time was about 27–40 ms. With uncapped benchmark, FPS was around 35 rather than 30; the FPS limit is **not established as a fixed 30 FPS cap**.

With `AIVERSE_FRAME_TRACE=1`, user reported:
```text
[mac-frame] avg/120 events=0.002 ms prep=0.000 ms begin=0.226 ms editor=28.905 ms end=0.031 ms gpu=1.090 ms
[editor-frame] avg/120 update=0.119 ms ui-setup=0.021 ms windows=27.722 ms ui-render=0.075 ms
[editor-window] avg-top=23.690 ms last-top=scene
[scene-frame] avg/120 viewport=0.014 ms renderScene=23.990 ms total-before-input=24.004 ms
```
Startup/scene-load samples were substantially slower (e.g. 72–152 ms), so distinguish warm steady-state from loading. The `[editor-window]` measurement records average cost of the slowest window per frame but `last-top` is only the most recently observed ID, not a ranked aggregate.

## New confirmed profiling result (2026-11-17)

The owner ran the newest `[scene-stages]` trace. After warmup, two consecutive 120-frame averages were:

```text
[scene-stages] avg/120 build=13.245 fields=0.001 view=8.257 overlays-fx=0.187 renderer=1.601 post-overlays=0.441 ms
[scene-stages] avg/120 build=13.140 fields=0.001 view=8.126 overlays-fx=0.187 renderer=1.607 post-overlays=0.435 ms
```

**Confirmed hotspots:** `buildEditScene(scene_)` (~13.1 ms, 56%) and `view_.draw(...)` (~8.1 ms, 35%). Together they consume ~90% of the warm ~23.5 ms `renderScene()`. `r3d_.render` is only ~1.6 ms. The earlier 36.884 ms renderer sample was during startup and is not representative.

**Immediate code inspection:** `apps/editor/Editor.cpp:buildEditScene` clears/rebuilds scene objects each frame, loops over `d.entities`, invokes `rules().findType(e.type)`, `objectHidden(...)`, and checks `selectionSet` with linear `std::find` per entity. It may also iterate `world_.rows` and call `wantWorld()`. These are *hypotheses* for the 13 ms; measure substeps and entity counts before choosing caching/invalidation or lookup indexing. The `view_.draw` ~8 ms needs separate instrumentation in `modules/rts/view`; identify model submission vs traversal vs terrain/overlays.

Do not simply skip rebuilding scene based only on map revision: selection, hover, visibility, world preview, project rules and planet state also affect output. Preserve correctness and regression-test editing interactions.

## Immediate next step (highest priority)

**The latest `[scene-stages]` measurements have arrived (see above).**, which split `Editor::renderScene()` into:
`buildEditScene`, `fieldPreview_.fill`, `view_.draw`, edit overlays/FX, `r3d_.render`, and post-processing/overlays.

```sh
grep '\[scene-stages\]' /tmp/aiverse-frame-profile.log | tail -10
```

The latest diagnostic commit was `0b9a4f1`. If `build` or `fields` dominates, investigate scene rebuilding / caching and invalidation by map revision. If `view` dominates, profile object traversal, model submissions and culling. If `renderer` dominates, break down `Renderer3D::render` passes and CPU waits. If `post-overlays` dominates, isolate post effects, `endPass` and overlays. **Do not optimize based on guesses or assume GPU culling will fix this 24 ms CPU cost.**

## Constraints / safety

- Keep RHI cross-platform (Metal, Vulkan, GLES3/WebGL2); preserve CPU/indexed fallback and graphics correctness for transparent, skinned, morph, HLOD and LOD models.
- Do not conflate API submissions, indirect command count, actual GPU draw commands, GPU frame time and total editor CPU frame time.
- The A/B button compares draw counts on the same camera/frame, **not** GPU/CPU timing or upload cost.
- Memory statistics exclude textures and non-pooled model buffers; measure total resident GPU memory before claiming mobile readiness.
- Avoid per-frame logging (current traces aggregate 120 frames). Keep diagnostics opt-in.
- Build and run regression tests after each fix; test on the owner's Metal hardware. The latest diagnostic code has not yet been independently compiled/tested by this agent.
- PR is open; do not merge without owner confirmation. Preserve existing game data and rendering defaults.

## First targeted optimization (commit `171fe6b`)

`apps/editor/Editor.cpp::buildEditScene` now reserves output vectors, caches `rules().findType` by type and `objectHidden` by owner/type **per frame**, and replaces per-object linear entity selection searches with an indexed byte array. Caches are deliberately frame-local to avoid stale data after rules/visibility edits. Live-world selections retain existing semantics.

**Not yet benchmarked or compiled on the owner's Mac.** Compare steady-state `[scene-stages] build` and `view` before/after this commit, keeping camera and scene fixed. If build is still expensive, instrument `wantWorld`, entity loop and world-row loop individually. `view_.draw` remains a separate ~8 ms target. Do not claim the 13 ms is fixed without new measurements.

## Object-loop measurements (2026-11-17)

Latest warm 120-frame averages from the owner, with `AIVERSE_FRAME_TRACE=1`:

```text
[view-stages] avg/120 setup=0.318 ground=0.085 objects=8.758 anim-cleanup=1.361 overlays=0.361 ms
[view-objects] avg/120 state=1.447 animate=2.112 drawObject=2.168 ms objects/frame=14772.0
```

Previous warm sample agrees: objects=8.824, state=1.432, animate=2.113, drawObject=2.253 ms. Early frames had 15–17 ms objects and 8–9 ms drawObject: treat as warmup.

**Important:** `objects/frame=14772` counts objects that reach the traced `animate/drawObject` path, not necessarily all scene entities. The three instrumented sub-phases total ~5.73 ms, leaving ~3.03 ms in object loop for ground/height queries, effects, shadows, skipped branches, profiling overhead, etc. Per-object timing itself adds overhead; benchmark without trace before claiming FPS gain.

**Next actions:** (1) Profile remaining ~3 ms with aggregate timestamps rather than 14k+ high-resolution clock calls; (2) inspect `stateFor`, `animate`, `drawObject` and identify truly static model cases; (3) avoid skipping `stateFor` or `animate` indiscriminately because `st.seen`, death/corpse cleanup, selection, hover, construction, animation and effects may depend on per-frame updates; (4) evaluate batched/static submission only after proving correctness; (5) compare scene with trace disabled and inspect GPU memory.

## Latest animation CPU optimization (commit `3a03f93`)

In `modules/rts/view/RtsAnimate.cpp`, computing procedural ground normal used four `heightAt` calls and a rotation even when the model had no procedural modifiers. The calculation now runs only when `!ch.procs.empty() && !st.graph.hasContacts()`, the same condition that consumes `ProcInputs` in `applyProcedural`. The previously resolved `kingstonStride` is also reused during pose correction instead of a repeated FX map lookup. **No animation states or idle clips are skipped.**

Benchmark warm `[view-objects]` `animate` and `[view-stages]` `objects` against the previous ~2.1 ms and ~8.6 ms, with 14,772 objects. This change has not yet been compiled or measured on the owner's Mac; do not assert a performance win before checking. Keep testing programmable graphs, procedural models, contacts, foot IK and weapon poses.

## Verified procedural normal optimization (2026-11-17)

The owner tested commit `3a03f93` on the same ~14,772-object scene. Warm averages: `animate` decreased from ~2.13 ms to ~1.30 ms (~39%); `objects` from ~8.63 ms to ~7.79 ms (~10%). `buildEditScene` remains ~2.93 ms. `drawObject` fluctuates ~2.18–2.24 ms and is not proven improved. `Renderer3D::render` measured ~2.3–2.4 ms in this run, compared with ~1.5–2.2 ms previously; do not attribute unrelated variance to the animation change. The complete `renderScene` stage sum is now ~15.9 ms; FPS still requires separate full-frame measurement.

The next most promising targets are `drawObject` (~2.2 ms), remaining ~3 ms of the object loop not covered by per-object timers, and animation cleanup (~1.4 ms). Avoid unconditional skipping of idle animation or static entity state; first inspect model/animation features and correctness requirements.

## Latest drawObject fast path (commit `5c8d3bc`)

In `modules/rts/view/RtsViewDraw.cpp`, the attachments/effect-anchor block previously calculated `stateBits()` even when a model had neither attachments nor effects. The block now executes only when `!ch.attach.empty()` or a relevant `show->fx` list is nonempty. This preserves state calculation and effect submission for models that actually consume it, including stage choices and tint overrides. It does **not** skip `animate`, `stateFor`, `r.drawModel`, or any geometry.

This is a targeted low-risk optimization, **not yet benchmarked or compiled on the owner's Mac**. Compare warm `[view-objects]` drawObject (~2.1–2.2 ms baseline), `[view-stages]` objects (~7.6–7.8 ms), and full `[scene-stages]` view (~9.6–10.0 ms). Test attachments, weapons, FX anchors and staged model choices visually. If no gain, focus on the remaining uninstrumented ~3 ms object-loop cost; avoid broad static caching until invalidation rules are defined.

## Latest procedural-input fast path (2026-11-17)

Commits `d10016f` and `f39b62a` change `modules/rts/view/RtsAnimate.cpp`: `ProcInputs` initialization, target-angle calculations and terrain-normal samples are now inside the `!ch.procs.empty() && !st.graph.hasContacts()` branch, the only branch that invokes `applyProcedural`. `st.objXf` is reset on every call, and regular pose updates, IK, animation graphs and events remain outside this branch. The second commit fixes the block scope of the first; **use the final branch head**.

This optimization has not been built or benchmarked by the owner yet. Compare warm `[view-objects]` animate (~1.30–1.35 ms) and `[view-stages]` objects (~7.6–7.8 ms), as well as model/animation correctness. If CPU improvement is negligible, investigate remaining uninstrumented object-loop cost rather than repeating speculative micro-optimizations.

## Same-session MDI A/B benchmark (commit `7298992`)

`apps/editor/windows/SceneWindow.cpp` now offers `Benchmark MDI OFF / ON (300 frames each)` when the device supports MDI and the editor is not playing. It freezes the **rendered** camera at start, warms up each mode for 60 frames, then collects 300 CPU timings for the Scene render section and average renderer draw submissions. The result shows mean, P95 and ON-vs-OFF CPU delta. The same loaded Geometry Pool is retained across both phases, unlike disabling `AIVERSE_EXPERIMENTAL_METAL_ICB` at process startup. The scene itself is not frozen: animated objects and time-dependent FX may still vary, so rerun in reverse order if differences are small. The benchmark does **not** measure full editor frame CPU or correctly attributed per-mode GPU time. Do not claim a GPU speedup from this output. GPU frame timings are asynchronous; a future per-command-buffer tagging system would be needed to compare modes reliably.

The benchmark code is committed but **has not been compiled or run on the owner's Mac**. Build and verify both phases render identically, the button works, and the popup remains open long enough to read results. Metal ICB remains opt-in and potentially unstable.

## Metal ICB overhead profiling (2026-11-17)

A fixed-camera 300-frame same-pool Scene CPU A/B reported **MDI OFF 12.939 ms avg / 13.542 ms P95 / 463 submissions; MDI ON 13.609 ms avg / 14.309 ms P95 / 385 submissions**. MDI ON is **+0.670 ms (+5.2%)** slower on this scene, despite fewer API submissions. GPU A/B timings were not attributed, so GPU comparison remains unknown. Keep Metal ICB experimental and off by default.

Commits `8c31b10` and `41dd5de` add opt-in `AIVERSE_ICB_PROFILE=1` instrumentation:
- `[render-indirect]`: command grouping, pipeline setup, indirect-buffer upload, backend invocation.
- `[metal-icb]`: PBR argument preparation, ICB/uniform allocation, per-command encoding, resource residency/execute setup.

Each log reports averages per **600 successful indirect submissions** (not 600 frames). This diagnostic does not instrument unsuccessful/fallback submissions, and is not a full GPU timestamp comparison. **No owner build or measurement yet**. Run with `AIVERSE_EXPERIMENTAL_METAL_ICB=1 AIVERSE_BENCH_UNCAPPED=1 AIVERSE_ICB_PROFILE=1` and grep both log prefixes. The Metal backend currently allocates a fresh ICB, uniform snapshot buffers and PBR argument buffer per indirect submission; this is a plausible CPU hotspot, not yet proven. Preserve resource lifetime, indirect bounds checks, and Apple AGX stability while optimizing.

## Reusable Metal ICB resource pool (commit `4a2e9ce`)

`engine/gfx/metal/GfxMetal.mm` now keeps per-frame-slot (`kFramesInFlight=3`) vectors of per-submission resources: ICB with capacity, eight uniform snapshot buffers and PBR fragment argument buffer. Each slot's cursor resets in `beginFrame` after the existing in-flight semaphore; each indirect submission gets a distinct resource entry for the frame. ICB is reset with `resetWithRange` before encoding; uniform snapshots are copied into reusable shared buffers; PBR arguments are rewritten into reusable storage. Larger command/uniform/argument capacities allocate only on demand. Previous per-frame vectors holding transient ICB/uniform objects are removed; the pool itself retains them. Existing bounds validation, explicit resource residency and `executeCommandsInBuffer` remain intact.

**Critical validation:** this is an experimental Metal change and has NOT been built or exercised on the owner's Apple Silicon hardware yet. Confirm Objective-C++ ARC compilation, ICB reset behavior, multiple submissions per frame, PBR texture/sampler changes, geometry reload, resizing, and several minutes of camera movement without AGX faults. The pool retains high-watermark memory until device shutdown; monitor memory under changing scenes. The existing semaphore is global to in-flight frames, so verify slot reuse is safe under real command-buffer completion ordering. If errors occur, revert this commit rather than disabling GPU bounds checks.

Re-run `AIVERSE_ICB_PROFILE=1`: prior `[metal-icb]` alloc ~0.0565 ms/submission, `[render-indirect]` backend ~0.068 ms/submission. Run fixed-camera 300-frame OFF/ON CPU benchmark again: prior OFF 12.939 ms, ON 13.609 ms (+5.2%). The new pool is intended to remove allocation overhead; a speedup is **not yet established**. GPU A/B timing is still not implemented.

## Full model-pass MDI diagnostics (commit `93f3396`)

Opt-in `AIVERSE_MODEL_BATCH_PROFILE=1` in `engine/render/Renderer3DModels.cpp` emits `[model-batch]` per 120 nonempty renderModels() calls for each mode (ON/OFF) and phase. Columns: select, sort (includes material classification and sorting), draw, scan (compatible-group discovery), attempt (indirect batch submission), and candidate/accepted/rejected/short counts per pass. Run the editor with `AIVERSE_EXPERIMENTAL_METAL_ICB=1 AIVERSE_BENCH_UNCAPPED=1 AIVERSE_MODEL_BATCH_PROFILE=1`, use fixed-camera OFF/ON 300-frame benchmark, and collect `grep '\[model-batch\]'`. Timing instrumentation itself has overhead, and model pass timings are subsets of Scene CPU, not additional frame costs. Empty phases do not emit. No owner build/measurements yet.

## Completion criteria

Identify and materially reduce the warm ~24 ms `renderScene()` CPU hotspot, demonstrate repeatable same-scene timing and draw-count A/B, preserve visual output, validate stability on Apple Silicon, and report remaining GPU-memory/mobile limitations. GPU-driven culling and geometry streaming are **not yet implemented as part of this PR**.
