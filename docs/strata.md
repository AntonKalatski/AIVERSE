# Strata — AIVERSE virtual geometry

Status (2026-11-19): **experimental CPU staging and per-pass planning, not GPU draw submission**.
Working branch: `feature/strata-virtual-geometry`.
Agent resume log: [strata-virtual-geometry handoff](agent-handoffs/strata-virtual-geometry.md).

## Scope and ownership
Strata is a genre-independent **presentation** system for rigid, opaque geometry.
It must not own ECS worlds, physics/terrain simulation, game logic or materials.
It does not replace `GeometryScene`, existing `GeometryCluster`, AutoLOD,
`GlobalGeometryPool`, Metal batching, or the existing RHI.
Strata is intended to become an optional geometry representation consumed by
the shared renderer. The current implementation has no GPU rendering path.

## Current public contract
- `engine/render/StrataGeometry.h`: `ClusterNode`, `SelectionInput`,
  `SelectionOutput`, `validate()`, CPU reference `select()`.
- `engine/render/StrataBuilder.h`: `BuildInput`, `BuildOptions`,
  `Meshlet`, `ClusterBounds`, `Asset`, `build()`.
- `engine/render/StrataModelAdapter.h`: `ModelAsset`,
  `ModelPrimitiveAsset`, `ModelPrimitiveSkip`, `buildModelAsset()`.
  This is a read-only, opt-in adapter for CPU `Model` assets, not a renderer hook.
- Link `aiverse_strata_builder` or `aiverse_strata_model_adapter` in project
  CMake when `AIVERSE_BUILD_STRATA=ON` (ON by default). Both reuse the pinned
  `aiverse_meshoptimizer_lod` dependency. There is **no render integration**.
- `build()` accepts **one material partition per invocation**; vertex
  positions begin with three floats and may be interleaved. Source indices
  and positions are borrowed during the call only. On failure, output stays
  unchanged. Output is a CPU-owned asset independent of gfx::Device.
- `Meshlet` is a **leaf** with compact local uint8 triangle indices and
  a map to original global vertex indices. Defaults: max 64 vertices, max
  124 triangles. Original global indices are retained for indexed fallback.
- `ClusterNode` is a **drawable group** in the current schema; interior nodes
  are not guaranteed to fit a single hardware meshlet. `firstIndex` and
  `indexCount` reference the asset's flat index array. Children are
  contiguous and listed after parents (breadth-first reordering).
  `Asset::bounds` aligns one-to-one with `Asset::nodes`.
- Parent clusters use `meshopt_simplify` (position-only inputs) or
  `meshopt_simplifyWithAttributes` (optional 1..32 weighted float attributes),
  with `meshopt_SimplifyLockBorder | meshopt_SimplifyErrorAbsolute`.
  The parent's geometric error is conservatively accumulated from its
  children. A group with under 10% reduction stays unmerged; multiple roots
  are permitted. Max levels, target ratio and relative error are configurable.
  Error in `ClusterNode` is in object units; the caller must project it to
  pixels using the correct pass and transform.
- `select()` requires **all roots resident**. A missing child prevents
  refinement of its parent; budget pressure retains the parent.
  The caller supplies a valid projected error for every node. The implementation
  is a CPU reference selector, not GPU culling or residency.
- `buildModelAsset()` projects 15 attribute components per vertex from `Model`
  (normal xyz, UV0, tangent xyzw, vertex RGBA, packed lightmap UV1), with
  weighted seam-aware simplification. It does not mutate source Model or LOD0.
  It skips mutable nodes, skinned/morphed, already uploaded, meshes with existing
  authored/generated LODs, material variants and alpha/custom-shader materials.
  It returns explicit skip reasons and fails transactionally on invalid eligible
  geometry. The adapter is not hooked up to GeometryScene draw calls yet.
- This is still experimental: no verified cracks/material parity, GPU streaming
  or per-pass culling. Do not automatically route displaced surfaces here.
  Importers should preserve existing meshes and exact LOD0 as a fallback.

## Stage 4 — optional CPU staging in GeometryScene

`GeometryScene` remains the source-of-truth for scene instances, model
ownership, existing HLOD, visibility and draw submission. New APIs:

```cpp
render::GeometryScene scene;
scene.setStrataStagingEnabled(true);  // Before load/loadPrepared, default OFF
scene.loadPrepared(device, sceneJson, std::move(models));
// Scene draws through the existing Renderer3D exactly as before.
const auto& counters = scene.stats(); // strataSources, strataNodes,
// strataMeshlets, strataIndexBytes, strataSkippedSources,
// strataSkippedPrimitives, strataFailures
const auto* cpuAsset = scene.strataAsset("models/stone.glb");
// cpuAsset is nullptr when unavailable or the key is not staged.
scene.shutdown(device); // invalidates cpuAsset and all staging handles
```

Staging is compiled only when `AIVERSE_BUILD_STRATA=ON`, and is inert unless
`setStrataStagingEnabled(true)` is called before loading a scene.
`strataStagingSupported()` reports compile-time availability. The method
returns false on builds where Strata was disabled. Scene staging is configured
by **native API**, not via scene JSON, so no existing saved scene is affected.
There are no additional draw calls and no GPU-resident Strata buffers.

The CPU `SceneStaging` object is in
`engine/render/StrataModelAdapter.h` and can also be used by tools without
creating a scene or renderer. It stores at most one copy per unique source key.
It preserves the source `Model`, reports skip/failure diagnostics and
has configurable default caps of 250k vertices and 750k indices per source.
These are **per-source preparation bounds**, not global Strata/VRAM limits.
All staged data is released with `GeometryScene::shutdown` and re-created
on subsequent loads. Source proxies and generated HLOD proxies are deliberately
excluded. Models ineligible for Strata continue through the existing renderer
unchanged. A failed staging operation must never fail the scene's legacy upload.

The `strataIndexBytes` counter measures only auxiliary Strata flat index
storage (not total Strata CPU heap usage); do not report it as resident VRAM.
All counters are diagnostics and are not part of a public save/network schema.

**Not yet active:** GPU scene buffers, meshlet culling, GPU residency,
GPU-encoded indirect draws, software rasterization, per-pass Strata selection,
actual Strata render submission, or terrain/foliage integration.

## Stage 5 — shared GPU batching foundation (pinned integration)

A genuine two-parent merge commit `0913b82088b63913e1313557de16d85e7c79b89f`
integrates the pinned `perf/incremental-static-model-cache` head
`1043a9de747fe89cb42de899c15c6d2f4db8e0e1` into the Strata feature.
The batching branch/PR #34 remains separately owned by its agent.

**Reused existing systems** (do not recreate them in Strata):
- `GlobalGeometryPool`, eligibility checks and geometry revisions: shared GPU
  source buffers and safe invalidation. Strata's future resident pages must
  coordinate allocator lifetime and buffer handles with this pool.
- `StaticModelCache`, `StaticModelCachePolicy.h` and persistent render
  packets: existing per-draw transform/primitive reuse; never add a second
  static transform cache to Strata.
- `StaticCrossModelBatchPolicy` and CPU MDI/GPU ICB paths:
  `Renderer3D` remains the single owner of material grouping/indirect
  submissions. A future Strata culler supplies selected cluster ranges to
  the same render submission path, with source selection mutually exclusive.
- `EcsStaticModelRender.h`: the common ECS-to-presentation adapter. No Strata
  ECS components are necessary to render static instances.

**Current behavior remains unchanged:** `GeometryScene` Strata staging is
OFF unless explicitly enabled; CPU/GPU batching experimental flags retain
their existing defaults. No Strata GPU culling, selected-cluster draw,
streaming or residency path has been activated or validated. PR #35 is a
stacked draft based on the batching work until PR #34 is merged or replaced.
The batching agent may push new commits after the pinned snapshot; explicitly
review and merge those changes before relying on later APIs.

