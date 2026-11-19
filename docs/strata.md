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
- Link `aiverse_strata_builder` in project CMake when
  `AIVERSE_BUILD_STRATA=ON` (ON by default). The target reuses the pinned
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
- Parent clusters are produced by `meshopt_simplify` using
  `meshopt_SimplifyLockBorder | meshopt_SimplifyErrorAbsolute`.
  The parent's geometric error is conservatively accumulated from its
  children. A group with under 10% reduction stays unmerged; multiple roots
  are permitted. Max levels, target ratio and relative error are configurable.
  Error in `ClusterNode` is in object units; the caller must project it to
  pixels using the correct pass and transform.
- `select()` requires **all roots resident**. A missing child prevents
  refinement of its parent; budget pressure retains the parent.
  The caller supplies a valid projected error for every node. The implementation
  is a CPU reference selector, not GPU culling or residency.
- The current builder is position-only (no normal/UV/color weighting).
  Do not route skinned, morphed, masked/transparent, displaced or shader-graph
  surfaces through it without explicit authoring and A/B parity work.
  Importers should preserve existing meshes and exact LOD0 as a fallback.

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
1. Make tests compile and pass. Command:
   `cmake -S . -B build/strata -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_STRATA=ON`
   then `cmake --build build/strata --target aiverse_strata_builder_tests`
   then `ctest --test-dir build/strata -R '^strata_builder$' --output-on-failure`.
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
