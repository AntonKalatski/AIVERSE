# AIVERSE SpectraFX

**SpectraFX** is the official name of the universal, genre-independent visual-effects system in AIVERSE. Its design is inspired by the authoring and event-driven workflow of Unreal Niagara, but it is **not** a copy of Unreal code, asset formats, or editor. `aiverse::spectra` is the public C++ API namespace.

> **Current scope (2026-11-18):** Graph v1 frontend, reusable CPU particle backend, graph-asset variants/hot reload and optional read-only ECS presentation adapter have been **committed**. A basic graph-v1 node-canvas editor and in-memory CPU preview are **committed but unverified**; a limited Metal GPU particle prototype is **committed but unverified**; full GPU/CPU integration, Niagara dataflow and automatic RTS/FPS production integration are **not finished**. Tests were added but have **not yet been run** in the authoring environment. Refer to `docs/agent-handoffs/universal-niagara-vfx-2026-11-18.md` for exact SHA and verification status.

## Architecture

```text
Gameplay ECS (authoritative, deterministic)
  ├─ typed gameplay events ────────────────┐
  └─ data-only SpectraSourceComponent ──┐  │
                                       ▼  ▼
                               SpectraFX presentation
                               (read-only World + events)
                                       │
                          exposed graph parameters
                                       │
                graph assets fx/graphs/*.vfx.json
                                       │
                     compileGraph (versioned IR)
                                       │
                   existing fx::EffectDef schema
                                       │
                    CPU fx::ParticleSystem/RHI
                                       │
                         renderer + quality tiers

Future opt-in path: GPU simulation/backend + advanced Niagara dataflow nodes
```

**No genre-dependent FX engines.** An RTS turret, FPS gun, spaceship engine or racing car all use the same source component and event presenter. The host game supplies only the pose resolver and maps its event IDs to effect assets. Graphics state and particle handles never enter deterministic gameplay state/checksums.

## Public C++ entry points

- `engine/fx/SpectraFX.h`: `aiverse::spectra::Runtime`, `Effect`, `Handle`, `GraphAssets`, `GraphLoadResult`, `compileGraph`.
- `libraries/include/aiverselib/genre/SpectraFX.h`: `aiverse::spectra::SourceComponent`, `EventBinding`, `PresentationSystem`.
- `AIVERSE::spectrafx`: CMake link target, alias of existing `aiverse_fx`; the legacy target remains valid for other agents and projects.

### Author a graph

Put this file at `fx/graphs/engine_fire.vfx.json` inside a project:

```json
{
  "version": 1,
  "parameters": {
    "color": [[0, "#ffe8a0ff"], [1, "#ff330000"]]
  },
  "nodes": [
    {"id": "spawn", "type": "Spawn", "properties": {"rate": 24, "duration": 1, "loop": true}},
    {"id": "initialize", "type": "Initialize", "properties": {"life": 0.6, "speed": 1.0, "size": 0.4}},
    {"id": "update", "type": "Update", "properties": {"color": {"$param": "color"}}},
    {"id": "output", "type": "SpriteOutput", "properties": {"texture": "builtin:smoke"}}
  ],
  "links": [
    {"from": "spawn", "to": "initialize"},
    {"from": "initialize", "to": "update"},
    {"from": "update", "to": "output"}
  ],
  "outputs": ["output"]
}
```

The v1 compiler accepts `Spawn → Initialize → Update* → SpriteOutput|MeshOutput|RibbonOutput` chains, with output fan-out but no multiple-input node merges. Every node has a `properties` object whose keys are validated against `engine/fx/FxEffect.h`. The module chain compiles to the existing `fx::EffectDef` — it does **not** execute arbitrary per-particle graph instructions.

```cpp
#include "engine/fx/SpectraFX.h"

aiverse::spectra::Runtime particles;
particles.init(device); // The native host owns gfx::Device.
particles.setFileReader(readGameContent);
aiverse::spectra::GraphAssets assets(particles, readGameContent);

// Converts a versioned graph asset into a CPU particle effect.
auto flame = assets.load("engine_fire");
if (flame.effect >= 0)
    particles.play(flame.effect, {0.0f, 1.0f, 0.0f});

// Overrides produce independent graph variants:
auto cold = assets.load("engine_fire",
    aiverse::Json::parse(R"({"color":[[0,"#aaddffff"],[1,"#0044ff00"]]})"));
```

Runtime loop: call `setView(camera, viewProj, width, height)`, `update(dt)`, then `render(fog)` after scene rendering while its pass remains open, as described in `engine/fx/ParticleSystem.h`.

### Opt-in native ECS presentation

```cpp
#include <aiverselib/genre/SpectraFX.h>
using namespace aiverse;

auto entity = world.create();
spectra::SourceComponent source;
source.effect = "graph:engine_fire:<hash>"; // Better: use GraphLoadResult::runtimeName.
source.killOnDetach = false;
world.add<spectra::SourceComponent>(entity, source);

spectra::PresentationSystem presenter(particles);
presenter.sync(world, [](const World& w, Entity e,
                         fx::float3& position, fx::float3& direction) {
    // Resolve the render transform using YOUR game's component types.
    // Do not hardcode FPS/RTS into SpectraFX.
    return gameResolveEffectPose(w, e, position, direction);
});

presenter.consumeTickEvents(tick, sim.events(),
    {{hitEventId, spectra::EventBinding{"impact_flash", 1_fx}}});
```

Use the actual `GraphLoadResult.runtimeName` for continuous graph assets after `GraphAssets::load`; the `fx/graphs` registry does not automatically initialize all graphs when the game starts. After rollback/new match, call `presenter.reset()` to discard outstanding presentation handles and reset the presented-tick cursor; GPU/CPU simulation state still belongs to the host.

### Compatibility

The new branded `Spectra*` types were introduced without changing the identity of the old `Vfx*` aliases. Existing `fx/<effect>.json`, `fx/fx.json`, `aiverse_fx` and `compileVfxGraph` remain available. Engine runtime files stay under `engine/fx` during migration. Do not remove compatibility aliases or change persistent binary save identifiers until native project adoption and regression tests pass.

## MCP graph asset authoring (stage 7)

The editor-core `registerFxOps` also registers universal `fx_graph_*`
operations through `apps/editor/core/SpectraGraphOps.cpp`. They are available
from the GUI Workspace and the standalone MCP server, independent of the
project's gameplay genre. No editor GPU context is required to validate graphs.

| Operation | Inputs | Result |
|---|---|---|
| `fx_graph_list` | none | All `fx/graphs/*.vfx.json`, per-file compile diagnostics |
| `fx_graph_get` | `name` | Graph JSON, source text and validation |
| `fx_graph_validate` | `graph` or `text` or `name`, optional `overrides` | Strict compiler errors, emitter count and textures; read-only |
| `fx_graph_set` | `name`, `graph` or `text` | Validated file creation/update, `Change::Fx` notification |
| `fx_graph_delete` | `name` | Deletes only the graph source asset |
| `fx_graph_bake` | `name`, `effect`, optional `overrides`, `overwrite` | Compiles graph into `fx/<effect>.json` for existing `fx_preview` and legacy runtime |

Graph names are limited to lowercase letters, digits and underscores (1–48
characters) for editor compatibility. Graph documents have a 1 MiB input
limit. Invalid compilation returns `ok:false` and does not overwrite a
previous valid graph; `fx_graph_bake` refuses to replace an existing legacy
effect unless `overwrite:true` is explicitly supplied.

Example MCP workflow:

```text
fx_graph_set {"name":"engine_fire","graph":<graph v1 object>}
fx_graph_validate {"name":"engine_fire","overrides":{"color":[[0,"#ffffffff"],[1,"#ffffff00"]]}}
fx_graph_bake {"name":"engine_fire","effect":"engine_fire_cpu"}
fx_preview {"effect":"engine_fire_cpu","advance":1,"freeze":true}
view_screenshot {"window":"scene"}
```

**Scope:** baking is a CPU-compatible materialization step, not GPU graph
execution and not a live reference: editing the graph again does not update
an already baked effect until re-baked. Graphs still have no dedicated node
canvas in the editor; that is the next authoring stage. The old
`fx/<name>.json`, `fx/fx.json` and RTS event bindings are unaffected.
The new `vfx_graph_ops` test requires `aiverse_editor_core` to be built;
the test is committed but was **NOT RUN** in the connector-only environment.

## Visual SpectraFX graph editor (stage 8)

The standard AIVERSE Editor has an optional **SpectraFX** window in the
**Графика / Look** group, defined in
`apps/editor/windows/SpectraGraphWindow.cpp`. Its source is automatically
included by the editor's `apps/editor/windows/*.cpp` CMake GLOB.

- Open/create `fx/graphs/<name>.vfx.json` directly from the project.
- Add nodes of types `Spawn`, `Initialize`, `Update`,
  `SpriteOutput`, `MeshOutput`, `RibbonOutput`; drag nodes and pan
  the canvas with the middle mouse button.
- Click the source node's output pin, then the target's input pin to link;
  an input may have one incoming edge. Cycles and invalid endpoint types
  are blocked; unfinished graphs show compiler errors until connected.
- Select a node to edit its `properties` JSON. Edit exposed
  `parameters` as a JSON object; undo and redo graph edits.
- Save through `fx_graph_set`; compile diagnostics come from the exact
  `spectra::compileGraph` implementation used at runtime.
- **Предпросмотр** calls the new GUI-only `fx_graph_preview` command,
  which compiles `graph` in memory, defines an ephemeral effect on
  `Editor::gameFx_`, and plays it in Scene/Game. No temp FX files are written;
  screenshots still work via `view_screenshot`.

