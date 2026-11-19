# AIVERSE MotionWeave

**MotionWeave** is the canonical name for AIVERSE's modular, genre-independent
animation subsystem: AnimationGraph, clips, blend trees, IK, Motion Matching,
Montages, root motion, ragdoll poses, inertialization and animation LOD/GPU crowds.

## Public API

Use the new CMake target and the branded public header in native projects:

```cmake
target_link_libraries(my_game PRIVATE AIVERSE::MotionWeave)
```

```cpp
#include <aiverselib/motionweave/MotionWeave.h>

// Separate client presentation ECS World; never Simulation::world().
aiverse::World presentation;
// Configure animation asset, player and output pose components first.
aiverse::motionweave::MotionWeave::update(presentation, frameDt);
```

This is the *same* implementation as the existing library, not a second engine:

| Recommended new symbol | Compatible existing symbol |
| --- | --- |
| `AIVERSE::MotionWeave` | `AIVERSE::animation_ecs` |
| `AIVERSE::motionweave` | `AIVERSE::animation_ecs` |
| `aiverse::motionweave` | `aiverse::lib::animation` |
| `MotionWeave::update(world,dt)` | `stepAnimationPresentation(world,dt)` |
| `<aiverselib/motionweave/MotionWeave.h>` | `<aiverselib/animation/EcsAnimationSystems.h>` |

All specialist libraries still have their historical
`<aiverselib/animation/...>` paths. Existing `render::AnimationGraph`,
`render::Animator` and `Pose` names are deliberately unchanged.

## Runtime design

**Presentation:** floats, AnimationGraph instances, IK and skinning reside in
a dedicated visual ECS World. FPS opts into `fps.visual.model`, configurable
Graph or baked crowd animation, preserving cube fallback. Existing RTS view
continues using its original animation path until migrated deliberately.

**Gameplay:** fixed-point, authored root motion enters the authoritative
simulation only through a collision-aware motor. Cosmetic AnimationGraph
and Montage notifies cannot trigger authoritative damage, reloads or hits.

**Authoring:** visual editor windows, Humanoid Avatar profiles, Ragdoll
descriptors, Montage and root-motion baking operations share Workspace/MCP.
Persisted asset schemas and editor IDs do not change with branding.

## Status and verification

New standalone test `tests/animation/motionweave_api.cpp` is registered as
`motionweave_api` (target `aiverse_motionweave_api_tests`).
**NOT RUN**: the user requested to defer tests, CMake builds and visual QA.
Naming changes do not imply the large animation PR is feature-complete.

Before further implementation read `AGENTS.md` and the latest entry in
`docs/agent-handoffs/animation-universal.md`. Every implementation stage
must be followed by its **own committed, append-only handoff**.

## Multithreaded presentation runtime

For a game with many animated characters, create a **persistent host-owned**
scheduler once and pass it to the three-argument MotionWeave update:

```cpp
#include <aiverselib/motionweave/MotionWeave.h>

aiverse::motionweave::MotionWeaveScheduler scheduler;
auto stats = aiverse::motionweave::MotionWeave::update(
    presentationWorld, frameDt, scheduler);
```

The existing two-argument `MotionWeave::update(world,dt)` is still
**sequential** for compatibility. The scheduler owns a persistent, bounded
work-stealing thread pool (up to seven background threads plus the frame
thread). Each frame it classifies models/compiled graphs, separates
independent clip, blend, controller, motion-matching and compatible IK
work into cost-weighted chunks, then executes them in parallel.
Native/Lua callbacks, animation traits, environment contacts, and
callback-sensitive rig operations **always run on the frame thread
after the workers have finished**. A `mainThread=true`
`AnimationWorkHintComponent` forces this lane explicitly.

`MotionWeaveScheduleOptions` exposes minimum entity count, minimum
estimated work and chunk sizing. `AnimationWorkHintComponent` also
supports a per-character cost multiplier. Graph safety/cost plans are
memoized per frame, while dense work-list allocation capacity is reused.
Invisible characters with an already valid pose carry reduced estimated
work. Caller code must not change World structure or snapshot it until
`MotionWeave::update` returns.

`MotionWeaveScheduleStats` provides eligible/parallel/serial actor counts,
batches, worker count, estimated work, unique graph count, offscreen actor
count and safe/serial phase times. These measurements are non-authoritative
presentation diagnostics; they never affect gameplay lockstep/checksums.

