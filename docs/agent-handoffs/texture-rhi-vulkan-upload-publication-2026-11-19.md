# AIVERSE Texture RHI — Vulkan staging/publication failure safety
Date: 2026-11-19

## Branch, implementation, affected API
- Branch: `feature/texture-rhi-unification`, no main merge.
- Implementation commit: `2f7dc4e6dc78177b45902d01682a88cc4d10dbd7`.
- Modified `engine/gfx/vulkan/VkResources.cpp`, specifically `VulkanDevice::updateTextureRegion`.
- Generic `gfx::Device::updateTextureRegion` and `VirtualTextureStreamer` public APIs unchanged. This belongs to genre-independent graphics infrastructure.

## Verified in source review (not runtime validation)
- Virtual texture upload queue treats `Device::updateTextureRegion == true` as successful GPU upload **submission** and publishes the physical page only after all physical mips have submitted successfully.
- Metal code uses `MTLTexture::replaceRegion`; GL33/GLES3 uses `glTexSubImage*` with a final `glGetError`; Vulkan records copies into `transferCmd()` with image barriers.
- Previously Vulkan `updateTextureRegion` called a `void uploadImage` helper. `uploadImage` can simply return when `stagingWrite()` fails. That meant `updateTextureRegion` would return true and falsely acknowledge a dropped texture copy, permitting a virtual page to become resident without correct bytes.

## Implementation
`updateTextureRegion` now reserves staging with `stagingWrite` and copies the packed bytes **before** recording image layout barriers or copy commands. It returns false if staging allocation fails. Successful staging uses `VkBufferImageCopy`, `vkCmdCopyBufferToImage`, and the existing Vulkan image barriers and mip regeneration. This keeps ordered copy and page-table publication within the backend's command stream, without forcing a device stall on every physical mip.

## Important limitations / tests NOT run
- GitHub source write completed. No CMake build, Vulkan execution, Metal, GLES, or CTest has run since this change.
- `true` still means **recorded GPU transfer**, not a signaled GPU fence or a completed rendering result. Runtime queue ordering, descriptor visibility, cross-frame render hazards and device-loss recovery must be established separately.
- The generic `uploadImage` helper used by initial image creation and `updateTexture()` still has its prior void/nonpropagating failure contract; consider refactoring it to return bool and propagating error in those callers in a later stage.
- Streaming/LRU mock tests cannot prove real GPU synchronization. The platform shader/streamer tests remain subject to one-pass final acceptance.

## Next steps for AI agents
1. Inspect all `uploadImage` call sites and extend allocation error propagation to initial texture creation and older single-region paths, ensuring partial allocations are destroyed correctly on failure.
2. Add deterministic Vulkan staging allocation failure injection with an independent test that verifies `updateTextureRegion=false` and that the VT streamer never publishes the page. Avoid relying on real OOM.
3. Verify GPU ownership/queue ordering for previous in-flight reads before reusing physical atlas slices. Add synchronization only where needed, not a global stall for every mip.
4. Complete runtime GLES and Vulkan readback parity; run a long streaming soak under high eviction/cancellation pressure.
5. After all feature changes, run `bash tools/texture-rhi-final-verify.sh --jobs 4 --full-engine` on the Mac Studio, plus Vulkan/GLES tests on suitable devices. Keep PR #21 Draft and GPU-authored ICB default OFF until validated.

Each next stage needs another handoff documenting actual tests and their outcomes. 