Do not conflate the already existing `GeometryCluster`/static pooling path
with Strata's new `ClusterNode` hierarchy. Strata staging currently stores
auxiliary CPU data; it is NOT a substitute for pooled GPU pages and must not
double-submit scene primitives.

## Stage 6 — per-pass Strata selection contract

`engine/render/StrataView.h/.cpp` introduces a CPU-only per-pass reference
selector. The selected geometry uses **Asset-local index ranges**; ranges
are not GPU draw commands and MUST NOT be passed to
`gfx::Device::drawIndexedIndirect` without first arranging the auxiliary
index stream through the shared geometry residency/allocator and correct
material/instance grouping.

```cpp
#include "engine/render/StrataView.h"
render::strata::ViewConfig pass{};
pass.objectToClip = cameraViewProj * instanceWorld * bindNodeWorld;
pass.width = renderTargetWidth;
pass.height = renderTargetHeight;
pass.clipDepthZeroToOne = device.info().clipDepthZeroToOne;
pass.errorThresholdPixels = 1.0f;
pass.maxSelectedNodes = 65536;
render::strata::ViewPlan selected;
std::string error;
bool ok = scene.planStrataView(
    "models/stone.glb", /* mesh */ 0, /* primitive */ 0,
    pass, selected, &error);
// Existing Renderer3D remains the sole draw issuer regardless of 'ok'.
```

`planStrataView` resolves the already-staged source once and calls the
independent `strata::planView(asset, pass, result, error)`.
The caller must supply the correct mesh-to-clip matrix **for each node and
pass**. The API does not silently reuse the main view for shadows,
reflections, portals or stereo. `strata::planView` validates bounds,
index ranges, matrix and viewport; projects the existing per-cluster
geometric error with `geometryErrorPixels`, runs the resident-parent
fallback selector, then conservatively rejects offscreen selected nodes
through `geometryBoxVisible`. Errors leave prior output unchanged.
The resulting ranges identify node ID, firstIndex, indexCount and material
group in their individual `strata::Asset` index stream; stats include
tested/cut nodes, triangles, residency misses and budget pressure.

The planner is packaged as `aiverse_strata_view`, using the existing
`aiverse_models` geometry helpers. The CTest target is
`aiverse_strata_view_tests` / `strata_view`. Neither test execution nor
Metal/Vulkan validation has been claimed. The implementation remains a
CPU reference path to compare eventual compute culling against; there is
**no GPU scene buffer, RHI command upload or Strata draw path** yet.

## Stage 7A — experimental GPU cluster metadata (not geometry residency)

`engine/render/StrataGpuScene.h/.cpp` supplies a versioned, fixed-layout
storage buffer for CPU-prepared cluster records, primitive mappings and
hierarchy root indices. It is optional, off by default, and requires the
source `Model` to have been uploaded into **the existing**
`GlobalGeometryPool`. It does not create or own geometry VB/IB pages.

The CPU packer `strata::packGpuMetadata(ModelAsset, bytes, error)` is pure,
transactional and validates unique primitive keys, all cluster hierarchies,
bounds, materials, source-local index ranges and u32 offsets. Layout version 1:

| GPU metadata record | Size | Purpose |
| --- | ---: | --- |
| `GpuMetadataHeader` | 32 B | Version, counts, byte offsets |
| `GpuPrimitive` | 32 B | Model mesh/primitive IDs, global node/root spans |
| `GpuNode` | 64 B | AABB, global child ID, **source-local** index span, error/flags |
| Root IDs | 4 B each | Global root IDs, padded to 16-byte total length |

The compact arrays are stored in one RHI `BufferType::Storage` buffer
per eligible source (not per instance). Default bounds: 1 MiB per source,
8 MiB per scene for metadata. Experimental upload is restricted to Metal
and Vulkan devices reporting compute support and a matching existing
`Model::geometryPoolPage`, shared VB/IB and nonempty pooled extents.
Noneligible sources do not abort scene loading and continue normal rendering.
`Model::geometryRevision`, source identity and source GPU handles are checked
when retrieving metadata; stale source records are never silently reused.

Native integration is explicit:
```cpp
render::GeometryScene scene;
scene.setStrataStagingEnabled(true);
scene.setStrataGpuMetadataEnabled(true); // both OFF by default
scene.loadPrepared(dev, sceneJson, std::move(models));
auto gpuBuffer = scene.strataGpuMetadataBuffer("models/stone.glb");
auto stats = scene.stats();
// strataGpuMetadataSources, strataGpuMetadataDeclined,
// strataGpuMetadataFailures, strataGpuMetadataBytes
scene.shutdown(dev); // call before destroying gfx::Device
```

The metadata buffer is read-only for future compute code; it is **not** a
GPU scene draw list and no compute shader consumes it yet. `GpuNode::resident`
is a snapshot of the CPU cluster flag, NOT proof of GPU-resident Strata
geometry. Most importantly, `GpuNode::sourceFirstIndex` references the
auxiliary CPU `strata::Asset::indices`, not the `Model::ib` index stream or
any pooled GPU allocation. Until the shared pool gains safe, versioned
**auxiliary Strata index-stream allocations**, **do not emit** indexed or
indirect draws from these metadata offsets.

Existing `GeometryCluster` preparation can make `Model::geometry` non-null
and intentionally exclude that model from the current rigid shared geometry
pool. Such a source will decline experimental GPU metadata staging. Resolve
the pool's admission/ownership policy explicitly; do not bypass its safety
guards. This stage leaves `GlobalGeometryPool.cpp`, active `Renderer3D`
batching, all render passes, ECS and saved scene formats unmodified.

`aiverse_strata_gpu_scene` builds the metadata and shared-page sidecar layer; CTest
`strata_gpu_scene` checks CPU binary layout, multi-primitive global
offsets, source flags, malformed ranges and transactional failure.
**Native compilation and GPU/CTest execution have not been verified**.

## Stage 7B — shared-page auxiliary Strata index residency

**Status: code committed; native/GPU validation NOT RUN.** All data remains
experimental and the draw path remains the existing `Renderer3D`.

`GlobalGeometryPool.h/.cpp` now offers the following opt-in source-attached
API. It does not introduce a second GPU geometry allocator:

```cpp
render::GeometryPoolIndexSpan lease;
if (render::uploadAuxiliaryGeometryIndices(
        device, sourceModel, strataIndices.data(), strataIndices.size(), lease)) {
    // lease.buffer == sourceModel.ib == sourceModel.vb (same page).
    // lease.firstIndex is an absolute uint32 GPU index element offset.
    // lease.indexCount counts only uploaded indices in the sidecar stream.
    // Indices remain relative to sourceModel.gpuBaseVertex.
    assert(render::auxiliaryGeometryIndicesValidFor(lease, sourceModel));
}
// Explicitly retire before destroying device / source GPU objects:
render::releaseAuxiliaryGeometryIndices(device, lease);
```

The function accepts only models **already uploaded into the existing
`GlobalGeometryPool`**, valid shared VB/IB handles, a complete checked
shared-buffer write, and source-relative indices that fit the reserved
vertex slice. It never creates a new page for Strata indices. If the page's
remaining vertex-to-index gap is too small, the caller keeps the existing
legacy renderer. No change is committed to the allocator if the RHI write
fails; all previous model and sidecar byte ranges remain reserved.

Index-only allocations use the same `GeometryPoolPage::indexBegin` frontier
as ordinary model index allocations. A sidecar lease retains its GPU page
through `shared_ptr`, preventing page destruction even if the source
`Model` releases its own lease first. The source revision, base vertex,
first index, shared page and GPU buffer handles must all match for a binding
to be usable. `geometryPoolStats` now reports separate *actual*
`liveModels` and live/retired/reusable auxiliary index counters.

Released auxiliary spans may be recycled only when the existing
`AIVERSE_GEOMETRY_POOL_RECYCLE=1` opt-in is enabled **and** the backend
reports a nonzero, healthy `completedFrameSerial()` at or past the recorded
release `frameSerial()`. Otherwise, storage remains conservatively
quarantined/stranded until the entire page is destroyed. This is intentional:
no CPU allocator is allowed to overwrite an in-flight GPU read. There is
no extra Strata-specific fencing or background worker.