### Automatic backend for FPS

FPS supports `fps.visual.animationMode: "auto" | "cpu" | "gpu"`
and legacy `bakedCrowd: true`. With an eligible simple imported,
skinned model, auto mode enters GPU baked instancing at 24 visible
remote characters and exits at 12, avoiding frequent toggling.
Complex custom graphs, IK/controller requirements and `graphFile`
stay on CPU; the FPS client lazily constructs the persistent scheduler
when 16+ palette characters are present.
`fps.visual.animationStats: true` enables optional HUD diagnostics.

GPU scene draws and uploads remain on the graphics thread.
The fast crowd path reuses AIVERSE's existing baked skinning and
Renderer3D instancing; MotionWeave does not claim a new GPU compute
skinning backend.

**Test status:** all new worker/scheduler/backend CTest fixtures are
committed but **NOT RUN** (user-requested deferral). Verify compilation,
TSAN, macOS FPS screenshots, FPS save/network parity and real FPS/RTS
benchmarks before declaring production readiness.

### CPU performance fixture (deferred)

Build the standalone (no GPU/model files) benchmark after configuring
`AIVERSE_BUILD_TOOLS=ON`:

```sh
cmake --build build/ecs-test --target aiverse_motionweave_bench
./build/ecs-test/aiverse_motionweave_bench --actors 1000 --frames 120
./build/ecs-test/aiverse_motionweave_bench --actors 5000 --frames 120 --ik
```

It creates identical serial and persistent-worker Worlds, warms both,
measures average CPU frame times and verifies one output pose for parity.
The benchmark intentionally is **not registered in CTest** because timings
are machine-dependent. It must be run together with ThreadSanitizer,
real-model Metal screenshots and a representative RTS/FPS frame trace
before changing production worker thresholds. **NOT RUN**.

## Strict genre-independent contract

MotionWeave must NEVER depend on or inspect FPS/RTS/RPG modules.
Dependency direction is one-way:

```text
Any game module / game simulation / editor
      ↓ generic animation model and named parameters
MotionWeave reusable presentation ECS
      ↓ generic Pose / Notifies / Root delta
Host rendering, audio/VFX and host-owned gameplay motor
```

The core doesn't know what "fire", "attack", "move", "cast",
"reload" or another action means. Authors select parameter/event
names in their own graphs; clients project their state into the
same reusable API.

Generic `<aiverselib/animation/MotionWeaveSourceBridge.h>` exposes
`AnimationSourceFrame {sourceId,generation,model,graph,inputs}` and
`MotionWeaveSourceBridge::sync(presentationWorld, sources)`.
A bridge maintains visual ECS entity identity, resets visual players
on source replacement, copies graph-defined scalar/vector parameters
and removes missing sources. It neither evaluates animation nor
commits gameplay state. Call the normal MotionWeave scheduler
after all input projection and optional view budget configuration.

The original FPS event adapter has been moved out of the animation
library to `modules/fps/view/FpsAnimationEvents.h`.
The shared `<aiverselib/animation/AnimationActionEdges.h>` now
processes only opaque named counters/boolean state transitions, with no
FPS/RTS/action-type semantics. `playAnimationSlot` lives beside the
generic ECS `AnimationMontageSlotsComponent` definition.

## Engine View migration

The canonical scene/view type is `aiverse::View` (also
`aiverse::view::View`) in `engine/view/View.h`, linked through
`AIVERSE::view`. Host code supplies `aiverse::view::Object`
records containing stable IDs, generic `render::ModelDraw` packets,
optional AnimationGraph and named parameters. The generic View
manages independent presentation ECS states, animation LOD budgets,
worker scheduling and pose-based scene submission.

Use `View::update(span,dt)` before `View::submit(renderer,span)`
or `View::submitOne(renderer,object)`. A packet may contain a model
with no animation graph. View does not depend on RTS, FPS, camera
controller, game rules, network, combat, map or terrain modules.

A transition adapter still exists in
`modules/rts/view/RtsView.cpp` and `RtsViewDraw.cpp`: it projects
only simple, eligible compiled animation graphs into the generic
View and delegates their **final ModelDraw submission** to
`View::submitOne`. Complex RTS procedural animation, contact IK,
genre-specific attachments/FX/fog/terrain remain in the game
host until each has a generic render contract. The compatibility
class `game::RtsView` remains for older game code; it is not the
canonical engine View and should not be used in new projects.

