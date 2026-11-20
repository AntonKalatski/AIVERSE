# SpectraFX Stage 100 — GPU impact callback invalidation and lifecycle regression

**Date:** 2026-11-19
**Branch:** `feature/spectrafx-generic-cue-queue`
**Implementation commits:** `dcd0d38481561c5e5fc85cb1e7881893e5d9ba69` (real Metal impact callback fixture), `a232c4535d221ff684ca81435bedeb97f35d1b36` (production retirement invalidation).
**Most recent validated native baseline:** Stage 97, `4e363db3a6fc0d1631a574165febb7d26e08c3b8` on Apple M3 Ultra; 8/8 acceptance stages, 2/2 native MetalGPU test PASS **at that older SHA only**.

## Problem / why this stage exists

GPU impact feedback is intentionally asynchronous; readback callbacks arrive after a submitted Metal command buffer finishes. Scene rollback, floating-origin reset, entity kill or replacing a keyed emitter can retire a GPU source while its old GPU readback is still in flight. Holding GPU buffers for 3 completed frame slots protects resource lifetime, but **must not keep the old scene's impact notifications logically alive**. Recycled particle slot indices also cannot be allowed to replay impacts for a new particle generation.

## Implemented

- `engine/fx/SpectraFxHybridEmitter.h`: added `invalidateGpuImpacts()`, an owner-thread presentation-only call to `SpectraFxGpuImpactFeedback::reset()`. It increments the callback epoch, resets in-flight state and discards undelivered events. Existing GPU buffers remain allocated for deferred Metal retirement.
- `engine/fx/SpectraFxHybridPlayback.h`: `retireDedicatedEmitter()` calls `invalidateGpuImpacts()` for **all GPU-retaining emitters**, before CPU fallback handles are stopped and before the emitter enters `retiringDedicated_`. It covers Stop, Kill, keyed replacement, expired one-shots and `resetPresentation()`. The immediate CPU-only retirement path still runs `stop()`, which already resets feedback.
- `tests/spectrafx_compile_smoke.cpp`: compile-only public symbol check for the new invalidation function, without any FPS/RTS/Editor includes.
- `tests/spectrafx_metal_rhi.mm`: extended existing real production Metal RHI fixture from Stage 98 with an **actual GPU ground-collision + asynchronous RHI readback**. It requests impact feedback, resets the scene epoch before callback delivery, cycles the 3-slot Metal fence and verifies no old event is published; then emits a new generation in the same physical slot, receives a new GPU impact callback, recycles the slot before draining, and verifies generation mismatch causes the event to be discarded. Checks Metal health at the end and destroys resources only after command buffers have completed.

## API and invariants

```cpp
// Presentation owner thread only; no gameplay/ECS write.
emitter.invalidateGpuImpacts();
// Underlying GPU storage remains valid until the RHI's retirement window.
// User code normally calls playback.resetPresentation() or Stop/Kill,
// which now invalidates feedback automatically during retirement.
```

- Weak callback ownership plus request epoch invalidation stops older GPU callbacks even when the RHI completes them later.
- Recycling a particle slot increments its generation and `SpectraFxGpuImpactFeedback::drain()` rejects the stale handle.
- The GPU callbacks never publish authoritative physics hits, damage or ECS state.
- `gfx::Device` must outlive all emitters, queued GPU feedback and borrowed compute programs. `reset()` remains **teardown-only** and assumes the RHI owner has quiesced outstanding GPU work. Live gameplay must use `resetPresentation()` at an outside-pass safe point.
- The Stage 98 new Metal RHI test uses an independent 3-frame retention queue; this verifies buffer lifetime smoke and callback invalidation but **does not yet stress the complete production `HybridPlayback` retire/recovery path**. The new production invalidation is covered by C++ source/header checks pending native compilation; full runtime integration testing remains a separate step.
- No changes to FPS/RTS/Editor, no PRISMA/PHYSIS/NEREID/TerraWeave coupling, no asset/serialization schema changes.

## Actual checks / status

Source and branch SHA were fetched from GitHub; exact replacement anchors checked, code commits created and fast-forward pushed. Updated test fixture was reviewed against `SpectraFxGpuParticleSlots`, `SpectraFxGpuImpactFeedback`, real `GfxMetal.mm::readBufferAsync` and the runtime GPU collision shader.

**NOT RUN on Stage 98–100:** native configure/Clang, full build, CTest, Metal GPU callback test, real rendered RHI pixels, Editor screenshots, GPU callback timing/leak measurements. Stage 97's successful Mac Studio run does NOT apply to these code changes. Do not claim the new shader test passes before real logs.

## Next action / one command acceptance

On a **clean detached** Mac Studio SpectraFX validation worktree, after checking `git status --short`:

```bash
cd ~/Projects/AIVERSE-spectrafx-validation
git status --short
git fetch origin
git switch --detach origin/feature/spectrafx-generic-cue-queue
bash tools/spectrafx/acceptance.sh build/spectrafx-validation
```

The acceptance script configures/builds core + native hosts, runs the architecture gate, full CTest, SpectraFX and `MetalGPU` tests. **Expected macOS MetalGPU count is now 3**: previous real compute, previous raw Metal billboard, plus the new production RHI/callback test. Check and fix any compiler/RHI/shader/fixture failures in new commits, keeping reported tests tied to exact tested SHA.

After acceptance: integrate a real Renderer3D/Game viewport test with two camera RTs, stress the **actual HybridPlayback** retirement under rapid key churn/reset and async fault injection, measure memory/fragmentation and GPU frame costs, then perform live Editor preview/screenshots. Merge with main only after conflict review and visual/functional QA, not based on older green test logs.