`GpuMetadataScene::stage()` now concatenates each eligible primitive's
`strata::Asset::indices` and uploads that stream to its source model's
shared page (default max 1 MiB per source and 8 MiB per scene). Once uploaded,
it publishes a **version 2** cluster metadata buffer:
`GpuPrimitive::gpuFirstIndex` contains the *absolute GPU sidecar index
base for that primitive*; `GpuNode::sourceFirstIndex` remains local to the
primitive, so the future draw offset is their sum. The original, pure CPU
`packGpuMetadata()` intentionally emits **version 1** with
`gpuFirstIndex=invalidNode`; that format is **not GPU drawable**.

```cpp
scene.setStrataStagingEnabled(true);
scene.setStrataGpuMetadataEnabled(true); // both default OFF
scene.loadPrepared(device, json, std::move(models));
GeometryScene::StrataIndexBinding binding;
if (scene.strataGpuIndexBinding("models/stone.glb", binding)) {
    // binding.buffer: same pool as the source Model.
    // binding.firstIndex: base of the entire Strata sidecar.
    // binding.baseVertex: original pooled source vertex base.
    // No Strata draw/dispatch is issued by this API.
}
```

The metadata and sidecar allocations share one source lifetime and are
released **before** `GeometryScene` frees its source models. Source geometry
remains immutable; existing materials, HLOD, batching and shadows remain
authoritative. No GPU compute culling, Hi-Z, indirect command generation,
Strata render pass, texture streaming or actual measured FPS benefit yet.

`tests/render/strata_gpu_scene.cpp` extends the CPU packing fixture with a
recording RHI and exercises same-page reservation, index byte identity,
neighboring model protection, upload failure rollback, revision invalidation,
source-vs-sidecar lifetime and release behavior. These scenarios are **not
claimed to pass until run**. The next stage must validate deferred recycling
under real backend serials, build all focused CTests, add OFF/ON draw parity,
then implement a capability-gated single render submission owner.

## Stage 8 — diagnostic Metal GPU cluster culling

**Experimental, no render integration or visual parity claim.** The
`aiverse_strata_gpu_culling` library adds a Metal compute kernel that traverses
the version-2 Strata GPU metadata hierarchy for each static source instance
and applies the same projected-error and conservative frustum policies as
the CPU `strata::planView` reference.

Inputs are a staged metadata v2 storage buffer, a bounded per-source list of
`GpuSceneInstance` records (object-to-world matrices including each Model
node bind transform, GPU primitive index, enabled flag, diagnostic token),
and `GpuPassConfig` with a separate **view-projection** matrix. Do not pass
a pre-multiplied objectToClip matrix to the GPU and multiply the instance
twice. Use `strata::cpuReferenceView(pass, instance)` to obtain the matching
CPU per-instance `ViewConfig`.

```cpp
render::GeometryScene scene;
scene.setStrataStagingEnabled(true);
scene.setStrataGpuMetadataEnabled(true);
scene.setStrataGpuCullingEnabled(true); // ALL opt-in, default OFF
scene.loadPrepared(device, sceneJson, std::move(models));

// Within the graphics frame and OUTSIDE any render/compute encoder:
render::strata::GpuPassConfig pass;
pass.viewProjection = mainCameraViewProj;
pass.width = viewportWidth;
pass.height = viewportHeight;
pass.clipDepthZeroToOne = device.info().clipDepthZeroToOne;
pass.errorThresholdPixels = 1.f;
pass.maxOutputClusters = 65536;
render::strata::GpuSelectionSubmission result;
bool encoded = scene.dispatchStrataGpuView(
    device, "models/stone.glb", pass, device.frameSerial(), result);
// result.ranges / result.counters are GPU buffers; NOT a draw command list.
```

`GeometryScene::collectStrataGpuInstances(sourceKey, output)` constructs
the static scene input from its **existing** instance and node transforms;
it does not copy ECS state into a second world. A token is a local
diagnostic identifier, not a persistent Entity ID. More than 4096 staged
primitive instances is rejected conservatively. The source must have valid
Strata v2 metadata backed by the same `GlobalGeometryPool` index lease.

`GpuClusterCulling::dispatch()` uses Metal `gfx::Device::createCompute()`
and slots 0/1/2/3 for metadata, instances, selected clusters and atomic
counters, with pass uniform bytes on slot 8. Each GPU thread walks one
primitive instance's cluster hierarchy, refining only when all child
clusters are resident. The 128-item local traversal stack and bounded
output flag malformed/excessive work; **overflow/invalid results must never
be used for drawing**. The shader emits `GpuSelectedCluster` entries with
instance token, absolute pooled GPU index offset, count, material ID,
global node ID and primitive ID. The atomic compaction order is undefined,
so eventual GPU/CPU comparisons must sort by stable instance/node keys.

This stage does NOT issue any `drawIndexed`, GPU-authored indexed indirect
commands, GPU ICBs, or readback in the normal render loop. The existing
`Renderer3D` remains sole draw owner and continues drawing all original
geometry, even when the diagnostic dispatch succeeds. No Hi-Z, occlusion
history, mesh shading, streaming or Vulkan compute path has been added.

Frame safety: three frame slots, up to 64 independent submissions per
frame slot, fresh storage buffers per same-frame submission. A recycled
slot is rejected unless the backend confirms its old frame completed
(`completedFrameSerial >= previousFrameSerial`); an absent/unknown
completion serial fails closed rather than overwriting in-flight data.
Call `GpuClusterCulling::shutdown(device)`, or
`GeometryScene::shutdown(device)` when scene-owned, before the device is
destroyed. Metal-only capability gate rejects other backends; the legacy
renderer is untouched.

Targeted tests:
`aiverse_strata_gpu_culling_tests` (`strata_gpu_culling`) uses recording
RHI assertions for CPU reference transform composition, disabled backends,
Metal compute dispatch setup, independent same-frame submissions, frame-slot
retirement and zero draw calls. This does **not** compile the shader through
Metal or confirm that GPU selections equal CPU selections. The focused
workflow `.github/workflows/strata-focused.yml` configures and runs all
five Strata CPU/mock contract targets on Linux and macOS. Treat queued,
cancelled or unavailable Actions as **NOT VERIFIED**.

Before enabling draws: native compile + focused tests, actual Metal shader
compilation, asynchronous GPU counter/result readback in a test harness,
CPU `planView` comparison over randomized projections, LOD/fallback
transitions, shadows, meshes/materials and multiple instances. Then prove
exclusive draw ownership and compare legacy-vs-Strata pixels on real GPU
before even considering using compacted ranges in an indirect path.

## Stage 8.2 — asynchronous readback, CPU oracle and real Metal parity runner

The owner ran the **previous Stage 8 code** on Mac Studio on 2026-11-19.
`aiverse_strata_gpu_culling_tests` built, and all five focused CTests
(`strata_builder`, `strata_model_adapter`, `strata_view`,
`strata_gpu_scene`, `strata_gpu_culling`) **passed (5/5, 1.19 s)**.
That evidence proves the prior native C++ tests, **not** actual Metal shader
execution or the new Stage 8.2 code.

Stage 8.2 adds a bounded, asynchronous diagnostics API:

```cpp
render::strata::GpuSelectionSubmission selected;
if (scene.dispatchStrataGpuView(device, sourceKey, pass,
                                device.frameSerial(), selected)) {
    scene.requestStrataGpuReadback(device, selected,
        [&](render::strata::GpuSelectionReadback readback) {
            // Device owner thread at a subsequent beginFrame.
            // Incomplete/overflow/invalid results may NEVER be drawn.
            if (!readback.valid()) return;
            // Verify against CPU planView before considering rendering.
        });
}
```

