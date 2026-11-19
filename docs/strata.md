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
   cmake --build build/strata --target aiverse_strata_builder_tests aiverse_strata_model_adapter_tests aiverse_strata_view_tests aiverse_strata_gpu_scene_tests -j 8
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
5. Build a capability-gated GPU scene/cluster selection path using the
   CPU planner as a correctness oracle. Submit visible ranges through one
   existing `Renderer3D` batching/MDI owner, or fall back to normal models.
6. Benchmark CPU and GPU time separately, validate view-dependent shadows,
   reflection views and multi-world lifetime, then pursue streaming.

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