Graph nodes may optionally include authoring metadata
`"position":[x,y]` (two finite bounded numeric coordinates), validated
by the v1 compiler but ignored during CPU effect construction. Existing
graph assets without coordinates remain valid; the editor places them in
a default left-to-right layout. This metadata does not enter gameplay ECS
state or shader runtime resources.

This is **only a basic v1 editor**, not full Niagara parity: there is no
arbitrary typed dataflow, function graph, GPU compute execution, depth
collision, linked graph preview in shipped clients or complete UE-style
module UI. Source-level smoke coverage was added to
`tests/vfx_graph_compiler_test.cpp` for node layout metadata; GUI launch,
drag/link interactions and GPU screenshots are **NOT RUN** in the
connector-backed development environment. Manual QA is required.

## Typed module properties (stage 9 — source committed, not yet build-verified)

`engine/fx/SpectraModuleSchema.h` provides a **headless, genre-neutral**
catalog for common graph-v1 fields. The canonical public header
`engine/fx/SpectraFX.h` includes it so native projects and the editor use
the same types and value bounds.

```cpp
#include "engine/fx/SpectraFX.h"

aiverse::Json node = aiverse::Json::parse(
    R"({"id":"emitter","type":"Spawn","properties":{"burst":8}})");
std::string error;
bool changed = aiverse::spectra::applyTypedField(
    node, "burst", aiverse::Json::number(int64_t(24)), error);
// Also available: fieldsForNode("Spawn"), fieldForNode(...),
// validateTypedField(...), removeTypedField(...).
```

The **SpectraFX** visual window now has **Типизированные модули** above
**Расширенные свойства (JSON)**. The typed inspector supports number,
integer, boolean, string, choice and 3-component vector properties. Its
catalog currently includes:

| Stage | Common fields |
|---|---|
| Spawn | `burst`, `rate`, `perMeter`, `delay`, `duration`, `loop` |
| Initialize | `life`, `speed`, `size`, `direction`, `spread`, `shape`, `radius`, `gravity` |
| Update | `drag`, `spin`, `rotation`, `speedLimit`, `stretch`, `velocity`, `hidden` |
| SpriteOutput | `texture`, `blend`, `align`, `fps` |
| MeshOutput | `blend`, `align` |
| RibbonOutput | `hidden` |

All typed field operations are **opt-in edits**. Validation rejects wrong
types, invalid enum choices and values outside declared ranges without
mutating the source node. The inspector does not coerce a MinMaxCurve,
range-array, colour gradient, mesh list or nested effect module into a
scalar: such fields remain intact and must be edited via advanced JSON or
the existing particle module tools. Unknown properties are always
preserved when updating one typed field. The graph compiler
(`parseEffect`) remains the final validation authority.

This is authoring metadata only, not an executable GPU node program.
These changes require **native build and GUI verification**; source
commits and added tests do not prove a passing editor build.
`tests/vfx_graph_compiler_test.cpp` contains schema/compile-contract
checks on top of the preexisting SpectraFX CTest target.

## Metal GPU particle prototype (stage 10 — source committed, unverified)

A new **opt-in** `aiverse::spectra::ComputeParticles` class in
`engine/fx/SpectraGpuParticles.{h,cpp}` executes a limited billboard
particle subset on the **Metal RHI compute path**. It is not wired into
existing RTS/FPS clients and does not replace the legacy
`aiverse::fx::ParticleSystem`. This subset intentionally contains a
single additive soft-textured emitter with configurable burst count,
lifetime, initial speed, size, gravity, linear drag and alpha-over-life.
Complex attributes, noise, trails, mesh particles, collision, GPU
subemitters, Niagara dataflow and user-authored shader graphs remain
on the existing CPU emitter backend until separately implemented.

```cpp
#include "engine/fx/SpectraFX.h"

aiverse::spectra::ComputeParticles particles;
aiverse::spectra::ComputeEmitterDesc params;
params.capacity = 1024;
params.burst = 16;
params.backend = aiverse::spectra::ComputePreference::Automatic;
params.quality = aiverse::fx::FxQuality::High;

// Initialization can occur outside a frame. Device may be nullptr for
// pure CPU/headless tests; RequireGpu fails if compute is unavailable.
if (!particles.init(device, params)) {
    // inspect particles.reason()
}

// Presentation frame, AFTER Device::beginFrame, BEFORE scene render pass:
particles.setView(camera, viewProjection, width, height);
particles.burstAt({0,2,0}, {0,1,0}, {1,0.5f,0.1f,1});
if (!particles.step(1.f/60)) {
    // RHI timing/encoder order error; inspect particles.reason()
}

// During the scene's existing still-open depth-tested render pass:
particles.draw();
// After the render pass: Device::endPass, Device::endFrame.
// Before destroying gfx::Device: particles.shutdown().
```

The headless pure policy `chooseComputeBackend(backend, supportsCompute,
preference)` selects the GPU path only for Metal with compute support.
`CpuOnly` forces CPU; `Automatic` and `PreferGpu` fall back on
unsupported RHI or failed pipeline allocation; `RequireGpu` rejects
unsupported devices rather than silently running on CPU. Low-quality
GPU configurations intentionally choose CPU, respecting the existing
particle quality behavior. RHI compute calls must run outside render
passes and inside a GPU frame. The caller is responsible for a
single presentation owner/device thread and cleanup before device
destruction; this module contains no ECS gameplay or deterministic
simulator state.

**Metal implementation:** three compute passes per presentation frame
(queue accepted births → integrate age/velocity/position → expand
camera-facing quad vertices). GPU buffers are persistent:
64 bytes/particle for states and seeds, a 4-byte slot index and
40 bytes per billboard vertex. The compute shader writes a vertex
buffer directly sampled by the regular RHI particle vertex shader;
normal frames do **not** read the GPU positions back to CPU.
One depth-tested additive draw renders the pool. The current
prototype draws the entire capped slot index array (dead slots emit
degenerate quads); this is not a compacted or GPU-indirect renderer.
Maximum pool is 4096 slots, with a maximum of 64 spawned per call.

**GLSL source** is supplied for future Vulkan compute, but Vulkan
`gfx::Device::supportsCompute` is not implemented in the current RHI.
GLES3/GL33/WebGL2 do not run the compute kernel, so the existing CPU
particle renderer is used instead. Do not report those platforms as
GPU-supported. The dedicated test
`tests/vfx_gpu_backend_test.cpp` checks the selection policy, bounds,
headless CPU-fallback emissions and lifetime. It also contains a recording
Metal RHI mock that verifies spawn→integrate→expand dispatch ordering and
render-pass command sequencing **without compiling/launching real GPU
shaders**. It is not a Metal GPU integration test. CI and native GPU validation are still required.

**Known limitations:** no GPU compaction, no camera culling/LOD for
per-particle draws, no GPU-particle soft intersection, no mesh/ribbon
GPU rendering, no advanced emitter graphs, no cross-CPU/GPU effect
scheduler, no automatic FPS/RTS runtime migration, and no GPU
performance measurements. Runtime Metal shader compilation, GPU
resource hazards, multi-frame correctness and framebuffer output
remain **NOT VERIFIED**.

## Automatic graph-to-backend routing (Stage 11 — source committed, not verified)

`engine/fx/SpectraGraphRuntime.{h,cpp}` introduces a new **optional,
genre-independent** `aiverse::spectra::GraphRuntime`. It compiles a graph-v1
asset or accepts a precompiled `fx::EffectDef` and decides whether it can run
without semantic loss on the existing Metal compute prototype. Every
unsupported graph is executed with the **exact same full EffectDef** on
`fx::ParticleSystem`; complex modules are never stripped to fit the GPU.

```cpp
#include "engine/fx/SpectraFX.h"

aiverse::spectra::GraphRuntime runner;
aiverse::spectra::GraphRuntimeOptions options;
options.preference = aiverse::spectra::ComputePreference::Automatic;
options.quality = aiverse::fx::FxQuality::High;
options.capacity = 1024;

if (!runner.initGraph(device, graphJson, aiverse::Json::object(), options)) {
    // runner.reason() and runner.errors() describe the rejection
}

// After Device::beginFrame, before the scene render pass:
runner.setView(camera, viewProj, width, height);
runner.burstAt({0, 1, 0}, {0, 1, 0});
runner.step(1.f / 60.f);

// While the scene pass is open:
runner.draw(fog);
// On match change/rollback: runner.clear() outside an active encoder.
// Before device shutdown: runner.shutdown().
```

The schema validator `inspectGraphGpuEligibility(effect, options)` does not
require a GPU and is safe for native modules, headless tests and editor tools.
It returns `GraphGpuEligibility { eligible, gpu, reason }`. GPU v1 is allowed
**only** for one additive `builtin:soft` camera-aligned point emitter
containing one burst of 1–64 particles, constant speed/life/size/drag,
zero spread, world-Y gravity, and the default white-to-transparent
colour ramp. The current prototype supports only this limited payload.
Custom colours/gradients, multiple emitters, random ranges/curves,
custom shader graphs, trails/ribbons, noise, force fields, sub-emitters,
lights, collisions, texture animation, or any other unsupported module
force a full CPU fallback. The validator compares normalized complete
effect definitions against a known-supported template and also explicitly
checks sensitive unsupported modules and colour keys.

`ComputePreference::RequireGpu` fails if the graph is not eligible,
the real device does not expose Metal compute, the quality tier is too
low or the shader/pipeline cannot initialize. `Automatic` and
`PreferGpu` instead use the unchanged CPU asset. `CpuOnly` never
attempts GPU. A GPU health failure inside `ComputeParticles` can
subsequently fall back to its restricted CPU equivalent; `GraphRuntime`
continues to own that instance and reports the actual backend.

