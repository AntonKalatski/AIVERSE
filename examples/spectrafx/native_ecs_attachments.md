# SpectraFX native ECS attachment integration — Stage 36

This example is a **C++ integration example**, not a runnable game executable.
The header [native_ecs_attachments.h](native_ecs_attachments.h) is a usable
reference for a native project's presentation module; the host must provide
a `gfx::Device`, viewport, scene rendering, and committed `World`.

## Native project integration

1. Include `<aiverselib/genre/SpectraFX.h>` from your project's native
   renderer module; link `AIVERSE::spectrafx`.
2. Define an effect in `GpuPoolWorld` (the sample uses `"demo_flame"`
   and passes a compiled `fx::EffectDef`). `PreferGpu` uses Metal
   only for the *entire* strictly supported billboard subset. All other
   effects use the full existing shared CPU implementation.
3. Attach the existing `lib::genre::SpectraSourceComponent` to an ECS entity.
   Use `scale=1_fx` because current strict pooled Metal path rejects
   non-unit source scale rather than silently changing semantics.
4. Provide a read-only `ResolvePose` callback using a game-native
   presentation transform. The sample stores an example *fixed-point*
   `DemoPose` in ECS and converts it into float positions only in the
   presentation callback. It neither stores GPU handles nor mutates World
   during rendering.
5. Call `NativeAttachmentExample::prepare` once per presentation frame,
   after the host calls `Device::beginFrame` and **before** the render
   encoder/pass. Call `drawInsidePass` inside the depth-tested render
   pass and `finish` after ending the scene pass. The device remains
   owned by the host. Both CPU and GPU effects share the same one-world
   frame submission.
6. On entity destruction or disabling the component, the bridge retires
   the old `Entity::packed()`-keyed source. A newly recycled entity
   index has a different generation and starts its own VFX source.
7. On rollback, end any active render pass/frame, call `afterRollback`
   at a safe graphics boundary, and only then restart presentation from
   the restored authoritative snapshot. This resets **the entire pool**:
   use a dedicated rollback scope, not a pool shared with unrelated
   presentation systems.

## What the example does not implement

- A `main` function, application loop, device creation, scene renderer
  or game networking. Those belong to the native project.
- A custom source ECS component; it reuses the engine component.
- GPU collision, damage, authoritative physics, character attachments,
  materials outside the strict Metal subset, or arbitrary Niagara graphs.
- A completed C++ build or running Apple Metal sample. These stages
  are still **not tested**. Source existence is not a passing test.

## GPU selective-kill optimization

Stage 36 replaces the many-contiguous-run dead-state Metal blits with one
checked upload of up to `capacity * sizeof(uint32_t)` bytes and one
compute shader dispatch in **mode 6**. This scatter pass invalidates the
tagged physical slots **before** Spawn/Integrate/Compact in the same
compute encoder. Slots remain quarantined until Metal reports a
successfully completed frame serial, preserving the Stage 35 fence guard.
`ComputeStats::gpuKillScatterDispatches`, `gpuKillIndexBytes` and the
corresponding pooled checkpoint stats expose the work; they are
instrumentation counters, not measured GPU bandwidth or performance claims.

## Next validation after feature work

Build on macOS with Clang and the Metal device, run
`ctest -L SpectraFX`, check `tests/vfx_metal_compaction_smoke.mm`,
and add coverage for kill shader mode 6, fragments, empty-kill frames,
source generation recycling, committed tick rollback, and multi-emitter
GPU/CPU fallback. No build or test has been run for this Stage 36 handoff.
