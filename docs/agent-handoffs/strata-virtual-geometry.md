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