The editor and MCP `fx_graph_validate`, `fx_graph_get`,
`fx_graph_list` responses now add **`metalGpuEligible`** and
**`metalGpuReason`** after successful compilation. These are only
**asset capability diagnostics**, **not** proof that Metal is present
or that GPU execution succeeded. The SpectraFX visual editor shows the
same capability verdict under the properties inspector; its in-memory
graph preview still uses the established CPU renderer.

**Important boundaries:** This is an opt-in native API. It is not
automatically wired into existing FPS, RTS, editor gameplay, ECS
authoritative state, file assets or current save protocols. The
existing production client renderer stays on CPU. Actual Apple Silicon
GPU rendering, MSL compilation, multiple frames in flight and all
regression tests remain **unverified** until run in a real checkout.

Source tests: `tests/vfx_graph_runtime_test.cpp` exercises a supported
graph, strict rejections and CPU fallback; `tests/vfx_graph_ops_test.cpp`
checks MCP eligibility diagnostics. CTest name:
`vfx_graph_runtime` (label `SpectraFX`).

## Metal lifecycle and sparse draw-range hardening (Stage 12 — source committed, unverified)

The opt-in `ComputeParticles` Metal backend now tracks its occupied
**slot prefix** rather than always updating and drawing all allocated
slots. The CPU-owned slot-age bookkeeping determines the greatest
currently live slot index; compute integration and quad expansion
run through that prefix and the indexed draw is limited to
`submittedSlots * 6`. A pool containing no live particles and no
pending births performs no compute dispatch or draw.

**This is not GPU compaction:** gaps between active slots still produce
degenerate quads. True GPU stream compaction, prefix sums, indirect
draw counts, and thousands of materials are subsequent phases.
The hard 4096-slot capacity and original RHI buffer format remain.

`ComputeStats` now exposes:

- `submittedSlots`: prefix length actually submitted in the latest
  GPU draw (may exceed `alive` when holes exist).
- `gpuFallbacks`: automatic runtime transitions to CPU.
- `lostOnFallback`: number of GPU-resident particles discarded
  when the GPU device became unhealthy. Existing stats remain.

A **runtime device-loss event** is not a seamless animation step:
`ComputeParticles::step` reports failure. For `RequireGpu`, the
emitter becomes unavailable, retains its Metal backend identity
until shutdown and **does not switch to CPU**. For `Automatic` and
`PreferGpu`, the class attempts to initialize a CPU emitter, records
the discarded GPU particles and reports `false` for the transition
frame. CPU rendering on a device reported unhealthy is **not guaranteed**;
a real graphics-device recreation may be required. RHI encoder timing
errors continue to return failure without silently switching backend.

`GraphRuntime::step` now forwards the actual readiness, backend and
error message of its contained GPU/CPU runtime. `GraphRuntime::gpuActive`
also checks the underlying compute emitter, preventing false-positive
success reports after `RequireGpu` device loss. On clear, the sparse
range and GPU slot counters reset; attempting `clear` inside an
active GPU encoder leaves state intact and emits a diagnostic.

The RHI recording test
`tests/vfx_gpu_backend_test.cpp` has scenarios for active encoder
clear protection, sparse draw range, empty-pool zero dispatch, GPU
loss with `RequireGpu`, GPU→CPU transition with explicit lost-particle
telemetry and staggered particle expiry over multiple frames.
**These are mocked host-command tests, not actual MSL execution.**

A focused workflow,
`.github/workflows/spectrafx-validation.yml`, provides
pull-request and manual gates: Linux/macOS build and headless CTest
for SpectraFX; Linux additionally builds the editor and tests MCP
graph operations. The focused workflow does **not** validate a real
Metal compute shader or GPU image correctness. Actual compilation,
CI results, Apple Silicon rendering, screenshots and performance
measurements remain **NOT VERIFIED** until they run.

## Dense billboard gathering (Stage 13 — source committed, GPU validation pending)

The optional Metal `spectra::ComputeParticles` backend now **gathers
only live physical particle slots into contiguous GPU billboard
vertices**. The persistent particle-state pool still uses stable indices
for spawn/recycle; one additional 4-byte-per-slot storage buffer maps
dense draw ordinal → physical state slot. The CPU already owns
lifetime/slot bookkeeping and publishes the live-index list to GPU
before the compute encoder starts. The GPU reads each selected
particle's position/velocity/tint from its own resident state buffer
and writes a contiguous block of four vertices. Motion state is
**never read back to CPU**.

The frame order is: upload any new particle states and slot indices →
upload the current live slot map → compute births → integrate the
physical-slot range that was live *before* the step → GPU gather and
expand exactly the particles still alive *after* the step → regular
RHI indexed draw of `alive * 6` indices. The compute kernel uses
slot 4 for the live-index buffer and `Uniforms.meta.w` for the
alive count; both MSL and future GLSL sources use the same layout.
GPU updates stay between `beginFrame` and `beginPass`; draw remains
inside the scene render pass.

`ComputeStats::submittedSlots` now equals the densely emitted live
count (not the high-water physical slot index). For example, after
slots 0–2 expire while slots 3–5 survive, the GPU receives
`[3,4,5]` and submits 18 indices, not 36. The recording-RHI
regression verifies this map and the draw count across staggered
birth/death frames. The new live-slot upload is `4 * alive` bytes
each active frame and is tracked under `ComputeStats::uploadedBytes`.
An idle particle pool continues to skip compute and draw.

**Scope and limitations:** This is GPU *vertex gathering* from a
CPU-maintained live-slot list. It eliminates sparse-dead-slot rendering,
but it is **not a fully GPU-resident atomic/prefix-sum compaction**
or GPU-authored indirect draw. CPU still scans at most 4096 age
records per frame, and live-index uploads incur bandwidth. The
fixed 16-bit index buffer still limits the pool to 4096 billboards.
The backend is restricted to additive soft sprites eligible under
`GraphRuntime` and is not enabled by default in existing RTS/FPS
clients. Metal shader compilation, image output and performance on
Apple Silicon remain **NOT VERIFIED** until native CI/device tests
complete. Non-Metal RHI paths continue using the original CPU
`ParticleSystem`.

## Experimental GPU-authored particle draw (Stage 14)

The Metal subset now has an **explicitly opt-in** GPU-side compact/draw
mode, using the existing optional RHI methods. This does not change
the default backend or any existing FPS/RTS runtime:

```cpp
#include "engine/fx/SpectraFX.h"

aiverse::spectra::GraphRuntimeOptions opts;
opts.preference = aiverse::spectra::ComputePreference::Automatic;
opts.gpuIndirect = true; // experimental; default is false
// For the standalone billboard emitter:
// aiverse::spectra::ComputeEmitterDesc cfg;
// cfg.gpuIndirect = true;
```

### RHI contract and compute passes

The portable `gfx::Device` adds the default-false capability query
`supportsGpuAuthoredIndirectDraw()`. Metal reports support through
its existing plain `drawIndexedGpuIndirect` path, independently of
experimental ICB features. Vulkan/GL/GLES/WebGL continue to return
false. A device advertising only `supportsCompute()` is not enough.

Metal compute uses two additional persistent storage buffers:
`aliveCounter` (4-byte atomic uint, compute slot **5**) and
`gpuDrawArgs` (20-byte `gfx::DrawIndexedIndirectCommand`, slot **6**).
The existing `ParticleState` and 40-byte expanded vertex formats
are unchanged.

```text
Mode 3  reset GPU alive counter
Mode 0  spawn into persistent physical GPU particle slots
Mode 1  integrate physical state / kill expired particles
Mode 4  atomic-fetch-add surviving particle -> dense vertex ordinal
Mode 5  write GPU indexed draw args (indexCount = alive * 6)
Render  gfx::Device::drawIndexedGpuIndirect(gpuDrawArgs, 0)
```

Metal's RHI inserts a buffer memory barrier after each compute
dispatch, and graphics submission occurs in the following render
encoder. Surviving particle order is nondeterministic (atomic allocation),
which is acceptable only for the current **additive** billboard subset.
The GPU counter and draw arguments are not read back to CPU during
normal rendering.

If `gpuIndirect=true` but the device lacks
`supportsGpuAuthoredIndirectDraw()`, SpectraFX retains Stage 13's
CPU-published dense slot gather with regular `drawIndexed`. If an
already-enabled indirect command is rejected *during drawing*, it
reports an error and skips the draw instead of drawing potentially
uninitialized/stale vertices with an approximate CPU count.

`ComputeParticles::gpuIndirectActive()` reports actual selection;
`ComputeStats::gpuCompactDispatches` and
`gpuIndirectDraws` track submitted GPU operations. When indirect is
active, `stats().submittedSlots` and `stats().alive` are **host-side
lifespan estimates**, not GPU-readback measurements. The actual
indexed count comes from the GPU-authored 20-byte command.

### What remains on CPU

GPU indirect removes the **per-frame upload of the live slot map**,
and GPU compaction/draw-count calculation no longer relies on that
map. The CPU **still** owns slot allocation and lifetime/free-list
bookkeeping, and scans a bounded maximum of 4096 age records.
GPU motion and dense quad generation remain GPU-side.
This is not a GPU-only allocator, multi-emitter scheduler,
sorting system, or complete Niagara simulation.

### Validation gates

- `tests/vfx_gpu_backend_test.cpp`: the recording RHI verifies
  reset/spawn/integrate/atomic compact/args ordering, absence of
  host live-slot uploads in GPU-driven mode, capability fallback,
  and rejection without unsafe direct draw.
