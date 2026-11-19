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

For optional visual GPU ground-impact feedback, set
`environment.groundCollision.emitImpactEvents: true`, and optionally
`"impactEffect": "sparks"` at the emitter asset root (the named
`fx/sparks.json` must exist). The compute kernel uses a horizontal
world-Y plane; there is no terrain mesh/depth collision. Impact event
feedback requires Metal's asynchronous buffer readback and at most
4,096 GPU slots; a CPU fallback is selected when the policy disallows
the requested GPU feature. The returned impact is **visual only**:
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
      "oneShotSeconds": 2.5
    }
  ]
}
```

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

## API boundaries and known work

`SpectraFxHybridPlayback` now shares a billboard GPU pipeline and
quad buffers across its emitter instances, but particle buffers/textures
are still per emitter. There is no cross-emitter compaction, global GPU
sorting, depth collisions, per-particle collision/death callback,
indirect live-count rendering or Vulkan/Metal acceptance evidence.

The next steps are to implement GPU-side collision and event readback,
cross-emitter pooling and LOD, and to integrate opt-in playback into
additional native host genres without duplicating the gameplay solver.
Run native build, CTest, editor UX, CPU/GPU fallback, Metal/Vulkan
shader/render and performance checks **after functional implementation**
as requested. Until then this branch is not ready to merge.
