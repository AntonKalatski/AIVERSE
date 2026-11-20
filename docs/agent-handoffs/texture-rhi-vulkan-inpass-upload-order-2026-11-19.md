# AIVERSE Texture RHI — Vulkan in-pass upload ordering guard
Date: 2026-11-19

## Branch / commit
- Branch: `feature/texture-rhi-unification`; main was not changed.
- Implementation: `56644729ab93bba92546dc84d90e18be33d58f0c`
- File changed: `engine/gfx/vulkan/VkResources.cpp`.
- The production public Device API is unchanged. Generic texture and VT code remain independent of RTS/FPS or genre modules.

## Bug identified
`VulkanDevice::transferCmd()` returns `slots_[slot_].pre` whenever a render pass is active, even when the call occurs while `recording_` is true. At `endFrame()`, the Vulkan backend submits the pre command buffer BEFORE the main command buffer. Consequently a `updateTextureRegion()` called during a pass would be placed into pre and execute before that frame's previously recorded draws. In the case of an LRU virtual atlas slice reuse this can reorder a write ahead of in-flight reads of the evicted slice.

## Implemented safety constraint
`VulkanDevice::updateTextureRegion()` now returns false with an explicit error if `inPass_` is true, BEFORE staging memory allocation, image layout transitions or copy commands are recorded. Streamers are required to drain on the graphics-device thread **between** render passes. Outside active render passes the existing Vulkan `transferCmd()`, staging allocation, copy and barriers are unchanged.

This protects the detected in-pass pre-buffer reorder but does **not** guarantee old GPU consumers have retired across frames or across graphics queues. Do not claim comprehensive GPU atlas reuse safety.

## Validation
The source was fetched and inspected; GitHub source update succeeded. **No CMake configure/build, CTest, Metal, Vulkan GPU or GLES execution was performed in this stage.** Actual acceptance is pending. Existing CPU/mock streaming tests do not validate Vulkan command submission order.

## Next actions
1. Build the Vulkan backend on a Linux/Vulkan SDK host. Verify that the in-pass rejection compiles and does not break legitimate texture initialization/streaming calls.
2. Add an opt-in native Vulkan regression with a real render pass and intentional `updateTextureRegion()` attempt, asserting false/error and no corrupted sampled atlas image. Also exercise successful uploads between passes.
3. Review the other `transferCmd()` upload call sites (`createTexture`, buffer updates, etc.) for prebuffer reordering and define a general RHI ownership/sync contract, rather than adding indiscriminate waits.
4. For cross-frame atlas slice reuse, introduce explicit GPU-retirement tracking or an ordered upload/readback integration test with frame-slot fences; test eviction while previous frames still sample slices. Keep performance budgets in mind.
5. Complete Vulkan/GLES framebuffer parity and streaming soak, then run Mac Studio `bash tools/texture-rhi-final-verify.sh --jobs 4 --full-engine`; do not mark PASS until logs prove it.

Keep PR #21 Draft and experimental Metal GPU ICB OFF until full acceptance. Create an additional handoff after each future stage with changed files, implementation, commits, tests actually run, restrictions and next step.