### Host migration status

Both existing game frontends now send at least their supported
animated-model draws through the universal engine
`aiverse::View::submitOne`:

- The legacy game-side `game::RtsView` delegates a strictly
  eligible subset of pure AnimationGraph pose evaluation and the
  final styled model draw.
- The FPS reference client projects its existing CPU Graph / GPU
  Baked `render::ModelDraw` through `View::submitOne`, preserving
  the precomputed pose. Its replicated gameplay state never enters
  engine View.

The common CMake name `AIVERSE::View` (legacy lowercase
`AIVERSE::view` also works) and public
`<engine/view/View.h>` describe the preferred engine API. The
old `aiverse_game_view`/RTS-specific presentation remains
compatibility code during migration; it is **not** identical to
the new generic View yet. Shared camera/culling/terrain/lighting
composition and full legacy game adapter removal require further
refactoring. No source-level genre dependency is allowed inside
`engine/view`.

### Render-resource owner (Stage 99)

MotionWeave graph and pose state does **not** own GPU Model allocations.
Generic native hosts can use `aiverse::render::ModelResourceCache`
(`engine/render/ModelResourceCache.h`) with a separate presentation
`World`/`aiverse::View`. The cache owns the model map and shared
texture leases. On model/graph replacement, hosts must detach affected
`AnimationSourceFrame` and pose/graph players **before** moving the
old model to the cache's deferred retirement queue. At the next safe
host frame boundary, `collectRetired(device)` invokes the explicit
`gfx::Device::waitForResourceIdle` barrier and releases only resources
whose previous GPU submissions are complete. Never derive completion
from `frameSerial()` or a fixed number of frames in flight.

Legacy `game::view_adapter::RtsModelResourceCache` and
`RtsModelPreparationService` are aliases to the universal render
classes. Material preview sphere/box resources in `game::Portrait`
are the first independently owned real editor consumer. The complete
RTS scene compatibility adapter is not yet retired. Deferred
GPU/backend and animation parity tests are **NOT RUN**.

### Nonblocking GPU model retirement (Stage 100)

Model and texture GPU lifetime is independent of MotionWeave player state.
A native host can own `render::ModelAssetProvider` (from
`engine/render/ModelAssetProvider.h`) without loading RTS/FPS headers,
then submit its borrowed model through `aiverse::View::submitModel` or
the generic ordered `ScenePacketBatch`. After graph/source invalidation,
detach the owning presentation ECS sources before moving models into
`render::ModelResourceCache` retirement. Only one animation owner
evaluates each graph; packet submission must not tick its graph a second
time.

Stage 100 adds asynchronous per-batch retirement markers via
`gfx::Device::beginResourceRetirement`,
`resourceRetirementComplete`, and `endResourceRetirement`.
`ModelResourceCache::collectRetired` polls rather than waiting for the
whole GPU on supported backends. The old synchronous wait is only a
fallback or explicit shutdown barrier. Never use a frame counter,
fixed delay or time-since-last-use as a GPU completion condition.
The new `game::Portrait::renderStaticModel` is a direct native-model
preview consumer using the canonical View, with no game-side owner
required for that code path. Dynamic RTS models still use compatibility
presentation and rendering.

**NOT RUN:** deferred native/Metal/Vulkan/GL/WebGL2 build, runtime,
GPU-lifetime, sanitizer and visual parity checks.

### Stage 101 — async model source preparation and GPU retirement pressure

Animation graphs remain separate from render-resource loading. Native
project hosts can use `render::ModelAssetProvider::requestAsync(path)`
for bounded, single-worker CPU model import, then call
`publishReady(device, maxCount)` on the owning render thread before
constructing `aiverse::View` packets. The CPU queue records source
revisions: invalidating a model source makes any older worker result
unpublishable even if the CPU task completes later. MotionWeave source
identity/generation and any live pose/graph players must still be
detached before replacing a resident Model.

