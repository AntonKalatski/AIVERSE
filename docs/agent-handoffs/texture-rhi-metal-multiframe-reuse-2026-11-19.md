# AIVERSE textures — queued Metal atlas slice reuse GPU regression
Date: 2026-11-19

## Branch and commits
- Branch: `feature/texture-rhi-unification`; do not merge main.
- Test implementation: `9f81a0ecc2367fc8c9e4f18b992ce0f374ff9890`.
- Changed file: `tests/render/metal_texture_gpu.mm`.
- This handoff is separate from the implementation.

## What changed
Added `orderedArrayReuse(Device&)` to existing opt-in real Metal texture GPU acceptance test. It creates a non-auto-mip RGBA8 Texture2DArray with two slices and two mip levels, uploads a pinned-root color into slice 0, then queues eight consecutive frame-local writes of different colors to slice 1 using the production `Device::updateTextureRegion` path. A `readTextureAsync` is queued after each write in the same frame, using an independent snapshot/callback. Subsequent frames overwrite slice 1 before prior readbacks have necessarily retired; every callback's bytes must match its unique frame's color. Finally, a separate GPU readback verifies slice 0 still contains the pinned-root bytes, and `gpuHealthy` must stay true.

This probes ordering of GPU array uploads, same-command-buffer readbacks and subsequent overwrites and exercises the new staging-blit path from Metal commit `ae9304c6191f96dcb34930b196fc125a45675476`. It DOES NOT itself assert full virtual page-table residency, dynamic LRU selection, or cross-queue/external GPU access. Existing virtual texture physical atlas/disk LRU tests in this fixture cover separate page-table semantics.

## Test integration
The test is called from the existing `metal_texture_gpu` target's `main()`; no new CMake target or opt-in flags. The independent snapshots are not compared using the fixture's `verifyReads()` until all queued frames have completed, because that function initiates its own frame. The test then calls `verifyReads()` to validate the pinned root.

## Real validation
GitHub source commit succeeded. NO macOS Metal build, source compiler, CTest, GPU readback, Vulkan or GLES execution occurred after this change. Do not claim PASS or that frame concurrency was observed; the test queues eight frames but GPU processing speed can vary.

## Next actions
1. Run Mac Studio `metal_texture_gpu` as part of the consolidated final suite, ideally also with Metal validation enabled. Fix compile/API errors and inspect every native readback.
2. Extend to an explicit page-table scene using eviction while old-frame shader draws still reference that slice; include deterministic GPU load to guarantee overlap if feasible.
3. Audit glTexSubImage3D GLES WebGL ordering and native Vulkan atlas back-to-back read/write parity. Add device-loss and stress recovery cases.
4. Run one-pass `bash tools/texture-rhi-final-verify.sh --jobs 4 --full-engine` when all texture features are complete. Keep PR #21 Draft until acceptance.
5. Keep recording implementation, API, modified files, real tests, limits, commit and next action in a new handoff after each stage.
