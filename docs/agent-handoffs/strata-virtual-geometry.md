# Strata virtual geometry — agent handoff (stage 1, 2026-11-19)

## Resume here
Branch: `feature/strata-virtual-geometry` (based on `main` at branch creation).
Implementation commit: `a87d4fd4fa8b3609243530ab44bde261ccee2391`.
This document is the first Strata handoff. Read `AGENTS.md`, `docs/metal-batching-agent-handoff.md`, `docs/geometry-performance.md`, `docs/geometry.md`, `docs/models.md` and inspect current branch HEAD before edits. Preserve other agents' changes.

## Existing architecture found on main
- `engine/render/Geometry.h`: `GeometryPolicy`, `GeometryCluster`, `GeometryData`, CPU geometry preparation and occlusion.
- `engine/render/GeometryScene.h`: static instances, HLOD groups, cache, proxies.
- `engine/render/AutoLodRuntime.cpp`: meshoptimizer-based runtime LOD for eligible imported opaque rigid models.
- `engine/gfx/Gfx.h`: BufferType::Storage/Indirect, DrawIndexedIndirectCommand (20 bytes), Metal/GLES/GL/Vulkan. Existing indirect commands are generated on the CPU (see `docs/models.md`).
- `docs/geometry-performance.md`: restrictions of current CPU clustering, shadow selection, cache and Metal batching.
Do NOT duplicate GeometryScene's ownership, issue independent material state/draw submits, or replace existing batching without A/B evidence.

## Stage 1 implementation (completed)
Added `engine/render/StrataGeometry.h`: independent CPU reference types `ClusterNode`, `SelectionInput`, `SelectionOutput`, hierarchy `validate` and `select`. Flat ordered DAG-free tree with roots, per-node material/index references and residency; child refinement is atomic (fallback to parent if any child is not resident); projected per-node pixel error supplied by caller; bounded selection and explicit budget/missing-residency diagnostics.
No existing runtime pathways have been modified. This is a preparatory contract, not a functioning virtual geometry backend.

## API / compatibility
Namespace `aiverse::render::strata`. Header-only API; no new CMake targets, binary formats, ECS components, shader resources, backend capabilities or save formats. No breaking runtime changes.
`select` has no occlusion/frustum handling; current renderer remains the visibility authority. Root clusters must be resident. Input errors must be computed conservatively for the actual pass. Keep material groups and opacity compatibility in asset pipeline.

## Actual validation
GitHub commit successfully created. Local compilation, CTest, GPU tests and CI: **NOT RUN** (GitHub connector does not execute native build commands). DO NOT claim this compiles or renders on hardware. First next action: compile isolated header and add reference-selector fixtures.

## Next actionable stage
1. Add `tests/strata_geometry_test.cpp`, CMake test target, verify invalid graphs, resident fallback, refinement, budget, NaN/Inf and material partition cases. Fix selector issues before enabling runtime path.
2. Implement offline/imported cluster building with existing pinned meshoptimizer; use group hierarchies and conservative error/bounds, preserve original source mesh/LOD for fallback.
3. Add a single shared GeometryScene→Strata adapter, opted in by capability/asset policy. Do not divert dynamic/skinned/morphed meshes by default.
4. Introduce GPU Scene instance storage owned by the render presentation layer, not simulation ECS, then GPU frustum/Hi-Z selection, indirect command compaction, residency/streaming with feature-tested Metal/Vulkan backend paths. GLES/WebGL keep CPU fallback.
5. Add per-pass diagnostics, GPU timestamps and A/B benchmarks on instancing-heavy scenes plus terrain/foliage; do not report hypothetical speedups as measured.
6. Integrate TerraWeave immutable chunks first, then dirty-region rebuild and PHYSIS event notifications with versioned handles; only after residency and synchronization safety are proven.

## Coordination
- Strata owns mesh cluster hierarchy, selection, geometry residency and presentation-side GPU visibility.
- Existing batching owns compatible material/draw grouping; do not develop a second command allocator in parallel.
- TerraWeave owns terrain generation and changed-chunk events; Strata consumes render meshes/versions, does not mutate terrain.
- PHYSIS supplies world events and simulation outcomes; no genre dependencies.
- Prisma is postprocessing, SpectraFX is VFX, neither owns Strata geometry.

## Handoff discipline
Every stage: first commit code, then append a dated section here with exact code SHA, modified paths, actual tests and results, blockers, API compatibility and one next action; commit handoff separately. Never write PASS for unrun tests. Rebase/merge main safely before substantial integration; avoid force push.