`render::ModelResourceCache` now exposes model-count-based retirement
pressure telemetry and a soft admission limit for NEW generic model
loads. Existing graph sources continue to render; a saturated backlog
must never force early GPU destruction. The default is 128 retired
Model objects, NOT a byte-accurate GPU memory budget. Editor's Statistics
panel reports pending GPU model retirements and fence fallbacks.

The editor Model window provides an opt-in native-source preview for
static unskinned models, using an independently owned
`render::ModelAssetProvider` and
`game::Portrait::renderStaticModel`. Default fully authored
RTS/character portraits still use their previous presentation path,
preserving material overrides, attachments and animation playback.
The raw-model inspection path is not a parity replacement.

**NOT RUN:** CMake/compiler, CTest, Metal/Vulkan/GL/WebGL2 GPU tests,
native/editor preview, sanitizers and simulation/animation visual
regressions. Keep stages independent and do not claim the old RtsView
has been removed.

### Stage 102: model source priorities and estimated GPU-retirement memory

The host owns one `render::ModelAssetProvider`, or a separate provider
per presentation scope, not one per animated actor. For native model
imports, `requestAsync(source, ModelLoadPriority)` selects
`Background`, `Visible` or `Interactive` for queued CPU jobs; an
active worker is never preempted. Call
`publishReady(device,maxCount,budgetMs)` at the render-thread frame
boundary, with a hard model-count cap and an advisory elapsed-time cap.
The first GPU upload may exceed `budgetMs`; nothing is published from
inside a worker. Preserve source revisions, and detach any old
`AnimationSourceFrame`, graph, pose and borrowed `Model*` before
invalidation. No second MotionWeave evaluation is introduced.

`ModelResourceCache` can now estimate the *known, model-owned*
retirement footprint and apply an optional byte-derived admission
limit. `ModelResidentEstimate` deliberately does not charge borrowed
texture leases or shared geometry pages and is NOT an exact VRAM
reading. Never release an object because a limit is reached; only
its own completed GPU fence authorizes destruction.

The standalone `aiverse_modelview --provider --no-merge` is a second
real generic `aiverse::View` consumer for static raw assets, alongside
the editor's opt-in independent raw preview. Precomputed poses on the
legacy animated modelview path remain authored by the existing host;
View only submits them once. The game-specific `RtsView` adapter is
still in use elsewhere and has not been retired.

**NOT RUN:** all CMake, CTest, native modelview/editor, GPU backend,
sanitizer, snapshot and visual parity checks remain deferred.

### Stage 103 — multi-worker source preparation and single-authority View display

The general model resource owner can now prepare multiple independent
model files on CPU workers without involving the ECS World or GPU:
`render::ModelAssetProvider::setCpuWorkerCount(1..8)`,
`setCpuMaxQueued`, and `requestAsync(source,priority)` forward to
`AsyncModelCpuQueue`. The default remains one worker; an editor or
native host can opt into more. Workers have no knowledge of genre,
FPS/RTS player types, scene packets or MotionWeave graph evaluation.
Only `ModelAssetProvider::publishReady(device,...)` may upload a
prepared Model on the render/device owner thread.

Priorities are `Interactive`, `Visible`, `Background`. Pending
jobs of equal priority are FIFO; after sustained high-priority
dispatches, an oldest queued request gets scheduled to reduce
background starvation. No executing job is preempted. Each CPU
result contains its source revision; on asset invalidation it is
discarded rather than publishing a stale Model, and on shutdown all
worker futures are joined before the reader/texture cache is released.
Caller-provided `FileReader` callbacks must be reentrant when more
than one worker is configured.

Shared GPU texture ownership is measured once by
`ModelTextureCache::gpuOwnerEstimate()`, never per borrower. The
device-global geometry pool remains a separate resource owner; these
figures are source-derived RGBA8-equivalent diagnostics, not exact
driver VRAM usage.

The editor `SceneRenderable` source now uses the canonical
`aiverse::View::submitModel` and `submitMesh` for ordinary model
and mesh submissions. Selected/hovered specialized mesh silhouettes
retain their previous renderer-specific path. The owner still
constructs the ModelDraw transformation, tint and outline; View
does not run graph animations a second time. The legacy RTS
portrait/type-appearance compatibility layer remains active.

**Validation NOT RUN:** native CMake compiler, CTest, sanitizer,
actual GPU/Metal/Vulkan/GL/WebGL, FPS/RTS/editor screenshots,
multithreaded loader contention and visual parity have not been
verified. See the Stage 103 handoff for exact SHA and next action.