- `tests/vfx_metal_compaction_smoke.mm`: **real Metal compute test**
  compiling the MSL `cs_main`, running GPU passes and reading
  the counter, indirect arguments and compacted vertex results
  between frames. On macOS without a Metal GPU it returns 77 and
  CTest marks it **SKIPPED**, not PASS.
- `.github/workflows/spectrafx-validation.yml` builds and attempts
  the Metal smoke on macOS alongside the headless suite; Linux
  still tests portable CPU/recording paths.

All of these changes are **committed source, not verified execution**.
At the latest workflow query, the new SpectraFX CI jobs were still
queued. No passing MSL compilation, Metal dispatch, offscreen image,
timing or GPU visual parity claim is made. A real image-level Metal
and multiple-frames-in-flight test is still required before a
shipping/default GPU selection.

## Cross-instance material batching (Stage 14B — source committed, unverified)

Multiple `GraphRuntime` instances previously owned separate
`fx::ParticleSystem` objects and could each submit the same sprite
material as a separate GPU draw call. The opt-in **`spectra::BatchWorld`**
(`engine/fx/SpectraBatchWorld.{h,cpp}`) puts arbitrary graph/effect
definitions into **one shared presentation ParticleSystem**. This
reuses the existing global `ParticleSystem::Impl::batchFor` and
`FxRender.cpp` render queue, which already groups particles
**independent of effect and instance IDs**.

```cpp
#include "engine/fx/SpectraFX.h"

aiverse::spectra::BatchWorld shared;
shared.init(device, aiverse::fx::FxQuality::High);

aiverse::spectra::GraphRuntime smoke, fire;
// smokeJson and fireJson may represent different effects/parameters.
smoke.initGraph(shared, smokeJson);
fire.initGraph(shared, fireJson);

// Each GraphRuntime keeps its OWN handles; no CPU renderer per source.
smoke.burstAt({0,1,0});
fire.burstAt({2,1,0});

// ONCE PER FRAME (not once per GraphRuntime):
shared.setView(camera, viewProjection, width, height);
shared.update(dt);
// INSIDE the existing scene render pass, ONCE:
shared.draw(fog);

// Kill smoke's particles without affecting fire:
smoke.clear();
// Full scene/match reset: shared.clear().
smoke.shutdown();
fire.shutdown();
shared.shutdown(); // MUST outlive all attached GraphRuntime instances.
```

`BatchWorld::defineEffect` caches equivalent fully serialized effect
definitions while preserving distinct emitter data. `defineGraph`
compiles v1 JSON through the canonical compiler and registers its
effect in the same world. `play`, `move`, `stop`, `kill` and
`playing` are the underlying ParticleSystem handle operations.
The caller can set texture/mesh loaders on the shared world before
registering effects, retrieve `stats()`, call `collectLights`, and
use its existing global quality budget. These APIs are **independent
of FPS, RTS or any particular game module** and live outside
authoritative ECS/rollback state.

**Draw batching key:** the actual underlying RHI texture/atlas
handle, **blend mode**, compiled shader-graph identity and
primitive kind (mesh vs quad). Effects with distinct animations,
sizes, instances or spawn parameters can use the same draw call
when these render-state requirements match. Even different
sprites in the **same atlas** can batch because their UVs are
encoded independently in vertex data. The same texture with
different alpha/additive blending or shader graph must NOT batch.
A draw is split at the 16-bit-index limit of 16,384 quads.
Alpha sorting still follows the current renderer's existing
ordering rules; a single cross-texture globally sorted transparent
render queue is a different architectural task.

`GraphRuntime::initGraph(BatchWorld&, ...)` and
`initEffect(BatchWorld&, ...)` explicitly opt the instance into
this shared renderer. Its `burstAt` captures handles and
`clear()/shutdown()` kills **only those handles**, not all the
world's particles. The source handle list is bounded at 65,536:
the oldest handle is killed before overflowing the bound.
In shared mode, per-source `step`, `setView` and `draw`
are no-ops to prevent double updates and duplicate draw calls.
**The host must call the shared world's methods once per frame.**
`RequireGpu` is rejected for shared-world registration, not
silently downgraded; current pooling is for the mature
CPU-simulated/RHI-rendered emitter path.

**Existing game apps:** RTS/FPS clients already use one central
`ParticleSystem` and benefit from its built-in batching.
This feature addresses **new native/graph instance contexts**
that formerly created separate renderers. It does not override
individual GPU `ComputeParticles` instances. Shared Metal
GPU-particle pools across multiple independent effects still
need a dedicated multi-emitter GPU scheduler and actual device QA.

**Regression source:** `tests/vfx_shared_batch_world_test.cpp`
records RHI indexed draw calls and verifies 5 + 7 particles
from distinct effect definitions sharing one atlas texture
produce **one** draw. Different blend modes produce **two**
draws. It also checks independent source cleanup, graph
registration, deduplication and no accidental per-source
updates. CTest: `vfx_shared_batch_world`, target
`aiverse_vfx_shared_batch_tests`, label `SpectraFX`.
The focused CI workflow builds this test on Linux and macOS;
actual CI/CTest results remain **unverified** in this session.

## Genre-neutral cue scheduling and ECS source bridge (Stage 15)

The opt-in **`spectra::CueScheduler`** in
`engine/fx/SpectraCueScheduler.{h,cpp}` coordinates many independent
event/attached-effect sources over the already existing **`BatchWorld`**.
It does **not** create another ParticleSystem or issue `update`/`draw`
itself; the owning presentation host still calls `BatchWorld` once
per frame. All scheduling and particle handles stay outside gameplay
ECS, deterministic hashes and rollback state.

Use the canonical include `engine/fx/SpectraFX.h` for the pure
presentation API, or `<aiverselib/genre/SpectraFX.h>` in native
ECS projects to also access `syncEcsCueSources` and
`acceptSimCueTick`. The ECS adapter **reuses**
`SpectraSourceComponent` (no new ECS component type or ID).
The host supplies the same generic render-pose resolver as the
existing `SpectraPresentationSystem`. Source keys use
`Entity::packed()` so recycled indices with new generations
cannot reuse an old effect handle.

```cpp
#include <aiverselib/genre/SpectraFX.h>

// BatchWorld and CueScheduler must outlive each other's handles in this
// order: shared BatchWorld initialized first, CueScheduler destroyed first.
aiverse::spectra::BatchWorld shared;
shared.init(device, aiverse::fx::FxQuality::High);
aiverse::spectra::CueBudgets limits;
limits.maxQueued = 512;
limits.maxPerTick = 64;
limits.maxPerFrame = 128;
limits.maxAttachments = 1024;
limits.maxTrackedOneShots = 2048;
aiverse::spectra::CueScheduler cues(shared, limits);

cues.registerEffect("impact", impactEffectDef); // or registerGraph(...)
cues.registerEffect("engine_fire", loopingEffectDef);
cues.bindCue(72, "impact"); // game-defined event type, not an FPS/RTS enum

// On the presentation thread, after a committed gameplay tick:
aiverse::spectra::acceptSimCueTick(cues, tick, simEvents);

// ONCE per presentation frame, with graphics Device in a valid frame:
shared.setView(camera, viewProj, width, height);
aiverse::spectra::syncEcsCueSources(world, cues, resolvePose);
cues.flush();          // deferred one-shots, bounded per frame
shared.update(dt);     // exactly once for ALL sources
// With the scene render pass open:
shared.draw(fog);      // exactly once, batches compatible materials

// Before replaying restored ticks after rollback/new match:
cues.resetForRollback();  // kills only this scheduler's cues/sources
// Do not call shared.clear() unless resetting ALL other presentation owners.
// Destroy cues BEFORE shared.shutdown()/gfx::Device destruction.
```

### Budgets and ownership rules

`CueBudgets` bounds outstanding events, per-tick admissions,
per-frame one-shot starts, simultaneous ECS attachments and the
rollback-tracked one-shot handle ring. Inputs are clamped to safe
hard ceilings; tracked one-shot capacity cannot be zero.
`CueScheduler::acceptTick(tick, events)` ignores duplicate or
decreasing ticks until `resetForRollback()`, but **does not**
advance the underlying particle renderer. `flush()` starts up to
the frame budget and leaves the remaining bounded queue for later
frames. The queue stores resolved effect IDs at acceptance time
to avoid silent behavior changes during an asset hot-reload.

When the tracked one-shot handle ring fills, the oldest effect
is **killed before** its slot is reused, so its particles cannot
escape rollback ownership. `syncAttachments` consumes the complete
set of active sources for the current frame, moves existing handles,
restarts on effect/revision/scale change and retires absent sources
**before** applying the attachment cap to newcomers. Normal detach
uses each component's stop-vs-kill policy; **rollback always kills**
the scheduler's own handles and clears its pending queue, allowing
older restored committed ticks to play again. Other producers using
the same `BatchWorld` are untouched.

`CueSchedulerStats` reports accepted/duplicate ticks, accepted
and emitted cues, queued/attached counts, budget drops, unbound
effects, failed spawns, evicted one-shot handles and source restarts.
Call `registerGraph`/ `registerEffect` once during setup or asset
reload, not every tick; `bindCue` maps generic unsigned event
types to registered effect names. For custom colour, scale or direction
per event, call `acceptTick` directly with `CueEvent`;
`acceptSimCueTick` uses the default up-direction, scale and tint
and converts SimEvent fixed-point positions into presentation floats.

This scheduler currently uses the mature **shared CPU particle
backend** plus RHI rendering/batching. It is deliberately
**not** a multi-emitter Metal compute scheduler and does not
enable `gpuIndirect` for ordinary graph assets. Custom GPU
per-instance scheduling, automatic native RTS/FPS client migration,
full rollback visual parity and actual platform performance remain
future verification/work items.