`GpuClusterCulling::requestReadback` submits two independent RHI
`Device::readBufferAsync` requests (GPU counters and selected ranges).
The existing Metal backend limits each request to 64 KiB, so requests
with `GpuSelectionSubmission::capacity > 2048` are rejected instead of
publishing an apparently complete prefix. Callbacks run later on the
**device owner thread**, not synchronously with the GPU dispatch.
The output is a `GpuSelectionReadback` containing counters and a status
(`Valid`, `Overflow`, `Invalid`, `ReadbackFailed`). It never allocates
new GPU geometry or submits draw commands. Do not retain pointers to an
unloaded `GeometryScene` in readback callbacks; manage observer lifetime
explicitly. On rejected readback scheduling, the callback reports failure.

`strata::compareGpuSelection(source, instances, pass, pooledFirstIndex,
readback, error)` is the CPU/GPU oracle. It independently projects each
instance via `cpuReferenceView()` and `planView()`, converts each local
range into absolute shared-pool index/node addresses, sorts CPU/GPU records
by their stable record fields, and requires exact equality. It rejects
malformed instances, duplicate/zero enabled tokens, output overflow and
invalid GPU status. GPU atomic ordering is explicitly not significant.
The pooledFirstIndex must be the original v2 sidecar binding base from the
same source/revision. This comparison is **diagnostic**, never an authority
for production draw submission.

A separate **real Metal GPU** executable was added at
`tests/render/strata_metal_parity.cpp`, gated by the CMake option
`AIVERSE_STRATA_METAL_GPU_TESTS=ON` (default OFF, macOS only).
It uses the real `platform::run` + CAMetalLayer, actual
`GlobalGeometryPool` source upload, Strata metadata v2, diagnostic compute
and asynchronous GPU readback. It executes five cases: fine LOD with
two instances (four leaves), coarse LOD (two parents), frustum rejection,
a disabled instance, and deliberate output overflow. It reports the
result of each GPU/CPU comparison on the console and exits nonzero on
failure. It enables only the old **CPU shared pool admission**
(`AIVERSE_METAL_CPU_MDI=1`) locally for the fixture; it does not submit
ICB or Strata draws.

Mac Studio commands:

```sh
cd ~/Projects/AIVERSE-strata-stage8
git status --short
git fetch origin feature/strata-virtual-geometry
git switch --detach origin/feature/strata-virtual-geometry
cmake -S . -B build/strata \
  -DAIVERSE_BUILD_TESTS=ON \
  -DAIVERSE_BUILD_STRATA=ON \
  -DAIVERSE_STRATA_METAL_GPU_TESTS=ON
cmake --build build/strata \
  --target aiverse_strata_gpu_culling_tests aiverse_strata_metal_parity \
  --parallel 8
ctest --test-dir build/strata \
  -R '^strata_(builder|model_adapter|view|gpu_scene|gpu_culling)
- **One scene/instance truth**: `GeometryScene` owns static render instances
  and HLOD; do not create separate Strata transforms or introduce gameplay
  entities solely for rendering.
- **One grouping authority**: indirect draw grouping/material binding stays
  in the existing renderer, with a future Strata adapter supplying visible
  cluster ranges. Never double-submit the same primitives.
- **GPU capability negotiation**: GLES3/WebGL2 should retain traditional
  indexing paths. Metal/Vulkan/D3D future paths must gate compute, indirect
  count, storage resources and synchronization independently.
- **Multi-world**: geometry assets can be shared by handle across worlds,
  while scene-resident instance tables and render budgets are per world/view.
- **Dynamic terrain**: TerraWeave owns chunk geometry and versions; Strata
  consumes immutable snapshots and schedules deferred release. PHYSIS owns
  simulation/events, not GPU buffers.
- **Pass separation**: main-camera, shadows, reflections and portals may
  require distinct culling/error metrics and shadow caster inclusion.

## Known shortcomings / next steps

1. Compile and execute focused tests on a native project checkout:
   ```sh
   cmake -S . -B build/strata -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_STRATA=ON
   cmake --build build/strata --target aiverse_strata_builder_tests aiverse_strata_model_adapter_tests aiverse_strata_view_tests aiverse_strata_gpu_scene_tests aiverse_strata_gpu_culling_tests -j 8
   ctest --test-dir build/strata -R '^strata_(builder|model_adapter|view|gpu_scene) --output-on-failure
   ```
2. Add a recording-device scene fixture comparing Strata staging OFF/ON,
   instance deduplication, material/LOD/shadow draw parity and unload/reload.
3. Validate silhouette and seam quality for different LOD depths and attribute
   discontinuities; parent simplification and locked borders alone do not
   guarantee crack-free transitions.
4. The existing `GlobalGeometryPool` now reserves auxiliary Strata index
   spans without creating a new page, and version-2 metadata maps primitive
   bases to their actual pooled GPU offsets. Verify on Mac Metal and Vulkan:
   shared-buffer writes, reupload/revision invalidation, in-flight retirement,
   surrounding rigid models, and all original fallback paths before enabling
   submission. Default staging remains OFF.
5. Stage 9 adds an explicit exclusive route/candidate command preflight.
   Execute the Stage 8.2 real Metal parity runner and the sixth route test,
   then randomized camera/LOD/material variants before accepting GPU output;
   previous mock/CPU tests do not establish raster parity.
6. Add mutually exclusive Renderer3D selected-range submission only after
   GPU/CPU selection AND screenshot parity. Preserve legacy fallback on all
   unsupported devices; then pursue Hi-Z, streaming and independent CPU/GPU
   benchmarks.

## Verification policy

Compiler, CTest, screenshots, GPU timing and benchmarks must be reported only
after **actual execution** with exact logs or CI URLs. Queued/cancelled runs
are not passing results.
 --output-on-failure
   ```
2. Add a recording-device scene fixture comparing Strata staging OFF/ON,
   instance deduplication, material/LOD/shadow draw parity and unload/reload.
3. Validate silhouette and seam quality for different LOD depths and attribute
   discontinuities; parent simplification and locked borders alone do not
   guarantee crack-free transitions.
4. Extend **existing** `GlobalGeometryPool` to safely allocate an auxiliary
   Strata index stream on an existing pooled page, with explicit scene/
   model revision checks, upload failure rollback, frame-fenced retirement
   and shared-buffer synchronization. Do not add a second geometry VB/IB pool.
   All current Strata metadata offsets remain source-local, not drawable.
5. Build a capability-gated GPU scene/cluster selection path using the
   CPU planner as a correctness oracle. Submit visible ranges through one
   existing `Renderer3D` batching/MDI owner, or fall back to normal models.
6. Benchmark CPU and GPU time separately, validate view-dependent shadows,
   reflection views and multi-world lifetime, then pursue streaming.

## Verification policy

Compiler, CTest, screenshots, GPU timing and benchmarks must be reported only
after **actual execution** with exact logs or CI URLs. Queued/cancelled runs
are not passing results.
 \
  --output-on-failure
build/strata/aiverse_strata_metal_parity
```

**Validation distinction:** The previous five CPU/mock CTests were
owner-confirmed PASS. The new Stage 8.2 changes, real MSL shader compile,
GPU dispatch, asynchronous readback, Metal GPU-vs-CPU selection parity and
image parity are **NOT RUN** as of this documentation entry. The real
runner is intentionally not an unattended CTest; it creates a macOS window.
Do not infer speedup or complete rendering from a valid diagnostic selection.

Remaining mandatory gate before enabling draw submission:
native Stage 8.2 tests, genuine Metal shader compilation, mismatched/overflow
handling, camera thresholds and geometry quality, explicit GPU/CPU parity
over randomized transformations and materials, per-pass shadows/reflections,
and a one-owner, non-double-submitted Renderer3D integration. Vulkan compute,
Hi-Z and streaming remain future work.

## Stage 9 — exclusive renderer routing preflight (no GPU draws)

After Stage 8.2, the batching PR #34 was merged into `main` at
`58e29ab052458fffcdebebffe27022b53adb9367`. Strata synchronizes
this `main` through a two-parent merge `a77cd830e8dee751658028786066111bc8e65f6a`
while preserving the independent batching changes and handoff.
Renderer3D, StaticModelCache, GlobalGeometryPool and CPU MDI/GPU ICB still
belong to the existing batching implementation.

`engine/render/StrataRenderRouting.h/.cpp` defines an explicit **candidate
preflight and exclusive source route decision** (not a live submission path).

```cpp
using namespace aiverse::render;
strata::RenderCandidate candidate;
strata::ViewConfig view; // pass object-to-clip, viewport & error budget
// Scene must have been staged with both CPU and pooled-GPU Strata enabled.
const bool prepared = scene.prepareStrataRenderCandidate(
    "models/stone.glb", 0, 0, strata::RenderPass::MainOpaque,
    view, candidate);