### Stage 104 — persistent multi-worker import and View selection styles

The native `render::AsyncModelCpuQueue` now reuses 1–8 persistent
`std::thread` workers instead of spawning a new asynchronous task
for each model file. It uses condition-variable dispatch, bounded
waiting/ready queues, source revision invalidation and the existing
Visual/Interactive/Background priorities. Configuration is host-owned:
the reader and CPU image cache must be reentrant; all GPU upload,
source publication and MotionWeave pose state remain on the render
owner thread. Changing the worker count joins in-flight tasks;
do not reconfigure during an interactive rendering frame.

Shared GPU geometry is accounted by page identity, not by the
number of animations or imported Models. The
`geometryPoolReferencedStats(device,modelPointers)` API can accept
models from multiple render host owners in ONE collection, avoiding
double charging a page referenced by many models or views.
`ModelResourceCache::geometryPoolModelReferences` includes
currently resident and GPU-retired models. The device's global
geometry pool remains the authoritative page capacity owner, and
shared textures remain counted by their texture-cache owner.

`aiverse::view::MeshObject` now has genre-independent
`selectionOutline` / `selectionColor` styling. The editor's
`SceneRenderable` selected/hovered mesh submissions and selection
edge lines are routed through `aiverse::View::submitMesh/submitLine`
with the legacy renderer-specific silhouette implementation
preserved internally. This does not tick an animation graph or add
gameplay selection components to MotionWeave.
Full `game::RtsView` retirement is still unfinished.

**NOT RUN:** native build, CTest, desktop/mobile GPU tests, editor
selection visual parity, CPU thread sanitizer and geometry pool
GPU-lifetime checks. See Stage 104 agent handoff.

### Stage 105 — cross-host GPU resources and generic authored portrait packets

`render::RenderResourceOwnerGroup` is a non-owning aggregation
facade over multiple independent native `ModelResourceCache` hosts.
A group computes unique model and shared texture ownership, includes
retired models waiting on GPU fences and deduplicates shared geometry
pages by their device-global identity. Its `Ready`,
`PendingGpuRetirement` and `DeviceUnhealthy` statuses are
diagnostics/admission policy, not a license to free resources early.
Pending GPU work is expected, not an error. Estimates use known
source geometry and RGBA8-equivalent texture metadata; never call
them exact driver VRAM occupancy.

Native editor raw-model preview now groups the main scene,
portrait preview and raw-source provider, with an optional
group-wide estimated admission budget. Failed GPU health pauses
new model publication, leaving all GPU-retired models alive
until correct fence completion can be established.

`game::Portrait::renderAppearance(const render::ModelDraw&,
RTSCamera,background)` is a generic synchronous offscreen
consumer that keeps the host-selected material override,
node-material mapping, geometry policy, team tint, variant
and any precomputed pose. Raw-model, precomputed-pose and
RTS-resolved **static** portraits delegate to it. Complex
animated portraits retain their existing RTS compatibility
fallback and its one MotionWeave graph evaluator; no second
player or render-side animation tick is introduced.

**Validation NOT RUN:** new CTest contracts, native compiler,
GPU fence/device loss recovery, renderer/portrait screenshot
parity, FPS/RTS tests and thread sanitizers are deferred by
the user. Follow Stage 105 in the animation handoff.

### Stage 106 — expiring GPU owner references and verified failure states

Multiple native model/resource owners can be observed through
`render::RenderResourceOwnerGroup`. Its registrations now carry
the caches' `weak_ptr` lifetime tokens, so a destroyed cache
is skipped rather than accessed. The group is still
device-owner-thread-only; a weak reference does not make
simultaneous GPU resource teardown and snapshot thread-safe.
The group doesn't own or shut down render resources.

GPU retirement now distinguishes a normal outstanding fence
from an actual failed completion token via
`gfx::Device::resourceRetirementStatus`. A failed Model
retirement batch remains alive, even when newer tokens signal
success. Only a successful full-device GPU idle barrier
permits cleanup and `abandonRetirementTokenAfterIdle`.
`RenderResourceGroupStats::health` reports
`RetirementFenceFailed` separately from
`PendingGpuRetirement` and `DeviceUnhealthy`.
These statuses only affect NEW resource admission, never
authoritative gameplay or existing MotionWeave graph clocks.