Tests: `tests/vfx_cue_scheduler_test.cpp`, target
`aiverse_vfx_cue_scheduler_tests`, CTest `vfx_cue_scheduler` with
label `SpectraFX`. Scenarios cover two unrelated ECS genres,
generation-safe attachment replacement, committed tick dedup,
queue/frame/attachment caps, one-shot eviction, rollback cleanup
without affecting an unrelated BatchWorld effect, and graph
registration diagnostics. **Tests are source-committed but not
confirmed executed** until native CI results are available.

## Typed dataflow graph v2 (Stage 16)

SpectraFX now accepts `"version":2` graph assets through the same
`fx::compileSpectraGraph` / `spectra::compileGraph` API used by
the editor, MCP, `GraphAssets`, `GraphRuntime` and shared `BatchWorld`.
All existing `"version":1` assets retain their original compilation
path; the public `spectra::kGraphVersion` now reports the newest
authoring version (2), with `kLegacyGraphVersion=1`.

V2 adds two distinct types of connections: **execution links**
`{"from":"spawn","to":"init"}` order Spawn→Initialize→Update
modules→Output; **typed value-port links**
`{"from":"math","to":"init","toPort":"speed"}` bind the
value of a data node into one property (dotted property paths are
supported, e.g. `noise.frequency`). Equivalent inline references
are `{"$node":"math"}` and `{"$param":"wind"}`.
Data links can connect math nodes to other math nodes, with cycle
validation even for disconnected data branches.

The new `engine/fx/SpectraValueGraph.h` implements a bounded
constant-expression DAG of literal values, typed exposed parameters,
scalar/vector math, comparisons and `Select` operations.
Representative nodes: `Float`, `Integer`, `Bool`,
`Vec2/Vec3/Vec4`, `Color`, `String`, `Curve`, `Gradient`,
`Parameter`, `Add`, `Subtract`, `Multiply`, `Divide`,
`Min/Max`, `Clamp`, `Lerp`, `Sin/Cos`, `Pow/Sqrt`,
`Floor/Ceil`, `Normalize/Length`, `Dot/Cross`,
`ComposeVec2/3/4`, `Select`, `Greater/Less/Equal`.
Invalid types, nonfinite results, division by zero, excessive graph
depth, duplicate node IDs and cyclic references are rejected.
Parameter overrides must match their declared JSON type.

`AgeNormalized` is meaningful only inside an explicitly
sampled `{"$curve":"expression_id","samples":33}`
reference. It compiles a scalar expression over normalized age to
2–64 keys understood by the existing CPU FxRange/curve system.
It is **not** a per-particle script or general programmable GPU
kernel. Math expressions without an age dependency are resolved
at graph compile time; sampled curves are evaluated during
the already-existing particle lifetime LUT update.

The new `engine/fx/SpectraGraphV2.h` compiler lowers module-stage
nodes to existing strict `Update` module properties:
`NoiseModule`, `CollisionModule`, `TrailModule`,
`LightModule`, `ForceModule`, `VelocityModule`,
`SubEmitterModule`, `TriggerModule`,
`InheritVelocityModule`, `SpeedLimitModule`,
`ColorBySpeedModule`, `SizeBySpeedModule`,
`SpinBySpeedModule`, `LifeBySpeedModule`. Flat
`ShapeModule`, `FlipbookModule`, `MaterialModule`,
`SortModule`, `CustomModule` reuse the complete existing
`FxEffect` grammar; unsupported emitter keys are still rejected
by `fx::parseEffect`. `SubEmitterModule.properties.items`
contains the sub-emitter array. Existing sprite/mesh/ribbon
outputs remain available.

The output is an ordinary `fx::EffectDef` compiled by the
**same authoritative v1 parser**, preserving native C++/genre
compatibility and the mature CPU render path. Any effect with
modules outside the limited Metal subset continues to use
the full CPU fallback. There is no graph-specific dependency
on FPS, RTS or other gameplay modules.

**Example asset:** `examples/spectrafx/typed_noise_trail_v2.vfx.json`.
It combines exposed parameters, multiply nodes, explicit value
connections, an age-dependent fade sampled into keys, and noise
and trail modules.

**Explicit limits:** V2 is a typed authoring/dataflow *frontend*
for existing CPU effect modules, not arbitrary runtime per-particle
bytecode or GPU kernel compilation. Full user-defined functions,
variable-length particle attribute buffers and per-particle
event graph execution remain separate implementation steps.
The code is committed, but no CMake build, test run, screenshot
or Metal validation has yet been executed in this stage.

## Visual authoring and safe drafts for graph v2 (Stage 17)

The optional **SpectraFX** ImGui editor now creates graph version 2
by default. Graph version 1 assets can still be opened and edited,
and the **v1 → v2** button upgrades a draft in memory without
immediately overwriting its source file.

The shared pure C++ model `engine/fx/SpectraAuthoring.h`
provides `spectra::authoring::nodeCatalog`, `newGraphV2`,
`addNode`, `connectExec`, `connectData`, `disconnect`,
`inputPorts`, `node`, `isData`, `isOutput` and
`reaches`. It lives outside the editor and can be reused
by native C++ tooling or agents through
`engine/fx/SpectraFX.h`. Node definitions contain
categories, default properties and typed input port names.
The editor shows a grouped catalog for value, math, vector,
logic, simulation, force, event and renderer nodes.
Execution links and typed data links use different canvas colours;
the right inspector lets the author choose and connect input
properties (including nested paths) to a selected value node.
Cycle prevention applies to both link kinds.

The right inspector offers fast editors for numeric, integer,
bool, vector and colour literal nodes, module-specific field
controls for noise, collision, force, trails, light, rendering,
sorting and shape settings, and keyframe editing for
`Curve` and `Gradient` value nodes. A curve can add/remove
keys and edit time/value; a gradient can edit RGBA keys.
Unknown modules, $node expressions and complex nested fields
remain accessible through the existing raw JSON inspector,
without lossy coercion.

### Safe incomplete graph workflow

A graph need not compile successfully at every intermediate
authoring step. Draft assets live under
`fx/graphs/_drafts/<name>.vfx.json` and are *not*
enumerated or loaded by the production graph registry.
The editor has separate **Черновик** / **Опубликовать**
actions and a draft list. A draft is saved even while links
or modules are incomplete; publishing compiles the exact
saved draft and refuses to replace a published graph if
the compiler returns errors. Existing
`fx/graphs/<name>.vfx.json` and legacy `fx/*.json`
remain unchanged by saving an incomplete draft.

Additional MCP actions:

- `fx_graph_node_catalog`: node types, categories, default
  property JSON, data input port names, and a new default graph.
- `fx_graph_edit`: pure in-memory `new`, `upgrade`, `add`,
  `connect`/`disconnect`, `deleteNode`, `setProperty`
  operations. Returns edited graph and compiler diagnostics;
  no file is written.
- `fx_graph_draft_set`, `fx_graph_draft_get`,
  `fx_graph_draft_list`, `fx_graph_draft_publish`:
  non-destructive authoring, validation and explicit publication.
- Existing `fx_graph_get/set/list/validate/bake` continue
  working for both graph versions; published graphs always
  undergo strict compilation.

The code is committed; **GUI launch, CMake build and CTest are
intentionally deferred** until functional work is finished.
Current advanced editing is still a basic ImGui canvas and
raw JSON fallback, not a complete commercial Niagara editor;
function subgraphs, custom per-particle attribute debugging,
profiler timelines and shader node codegen remain further
work items.

## Reusable typed function subgraphs (Stage 18)

SpectraFX graph v2 now supports reusable **pure value functions**.
Function bodies are nested typed value graphs, independent of
rendering, gameplay genres and ECS state:

```json
{
  "version": 2,
  "functions": {
    "scale_speed": {
      "parameters": { "base": 1, "gain": 1 },
      "returns": "number",
      "nodes": [
        { "id": "a", "type": "Parameter",
          "properties": { "name": "base" } },
        { "id": "b", "type": "Parameter",
          "properties": { "name": "gain" } },
        { "id": "out", "type": "Multiply",
          "properties": {
            "a": { "$node": "a" },
            "b": { "$node": "b" }
          } }
      ],
      "output": "out"
    }
  }
}
```

A caller graph references the function with a `FunctionCall`
value node:

```json
{"id":"scale","type":"FunctionCall","properties":{
  "function":"scale_speed",
  "arguments":{"base":{"$param":"speed"},"gain":1.5}
}}
```

Function signatures declare typed default `parameters`, an output
node ID and an optional strict `returns` type:
`number`, `bool`, `string`, `vec2`, `vec3`, `vec4`.
Calls inherit the caller's compile-time or normalized-age
sampling context, allowing reusable fade/wind/response functions
that compile into existing CPU FxRange curves. Function definitions
contain only value DAG nodes, never Spawn/Update/Output stages.
The graph compiler rejects unknown functions, wrong argument
types, recursion (including disconnected definitions), more
than 32 declared functions or more than eight nested calls.
No native scripts, reflection or arbitrary executable code
are evaluated.

### Shared function asset libraries

`engine/fx/SpectraFunctionAssets.h` provides
`validateFunctionLibrary`, `mergeFunctionLibrary` and
`FunctionLibraryAssets` with an injected file reader and
last-known-good reload fallback. Library files use
`fx/functions/<name>.vfxfn.json`:

```json
{"version":1,"functions":{"my_function":{
  "parameters":{"x":1},
  "returns":"number",
  "nodes":[{"id":"result","type":"Parameter",
            "properties":{"name":"x"}}],
  "output":"result"
}}}
```

