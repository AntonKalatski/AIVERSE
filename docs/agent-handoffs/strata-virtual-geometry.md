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


---

## Stage 2 — deterministic meshlet/cluster asset builder (2026-11-19)

### Branch and exact implementation commits
- Branch: `feature/strata-virtual-geometry`; base originally created from `main`.
- `12cff0e026db581869f352b5302f4c20be2c126f`: new `engine/render/StrataBuilder.h` import-time public types and API.
- `81377a619cbbbbecf4ff521a71ec1314d0bbec13`: new `engine/render/StrataBuilder.cpp` meshlet packer, parent simplification and BFS tree layout.
- `86d64e2417916aa3ecb04a9bb1a9da189986114f`: new `tests/render/strata_builder.cpp` scenarios.
- `82e84e8475f7589d60f7dc439c0d09526f810a15`: CMake option `AIVERSE_BUILD_STRATA=ON`, standalone `aiverse_strata_builder` and `strata_builder` CTest target.
- `3527d0f7994e6ac72da86f93bfd3bf5f272bf598`: active asserts under Release tests.
- `a260e9155e0c7e38093cf909a4b10b6f93d22f3b`: API/architecture document `docs/strata.md`.
- `3a813fece0aad4c8454939da12bd3240e3d4d0fa`: ownership and handoff pointer in root `AGENTS.md`.
- `f00ee0c0ae372a89c3995d0bb900cb3aa1c7128f`: explicit `<iterator>` include.

### Implemented
- Transactional, CPU-only import-time `build(input, options, asset, error)` for a **single material partition**. Input is borrowed, bounds and indices are validated; failure does not overwrite an existing asset.
- Deterministic greedy meshlets in source triangle order, with per-leaf local uint8 vertex indexing, global vertex mapping and original-triangle indexed fallback. Defaults are 64 vertices / 124 triangles.
- Group parent generation via already vendored meshoptimizer v1.3 `meshopt_simplify` (no new vendored dependency): border lock and absolute position error, cumulative parent error, per-node AABB, opt-out of ineffective parent groups (less than 10% reduction).
- Tree flattening to the existing `StrataGeometry.h` top-down contiguous-child contract. Multiple roots allowed. All newly generated data CPU-resident until a future residency manager exists.
- Isolated `aiverse_strata_builder` static target and focused CTest; existing `aiverse_models`, `GeometryScene`, RHI and active draw paths remain unchanged.
- `docs/strata.md` is the public engineering contract; AGENTS now points to it and this handoff.

### API and compatibility
- New headers are `engine/render/StrataBuilder.h` and `StrataGeometry.h` in `aiverse::render::strata`.
- New CMake option `AIVERSE_BUILD_STRATA` defaults ON. This adds the builder to default build and keeps it unlinked to production renderer; it shares the existing meshoptimizer library, including when runtime AutoLOD is OFF.
- No changes to existing model/game/save formats, existing runtime draw behavior, frame scheduling, shaders, ECS state or network semantics.
- Parents represent indexed-draw *cluster groups*, not guaranteed hardware meshlets. Leaves fit configured compact meshlet limits. All simplification uses only vertex positions; normals, UV, tangent and color seam behavior still needs attribute-aware handling.
- Source LOD0 and existing model buffers remain intact; no model importer is calling this builder yet. No Vulkan/Metal/GL GPU functionality is claimed.

### Testing and CI (actual status at handoff)
- Compilation: **NOT RUN** (working environment lacks the project checkout and can only use GitHub connector; not a local build runner).
- CTest `strata_builder`: **NOT RUN**. CMake target and fixture were added, but there is no execution result.
- GPU / visual parity / benchmark: **NOT RUN**.
- GitHub workflow runs for `3a813fe` were observed **queued/pending**, no successful conclusion was available at check time. Do not interpret queued as PASS, and inspect the current head's CI independently.

### Specific next step (mandatory before enabling rendering)
1. Pull the exact current feature branch and newest `main` safely (no force push); read `docs/strata.md`, this handoff, root `AGENTS.md` and batching handoff.
2. Configure and compile `aiverse_strata_builder_tests`:
   `cmake -S . -B build/strata -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_STRATA=ON`
   `cmake --build build/strata --target aiverse_strata_builder_tests`
   `ctest --test-dir build/strata -R '^strata_builder$' --output-on-failure`
   Fix compiler errors, warnings or failing assertions with committed implementation and a fresh appended handoff.
3. Expand importer-side attribute preservation: `meshopt_simplifyWithAttributes` and explicit material/UV/normal/vertex-color seam tests, plus error/bounds monotonicity. Evaluate simplifier adjacency and crack-free LOD transitions; conservative error and border lock are not sufficient to claim universal crack-free results.
4. Only then add an **opt-in** single `GeometryScene` / `Model` adapter maintaining a single instance/material grouping owner; use existing indexing fallback until GPU culling and GPU residency are verified.

### Blockers / handoff warning
Current asset builder is not a GPU-driven renderer and has not been compiled in this session. Avoid merging PR #35 into `main` as production-ready until focused native tests and full renderer/asset compatibility checks pass. Do not let another agent duplicate Strata meshlet storage in batching, TerraWeave or PHYSIS.


---

## Stage 3 — weighted simplification and read-only Model adapter (2026-11-19)

### Branch / code commits
Branch `feature/strata-virtual-geometry`; no merge into main. Existing draft PR is #35.
- `11ec604a4fcf3a9e044b991ea8685f9d9896ed51`: `engine/render/StrataBuilder.h`, optional borrowed float attributes/stride/count/weights in `BuildInput`.
- `4773baf2b4d4c625a240d58e2ea83f81db92ee0a`: `engine/render/StrataBuilder.cpp`, validate all attribute records and finite positive weights; use pinned `meshopt_simplifyWithAttributes` when provided, preserving the old position-only path.
- `dedddc9c4c5368e411514357c343517a7692d95f`: `engine/render/StrataModelAdapter.h`, new opt-in, presentation-only API `buildModelAsset(const Model&, const BuildOptions&, ModelAsset&, std::string*)`.
- `9485c02f90187a6faf32224df07a557e9d1b70ef`: `engine/render/StrataModelAdapter.cpp`, construct per-primitive, per-material assets from existing CPU `Model` without mutating it; 15 weighted vertex attributes (normal, UV, tangent, RGBA, packed lightmap UV); skip unsupported materials/animation/deformation/existing authored LODs; transactional failures.
- `1f2d48f84ee1197ffc04ea227f202994a5419bb8`: `tests/render/strata_model_adapter.cpp` read-only, skip-policy and transactionality assertions.
- `bb070029f04f5b5a77e87edd344083a753374cab`: `CMakeLists.txt`, new static `aiverse_strata_model_adapter` target and `strata_model_adapter` CTest.
- `21fa0b2b9bab41d0988485fccf53d89e0287a35e`: `docs/strata.md`, public API updated.
- `60619eb0560e03f9904f985ab91d8c49283b51c0`: `tests/render/strata_builder.cpp`, exact source LOD0 triangle reconstruction, monotonic parent errors and invalid attribute layouts.
- `b18ddf84746644fe2cd921080d6e1687852aa6ab`: `AGENTS.md`, ownership/status update.

### Implemented and constraints
New Model adapter does **not** hook into active `GeometryScene` draw path. Material partitions retain their exact indices; all source Model vertices, indices, nodes, transforms and LOD0 remain unchanged. Skinned/morphed/mutable, authored-LOD, uploaded GPU, alpha masked/blended and shader graph surfaces are explicitly skipped. Output is wholly owned CPU data with diagnostic skip reasons. Any failure building an eligible primitive keeps the previously returned `ModelAsset` untouched.

The adapter uses `ModelVertex` layout (60 bytes, xyz at offset zero), supplies 15 components per vertex to meshoptimizer. Parent errors are cumulative; retaining locked borders alone does not prove watertight cross-LOD transitions. No runtime GPU path, no new shader, no new scene JSON or persistence format, no ECS changes.

### Tests and CI observed (NOT a pass)
- Local compiler / CMake / CTest: **NOT RUN**. Connected GitHub cannot execute a native build, and this assistant container cannot clone github.com.
- New CTest targets: `aiverse_strata_builder_tests` and `aiverse_strata_model_adapter_tests`. Tests **added, not executed**.
- Historical CI for commit `507ab6d0` was observed with the main CI workflow **cancelled** (other validation workflows queued). CI for `bb070029` and `60619eb0` was observed **queued**, no concluded success. Check newer head before claiming an outcome.
- No GPU timing, image comparison, streaming pressure or memory regression evidence exists.

### Immediate next tasks
1. On an AIVERSE build-capable machine, fetch and compile the branch:
   `git fetch origin feature/strata-virtual-geometry`
   `git worktree add --detach ../AIVERSE-strata origin/feature/strata-virtual-geometry`
   `cmake -S ../AIVERSE-strata -B ../AIVERSE-strata/build/strata -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_STRATA=ON`
   `cmake --build ../AIVERSE-strata/build/strata --target aiverse_strata_builder_tests aiverse_strata_model_adapter_tests -j 4`
   `ctest --test-dir ../AIVERSE-strata/build/strata -R '^strata_(builder|model_adapter)$' --output-on-failure`