// Renderer3D owns final material/pipeline validation and the real uniform slot.
strata::RenderRouteGate gate;
gate.requested = true;
// DO NOT set proof flags speculatively: real Metal CPU/GPU and raster parity
// must be established first, for the applicable pass/material.
gate.sourceAndMaterialSafe = prepared;
gate.sourceResident = prepared;
gate.cpuGpuSelectionParityVerified = false; // unverified in production
gate.rasterParityVerified = false; // unverified in production
gate.rendererOwnsSubmission = false;
auto route = strata::chooseExclusiveRenderRoute(gate);
// route == Legacy; original Renderer3D draws stay authoritative.
```

The CPU preflight validates an exact source model, primitive and material,
checks a **fresh** `GpuMetadataScene::indexBinding` shared-pool sidecar
lease, live source/model GPU handles, geometry and material revisions,
no animation/skin/morph/vertex displacement/custom shader, no alpha
blending, existing model LOD, shadow batch, material batch or variant
ownership. It calls CPU `planView` with the correct pass projection and
converts selected primitive-local indices into absolute shared GPU
index offsets. For fully offscreen primitives it may return an empty
candidate; this is a valid pass-specific cull, **not** an instruction
to also draw the original primitive.

`encodeRendererCommands(liveModel, liveResidency, candidate, slot, gate,
commands)` converts the geometry ranges into the shared 20-byte
`gfx::DrawIndexedIndirectCommand` ABI **only if** every route gate is
explicitly satisfied. It independently rechecks the live material/geometry
revisions, source pointer identity, GPU buffers and *current* sidecar lease
(including bounds). The real `firstInstance` uniform slot must be
provided by Renderer3D; Strata never allocates a second transform table,
pipeline or material grouping authority. On failure it leaves output
untouched. This is still diagnostic command construction, NOT dispatch.

**Important:** merely setting every `RenderRouteGate` boolean does not
constitute genuine GPU or raster parity. The production renderer does not
currently call this encoder or suppress legacy draws. Do not wire in a
Strata-only branch until the Stage 8.2 real Metal runner and full OFF/ON
visual parity pass, including sun shadows, reflections, material/LOD
transitions and multiple instances. Never submit a Strata candidate in
addition to the corresponding `ModelDraw`.

A new `aiverse_strata_render_routing` static target and focused CTest
`strata_render_routing` cover the gate, pooled index translation,
per-pass parent/leaf choices, fully culled primitives, live source
mutation, revoked/invalid GPU leases and absence of actual raster/
indirect draws. Tests are not considered passing until executed.
The `.github/workflows/strata-focused.yml` workflow builds this sixth
contract suite on Linux/macOS.

## Stage 10 — actual ModelDraw dry-run in Renderer3D (no rendering change)

**Renderer3D now has a diagnostic preview call, not a Strata draw path.**
The Stage 9 source routing preflight alone was unaware of the actual
per-draw tint, pose, node transforms and independent renderer passes.
The new `StrataRendererPreview.h/.cpp` adds a stateless, testable
`strata::previewModelDraw` evaluator that accepts the existing
`ModelDraw`, per-source `ModelAsset` and GPU sidecar residency,
plus a `PreviewContext` containing main/sun view projections and
target dimensions. It never creates command buffers, calls
`drawIndexed`, changes ModelDraw or caches transform ownership.

`Renderer3D::previewStrataModel(draw, stagedAsset, gpuScene, sourceKey)`
runs the same evaluator with the **renderer’s real** current matrices,
sun shadow quality, viewport size and depth convention.
`Renderer3D::strataPreviewStats()` provides per-`beginScene` counts:
modelDraws, attempted, ready, rejected, selectedClusters, fullyCulled
and skippedUnsafeDraws. A 'ready' candidate only means the CPU
reference geometry and pooled indices passed preflight — it does
**not** mean GPU/CPU selection or image parity has passed.

`GeometryScene` owns the only current integration call:
```cpp
scene.setStrataStagingEnabled(true);
scene.setStrataGpuMetadataEnabled(true);
scene.setStrataRenderPreviewEnabled(true); // additional opt-in, default OFF
scene.loadPrepared(device, json, std::move(models));
// In an existing renderer frame:
scene.draw(renderer);
// Every source still goes through original Renderer3D::drawModel().
auto diagnostics = renderer.strataPreviewStats();
```

Only source models (not HLOD or surface proxy replacements) are
previewed, and only when the source is already GPU-resident in the
original `GlobalGeometryPool` and its Strata CPU data is staged.
No new ECS data, renderer material cache, GPU pool or alternate draw
path is introduced. The temporary sourceKey and pooled data are borrowed
only synchronously for this evaluation and never stored in Renderer3D
packets. Preview refuses animated pose/baked skinning, material and node
overrides, alpha/fade, model variants, reveal, water passes and
incompatible live shader/material/geometry. Each eligible source node
is evaluated separately with the proper per-node bind transform and
per-pass projection. Sun shadow selection is independent of the main
camera, and an empty successful selection counts as a fully culled
preview rather than an invitation to render a second source.

**No duplicate draw calls:** after an optional preview,
`GeometryScene::drawInstance` ALWAYS invokes the original
`renderer.drawModel(d)` exactly once. HLOD/proxy substitutions never
enter the dry-run. Stage 9 `RenderRouteGate` remains a proof contract
and is not connected to the live submission path, even if a caller
manually enables the preview flag.

`tests/render/strata_render_routing.cpp` additionally exercises
source ModelDraw with independent main/shadow views and a recording
RHI to verify correct preflight counts, opaque/alpha restrictions,
and **zero draw submissions**. The six focused Strata CTests are still
required on a native build; Stage 10 has not been executed by this
assistant. The user's earlier Stage 8 Mac Studio 5/5 CTest PASS remains
a confirmed **older** code snapshot, not Stage 10 verification.

## Stage 10 verified Metal selection (owner Mac Studio, 2026-11-19)

Native macOS focused suite: **6/6 CTest PASS**, 0 failed, including
`strata_gpu_culling` and `strata_render_routing`.
Owner also compiled and ran the actual `aiverse_strata_metal_parity`
executable on Mac Studio. The real GPU created the Strata Metal compute
pipeline, dispatched it, copied visible clusters asynchronously and
compared them to the CPU oracle. All five supplied fixture cases passed:
fine LOD (selected 4, examined 6), coarse LOD (2/2), frustum rejection
(0/6), disabled instance (2/3), and bounded-output overflow correctly
rejected. Final output: `[strata-metal] cases=5/5 status=PASSED`.

The tested checkout SHA was not included in the supplied terminal output.
These results **do not** yet establish screenshot parity of legacy and
Strata raster geometry; they do not enable Strata draws in Renderer3D.
Production still renders exactly the legacy model once, and the Stage 10
preview remains opt-in. The next required gate is real Metal raster/image
parity with the source geometry and sun shadow/reflective passes, with
no duplicate primitive submission and a conservative fallback.

A separate `AIVERSE-static-scene` full CTest run reported 125/127 PASS;
its two skill-registration failures were unrelated to this Strata
GPU fixture and should not be conflated with Strata's 6/6 focused tests.

## Stage 11 — real Metal raster A/B fixture, separate frames (not production)

`tests/render/strata_metal_raster_parity.cpp` is an **opt-in
macOS Metal raster parity probe** built by target
`aiverse_strata_metal_raster_parity` when
`AIVERSE_STRATA_METAL_GPU_TESTS=ON`. The target is purposefully not
an unattended CTest because it opens a real CAMetalLayer.

The fixture creates one simple rigid, opaque, UV-mapped quad and
stages a v2 Strata source whose parent uses the same original two
triangles as both resident children combined. It uploads the source
only once via the existing `GlobalGeometryPool`, then stages the
auxiliary Strata indices **inside the same pooled GPU buffer**.
Two test-only alias models borrow that exact vertex/index GPU buffer
with `gpuBaseVertex` from the original source and `gpuFirstIndex`
from the staged sidecar; neither is uploaded or freed separately.
The alias primitives reference the CPU `prepareRenderCandidate`
selected ranges, not a second vertex allocation.

Using the production `Renderer3D::drawModel`, each test frame
submits at most ONE of: nothing, Legacy source, Strata fine children,
or Strata coarse parent. A floor serves as a shadow receiver.
The normal renderer processes its main and sun shadow passes; no
model is drawn through a second competing raster pipeline.
Separate frames are captured with the existing Metal
`gfx::Device::captureNextFrame`. The test requires an empty-vs-Legacy
image difference (guards against black/blank images), then byte-exact
Legacy-vs-fine and Legacy-vs-coarse screenshots. This **exact**
fixture deliberately uses non-simplified parent geometry; it is not
a crack/seam or perceptual-error allowance for coarse real-world LODs.

The runner writes four binary PPM captures to the working directory
by default (`strata-raster-empty.ppm`,
`strata-raster-legacy.ppm`,
`strata-raster-strata-fine.ppm`,
`strata-raster-strata-coarse.ppm`). Set
`AIVERSE_STRATA_RASTER_CAPTURE_PREFIX=/path/to/prefix` to choose
the location. It checks a single ModelDraw submission per candidate
frame, nonzero main and sun-shadow submission counters and
zero changed channels in the compared screenshots.
It does NOT yet read the shadow-map texture directly, exercise
planar-water reflection or automatically use the GPU compute
selected-range buffer as authoritative draw input. These remain
separate acceptance tests.

Manual validation on a Mac Studio:
```sh
cd ~/Projects/AIVERSE-strata-validation
git status --short
git fetch origin feature/strata-virtual-geometry
# If clean (or after preserving local modifications):
git switch --detach origin/feature/strata-virtual-geometry
cmake -S . -B build/strata -DAIVERSE_BUILD_TESTS=ON \
  -DAIVERSE_BUILD_STRATA=ON -DAIVERSE_STRATA_METAL_GPU_TESTS=ON