Importing a library into a v2 graph explicitly copies its
validated function definitions; existing function names with
different bodies reject the import unless replace is explicitly
requested. Import works with incomplete editor drafts, and
the pure graph compiler still performs no filesystem IO.
The native API is available from `engine/fx/SpectraFX.h`.

MCP supports `fx_function_list`, `fx_function_get`,
`fx_function_set`, `fx_function_import`, and
`fx_function_export`. An import returns an edited graph in
memory; the user explicitly publishes or saves a draft.
The SpectraFX editor has a FunctionCall selector, embedded
function JSON editor, plus import/export controls for reusable
function-library names. Example assets:
`examples/spectrafx/common_math.vfxfn.json` and
`examples/spectrafx/function_calls_v2.vfx.json`.

**Status:** full code committed, testing deliberately deferred.
Functions compile pure values and age-sampled curves into the
existing CPU emitter definitions; they do not yet define
per-particle mutable attribute bytecode or programmable GPU
simulation kernels.

## Compiled per-particle CPU programs (Stage 19)

The existing `fx::ParticleSystem` can now execute an optional
`EmitterDef::particleProgram`: a small compiled, bounded expression
program running at **individual particle** lifecycle stages. It
does NOT reinterpret JSON for every particle or run any C++/Lua
script from the effect file. The compiler validates opcodes,
targets, nesting, finite constants and budgets, and converts
the JSON into precompiled instructions in
`engine/fx/SpectraParticleProgram.h`. On every particle step,
the VM uses a fixed-size stack with **no dynamic allocation**.
No gameplay ECS state is read or written.

Use the `ParticleProgramModule` pipeline node in a v2 graph
before an output, or provide `particleProgram` in an ordinary
legacy emitter definition. Supported named program stages are
`spawn`, `update`, `death`, `collision`, `trigger`.
Each stage contains at most 32 assignments/updates and 256
postfix VM instructions. An expression is a finite number,
`{"var":"field"}`, or a tree of built-in arithmetic, clamp,
lerp, comparison and trigonometric operations. Recursion
depth is bounded by 16. No arbitrary executable source,
network, file access or plugin invocation is allowed.

```json
{
  "version":1,
  "spawn":[
    {"target":"attribute0","value":0.75}
  ],
  "update":[
    {"op":"addRate","target":"velocity.y",
     "value":{"op":"multiply",
       "a":2.2,
       "b":{"op":"subtract","a":1,
            "b":{"var":"ageNormalized"}}}},
    {"op":"set","target":"tint.a",
     "value":{"op":"saturate","a":{
       "op":"subtract","a":1,"b":{"var":"ageNormalized"}}}}
  ]
}
```

Supported scalar reads include `age`, `ageNormalized`,
`dt`, `uid`, world-space `position.x/y/z`,
`velocity.x/y/z`, `size`, `rotation`,
`tint.r/g/b/a`, and eight independent user-defined
`attribute0..7` floats stored with each visual particle.
Read-only lifecycle fields cannot be written by a program.
Actions `set`, `add`, `multiply`, `addRate`
(`value * dt` per second) apply to writable fields.
Output values are finite/bounded, tint channels clamp to
0..1 and size clamps to nonnegative.

`spawn` is invoked after built-in particle initialization
and before trails/birth handlers; `update` runs after
existing gravity/forces/integration and before collision
and trigger testing; event stages run at the
corresponding collision, trigger and death boundaries.
The legacy velocity/size/light/noise/trail mechanics still
run for any effect lacking a program with no new script
evaluation. Dynamic attributes can drive size, rotation,
tint and motion through subsequent VM statements.
Function graphs v2 remain a separate compile-time
subgraph feature; arbitrary graph-to-GPU kernel codegen
is not implied.

**Example**: `examples/spectrafx/runtime_particle_program_v2.vfx.json`
uses a custom attribute, age-dependent rising force, alpha fade,
variable size and a death handler. Advanced effects using this
CPU-only VM are automatically excluded from the restricted
experimental Metal GPU subset: `GraphRuntime` must preserve
the full source effect on CPU, not silently drop the program.
User requested postponing builds/tests; source is committed
but **not compiled or runtime verified**.

## Graph v2 named live instance parameters (Stage 20 — functional source committed, tests deferred)

Graphs can declare up to **eight named CPU particle VM scalar inputs**
separately from compile-time `parameters`. Runtime instance values
can change without rebuilding an effect, recompiling a graph, or changing
another instance's data. This is **presentation-only**, genre-neutral
and does not write values into gameplay ECS.

The graph-v2 `instanceParameters` root maps a safe name to
`{slot,default,min,max}` (unique slots 0..7, finite bounded floats).
A data node of type `InstanceParameter` refers to `properties.name`,
and a `ParticleProgramModule` expression using
`{"$runtime":"node_id"}` is lowered at compile time to the CPU VM
read `param0` … `param7`. The values stay dynamic at particle
runtime, unlike normal graph `$param` literals.

```json
{
  "version": 2,
  "instanceParameters": {
    "lift": {"slot": 0, "default": 2.2, "min": 0, "max": 25},
    "intensity": {"slot": 1, "default": 1, "min": 0, "max": 10}
  }
}
```

Embed the declaration alongside the normal `nodes`, `links`,
`outputs` and `parameters` fields. See
`examples/spectrafx/per_instance_parameters_v2.vfx.json` for a
complete graph whose per-particle upward acceleration uses the
named `lift` value. `engine/fx/SpectraInstanceParameters.h`
contains the shared validation metadata.

```cpp
#include "engine/fx/SpectraFX.h"

// Same graph asset can be compiled by multiple independent runtimes:
aiverse::spectra::GraphRuntime fireA, fireB;
fireA.initGraph(device, graphJson);
fireB.initGraph(device, graphJson);

fireA.setNamedProgramParameter("lift", 2.5f);
fireB.setNamedProgramParameter("lift", 10.0f);

// For already-live particles, propagate the new value explicitly:
fireA.setNamedProgramParameter("lift", 5.0f, true);

// Or without a graph schema, use raw CPU VM slot 0..7:
fireA.setProgramParameter(0, 5.0f, true);
```

Calling `setProgramParameter(slot,value,affectExisting=false)` on
`GraphRuntime` changes future births for that **source only**;
`affectExisting=true` additionally updates current particles
owned by its tracked handles. The public `fx::ParticleSystem`
also offers `setProgramParameter(handle,slot,value,affectExisting)`
and atomic `playWithProgramParameters(effect,pos,dir,scale,tint,params)`,
which sets all eight scalars **before prewarm and Spawn callbacks**.
`spectra::BatchWorld` exposes the same atomic playback and per-handle
setter. The standard `play` remains source-compatible and passes zeros.

`spectra::CueEvent` and `AttachedCue` optionally carry
`programParams[8]` plus a `programParamMask`. The cue scheduler
applies enabled values before a new one-shot/attached effect starts.
Changes to the parameters on an existing continuous source update
the current particles **without restarting the effect**. Disabled
slots remain zero; removing an override clears the old slot value.
The scheduler still owns the handles solely on the presentation side.

In the SpectraFX editor (version 2), the right panel has
**Параметры экземпляра (runtime)**. It edits a validated JSON schema,
shows named slider values for the current preview instance, and
passes `instanceValues` to the in-memory `fx_graph_preview`
operation. The preview runs the existing CPU ParticleSystem with
values applied before first Spawn and persists them during loop
restarts. This does not create new `fx/*.json` files.

**Backend contract:** arbitrary CPU particle VM and named
`InstanceParameter` graphs do not run on the experimental Metal
simple-billboard kernel. `GraphRuntime::initGraph` selects the full
CPU runtime for such graphs; `RequireGpu` rejects the request
rather than silently dropping instance parameters. No GPU VM/JIT
translation, HLSL/MSL codegen, or GPU-side arbitrary per-instance
parameter buffer is implied.

**Status:** the user requested functional implementation first.
All changes above are committed source; neither native compile,
CTest, GUI preview nor cross-platform GPU execution was run for
Stage 20. Do not claim source completeness or parity until verified.

## CPU VM custom particle attributes in shader materials (Stage 21 — source committed, tests deferred)

Emitters may now opt into a **four-channel shader-graph input map**
via `graphInputs`. The renderer reuses the preexisting 40-byte vertex
layout and its `float4 graphData` field, so compatible draw batching
and legacy sprite/mesh vertex formats remain unchanged. The source
mapping is validated by `fx::parseEffect`, serialized by
`fx::effectToJson`, and precomputed once in
`ParticleSystem::Impl::build` into four small integer selectors.
It adds no JSON evaluation in the per-particle draw loop.

Example inside any emitter in `fx/<name>.json` or a graph-v2
`MaterialModule` / output node's flat properties:

```json
{
  "graphInputs": ["attribute0", "attribute1", "param0", "age"]
}
```

Exactly **four** source names are required. Valid names:
`age`, `lifetime`, `random`, `zero`,
`attribute0` through `attribute7`, and
`param0` through `param7`. The scalar user attributes are
written by the CPU `particleProgram` at Spawn/Update/event stages.
The instance parameters are captured at emission or updated by the
per-instance setters from Stage 20. Both are presentation-only.

`shaderGraph` already consumes the four vertex channels as
`sg_particle` in the built-in material graph compiler. This
Stage 21 maps chosen VM values into those channels; authors
can combine them with the graph's math/colour/texture nodes.
It does **not** change the material graph language, the number
of vertex channels or shader/texture slot budgets.

**Source compatibility:** when `graphInputs` is absent,
the generated vertex data is exactly
`[particle age, particle lifetime, stable random, zero]`,
matching the original shader graph contract. This mapping is
opt-in for CPU-backed particles and does not alter the generic
non-shader particle material's output. A newly authored mapping
is included in the fully serialized effect identity for
`BatchWorld` deduplication; effects with different vertex
inputs still share draw calls when their actual material/render
state matches, as each vertex carries its own data.