2. Fix any compile/test errors and commit implementation; append a new section to this handoff as a **separate commit**.
3. Add material-safe and watertight transition fixtures: normals, UV seams, texture parity, geomorph/skirts/stitching and nearby clusters at different LOD depths. Verify error budgets with per-pass projection and non-uniform transforms.
4. Integrate **opt-in staging only** into `GeometryScene` after native checks. Keep current geometry owner/batching/draw submission untouched until profiling/visual parity demonstrates safety. Never enable both draw paths for one primitive.
5. Then implement GPU scene buffers/culling behind capability gates; retain CPU indexed fallback on all backends.

### Important integration ownership
Strata's `ModelAsset` is an auxiliary render-asset representation. `GeometryScene` remains the static instance owner, `Renderer3D` remains the material/draw grouping authority, and TerraWeave/PHYSIS keep simulation and terrain ownership. Avoid modifications to batching agent's concurrent GPU indirect code without coordinating current main and its handoff.


---

## Stage 4 — opt-in GeometryScene CPU staging (2026-11-19)

### Resume point / scope
**Branch:** `feature/strata-virtual-geometry`. Current design remains staging-only: default-OFF, no replacement of active `Renderer3D`, no new GPU submission, no ECS or scene format changes. PR #35 stays draft until native and GPU parity verification.

### Exact implementation commits (ordered)
- `0a47de5ed76ae833cbb1b2584e5e0c7dca06e000`: `engine/render/StrataModelAdapter.h` — `StagingStats`, `SceneStaging` APIs, per-source bounds, diagnostics and cache.
- `cb11bf5a4e5a16d46850498d2db12358ff49035c`: `engine/render/StrataModelAdapter.cpp` — deduplicate by source key, preserve old cache on failures, skip oversized source, collect skip/failed/staged source statistics, `find()`, `clear()`.
- `def009b11c3edd2df2fc86459dc85eb21649dfc1`: `engine/render/GeometryScene.h` — staging opt-in setter, support query, source lookup, counters and staging ownership.
- `16e93c91de762a309a269e0f67f42fd1a7a4e2f5`: `engine/render/GeometryScene.cpp` — stage source models once after geometry preparation and before `uploadModel()`, exclude proxy-only assets, release on `shutdown()`, preserve existing draw path.
- `fc9f49a6c2643de1a70c691ba500f692fe1e1e4c`: `CMakeLists.txt` — link adapter into runtime under `AIVERSE_BUILD_STRATA`, define `AIVERSE_STRATA_STAGING_ENABLED` only on runtime target.
- `39fda592f3b8188a75cb6c042abbb0a5702f1b1a`: `tests/render/strata_model_adapter.cpp` — duplicate source keys, error isolation, storage ownership, staging limits and teardown.
- `ec5e88a94b69f8ab8919f01a3fa2b90b955161ea` and `87fe730a5b298ee7ebe0666e17c1854117ccc00f`: separate skipped-source and skipped-primitive diagnostics in `GeometryScene`.
- `febe96105f1e1c211f249943e639dbb6b60d3a39`: `docs/strata.md` — native API, lifecycle, CPU/GPU boundaries and memory counters.
- `1fbce8c3d45faa5b527cbd998799919ca96fdf48`: `AGENTS.md` — explicit Strata ownership and default-OFF state.

### What is working in code (not yet tested on hardware)
- `GeometryScene::setStrataStagingEnabled(true)` **must be called before** `load()` / `loadPrepared()`. Disabled by default; scene JSON has no Strata field.
- A scene creates exactly one `strata::ModelAsset` per *participating original source model* (keyed by the scene model file). Duplicate scene instances use that same CPU asset. Source-only surface proxies and generated HLOD proxies are excluded.
- Builds happen before `uploadModel` can release raw CPU vertices, and after legacy `prepareGeometry`; no changes to source model materials, indices or node transforms. `Renderer3D::drawModel`, material grouping, shadows, GI and HLOD remain authoritative.
- `GeometryScene::strataAsset(file)` returns a pointer to the staged CPU asset or null. **Never retain this pointer after shutdown/reload**. `strataStagingSupported()` reports whether Strata was compiled into the runtime.
- Per-source staging defaults to max 250,000 vertices / 750,000 indices; oversize/invalid sources do not abort the existing legacy scene upload.
- `GeometryScene::stats()` adds `strataSources`, `strataPrimitives`, `strataSkippedSources`, `strataSkippedPrimitives`, `strataFailures`, `strataNodes`, `strataMeshlets`, `strataIndexBytes`. The latter covers auxiliary flat index data only, *not* total heap or GPU residency. Scene shutdown clears the staging memory and statistics.

### API/format compatibility
Public native C++ API is additive. Existing `GeometryScene` declarations, `setRuntimeOptions`, save/scene JSON format, model assets, ECS and RHI are unchanged. `AIVERSE_BUILD_STRATA=OFF` compiles a no-op capability path with no staging objects; other renderers keep working. This stage does **not** implement a Strata draw path or GPU Scene, culling, GPU residency, streaming, dynamic terrain nor skinned mesh support.

### Tests and CI — factual, NOT a passing claim
- Code-level tests added to `tests/render/strata_model_adapter.cpp` for source deduplication, limits, input validation/failure isolation and `clear()`. Existing `tests/render/strata_builder.cpp` continues to exercise meshlets and hierarchy.
- **Native C++ compile: NOT RUN.** This assistant's container has no AIVERSE checkout and GitHub cloning from that container failed due DNS resolution. GitHub connector can edit files but not execute a native build.
- **CTest: NOT RUN.** No measured GPU performance, screenshots or render parity tests.
- GitHub CI for preceding head `c7098130` was seen **cancelled**. CI for staging commit `fc9f49a6` was also observed **cancelled**; other validation workflows were queued. No completed passing run was observed. Do not claim green CI until checking actual latest head's jobs/logs.

### Immediate next actionable step
First validate the native build on Apple Silicon / Linux, before any GPU draw integration:

```sh
cd ~/Projects/AIVERSE
git fetch origin main feature/strata-virtual-geometry
git worktree add --detach ../AIVERSE-strata origin/feature/strata-virtual-geometry
cmake -S ../AIVERSE-strata -B ../AIVERSE-strata/build/strata -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_STRATA=ON
cmake --build ../AIVERSE-strata/build/strata --target aiverse_strata_builder_tests aiverse_strata_model_adapter_tests -j 4
ctest --test-dir ../AIVERSE-strata/build/strata -R '^strata_(builder|model_adapter)$' --output-on-failure
```

Then add and run a real `GeometryScene` staging fixture with a recording gfx::Device and compare the exact legacy draw calls/counters at staging OFF vs ON; verify two instances of one model create one CPU asset, failed/oversized source continues to draw, unload/reload frees assets, HLOD/shadows unchanged. Respect `docs/metal-batching-agent-handoff.md` and the latest main changes. Only after proof of compile and draw parity, implement a capability-gated Strata selected-range adapter with one material/draw submission owner. Keep it OFF by default. Record concrete test commands/logs in the next separate handoff commit.

### Risks to track
- Staging consumes auxiliary CPU memory: enforce project-wide and per-scene budgets before production, not just per-source input caps.
- Import simplification does not yet prove crack-free transitions, topology stability or attribute/texture parity under all content and pass views.
- No source asset handle/version tracking for dynamic/streamed TerraWeave meshes.
- GPU culling and indirect draw path are unimplemented, so the presence of staging counters must never be presented as a performance improvement.
- Concurrent batching changes exist in other branches; do not merge renderer internals without resolving interface ownership.


---

## Stage 5 — merge incremental static model cache into Strata (2026-11-19)

**Merge commit**: `0913b82088b63913e1313557de16d85e7c79b89f` in `feature/strata-virtual-geometry`. Two parents:
1. Pre-merge Strata `4c4a5587b26f3c81e9d33b770c962837db56548f`
2. Pinned batching `1043a9de747fe89cb42de899c15c6d2f4db8e0e1` from `perf/incremental-static-model-cache`

The merge was constructed on GitHub using the pinned batching tree as base, overlaying exactly the 12 non-overlapping Strata files and manually combining the **Strata ownership section** into the batching branch's latest `AGENTS.md`. Thus all GPU batching, renderer, RHI, geometry pool and its agent documentation remain intact; all Strata files and CPU staging are preserved. GitHub compare confirmed the merged head is descended from both parents. Against pinned batching, the only changed paths are Strata's 13 paths including combined `AGENTS.md`. No changes were sent to the batching feature branch.

**Subsequent engineering documentation commit**: `6c48a4bf9e7b71e021d9b403e1485e8ba06766c2` updates `docs/strata.md` with shared ownership/API policy.

### Integration decisions
- Reuse `GlobalGeometryPool` and its geometry revision/eligibility contracts. Do not create a second GPU pool for source models.
- Reuse existing `StaticModelCache`, `StaticModelCachePolicy`, model geometry/material revision paths and existing `EcsStaticModelRender` adapter.
- `Renderer3D` owns CPU MDI/GPU ICB/material grouping and emits draw commands. Future Strata culling must supply visible cluster ranges through one exclusive rendering path; never double-submit the original source.
- `GeometryScene` still owns source instances, HLOD and Strata's *opt-in CPU staging*. The existing legacy render route remains authoritative. All experimental render flags retain defaults.
- Treat the Strata PR as stacked atop batching PR #34 while the latter is draft, to prevent duplicated PR diffs and accidental merge into main before its dependency.