The model material-preview shape now delegates offscreen draw
to the same generic `Portrait::renderAppearance` +
`aiverse::View::submitModel` path as native static models
and precomputed poses. Material authoring still happens through
the old RTS-compatible host; dynamic/complex animation fallback
has not been removed and retains exactly one animation writer.

**NOT RUN:** compilation, CTest, GPU/device-loss tests,
editor screenshots, FPS/RTS parity, thread sanitizer and CI.
Stage 106 handoff lists exact source refs and next step.

### Stage 107 — scoped device owners, deterministic retirement trace and independent authored appearance packet

The generic `render::ModelResourceCache` now explicitly binds to
one `gfx::Device` and one owning thread via
`bindResourceDevice(device)`. Cross-device or cross-thread GPU
model mutation is rejected. `RenderResourceOwnerGroup` supports
`registerScoped(device,cache)` returning a move-only scoped
registration. The editor preview uses these scoped registrations
rather than leaving resource-owner observer pointers in an
unbounded registry. Resource owners do not outlive the device,
and a ModelResourceCache may be rebound only after a successful
GPU-safe shutdown. The old CPU-only `add(cache)` survives for
existing tools but is not the recommended device-bound route.

New opt-in `RetirementDiagnosticEvent` callbacks on the
resource retirement queue, and `RetirementStatusReplay`
with `script/issue/advance/status/events`, allow deterministic
synthetic GPU token timing and pending/failure/idle-recovery traces.
Synthetic results never replace a real GPU completion fence and
cannot authorize GPU resource destruction on hardware. Failed
markers stay retained until the real backend supplies a successful
full idle barrier.

`engine/view/AppearancePacket.h` now owns generic visual
packet composition for material overrides, node-material
assignment, variant, team/tint, geometry policy and optional
graph/parameter inputs. The RTS static preview adapter maps
its typed selection and authored placement into the same
`view::AppearanceSpec`, then submits the resulting canonical
`view::Object`. The RTS host still chooses type, placement,
material library and gameplay policy; no RTS/FPS header enters
the generic View code. Static/precomputed appearance uses
`submitPrecomputedAppearance` and MUST have no attached graph.
For graph appearances, the owning View must call
`update` exactly once, then `submitOne`; do not create a
second MotionWeave writer.

**Validation NOT RUN:** native C++ builds, all CTest fixtures,
metal/Vulkan/GL GPUs, editor/portrait FPS/RTS visual parity,
sanitizers and CI. New deferred fixtures and the exact next
stage are recorded in the Stage 107 handoff.

### Stage 108 — GPU device epochs, bounded diagnostics and shared material/graph authoring

Native host model caches now support
`RenderDeviceLifetime::Lease` and
`ModelResourceCache::bindResourceDevice(device,lease)` to
distinguish re-created GPUs even when a platform reuses the
same `gfx::Device*` address. `RenderResourceOwnerGroup`
supports `registerScoped(lease,cache)` and rejects expired
generations. The host must retire its device epoch only
after safe GPU model-cache shutdown. This is NOT device
loss recovery or a way to release unresolved fence-retired
resources.

`RetirementDiagnosticRing` stores a bounded, nonowning
history of resource retirement events; the editor keeps
128 events and reports dropped/overflow records without
holding any GPU resources. Diagnostic callbacks do not
change resource lifetime or GPU completion semantics.

New `engine/view/AppearanceAuthoringService.h` separates
material library inheritance and immutable animation graph
compilation from RTS policy. It accepts library JSON,
texture-loader and warning callbacks, FileReader, model,
operations/traits and optional named graphs. Both type
appearance compilation in RtsLook and runtime control
graph compilation in RtsAnimate now use this generic
service. Existing MotionWeave pose/graph players still
have one owner; this service only creates immutable
programs and does not evaluate or advance them.
Authoring changes do not affect authoritative gameplay
tick, movement, networking or serialization.

The remaining legacy RtsView still chooses type/weapon,
procedural animation, materials per entity and attachment
policies. Full dynamic genre-neutral appearance state
migration and real visual parity remain open.
**NOT RUN**: native compiler, CTest, renderer/GPU validation,
FPS/RTS screenshots, sanitizer and CI.