cmake --build build/strata --target \
  aiverse_strata_metal_parity aiverse_strata_metal_raster_parity \
  --parallel 8
ctest --test-dir build/strata \
  -R '^strata_(builder|model_adapter|view|gpu_scene|gpu_culling|render_routing)
- **One scene/instance truth**: `GeometryScene` owns static render instances
  and HLOD; do not create separate Strata transforms or introduce gameplay
  entities solely for rendering.
- **One grouping authority**: indirect draw grouping/material binding stays
  in the existing renderer, with a future Strata adapter supplying visible
  cluster ranges. Never double-submit the same primitives.
- **GPU capability negotiation**: GLES3/WebGL2 should retain traditional
  indexing paths. Metal/Vulkan/D3D future paths must gate compute, indirect
  count, storage resources and synchronization independently.
- **Multi-world**: geometry assets can be shared by handle across worlds,
  while scene-resident instance tables and render budgets are per world/view.
- **Dynamic terrain**: TerraWeave owns chunk geometry and versions; Strata
  consumes immutable snapshots and schedules deferred release. PHYSIS owns
  simulation/events, not GPU buffers.
- **Pass separation**: main-camera, shadows, reflections and portals may
  require distinct culling/error metrics and shadow caster inclusion.

## Known shortcomings / next steps

1. Compile and execute focused tests on a native project checkout:
   ```sh
   cmake -S . -B build/strata -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_STRATA=ON
   cmake --build build/strata --target aiverse_strata_builder_tests aiverse_strata_model_adapter_tests aiverse_strata_view_tests aiverse_strata_gpu_scene_tests aiverse_strata_gpu_culling_tests -j 8
   ctest --test-dir build/strata -R '^strata_(builder|model_adapter|view|gpu_scene) --output-on-failure
   ```
2. Add a recording-device scene fixture comparing Strata staging OFF/ON,
   instance deduplication, material/LOD/shadow draw parity and unload/reload.
3. Validate silhouette and seam quality for different LOD depths and attribute
   discontinuities; parent simplification and locked borders alone do not
   guarantee crack-free transitions.
4. The existing `GlobalGeometryPool` now reserves auxiliary Strata index
   spans without creating a new page, and version-2 metadata maps primitive
   bases to their actual pooled GPU offsets. Verify on Mac Metal and Vulkan:
   shared-buffer writes, reupload/revision invalidation, in-flight retirement,
   surrounding rigid models, and all original fallback paths before enabling
   submission. Default staging remains OFF.
5. Experimental Metal compute culling now emits diagnostic GPU-selected
   ranges but does not render them. Run native Metal compile, GPU readback
   and sorted CPU `planView` parity, including stack/output overflow and
   zero-to-one depth convention, before any renderer connection.
6. Add mutually exclusive Renderer3D selected-range submission only after
   GPU/CPU selection AND screenshot parity. Preserve legacy fallback on all
   unsupported devices; then pursue Hi-Z, streaming and independent CPU/GPU
   benchmarks.

## Verification policy

Compiler, CTest, screenshots, GPU timing and benchmarks must be reported only
after **actual execution** with exact logs or CI URLs. Queued/cancelled runs
are not passing results.
 --output-on-failure
   ```
2. Add a recording-device scene fixture comparing Strata staging OFF/ON,
   instance deduplication, material/LOD/shadow draw parity and unload/reload.
3. Validate silhouette and seam quality for different LOD depths and attribute
   discontinuities; parent simplification and locked borders alone do not
   guarantee crack-free transitions.
4. Extend **existing** `GlobalGeometryPool` to safely allocate an auxiliary
   Strata index stream on an existing pooled page, with explicit scene/
   model revision checks, upload failure rollback, frame-fenced retirement
   and shared-buffer synchronization. Do not add a second geometry VB/IB pool.
   All current Strata metadata offsets remain source-local, not drawable.
5. Build a capability-gated GPU scene/cluster selection path using the
   CPU planner as a correctness oracle. Submit visible ranges through one
   existing `Renderer3D` batching/MDI owner, or fall back to normal models.
6. Benchmark CPU and GPU time separately, validate view-dependent shadows,
   reflection views and multi-world lifetime, then pursue streaming.

## Verification policy

Compiler, CTest, screenshots, GPU timing and benchmarks must be reported only
after **actual execution** with exact logs or CI URLs. Queued/cancelled runs
are not passing results.
 \
  --output-on-failure
./build/strata/aiverse_strata_metal_parity
./build/strata/aiverse_strata_metal_raster_parity
```

Expected **but NOT yet observed** for the new raster probe:
`[strata-raster] PASS: real Renderer3D legacy vs fine/coarse source-page pixel parity`
followed by `[strata-raster] status=PASSED`.
Failure exit or any image difference means raster parity remains unverified.
The prior **actual** Metal GPU compute parity 5/5 and focused CPU
CTests 6/6 are recorded separately and must not be represented as
proof that Stage 11 raster parity passes.

`.github/workflows/strata-focused.yml` now uses the corrected
six-test CTest regex and additionally **compiles** both native Metal
fixtures on macOS CI. It does **not execute the GUI tests there**,
so a green compile job alone is not evidence of GPU raster parity.
Keep the feature PR Draft and production renderer unchanged until
exact visual and shadow/reflection parity is demonstrated on hardware.