### Evidence and verification
- Branch and commit ancestry: **verified through GitHub's compare and Git Commit API**.
- Source integration conflict resolution: only `AGENTS.md` overlapped. Both sections retained in merged tree; all previously unique paths preserved.
- **Native compile, CTest, Metal screenshots, full Vulkan/GLES checks: NOT RUN in this session**. Assistant container has no mounted AIVERSE checkout; connected GitHub tools cannot execute native builds.
- Batching PR #34 is still draft and under active development. Its owner's actual 2026-11-19 small synthetic Metal run (handoff Stage 69) passed 33 CPU checks and pixel parity with Direct fallback on that fixture, but *does not validate the Strata merge* or bigger CPU/GPU performance.
- The batching agent pushed newer feature branch commits while merge planning. Integration deliberately pins `1043a9de`; check `git fetch origin` for further batching updates before depending on newer APIs.
- There is still no Strata GPU cluster-selection or residency implementation, and no measured frame-time reduction.

### Next exact actions
1. Fetch both branches and compile the merged Strata feature branch on a Mac Studio:
   `git fetch origin main perf/incremental-static-model-cache feature/strata-virtual-geometry`
   `git worktree add --detach ../AIVERSE-strata-merged origin/feature/strata-virtual-geometry`
   `cmake -S ../AIVERSE-strata-merged -B ../AIVERSE-strata-merged/build/strata -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_STRATA=ON`
   `cmake --build ../AIVERSE-strata-merged/build/strata --target aiverse_strata_builder_tests aiverse_strata_model_adapter_tests aiverse_material_batches -j 8`
   `ctest --test-dir ../AIVERSE-strata-merged/build/strata -R '^strata_(builder|model_adapter)$|^model_batching_policy$' --output-on-failure`
   If `aiverse_material_batches` is not enabled under this configuration, inspect its CMake guard, enable required option explicitly and record it.
2. Run explicit staging OFF/ON parity on `GeometryScene`, including repeated models, failures, unload/reload, shader/material combinations and moving shadows. Use real Metal validation before enabling experimental ICB.
3. Add an *exclusive* shared render-command adapter in existing batching; only after step 1+2 pass introduce Strata selection output into the runtime. Gate by backend capabilities and maintain legacy direct rendering.
4. Before any future batching merge, compare current `perf/incremental-static-model-cache` head against pinned `1043a9de`, inspect its handoff, and merge only after checking concurrent renderer ownership and test status.
5. Record code and verification in a new handoff appendix and commit separately. Do not mark checks that only queued/cancelled as passing.

### Risks
- Both branches are experimental and under heavy active development; a clean semantic merge does not prove compiler or GPU compatibility.
- Batching branch uses large shared renderer and GPU pool; future Strata-specific GPU logic must not fork the pool/command scheduler.
- Stack PR #35 on PR #34 until batching lands; keep draft and do not merge into main prematurely.


---

## Stage 6 — per-pass CPU cluster selection oracle (2026-11-19)

### Branch, upstream sync and exact implementation commits
**Branch:** `feature/strata-virtual-geometry`, draft PR #35, stacked on `perf/incremental-static-model-cache` PR #34. Main unchanged.

- `d9c4dd4f55b8b15ce31d7921f4995f79ac530b17` — two-parent merge of fresh batching `29f6cb3c7cd8965b5123bfbf17f09d2164e45dbb` into Strata. The batching difference since pinned `1043a9de` was its handoff doc (not a renderer implementation change). The merge tree initially had that doc's earlier blob.
- `38abceaa6cf30f028fb9df5e9d3074ca602349e5` — **corrective update** copying exactly the new batching handoff blob into Strata. Do not drop this fix or falsely describe the diff as a material_batches fixture change.
- `799686566cef85411e15d9ab88cb0ef161aa3ac4` — `engine/render/StrataView.h`, read-only `ViewConfig`, `SelectedRange`, `ViewPlan`, `planView` public API.
- `288c15c90170fe87cb37ca7ad71e4d03e167b620` — `engine/render/StrataView.cpp`: validating source topology and node bounds/indices, projecting errors via existing `geometryErrorPixels`, using residency-aware hierarchy selection, conservative frustum rejection, transactional output.
- `76ce342609a42877a1be9f4d0c9ca78c1da5e688` — `tests/render/strata_view.cpp`: main vs scaled pass, parent/child detail choice, root fallback on missing residency, budget restriction, frustum exclusion and malformed-asset rejection.
- `a07c002f4e4684897331c451c4a870df2300a564` — `CMakeLists.txt`: `aiverse_strata_view` library and `aiverse_strata_view_tests`/`strata_view` CTest registration.
- `596c5292c6603a5b3c4afdb9173002c3666ca4d2` — `engine/render/GeometryScene.h`: read-only `planStrataView(sourceKey, mesh, primitive, config, out, error)`.
- `226c240e9214ca7c104db79fe73ce348b77211e6` — `engine/render/GeometryScene.cpp`: delegates source-local selection to staged `ModelPrimitiveAsset`; reports absent/ineligible source without changing scene or output. Compiles to no-op if `AIVERSE_BUILD_STRATA=OFF`.
- `7eb22c2737df70d488de8dadcace1704ca7985e8` — `CMakeLists.txt` links runtime to `aiverse_strata_view` instead of only the Model adapter.
- `95cb5c4c62927edc35f3afbd9bebecbeee402b12` — `docs/strata.md`: Stage 6 planner API and caveats; repairs malformed/duplicated 'Known shortcomings' section inherited from previous doc edits.
- `bd3ecb7a9101826a781a6512cf71b6d2ea1f6f19` — `engine/render/StrataView.cpp` adds explicit `<utility>` header.
- `fb35f10072bfaf270e6e8e4c4f3bd1becc6fe9a1` — `AGENTS.md` update with source-local index-offset safety.

### What Stage 6 actually implements
- `strata::planView` takes **one already-built primitive asset** and a pass-specific object-to-clip transform, target dimensions, depth convention, pixel threshold and max node budget. It returns a list of selected node index ranges in **that Strata asset's own** index vector, plus selection/visibility diagnostic stats. It never creates gfx buffers or submits draws.
- The implementation reuses `strata::select` and existing `render::geometryErrorPixels` / `render::geometryBoxVisible`. CPU plan results for main, sun shadows, reflections and portals are intentionally independent. Caller supplies objectToClip including the correct bound node transform. Non-uniform model transforms are part of that matrix.
- Frustum testing occurs AFTER choosing resident hierarchical detail. Near-plane conservative error can prevent refinement but does not cause holes; missing child residency keeps the parent. No GPU Hi-Z, occlusion history or runtime geometry compression has been added.
- `GeometryScene` now exposes `planStrataView`, which uses its existing staging cache, picks a source material partition by mesh/primitive ID, and delegates to the same planner. Must enable staging before scene load to retrieve any asset. No draw list or instance owner is duplicated.

### Ownership, compatibility and blocker
- **Critical:** `SelectedRange::firstIndex` refers to `strata::Asset::indices`, never `Model::indices` or a pooled `Model::ib`. The caller cannot submit this range until the auxiliary Strata index stream is uploaded and offset correctly into a shared-pool page. Do not insert these offsets directly into the existing MDI command path. A dedicated versioned residency/allocation contract with `GlobalGeometryPool` is the next design/implementation gate.
- Public APIs are additive, opt-in and read-only. Existing scene JSON, saves, ECS, material groups, shadows and `Renderer3D::drawModel` remain unchanged. No D3D/Metal/Vulkan shader or index-buffer format change.
- The planner CPU implementation is a reference oracle, not an optimized production GPU path; it visits all nodes and reruns hierarchy validation. It is useful for correctness tests but should not be run unconditionally each frame.
- CPU view selection currently uses conservative frustum planes and projected geometric errors; cross-LOD cracks, material seams, dynamic deformation, GPU timestamps and real frustum test parity remain unverified.

### Real verification evidence
- GitHub connector successfully created and updated code and documentation commits; two-parent branch ancestry verified in previous stages.
- **Native compilation: NOT RUN. CTest: NOT RUN. GPU/Metal/Vulkan visual parity: NOT RUN.** This assistant does not have the AIVERSE source mounted in a build-capable environment.
- Checked GitHub Actions: CI for `38abceaa` and `a07c002f` was observed **cancelled**; at `95cb5c4c` it was **pending** and other workflows **queued**. No passing status has been observed. A queued/absent status is not a pass.

### Exact next steps, in order
1. On native Mac Studio:
   ```sh
   cd ~/Projects/AIVERSE
   git fetch origin main perf/incremental-static-model-cache feature/strata-virtual-geometry
   git worktree add --detach ../AIVERSE-strata-stage6 origin/feature/strata-virtual-geometry
   cmake -S ../AIVERSE-strata-stage6 -B ../AIVERSE-strata-stage6/build/strata -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_STRATA=ON
   cmake --build ../AIVERSE-strata-stage6/build/strata --target aiverse_strata_builder_tests aiverse_strata_model_adapter_tests aiverse_strata_view_tests -j 8
   ctest --test-dir ../AIVERSE-strata-stage6/build/strata -R '^strata_(builder|model_adapter|view)$' --output-on-failure
   ```