The experimental Metal simple-billboard backend does not execute
the CPU particle VM or arbitrary shader graphs. Modified effects
that use nondefault `graphInputs` are rejected by the strict GPU
eligibility gate and retain the existing full CPU renderer.
No automatic GPU shader VM compiler, GPU graph-param binding,
full custom-attribute register file, or guaranteed visual parity
is claimed.

**Verification:** user deferred building/testing while finishing
functional work. Stage 21 comprises committed C++ source only:
no CMake/CTest, editor screenshot, cross-backend shader compilation,
GPU timing or material output comparison was actually run.

## Experimental Metal scalar particle VM execution (Stage 22 — source committed, unverified)

`engine/fx/SpectraGpuProgram.h` provides a fixed binary payload for a
strict CPU particle-program subset. The Stage-22 Metal compute kernel now
**interprets** that validated payload directly during particle Spawn and
Update. This is not dynamic MSL shader generation; the shader compiles
once, while the payload is an ordinary persistent RHI storage buffer.
The older compiler/ABI preparation existed before the GPU execution
implementation; actual compute execution and parameter buffers were
added in Stage 22.

**Opt-in is mandatory and false by default:**

```cpp
#include "engine/fx/SpectraFX.h"

aiverse::spectra::GraphRuntimeOptions options;
options.preference=aiverse::spectra::ComputePreference::Automatic;
options.gpuProgram=true;  // EXPERIMENTAL; defaults to false
options.gpuIndirect=false; // independent option from Stage 14

aiverse::spectra::GraphRuntime emitter;
if(!emitter.initGraph(device,graphJson,aiverse::Json::object(),options)) {
    // emitter.reason(), emitter.errors()
}
emitter.setNamedProgramParameter("lift",3.5f);
emitter.burstAt({0,1,0}); // parameters snapshotted before Spawn
// Per-frame device sequence: beginFrame -> emitter.step -> beginPass ->
// emitter.draw -> endPass -> endFrame.
emitter.setNamedProgramParameter("lift",5.0f,true);
// affectExisting = true queues a GPU parameter-buffer upload for next step.
```

The opt-in GPU eligibility gate first compares **every** compiled
emitter field against the single additive `builtin:soft` point-burst
contract; it only permits a `particleProgram` when its entire program
passes `gpu_program::prepareForMetal`. Any other module, arbitrary
material `shaderGraph`, `graphInputs`, events, collision, trails,
multiple emitters, mesh particles, forced fields, sorting, random
ranges or graph stages outside the supported subset still use the
**exact original full CPU effect**. `RequireGpu` fails instead of
silently dropping behavior.

Supported VM stages: **Spawn** and **Update**. Readable scalar fields:
`age`, `ageNormalized`, `dt`, `position.x/y/z`,
`velocity.x/y/z`, `size`, `tint.r/g/b/a`, and
read-only per-instance `param0..param7`.
Writable fields: position/velocity components, size and tint.
Actions: `set`, `add`, `addRate`, `multiply`.
Supported expression ops: finite constants and field reads,
add/subtract/multiply, min/max, abs/negate/saturate, clamp and
lerp. Numeric results are finite/bounded; size clamps at zero,
tint channels at [0,1]. The kernel rejects malformed instructions
and skips failed statements rather than modifying particle state.

Unsupported: user `attribute0..7` registers, `uid`, rotation,
division/pow/trig/sqrt/floor/ceil, comparisons, conditional Select,
collision/trigger/death programs, loops/recursion, runtime user
scripts and all non-Metal compute backends. The GPU interpreter
is bounded at **24 total statements**, **192 instructions** and
**48 instructions per statement**. ABI/opcode/field numbers
are compile-time asserted against the CPU VM enums; these checks
still require an actual native build to confirm.

The original 64-byte GPU particle state and 40-byte billboard vertex
are unchanged. The Metal compute pipeline additionally binds:
- slot **7**: seed-time parameter snapshots (32 bytes per queued birth);
- slot **9**: persistent parameter snapshots (32 bytes per physical slot);
- slot **10**: validated GPU VM payload (10,256 bytes, ABI version 1);
- slot **8** remains the existing `Uniforms` constant buffer.

Both old host-gather and opt-in indirect modes call the same
Spawn/Update kernel and use GPU-resident particle state. Runtime
parameter changes without `affectExisting` apply only to later
births; enabling it updates already tracked particles through the
host's bounded CPU slot map and schedules one 32-byte-per-slot
buffer refresh outside the render pass. **GPU particle positions
are never read back to the CPU.** GPU device-loss behavior
continues to report non-seamless CPU fallback.

Use `examples/spectrafx/gpu_vm_lift_v2.vfx.json` for an authored
single-burst, age-dependent lift/fade example whose named `lift`
value is passed to the VM. The editor and `fx_graph_validate/get/list`
report `metalVmEligible` and `metalVmReason` separately from the
original billboard-only GPU verdict. These are **source capability
diagnostics only**; editor previews continue to use the mature CPU
runtime.

**Verification:** tests were postponed by the user. This GPU VM
and its shaders have **NOT** been compiled/run on Apple Silicon,
Linux, or other platforms in this stage. No Metal image output,
CPU/GPU numeric parity or performance evidence is available.
Do not enable `gpuProgram` by default before real-device tests.

## GPU VM custom scalar attributes (Stage 26 — functional source, tests deferred)

The optional Metal `ComputeParticles` GPU VM now supports **eight
persistent per-particle custom floats** (`attribute0` through
`attribute7`) as both expression inputs and writable Spawn/Update
targets, matching the corresponding subset of the mature CPU
`SpectraParticleProgram`. This extends the prior Stage-22 interpreter;
it is **not** a new renderer, GPU-only free-list allocator or an
automatic production switch for native FPS/RTS clients.

- The fixed `ParticleState` stays **64 bytes**; the billboard
  `Vertex` stays **40 bytes**. GPU attributes live in a **separate,
  32-byte-per-physical-particle storage buffer**, bound at Metal
  compute slot **11**, with two `float4` values.
- On the GPU Spawn dispatch, each reused physical slot receives a
  fresh zero-initialized attribute record; the Spawn VM may write
  its registers before the result is stored. Every later Update
  dispatch reads the same slot's values, executes its bounded
  expression statements sequentially, and writes them back.
- The existing Stage-22 GPU payload enum ABI remains **version 1**.
  Fields 16–23 are `attribute0..7`; fields 24–31 remain
  `param0..7`. The CPU capability gate permits these fields for
  the integrated Metal kernel. All other unsupported fields,
  instructions and non-Spawn/Update stages keep the original
  full-effect CPU fallback.
- Initialization, `clear()` and `shutdown()` allocate/reset/release
  this additional buffer. `ComputeStats::gpuBytes` now accounts for
  its full capacity. No per-frame GPU-to-CPU attribute readback
  is required.

Authoring example:
`examples/spectrafx/gpu_vm_custom_attributes_v2.vfx.json`.
It captures a named instance `intensity` into `attribute0` on
Spawn, then reads the stored attribute on Update to drive
`tint.a` and `size`. With `GraphRuntimeOptions::gpuProgram=true`,
this program fits the **restricted additive billboard GPU VM**
schema, subject to actual device and shader capability. The
same graph also executes under the established CPU VM.
Actual numerical/image parity has **not** been verified.

**Important separation:** standalone
`gpu_program::MetalVmPass` is a distinct experimental compute
utility with a different shader. It does **not** yet have a custom
attribute buffer. Its initializer explicitly rejects payloads
reading or writing fields 16–23, rather than silently producing
incorrect values. Only the integrated `ComputeParticles` kernel
has the new register storage.

The Stage-21 CPU-only `graphInputs` shader material mapping is
**not yet implemented in the GPU sprite renderer**. GPU custom
attributes can currently influence visible GPU output indirectly
through supported VM writes to `tint`, `size`, position or
velocity; arbitrary `shaderGraph` remains CPU-only. Event-stage
VM programs (collision, trigger, death), custom rotations, subemitters,
mesh/ribbon outputs, complex material graphs, alpha sorting and
Vulkan/GLES/WebGL GPU execution remain outside this GPU subset.

**Verification:** by explicit user instruction, functionality is
being implemented before testing. This stage has **NOT RUN**
CMake, CTest, MSL compilation, real Apple GPU rendering or
performance parity checks. The commits establish source changes,
not confirmed production behavior.

## Validation checklist

```bash
cmake -S . -B build/vfx-test -DAIVERSE_BUILD_TESTS=ON
cmake --build build/vfx-test --target aiverse_vfx_graph_tests aiverse_vfx_ecs_tests aiverse_vfx_assets_tests aiverse_vfx_ops_tests aiverse_vfx_gpu_tests aiverse_vfx_graph_runtime_tests aiverse_vfx_shared_batch_tests aiverse_vfx_cue_scheduler_tests -j4
ctest --test-dir build/vfx-test --output-on-failure -L SpectraFX
python3 tools/skills/catalog.py check
```

The current tests focus on graph compilation, asset caching/validation and read-only cross-genre ECS presentation. Visual regression screenshots, Android/Metal GPU performance, editor/MCP graph UX and FPS/RTS production event parity are still mandatory gates for a full Niagara-like release.

## Planned work

