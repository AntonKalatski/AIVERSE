# AIVERSE Texture RHI — Vulkan staging failure propagation
Date: 2026-11-19

## Branch and commits
- Branch: `feature/texture-rhi-unification` (main unchanged).
- `engine/gfx/vulkan/VkDevice.h`: `952dee7079bba993d19db120cf4f0caadf1c09dc`.
- `engine/gfx/vulkan/VkResources.cpp`: `ba0b56ec82770e4c5d9d1eb3f5222fff18c9dab1`.
- This handoff is a separate commit.

## Implementation and API
The private `VulkanDevice::uploadImage` helper now returns `bool`: false on stagingWrite allocation failure, true after staging bytes are copied and vkCmdCopyBufferToImage is recorded. The helper does NOT wait for GPU execution.

`VulkanDevice::createTexture` now checks uploads for authored mip arrays and initial image data. On failure it returns invalid TextureHandle and defers release of VkImage, VkImageView, and allocation until recorded references have retired, rather than silently creating an uninitialized resource. `updateTexture` now delegates its 2D mip-zero update to the checked `updateTextureRegion` path, preserving row pitch and mip-generation behavior while avoiding unchecked staging failures. The existing `updateTextureRegion` upfront staging validation remains from the preceding stage.

No public RHI interface or genre-specific module changed; only Vulkan backend-private behavior.

## Verification status
Source writes to GitHub succeeded. **No CMake configure, C++ compiler, CTest, Vulkan GPU, Metal GPU, GLES runtime or injection test was executed after these commits**. Do not report build PASS or GPU correctness yet. Native Vulkan driver behavior and deferred destruction timing remain to be measured.

## Risks and follow-ups
- Confirm build and explicit failure-path behavior with deterministic staging allocation failure injection. Test both initial mip-chain creation and legacy updateTexture.
- Check GPU serial handling when Vulkan staging allocations fail after a transfer command buffer is acquired, and verify deferred destruction does not free referenced allocations prematurely.
- Review additional `stagingWrite` uses such as buffers, texture readbacks and shader input upload; this stage targeted texture initial/region/legacy paths.
- Prove in-flight atlas slice reuse safety and table visibility across command buffers/frames, not merely CPU queue ordering. Vulkan layout barriers alone do not certify fence lifetime.
- GL/GLES upload errors, runtime backbuffer comparisons, high-pressure streaming soak and physical mip GPU filtering are pending.
- After feature closure run Mac `bash tools/texture-rhi-final-verify.sh --jobs 4 --full-engine`, then suitable Vulkan/GLES hardware gates. Keep PR #21 Draft and GPU-authored ICB off until verified.

## Agent handoff requirement
At every stage record: actual implementation and APIs, changed files, branch and commits, tests actually run with their results, risks and exact next stage. Do not mistake a code commit for a passing test.