2. Fix compiler/test issues and record native results, including exact logs and platform, with implementation commit and separate handoff commit.
3. Add `GeometryScene` recording-Device OFF/ON draw parity tests, including source-node transforms and shadow-only visibility; ensure root-fallback coverage never draws a source primitive twice.
4. Design and implement a versioned auxiliary index/cluster metadata allocation managed by existing `GlobalGeometryPool` and RHI lifetime fences. Keep batch material/indirect command ownership in `Renderer3D`, with an exclusive Strata/legacy decision per eligible primitive.
5. Add GPU frustum/LOD/Hi-Z compute path with CPU `strata::planView` parity oracle, platform capability gates and CPU fallback. Only then performance benchmarks for >=1k/10k/100k instances.

### Development coordination
Batching branch ownership is separate. Its live head can advance after this Stage 6 snapshot; verify newest PR #34/head before another merge. Keep feature flags default OFF and PR #35 Draft; do not merge either experimental code path into main until native and cross-platform validation is complete.


---

## Stage 7A — GPU cluster metadata anchored to GlobalGeometryPool (2026-11-19)

### Branch and exact implementation commits
**Branch:** `feature/strata-virtual-geometry` (Draft PR #35 stacked on active batching PR #34).
- `cffb601ee04cf9c05bb1213c88e958302276ad1a` — new `engine/render/StrataGpuScene.h`: versioned GPU metadata storage layouts and per-scene staging interface.
- `7a2baf58249b7d67e2edd4a8f6139b219c691816` — new `engine/render/StrataGpuScene.cpp`: pure transactional metadata packer, existing-shared-pool admission, metadata buffer allocation, geometry revision/handle validation, explicit teardown.
- `46e5dc2d0a50171e1e11841327c4e091561fafec` — fix validation order before accessing a possibly empty cluster tree.
- `2cc8720847dc999cc4076653f2e5966707ef7b54` — new `tests/render/strata_gpu_scene.cpp`: binary layout/offset, material/root/child IDs, missing residency flags and malformed source cases.
- `662e082ecd7be89554bcb1af42b83a3581be3dd3` — `CMakeLists.txt`: new `aiverse_strata_gpu_scene` and `aiverse_strata_gpu_scene_tests` / `strata_gpu_scene` target; runtime linked only if `AIVERSE_BUILD_STRATA=ON`.
- `8615c55f2d138956884149cecfff8229356002fc` — `engine/render/GeometryScene.h`: GPU metadata feature toggle, query, statistics.
- `03df91ed51724b36df9ff7ab7e56c766a4e6306a` — `engine/render/GeometryScene.cpp`: stage metadata after ordinary `uploadModel`; on shutdown destroy metadata before source models.
- `f91798d35472ecb5280045da0da0b9baafda11f7` — `docs/strata.md` updated with layout, lifecycle and feature gates.
- `bf15b90723d65da1c9d62b91d6f7fd4a2aed6ed2` — `AGENTS.md` requires same shared geometry pool and forbids using source-local indices as pooled GPU draw offsets.
- `668b37d3d888eb18bebc39159ad97ad65095e239` — `tests/render/strata_gpu_scene.cpp` recording RHI fixture: use real `uploadModelToGeometryPool`, reject nonpooled model, allocate metadata once, invalidate on `markGeometryChanged`, shutdown both metadata and shared original GPU page.

### Current functionality
`strata::packGpuMetadata(ModelAsset, vector<uint8_t>&, error)` builds one version-1 packed Storage buffer payload:
- 32 B `GpuMetadataHeader`; 32 B `GpuPrimitive` per asset partition; 64 B `GpuNode` per hierarchy node; 4 B root global indices; 16-byte total padding.
- Validates nonempty and DAG-free forest, material partitions, bounds and index ranges; 32-bit overflow and 64 MiB temporary payload bound. Pack remains transactional on invalid input.
- GPU node `sourceFirstIndex` is deliberately **ASSET-local**, not a ready GPU index buffer offset. `GpuNode::resident` is a CPU flag snapshot, **not** proof of actual GPU-resident triangles.

`strata::GpuMetadataScene` is experimental metadata **only**. It uploads via `gfx::Device::createBuffer(BufferType::Storage, Usage::Immutable)` after checking an existing `Model::geometryPoolPage`, shared valid VB/IB, nonempty pool allocations, Metal or Vulkan backend and compute capability. It never allocates source geometry or calls `drawIndexed`; `Renderer3D` remains the single draw owner. Allocations are bounded to 1 MiB per source and 8 MiB per scene by default. Model pointer, `geometryRevision` and GPU VB/IB handles are rechecked for each `find`; stale records require explicit scene reload instead of in-place overwrite.

`GeometryScene::setStrataGpuMetadataEnabled(true)` enables this path **only** if `setStrataStagingEnabled(true)` also ran before scene loading. Both default OFF. `strataGpuMetadataBuffer(sourceKey)` returns the optional metadata handle or invalid. `stats()` adds metadata uploaded/declined/failed source counts and metadata bytes. `shutdown(dev)` frees metadata buffers before original source models and the device. Old scene JSON, ECS, save files, materials, draw calls, dynamic rendering and HLOD remain unchanged.

### Important blocker — geometry residency NOT implemented yet
The existing `GlobalGeometryPool` is shared by legacy models and handles full vertex/index pairs. It does not expose an API for adding *auxiliary Strata index-only spans* to a currently uploaded source without a versioned reservation, frame-complete retirement and overlap-safe writes. Stage 7A therefore uploads only metadata, and refuses dedicated/nonpooled sources. It does **not** publish GPU-resident Strata meshlets/indices, call compute culling or issue indirect draws. Never pass Strata's `sourceFirstIndex` to existing pooled `Model::ib`. A model with `Model::geometry` (older GeometryCluster path) is intentionally rejected from the current rigid shared pool; do not bypass this policy without a proven safe compatible representation. Metadata buffer availability alone must never activate a Strata render path.

### Verification actually done
- GitHub connector writes succeeded, file/API contracts were inspected. No changes were made to the active batching branch.
- **Native CMake compile: NOT RUN. Focused CTest: NOT RUN. Mock RHI fixture: NOT RUN. Metal/Vulkan hardware/raster test: NOT RUN.** This runtime does not have an AIVERSE checkout, and direct GitHub access from the local container fails DNS. The test code exists but has not been executed.
- GitHub Actions earlier Stage 6 head `db597cc7`: main CI and GPU batching checks were **cancelled**, not passing. Stage 7 code head `03df91ed`: main CI observed **pending**, other workflows queued. Re-check current head, not historical runs.

### Immediate next actions
1. On Mac Studio fetch feature branch, build and run all four focused tests:
   ```sh
   cd ~/Projects/AIVERSE
   git fetch origin main perf/incremental-static-model-cache feature/strata-virtual-geometry
   git worktree add --detach ../AIVERSE-strata-stage7 origin/feature/strata-virtual-geometry
   cmake -S ../AIVERSE-strata-stage7 -B ../AIVERSE-strata-stage7/build/strata -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_STRATA=ON
   cmake --build ../AIVERSE-strata-stage7/build/strata --target aiverse_strata_builder_tests aiverse_strata_model_adapter_tests aiverse_strata_view_tests aiverse_strata_gpu_scene_tests -j 8
   ctest --test-dir ../AIVERSE-strata-stage7/build/strata -R '^strata_(builder|model_adapter|view|gpu_scene)$' --output-on-failure
   ```
   Fix all compilation warnings/failures, report actual results in another implementation + handoff commit.
2. Add a real `GeometryScene` recording-device OFF/ON parity test, verify pooled vs dedicated model diagnosis and unload/reload with multiple worlds, material variants and shadow passes.
3. Coordinate with batching agent on **extending the existing GlobalGeometryPool** for bounded, versioned and frame-fenced *auxiliary index spans*, not a second VB/IB source pool. Preserve GPU allocation failure rollback, model revision and shared pool admission.
4. Only after sidecar indices are resident implement GPU scene instance records and compute frustum/LOD, with `strata::planView` as oracle and a **single mutually exclusive render submission owner**. Record real GPU timing and visual parity.
5. Check newest `perf/incremental-static-model-cache` head before syncing. In the previous stage it advanced while Strata was running; do not blindly overlay its files, handoff or CMake.

### Coordination and risks
PR #35 remains a Draft stacked on PR #34. Keep all GPU/ICB experimental flags OFF by default, do not merge into main until native CTest + platform validation. Metadata GPU buffers must be released while their owning gfx::Device is still alive; the ordinary backend is responsible for keeping in-flight references alive when destroyBuffer is called. No real GPU compute, index-stream residency or render acceleration has been demonstrated.


---

## Stage 7B — pooled auxiliary Strata index residency (2026-11-19)

### Resume point
Branch `feature/strata-virtual-geometry`, Draft PR #35 stacked atop the independently evolving `perf/incremental-static-model-cache` Draft PR #34. Do not merge either experimental branch into `main` without actual native and visual parity results. **Stage 7B is source-attached sidecar index residency, NOT GPU compute culling or Strata indirect rendering.** All experimental opt-ins stay disabled by default.

### Exact code commits, in order
- `781a624ee3d459503c0cc14a0439909190d1dfd7` — `engine/render/GlobalGeometryPool.h`: add move-only `GeometryPoolIndexSpan`, `uploadAuxiliaryGeometryIndices`, `auxiliaryGeometryIndicesValidFor`, explicit `releaseAuxiliaryGeometryIndices`, and separate sidecar counters.
- `7a87f98ca0020b5a81b6765e87faa89ed3e960a4` — `engine/render/GlobalGeometryPool.cpp`: allocate index-only sidecars in the *existing* model page, synchronized with `poolMutex`, check all source-relative indices, reuse released ranges only with verified GPU completion and recycling opt-in, retain page lifetime until last Model or sidecar owner releases; no second GPU page.
- `c54d33abe88f367695ae89725c2ace93b8168f4d` — `engine/render/StrataGpuScene.h`: `GpuPrimitive::gpuFirstIndex`, binary metadata v1/v2 mapping, per-source/scene index budgets, read-only `GpuMetadataScene::indexBinding`.
- `fa935f01ad062fffa88faa7c2f911b765550e836` — `engine/render/StrataGpuScene.cpp`: concatenate per-primitive Strata indices, upload into source shared page, version-2 GPU metadata patch with absolute per-primitive base indices, joint lifetime and allocation failure cleanup.
- `5f5dc552e27ab5269cba14b2389c530443946123` and `1dd5ecf968cb4e221d3d358f217af2cbd4e4d708` — `GeometryScene.h/.cpp`: `strataGpuIndexBinding(sourceKey, out)` and index residency stats; draw path unchanged.
- `3931b1e4b087de3b1acf872850694fdf8237ee9a` — `tests/render/strata_gpu_scene.cpp`: synthetic recording-RHI v2 payload, full actual pooled byte comparison, source revision invalidation, checked GPU-write rollback, lifetime across Model release.
- `268fcd5e06873ae73465927303964e44582c7f25` — same fixture: no additional GPU page on index allocation exhaustion; no corruption of a second ordinary pooled Model.
- `b108001f4abda9b003b03e693dc2fa38eb417df0` — `GlobalGeometryPool.cpp`: count actual model leases separately from sidecar leases, preserving meaningful `GeometryPoolStats::liveModels`.
- `83da15840341c1b2d02e78ec41afe32e5c8816aa` — test explicit independent source model and auxiliary span ownership counts.
- `71616a0b212c0aabcfbc376dcf1a83b71cc8e2a4` — Strata GPU header comment corrected to state v2 sidecar residency behavior.
- `4761639fd7f5acb08816b08556dd4665b607d439` — `docs/strata.md`: new shared-pool residency API, metadata v2, lifetime/budget/per-pass offsets, limitations and native checklist.
- `15548c847c33614b4d84cf22571794464b01103b` — `GlobalGeometryPool.cpp`: clarify last-owner teardown and completed-serial diagnostics.
- `1f2ecc55f7146df11e395cd1dbc93430a274d574` — `AGENTS.md`: required single-pool, v2 format and no-double-draw rules.
- `986d633056d293979ea145578cf4f8ab93c6f445` — `tests/render/strata_gpu_scene.cpp`: simulated frame serials confirm a released GPU index range is quarantined before completion and optionally reused only after the completion fence.

### Architecture and API contract
`GeometryPoolIndexSpan` is **move-only**, retains the existing `GeometryPoolPage` with shared ownership, stores the pooled GPU index-buffer handle, absolute uint32 `firstIndex` and `indexCount`, source Model pointer identity (not dereferenced), `geometryRevision`, GPU VB/IB and model base offsets. The source Model's original vertex buffer is used unchanged. `uploadAuxiliaryGeometryIndices` accepts only an already pooled, valid immutable model and source-relative indices, checks the page belongs to the same device, and writes into unused space between `vertexEnd` and `indexBegin`. It never allocates a new Strata page. Failed checked RHI uploads do not commit a sidecar lease or advance the page frontier. A full page rejects the sidecar and legacy rendering continues. Call `releaseAuxiliaryGeometryIndices(dev, span)` before Device shutdown; the sidecar may legitimately outlive its source Model lease, preventing premature destruction of the existing shared GPU page.

Only the pre-existing `AIVERSE_GEOMETRY_POOL_RECYCLE=1` opt-in permits future reuse of a retired index-only span; reuse additionally requires nonzero `completedFrameSerial >= retireFrame` from a healthy device. In absence of a trustworthy serial, released spans stay stranded until the GPU page itself is destroyed, intentionally favouring safety over memory reuse. Sidecar accounting is distinct from Model count; `GeometryPoolStats` has `auxiliaryIndexSpans`, `auxiliaryIndexBytes`, retired/reusable span counts and recycled auxiliary uploads.

`GpuMetadataScene` now stages a complete source-level concatenated Strata index stream on the original page (defaults: <=1 MiB per source, <=8 MiB per scene). If upload succeeds, v2 GPU metadata records each primitive's `gpuFirstIndex` as an **absolute pooled GPU index offset**. The node-level `sourceFirstIndex` remains local to that primitive; the future GPU command offset is **primitive.gpuFirstIndex + node.sourceFirstIndex**, with the original Model's `gpuBaseVertex` used for vertex addressing. The pure CPU `packGpuMetadata` still emits v1 (gpuFirstIndex invalid); this metadata cannot be submitted for drawing. The source page, sidecar and metadata have linked lifetime. `GpuMetadataScene::indexBinding` and `GeometryScene::strataGpuIndexBinding` return read-only handles/ranges only while Model identity, revision, shared allocation and GPU handles remain valid; errors preserve caller output.

`GeometryScene::setStrataStagingEnabled(true)` and `setStrataGpuMetadataEnabled(true)` must both be called before load (both default OFF). `GeometryScene` releases Strata GPU metadata and pooled index spans **before** releasing the owning legacy models. No shaders, per-instance GPU transforms, computation, occlusion/Hi-Z, draw commands, physics, ECS, save schema or genre-specific renderer code are changed. The existing `Renderer3D` and CPU/GPU batching paths remain the only draw authority.

### Actual validation (do NOT report PASS)
- Source changes and exact commits confirmed by connected GitHub writes. The batching agent's branch was **not** modified.
- **Native compile: NOT RUN. CTest (all Strata targets): NOT RUN. Recording fake RHI test: NOT RUN. Actual Metal/Vulkan execution, GPU fences, visual parity, memory/perf measurements: NOT RUN.**
- GitHub Actions for code head `1f2ecc55` were observed **queued**; not passing. Recheck current head after this handoff commit. Historical queued/cancelled validation is not proof of correctness.
- The recording-RHI test now includes a simulated monotonic `frameSerial` and `completedFrameSerial`. Its presence does not constitute execution.

### Immediate native verification
```sh
cd ~/Projects/AIVERSE
git fetch origin main perf/incremental-static-model-cache feature/strata-virtual-geometry
git worktree add --detach ../AIVERSE-strata-7b origin/feature/strata-virtual-geometry
cmake -S ../AIVERSE-strata-7b -B ../AIVERSE-strata-7b/build/strata \
  -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_STRATA=ON
cmake --build ../AIVERSE-strata-7b/build/strata \
  --target aiverse_strata_builder_tests aiverse_strata_model_adapter_tests \
  aiverse_strata_view_tests aiverse_strata_gpu_scene_tests -j 8
ctest --test-dir ../AIVERSE-strata-7b/build/strata \
  -R '^strata_(builder|model_adapter|view|gpu_scene)$' --output-on-failure
```

Then build actual runtime and exercise source sharing + OFF/ON pixel parity on Metal; perform Vulkan parity where supported, HLOD/shadow/reflection passes, memory churn, frame-serial fence/retire under real GPU load and multi-world unload before activating any Strata draws. Fix native compile/test issues and commit code + separate handoff on the **same branch**.

### Next bounded code step
After native builds/tests pass, add an **opt-in, mutually exclusive** Strata submission route in the existing `Renderer3D`, using selected cluster ranges and correct shared buffer + base-vertex mapping; never draw an original primitive twice. Implement pass-specific GPU scene instance records and compute frustum/LOD with CPU `planView` as oracle, then Hi-Z and async streaming only after measured parity and safe fences. Avoid changing existing GPU ICB defaults or duplicating `GlobalGeometryPool` / ECS transforms.

### Known risks
- The existing GPU pool still uses a bounded, page-local, first-fit free-gap allocator; a sidecar can fail even if other pages have available memory (no second page is allocated for this source). Retired index ranges may strand memory when recycling is OFF or serials unavailable, by design.
- The sidecar model lease is explicit and move-only, not a self-destroying GPU RAII object; callers MUST invoke release before Device teardown.
- Hierarchical LOD may still crack or alter attributes across cluster boundaries. No measured speedup, GPU mesh shading or GPU indirect draw has been proven.
- The active batching branch has advanced considerably during this task. Do not blindly overwrite its `GlobalGeometryPool.cpp` work; re-diff current batching head and reconcile it before a later merge. PR #35 remains stacked Draft on PR #34.


---

## Stage 8 — Metal diagnostic GPU cluster culling (2026-11-19)

### Resume state and precise commits
**Branch:** `feature/strata-virtual-geometry`. PR #35 Draft, stacked on `perf/incremental-static-model-cache` PR #34 (that separate branch was still evolving; do not overwrite its files). No merge to main. Main legacy renderer still owns ALL draws; Strata GPU selection is off by default.

- `8dfcf4f5479e4730e165ccad754c1e08f46a9007`: new `engine/render/StrataGpuCulling.h` with 80-byte `GpuSceneInstance`, 32-byte selected-range record, 16-byte atomic counters, 96-byte pass uniforms, per-frame submission descriptors, culling lifecycle.
- `8b6bf01418ed65f6d5f586cdd81eefad33125f2c`: new `engine/render/StrataGpuCulling.cpp` with experimental Metal compute shader, hierarchy traversal, parent-residency fallback, projected pixel error, frustum selection, atomic bounded output and no draw submission.
- `1b91431dcca08a5d44bf4f2c1be83920180d8465`: make GPU frustum near-plane clipping match the existing conservative `geometryBoxVisible` CPU reference (not a replacement for exact hardware clipping).
- `ab811a3de4c3a589904e563c4d1fa59cf42269e6` and `e987b734c12dd58a8ae7ec28cc5694ed81ed0764`: distinguish GPU `GpuPassConfig::viewProjection` from CPU `ViewConfig::objectToClip`; add `makeGpuSceneInstance` and `cpuReferenceView` utilities to prevent double-applying world transforms.
- `23fdb1fad2f9ecc5264004bc1bb2f28afd2dc3aa`: new `tests/render/strata_gpu_culling.cpp` with recording gfx::Device, matrix helper, viewport uniform, Metal-only gate, multi-dispatch same frame and no-draw assertions.
- `0d051e01b8e999857a9c375b46e05f7e7fa667db`: CMake static target `aiverse_strata_gpu_culling`, CTest `strata_gpu_culling`.
- `3cb86a52506336b59c949f46a943251447447d62`: conservative frame reuse; if previous 3-frame slot has no known completed GPU serial, GPU diagnostic selection declines rather than overwriting in-flight data.
- `2c5bc99a4478d7353ac28eed132d8756db3bf289`: new `.github/workflows/strata-focused.yml`, targeted Ubuntu/macOS compile and five Strata CPU/mock CTests.
- `b08a684f5e7ed99cc742aac7d8cfe1238474f73a`: `GeometryScene.h` diagnostics API: collect staged static instances and request opt-in GPU per-pass selection.
- `21730654aee6b9fb1433bf476c67a1178a2ae1b5`: `GeometryScene.h` stores borrowed owner gfx::Device pointer for dispatch identity check.
- `7e2e015254bb3a6e2dde11f45c728718ea31f801`: `GeometryScene.cpp`, collect source Model node/world transforms into GPU scene records; dispatch diagnostic compute if metadata v2 and all flags are opted in, no draw calls.
- `a6a9e109bdefb4a1ad28c263764ec1ddb5416816`: runtime links `aiverse_strata_gpu_culling` when `AIVERSE_BUILD_STRATA=ON`.
- `b137a9e229a16ca32f4aa16900db2659ef5b11ec`: fix `GeometryScene::shutdown` incomplete GPU class method calls for `AIVERSE_BUILD_STRATA=OFF` by guarding hardware-specific method references.
- `e7a08912d0b967f38b8154eb4baa9b8670640cae`: `docs/strata.md`, Stage 8 public contract, example, limitations, validation gates.
- `612acabb7e2c12245e48bd1db4c79dd9ecbf34e1`: `AGENTS.md` ownership, no-render rule, GPU matrix semantics, index base safety.

### Implemented and architecture
The GPU culling stage is **not yet production GPU-driven drawing**. `GpuClusterCulling::dispatch()` is a standalone Metal-only diagnostic compute call: it takes one `GpuMetadataScene` **version 2** storage buffer and an array of GPU scene instances for that source. It stages bounded instance matrices and a buffer of selected cluster ranges; the kernel traverses the encoded tree for each instance using a fixed 128-entry stack and chooses parent clusters when descendants are missing. The output is atomic-compacted, nondeterministically ordered, and reports overflow/invalid/traversal counters. Every range contains a valid-looking *candidate* absolute pooled index offset, but the `Renderer3D` batching/material/shadow pipeline is **never invoked by this module**. It never calls GPU indirect draw or reads GPU results back during normal rendering.

`GpuPassConfig::viewProjection` is pass-level ONLY; `GpuSceneInstance::objectToWorld` contains the source node transform (including instance and bind hierarchy). `cpuReferenceView(pass, instance)` composes these for the CPU `planView()` oracle. The GPU path is bounded to 4096 primitive instances, 65536 output clusters and 64 independent submissions per GPU frame slot. If a slot may still be used by the GPU and no completion serial proves retirement, dispatch returns false. Staging/culling are independent of game genres, ECS authoritative state, dynamic gameplay and other render passes.

`GeometryScene::setStrataGpuCullingEnabled(true)` is a **third explicit opt-in**, in addition to CPU staging and pooled GPU metadata/index staging. The scene can construct static instance+node transform records for a source with `collectStrataGpuInstances`, and trigger a compute pass with `dispatchStrataGpuView(device, sourceKey, pass, device.frameSerial(), result)`. This API is for use between beginFrame/endFrame and **outside** any render/compute pass. Its metadata must be owned by the same Device; the scene guards against a different device instance. All source models, materials, shadows, HLOD, GI, legacy batching, draw calls and GPU pool allocations remain managed by previous systems. No scene JSON or save format changes.

**Critical failure semantics:** a successful `dispatch()` means only that the RHI accepted compute encoding, not that visible ranges are correct or safe to submit. A nonzero GPU `GpuSelectionCounters::overflow` or `invalid` marks the entire result unusable for rendering. Atomic output order is unspecified; sort by `instanceToken/nodeIndex/primitiveIndex` for GPU vs CPU parity checks. Unlike CPU's global output budgeting, GPU overflow does not recover by selecting coarser parents: on overflow legacy rendering must remain the fallback. Never modify ordinary draw counts from this result.

### Actual validation status
- Connected GitHub API accepted each source and documentation commit and reported the feature HEAD.
- Native C++ compilation: **NOT RUN**. `strata_gpu_culling` recording RHI test: **NOT RUN**. Existing four Strata CTests: **NOT RUN** in assistant environment. Real Metal shader compilation and execution: **NOT RUN**. GPU async counters/readback parity with CPU: **NOT RUN**. Vulkan compute, Hi-Z, real A/B screenshots, frame-time benchmark: **NOT RUN**.
- GitHub Actions `Strata focused CPU and GPU-dispatch contracts` appeared **QUEUED** for code head `612acabb`; no PASS/FAIL result available. Other CI jobs also queued. Do not mark any unexecuted work green.
- Full GPU command submission remains purposefully unimplemented.

### Native validation required before further rendering
```sh
cd ~/Projects/AIVERSE
git fetch origin main perf/incremental-static-model-cache feature/strata-virtual-geometry
git worktree add --detach ../AIVERSE-strata-stage8 origin/feature/strata-virtual-geometry
cmake -S ../AIVERSE-strata-stage8 -B ../AIVERSE-strata-stage8/build/strata \
  -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_STRATA=ON
cmake --build ../AIVERSE-strata-stage8/build/strata \
  --target aiverse_strata_builder_tests aiverse_strata_model_adapter_tests \
    aiverse_strata_view_tests aiverse_strata_gpu_scene_tests \
    aiverse_strata_gpu_culling_tests -j 8
ctest --test-dir ../AIVERSE-strata-stage8/build/strata \
  -R '^strata_(builder|model_adapter|view|gpu_scene|gpu_culling)$' \
  --output-on-failure
```
Then explicitly test `AIVERSE_BUILD_STRATA=OFF` with the ordinary runtime target, compile the dynamic MSL kernel on actual Mac/Metal, dispatch it with real metadata v2 after a valid `beginFrame`, read back asynchronously when the GPU frame completes, check overflow and invalid counters, sort output and compare every actual selected range against `strata::planView` for varied camera projections, thresholds, LOD levels, node transforms, shadows and residency changes. Verify 4096-instance cap, 65536-output cap, same-frame multiple views, recycled frame slots, and that DrawCalls/draw-model frame images are unchanged with diagnostics OFF/ON.

### Next bounded step
If native tests and GPU shader parity are clean, implement a single mutually exclusive Strata/legacy per-primitive selection owner inside the existing renderer (not another batching/geometry pool), with explicit opt-in and direct fallback on any mismatch/error. Do not enable GPU compute-authored indirect draws or Hi-Z until the exclusive source submission path and pixel parity are verified. Streaming/dynamic TerraWeave and cross-platform Vulkan compute remain future stages.

### Live batching dependency
PR #34 has advanced independently. Its newer commits are **not automatically merged** into this Stage 8 snapshot. Fetch current batching HEAD, compare changed GPU renderer files and notes, resolve any conflicting semantics and preserve its handoff before syncing. Neither agent should force-push or merge to main without shared native regression results.


---

## Stage 8.1 — Mac Studio compilation fix in Strata GPU culling test (2026-11-19)

**Branch:** `feature/strata-virtual-geometry`; draft PR #35 stacked on batching PR #34.
**Implementation commit:** `d71aa7790ca74260b2280b0ddd440bc238acc3a8`.
**Changed file:** `tests/render/strata_gpu_culling.cpp` only. Replaced three invalid `compute.computeCalls` / `compute.dispatchCount` field accesses with `dev.computeCalls` / `dev.dispatchCount`. Counters belong to the `RecordingComputeDevice` fake RHI; `GpuClusterCulling` has no such public counters. No production GPU code, rendering logic, CMake, ABI or asset formats changed.

### Actual native evidence supplied by owner
The user compiled on a Mac Studio using AppleClang / MacOSX26.2 SDK from worktree `~/Projects/AIVERSE-strata-stage8`:
- `aiverse_strata_builder`, `aiverse_strata_model_adapter`, `aiverse_strata_gpu_scene`, `aiverse_strata_view` and `aiverse_strata_gpu_culling` library targets: **BUILT** according to owner-provided output.
- Test executables: `strata_builder` **PASS 0.83s**, `strata_model_adapter` **PASS 0.38s**, `strata_view` **PASS 0.39s**, `strata_gpu_scene` **PASS 0.46s** (owner-reported CTest log, previous source snapshot).
- `aiverse_strata_gpu_culling_tests` **failed to compile** from exactly three invalid field accesses on lines 129 and 151; `strata_gpu_culling` was **NOT RUN**, CTest reported 4/5 passed. This patch addresses those exact compiler diagnostics.
- **Post-fix native build / `strata_gpu_culling` CTest: NOT YET RUN.** No new Metal compute execution, shader compilation/parity or screenshot evidence. Do not convert owner-provided pre-fix passes into post-fix claims.

### Next exact verification action (existing detached worktree)
```sh
cd ~/Projects/AIVERSE-strata-stage8
git status --short
git fetch origin feature/strata-virtual-geometry
git switch --detach origin/feature/strata-virtual-geometry
cmake --build build/strata --target aiverse_strata_gpu_culling_tests -j 8
ctest --test-dir build/strata -R '^strata_(builder|model_adapter|view|gpu_scene|gpu_culling)$' --output-on-failure
```
Do not discard uncommitted work if `git status` is nonempty. On success, append the actual output and test commit/OS to a fresh handoff section; then address real Metal kernel compile and GPU-vs-CPU selection parity. Keep Strata diagnostics OFF by default and main unchanged.


---

## Stage 8.2 — owner 5/5 CPU CTests; diagnostic async GPU readback and real-Metal parity harness (2026-11-19)

### Branch, scope and test evidence
**Branch:** `feature/strata-virtual-geometry`, PR #35 Draft stacked on batching PR #34. Main unchanged.

The user ran Stage 8.1 **before** the new Stage 8.2 implementation on Mac Studio at `~/Projects/AIVERSE-strata-stage8`. The native build produced `aiverse_strata_gpu_culling_tests` and CTest reported **100% tests passed, 5/5, 0 failed, 1.19 s**:
- `strata_builder` PASS 0.17 s
- `strata_model_adapter` PASS 0.17 s
- `strata_view` PASS 0.16 s
- `strata_gpu_scene` PASS 0.20 s
- `strata_gpu_culling` PASS 0.48 s

This is **real user-reported Mac native compilation / CPU + recording-RHI tests** for the older code, not a Metal compute parity pass or execution of this new Stage 8.2 code.

### Implementation commits
- `36636cbb1c06ca6fcd446922766be308ad04d82f`: `engine/render/StrataGpuCulling.h` adds `GpuSelectionReadback`, explicit statuses `Valid/Overflow/Invalid/ReadbackFailed`, asynchronous readback callback API, sorted CPU-vs-GPU parity oracle.
- `cb7d0a40a77be93033b05da6a089bc68850c6d6a`: `engine/render/StrataGpuCulling.cpp` implements two independent RHI asynchronous reads (counters and ranges) with owner-thread completion, fail-closed invalid and overflow, 2048-record/65536-byte Metal limit and `compareGpuSelection` sorted exact CPU oracle.
- `9716fee0e64089210d0d5dee3a8d08d24d58b248`: `tests/render/strata_gpu_culling.cpp` extends fake RHI for async callback sequencing, synthetic GPU selected records, GPU/CPU comparison, rejected overflow and oversized readback.
- `cab54aa847d5819430610aa95da00252396d0b08`: `tests/render/strata_metal_parity.cpp` new real Metal/AppKit platform fixture. Uses pooled source Model + metadata v2, GPU compute dispatch and asynchronous readback; checks five scenarios: two instances/fine LOD, coarse parent LOD, frustum rejection, disabled instance, capacity overflow. Never submits renderer draws. Fixture fails on MSL compile errors or GPU/CPU mismatch.
- `e10d0b3a37ed4037ed7872d845a5c02813879dd8`: `CMakeLists.txt` optional macOS target `aiverse_strata_metal_parity` gated by `AIVERSE_STRATA_METAL_GPU_TESTS=ON` (default OFF; intentionally not an unattended CTest because it opens a native Metal window).
- `7221b7fa9dd59346b731e8636c93d2fa342be247`: `engine/render/GeometryScene.h` additive async readback API.
- `52d4941f0008b18b939008eb6ad3588bf35a4442`: `engine/render/GeometryScene.cpp` delegates readback to scene-owned culler with gfx::Device identity/lifetime and opt-in gating. Compile-time Strata-OFF path is no-op.
- `210de63271823291fd4fe23c1ec6beaaad09fc69`: real Metal fixture stores a validated pooled GPU sidecar index base during setup, avoiding late mutable lookups in callbacks.
- `77002ea0473c329d417e5a1b4c6a36f4591beeef`: `docs/strata.md` readback contract, real Metal testing instructions, validation distinctions.
- `fc28f78d772835882beaac8a6346e030eaedf036`: `AGENTS.md` stages and prior owner-confirmed pass.
- `355044339ff142188a67619627e28a29bfbb9b3b`: `tests/render/strata_gpu_culling.cpp` additionally rejects nonzero `GpuSelectionCounters::invalid` with a complete-looking GPU prefix.

### API and correctness
`GpuClusterCulling::requestReadback(dev, selection, cb)` must run **during the same GPU frame and outside Metal encoders**, after a successful diagnostic `dispatch()`. It issues two `gfx::Device::readBufferAsync` copies. Metal supports at most 65536 bytes per request, so `GpuSelectionSubmission.capacity > 2048` fails immediately and reports `ReadbackFailed` to callback. GPU callbacks are delivered at a later `Device::beginFrame` on the device owner's thread. The shared callback state waits for both reads; malformed/incomplete, overflow or invalid counters do not publish drawable ranges. A scheduling failure reports failure once. Diagnostic only, no renderer submission.

`compareGpuSelection(source, instances, pass, pooledFirstIndex, gpu, error)` independently invokes `planView` for each enabled instance with `cpuReferenceView`, computes absolute pooled offsets from source primitive concatenation, translates local node IDs to global metadata IDs, sorts both CPU and atomic-order GPU output by full record keys and requires exact match. Duplicate enabled instance tokens, malformed source, GPU invalid/overflow or CPU budget fallback are rejected. It is a correctness ORACLE, not a production draw backend.

Real Metal fixture exercises `platform::run` and `gfx::createMetalDevice` through the existing engine runtime, no second RHI implementation. It enables only the pre-existing **CPU MDI admission** environment switch `AIVERSE_METAL_CPU_MDI=1` so its source can enter the shared `GlobalGeometryPool`; GPU-authored MDI/ICB and Strata renderer submission are NOT enabled. The fixture receives actual GPU results asynchronously, verifies expected LOD/frustum counts and CPU parity, and exits with failure on any issue. `GeometryScene` has a public readback convenience method for other diagnostic hosts; they MUST manage callback lifetime across scene unload.

### Validation as of this handoff
- **Previous Stage 8 Mac native focused tests: PASS 5/5** (user-supplied logs above).
- Stage 8.2 native compilation and all new fake RHI checks: **NOT RUN**.
- Actual Metal MSL compile, GPU dispatch, async readback and CPU/GPU parity: **NOT RUN**.
- GPU raster screenshots, no-double-render proof, Vulkan and performance benchmarks: **NOT RUN**.
- GitHub Actions was observed **QUEUED/PENDING** on Stage 8.2 intermediate commits; queued is not a passing result.

### Exact next commands (existing worktree on Mac)
```sh
cd ~/Projects/AIVERSE-strata-stage8
git status --short
git fetch origin feature/strata-virtual-geometry
# Only if your worktree has no unsaved changes:
git switch --detach origin/feature/strata-virtual-geometry
cmake -S . -B build/strata -DAIVERSE_BUILD_TESTS=ON \
  -DAIVERSE_BUILD_STRATA=ON -DAIVERSE_STRATA_METAL_GPU_TESTS=ON
cmake --build build/strata --target aiverse_strata_gpu_culling_tests \
  aiverse_strata_metal_parity --parallel 8
ctest --test-dir build/strata \
  -R '^strata_(builder|model_adapter|view|gpu_scene|gpu_culling)$' \
  --output-on-failure
build/strata/aiverse_strata_metal_parity
```

On any compiler error, shader pipeline error or failed GPU/CPU comparison, save the exact output, update the implementation and append a **new handoff section with actual results**. Inspect Metal shader compiler diagnostics, frame serials and GPU counters first. The real fixture must print five PASS scenario lines and `cases=5/5 status=PASSED` before claiming stage 8.2 GPU parity. Those are future required results, NOT current achievements. Preserve diagnostic-only mode and the legacy Renderer3D draw owner. Next work after real parity: randomized passes/transforms and conservative CPU/GPU pixel parity before mutually exclusive submission and Hi-Z.


---

## Stage 9 — exclusive renderer source routing preflight (2026-11-19)

### Current branch and upstream integration
**Working branch:** `feature/strata-virtual-geometry`; PR #35 remains Draft. PR #34 for `perf/incremental-static-model-cache` was merged into `main` at commit `58e29ab052458fffcdebebffe27022b53adb9367`. This Strata branch includes the new `main` using a **two-parent merge** `a77cd830e8dee751658028786066111bc8e65f6a` while keeping all Strata files and latest batching changes (`GfxMetal.mm`, `Renderer3DModels.cpp`, `AutoCpuBatchPolicy.h`, material batching fixture and batching handoff/script). No changes were made to the batching branch or main by Strata. PR base should now be `main`, not the already merged batching feature branch.

### Exact implementation commits
- `a77cd830e8dee751658028786066111bc8e65f6a`: actual merge from current batching `main`.
- `2930878b938b4a30743f94cf8f1af5e203ce3491`: new `engine/render/StrataRenderRouting.h`, exclusive source route decision, per-pass geometry-only candidates and common indexed indirect ABI.
- `8cd26285c19863a0fd9c6a8695291d9b4e01a85b`: new `engine/render/StrataRenderRouting.cpp`, strict active-source preflight, CPU `planView()` selection and pooled sidecar index translation; no draw calls.
- `0c3ead9339db925877f7444d8b0e0f71742e9a0e` / `e70fa64ad77589aba7efaaef625e36c9ebb320bc`: tie a command candidate to the exact Model identity, geometry/material revisions and pooled buffer handles at encoding time.
- `63632145689003fdef185a7ff66dc49a8912b7d4`: decline existing material and shadow batching ownership, in addition to skinned/morphed, alpha, vertex displacement, variants, material overrides and authored LOD.
- `76fb03c2ee1859586392abc14be8b850e1affbfd`: new `tests/render/strata_render_routing.cpp` recording-RHI route preflight fixture.
- `c86fe0110c8ad835954813d4c1ec96c8006a500c`: new CMake library `aiverse_strata_render_routing`, test executable `aiverse_strata_render_routing_tests`, CTest `strata_render_routing`; runtime links the preflight library only when Strata is compiled.
- `94456211a4eac216c0594bb3a0047f19f1d9fade` / `cbc65002a1ae99f7c4ba19cf215b161acd2ffeef`: additive `GeometryScene::prepareStrataRenderCandidate` API; reads scene-owned source and staged metadata but does not alter `GeometryScene::draw`.
- `a8d0a3b1a969fec2797906e0005d9522314ca72e` / `82e7cea293b2c021ae98dd6fff1015c1a0a98cb7`: always check a **fresh live `GpuMetadataScene::indexBinding`** during command encoding, preventing reuse after GPU sidecar retirement. Re-check live material/geometry compatibility even when a caller forgot to mark a material revision.
- `df20b874f99c04780d2c8cf1b703dc6ebaa753eb`: extend recording RHI tests for live lease checks.
- `4a4462001ae4ee2e11f453caa0665dd186e973a7`: update `.github/workflows/strata-focused.yml` to compile/test all six focused Strata targets.
- `f0272ecdf1b0f05b31a265571880c60a3498d77e`: regression cases for missing sidecar, changing a material alpha without a revision increment, and existing shadow-batch ownership.
- `54fb33b73d601762bffdccc320abeb604a20d3f9`: `docs/strata.md` stage 9 API, gated route and no-live-draw policy.
- `ee9cd11e116c3540bf5df5008bf678e7783df739`: root `AGENTS.md` ownership notes, no-double-render guard and merged batching provenance.

### Architecture/API contract
`strata::prepareRenderCandidate(const Model&, const ModelAsset&, const GpuMetadataScene&, sourceKey, mesh, primitive, pass, ViewConfig, RenderCandidate&, error)` requires a **currently live** source model, matching per-source metadata v2 and existing `GlobalGeometryPool` auxiliary index lease, rigid nonanimated/nondeformable object, opaque compatible material, no material batching, variant, explicit authored LOD, or shadow batch that would duplicate submission. It calls per-pass CPU `planView` and rejects budget fallbacks not representable by the GPU output. The candidate includes actual `gfx::BufferHandle` identity and ranges with **absolute pooled GPU index offsets**, original pooled base vertex, source geometry and material revision. Zero ranges after conservative frustum culling is a legitimate Strata-only skip, not permission to also draw the legacy primitive. It never submits anything.

`strata::RenderRouteGate` has six explicit opt-in conditions: requested, source/material safety, source residency, CPU/GPU parity, raster parity and Renderer3D submission ownership. `chooseExclusiveRenderRoute` defaults to `Legacy` unless **all six** are true. There is no supported path for simultaneously drawing Strata and legacy for the same primitive/pass. In production, these bits MUST come from proven actual tests and renderer control flow; simply setting them to true is not validation.

`encodeRendererCommands(liveModel, liveGpuMetadataScene, candidate, rendererInstanceSlot, gate, output)` requires a real renderer-owned per-draw uniform slot. It rechecks live Model identity/revisions/vertex+index handles and **fresh** source sidecar binding before preparing shared 20-byte `gfx::DrawIndexedIndirectCommand` records. Every selected range must be inside the active GPU lease, triangle aligned and have the correct material/base vertex. It is transactional: error preserves caller output. This only *constructs* command bytes and never calls `drawIndexed` / `drawIndexedIndirect`, sets pipeline/uniforms, or suppresses legacy ModelDraw. Renderer3D still owns all renders, material grouping, shadows, GI and CPU/GPU batching. No GPU culling was made authoritative.

`GeometryScene::prepareStrataRenderCandidate` exposes the existing staged source to this helper behind explicit CPU/GPU Strata staging. The caller supplies the exact pass-specific object-to-clip matrix including the node transform. Do not use a shared main-camera selection for sun shadows, reflection, portal or stereo passes. The preflight is per primitive and per pass; use `Legacy` on unsupported backend/material/model or missing/off GPU residency.

### Actual test evidence (not inferred)
- **Owner-provided Mac Studio result from earlier Stage 8.1:** 5/5 focused CPU and recording-RHI CTests PASS in 1.19 s. This remains valid only for that prior code snapshot.
- New Stage 8.2 Metal async readback, actual real-Metal parity runner and **Stage 9 route preflight**: native build/CTest **NOT RUN in this session**. No actual GPU/CPU parity or screenshot validation has been furnished. Do not claim Stage 9 tests PASS because a fake RHI fixture was added.
- GitHub Actions for intermediate current-feature commits are queued/pending, not yet a verified PASS. The assistant has no AIVERSE checkout or macOS Metal GPU execution capability.

### Immediate native commands (existing Mac Studio worktree)
```sh
cd ~/Projects/AIVERSE-strata-stage8
git status --short
git fetch origin main feature/strata-virtual-geometry
# Only when status shows no uncommitted work:
git switch --detach origin/feature/strata-virtual-geometry
cmake -S . -B build/strata \
  -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_STRATA=ON \
  -DAIVERSE_STRATA_METAL_GPU_TESTS=ON
cmake --build build/strata --target \
  aiverse_strata_render_routing_tests aiverse_strata_gpu_culling_tests \
  aiverse_strata_metal_parity --parallel 8
ctest --test-dir build/strata \
  -R '^strata_(builder|model_adapter|view|gpu_scene|gpu_culling|render_routing)$' \
  --output-on-failure
build/strata/aiverse_strata_metal_parity
```
The real Metal fixture MUST actually print five scenario PASS lines and `cases=5/5 status=PASSED` before GPU selection parity is declared. Compile and test `AIVERSE_BUILD_STRATA=OFF` as a separate portability check; this branch is otherwise experimental and default-off for all runtime Stage 7+ flags.

### Next bounded step for a subsequent agent
1. Fix any native compiler/CTest errors reported for Stage 8.2/9 with a code commit, then append a separate handoff including the actual logs.
2. Run and validate the real Metal GPU `strata_metal_parity` fixture (not just the fake RHI tests), then expand to random camera/LOD thresholds, scene node transforms, materials, shadow pass and multiple instances. Record exact compared counters/ranges and GPU timeline if available.
3. Once *real GPU-vs-CPU* and raster parity are independently confirmed, add a single exclusive Strata route inside existing `Renderer3D::renderModels` / `drawGeometryBatch`, not another renderer. Per-primitive visibility, material state, instance uniforms and material/shadow batch roles must be owned by the existing pipeline. Direct fallback on any error or unsupported pass. Never submit both old and Strata indexed geometry in one pass.
4. Only afterward implement compute-to-ICB indirect submission, Hi-Z and streaming; preserve GLES/WebGL and non-Metal legacy behavior.

### Risk register
- Stage 9's command output is **preflight**, not rendering. Boolean route gates are declarations by a future renderer caller, not evidence from GPU.
- Metal GPU buffer/metadata v2 must stay alive through all GPU frames that reference any selected index span, including async diagnostic readback.
- Auxiliary Strata index ranges live on the existing model GPU page; never copy a stale candidate after device, source or material revision change.
- CPU LOD simplification may have visual seams/cracks under different depths; texture parity remains unverified.
- PR #35 remains Draft. The batching PR #34 was already merged; rebase the PR base onto `main` after this exact upstream integration and continue writing per-stage handoffs.