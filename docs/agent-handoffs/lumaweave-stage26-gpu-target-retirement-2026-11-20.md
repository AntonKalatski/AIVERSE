# AIVERSE LumaWeave — Stage 26: bounded, GPU-fence-safe target replacement

Date: 2026-11-20. Repo: `chromealex/AIVERSE`. Branch: `feature/lumaweave-massive-lighting`. No main merge or GPU signoff.

## Problem and exact source commits

The branch predates the current main RHI `Device::gpuSubmissionFence()/gpuCompletedFence()` methods. Before this stage, a camera/material resolution change called `destroyTexture()` immediately on the outgoing Luma HDR composite-depth, opaque material/normal+roughness target pairs and direct compute output/history targets. `LumaWeaveScreenMetal::pending_` protects compute completion ONLY; a prior graphics composite may still sample the output when the compute callback has completed. CPU frame numbers and the compute marker do not prove graphics completion.

- `75072cc30f721717e7c98755b6d98779a0ed658a` — new `engine/render/LumaWeaveTextureRetirement.h`: bounded queue (48 texture handles) with explicit prepare-before-replace, per-handle real submitted-GPU token or zero for unsupported RHI, nonblocking `collect(completedFence)` (only nonzero ordered completed tokens can free), and full graphics-GPU-idle-only `drainAfterGpuIdle()`. C++20 `requires` adapters detect optional newer device submission/completion APIs without adding fake fences to old backends.
- `c21f28e2f6da2970a13b244540574f9f52b2ca67` — `engine/render/LumaWeaveScreenMetal.h`: replaces immediate destruction of five old compute/output/history RGBA16F textures at resize with submitted-token quarantine, collects on `resize`, adds retirement count/collect/explicit GPU-idle drain APIs and blocks ordinary `shutdown()` if retired textures or pending compute remain. New textures have transactional allocation rollback; old output state remains usable if retirement queue is full.
- `d2db1d262980465620e9db37346c42644a02780e` — `engine/render/LumaWeavePostProcess.h`: moves old material/receiver-depth, normal/roughness/receiver-depth, and additive compose-depth targets into the same independently bounded retirement queue. Nonblocking collector called at `scenePass`; explicit `drainRetiredAfterGpuIdle()`, counts and ordinary `shutdown()` guard. No Luma output or material is sampled from a recycled old handle.
- `77fb0916791735dc1b2160d0cf23974083abac1f` — `tests/render/lumaweave_gpu_packet.cpp`: extends existing CTest source to assert two ordered completed tokens release exactly those handles, unfenced token stays quarantined, full idle drains the residual, bounded capacity rejects excess and optional old/new RHI fence interface detection works.

## Runtime contract

- On the current older Luma branch, Device has **no** GPU submission/completion interface; submitted token is always zero. This is intentionally conservative: only a host-proven **full GPU idle** permits reclaiming retired images. A fast viewport-resize loop may hit the 48-handle bound and fail closed instead of silently destroying in-flight resources or allocating unlimited VRAM.
- On integration into current main, when the source actually compiles with the newer RHI that exposes real completion-fence methods, old targets tagged with nonzero tokens can be reclaimed nonblockingly on the graphics owner thread. Unknown zero-token targets remain quarantined. `frameSerial` is not a completion fence.
- The host must call `post.drainRetiredAfterGpuIdle()` **only after proven full queue/device idle, outside passes**, and after any producer compute callback is polled; then normal `shutdown()` may proceed. `shutdown()` still assumes host idle for remaining ACTIVE resources, pipelines and shaders. This stage does not invent an automatic wait/fence in old RHI or certify teardown after device loss.
- Caution: standalone `LumaWeaveScreenMetal` has its own equivalent GPU-idle drain; a host using it without the PostProcess must drain explicitly.
- Source is opt-in and genre independent. No ECS save/network changes, no light transport shader or Metal ABI changes, no Vulkan/GLES implementation claims, no default-on lighting.

## Verification truth

- **PERFORMED:** connected GitHub source inspection, RHI and main retirement-contract comparison, four acknowledged source commits, existing CMake test source inspected.
- **NOT RUN:** native macOS/Metal or Linux compiler/CTest, `lumaweave_gpu_packet` execution, actual GPU fences/texture readbacks, shader compilation, Vulkan/GLES hardware or full AIVERSE acceptance. Test source was authored but is not a passing test result.

## Risks and next action

1. In a dedicated integration worktree, reconcile `feature/lumaweave-massive-lighting` with current main's actual `GpuResourceRetirement` and new RHI fence APIs. Prefer using its shared queue directly once available; remove compatibility adapters if redundant.
2. Test resize cycles on multiple Scene/Game cameras while old GPU composites are in flight, including 48-token saturation, callback failure, shutdown under pending work, explicit full-idle drain and device loss. Distinguish compute completion vs graphics queue completion.
3. Audit ACTIVE target and pipeline teardown: GPU-idle precondition must be enforced by native host before `post.shutdown()` (this stage addresses replacement, not a proven final active-program destruction fence).
4. Run one exact-SHA final Clang/Metal, GCC/Clang/Vulkan lavapipe+validation, Android/GLES/WebGL and full CTest acceptance after all lighting features are frozen; collect real screenshots and GPU timestamps, then consider main merge.