## Stage 11.1 — Metal raster capture alignment fix (2026-11-19)

The owner compiled the real Metal `aiverse_strata_metal_raster_parity`
fixture and ran it on Mac Studio. The prior GPU cluster compute parity
**remains PASS (5/5)**, but the raster fixture reported:

```text
[strata-raster] FAIL: Legacy and empty screenshots identical; test drew no model
[strata-raster] status=FAILED frames=45
[metal-assets] ... pipelines=16 pipelineFailures=0
```

This does **not** prove a vertex/index geometry bug. Inspection of
`GfxMetal.mm` revealed a test scheduling defect: when the current
drawable is `framebufferOnly`, `captureNextFrame` sets
`captureDelay_=1` and the requested image is copied in a *later*
frame. The fixture requested its captures on frames 8, 19, 30 and
41, exactly before transitions at 9, 20, 31 and 42. Thus an
"Empty" request could capture the following Legacy geometry, a
"Legacy" request the following Fine/Strata geometry, etc. Identical
"Empty/Legacy" screenshots may therefore be a false attribution
even when valid geometry was rendered. Do not weaken the independent
blank-baseline requirement to hide this test failure.

Fix `bcc471e93c9a4a35f7fda457d4fa15ccbda13fe9` in
`tests/render/strata_metal_raster_parity.cpp`:

- Explicitly use `Device::setScreenReadable(true)` at fixture
  initialization, before the platform begins drawing, so drawable
  textures can be captured in the intended frame. Reset the flag
  before shutdown.
- Request each image in the middle of its steady test mode:
  **frame 4 Empty, 15 Legacy, 26 Strata Fine, 37 Strata Coarse**.
  These slots remain safe even if a supported RHI adds up to two
  capture frames of delay.
- Print requested mode/frame and actual callback frame, with
  main/shadow pass counters. If Empty and Legacy remain identical,
  print captured mode counters before failing, to distinguish model
  invisibility from screenshot phase errors.
- Retain strict `Empty != Legacy`, `Legacy == Fine` and
  `Legacy == Coarse` bytewise GPU screenshot checks. Do not
  alter production renderer, shadow pipelines, asset builder,
  global GPU source pool or the indexed sidecar.

**Validation:** This fix is committed in GitHub but has
**NOT yet been compiled or run by the assistant**. The owner's
pre-fix Stage 11 raster result is FAIL, not PASS. The owner-confirmed
six focused CPU/mock tests and five real Metal compute parity scenarios
remain independently PASS on their prior tested snapshot.
Real main/shadow screenshot parity remains unverified until a
post-fix successful Mac run; a pure CMake build or queued CI is not
proof of visual correctness.

From the existing worktree, after making sure local changes are
preserved:

```sh
cd ~/Projects/AIVERSE-strata-validation
git status --short
git fetch origin feature/strata-virtual-geometry
git switch --detach origin/feature/strata-virtual-geometry
cmake --build build/strata --target aiverse_strata_metal_raster_parity --parallel 8
./build/strata/aiverse_strata_metal_raster_parity
```

If the corrected baseline still fails, inspect the new
`capture requested` / `capture received` frame indices,
the `shadow/opaque` counters, and four saved PPM files. Confirm
actual model visibility/culling and valid source pooled base offsets
before changing Strata geometry or relaxing screenshot parity.
Future real raster parity still needs separate shadow-map depth
readback, water reflection view, multi-material and simplified
LOD error-budget tests before production draw routing.

## Architectural constraints
- **One scene/instance truth**: `GeometryScene` owns static render instances
  and HLOD; do not create separate Strata transforms or introduce gameplay
  entities solely for rendering.
- **One grouping authority**: indirect draw grouping/material binding stays
  in the existing renderer, with a future Strata adapter supplying visible
  cluster ranges. Never double-submit the same primitives.
- **GPU capability negotiation**: GLES3/WebGL2 should retain traditional
  indexing paths. Metal/Vulkan/D3D future paths must gate compute, indirect
  count, storage resources and synchronization independently.
- **Multi-world**: geometry assets can be shared by handle across worlds,
  while scene-resident instance tables and render budgets are per world/view.
- **Dynamic terrain**: TerraWeave owns chunk geometry and versions; Strata
  consumes immutable snapshots and schedules deferred release. PHYSIS owns
  simulation/events, not GPU buffers.
- **Pass separation**: main-camera, shadows, reflections and portals may
  require distinct culling/error metrics and shadow caster inclusion.

## Known shortcomings / next steps

1. Compile and execute focused tests on a native project checkout:
   ```sh
   cmake -S . -B build/strata -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_STRATA=ON
   cmake --build build/strata --target aiverse_strata_builder_tests aiverse_strata_model_adapter_tests aiverse_strata_view_tests aiverse_strata_gpu_scene_tests aiverse_strata_gpu_culling_tests -j 8
   ctest --test-dir build/strata -R '^strata_(builder|model_adapter|view|gpu_scene) --output-on-failure
   ```
2. Add a recording-device scene fixture comparing Strata staging OFF/ON,
   instance deduplication, material/LOD/shadow draw parity and unload/reload.
3. Validate silhouette and seam quality for different LOD depths and attribute
   discontinuities; parent simplification and locked borders alone do not
   guarantee crack-free transitions.
4. The existing `GlobalGeometryPool` now reserves auxiliary Strata index
   spans without creating a new page, and version-2 metadata maps primitive
   bases to their actual pooled GPU offsets. Verify on Mac Metal and Vulkan:
   shared-buffer writes, reupload/revision invalidation, in-flight retirement,
   surrounding rigid models, and all original fallback paths before enabling
   submission. Default staging remains OFF.
5. Experimental Metal compute culling now emits diagnostic GPU-selected
   ranges but does not render them. Run native Metal compile, GPU readback
   and sorted CPU `planView` parity, including stack/output overflow and
   zero-to-one depth convention, before any renderer connection.
6. Add mutually exclusive Renderer3D selected-range submission only after
   GPU/CPU selection AND screenshot parity. Preserve legacy fallback on all
   unsupported devices; then pursue Hi-Z, streaming and independent CPU/GPU
   benchmarks.

## Verification policy

