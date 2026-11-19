# SpectraFX Stage 109 — production playback stress + configurable Metal GPU churn

Date: 2026-11-20  
Branch: `feature/spectrafx-generic-cue-queue`  
Code commits:
- `a5bf5979a17019a2e8e4b4170922698e627d4a89` — deterministic production HybridPlayback stress, configurable real Metal RHI churn, live resource telemetry.
- `5310c44c0d500b5b2bb74e5299bd2b458f4f5ded` — clarify Stage 107 HDR/Bloom fixture's center-pixel assumption: screen-space bloom can spill across physically occluding geometry.
**Previously tested hardware source:** `c4fc948535557f1cc1a00459d1a3cd48257ad6ca` on Apple M3 Ultra, 126/126 full CTest and 4/4 hardware MetalGPU PASS. Stages 106–109 are **new and untested natively**.

## Implementation

### Deterministic production playback stress

`tests/spectrafx_retirement_test.cpp` now exercises **1024** same-key Play→replacement→`resetPresentation()` cycles. This creates/retires at least **2048** real `SpectraFxHybridEmitter` GPU-resource owners via production `SpectraFxHybridPlayback` against a controlled fake Metal-capable gfx device (not a replacement for the real GPU tests). The fixture:

- Retains GPU allocations for the frame-serial retirement window and verifies no duplicate active keyed emitter.
- Verifies `usedGpuSlots()` never exceeds the production playback cap (65536), peak retiring slots stay bounded (<=512), and peak live RHI resource handles stay bounded (<=4096).
- Drains all pending owners, checks dedicated retired/released counters match, double-destroy detection, `usedGpuSlots()==0`, and exact shared resource teardown.
- Reports frame count, retired emitters, peak reserved slots, peak live handles, lifetime handle allocations and CPU wall milliseconds. Wall time is diagnostic only: it does not fail on slower CI hosts.

### Real GPU stress configurable without slowing default acceptance

`tests/spectrafx_metal_rhi.mm` now accepts two optional bounded env variables:
- `AIVERSE_SPECTRAFX_RHI_STRESS_FRAMES` = integer **24..2048** (default 24)
- `AIVERSE_SPECTRAFX_RHI_STRESS_CAPACITY` = integer **1..4096** (default 1)

For each frame, it initializes real Metal GPU buffers/textures for the requested particle capacity, encodes evolution and an actual RHI render pass, and keeps GPU resources alive through the 3-in-flight frame-serial window. It reports total frames, particle capacity, maximum **nominal** (not driver-resident) outstanding GPU payload in KiB, pending retirement count and `gpuHealthy()`. Zero retired resources remaining and healthy RHI remain assertions. This tests churn and bounds, **not** OS-level GPU residency or memory fragmentation, which require independent Instruments/counter work.

Example of the real stress scenario on M3 Ultra, together with the final strict acceptance (note that the full / focused CTests run multiple times, so stress is repeated by the acceptance harness):

```bash
cd ~/Projects/AIVERSE-spectrafx-validation
git status --short
git fetch origin
git switch --detach origin/feature/spectrafx-generic-cue-queue

AIVERSE_ACCEPTANCE_REQUIRE_METAL=1 \
AIVERSE_SPECTRAFX_RHI_STRESS_FRAMES=512 \
AIVERSE_SPECTRAFX_RHI_STRESS_CAPACITY=256 \
AIVERSE_SPECTRAFX_SCENE_CAPTURE=/tmp/spectrafx-scene \
  bash tools/spectrafx/acceptance.sh build/spectrafx-validation
```

For a quick standard acceptance, omit the two stress env variables; the RHI fixture defaults to 24x1 as in previously passing Stage 105. **No run has yet been made with the new source or stress parameters.**

### PRISMA depth/bloom test correctness

The previous Stage 107 fixture asserted that the *final post-bloom* screen-center pixel must match the no-FX baseline to within five red channel values. This conflates **raw depth occlusion** (which is correctly tested before postprocessing in Stage 104) with intentional screen-space bloom spread from nearby particles, which can illuminate pixels behind the foreground box after compositing. The assertion now requires the center halo's red channel difference to remain below the maximum visible FX contribution. The fixture keeps the strong baseline raw Renderer3D depth test intact. This fix is source-authored, not a reported hardware outcome.

## Boundaries and actual verification

- Core remains genre-independent. Only native tests and test diagnostics changed. No gameplay/ECS world authority, FPS/RTS/Editor code, PRISMA source, RHI interface or serialized assets changed.
- GitHub current branch source was inspected, exact replacement anchors confirmed, code pushed by commits above.
- **Native CMake/Clang, full CTest, Metal 4/4, PRISMA HDR, sorted-alpha two-camera, 1024-cycle CPU policy and 512x256 GPU stress HAVE NOT RUN on current SHA.** Stage 105's older result cannot be used for these stages.

## Next tasks for subsequent agent

Do **one strict native acceptance** with stress env variables and optional PPM captures as above, record exact SHA and output `spectrafx_retirement`, `alpha_views_first`, `prisma_hdr`, `spectrafx_metal_scene` and `metal_evidence`. For any failure, inspect actual compile/link/shader/pixel diagnostic and implement precise fixes with a new stage handoff. Visually inspect the saved PPM images (not received in this session), and perform interactive Editor Preview pause/step/orbit/hot-reload QA. For production readiness, review true Metal residency/driver leaks and frame pacing with Instruments, and verify Vulkan/GLES CPU fallback. Do not merge divergent `main` or any other agent branch without targeted conflict review.
