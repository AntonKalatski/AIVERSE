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
  "allowUnsortedAlpha": false,
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
  "gpuFrameBudgetMs": 12.0,
  "minGpuSpawnScale": 0.25,
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
default to the existing sorted **CPU fallback**. A dedicated emitter
can opt into **Metal-only GPU alpha sorting** by authoring
`"gpuSortAlpha": true` with `"appearance": {"additive": false, ...}`.
This is supported for 1..4096 particles and requires GPU compute,
RGBA32F and a valid camera-forward direction on every update; the
renderer performs back-to-front bitonic sorting on GPU before its
normal instanced alpha draw.

GPU alpha sorting is deliberately **not enabled by default** because
the sorting network performs multiple Metal compute dispatches and may
cost more than CPU for small effects. It cannot be combined with
`"gpuIndirectCompaction": true` (the latter packs unsorted indices).
On unsupported GPUs/Metal-sort initialization or runtime failure, the
effect uses the authored CPU fallback, if available. An author can
alternatively set `"allowUnsortedAlpha": true` to use the older
unsorted dedicated GPU alpha path for effects whose visual order is
irrelevant. Shared auto-pooling remains additive-only even if either
alpha override is enabled. Sorting is per dedicated emitter, **not**
globally back-to-front across multiple emitters, scene transparents
or different particle materials. GPU impact
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

## Optional adaptive GPU spawn budget and memory diagnostics

The `routes.json` example above opts into a **12 ms whole-GPU-frame**
presentation budget. Remove the two budget keys or set
`"gpuFrameBudgetMs": 0` (default) to disable the governor. The supported
`minGpuSpawnScale` range is 0..1 (default 0.25). The governor reacts
to completed `gfx::Device::gpuFrameMs()` samples by smoothly reducing
new GPU particle emissions when the total GPU frame exceeds 110% of
budget, and gradually restoring them below 80% of budget. It does not
change existing particles, physical integration, camera, collision or
deterministic ECS data. If frame timing is unavailable, visual spawn
scaling remains unchanged; unsupported platforms are not assumed slow.

This is a *whole-frame* feedback heuristic. It includes all other
rendering (lighting, geometry, UI, postprocessing), not just SpectraFX
GPU time. It is not a measurement of SpectraFX performance and is
disabled unless explicitly configured. Use GPU timing/profile tools
to calibrate actual values for each target GPU.

`SpectraFxHybridPlayback::nominalGpuBytes()` reports the estimated
payload bytes for allocated particle positions, velocities, GPU output
textures, optional Metal compaction and alpha sorting buffers. It
excludes GPU driver/pipeline caches, alignment, transient staging and
other renderer allocations. With `AIVERSE_FX_DEBUG` enabled, the
client reports this estimate (KiB), reserved GPU slot count, pool
count, completed-frame budget filter and visual spawn scale.

GPU impact feedback is now allocated on demand. When
`emitImpactEvents:false`, only a 16-byte dummy storage buffer is
needed to satisfy the shader resource binding; the compute kernel
skips event writes. With feedback enabled the full
`16 * particle_capacity`-byte buffer is allocated and async events
are permitted only under their existing Metal capability policy.

## In-app editor

Use **SpectraFX GPU Assets** to edit named `*.gpu.json` definitions
and the **Event routes (routes.json)** item. Save validates JSON through
the same library parsers used by the game. **Restart the game client /
hosted play-test** to apply changed routes; route hot-reload is not
implemented in the running `ClientFx`. The separate **SpectraFX Graph**
editor edits `fx/graphs/*.vfx.json` for CPU compiled graph output,
not these GPU emission assets.

## FPS reference client: local weapon fire cue

The **actual** native `apps/fps/client.cpp` now uses the generic
`SpectraFxSceneHost`; no genre logic is added to the core FX library.
A project must explicitly provide `fx/gpu/routes.json`. Without it,
there are no new FX resources or behavior.

Within an FPS project's root, author
`fx/gpu/routes.json` for the **FPS presentation-only eventType 1**:

```json
{
  "version": 1,
  "routes": [
    {
      "eventType": 1,
      "asset": "fps_muzzle",
      "enabled": true,
      "oneShotSeconds": 0.25,
      "ownPlayerOnly": true
    }
  ]
}
```

`eventType 1` here is **local player weapon fired** in the FPS
client's presentation adapter, *not* an authoritative `SimEvent` type
and not a universal event number shared by RTS or other genres.
At most four local shot visual requests are submitted in one frame.
Remote players' shots are not automatically turned into SpectraFX
cues, avoiding accidental visibility leaks or duplicate effects.

A matching `fx/gpu/fps_muzzle.gpu.json` can use the ordinary v1
schema from the project asset example above. For instance use
`"spawn": {"burst": 16, "rate": 0, "life": 0.15,
"speed": 3, "maxPerFrame": 64}`, an additive billboard appearance
and `"particles": 256`. For cross-platform CPU fallback, provide
an authored `fx/muzzle_flash.json` and use
`"cpuEffect": "muzzle_flash", "preference": "auto",
"allowCpuFallback": true`. The engine does **not** automatically
install a matching CPU effect file; choose a real effect already
available in your project or author one in the existing Effects
editor. This is an example configuration, not an installed gameplay
feature.