1. Validate current graph CRUD and basic visual node canvas, then add typed module inspector, scalable graph execution, richer dataflow and editor usability features.
2. Validate the Metal GPU compute/draw prototype on an actual device; implement GPU compaction, culling, sorting, richer renderers and graph execution with CPU fallback for GLES3/mobile/WebGL.
3. Rich graph dataflow, event handlers, function modules, simulation stages, user parameters, renderer plugins, soft particles, depth collisions, advanced quality/scalability.
4. Migration of existing RTS/FPS presentation adapters into a single SpectraFX path, native project sample games, performance and platform QA.

**Mandatory for agents:** after each bounded stage, append a separate handoff commit containing code SHA, affected files, API/serialization contracts, tests actually run, limitations and exact next task. See `AGENTS.md`.


## Stage 24 — generation-safe GPU VM particle parameter snapshots (experimental; tests deferred)

The public `engine/fx/SpectraGpuSourceParameters.h` provides a renderer-only
`gpu_program::SourceParameters` registry. `create(initial[8])` returns a
`SourceId {slot,generation}`; `spawn(physicalParticleSlot,id)` snapshots
all eight values at birth. `set(id,param,value,affectExisting)` changes
future births only by default, or also modifies already-born particles carrying
that exact source generation. `retire(id)` invalidates the source and preserves
existing particle snapshot values until explicit `kill(physicalSlot)`.
Generations never wrap to zero; exhausted slots cannot accidentally validate
stale handles. The table is presentation-only, not serialized into gameplay ECS.

`gpu_program::MetalVmPass::dispatchSources(states,count,stage,dt,registry)`
now uploads a 48-byte per-slot `ParticleParams` array and reads its individual
float8 values in the experimental Metal interpreter; legacy `dispatch(...,values)`
continues supplying one uniform parameter set. GPU particles retain the existing
64-byte state ABI. All physical slots must match the host's source registry,
and per-dispatch GPU buffer sizes are bounded. A separate host is responsible
for spawning/killing physical GPU slots in sync with the CPU mirror, and for
correct Spawn/Update scheduling.

**Not production integrated:** `GraphRuntime` and `ComputeParticles` remain
unchanged; no user graph automatically executes through the Metal VM. This pass
has no asynchronous GPU diagnostic readback or live-particle migration on device
failure and currently uploads the active prefix of snapshots each dispatch.
Do not claim GPU/CPU parity, performance or error-free execution. No native
compile, Metal device test or CTest was performed, in accordance with the user's
instruction to defer testing until feature work is complete.


## Stage 25 — GPU VM physical slot lifecycle and incremental snapshot upload (experimental)

The public `gpu_program::ParticleSlots` pairs a renderer-owned physical slot
with a generation-safe `ParticleId` and `SourceParameters` snapshot. The
host calls `acquire(sourceId)` before uploading a GPU spawn seed,
`markSpawnSubmitted(particleId)` only after successful Spawn VM submission,
and `release(particleId)` **only after GPU work using the old physical slot
has completed**. `needsSpawn`, `alive`, `physicalPrefix` and `activeCount`
assist generic native hosts without an FPS/RTS coupling. The adapter does not
schedule rendering or advance game simulation.

Stage 25 also marks newborn snapshots with `reserved=1`; the experimental
Metal kernel executes the Spawn VM only on these pending snapshots and skips
older particles for that stage. The host acknowledges submission to clear the
pending flag. `Update` remains applicable to all active particles. This
corrects the previous risk of repeated Spawn execution on all live particles.

`MetalVmPass::dispatchSources` caches its own previous per-particle values and
uploads only changed contiguous ranges, instead of uploading the entire active
prefix every call. On Metal, the RHI enqueues storage-buffer blits in command
order before a compute encoder. `readErrors(uint32_t&)` provides a strictly
optional RHI diagnostic readback; a failed read is **not** equivalent to zero
VM errors. Readbacks must be done between completed frames, not in an active
compute/render encoder. GPU error handling/fallback is not yet automatic.

**Boundary:** there is still no automatic `GraphRuntime` / `ComputeParticles`
VM integration, no fully GPU-owned multi-source allocator, no proven shader
compilation, no GPU/CPU numerical parity and no lossless device-failure migration.
Tests and native builds remain deferred by user instruction.


## Stage 27 — integrated Metal GPU VM error diagnostics and honest fallback accounting

The integrated Metal particle kernel (`SpectraGpuShaders.h`, *not* the
standalone experimental Stage 23 kernel) now counts failed VM statements,
invalid arithmetic and nonfinite assignment results into a dedicated atomic
storage-buffer counter. The counter is reset before a nonempty compute frame
and bound as Metal compute buffer slot 12. A failed VM statement continues to
be skipped, as the existing CPU VM semantics require; diagnostics do not enable
unsupported GPU graph operations.

`ComputeParticles::readGpuVmErrors(uint32_t&)` and
`GraphRuntime::readGpuVmErrors(uint32_t&)` expose an opt-in, blocking,
**between-completed-frames** diagnostic. A `false` result indicates
unsupported/unavailable readback, never a count of zero.
`ComputeStats::gpuVmErrorsKnown` is false until a successful read and is
invalidated on every new GPU frame or CPU fallback. Do not call this from an
active render/compute encoder. The Metal RHI's `readBuffer` explicitly refuses
an active command buffer and waits for a separate readback submission; only use
for diagnostics, not every game frame.

On unhealthy GPU, `RequireGpu` continues to reject fallback. In permissive
modes, the existing `switchToCpu` creates a new CPU emitter, **not** a live
state migration. The `lostOnFallback` count now includes pending GPU slot
allocations rather than only last frame's drawn particles. This change
improves accounting, but GPU-resident particle positions, attributes,
per-particle ages and remaining lifetimes are still lost. A recovery policy
requiring lossless migration must retain or read back a complete checkpoint
and reconstruct CPU state. **No such migration exists yet.**

Status: source code committed; native C++ build, Metal shader compile,
CTest, editor UI and GPU fault-injection tests NOT RUN. Do not claim that
this is a production-grade recovery implementation.


## Stage 28 — diagnostic Metal GPU checkpoints + asynchronous VM error requests

`ComputeParticles::captureGpuCheckpoint(GpuCheckpoint&)` (and the graph-runtime
wrapper) reads all **physical GPU slots**, including the 64-byte position/age,
velocity/lifetime, tint and size state, the eight 32-bit VM instance parameters
and eight VM custom attributes. It also captures the CPU-side remaining-life
mirror and completed frame serial. Each exported `GpuCheckpointParticle`
occupies 128 bytes. A checkpoint is marked `complete` only after all three
storage-buffer reads succeed; invalid, unavailable, active-frame or pending
birth state is rejected. Calls are blocking, strictly between completed Metal
frames: never run one every interactive frame. The Metal RHI's synchronous
`readBuffer` uses a separate copy command and waits for completion.

`ComputeParticles::requestGpuVmErrors(callback)` uses RHI
`readBufferAsync` to return `callback(bool success,uint32_t count)` on a
subsequent frame. Only one pending diagnostic request is accepted at a time.
The lifetime token cancels callbacks if GPU resources have been destroyed;
frame-revision checking prevents old error results from overwriting stats for
a newer frame. No callback is promised if the VM runtime is destroyed before
its readback returns. `GraphRuntime` forwards both APIs.

**Important:** this is a GPU state *export* and a basis for future recovery,
**not** lossless GPU→CPU fallback. The existing CPU `ParticleSystem` does not
provide an import API for live particles with their custom attributes and
instance state. If the GPU becomes unhealthy, reading the now-lost buffers
cannot be assumed possible. Stage 29 must implement transactional CPU import
and periodic captured checkpoints with a safe resource/fence policy before
automatic recovery is enabled. `lostOnFallback` continues to report lost
particles honestly. `RequireGpu` still forbids silent CPU fallback.

No compiler, CMake, CTest, Metal execution, fault-injection test or CPU/GPU
parity test has been performed for Stage 28; source is unverified.


## Stage 29 — transactional CPU checkpoint import and guarded GPU-failure recovery

`fx::ParticleSystem::importLiveParticles(effect, particles)` appends already-live
particles to a compatible **single-emitter** effect without replaying Spawn or
creating an emitting instance. It validates the entire batch (finite position,
velocity, color, age, lifetime, size, eight VM params/attributes), existing
CPU capacity and supported emitter restrictions before altering the CPU pool.
Each imported particle keeps the position, velocity, age, remaining lifetime
encoded as age/total lifetime, size, tint and custom registers. It is tagged
with no emitting owner, participates in normal CPU Update and renderer batching,
and cannot start new emitter bursts. Unsupported complex ribbons, collision,
trigger, mesh and local-space effects are rejected rather than partly restored.

After a successful blocking `ComputeParticles::captureGpuCheckpoint`, the
runtime retains the complete export plus its presentation-frame revision.
When `switchToCpu` occurs after device failure, it attempts recovery **only**
when the checkpoint matches the last submitted GPU frame, has the full physical
slot count, and there are no unsent births. Imported particles are counted in
`ComputeStats::recoveredOnFallback`; remaining discarded allocations are
reported in `lostOnFallback`. On stale/missing/invalid snapshots the existing
CPU fallback still starts clean and reports losses. `RequireGpu` continues
forbidding an implicit CPU switch.

**Recovery is opt-in by capture timing:** checkpoints are not collected
automatically because the available synchronous Metal RHI readback blocks
until the GPU finishes. Call checkpoint capture only between completed frames
at deliberate safe intervals. An unhealthy GPU cannot be relied on to produce
new snapshots. Exact CPU/GPU numerical continuation is not proven: renderer
and simulation algorithms differ, imported particles have no original source
instance, and a formerly live effect may stop emitting. The term *lossless*
is not warranted until parity, all emitter categories and fault injection
are verified. No native compiler, CTest, Metal test or GUI smoke was run for
this stage; code remains unverified.
