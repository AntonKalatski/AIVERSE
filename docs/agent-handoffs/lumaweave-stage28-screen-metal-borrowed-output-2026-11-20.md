# AIVERSE LumaWeave — Stage 28: screen Metal output borrower completion gate

Date: 2026-11-20. Repo `chromealex/AIVERSE`, branch `feature/lumaweave-massive-lighting` (not main).

## Source change and real problem

- Source commit `4fdba702433055ce06181aff49ab27bd392170de` — `engine/render/LumaWeaveScreenMetal.h`.
- Stage27 gated `LumaWeavePostProcess` graphics targets, but its member `LumaWeaveScreenMetal` owned the actual filtered HDR output images and GPU compute programs. Its destructor calls `shutdown()` independently. If the parent had rejected shutdown due to a pending graphics read while the compute callback itself had already completed, the child could otherwise destroy its active Metal output resources while a later HDR composite still sampled them.
- `LumaWeaveScreenMetal` now carries its own mutable `LumaWeaveGraphicsUseGate`. Returning a valid texture from `encodedOutput(serial)` or `confirmedOutput()` registers the borrowing graphics use with a real Device submission fence when available. A zero/unknown fence blocks normal shutdown until the host proves full GPU idle. This is a conservative borrow: a host that asks for a handle but does not actually draw may still need to drain at idle.
- `canReuseConfirmedFor()` is now a read-only readiness probe: it examines readiness/history/handle directly, without calling `confirmedOutput()` and spuriously registering a borrowed GPU reference on a mere feasibility test.
- Ordinary `shutdown()` refuses both pending compute, pending retired handles, and live/unproven screen-output graphics readers before deleting buffers, textures or compute programs. Explicit `drainRetiredAfterGpuIdle()` clears the graphics borrower gate only after the caller asserts/proves the FULL device/queue idle.

## API and compatibility

- Public method signatures and shader ABI remain unchanged; no game ECS, save/network or CPU/GLES/Vulkan light behavior changes. Existing `LumaWeavePostProcess` takes a normal borrowed screen texture from the producer and now protects BOTH owners. Standalone clients borrowing these textures also inherit the guard.
- The older branch's `gfx::Device` exposes no GPU completion timeline, so borrow submission tokens remain zero and an explicit host-proven graphics idle is mandatory before shutdown. A Metal compute readback callback alone is never sufficient.
- The Stage26 48-handle bounded retirement queue and the Stage27 active material/HDR gate are preserved. Returning an output handle does not itself prove a GPU draw occurred: this intentionally errs on the safe side.
- If a caller destroys the owner without honoring `shutdown()==false`, unresolved driver handles can be leaked intentionally; the host must keep owner/device alive and retry after GPU idle or use a separate device-loss policy. This is not verified complete device-loss handling.

## Verification

- Actually performed: GitHub source inspection, source update acknowledgement and cross-check of existing screen Metal output APIs.
- NOT RUN: native C++ compiler/CTest, Metal shader compile, real GPU lifetime validation, Vulkan/GLES parity, screenshot/performance run. The Stage27 CPU gate fixture exists but does not constitute a runnable end-to-end Stage28 integration test result.

## Next exact step

1. Controlled integration of LumaWeave with current main's `GpuResourceRetirement`, `gfx::Device::gpuSubmissionFence()/gpuCompletedFence()`, and native render owner, without replacing other agents' renderer sources wholesale.
2. Add direct mock-device tests for the producer/wrapper shutdown behavior and a real Metal two-viewport resize/composite/race test; ensure shutdown returns false with incomplete graphics fence and succeeds only when real completion/host idle is proven.
3. Then test Luma direct+Hybrid GI ownership, Metal/Vulkan/GLES fallback, full compile/CTest and GPU frame captures/timings on one exact SHA before merging to main.
