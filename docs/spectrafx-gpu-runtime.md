# SpectraFX — opt-in GPU emitter playback in native AIVERSE projects

This document describes the source implementation in
`feature/spectrafx-generic-cue-queue`. The GPU path is **experimental and
not native-tested**. It is intentionally OFF unless a project authors
`fx/gpu/routes.json`. Existing `fx/fx.json` and CPU `ParticleSystem` are
untouched.

## Project assets

Create `fx/gpu/flame_gpu.gpu.json`:

```json
{
  "version": 1,
  "cpuEffect": "flame",
  "preference": "auto",
  "particles": 256,
  "allowCpuFallback": true,
  "quality": "medium",
  "lod": {
    "near": 20,
    "far": 100,
    "farSpawnScale": 0.15,
    "cullBeyondFar": true
  },
  "spawn": {
    "rate": 20,
    "perMeter": 0,
    "burst": 12,
    "life": 1.5,
    "speed": 2,
    "spread": 0.2,
    "seed": 123,
    "maxPerFrame": 128
  },
  "appearance": {
    "color": [1, 0.7, 0.2],
    "radius": 0.2,
    "additive": true
  },
  "environment": {
    "acceleration": [0, -2, 0],
    "damping": 0.99,
    "groundCollision": {
      "enabled": true,
      "height": 0,
      "restitution": 0.35,
      "tangentRetention": 0.8,
      "emitImpactEvents": false
    }
  }
}
```

`cpuEffect` must be the name of an existing `fx/flame.json` in that
project. If absent, choose an existing effect name; the example is not
installed automatically. The GPU implementation handles **simple,
unsorted, world-space alpha/additive billboards only**. All other effect
modules (collision, trails, ribbons, mesh particles, subemitters, lights,
sorting, flipbooks and local space) stay on CPU. The backend selection
permits GPU only on compatible Metal/Vulkan devices with compute and
RGBA32F; there is no D3D12 RHI in this project.

Distance-LOD is optional. With `lod.far: 0` (the default), the
effect's spawn intensity and visibility are unchanged by camera distance.
With a positive range, the spawn rate and startup burst decrease from
near to far distance; `cullBeyondFar` skips GPU billboard draw outside
the range, while the existing particles continue ageing for safe slot
recycling. This is not GPU frustum/occlusion culling or live-slot
compaction.

For optional visual GPU ground-impact feedback, set
`environment.groundCollision.emitImpactEvents: true`, and optionally
`"impactEffect": "sparks"` at the emitter asset root (the named
`fx/sparks.json` must exist). The compute kernel uses a horizontal
world-Y plane; there is no terrain mesh/depth collision. Impact event
feedback requires Metal's asynchronous buffer readback and at most
4,096 GPU slots; a CPU fallback is selected when the policy disallows
the requested GPU feature. For example, add `"impactEffect": "sparks"` at the top of
`flame_gpu.gpu.json` and change `emitImpactEvents` to `true`.
The `sparks` effect must already exist in the project, and the
GPU-impact data may be delayed or discarded after particle reuse.

The returned impact is **visual only**:
it is not a gameplay collision, damage, physics contact or
authoritative ECS event.

`SpectraFxHybridPlayback::publishGpuImpacts(router)` routes feedback
to the bounded presentation event queue for native hosts. The
`ClientFx` host separately consumes authored `impactEffect` CPU
sub-effects with a cap of 32 per frame. Hosts should not consume the
same pending feedback by both methods. Async feedback can be delayed or
dropped when unsupported or when particles are recycled before
delivery.

Create `fx/gpu/routes.json`:

```json
{
  "version": 1,
  "routes": [
    {
      "eventType": 42,
      "asset": "flame_gpu",
      "enabled": true,
      "ownPlayerOnly": true,
      "oneShotSeconds": 2.5,
      "sharedPool": "embers",
      "sharedPoolCapacity": 8192
    }
  ]
}
```

An optional `sharedPool` opts this route into the new project-level
homogeneous shared GPU arena. Multiple routes using the same pool name
share one GPU particle storage allocation, compute update and draw if
their emitter assets have *exactly identical* appearance (color,
billboard radius, additive blending), acceleration/damping, ground
collision and compaction settings. Their spawn rates, quality tiers,
movement and LOD settings can differ. `sharedPoolCapacity` sets the
**total particles in that pool**, not a per-event allowance (default
8192). Each route sharing that pool must specify the same capacity.
Without `sharedPool`, playback uses its existing dedicated
CPU/GPU emitter behavior.

Automatic shared grouping is currently restricted to **additive**
billboards. Alpha-blended effects require back-to-front sorting and
therefore fall back to the dedicated HybridEmitter path. GPU impact
readback/`impactEffect` also remains on dedicated emitters. If
the pool cannot initialize, becomes incompatible or fills up, the
existing dedicated path is attempted, retaining its authored CPU
fallback. One `SpectraFxHybridPlayback` permits up to 8 shared pools,
65536 **total GPU slots including dedicated emitters**, and 64 active
source instances across grouped/dedicated one-shots and keyed effects.
A one-shot in a shared pool receives an internal generation-safe ticket
and retires after `oneShotSeconds`.

**42 is an illustrative example, not a known AIVERSE event ID.** Replace
it with the numeric `SimEvent::type` defined by *your* GameModule.
Only local-player-owned events are eligible in the default RTS client;
spectator viewer 0 receives none from this GPU collector. The route
parser explicitly refuses `ownPlayerOnly: false` because that would
need a separate host-provided fog/visibility policy to avoid leaking
enemy activity. A route does not automatically suppress legacy
`fx/fx.json` bindings for the same event; avoid authoring two effects
unintentionally.

