# Strata — AIVERSE virtual geometry

Status (2026-11-19): **experimental, import-time implementation only**.
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
1. Make both focused test targets compile and pass (including new staging test cases). Command:
   `cmake -S . -B build/strata -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_STRATA=ON`
   then `cmake --build build/strata --target aiverse_strata_builder_tests aiverse_strata_model_adapter_tests`
   then `ctest --test-dir build/strata -R '^strata_(builder|model_adapter)
2. Add explicit material/attribute seam, silhouette and vertex displacements
   regression fixtures. Attribute-aware simplification is implemented for
   Model adapters, but locked borders are not a guarantee against cracks
   at all heterogeneous LOD frontiers.
3. Validate meshlet locality; consider importing full upstream meshoptimizer
   clustering/partition implementation in a versioned dependency update.
4. CPU-only `GeometryScene` staging is available behind an explicit opt-in
   switch, but requires full native validation and visual/performance A/B.
   Only then add a per-pass selected-range submission adapter; retain legacy
   material and batching ownership and prevent double submission.
5. Build independent CPU and GPU benchmark references; measure quality,
   memory, streaming stalls, frame time and fallback behavior.

## Verification policy
Compiler, CTest, GPU timing and screenshots must be reported only after
actual execution with logs/CI URLs. An opened or queued CI run is not a PASS.
 --output-on-failure`.
2. Add explicit material/attribute seam and silhouette policies; use
   `meshopt_simplifyWithAttributes` once vertex attributes are available.
   Current locked borders are not a universal guarantee against cracks
   at all heterogeneous LOD frontiers.
3. Validate meshlet locality; consider importing full upstream meshoptimizer
   clustering/partition implementation in a versioned dependency update.
4. Add a feature-gated, single `GeometryScene` adapter and per-pass bounds
   tests before any GPU Scene/culling path.
5. Build independent CPU and GPU benchmark references; measure quality,
   memory, streaming stalls, frame time and fallback behavior.

## Verification policy
Compiler, CTest, GPU timing and screenshots must be reported only after
actual execution with logs/CI URLs. An opened or queued CI run is not a PASS.
