# AIVERSE LumaWeave — Stage 27: fail-closed ACTIVE graphics resource shutdown

Date: 2026-11-20. Repo: `chromealex/AIVERSE`. Branch: `feature/lumaweave-massive-lighting` (not main).

## Source commits and architecture

- `a0dd77fc853c94ba73b57e47d2d51f37147aed03` — `engine/render/LumaWeaveTextureRetirement.h`: `LumaWeaveGraphicsUseGate` records actual graphics-submission fence or quarantines a zero/unknown fence; it cannot release active resources until a contiguous true GPU completion or a host-proven full idle. A later nonzero token does NOT retroactively prove a previous zero-token submission completed.
- `a5df6206993275e9c50483e3651c4bfe8a8eb37a` — `engine/render/LumaWeavePostProcess.h`: mark graphics use when a material or surface receiver render pass is handed to Renderer3D, and on additive fullscreen HDR composition. `shutdown()` returns false before destroying active shader/pipeline/targets or producer when the graphics-completion gate is not proven. `drainRetiredAfterGpuIdle()` explicitly clears the gate only after the host's full-device/queue idle proof and successful producer drain.
- `ddf47303fcc2485a50fec4f64af256a210684b8a` — `engine/render/LumaWeaveNativeDriver.h`: native-game `shutdownAfterGpuIdle()` convenience, forwarding producer/target full-idle drain then shutdown. The caller, NOT this wrapper, establishes the actual GPU idle barrier and polls any earlier Metal compute callback. Existing `shutdown()` now returns false when unsafe.
- `84354aa395896487132208f4d9af04de1efbbb35` — `tests/render/lumaweave_gpu_packet.cpp`: CPU-only executable regression source covers active zero-token + later valid token, exact completed-fence gate, loss of fence availability, full-idle reset.

## Contract / limitations

- This stage protects **ACTIVE** Luma material/normal/HDR graphics resources at explicit ordinary shutdown, complementing Stage26's old target retirement.
- Source behavior is fail-closed on the old RHI (no `gpuSubmissionFence/gpuCompletedFence`): after drawing Luma, ordinary `shutdown()` may return false until `shutdownAfterGpuIdle()` is called at a host-proven device-idle point. Do not call this convenience API while rendering; it is not itself a wait/idle check.
- Current main exposes real GPU submission/completion tokens; after an eventual controlled integration, natural nonblocking safe shutdown may proceed for all positively tagged and completed graphics work. A zero-token history still needs full idle. No CPU frame serial or compute callback may substitute.
- If a caller ignores `shutdown()==false` and simply destroys the owner or GPU device, unconfirmed raw handles may be orphaned. Host must retain the owner and device and retry after real GPU idle, or use a platform-controlled device-loss policy. This is not full device-loss recovery.
- The Metal-native effect and shader ABI, Vulkan/GLES fallback, light-source identity, opt-in default, ECS authoritative state and save files are unchanged.

## Actually performed verification

GitHub fetch and acknowledged source commits; reviewed source tests and branch's limited RHI surface. Native C++ compilation, `lumaweave_gpu_packet` CTest, Mac Metal shader compile, Metal GPU marker vs framebuffer fence, Vulkan/GLES parity, memory profiling and full AIVERSE suite are **NOT RUN**. No success can be claimed from source commits alone.

## Next action for next agent

1. Integrate with current main's shared `GpuResourceRetirement` and real device-fence surface, preserving Stage26/27 semantics and all other render agents' modifications. Avoid directly merging a branch thousands of commits behind main.
2. Add a mock-device Luma PostProcess/Screen shutdown regression on actual source instantiation; verify receiver pass and compositor marks, immediate shutdown false, real completion true, GPU-idle drain and no double deletes. Run native CMake/CTest once feature freeze is reached.
3. Validate finite light/material/specular paths, Luma + GI ownership, metal/vulkan/gles fallback visuals, texture allocation pressure and real GPU timings, then a final whole-project acceptance against an exact SHA before main promotion.
