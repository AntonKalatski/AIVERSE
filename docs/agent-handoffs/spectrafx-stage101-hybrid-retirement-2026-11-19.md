# SpectraFX Stage 101 — deterministic production HybridPlayback retirement test

Date: 2026-11-19  
Branch: `feature/spectrafx-generic-cue-queue`  
Implementation commit: `50df70bd39cf8059c54e75389c81a1c6eb40a306`  
Previous stage: `docs/agent-handoffs/spectrafx-stage100-impact-retirement-2026-11-19.md`

## Summary

Added a **CPU-side deterministic test of actual `SpectraFxHybridPlayback`**, not a replacement simulation implementation. The earlier standalone Metal RHI test uses its own small three-frame retire queue; that alone does not prove the real production playback's `retireDedicatedEmitter()`, `collectRetiredPools()`, `resetPresentation()` and slot accounting under keyed cue churn. The new fixture exercises those production methods using a deliberately minimal fake gfx Device with Metal capabilities and a controllable completed-slot-safe `frameSerial()`. **It is a retirement-policy test, not a real GPU execution test.**

## Changes

- New `tests/spectrafx_retirement_test.cpp` defines `FakeMetal : gfx::Device` implementing only normal RHI resource handles and pass/compute state transitions. `FakeMetal` tracks all allocated handles and fails on missing/double destroys, exposes a frame serial advanced on each `beginFrame`, and rejects in-pass GPU buffer mutations. Nothing is linked to a genre or editor.
- The fixture creates a real `ParticleSystem` and `SpectraFxGpuEmitterLibrary` with a deterministic in-memory JSON GPU emitter definition and instantiates **the real `SpectraFxHybridPlayback`**. GPU policy is forced (`preference:gpu, allowCpuFallback:false, particles:8, additive:true`) so test failures are not silently hidden by CPU fallbacks.
- Validates: a GPU keyed emitter reserves 8 slots, GPU update/render enters the correct fake encoder stages, `resetPresentation` during an active pass returns false with no mutation, Stop retires the key while retaining 8 slots, and resources release exactly after the third newer serial.
- Repeats 32 cycles of same-key Play→replacement→live-scene reset, verifying no duplicate active key, both previous GPU allocations stay budgeted, all retired sources eventually disappear and retired/released counters balance. Fake RHI verifies that destruction does not occur twice and shared compute/renderer resources are destroyed at playback teardown.
- `CMakeLists.txt` adds target `aiverse_spectrafx_retirement_tests`, CTest `spectrafx_retirement`, labels `SpectraFX;Architecture`, on desktop when tests are enabled. `tools/spectrafx/acceptance.sh` adds the new core test target to the one-shot acceptance build.

## Important invariants

- Generic and presentation-only; no game mode/RTS/FPS, no ECS authoritative world mutation.
- Real Metal compute/pixels and readback safety are covered separately by `spectrafx_metal_rhi` (Stage 98/100), which is not yet hardware-tested.
- The fake frame serial models the Metal three-slot completion guarantee. It does not emulate actual GPU timing or catch Metal driver synchronization faults.
- Test `ParticleSystem` is not initialized for drawing: the authored asset forbids CPU fallback and exercises only GPU resource policy.
- Root `gfx::Device` API remains unchanged by this stage; Stage 98 had already added optional diagnostic readback.
- `reset()` remains teardown-only and can destroy resources immediately only after the host has quiesced the GPU. Live scene reset is `resetPresentation()`.

## Actual verification

GitHub connector fetched source, applied exact CMake/script anchors, committed and fast-forward-pushed. **NOT RUN:** C++ compilation, CTest `spectrafx_retirement`, MetalGPU `spectrafx_metal_rhi`, Stage 98–101 full acceptance and editor/scene visual QA. Most recent user-supplied all-green hardware acceptance was Stage 97 on SHA `4e363db3a6fc0d1631a574165febb7d26e08c3b8`, which cannot be attributed to this new code.

## Next action

Complete remaining authored integration/visual work, then run **one complete acceptance** from the clean detached Mac Studio worktree (check `git status --short` before switching):

```bash
cd ~/Projects/AIVERSE-spectrafx-validation
git status --short
git fetch origin
git switch --detach origin/feature/spectrafx-generic-cue-queue
bash tools/spectrafx/acceptance.sh build/spectrafx-validation
```

The new tests should appear as `spectrafx_retirement` (desktop) and `spectrafx_metal_rhi` (macOS), with **3 MetalGPU tests** on an actual Metal Mac. Record exact target/compiler failures, fixture output, GPU result, CTest summary, tested SHA and outstanding limitations. Do not claim Stage 98–101 PASS on authored source. Native Renderer3D multi-RT/editor preview, real high-volume resource churn under 3 frames in flight, and memory/pacing measurements are separate release acceptance.