## In-app editor

Use **SpectraFX GPU Assets** to edit named `*.gpu.json` definitions
and the **Event routes (routes.json)** item. Save validates JSON through
the same library parsers used by the game. **Restart the game client /
hosted play-test** to apply changed routes; route hot-reload is not
implemented in the running `ClientFx`. The separate **SpectraFX Graph**
editor edits `fx/graphs/*.vfx.json` for CPU compiled graph output,
not these GPU emission assets.

## Runtime integration order

The existing generic RTS client has opt-in hooks:

1. `ClientFx::init` reads routes, initializes the asset library and
   playback manager only when routes are present.
2. `ClientFx::startMatch` installs a weak-owned
   `Simulation::addSystem("spectrafx_gpu.collect", ...)` sink for
   explicitly routed, local-player-owned `SimEvent` records. This
   system only copies cues into a 1024-entry presentation queue.
3. After the simulation tick, inside `gfx::Device::beginFrame` and
   **outside any render/compute pass**, `ClientFx::prepareGpuFrame(dt)`
   dispatches queued cues and runs GPU compute. No graphics calls
   occur from the authoritative Simulation collector.
4. After `Renderer3D::beginScene` and `Renderer3D::render` have
   established the scene pass, existing CPU particles render as
   before. `ClientFx::renderGpu(viewProj,camera)` then draws optional
   GPU billboards in the already open scene pass.
5. Scene post-processing (PRISMA), overlays and UI retain their
   existing ordering.
6. `ClientFx::stopMatch/shutdown` stop and release managed GPU
   instances **before** the RHI device and ParticleSystem are destroyed.
   Reset/rollback handlers must invalidate presentation instances and
   stale cues. Planet rendering resets/skips the flat-world GPU path.

One-shot cues (`key == 0`) have a bounded lifetime configured with
`oneShotSeconds` and share the 64-active-emitter limit with attached
instances. They are killed at the deadline (including CPU effect tails),
not guaranteed to fade naturally.

## Experimental GPU live-slot compaction and shared arena

The optional top-level emitter asset property

```json
"gpuIndirectCompaction": true
```

enables a **Metal-only** experimental path. It is **false by default**.
Following GPU evolution, a compute pass writes a packed texture of live
slot indices and GPU-authored indexed indirect arguments. A Metal GPU
indirect draw then submits just the live instance count without CPU
readback. The renderer restores its original full-capacity instanced draw
if indirect preparation or submission fails. Vulkan currently lacks the
required GPU-authored `drawIndexedGpuIndirect` implementation; it uses
the original instanced draw with transparent dormant slots. These shader
and render transitions **have not yet been compiled or verified**.

For many compatible instances with the **same** color, radius,
alpha/additive blend, acceleration, damping and collision settings,
native games can opt into one shared GPU arena:

```cpp
#include "engine/fx/SpectraFxGpuSharedArena.h"

aiverse::fx::SpectraFxGpuSharedArena arena;
aiverse::fx::SpectraFxGpuSharedArena::Config config;
config.particles = 8192;
config.gpuIndirectCompaction = true; // Metal only, optional
if (arena.init(device, config)) {
    aiverse::fx::SpectraFxGpuSharedArena::EmitterConfig flame;
    flame.spawn.particlesPerSecond = 50;
    flame.spawn.lifetime = 1.5f;
    // Host supplies a generation-safe effect/source key.
    arena.addEmitter(123456789ULL, flame, {0, 1, 0});
}

// In device.beginFrame/endFrame, before opening a render pass:
arena.update(dt, camera.eye());

// Inside an open compatible render pass:
const auto view = camera.view();
arena.render(viewProj,
    {view.at(0,0),view.at(0,1),view.at(0,2)},
    {view.at(1,0),view.at(1,1),view.at(1,2)});

// During scene teardown and before destroying gfx::Device:
arena.shutdown();
```

This is also usable as a separate, opt-in native
`SpectraFxGpuSharedArena` API (capacity 1..65536, up to 64 registered
sources), but now a project can additionally select named homogeneous
pools through `fx/gpu/routes.json` without writing C++. The standalone
API has no built-in CPU fallback; the project-level HybridPlayback
retains that fallback. Source removal can preserve existing particles
until expiry or kill them at a safe point.

The GPU compaction uses atomics and is **unsorted**. Transparent alpha
effects that need depth ordering still require the CPU fallback; this
is not general sorted GPU transparency, GPU-side collision meshes or
global multi-material batching. Measure actual GPU cost and memory
before enabling it in production.

## API boundaries and known work

`SpectraFxHybridPlayback` now shares billboard pipelines and quad
buffers across dedicated emitters and can group homogeneous additive
emitters into shared GPU particle arenas via project routes. Metal-only
live-slot compaction and indirect draws are optional. Ground-plane GPU
collision and best-effort Metal asynchronous impact feedback are
implemented in source, but actual mesh/depth collision, general GPU
alpha sorting, global heterogeneous/multi-material GPU batching,
GPU event parity and Vulkan/Metal acceptance remain incomplete.

The next steps are to harden GPU resource lifetimes, evaluate
transparent sorting and pooled memory/LOD behavior, and integrate
the generic interfaces into more native host genres without
duplicating gameplay solvers.
Run native build, CTest, editor UX, CPU/GPU fallback, Metal/Vulkan
shader/render and performance checks **after functional implementation**
as requested. Until then this branch is not ready to merge.