Compiler, CTest, screenshots, GPU timing and benchmarks must be reported only
after **actual execution** with exact logs or CI URLs. Queued/cancelled runs
are not passing results.
 --output-on-failure
   ```
2. Add a recording-device scene fixture comparing Strata staging OFF/ON,
   instance deduplication, material/LOD/shadow draw parity and unload/reload.
3. Validate silhouette and seam quality for different LOD depths and attribute
   discontinuities; parent simplification and locked borders alone do not
   guarantee crack-free transitions.
4. Extend **existing** `GlobalGeometryPool` to safely allocate an auxiliary
   Strata index stream on an existing pooled page, with explicit scene/
   model revision checks, upload failure rollback, frame-fenced retirement
   and shared-buffer synchronization. Do not add a second geometry VB/IB pool.
   All current Strata metadata offsets remain source-local, not drawable.
5. Build a capability-gated GPU scene/cluster selection path using the
   CPU planner as a correctness oracle. Submit visible ranges through one
   existing `Renderer3D` batching/MDI owner, or fall back to normal models.
6. Benchmark CPU and GPU time separately, validate view-dependent shadows,
   reflection views and multi-world lifetime, then pursue streaming.

## Verification policy

Compiler, CTest, screenshots, GPU timing and benchmarks must be reported only
after **actual execution** with exact logs or CI URLs. Queued/cancelled runs
are not passing results.


## Stage 11.1 — owner-verified real Metal raster parity PASS (2026-11-19)

**Actual native Mac Studio output**, after the Metal screenshot scheduling
fix `bcc471e93c9a4a35f7fda457d4fa15ccbda13fe9`, confirms:

- `aiverse_strata_metal_raster_parity` successfully built and executed.
- Capture requests/callbacks: Empty frame 4→6; Legacy 15→17;
  Strata Fine 26→28; Strata Coarse 37→39. All four capture callbacks
  returned **1,920,000 bytes**, consistent with 800 × 600 × 4.
- Renderer profile draw counts per requested frame:
  Empty shadow=0 opaque=0; Legacy shadow=1 opaque=1;
  Fine shadow=2 opaque=2; Coarse shadow=1 opaque=1.
- `Legacy/Fine changed_channels=0`;
  `Legacy/Coarse changed_channels=0`.
- Three reported PASS assertions: actual `Renderer3D`
  source-page raster pixel parity, main+sun-shadow submissions, and
  one ModelDraw per comparison frame with no second GPU geometry pool.
- Final `[strata-raster] status=PASSED frames=44`.
- Metal pipeline diagnostics:
  `[metal-assets] libraries=0 sourceMisses=11 pipelines=16 pipelineFailures=0 archive=0 strict=0`.

The prior fixture's `Legacy and empty screenshots identical` failure
was fixed without relaxing the nonblank baseline check or pixel parity
criteria. Real Metal cluster-selection parity had already been
owner-verified independently: `cases=5/5 status=PASSED`;
the focused native CPU/recording-RHI CTests passed 6/6 on the earlier
provided log. The Stage 11.1 run did **not** rerun all six CTests.

**This proves only lossless parent/leaf indices produce identical final
main-camera RGBA images through the existing renderer on this fixture,
with main/shadow pass submission counters.** It does NOT demonstrate
direct shadow-map depth-texture equality, reflection parity,
arbitrary material equivalence, intentionally simplified LOD visual
quality, crack-free mixed LOD seams, GPU-selected indirect rendering,
automatic streaming, or cross-backend parity. Production Strata
draw routing remains OFF; main remains unchanged; PR #35 stays Draft.

Next: add independent shadow depth texture readback parity and reflection/
multiview fixtures, then materially varied, simplified and multi-instance
cases. Continue enforcing one `GlobalGeometryPool` owner and one
`Renderer3D` draw authority; only then consider opt-in exclusive
Legacy/Strata production switching.


## Stage 12 — native Metal raw shadow-depth and independent two-view parity (2026-11-19)

**Implementation is committed; native compilation and execution are NOT
YET VERIFIED**. This is a new, opt-in, standalone fixture that does not
modify the previously owner-verified Stage 11 raster fixture, does not
switch Strata production renderer ownership, and does not introduce a
second GPU geometry pool.

### Diagnostic GPU API and lifetime

- `engine/gfx/Gfx.h`: `gfx::Device::readDepthTextureAsync(TextureHandle,
  DepthReadFn)` is optional (default false on other RHI backends);
  returns tightly packed native `float32` pixels in a
  `std::vector<uint8_t>` with width and height, or empty data on GPU
  failure. The callback fires on the owning device thread at a later
  `beginFrame`. The caller must schedule it after the writing pass
  and keep the texture alive until the command completes.
- `engine/gfx/metal/GfxMetal.mm`: bounded, non-stalling Metal
  `Depth32Float` private-texture-to-shared-buffer GPU blit. It only
  accepts non-multisampled 2D depth texture; rejects open encoders,
  invalid handles, unsupported format, dimensions over 4096², more
  than eight outstanding readbacks or transfers over 64 MiB.
  Uses 256-byte aligned Metal blit rows, repacks as tightly packed
  float32, checks GPU completion status and delivers callbacks on
  the device owner thread; no global synchronizing GPU wait.
  This API remains unused in ordinary rendering.
- `engine/render/Renderer3D.h/.cpp`:
  `requestSunShadowDepthReadback(DepthReadFn)` exposes **no owned
  shadow texture handle** and delegates to the RHI only when an
  active sun shadow map exists. Call after `Renderer3D::render`
  and after ending the still-open main pass.

### Stage 12 executable

`tests/render/strata_metal_depth_multiview_parity.cpp` builds as
`aiverse_strata_metal_depth_multiview_parity` on macOS with
`AIVERSE_STRATA_METAL_GPU_TESTS=ON`. It is intentionally not
registered as a headless CTest, since it opens a real CAMetalLayer.

For each of TWO independent cameras (view 0: pitch 0.2, yaw 0,
distance 5; view 1: pitch 0.55, yaw 0.7, distance 6), render four
separate and *exclusive* steady-mode frames:
Empty (no ModelDraw), Legacy, Strata Fine (two children) and
Strata Coarse (lossless parent). No frame ever submits both
legacy source and Strata; all draws go through the ordinary
`Renderer3D::drawModel`. The Strata alias uses the original
`GlobalGeometryPool` source VB and auxiliary index sidecar.
The sun map is intentionally 512×512 for bounded readback, while
the real screen image remains 800×600.

For each view/mode, the fixture captures:
- An actual main RGBA frame via `captureNextFrame`, after enabling
  screen-readability before frame acquisition, with screenshots
  requested inside a steady mode to avoid the known capture-delay
  bug; and
- The current sun shadow texture's real `Depth32Float` pixels,
  via the new GPU blit **after closing the main pass**, in the same
  frame. Data are not derived from GPU draw counters.

Acceptance requires: all eight RGBA and eight depth captures,
nonblank Legacy vs Empty for BOTH formats, zero changed RGBA channels
between Legacy/Fine/Coarse in each view, **zero byte changes in
raw float32 sun-shadow depth** between Legacy/Fine/Coarse in each
view, finite depth values in [0,1], at least one sample different
from clear-depth, nonzero shadow/opaque draw counters for model
cases, exactly one candidate ModelDraw per frame, and distinct
Legacy RGBA images from the two camera views.
This strict oracle is appropriate only because the fixture's
hierarchical LODs are intentionally **geometrically lossless**.
It does NOT prove fidelity of simplified LODs.

By default the runner saves eight `strata-depth-view{0|1}-MODE.ppm`
color captures and eight
`strata-depth-view{0|1}-MODE-shadow.f32` packed little-endian
32-bit float raw depth snapshots in the working directory.
Set `AIVERSE_STRATA_DEPTH_CAPTURE_PREFIX=/path/prefix` to adjust.
With 512² float32 values, each depth file is 1 MiB.

Manual Mac Studio build/run:

```sh
cd ~/Projects/AIVERSE-strata-validation
git status --short
git fetch origin feature/strata-virtual-geometry
git switch --detach origin/feature/strata-virtual-geometry
cmake -S . -B build/strata \
  -DAIVERSE_BUILD_TESTS=ON \
  -DAIVERSE_BUILD_STRATA=ON \
  -DAIVERSE_STRATA_METAL_GPU_TESTS=ON
cmake --build build/strata --target \
  aiverse_strata_metal_depth_multiview_parity --parallel 8
./build/strata/aiverse_strata_metal_depth_multiview_parity
```

Switching commits requires a clean worktree or saved local edits.
Expected, **not yet observed**, successful summaries:

```text
[strata-depth] view=0 color_fine=0 color_coarse=0 depth_fine_bytes=0 depth_coarse_bytes=0
[strata-depth] view=1 color_fine=0 color_coarse=0 depth_fine_bytes=0 depth_coarse_bytes=0
[strata-depth] PASS: exact sun-shadow Depth32Float Legacy/Fine/Coarse parity
[strata-depth] status=PASSED views=2 modes=4 ...
```

The `.github/workflows/strata-focused.yml` macOS job now
**compiles** the Stage 12 executable, but does not execute GUI GPU
fixtures. Until real owner Mac Studio output is provided, do not
claim Stage 12 PASS, real shadow-depth parity, independent-view
parity, or safety of production Strata rendering. Stage 11's prior
real Metal main-raster strict parity remains **actually PASS**
(0 changed channels for Fine and Coarse), and the prior real
Metal compute oracle remains **5/5 PASS** in its separate fixture.

**Next gates:** Planar reflection target readback and per-reflection
camera selection, multi-material and UV boundary parity, GPU selected
cluster range draw execution with exclusive legacy fallback, lossy
LOD quality/edge cracks, multi-world lifecycle, and GPU performance
before considering a production default.
