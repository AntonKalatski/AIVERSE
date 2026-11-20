# SpectraFX Stage 113 — actual screenshot review and Metal performance evidence

Recorded: 2026-11-20
Branch: `feature/spectrafx-generic-cue-queue`
**Validated production source SHA:** `441d69e08836888604e0aee6ec71765a51fddddc`
Source code changes: **none**. This stage documents the two user-uploaded artifacts, inspected directly in the current conversation. Stage 112 had already recorded the strict native acceptance (10/10 steps, four real Metal tests, five visual checks); the missing numerical and visual evidence is now available.

## Uploaded hardware log: exact measurements

File: `ctest_metal.log` (native Mac Studio, Metal Apple M3 Ultra). The verbose native test reports:

```
gpu_frame_timing requested=512 scheduled=512 valid=512 invalid=0
p50_ms=0.0849 p95_ms=0.3166
metal_alloc_baseline_kib=1472 peak_kib=1728 after_kib=1664
churn_frames=512 capacity=256 peak_nominal_kib=36
retire_remaining=0 gpu_healthy=1
alpha_views_first=51/229 expected~51/230
impact_queued=1 impact_discarded=1 delivered=0
```

- These timestamps measure **whole-command-buffer GPU frame intervals** in the specific 128 × 128 offscreen stress fixture with 256 particles per emitter, not exclusively SpectraFX compute, not production-game 4K frame time. All 512 requested timestamps were scheduled and all 512 results were valid; median is 0.0849 ms and p95 is 0.3166 ms.
- Metal device-wide currentAllocatedSize baseline **1472 KiB**, peak **1728 KiB**, after **1664 KiB**. Peak grew **256 KiB** above baseline, after declined **64 KiB** from peak but remains **192 KiB** above baseline. These are allocations reported for the entire MTLDevice, not physical GPU residency, not a precise leak detector, nor an isolated SpectraFX allocation measure. Some backend caches and RHI allocations can persist across frames.
- GPU nominal outstanding payload max **36 KiB** (separately counted by the test; not the same metric as MTLDevice.currentAllocatedSize).
- The GPU stress fixture completed all 512 frames, left the retirement queue empty, and reported a healthy device. Opposite-camera depth sorting yielded correct reversed first-slot indices. One stale impact was discarded and no stale impacts were delivered.
- All four real MetalGPU CTests passed. The PRISMA fixture reported `prisma_hdr=1 passes=10 fx_pixels=232 max_red_delta=185 depth_delta=-1` and `failures=0`.

## Uploaded real visual evidence: observed, not just parsed

The user uploaded `spectrafx-visual-review.ppm`, an actual binary P6 contact sheet of **532 × 400 RGB pixels** produced by `tools/spectrafx/review_frames.py`, from the previously successful on-device seven-frame capture. The agent directly opened and visually inspected it; a faithful PNG conversion was created in the active conversation for easier preview.

As encoded in the contact sheet layout (four columns × three rows with 4 px padding):
- **Row 1 / camera A:** baseline blue opaque square on black; effect image shows a red/orange soft circular particle behind that square, with its center occluded; difference map isolates the visible red crescent/ring; post-reset image returns to just the blue square with no visible ghost effect.
- **Row 2 / camera B:** baseline blue square shifted to the left; effect appears as a larger visible orange/red crescent to the right side of the square, consistent with changing the camera view; difference map has the expected changed region.
- **Row 3 / PRISMA:** baseline/effect color composition remains correctly layered with the opaque foreground. The effect's visible red/orange arcs show a subtle brighter halo in the difference map; no glaring background contamination or corrupted geometry was observed.
- These are tiny deterministic **128 × 128 scene fixtures**, NOT an artistic demo or real Editor viewport screenshot. Pixelated/aliased luminous edges are visible at this small output size; do not extrapolate that to production antialiasing quality. No independent objective HDR radiance ground truth is available from LDR PPM captures.
- Contact sheet confirms the prior analyzer result `changed A/B/PRISMA=192/322/232` and **5/5 objective PASS**. The companion source JSON was not uploaded in this turn; no unobserved JSON properties are claimed.

## API/architecture boundaries

No code changes. SpectraFX stays genre-independent (`SpectraFxView` camera packet, presentation-only GPU events); PRISMA owns HDR/Bloom/postprocessing; Renderer3D owns main scene/depth. No need to move game/Editor dependencies into `engine/fx`. The last native-validated SHA remains the Stage 111 source commit; later Stage 112–113 docs commits are not new engine validation.

## Remaining work / next handoff

1. **Interactive Editor GPU Assets Preview** still requires manual on-device QA (drag orbit, zoom, Reset view, Pause/Resume, Step 1/60, live JSON edits/hot reload, resizing and editor reopen/device cleanup). Full executable build/CTest does not validate these UI interactions.
2. Verify GPU allocation stability across **multiple** identical stress runs with an explicit post-retirement baseline; use Metal API Validation and Xcode Instruments for suspected driver heap leaks. The +192 KiB allocation delta from this one run is *not* enough to diagnose a leak.
3. Test perceptual blended alpha particle overlap under changing cameras (current separate GPU-sort-index test validates indices only); visually inspect higher-resolution interactive scene footage.
4. Compare actual production frame pacing (at target resolution and realistic particle counts) to benchmark on the same device; distinguish whole-frame GPU timeline from shader-exclusive cost.
5. Preserve parallel subsystem ownership and inspect divergence with `main` before merging; no automatic rebase or force-push.

Next agent should begin with the current handoff and focus on Editor Preview UI QA or repeatable device-memory baseline instrumentation, then write a new stage-specific handoff including actual executed tests.
