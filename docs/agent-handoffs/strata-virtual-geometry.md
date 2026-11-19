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