The native FPS game renders in a floating-origin local cell space.
The adapter submits effect poses in that same render-relative space
and resets scene presentation instances when the camera moves to
another `world::Cell`. A live scene switch uses
`SpectraFxSceneHost::resetScene()` and
`SpectraFxHybridPlayback::resetPresentation()`, which hide the old
effects immediately but **retain their GPU resources until the
appropriate Metal in-flight frame slots have retired**. These
methods must be called on the device thread outside all GPU encoders;
`resetScene()` returns false without changing presentation state
when the current RHI state is not safe. The host's complete
`shutdown()` / playback `reset()` remains a separate contract for
device teardown after render submission has stopped. GPU updates happen before the scene pass,
CPU particles update after camera setup, and the combined VFX draw
happens in the existing scene pass. This does not modify the
authoritative FPS motor, weapon fire, replication or hit detection.

## Universal presentation event inbox and scene quality

The genre-independent `SpectraFxCueInbox` accepts translated
`SpectraFxCue` records from any gameplay module or presentation
adapter. It is **not an ECS event bus** and never reads world state
itself. A caller supplies the presentation cue ID, action
(`Play/Move/Stop/Kill`), generation-safe instance key (zero for a
one-shot), world/render-relative position and direction, and an
explicit `visibleToViewer` decision from the game/view adapter.

The queue is bounded (1024 pending items in the generic scene host,
256 dispatched per frame by default). It coalesces keyed Move records
until the next lifecycle barrier and prioritizes Stop/Kill cleanup
when an overfull inbox would otherwise discard it. Invalid coordinates,
hidden events and events carrying an outdated scene epoch are rejected.
It is **not thread safe**: merge events from multiple workers into a
stable, caller-chosen sequence before submitting. Callback-generated
cues are only dispatched in a subsequent frame, never recursively.

Example using a native client that owns `SpectraFxSceneHost sceneFx`:

```cpp
// A presentation adapter has already checked visibility.
aiverse::fx::SpectraFxCue cue{};
cue.id = 1; // project-owned cue mapping, not a global game event ID
cue.action = aiverse::fx::SpectraFxCue::Action::Play;
cue.position = {0, 1, 0};
sceneFx.enqueueCue(cue, true, sceneFx.cueEpoch());

// After gfx::Device::beginFrame, before opening scene render passes:
sceneFx.prepareGpu(dt, camera);
```

`sceneFx.cueEpoch()` changes after `resetScene()` or shutdown,
ensuring that producer callbacks from previous scene/match generations
cannot resurrect effects. Host-authored translator functions can use
`SpectraFxSceneHost::translateCues` to convert a range of module-defined
events without introducing a genre dependency into the engine.

The optional route-root setting

```json
"sceneQuality": "medium"
```

selects a presentation tier of `off`, `low`, `medium` or
`high`. It caps the authored quality of **new GPU particle spawns**,
rather than raising an individual effect's authored quality. The
generic native host applies the same tier to its existing CPU particle
system. RTS ClientFx applies the setting only when it is explicitly
present; without it, the legacy CPU quality remains unchanged.
Applications can change quality at runtime using
`SpectraFxSceneHost::setQuality(FxQuality)` or
`SpectraFxHybridPlayback::setGlobalQuality(FxQuality)` (when the
app separately owns the CPU particle system). Quality Off does not
undo previous GPU effects: existing GPU particles age normally and
retire through the regular resource lifecycle.

The FPS floating-origin adapter retries a rejected `resetScene()`
and suppresses FX enqueue/update/draw until it can safely reset
outside all RHI encoders. It never draws particle positions from a
previous origin in a newly shifted render coordinate system.

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

For a separately authored, transparent GPU-sorted effect, the following
root-level settings are available:

```json
{
  "gpuSortAlpha": true,
  "gpuIndirectCompaction": false,
  "allowCpuFallback": true,
  "particles": 2048
}
```

These settings supplement the other required v1 fields, including
`cpuEffect` and a non-additive `appearance`. The alpha index sort
does not use indirect count compaction, and it does not sort across
separate effects or scene geometry. Paused GPU emitters do not generate
new startup bursts; fully empty batches skip GPU sorting/draw.

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
effects requiring back-to-front ordering use the CPU path by default
or the explicit Metal GPU sort described above; this
is not general sorted GPU transparency, GPU-side collision meshes or
global multi-material batching. Measure actual GPU cost and memory
before enabling it in production.

## Shared compute programs, failure recovery and CPU encode profiling

`SpectraFxHybridPlayback` now owns one lazily created GPU evolution
compute program, and — on Metal, only when requested — one alpha-sort
and one live-slot compaction program. Dedicated emitters and shared
arenas borrow those programs instead of recompiling the same source
for every effect. Private GPU particle buffers and transient sorted/
compacted textures are still independently allocated; the playback
releases borrowed resources before destroying shared programs and
before the rendering device is destroyed.

On compute dispatch failure or when the RHI reports an unhealthy GPU,
a dedicated hybrid emitter tries to restart its authored `cpuEffect`
and tears down its GPU resources. On success it will not also render
or simulate the GPU effect in that frame. Without an allowed and
available CPU fallback, the failed GPU emitter is suspended. The CPU
fallback starts at the emitter origin rather than attempting an
unsupported transfer of particle positions and ages. Shared arenas
still need further pooled-failure recovery work.

With `AIVERSE_FX_DEBUG` the client emits
`[spectrafx-cpu-encode]` counters for average/peak microseconds spent
in the playback's update/compute submission and draw submission.
These measurements are **CPU wall-clock encoding time only**, not GPU
render time, GPU-compute duration or full-frame application CPU time.
They also list successfully created shared evolution/sorting/compaction
compute programs and GPU-to-CPU runtime recoveries.

For custom hosts, `SpectraFxHybridPlayback::setCpuEncodeProfiling(true)`
enables these CPU counters; leave it off to avoid instrumentation cost.
The source is still experimental. Native shader, GPU/CPU fallback and
rendering tests have not yet run for these new features.

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
