# TerraWeave — AI agent handoff

Last updated: 2026-11-18. Branch: `feature/terraweave`.
Base: `feature/universal-entity-inspector-fps`.
Owner request: implement generic terrain/trees/foliage for AIVERSE; **update this file
at the end of EVERY development stage** so a different AI can pick up immediately.

## Stage 1 — generic generation core (IMPLEMENTED, build NOT VERIFIED)

Changes committed to branch (each is an individual commit):
- `fc9acfabac8969872b7dd7821612e530545f4757`: `engine/world/TerraWeave.h`;
  integer signed value-noise terrain, seeded climate/biome classification,
  tile generation, world-global foliage scatter, exclusion rectangles,
  two ECS-ready patch descriptors.
- `7ebf0f3dc3405ab1d6240510df31a56c4bc25fb8`:
  `tests/terraweave_test.cpp` tests seams, negative chunks, deterministic
  foliage distribution and input-order independence, thread-safe pure sampling,
  settings validation.
- `94cbd6553b3ca797542baa02c9f852b7073b4a08`:
  `CMakeLists.txt` registers `aiverse_terraweave_test` / CTest `terraweave`.
- `8edcd1fb4e4f4702b3341368febc69c400577e85`:
  `docs/terrain/TERRAWEAVE.md` API and integration design.

**Verification status**: source committed to GitHub via connector. Full repository
checkout/build was not possible in the authoring runtime (GitHub DNS/network
access unavailable there). Unit tests are authored, **not claimed passing**.
Next agent MUST compile C++20, resolve errors/warnings, run CTest and update
this status with exact commands/results. DO NOT assert successful engine tests
without logs. No screenshots or performance measurements yet.

## Code and design details

- Namespace: `aiverse::terraweave`; header-only new engine API at
  `engine/world/TerraWeave.h`. Native C++ modules can include it directly.
- World X/Z horizontal, Y height. All input/output distances integer cm.
  `ChunkKey` is signed int64 for negative regions. No GPU or float state.
- `TerrainSettings` seed/base/relief/period/octaves; `BiomeSettings`
  seed/sea/temperature/humidity/period/variation/lapse.
- `generateChunk` returns (resolution+1)^2 height samples plus biome ids,
  globally anchored so adjacent chunk edges are bit-identical.
- `scatter` returns `FoliageInstance` (stable key, prototype id, position,
  yaw, scale), with per-species biome mask/height/slope/weight, and rectangular
  exclusions. One jittered candidate per global lattice cell; sorting species
  by id makes input order irrelevant. Bounded to 1e6 candidate lattice cells.
- Pure functions can run in parallel; host determines scheduling. Do not
  mutate ECS worlds inside a worker thread or during a `World::each`.
- No ECS update system wired yet; `TerrainChunkComponent` and
  `VegetationPatchComponent` are descriptors, not simulated systems.
- Existing `modules/rts/view/RtsTerrainRender.*`,
  `modules/rts/view/RtsTerrainSurface.h`, `apps/editor/core/TerrainOps.*`,
  `modules/rts/gen/GenNoise.*`, `MapGrid` retain ownership of existing RTS
  terrain. A future adapter converts integer cm to map units (1/16 units)
  and pushes generation results through existing dirty-chunk pipeline.

## Immediate follow-up — Stage 1b (blocking)

1. Compile `aiverse_terraweave_test` on Clang/GCC/MSVC, including -Wall/-Wextra.
   Run CTest; fix issues in branch, log results here.
2. Verify settings validation/overflow at extreme signed world coordinates;
   establish a supported finite world extent and fail cleanly.
3. Validate that all instances are stable when chunk size changes and with
   more than two species. Add dense forest quality stats.
4. Finish adding `skills/world/aiverse-terraweave/SKILL.md` and an `AGENTS.md`
   rule (if not already present on branch; verify before editing).

## Next stage — Stage 2: existing terrain integration

Goal: generated chunks visible in the existing AIVERSE renderer and editor,
not a competing renderer.

1. Read `docs/terrain.md`, `docs/editor-windows.md`,
   `docs/ecs-agent-handoff.md`, `docs/native-modules.md` and actual current
   source. Rebase carefully; FPS/ECS feature work is happening concurrently.
2. Define world-generation configuration (optional; legacy maps unmodified)
   and an adapter for `MapDoc` heightfield corners. Preserve authored edits
   with a separate override layer and regenerate only dirty chunks.
3. Make scene streaming select visible keys; worker jobs generate pure data.
   Commit on main/guarded ECS sequence; include cancellation, stale generation
   checks and bounded queue; don't enable unsafe generic ECS parallel systems.
4. Integrate heightfield collider, navmesh updates and exclusion/stamp providers
   for roads, buildings, bridges and water. Tests for unchanged old checksums.
5. Editor preview and paint/sculpt tools; negative coordinates; undo/redo.

Acceptance: generated terrain is visible in a real scene; regenerate deterministically;
existing maps identical when disabled; build plus screenshot test.

## Stage 3 — Arbor + Flora / rendering

- Add reusable prototype registry (native + data), procedural branch/crown mesh
  generation with parameters and seed, generated geometric LOD and impostors.
- Forest species weights, competitive exclusion / crown collision,
  biome blending, designer-painted areas and persistence for removed trees.
- Pack millions of cosmetic plants into instance groups, no per-blade ECS
  entities; GPU culling + indirect/instancing RHI path with GLSL and MSL,
  WebGL/mobile CPU fallback. Bench + screenshots + memory budget.

## Stage 4 — Eco, streaming and production polish

- Wind/interaction, seasons, runtime growth, destruction & harvesting.
- Optional ECS features, with deterministic authoritative gameplay state.
- Sparse chunk persistence with generator version migrations, LOD streaming
  budget, editor node graph, pipeline to roads/water, quality tiers.
- Bench CPU gen time, frame GPU time, memory/triangle/draw counts at scales.

## Mandatory handoff format for all subsequent stages

After EVERY stage, append:
- Commit SHA(s), branch and changed files.
- Implemented vs not implemented behaviors and public API/units.
- Commands/logs for compilation/tests (or explicit NOT RUN).
- Compatibility concerns, architecture decisions and blockers.
- Next precisely scoped tasks and acceptance criteria.

Do not overwrite previous stages; keep history available to the next agent.

## Stage 2a — Arbor tree skeleton, initial mesh and LOD (COMMITTED; TESTS NOT YET VERIFIED)

Commit history (all on `feature/terraweave`):
- `f773eca934ac677f841865326a6d08e850275985`: created
  `engine/world/TerraWeaveArbor.h`, seeded local-space integer skeleton:
  trunk, trunk-anchored branches, foliage clusters, Broadleaf/Conifer/Columnar
  profiles, tree LOD High/Medium/Low/Impostor and stable instance-key overload.
- `1112b747ba845492e40d80da16d368e907b5ed27`: added
  `tests/terraweave_arbor_test.cpp` to test branch topology, LOD, seeded
  variation, input validation and parallel purity.
- `20843a5a2d800fdb44a8089599e046275db25ba7`: added CMake/CTest
  `aiverse_terraweave_arbor_test` / `terraweave_arbor`.
- `5c2951343ca819360b45f31f0953bdc165ed7519`: test include fix.
- `dd0714b2e51306b87928032bff65dc792e8f3020`: added
  `engine/world/TerraWeaveArborMesh.h` to build bark tube triangles and
  crossed leaf alpha cards with normals, UVs and bark/leaf material IDs.
- `98e481dab46ff05298bd913356b3d1b2a9948fc8`: added mesh bounds,
  indices, material and finite vertex checks to Arbor tests.
- `23a40a35899dc98ecb669081a6aa8a62bcb67753`: documented Arbor API
  in `docs/terrain/TERRAWEAVE.md`.

**Test status:** C++ tests are written, but neither local compilation nor CI
success was observed at this stage. The PR is still Draft (#24).
Run:
```sh
cmake -S . -B build/terraweave-test -DAIVERSE_BUILD_TESTS=ON
cmake --build build/terraweave-test --target aiverse_terraweave_test aiverse_terraweave_arbor_test -j
ctest --test-dir build/terraweave-test --output-on-failure -R '^terraweave(_arbor)?$'
```
Record exact output here (including platform and failure lines); repair first.

**Implemented:** reproducible tree structural data for native projects; basic
CPU triangle/leaf-card mesh conversion; coarse geometry LOD and conservative
impostor bounds.

**NOT implemented:** actual tree assets in the editor, GPU uploads/draws,
texture/material assignment, tangent computation, billboard atlas bake,
forest/tree placement adapters, wind/deformation, collisions and physically
correct growth. No C++ runtime scene currently renders the Arbor mesh.

**Next agent:** verify both tests, then attach `buildArborMesh` to the
existing `gfx::Device` upload path and model/material pipeline; avoid
direct GL/Metal calls. Prefer integrating the existing `RtsTerrainRender`
chunk renderer over creating a second independent renderer. Preserve
legacy map checksums. Add forest LOD group batching and instance buffers,
editor preview and a screenshot test. Update this handoff again after
each phase.

## Stage 2b — Flora stable LOD batch preparation (COMMITTED; NOT TESTED YET)

- `5c1e233eff8edb756b3401af14bc2c5e93710814`:
  `engine/world/TerraWeaveFlora.h` introduces `FloraView`,
  `FloraBatch` and `buildFloraBatches(placements, view)`.
- `6a01f25e815d75f1398eb753eea58838be831c9a`:
  `tests/terraweave_flora_test.cpp`: species+LOD grouping, radius
  culling, stable order/dedup, configurable far thinning, validation.
- `97763f8e85b806d453268dae338c6859298361d4`:
  `CMakeLists.txt` registers test `aiverse_terraweave_flora_test` /
  CTest `terraweave_flora`.
- `1bad4434381c7316761796d036b675542975a7e3`:
  `docs/terrain/TERRAWEAVE.md` updated with public Flora API.

**Verification:** files committed to PR #24, but no successful
CTest/CI run observed. Run:
```sh
cmake -S . -B build/terraweave-test -DAIVERSE_BUILD_TESTS=ON
cmake --build build/terraweave-test --target aiverse_terraweave_test aiverse_terraweave_arbor_test aiverse_terraweave_flora_test -j
ctest --test-dir build/terraweave-test --output-on-failure -R '^terraweave(_arbor|_flora)?$'
```

**Scope**: CPU-only visibility/cull/LOD batch descriptions; uses no
GPU APIs and does not mutate ECS. `FloraView` units are centimetres.
Do not claim GPU instancing is delivered. It requires a new render
adapter through `gfx::Device`, prototype registry, shader parity,
quality fallback and profiling.

**Next priorities**: resolve any compiler failures; patch TerraWeave
core world-coordinate overflow at large signed coordinates (currently
not hardened); wire geometry into real editor/runtime renderer,
stabilize save formats and scene regeneration, integrate road
exclusion masks and biome painting. Maintain the stage-by-stage
handoff here.

## Stage 2c — bounded world extent hardening (COMMITTED; NOT TESTED YET)

- `c0e4fc1fc84d9e500b53631a374693ec5d36f979`:
  `engine/world/TerraWeave.h` defines `kWorldLimitCm = 1000000000`
  and pre-validates noise sampling, chunk key/span, slope neighbouring
  samples and scatter bounding box before signed coordinate arithmetic.
- `7820438062da8639ba55fc23675745bce5fd9b4f`:
  `tests/terraweave_test.cpp` adds extreme signed-coordinate exception
  cases; run test to confirm.
- `c0e336f344d1f09cbdac90830c72fd8edd31490c`:
  `docs/terrain/TERRAWEAVE.md` documents finite coordinate range.

**Verification:** still not compiled or executed in runtime; CTest and
CI results must be recorded after a successful run. World extent is
a validation limit, not proof of scalable streaming/rebasing.

**Next:** compile all three standalone CTest targets, fix any compiler
or runtime errors, and report checked platforms. Then implement
the first `gfx::Device` mesh upload/render adapter to display a tree
inside a real editor scene; add screenshot and FPS/RTS no-regression.

## Stage 2d — convert Arbor meshes to standard render::Model (COMMITTED; UNVERIFIED)

- `945e7e76a792f6269fbcd8d8ad0606b6f44c3cc6`:
  new `engine/render/TerraWeaveModelAdapter.h` converts three procedural
  geometry LODs into one `render::Model`, with 2 material primitives per
  mesh, Node root, explicit projected-size LOD transitions and
  `Model::finalize()`.
- `90bfd028d90d4628561920b7c8106d2838a27107`:
  `tests/terraweave_model_test.cpp` checks three meshes, bounds,
  material/primitive structure, indices and repeatability.
- `6745d9ea4ac8903fe25a09a0ea6384888f091e01`:
  `CMakeLists.txt` registers `aiverse_terraweave_model_test` /
  CTest `terraweave_model`, links `aiverse_models aiverse_zstd_dec`,
  and enables active assertions in Release for all TerraWeave tests
  on GCC/Clang.
- `b8948a55820b1b1650f03f2fa5cfb791bba5bab8`:
  `docs/terrain/TERRAWEAVE.md` records adapter API, units and limits.

**Tests**: not run or observed successful. Must compile and run:
```sh
cmake -S . -B build/terraweave-test -DAIVERSE_BUILD_TESTS=ON
cmake --build build/terraweave-test --target aiverse_terraweave_test aiverse_terraweave_arbor_test aiverse_terraweave_flora_test aiverse_terraweave_model_test -j
ctest --test-dir build/terraweave-test --output-on-failure -R '^terraweave(_arbor|_flora|_model)?$'
```

**Architectural choice:** reuse existing `render::Model` pipeline instead
of writing new low-level GLSL/MSL or backend-specific rendering code.
`makeArborModel` is CPU-only and converts cm to metres at render edge;
upload via `render::prepareModelUpload` and
`render::uploadModel(gfx::Device&, Model&)` on appropriate stage.

**Unfinished**: runtime/editor spawning and material textures, real
draw-call submission or instancing, impostor baking, wind, collision,
node graph/authoring and visual proof. The procedural leaves currently
render as **opaque green cards** unless a material/texture is supplied.
Prototypes must be cached/reused; per-tree unique Model allocations
are not acceptable for large forests. No GPU benchmarks taken.

**Next agent acceptance test:** open a real scene in AIVERSE and draw
an Arbor prototype via standard renderer with several instances,
then wire the same to flora groups. Validate on Metal/GL/Vulkan,
compare screenshots and existing RTS/FPS test checksums. Fix compile
and tests before extending further. Append the next stage to this file.

## Stage 2e — isolated TerraWeave GCC/Clang CI (COMMITTED; RUNS PENDING)

- `1fbbeb681201dd3d48dbabc4ba9c36641078f58c`:
  added `.github/workflows/terraweave.yml` (push to feature/terraweave,
  PR relevant changes, manual dispatch). Matrix GCC and Clang on Ubuntu,
  Release builds. Builds only the four TerraWeave CTest targets and runs
  the `^terraweave(_arbor|_flora|_model)?$` suite. Enables test checks without
  requiring the entire 1258-target AIVERSE build to succeed.

**Observed broader CI failure (PR #24 initial head
`f6491d60360a5703ce67906a336569862ca446c1`):**
- Workflow `CI` run `44296668203`, Linux ARM64 job
  `134775374705`: **failed** during overall build.
- Error in pre-existing ECS 2.0 experimental test compilation,
  `tests/ecs_wave_executor_test.cpp` (unused variables/functions) and
  `tests/ecs_experiments_test.cpp` (unused variables/functions), under
  `-O3 -DNDEBUG -Werror`. Compilation was stopped before full suite.
- These are *not* TerraWeave source files. Do not claim TerraWeave
  unit tests failed or passed based on this broad-build error.
- ECS team is working in parallel; avoid modifying those tests in this
  branch without coordination. Track them as unrelated CI blockers.

**Verification status as of this documentation commit:**
isolated CI workflow created; no successful run/result observed yet.
Review the new workflow runs on latest branch/PR head, fix any
TerraWeave-specific GCC/Clang errors and append exact results here.

When ready, continue the visible forest scene integration and
instanced prototype reuse. Keep this PR Draft until compilation,
render screenshot and core engine compatibility checks are verified.

## Stage 2f — existing legacy heightfield editor adapter (COMMITTED; UNVERIFIED)

Commits:
- `a3b46c2de44bb9c88c814679e839b1fb03989120`:
  `engine/world/TerraWeaveHeightmap.h` maps integer-cm terrain
  to existing signed int16 1/16-metre corners.
- `2c1b36777ca2f026191cbb89682fdbbc5f279fd5`:
  `apps/editor/core/TerraWeaveMapAdapter.h` provides optional
  `bakeTerraWeaveTerrain(MapDoc&, ...)`. It refuses to replace authored
  corners by default; uses `beginEdit()`/ `touched()` to preserve
  undo and viewer rebuild. Flat maps only.
- `187ef5573d4712087d79fa78576f01e813b3f7d9`:
  validate map origin/span before signed world-coordinate addition.
- `eb1b38f843451f64b482f10116cbf47926a26d36`:
  `tests/terraweave_heightmap_test.cpp`: corner seams, integer
  negative heights, out-of-range values and invalid dimensions.
- `4ef95f969839fc6beec54f78646e4e823c066b5e`:
  `CMakeLists.txt` registers new `terraweave_heightmap` CTest target.
- `452950ef7c67177151eefe026efeb00e944dbfb3`:
  isolated TerraWeave workflow extended to 5 test targets.
- `341765f95bb9615b1c501b87381387a6fd041574`:
  API and caveats added to `docs/terrain/TERRAWEAVE.md`.

**Tests:** authored/committed, no observed successful execution.
Run all five:
```sh
cmake -S . -B build/terraweave-test -DAIVERSE_BUILD_TESTS=ON
cmake --build build/terraweave-test --target aiverse_terraweave_test aiverse_terraweave_arbor_test aiverse_terraweave_flora_test aiverse_terraweave_model_test aiverse_terraweave_heightmap_test -j
ctest --test-dir build/terraweave-test --output-on-failure -R '^terraweave(_arbor|_flora|_model|_heightmap)?$'
```

**Important integration limitations:** adapter is callable C++, not an
editor UI panel/MCP tool, and the inline `MapDoc` wrapper does not yet
have an editor integration test. It is a destructive *bake* only with
explicit replacement, not a non-destructive generation graph with
painted override layers. No terrain collision/navmesh regen event
wiring yet beyond existing `MapDoc::touched()` rebuild semantics.

**Next acceptance:** compile; test the MapDoc adapter in an editor
test with `undo()`, `redo()`, save/load, `terrain.model` opt-in and
screenshot view. Add UI/MCP to preview/commit and persistent
generation settings; do not overwrite legacy maps automatically.


## Stage 2f — reusable ArborForestAdapter (COMMITTED; GPU/CTest UNVERIFIED)

**Branch:** `feature/terraweave` (PR #24, leave Draft).

**Commits (chronological):**
- `088027762542230e8260255182c215710736fe11`:
  `engine/render/TerraWeaveForestAdapter.h` — shared species prototype
  registry, GPU upload/release entry points, CPU model draw transforms,
  stable per-instance identities, origin rebasing, standard Renderer3D submit.
- `8d6a88d1880eb39ea40e075bae3e1816b08fb0f5` and
  `3019a72d02fb32ca53a79dc328fe479bb03e09bf`:
  `tests/terraweave_model_test.cpp` — assertions for prototype reuse,
  duplicate species rejection, per-tree world transforms, camera origin,
  impostor fallback, stable IDs and invalid inputs.
- `873d69ea785bc92bcfd77c5c90ce56e082d1ed48`:
  `docs/terrain/TERRAWEAVE.md` — new host integration/API section.
- This handoff update is another commit following the above; use the
  current `feature/terraweave` HEAD to locate the exact handoff SHA.

**Architecture and units:** `ArborForestAdapter` remains genre-neutral;
no ECS structural writes, no RTTI-specific or GPU-backend-specific code.
`addPrototype` creates one shared `render::Model` for each species.
`forEachDraw` emits lightweight `render::ModelDraw` descriptors with
persistent IDs and floats in relative metres, after subtracting an int64
centimetre origin. `submit` sends them through the *existing* Renderer3D
LOD/batching/shadow path. `uploadModels` / `releaseModels` run on the
render thread and use existing `gfx::Device` upload code. No per-tree
`render::Model` allocation. Caller must keep the registry alive while any
draw references its model. No new gameplay ECS component is required for
non-interactable foliage.

**Actual verification (this stage):** GitHub source files were read and
commits were created successfully. **No local compilation, CTest execution,
engine scene launch or GPU screenshots were run or observed** in this stage.
`tests/terraweave_model_test.cpp` was extended but is *not* yet known to
pass. The isolated GCC/Clang workflow may run after pushes; inspect its
actual run/job logs before marking tests as passing.

**Verification commands:**
```sh
cmake -S . -B build/terraweave-test -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_EDITOR=OFF \
  -DAIVERSE_BUILD_CLIENT=OFF -DAIVERSE_BUILD_SERVER=OFF \
  -DAIVERSE_BUILD_HEADLESS=OFF -DAIVERSE_BUILD_TOOLS=OFF -DAIVERSE_FBX=OFF
cmake --build build/terraweave-test --parallel 2 --target \
  aiverse_terraweave_test aiverse_terraweave_arbor_test \
  aiverse_terraweave_flora_test aiverse_terraweave_model_test
ctest --test-dir build/terraweave-test --output-on-failure \
  -R '^terraweave(_arbor|_flora|_model)?$'
```

**Known limitations:** the adapter is a bridge API, not a host runtime/editor
integration. No generated terrain or forest is drawn in a real AIVERSE scene
until a host constructs `ArborForestAdapter`, uploads its prototypes, builds
Flora batches and invokes `submit` between `beginScene` and `render`.
CPU `FloraView` distance LOD buckets are advisory; the renderer selects LOD
by pixel size. `TreeLOD::Impostor` falls back to lowest geometric LOD
because a billboard atlas is not implemented. Leaf material is still an
opaque green placeholder; alpha masking and wind remain outstanding.
GPU instance/draw-call numbers and perf have not been measured. No changes
were made to legacy RTS terrain, save/replay/networking or editors.

**Next agent, exactly:**
1. Check current PR #24 isolated GCC+Clang CI run results and fix any
   TerraWeave compile/runtime failures first. Report exact commands/logs.
2. Wire an **opt-in** WorldScene/scene manifest or editor preview to this
   adapter using the standard model upload/renderer order, without
   changing legacy maps when TerraWeave is absent.
3. Add a real camera/render-frame smoke test and screenshot for a
   multi-species forest on Metal + at least one non-Apple backend. Verify
   model prototype reuse and actual draw/instance stats at 100/10k trees.
4. Then connect terrain corner heightfield and road/building exclusion
   adapters; verify FPS/RTS regression checksums and save/replay determinism.
5. Append a new stage to this exact handoff file after each phase.


## Stage 2g — structural BranchLOD per tree branch (COMMITTED; TESTS UNVERIFIED)

**Branch:** `feature/terraweave` (PR #24 remains Draft).
**Commits:**
- `3fbb6cafce85e70a7af0f4af62d8071878e1cdd0`:
  `engine/world/TerraWeaveArbor.h` — add `LeafCluster::parentBranch`,
  fill it from deterministic branch generation.
- `efdddbb9e41c39befaff7cb6c2716f029e6d88c0`:
  `engine/world/TerraWeaveArborMesh.h` — `ArborMeshSettings::leafPlanes`
  can choose a single leaf card or the previous crossed pair.
- `dda5e55d2582909019206945d32dd34bf7daebf5`:
  `engine/world/TerraWeaveBranchLod.h` — independent Full/Reduced/Minimal
  branch meshes; respect the leaf-to-branch relationship.
- `0ce7cd300e718b324e92f0ee8e8ad4c71febf325`:
  `engine/render/TerraWeaveModelAdapter.h` —
  `makeArborBranchLodModel` with one `render::Node` per branch and
  three mesh alternatives per node; the native renderer independently
  selects each node's projected-size LOD.
- `15c6e82d22afa8706d0388a5638c457b5d0a0848`:
  `tests/terraweave_model_test.cpp` — parent topology, per-branch
  triangles monotonically decreasing, LOD indices, repeatability and
  invalid thresholds.

**Architecture/API:** render-only, deterministic seeds, centimetre CPU
geometry -> metre model vertices, no new renderer or ECS entity per branch.
Existing `makeArborModel` kept unchanged for compatibility.
Default `BranchLodSettings` thresholds: 96 and 30 screen pixels.
Per-branch LOD works because existing `Renderer3DModels.cpp` evaluates
each `render::Node` and its own `MeshDef::lods`, independently of the
overall model. `TreeBranch::parent` remains the render node parent.
The renderer also considers the shadow-camera projected size, so shadow
LOD may require extra detail.

**Verification:** GitHub commits and source inspection completed.
**No compiler, CTest, GPU render or screenshot test executed/observed**
for this stage. Do not claim tests passed.

**Limitations:** one structural draw node per branch increases CPU
model traversal and may increase draw submissions. Existing renderer
instancing may group compatible meshes across trees, but no measured
numbers or large-forest benchmarks. The leaf cards near camera still
use placeholder opaque green unless project textures are supplied.
`BranchDetail::Minimal` preserves branches with 3-sided tubes + one
leaf plane; it does not merge branches or support full-tree impostors
(that follows in Stage 2h).

## Stage 2h — final alpha-masked whole-tree billboard (COMMITTED; UNVERIFIED)

**Commits:**
- `adf4f0184a6cca012276fbcb2e85f7313251559f`:
  `engine/render/TerraWeaveArborBillboard.h` —
  `makeArborBillboardModel` CPU-rasterizes a real transparent RGBA8
  tree silhouette (bark and leaf clusters) and a single XY quad,
  `render::AlphaMode::Mask`, no fake green rectangle.
- `a10670d6bf0dcf249a227fb85910689e5728c192`:
  `engine/render/TerraWeaveForestAdapter.h` —
  per-species model pair (near structural + far billboard),
  conditional Flora batch submission, view-facing yaw for far trees,
  upload/release via the existing gfx Device path.
- `f8c5935aa1ba1ec38b16c97e77c85db103305a09`:
  `tests/terraweave_model_test.cpp` — alpha pixels, mask,
  repeatable silhouette, model choice, facing and persistent IDs.
- `e5ff1bc696d40eb85faf0657f0626678f1b68fb3`:
  `engine/render/TerraWeaveForestAdapter.h` — camera extent hardening,
  default 8 radial segments to preserve three distinct geometry tiers.
- `d9831237ea54d5186ea5b13cd3de35e61db2c91b`:
  `docs/terrain/TERRAWEAVE.md` — updated native API/usage and
  explicit one-view procedural billboard limitations.

**Behavior and invariants:** no separate GPU pipeline. Only far
`TreeLOD::Impostor` Flora batches use the billboard; all closer
instances use one shared branch-LOD model per species. Every whole-tree
impostor is one quad, one alpha-masked material/texture, camera-facing
horizontal yaw and origin-relative transform. Both near and impostor
share the same stable `ModelDraw::persistentId`. Models are uploaded
and released by `ArborForestAdapter::uploadModels/releaseModels` on the
render thread. Textured billboard is generated on the CPU, with no
platform shader dependencies. Billboard has castShadow=false.

**Actual verification:** source changes pushed to GitHub and test
assertions updated. No local repository clone was available in this
runtime, and no network resolution for Git clone was available.
**CMake/CTest, real scene render, GLSL/MSL and GPU screenshots: NOT RUN.**
No CI pass/fail observed from the last queried head; check the newest
isolated TerraWeave GitHub workflow before asserting status.

**Reproduce (same CI target as previous stage):**
```sh
cmake -S . -B build/terraweave-ci -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_EDITOR=OFF \
  -DAIVERSE_BUILD_CLIENT=OFF -DAIVERSE_BUILD_SERVER=OFF \
  -DAIVERSE_BUILD_HEADLESS=OFF -DAIVERSE_BUILD_TOOLS=OFF -DAIVERSE_FBX=OFF
cmake --build build/terraweave-ci --parallel 2 --target \
  aiverse_terraweave_test aiverse_terraweave_arbor_test \
  aiverse_terraweave_flora_test aiverse_terraweave_model_test
ctest --test-dir build/terraweave-ci --output-on-failure \
  -R '^terraweave(_arbor|_flora|_model)?$'
```

**Next exactly:**
1. Review isolated GCC/Clang workflow results (and CTest), fix any
   header/adapter test errors first. In particular validate
   `makeArborBranchLodModel` with renderer upload and its per-node
   projected-size selection (including camera and shadow clip).
2. Add an opt-in forest placement path to the WorldScene runtime/editor.
   A standalone adapter header alone does **not** draw forests in any
   existing game. Keep legacy scenes unaffected if TerraWeave is disabled.
3. Capture a real GPU screenshot of a partially simplified tree, its
   individual branch LOD switch, then whole-tree billboard; verify
   GL/Metal/Vulkan and alpha masking. Quantify 100/1,000/10,000 tree
   model nodes, draw submissions, triangles and memory.
4. For production, add multi-view (8/16) baked albedo+normal/depth
   impostor atlas, temporal or dithered tree↔impostor crossfade and
   shadow policy to avoid visible pop. Consider branch cluster merge
   to lower draw count without losing local LOD selection.
5. Append complete measured results and a new handoff stage.

As always, no untested claim of a passing build, GPU visual quality,
or runtime integration is permitted.

## Stage 3a — editor/MCP procedural terrain controls (COMMITTED, TESTS UNVERIFIED)

**Branch:** `feature/terraweave`, PR #24 (Draft), base
`feature/universal-entity-inspector-fps`. This stage adds the first opt-in
user-visible editor control for terrain. It intentionally does not change
existing RTS/FPS maps unless a user invokes the generation action.

**Commits / changed files:**
- `2df5b10676bf83a521b7b2ab42ac38de9ba2b906` —
  `apps/editor/core/TerraWeaveMapAdapter.h`: refuse silently overwriting
  existing detail layers; when explicitly replacing, clear detail/splat/
  detailFile/scale after MapDoc undo snapshot, so corners become visible.
- `3fd0bab8de98ebdb27cafc36113706af7abec447` —
  `apps/editor/core/TerraWeaveOps.h`: `terraweave_preview` and
  `terraweave_generate` registerable `Workspace` operations with
  validated generator settings; version 1 generator metadata stored in
  `map.data.extra.terraweave`.
- `2d6e621e6968e7da7822746b73a2377309cdf960` —
  `apps/editor/core/TerrainOps.cpp`: registers the operations in
  `registerTerrainOps`, automatically used by editor and MCP.
- `2686779f35e5b44241605f6b54b74b248e4f1302` —
  `apps/editor/windows/TerrainWindow.cpp`: UI controls for seed,
  base height, relief, period, octaves; Preview and Generate buttons.
  Overwrite requires an explicit checkbox.
- `0aab2e0d2b6d5002aafb4577acecb631801dcfaf` —
  `tests/terraweave_ops_test.cpp`: deterministic 9×9 preview, versioned
  metadata, invalid configuration checks.
- `308aed89ce74076cb447637b80f18f9a696702b4` —
  `CMakeLists.txt`: `aiverse_terraweave_ops_test` / CTest `terraweave_ops`.
- `a34e7cde606cb75160ccc176027fb889743bb1ca` —
  `.github/workflows/terraweave.yml`: six independent TerraWeave
  CTest targets and paths for editor changes.
- `afa8e34ac57c119c688a127a84adb41b44981727` —
  `docs/terrain/TERRAWEAVE.md`: new public API, safety and limitations.

**Architecture:** `Workspace::call` is the single mutation entrypoint
for both GUI and AI/MCP. `terraweave_preview` samples without changing
MapDoc, `terraweave_generate` bakes to the existing corner pipeline,
invalidates views with `Change::Map`, records generator version/settings
for future regen and supports Undo. Detail data is *not* preserved in
the new map but is contained in the Undo snapshot. No simulation model
changes. These operations apply **only to flat RTS grid maps** because
`MapDoc::corners` is legacy RTS data; generic Terrain/Flora/Arbor code
remains cross-genre. No new GPU or shader code.

**Tests ACTUALLY performed:** source inspection and successful GitHub
commits via connector. **No build, CTest, live editor launch, screenshot,
MCP functional invocation, or FPS/RTS regression was observed** in this
authoring runtime. Do not claim passing tests; retrieve current isolated
workflow logs and record actual results before merging.

**How to test immediately:**
```sh
cmake -S . -B build/terraweave-test -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_EDITOR=OFF \
  -DAIVERSE_BUILD_CLIENT=OFF -DAIVERSE_BUILD_SERVER=OFF \
  -DAIVERSE_BUILD_HEADLESS=OFF -DAIVERSE_BUILD_TOOLS=OFF -DAIVERSE_FBX=OFF
cmake --build build/terraweave-test --parallel 2 --target \
  aiverse_terraweave_test aiverse_terraweave_arbor_test \
  aiverse_terraweave_flora_test aiverse_terraweave_model_test \
  aiverse_terraweave_heightmap_test aiverse_terraweave_ops_test
ctest --test-dir build/terraweave-test --output-on-failure \
  -R '^terraweave(_arbor|_flora|_model|_heightmap|_ops)?$'
```

**Risks/limitations:** undo/restoration not verified in a live editor;
numeric preview only, not a 3D preview; material/detail regenerated only
when user approves overwrite. Existing maps are unchanged unless opt-in.
Any async worker runtime must use revision tokens and deterministic ECS
commit order. Parameter metadata is versioned but loading does not
auto-regenerate it. Do not touch unrelated concurrent RTS/FPS/graphics
feature branches.

**Next agent task:**
1. Compile and run all 6 CTest targets, investigate fail logs and update
   handoff with platform, commands and final pass/fail.
2. Add an end-to-end editor/Workspace test: preview leaves revision/data
   unchanged; generate mutates corners; existing detail refuses by default,
   overwrite removes detail and single Undo/Redo restores; save/reload keeps
   `map.extra.terraweave` metadata and corners. Run a real editor screenshot.
3. Connect `ArborForestAdapter` to opt-in runtime/editor scene preview:
   cache shared prototypes, use `buildFloraBatches`, call existing
   `gfx::Device` and Renderer3D, measure draw calls and frame times.
4. Follow with biome masks, painted exclusion regions, terrain+water/roads
   ownership, camera-relative chunk streaming and shader alpha material.
5. Append another stage to this file with exact commits and verification.


## Stage 3b — shared multi-angle Arbor impostor (COMMITTED; TEST EXECUTION DEFERRED)

**Branch/PR:** `feature/terraweave`, draft PR #24 (base is
`feature/universal-entity-inspector-fps`). Existing sources are
concurrently edited by other agents; always fetch current branch HEAD
and file SHA before writing or rebasing.

**Code commits and files:**
- `7a9eeb2e507b0cde80dedf83e9704af38b2a0da7` —
  `engine/render/TerraWeaveImpostorAtlas.h` creates deterministic
  4/8/16-view RGBA8 silhouette atlas, one base model and one stable
  Material override vector per view; provides azimuth selection.
- `15b743d99eaf8a7178ec97de13fa289e8ba23118` —
  `engine/render/TerraWeaveForestAdapter.h` chooses camera-relative
  atlas frame for `TreeLOD::Impostor`, reuses one GPU model and
  texture per species, assigns `ModelDraw::materials` to cached view.
- `8d6b0ae05f7c6f3c349ec973e198ca8a3cdd10b5` and
  `b5d3494a8bea3b19040d02c61f29b232ffc56ca9` —
  `tests/terraweave_model_test.cpp` updates *deferred* assertions for
  atlas dimensions, deterministic colours/pixels, material UV slices,
  camera azimuth and per-tree orientation.
- `8503c32cce5d8d3b45ebdc0eb85d337e2b3d126a` —
  `docs/terrain/TERRAWEAVE.md` API/architecture.
- This handoff update is committed separately; inspect latest HEAD
  for its own commit SHA.

**Design:** geometry remains shared among all trees of a species.
Each eight-view impostor uses one 1024×256 RGBA8 texture, one four-vertex
model and eight `Material` override vectors with UV transforms.
It **does not duplicate models or textures per camera direction**.
`ArborForestAdapter::forEachDraw` computes a world-camera vs
tree-local-yaw azimuth index; `Renderer3D` groups draw submissions
via its normal material-state logic. CPU baker is independent from
renderer backends/genre. All coordinates are signed cm until the
existing presentation adapter converts them to metres.

**Actually verified:** GitHub source reads and pushes only. User asked
to defer tests. **Compilation, CTest, scene screenshots, GPU captures
and benchmarks NOT RUN.** Added checks are still unverified.

**Known limitations:** rasterized cards use simple colours (not textured
albedo/normal/depth), nearest-angle selection may pop during camera
orbit, multi-view atlas only supports fixed horizontal azimuths.
Alpha-edge filtering/mipmap coverage and exact draw batching still
require GPU verification. Scene/editor auto-spawning is not provided
by this stage. No gameplay ECS state or legacy map files were touched.

**Next:** add deterministic spread of the geometry→impostor transition
to avoid a sharp visible LOD ring; optionally crossfade individual
trees using standard renderer mechanisms after checking material
semantics. Implement an opt-in runtime forest-scene host that owns
persistent prototypes, bounded placement chunks and upload lifecycle.
Capture screenshot/performance once the user requests tests.

## Stage 3b — optional desktop editor integration regression target (COMMITTED; NOT RUN)

**Branch:** `feature/terraweave` / PR #24 Draft.
**Commits:**
- `add07dc38341b2ef539bc61cec5b9e7d0ab79397`:
  `skills/world/aiverse-terraweave/SKILL.md` documents safe
  `terraweave_preview` / `terraweave_generate` agent workflow.
- `7393938e914ee647bf2b57b9e9d9395ebffb8fe9`:
  `tests/terraweave_editor_test.cpp` constructs the actual
  `Workspace` registry and `MapDoc`, tests preview without state
  mutation, generation, default refusal to overwrite, persistent
  `map.extra.terraweave` metadata, Undo/Redo, and detail/splat
  replacement with restoration.
- `524a6b4da21eb9d774ba722d45ead7ced83226c6`:
  `CMakeLists.txt` adds `aiverse_terraweave_editor_test` / CTest
  `terraweave_editor` **only if `aiverse_editor_core` is built**.
  Requires desktop `AIVERSE_BUILD_TOOLS=ON` or
  `AIVERSE_BUILD_EDITOR=ON`; intentionally not included in isolated
  six-test CPU-only workflow.
- `a0691a2790bb2960234bdc55dc1979e383d2f3e4`:
  updated the integration test to call with source `editor` to
  avoid writing `logs/editor.log` during test execution.

**Verification:** committed to GitHub. No local compilation or
CTest/editor runtime was executed/observed, so status is UNVERIFIED.
Check compilation before claiming integration works. Run:
```sh
cmake -S . -B build/terraweave-editor-test -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DAIVERSE_BUILD_TESTS=ON \
  -DAIVERSE_BUILD_TOOLS=ON -DAIVERSE_BUILD_EDITOR=OFF \
  -DAIVERSE_BUILD_CLIENT=OFF -DAIVERSE_BUILD_SERVER=OFF \
  -DAIVERSE_BUILD_HEADLESS=OFF
cmake --build build/terraweave-editor-test --target \
  aiverse_terraweave_editor_test --parallel 2
ctest --test-dir build/terraweave-editor-test --output-on-failure \
  -R '^terraweave_editor$'
```

**Next step:** run the tests on macOS/Clang (primary desktop) and
Linux/GCC, fix any API or CMake linkage issues; verify a real editor
can generate, Undo, save and reload with a scene screenshot.
Then connect opt-in runtime forest preview using the already-present
`ArborForestAdapter` and reuse `engine/world/AsyncChunkStream.h`
rather than introducing a duplicate chunk job scheduler. For each
later phase update this handoff including exact tests and commits.


## Stage 3c — stable forest-wide transition band (COMMITTED; TESTS DEFERRED)

- `50bc00298e0ad5d9c27dd895b016919513c70810`:
  `engine/world/TerraWeaveFlora.h` adds
  `FloraView::impostorBlendWidthCm`, a deterministic per-tree choice of
  low geometry vs far impostor threshold. Default remains 0.
- `455d319bc5d6265b8a1e4837e5a0e024bc6f7e94`:
  `tests/terraweave_flora_test.cpp` records deferred checks for
  output stability across shuffled input, transition endpoints and
  invalid ranges.
- Updated documentation in `docs/terrain/TERRAWEAVE.md`.

**API:** All distances in integer centimetres. A per-tree stable
hash combines persistent key and species; band midpoint is
`lowRangeCm`. No new gameplay state, frame history or GPU resources.
A 0-width band preserves prior exact behavior. Population stagger is
not an individual tree crossfade.

**Actual tests:** GitHub writes/source review only; no compiler/CTest,
live render, screenshot or GPU benchmark run, by explicit user request.

**Next agent:** implement opt-in chunked forest host and eventually
transition crossfade/dither in the shader if visually necessary. Never
claim per-tree fade or flicker-free camera orbit without GPU verification.

## Stage 4a — opt-in editor forest scene overlay (SOURCE COMMITTED; UNVERIFIED)

Branch: `feature/terraweave`, draft PR #24, base
`feature/universal-entity-inspector-fps`. Other agents are actively
editing this branch: fetch current HEAD and each file SHA before modifying.

**Commits/files (2026-11-18):**
- `f3f2bd007c88724af210f7730ede2d7f594b498b`:
  `apps/editor/TerraWeaveForestPreview.h`. Editor-only owner of a
  reusable `ArborForestAdapter`, cached 5x5 presentation tiles
  around camera (32m tile); deterministic local placement from
  `map.extra.terraweave` recipe. Two common tree prototypes
  (broadleaf/conifer) shared across instances. Uses existing
  `scatter`, `buildFloraBatches`, renderer, LOD atlas, and
  `gfx::Device` lifecycle.
- `157de8af70245766ac09153deec47a35470f4632`:
  `apps/editor/Editor.h`: persistent preview instance and
  window-accessible toggle/count.
- `e1efeafc2b7113cf8182fc7bc22bc0f0875f8d94`:
  `apps/editor/Editor.cpp`: submit forest after existing
  `RtsView::draw` and before `Renderer3D::render` in Scene view,
  sample *current* (possibly sculpted) editor heightfield, release
  uploaded resources on editor/project/map lifecycle.
- `a3f34307123d34c0e7b81153538dda2a8491f34b`:
  correct global scatter coordinates vs map-local render coordinates,
  including nonzero generation origins.
- `75e31f36d852e5cf8ec4fb1497dfd21a3813b586`:
  `apps/editor/windows/TerrainWindow.cpp`: explicitly opt-in
  `Показывать процедурный лес в «Сцене»` checkbox and visible
  placement count. Disabled when the map has no generator recipe.

**API/data flow:** Existing `MapDoc` generator recipe is read only.
Prototype models are generated once, uploaded via `gfx::Device`,
and reused in `Renderer3D::drawModel`. Placement buffers are
recomputed only when map revision, saved recipe or camera tile
changes. Forest is for editor display only, not game save/ECS
entities. World positions stay integer centimetres until the
existing renderer bridge. Height is sampled from current visual
map so trees follow manual sculpt changes. Gameplay, physics,
navmesh, FPS and planet map remain untouched.

**Limitations:** Preview is not yet an ECS forest runtime.
No tree collision, road/building exclusion (beyond biome/height/
slope filtering), seasons, growth, true texture-mapped tree
materials or incremental streaming jobs. The 5x5 tiles are
generated synchronously on the editor render thread; bounded
cost but not suitable as production streaming. Runtime streaming
should use existing `AsyncChunkStream` and revision-safe commits.
No screenshot or FPS measurements. The renderer shader/alpha atlas
behaviour is still unverified on Metal/GL/Vulkan.

**Verification actually performed:** GitHub commits and inspection
of real `Editor::renderScene`, `Renderer3D` and `MapDoc` APIs.
No local compilation/CTest/editor launch/GPU screenshot has been
run or observed. DO NOT mark as passing or ready to merge.

**Next concrete tasks:**
1. Compile the entire `aiverse_editor` target (Mac/Clang first),
   fix any API or linkage errors and record exact logs.
2. Add a CPU-only/editor compile smoke target for
   `TerraWeaveForestPreview.h`, and tests for coordinate rebasing,
   stable placement and map-revision refresh.
3. Preview in editor: generate terrain, toggle forest, orbit camera,
   verify branch LOD and impostor transitions via screenshots.
4. Add road/building/water masks, prototype configuration and
   configurable preview budget. Then integrate async chunk streaming
   via existing `AsyncChunkStream`, not a duplicate scheduler.
5. Append a fresh handoff stage with measured tests and next step.


## Stage 3d — opt-in native & WorldScene streamed forest (COMMITTED; TESTS DEFERRED)

**Branch:** `feature/terraweave`, draft PR #24; base
`feature/universal-entity-inspector-fps`. Do not modify legacy
game projects or main automatically.

**Commits / changed files:**
- `13e0cf245704eed1a93daa4979c3e13f279b21a6`:
  `engine/world/TerraWeaveForestScene.h` — persistent shared model
  prototypes, bounded per-chunk CPU foliage cache, deterministic nearest
  residency/eviction, explicit exclusion invalidation, GPU lifecycle and
  Renderer3D submission, CPU stats.
- `d87e17e53aa7a33441fc326831fa062f7eb0550d`:
  `engine/world/TerraWeaveForestManifest.h` — optional JSON schema
  parser and `forestSceneFromManifest`, validated species/foliage
  configuration, seeds, camera LOD ranges and rectangular exclusions.
- `9c94303571be1eeb0d095da593ebee3e64f08392`:
  `engine/world/WorldScene.h` — optional forest host ownership and
  model-upload state.
- `47e2bea0006a710d36eebf35515e2389f5b54751`:
  `engine/world/WorldScene.cpp` — create host if root
  `terraweaveForest` exists; build and release resident CPU patches
  in `update`; upload prototypes through existing GPU update stage,
  submit between Renderer3D begin/render in `draw`, release in shutdown.
- `438bb52c0265ebeead2b24d9ae0c844469442833` and
  `53b416df88ba94e907da97b3f13eb44b51eeff0f`:
  forest host/schema defaults aligned so a 24,000 cm render cull is
  covered by a 6,400 cm chunk × 4-chunk resident radius, with 64
  resident chunks budget; avoid the previous 50,000 cm cull vs
  3,200 cm × 3 radius mismatch.
- `c84eca7792e26bffc4302106a8ec7ee0463ab145`:
  `docs/terrain/TERRAWEAVE.md` — opt-in manifest example, public
  native API and limitations.
- This stage handoff is another documentation commit; retrieve the
  current branch head for its SHA.

**Architecture/API:** `TerraWeaveForestScene` can be called directly
from any native genre host (no RTS dependency). `addSpecies` registers
`TreeProfile`, `FoliageSpecies` and mesh seed before first render.
`streamAt(ForestCameraCm)` uses integer cm and pure deterministic
`scatter`, caches only the nearest bounded chunks, and gives stable
instance identities regardless of chunk eviction/regeneration.
`setExclusions` accepts world-space half-open rectangles and invalidates
CPU patches. `visibleBatches` uses existing Flora LOD. Render thread
calls `uploadModels`, `submit`, `releaseModels`. `WorldScene`
automatically owns the host **only** when the top-level manifest
contains an explicit `terraweaveForest` block. Without it, the
old path is unchanged. A tree is cosmetic, not a per-tree ECS entity.

**Actual verification performed:** read target source and committed
changes successfully through the GitHub connector. **No CMake/CTest,
binary launch, GPU screenshot or performance measurement executed**;
tests were explicitly deferred by the user. No claims of CI passing.
No source files under unrelated agent features modified.

**Known limitations / risks:**
- CPU `streamAt` is synchronous, bounded but may cause a hitch
  when many patches first enter range; no worker queue/cancellation yet.
- Forest manifest is only integrated into the existing asynchronous
  `WorldScene` host, not automatically into the RTS MapDoc editor.
- Only first observer controls forest residency; multi-observer union
  needs a future policy.
- Procedural forest height samples from TerraWeave seeds, not arbitrary
  authored terrain/physics heightmap; trees may float/intersect the
  independently authored ground until a height source adapter exists.
- No gameplay collision/navmesh obstruction, save/harvest, growth,
  water and road generator bindings or editor forest placement UI.
- No budget/perf proof, alpha atlas GPU proof, or actual viewer demo.
- Implementation assumes the normal WorldScene GPU update/draw/shutdown
  ordering; validate all three in a real scene after tests resume.

**Concrete next steps:** implement a generic CPU height source override
for scattering plus road/exclusion integration to align tree bases to
authored terrain; then move streaming generation to worker jobs with
bounded staging and revision-check commit, maintaining deterministic
order. Wire an opt-in editor forest preview, inspect model LOD and
GPU batching, and only afterward perform CMake/CTest, headless,
Metal/GL/Vulkan screenshots, FPS/RTS regressions and benchmarks.
Append a new handoff after each phase.


## Stage 3e — bounded residency work and upload rollback (COMMITTED; TESTS DEFERRED)

**Commits:**
- `e48735049c7b702142cdc59eac4f8f85a2043cce` —
  `engine/world/TerraWeaveForestScene.h`: validates candidate
  lattice density across the full resident cache. Caps one chunk
  at 65,536 candidate cells and the maximum scene allocation/work
  estimate at 1,000,000 candidates. Validates `FloraView` LOD
  ranges early. On partial GPU prototype upload failure, releases
  already uploaded model resources rather than leaking them.
- `07836187c1f5109d60f65c7b21cfb15a647ddab0` —
  `skills/world/aiverse-terraweave/SKILL.md`: instructs agents
  how to use the new atlas/forest host, and warns of deferred tests.

**Architecture:** CPU scatter bound is validated in
`TerraWeaveForestScene` constructor. No change to deterministic
scatter algorithm, global lattice positions, ECS simulation or
legacy projects. GPU ownership remains the render/update thread
via `gfx::Device`. No new direct graphics backend calls.

**Actually tested:** no binary compile/test, live scene or screenshot
run, as requested. GitHub commit succeeded. This cap is a guard
against pathological configurations, *not* a measured latency
guarantee.

**Next agent:** during formal verification, check that
`AIVERSE_BUILD_TESTS=ON` compiles the new WorldScene integration,
run `terraweave_model`/`terraweave_flora` and true runtime
WorldScene capture, validate memory/draw count under the bounds.
Implement asynchronous streaming with bounded pending job queue and
stale revision tokens before marketing this as non-blocking.

## Stage 4b — placement masks, smoke test, GPU cleanup (COMMITTED; UNVERIFIED)

**Branch**: `feature/terraweave`, draft PR #24; another agent
may be working on this branch concurrently. Always refetch HEAD.

**Commits/files:**
- `e3abd75038d0217c67dcd4502ee98dfc7fba6c0f`:
  `apps/editor/TerraWeavePlacementMask.h` — editor-only placement
  filter for non-ground map cells, `DocEntity` exclusions and
  `MapData::roads` line-clearance.
- `fc704fc2559b276abba6ea50850d11aa86d57677`:
  explicit include of `engine/world/TerraWeave.h`.
- `5266194bb10185855b0fc9a968b9b2fe9b7d0ce6`:
  `apps/editor/TerraWeaveForestPreview.h` invokes the mask before
  queuing tree instances.
- `c74a673db757d2a80ed987e55590b1b769ca0f15`:
  `tests/terraweave_preview_test.cpp` checks preview opt-in state,
  cell mask, entity clearance, road segments, outside-map values
  and invalid settings.
- `ba92d702ffe3d420e17fef24b5ff3fc637a75a52`:
  registers `aiverse_terraweave_preview_test` in CMake.
- `bf2a64e2835830bd41dd82aaffa02f8e218f2dd0`:
  adds preview target + changed editor paths to isolated
  `.github/workflows/terraweave.yml`.
- `22e29e98be046208ce375db85af68e119bdfdae8`:
  `apps/editor/Editor.h` releases GPU forest models when user
  unchecks preview.
- `878282e69641d274a41cd500ceddda5c49fae3d2`:
  architecture/limits added to `docs/terrain/TERRAWEAVE.md`.

**Implemented API**: `terraWeavePreviewAllowsPlacement(const MapData&,
localXcm, localZcm, cellSizeCm, entityClearanceCm, roadClearanceCm)`.
Pure editor presentation policy, not authoritative ECS or collision.
Uses squared distance to legacy roads and filters cells/objects.
No new GPU shaders/pipelines or gameplay mechanics.

**Actual verification**: Source commits succeeded. A diagnostic
`git ls-remote https://github.com/chromealex/AIVERSE.git` from the
authoring container failed with
`Could not resolve host: github.com`; therefore no full repo checkout
and no local CMake build, CTest, editor launch or screenshots were run.
GitHub workflow results have not been independently observed passing.
**All tests remain unverified.** Do not claim otherwise.

**Known limitations**: This is a small synchronous editor overlay,
not streaming runtime foliage. 5×5 tile placement is regenerated on
camera tile/map revision; CPU stalls are possible on dense or large
scenes. It doesn't yet parse `roadNetwork` and `waterNetwork`
splines, footprint data or physics/AI collision. Preview foliage is
not saved as authored trees and does not become ECS entities.

**Next agent exactly**:
1. Configure/build `aiverse_editor` and run isolated CTests for
   `terraweave_preview` + other TerraWeave targets. Fix errors and
   record actual commands/output.
2. In a real editor project, use `Рельеф → TerraWeave` to generate,
   enable forest overlay, orbit/zoom; capture close/medium/far
   screenshots with Metal and GL/Vulkan and measure draw/triangles.
3. Implement non-blocking `AsyncChunkStream` integration with bounded
   jobs and stale revision discard. Ensure all GPU submissions remain
   on rendering thread. Use authored road/water splines as exclusion
   providers and correct building footprint mask.
4. Add species/biome authoring and persistent interactive tree ECS
   integration as separate opt-in packages, not per-grass entities.
5. Append another handoff after every stage with tested outcomes.

## Stage 4c — correct map-unit scaling and preview lifetime (COMMITTED; UNVERIFIED)

**Changes/commits:**
- `3127556ebc92e2168379112b0cac9d3990fa5c51`:
  `apps/editor/Editor.cpp` releases and disables forest preview on
  project/map changes; automatically releases stale GPU resources if
  an Undo removes the generator recipe while the Terrain window
  is closed.
- `6d00097e2b7b817b50099d0acc5102e2deb4b38e`:
  `apps/editor/TerraWeaveForestPreview.h` fixes camera-to-global
  and world-to-map coordinate conversion when
  `LegacyHeightfieldOptions.cellSizeCm != 100`.
- `2fd0b6c628439fbedc0081e1c44581c035ebd99d`:
  `apps/editor/TerraWeavePlacementMask.h` adds validated
  `terraWeaveWorldFromEditorCm` and
  `terraWeaveEditorFromWorldCm`.
- `e13006e86020624a42d80c16321d6032b5f2f0e0`:
  `apps/editor/TerraWeaveForestPreview.h` uses those helpers
  for source sampling and renderer placement.
- `eed584129006e7d77677e9d16c6438f316a32bab`:
  `tests/terraweave_preview_test.cpp` adds 150/200-cm
  generator spacing, signed-origin and invalid-spacing cases.

**Why:** MapDoc's renderer always treats each authored map cell as
one metre. TerraWeave can use an arbitrary positive world-cm
sample spacing for the procedural source. Without a conversion,
tree positions and camera-based chunk selection move incorrectly
relative to the existing map when cell size differs from 100 cm.
The current code normalizes trees into editor-local 100 cm/cell
before building GPU transforms and placement masks.

**Actual verification:** source pushed through GitHub connector;
no complete checkout, compile, CTest or GPU screenshot observed.
Local GitHub clone remains blocked by DNS resolution in this
runtime. Keep PR Draft until verified.

**Next:** run all seven CPU/compile tests plus
`terraweave_editor` and a full editor build. Add a visual
comparison for `cellSizeCm=100` vs `200`, and verify that
the world-space camera tracking/LOD selection remains stable.
Move costly per-tile scatter to the existing async chunk scheduler
once baseline GPU behaviour is measured.

## Stage 5 — external height and placement sources for universal forest (COMMITTED; UNVERIFIED)

**Branch**: `feature/terraweave`, Draft PR #24. Concurrent agents
are changing adjacent TerraWeave components; fetch current file SHAs.

**Files/commits**:
- `5c2cf60f533ebf3a2ac8613095c5b374963261c2` —
  `engine/world/TerraWeaveForestScene.h` adds optional
  `HeightSource = std::function<int32_t(int64_t,int64_t)>`
  and `PlacementFilter = std::function<bool(const FoliageInstance&)>`
  with `setHeightSource`/`setPlacementFilter`. Both invalidate the
  resident cache and revision when changed. During `streamAt`,
  placement instances are re-elevated from the provider and rejected
  by the optional placement filter before publishing to host cache.
  Samples outside supported ±1,000,000,000cm heights are rejected.
- `4ed8816a3ac95a873d4e52cc09b8b202d9de9811` —
  `tests/terraweave_forest_scene_test.cpp`: verifies height
  callback updates while preserving placement keys, filters,
  invalidation/revisions and an invalid height exception.
- `e170f218bdfce3c5124faba2bb1938d4f8c7ee33` —
  registers `aiverse_terraweave_forest_scene_test` in CMake.
- `ed67760eb9c9d6b18cb841d6137360608c34ea47` —
  includes the new target in isolated GCC/Clang workflow.
- `e31b0fe1c820a0db9980b39c610953ddfcdb393d` —
  `docs/terrain/TERRAWEAVE.md`: public provider API and limitations.

**Design:** The generic forest host does not depend on any genre or
specific MapDoc/WorldScene collision representation. Caller supplies
an immutable snapshot-based height/predicate provider. Placed trees
remain cosmetic; geometry reuse, collision, ECS and GPU ownership do
not change. No extra RHI backend. Applying providers changes only
the final height/allow-list, NOT the underlying procedural noise
biome/slope prefilters. A project requiring physically correct
authored-surface biomes must supply/extend a full terrain evaluation
provider in a subsequent stage.

**Actual verification:** C++ code and test files committed via
GitHub connector. No local CMake compilation, CTest, editor launch,
GPU screenshot or performance test observed. Container GitHub
DNS/network lookup failed during prior steps. The user had previously
deferred full tests: do not report them as passing.

**Next agent:** integrate the provider with the existing
`WorldScene` terrain/collision snapshot (with exact coordinate
conversion), and build the typed road/water/structure exclusion
provider. Prefer immutable snapshots for eventual worker threads,
with ownership/lifetime and stale revisions documented. Then
implement bounded asynchronous patch jobs or reuse the existing
`AsyncChunkStream`, rather than generating terrain synchronously
on the renderer thread. Add targeted tests for overridden height
matching physical collision, camera origin rebasing, and stable
multichunk persistence. After formal tests resume, compile all eight
isolated TerraWeave CTest targets and optional `terraweave_editor`;
inspect Metal/GL/Vulkan screenshots and stats.


## Stage 6 — authored-world forest alignment, mask correctness and persistent tree identities

**Branch**: `feature/terraweave`, PR #24 remains Draft. Files/commits
completed in this stage:

1. `9435bb8d9f64a8a82a0d5166fe8c8ee5c0a06b0b` —
   `engine/world/TerraWeaveForestScene.h`: fixed both sync
   `streamAt` and async `preparePatch` so a **standalone**
   `setPlacementMask` works even with no height or predicate callbacks.
   Previously the mask was silently skipped in that case.
2. `ca3c1e63648e76408984279f060ba1afbe52623e` —
   new `engine/world/TerraWeaveWorldHeightSource.h`:
   immutable `AuthoredWorldHeightSnapshot` and typed height tile,
   triangulated interpolation matching WorldScene collider mesh,
   signed global world centimetres with negative-safe cell indexing.
3. `7c0c8d5453f507d1dbc3e83c16aac2067dbcaea1` —
   `engine/world/TerraWeaveForestScene.h`:
   `setLandscapeSources(HeightSource, shared_ptr<const FoliageExclusionMask>)`
   publishes a single coherent height/footprint source revision rather
   than invalidating all queued async work twice.
4. `2fd9f5e1ab92832df63d9a9783e0a24a994523d3` —
   `engine/world/WorldScene.h`: optional `Prepared` per-chunk
   height samples and footprint geometry.
5. `3aa68800b4551a251f2020cb0bbbe2dc4425a68b` —
   `engine/world/WorldScene.cpp`: convert loaded terrain Fixed
   metres to centimetres during worker preparation, account snapshot
   CPU footprint against chunk reservations, rebuild one immutable
   resident terrain + collision-box mask when a relevant chunk is
   published/unloaded, and publish both to the forest via one
   revision-safe call. Fallback to procedural height if no loaded
   authored terrain tile covers a sample. Does not run on legacy
   scenes lacking `terraweaveForest`.
6. `f2f063d6f1ab46b9c2136dc47d7f554f478ce3dc` —
   `engine/world/TerraWeaveExclusions.h`: spatial bucket lookup for
   collision building footprints (64 m cells, >256-bucket shapes in
   an overflow list). Avoid scanning all streamed building boxes
   for every forest candidate.
7. `2a7304027c7663f3be79b712d94273e8556125a1` —
   `engine/world/TerraWeaveForestScene.h`: stable
   `ForestTreeIdentity{key,speciesId}` removal/restoration API and
   sortable snapshot for native game save integration; filters only
   `visibleBatches()`, so the same tree stays removed after cache
   eviction/regeneration. Bound set at 1,000,000 identities and expose
   a `removed` count in scene stats.
8. `427a2f3ccd4d6b25019503264ecb1082b1bda8da` —
   `docs/terrain/TERRAWEAVE.md`: implementation contracts, units,
   limitations, and examples.

**Correctness contract:** The typed authored source matches the
triangulation used by `readChunkCollision`, with centimetre-level
rounding; it never updates simulation physics or authoritative ECS.
The snapshot and mask are immutable during worker use; old
snapshots stay owned by callbacks captured in a revision of
`TerraWeaveForestScene`. No worker may mutate GPU state or ECS
structural data. A changed landscape invalidates previous foliage
patches. All tree positions remain signed cm until the render adapter.

**Compatibility and limitations:** Unmodified/disabled WorldScene
manifests are unchanged. Only a manifest with explicit
`terraweaveForest` includes this host integration. Resident authored
heightfield tiles are used; unloaded tile samples fall back to
procedural noise and can exhibit visible terrain boundaries.
Collision boxes are conservatively projected to X/Z regardless of Y;
roads/water/complex meshes need their own explicit masks.
The scatter core still chooses species by its own procedural
biome/slope, so an authored steep/underwater surface needs a custom
placement policy for correct ecology. Rebuilding a snapshot when
terrain residency changes invalidates all current async patches,
which may cause temporary missing trees during frequent chunk churn.
`removedTrees()` is a visual state export, not an authoritative
save/network/destruction system and needs generator version/seed
matching for persistence.

**Actual tests:** user explicitly requested to run tests later.
Only GitHub file reads and successful code/doc commits were
performed; **no CMake/CTest, runtime, GPU screenshot, performance
benchmarks, headless cross-genre replays or CI success verified**
for this stage. These implementations must be considered
unverified until the user resumes testing.

**Next agent instructions:**
- Inspect HEAD and current file SHAs before editing, because other
  agents are committing simultaneously on this feature branch.
- Once tests resume, build the TerraWeave targets + `aiverse_world` /
  `aiverse_editor`, exercise negative-coordinate tile edges,
  irregular terrain resolutions, collision-box footprints, sync and
  async mask-only settings, concurrent landscape revisions, undo
  of editor heightfield changes, and persistence across patch unload.
- Attach the native save layer to `removedTrees()` with generator
  fingerprint/version; add opt-in gameplay tree interaction ECS,
  collision/navmesh footprint updates and network replication using
  generic mechanics modules, not genre-specific one-off code.
- Consider per-tile revision invalidation rather than invalidating
  the whole forest when a single WorldScene tile arrives; this is
  a performance enhancement, not yet implemented.
- Finish true multi-view impostor image-quality work and capture
  actual Metal/GL/Vulkan outputs when visual tests resume.
- Append the next stage to this document, recording real results.

## Stage 6 — bounded async forest jobs + typed exclusion geometry (COMMITTED; NOT TESTED)

Work on this branch was coordinated with a parallel agent; retrieve
current HEAD before changing anything under `engine/world`.

**Commits:**
- `833582c98bef6cbd1dc452467198dc1bf0258c36` and
  `76704ec0469743ce98b1b7a2d1a3222d775a0609`:
  `ChunkStreamer.h/.cpp` supports dynamic removal of catalog cells.
- `6edaaee27906c71c0cc93e34f0dbea707a560185` and
  `3015096bb6a7692d753f0b45a0b93a47680654c5`:
  `AsyncChunkStream.h/.cpp` can remove canceled dynamic cells,
  retaining staging reservations until completed/canceled work drains.
- `1a9c083964089440b27fc6eaaa96545072b812a2` and
  `7a4b7466e33c4c993fba9bcaf16449e5f0596ac6`:
  `TerraWeaveForestScene.h` adds opt-in worker scheduling,
  bounded chunk reservations, snapshot revision tokens,
  stale-result rejection and render-thread publish.
- `478b5de3ef06bf142c1841d863f9a5a962784ac9`:
  `TerraWeaveForestManifest.h` enables bounded asynchronous
  generation by default only for the opt-in WorldScene manifest.
  Direct native `ForestSceneSettings` construction remains synchronous
  unless opted in.
- `17c0f3a94f08152190ab669065234f23445b3667`:
  `WorldScene.cpp` joins forest worker before shutdown.
- `79f9928de1bd085960c3557b26003855da1c18f9` and
  `cb762cf2fd9724b3f10a6909fe6ef62afecbe1ce`:
  `TerraWeaveExclusions.h` (buffered road segments, rectangular
  structures, footprint/water polygons) and
  `TerraWeaveForestScene.h` mask callback support.
- `80aa190851de58a9452ef2fb7c7f156bba0c0409`:
  forest scene CTest target now links the existing
  `aiverse_world` streaming library needed by async methods.

**Verification:** GitHub commits succeeded. No C++ compiler/CTest or
runtime race test observed. Note that another agent subsequently
added `setLandscapeSources`, authored terrain snapshots and fixes for
mask-only predicates; inspect its latest handoff/history rather than
reimplementing those changes.

**Known caveats:** asynchronous `HeightSource` and
`PlacementFilter` must capture immutable snapshots whose lifetime
outlives pending tasks; mutable ECS access would race. The bounded
worker queue is not proof of a frame-time budget. The typed exclusion
geometry is presentation-only, not an authoritative collision system.
Formal race, stale revision, fallback/mobile and lockstep validation
remain needed.

## Stage 7 — TerraWeave Horizon, aggregated forest/terrain HLOD (COMMITTED; UNVERIFIED)

**Goal requested:** forest and mountain/landscape visibility across
several kilometres without rendering a billboard/model per tree.

**Commits/files:**
- `c5cb0845897ebb16abe783e136a23f6f08323656`:
  `engine/world/TerraWeaveHorizon.h` creates coarse forest
  **cluster** silhouettes directly from deterministic climate and
  height samples, without individual trees, plus low-poly terrain
  sample grid and packed single RGBA8 atlas.
- `907b4a50febc3e18de16bb0f425f890427aeda96`:
  int64 clamp portability fix.
- `ff0fb85a8444921785fab8e01af3418a217f2ed2`:
  `HorizonBudget` and `estimateHorizonBudget` upper bounds for
  atlas bytes/triangles/vertices/cluster slots.
- `6bee547a2d7d5c882431d5a7d65e907d2a56746f`:
  `engine/render/TerraWeaveHorizonModel.h` combines coarse
  terrain + crossed alpha-masked forest cards into one
  `render::Model` with two material primitives and one atlas.
- `eb8e929e6255886302dd3263d29d3d423903e294`:
  terrain proxy slope normals from signed height gradients.
- `132b7e105c41ee041ce457c3d265c357da855e76` and
  `57aca4701df888cea0ff417af4be2892f7d52316`:
  `engine/render/TerraWeaveHorizonScene.h`, async region CPU
  preparation via existing `AsyncChunkStream`, stale region
  cancellation, render-thread GPU upload/rollback, keeping
  previous GPU model visible until successful replacement.
- `2bec856136910e37ffd3924c53a8bfb658d63055`:
  `engine/world/TerraWeaveHorizonManifest.h`, opt-in
  `terraweaveHorizon` config (can inherit seeds from near forest).
- `a675f775e27bddeb38f5e0753dd8a655761442cd`,
  `a85e3d93f7089495390d839e06c138acb71ef76b`:
  `engine/world/WorldScene.h/.cpp` owns Horizon with
  update/draw/shutdown, without changing scenes lacking opt-in.
- `2d2cf991143b270386fb9316cf7bda673db877e5` and
  `5f81dacdbfb4677c91a10a0302abcea7ed03261d`:
  `tests/terraweave_horizon_test.cpp` deterministic atlas,
  mesh topology/indices, resident budget limits and constructor.
- `6e2b270e96d7bbeeacbadbffbb1431fc66c4c070`:
  CMake test registration (links `aiverse_world`, models, zstd).
- `154d6025bbb0e22adb7e9f8aeaa70912d885c360`:
  isolated GCC/Clang workflow includes Horizon target.
- `ec476fb4f6ff6d510de28923c6c8168973c39ccf`:
  `docs/terrain/TERRAWEAVE.md` HLOD architecture, config,
  budgets and known visual limitations.
- `3e61539f58657c3b2d55db9fe6161dabd4bd193c`:
  `skills/world/aiverse-terraweave/SKILL.md` agent usage.

**Architecture:** nearby trees remain the existing ForestScene
LOD/geometry. At ~250m..1km the canopy is represented by 128m
cluster silhouette ribbons, at ~1km..6km by 512m clusters, using
one packed RGBA atlas and two crossed cards per cluster. One
coarse **48×48-quad** terrain grid is clipped around near ground.
Both terrain and forest share one Model with at most two material
primitives. Generic RHI upload/draw, not a new shader backend.
No per-tree ECS data or separate far-tree model for this layer.

**Theoretical worst-case default** (from `estimateHorizonBudget`):
832 possible cluster slots; atlas 1392×1392 RGBA8 =
7,750,656 bytes (~7.39 MiB), max 9,057 vertices and 7,936
triangles. These are **not actual GPU measurements**. Runtime
cost also includes mipmaps, texture upload, alpha overdraw, CPU
bakes and renderer passes.

**Verification actually performed:** GitHub file writes and
source/API inspection only. Container `git ls-remote` for
github.com fails DNS resolution. No CMake/CTest success, GUI
screenshots or frame-time/draw-call benchmarks have been observed.
Keep PR Draft. Do not claim the Horizon renderer visually passed.

**Critical remaining work:**
1. Run GCC/Clang and real macOS editor/runtime builds, CTest
   `terraweave_horizon` and pre-existing TerraWeave/world-async tests.
   Resolve any compiler, ABI, race, upload and depth-pass issues.
2. Test `terraweaveHorizon` in a real WorldScene with close/mid/far
   camera views and visual captures (Metal + Vulkan/GL), inspect
   silhouette atlas orientation, clipping, terrain boundaries and
   alpha-cutout material consistency.
3. Tune per-tier crossfade or stable dither near ~250m and ~1km;
   avoid HLOD popping or simultaneous duplicate individual/cluster
   trees. Compare CPU/GPU timings and memory on mobile/WebGL.
4. For static worlds, prebake/persist horizon atlases as assets
   (with fingerprint/version/hash), avoiding repeated CPU work on
   camera translation; implement horizon origin-shifting verification.
5. Distinct authored terrain vs procedural terrain may disagree:
   `distantTerrain` must be disabled or explicitly coordinated
   with native authored terrain ownership and biome masks.
6. Preserve stage-by-stage handoff and do not merge before tests.

### Horizon stage follow-up fixes (same day, still unverified)
- `b89901e8714ac651860993c1b98586376ad9f572`:
  `TerraWeaveHorizonScene.h` now retires an old GPU proxy if the next
  region has no visible clusters and `distantTerrain=false`
  (ocean/desert), preventing stale silhouettes.
- `d4bc70b51ad98c757cc232a1bae6bfa270df95cc`:
  `TerraWeaveHorizon.h` uses half-open mid/far distance bands,
  preventing duplicate card ownership on an exact LOD boundary.
No new build/CTest/GPU verification was performed for these fixes.

## Stage 8 — Horizon silhouette quality and staggered coverage (COMMITTED; UNVERIFIED)

**Date:** 2026-11-18. Branch: `feature/terraweave`, Draft PR #24.
Refresh the branch HEAD/individual file SHA before further edits;
other agents have been committing into this same feature branch.

**Commits/files:**
- `fabcac7aaa63ca6b0cfafc1f2f4ee423324562bf`:
  `engine/world/TerraWeaveHorizon.h` replaces uniform vertical
  foliage bands with deterministic overlapping rounded/tapered
  canopy silhouettes, colour/shading variation, alpha-cutout feather,
  sparse trunks and `HorizonSettings::crownDetail` (1–3).
- `7e64976e6294d069317a70de0fa7e3cbae76b884`:
  `engine/world/TerraWeaveHorizon.h` adds `nearBlendCm`,
  `tierBlendCm`, Q16 smoothstep coverage and world-hashed
  pixel masking to stagger near/mid/far cluster visibility.
- `1c0a928f850827a740181893c28bb7310a390e3b`:
  `engine/world/TerraWeaveHorizonManifest.h` exposes quality
  and blending parameters; when both Horizon and near Forest
  blocks exist, closeForestEndCm defaults to near Forest
  `cullRangeCm` rather than leaving an uncovered 240–250m band.
- `44875e003c118a268f8098656bd9532f98c4f654`:
  `tests/terraweave_horizon_test.cpp` covers deterministic
  canopy colours, near band visibility, smoothstep, invalid settings.
- `4cc15e342575ea77986b5e0ae74ea3772d3d7c04`:
  `engine/world/TerraWeaveHorizon.h` adds
  `HorizonSettings::orientationViews` (1 cheap / 2 quality)
  and `HorizonCluster` second atlas-frame coordinates.
- `fe3b644a595afebeb503cb99f931098ee5de74e3`:
  CPU Horizon baker renders independent X/Z silhouettes per
  cluster into a **single packed atlas** at 2-view quality.
- `6610b3a233a3ddbcb2c50b1af22663714528e6cb`:
  `engine/render/TerraWeaveHorizonModel.h` selects distinct
  subrect UVs for each crossed card orientation.
- `860ee5aaa3c66a2267eb915f6be51de1c257e9b2`:
  `tests/terraweave_horizon_test.cpp` verifies 2-view atlas
  bounds, distinct frames, unchanged indices/triangles and
  invalid view count.
- `30013eb337377379824f23e9e1933b55ebae7c0b`:
  `TerraWeaveHorizonManifest.h` parses `orientationViews`.
- `6b907d5553dd0591130606a577bb917a9ab91963`:
  `engine/render/TerraWeaveHorizonScene.h` captures
  `HorizonRuntimeStats`: actual live CPU preparation and GPU
  upload ms, atlas bytes, vertices, triangles, clusters,
  pending/discarded async jobs. These counters are *not*
  measured GPU kernel timings.
- `c28ebe28a2d9a77707afaac1932a9b970b059277`:
  horizon test verifies zero-initialized runtime counters
  before a GPU model is submitted.
- `df8d06b28e0f3edebd66fbd4f5ac65f838b6f7b4`:
  `docs/terrain/TERRAWEAVE.md` documents all settings,
  limitations, theoretical memory and runtime counters.

**Architecture**: Entire far forest remains one Model with two
primitive/material ranges (coarse terrain + masked cluster cards),
regardless of `orientationViews`. No individual foliage
ECS entities, GPU shader fork or per-cluster submissions.
Deterministic alpha-to-coverage is **baked per snapped
region**, avoiding a synchronized hard 250m/1km switch. It
does not continuously crossfade on every camera frame.
A 2-view atlas uses two orthogonal captured silhouettes and
one shared texture; default 1-view stays ~7.39 MiB vs
2-view ~14.77 MiB before mipmaps, triangle budget unchanged.
Higher crownDetail increases only CPU raster work.

**What is tested vs NOT tested:** Changes were inspected at
source level and successfully committed using GitHub connector.
Tests were extended, but **no successful compilation, CTest
execution, editor launch, Metal/GL/Vulkan screenshot or real
frame-time measurement has been observed**. Do not mark
this PR ready for merge yet. Previous GitHub DNS/clone
limitations prevented authoring-container checkout.

**Critical acceptance for next agent**:
1. Build `aiverse_terraweave_horizon_test` plus
   `aiverse_world` and `aiverse_editor` using real Clang
   and GCC. Run `ctest -R 'terraweave|world_async'`;
   record failures and compiler/linker diagnostics.
2. Load opt-in `terraweaveHorizon` with
   `orientationViews=1` and `2` on macOS Metal
   and one GL/Vulkan device. Orbit camera and compare
   near/mid/far forest; inspect alpha-mask edges,
   texture page UV bleeding, double draw and pop.
3. Exercise boundary conditions at 240m, 250m,
   920m, 1000m, 1080m and camera-region snap
   boundaries; compare one rendered atlas across
   equivalent seeds/platforms and log memory/draw
   metrics from `runtimeStats()`.
4. Assess whether spatial stochastic fading is
   sufficiently stable. If not, implement true
   GPU-temporal dither/crossfade through shared
   renderer shading (GLSL+MSL) with an appropriate
   transient memory/overdraw budget; do not claim
   true frame-by-frame fading at present.
5. For static worlds, cache prebaked atlas models
   with generator-version fingerprints; keep
   distantTerrain off when a separate authored
   landscape renderer covers the horizon.
6. After each actual work phase, append new
   commits/tests/limits to this handoff.

## Stage 9a — authored Horizon terrain/biome/forest masks (CODE COMMITTED, TESTS DEFERRED)

**Commit:** `a96408462d8e9e753ff114e1499ecbb32c16f1ef`.
**Branch:** `feature/terraweave` / PR #24, draft.
**Changed source:** `engine/world/TerraWeaveHorizon.h`.

**Public API:** `HorizonSurfaceSources` provides optional immutable
`heightAtCm`, `biomeAt`, and `allowsCanopyAt` callbacks to
`bakeHorizon(..., const HorizonSurfaceSources* = nullptr)`.
Pre-existing procedural sampling is the fallback for absent
callbacks. Crown samples are masked before atlas rasterization.
No shader/renderer/backend code changed; old callers still compile
conceptually without changing source.

**Actually performed:** GitHub source inspection and successful
file update. **No build, CTest, GPU screenshot or performance
measurement performed**, per user instruction: another agent owns
testing. DO NOT claim this feature passed.

**Next scoped code task:** expose a shared snapshot setter on
`TerraWeaveHorizonScene`, invalidate only its currently pending
region, and connect it to resident authored-world height/masks in
`WorldScene`, without racing the existing worker or touching ECS.
Follow with stable region caching and transition ownership.
**Tester handoff:** add coverage for supplied sample callbacks,
water/road suppression, same terrain snapshot in near/far paths,
GPU atlas preview and mobile fallback.

## Stage 9b — Horizon worker source ownership and authored WorldScene integration (CODE COMMITTED; NOT TESTED)

**Branch:** `feature/terraweave`, draft PR #24.
**Commits/files:**
- `37d8b85a5c357fdaa5216823a8bfc10595009085`:
  `engine/render/TerraWeaveHorizonScene.h` introduces
  `setSurfaceSources(shared_ptr<const HorizonSurfaceSources>)`,
  atomically captured by an async worker. Cancels old region
  selection on source changes but keeps the last uploaded model
  until successful GPU replacement. Adds read-only
  `terrainSettings()/biomeSettings()` for owning host adapters.
- `b124382a81888673a9d14ce4db68c5a7916d805d`:
  `engine/world/WorldScene.cpp`: authored collision terrain
  height snapshot and obstacle mask are now assembled for either
  opt-in near forest or opt-in Horizon, not only near forest.
  `Horizon` captures the resident immutable height tiles and
  collision-box mask; unloaded far terrain falls back to its own
  procedural height seed.
- Stage notes appended to `docs/terrain/TERRAWEAVE.md`
  and this agent handoff.

**Behavior/API:** The user can create a far landscape using
their actual authored collision heightfield within loaded regions
and suppress distant tree canopy where buildings/obstacles
have authored boxes. No new GPU shaders or per-tree entities.
Callbacks are resolved only in background CPU preparation and
own read-only snapshots via shared pointers. Source changes
cancel stale region jobs; main-thread GPU resource handling
remains unchanged.

**Actual verification:** GitHub connector writes and source review
only. **No tests/build/CTest/CI/screenshot executed**, per explicit
user decision: another agent is responsible for all validation.

**Next scoped functionality:** reduce recomputation of nearly
identical Horizon atlases when the camera crosses a small
streaming cell: add bounded atlas/model caching and stable
region-key hysteresis; consider static asset bake/cache
fingerprints. Additional incomplete behavior: far terrain only
matches *resident* authored height tiles, not unloaded chunks;
terrain ownership overlap and true temporal crossfade still
require design and renderer validation.

**Tester handoff:** validate terrain matching, road/building
mask discontinuities, snapshot change during pending CPU job,
old GPU proxy retention and destruction order, optional
Horizon-only scenes, and no-effects when both modules absent.

## Stage 9c — immutable Horizon atlas LRU and camera hysteresis (CODE COMMITTED, TESTING DELEGATED)

**Branch:** `feature/terraweave`, PR #24 Draft, base
`feature/universal-entity-inspector-fps`.

**Commits/files:**
- `2de0338b2d0ee2e722e63db9f1d99929973d48d4`:
  `engine/world/TerraWeaveHorizon.h` adds
  `maxCachedRegions` (0–4, default 2) and validation.
- `71ed672d84c4c8fbd5a2666501f410fa7cb67852`:
  `engine/render/TerraWeaveHorizonScene.h` introduces
  thread-safe bounded LRU of `shared_ptr<const HorizonBake>`
  snapshots keyed by `world::Cell`, with coherent source
  generation tokens, safe cache invalidation and ownership
  of old surface generations through in-flight workers.
  `HorizonRuntimeStats` adds `cachedRegions/cacheHits`.
  Worker still prepares a new render::Model for each CPU-cache
  hit and re-uploads on the RHI thread; no GPU cache is claimed.
- `2542ff4c43789c370279925dfb33f3642117926d`:
  `engine/world/TerraWeaveHorizonManifest.h` exposes
  `maxCachedRegions` setting (0 disables CPU retention).
- `91617b912864037a891073c6ab2ad43a29abdf66`:
  `engine/world/TerraWeaveHorizon.h` adds
  `regionHysteresisCm` (default 3200, less than ½
  `middleTileCm`) and validation.
- `3d05d2be12e166171456e42dc1b04c4adbb72cdc`:
  `engine/render/TerraWeaveHorizonScene.h` holds the
  previously selected region within an expanded cell before
  requesting a new CPU bake.
- `1be779f0041616a3f73aded5f9985a71c533c819`:
  manifest exposes `regionHysteresisCm`.
- Additional stage explanations in `docs/terrain/TERRAWEAVE.md`.

**Implemented vs pending:** CPU re-bakes are skipped for the most
recent region(s) unless authored surface revision changes. A
bounded hysteresis prevents rapid streaming at cell boundaries.
The renderer still owns one GPU model only; there is no disk
atlas cache and no per-frame animated transparency. The
cache does not eliminate CPU-to-GPU upload cost.

**Verification:** GitHub writes and source inspection performed.
The user explicitly assigned ALL tests to a different agent:
**NO builds, CTests, benchmarks, live renders or screenshots
performed or claimed.** Remain Draft.

**Next implementation-only scope:** implement an engine-generic
Horizon quality preset/API for mobile/desktop and a future optional
static-world pre-baked atlas asset adapter; do not create or execute
test targets in this agent's work. The other agent should validate
cache misses/hits, source revision races, camera jitter near
128m cell edges, memory during one/two atlas allocations,
hysteresis after large teleports and GPU rollback on failed upload.

## Stage 9d — native Horizon quality tiers (SOURCE COMMITTED; TESTS ASSIGNED TO OTHER AGENT)

**Commits/files:**
- `3b7a0fe487c6c0e6fabc928d614211f2e4c57565`:
  `engine/world/TerraWeaveHorizon.h` adds
  `HorizonQualityPreset` enum and
  `horizonSettingsForQuality` for minimal/mobile/balanced/high.
  Constructor validation remains centralized in
  `validateHorizonSettings`; presets do not modify ECS or RHI.
- `d76d35bea787168f983f32e6699c3be3420ded76`:
  `engine/world/TerraWeaveHorizonManifest.h` parses
  `terraweaveHorizon.quality` and applies per-field overrides.
- API matrix documented in `docs/terrain/TERRAWEAVE.md`.

**Behavior:** Minimal/mobile use a 16/24px atlas tile, 16/24
terrain grid, one-view projection and no cached CPU regions.
Balanced retains the previous defaults; High selects two-view,
denser crowns and a 64-resolution distant terrain grid, with
the same number of forest cluster cards. Genre independent,
opt-in manifest only; unconfigured worlds remain unchanged.

**Verification performed:** GitHub write operations and manual
source inspection only. **No CMake, CTest, CI inspection, GPU
renders, screenshot comparisons or benchmark executed** because
the user assigned tests to a different agent. Other agent should
validate every preset/field combination on desktop and mobile.

**Next functional work:** optional pre-baked persistent asset
cache for static-world Horizon regions to avoid CPU rasterization
across restarts, with version/fingerprint and strict size bounds;
full per-frame crossfade would need shared Renderer3D shader
semantics and actual visual validation.

## Stage 9e — static Horizon archive format and game-pack read path (CODE COMMITTED; TESTING DELEGATED)

**Branch:** `feature/terraweave`, draft PR #24.

**Commits/files:**
- `05d3c91caf799ebc7e2ab74505bb26743b8f96a5`:
  new `engine/world/TerraWeaveHorizonArchive.h`,
  pure little-endian versioned TWH1 archive encoder/decoder,
  bounded data size, FNV integrity and deterministic settings
  fingerprint with optional authored content hash.
- `936a55fea569ff6e38d2bcc4420523c3163e3ede`:
  atlas-frame bounds hardening in archive parser.
- `b1bf905092dcc15b4ae3e4689bd815151d7479bc`:
  `engine/render/TerraWeaveHorizonScene.h` adds
  optional `HorizonArchiveStore` callbacks, source fingerprint
  admission, cache-first → archive-read → CPU-bake flow,
  best-effort fallback on corrupted/missing assets and
  `HorizonRuntimeStats::archiveHits`.
- `73d338f50913d4b03ea1a3c1785732462d1fa067`:
  `engine/world/TerraWeaveHorizonManifest.h` accepts opt-in
  `archivePrefix` and optional `archiveContentHash`,
  builds a read-only `AssetReader` callback for files
  `<archivePrefix>/<cellX>_<cellZ>.twh`.
- `e338715df9be50641905f458986a0ffb568e9758`:
  `engine/world/WorldScene.cpp` passes its content reader
  to the Horizon manifest adapter.
- API/manifest/limitations detailed in
  `docs/terrain/TERRAWEAVE.md` § Prebaked Horizon archive assets.

**Functional behavior:** Native project build pipelines can
encode stable region atlases at asset-build time and ship them
for runtime reuse. The old codepath is entirely unchanged
unless archivePrefix or setArchiveStore is opted in.
Archive reads are performed on the existing background worker,
using a trusted game-supplied reader rather than direct filesystem
operations. A valid cache skips expensive biome/crown sampling;
the ordinary render::Model conversion/upload still occurs.
Any invalid archive simply falls back to baking; missing data
must never crash a scene. Authored external sources require a
nonzero stable authored-content fingerprint for disk cache use.

**Actual verification:** GitHub source writes and static code
inspection. **No compilation, CTest, CI run, GPU capture,
native app invocation, archive round-trip or performance run**:
the user explicitly assigned tests to another agent. The
implementation is unverified. Do not merge the Draft PR.

**Tester agent exact work:**
1. Configure and compile `aiverse_world`,
   `aiverse_terraweave_horizon_test` and the editor for
   AppleClang/GCC/Clang; expand tests for round-trip, magic,
   wrong content hash, FNV checksum, out-of-bounds fields,
   truncated arrays and two-view atlases.
2. Confirm archivePrefix path safety and packed asset
   load, missing/invalid asset regeneration, async worker
   shutdown when read callback is slow, no stale archive
   after source generation changes, and GPU upload rollback.
3. Compare actual tree and terrain geometry of
   `encode→decode` vs regenerated Horizon, including
   negative region coordinates, 16 MiB atlas budget and
   mobile preset.
4. Record test logs and actual GPU/CPU benchmark
   metrics and update this handoff; testing owner may fix
   implementation defects found.

**Next function-focused agent:** engine-native offline asset
bake command/pack integration and optional per-region patch
invalidations instead of invalidating a whole 12km atlas when
only one authored terrain tile changes. Preserve
genre independence and the per-stage handoff rule.

## Stage 10a — bounded offline Horizon batch API (COMMITTED; NOT TESTED)

- Commit `67c569b2fe9bda594c10e01df5853e60e924d81a` adds
  `engine/world/TerraWeaveHorizonBatch.h` (C++20 header-only,
  no RHI/genre/filesystem dependency).
- `HorizonBatchBounds` selects half-open signed Horizon camera-grid
  region keys (exactly the keys used by runtime `HorizonScene`).
  `bakeHorizonBatch` iterates deterministically z-major then x-major,
  limits work to `maxRegions` (default 4096), refuses external authored
  snapshots without a nonzero stable authored content fingerprint,
  skips matching valid `TWH1` archives through optional read callback,
  regenerates stale/corrupt archives, forwards bytes through a required
  asset-writer callback, supports cooperative cancellation and reports
  visited/generated/skipped count and output bytes. It does not create
  directories or write files from engine code; native build tools own that.
- Region centre uses key*middleTileCm + middleTileCm/2 to match
  `TerraWeaveHorizonScene` and `<cellX>_<cellZ>.twh` filenames.
- **Verification:** source committed via GitHub; build, CTest,
  runtime archive regeneration and filesystem operation NOT RUN, per
  user's explicit functions-first/testing-by-other-agent split.
- **Next functional step:** expose the batch API via a host-only CLI
  `aiverse_horizon_bake` using the same manifest parser, without
  changing gameplay, and register it as an opt-in build tool. Add
  docs for exact invocation. A separate test agent validates
  region-key edge cases, stale hashes, partial outputs and cancellation.

## Stage 10b — host-only bulk Horizon archive CLI (COMMITTED; NOT TESTED)

- `7a5ad0698dd1da1b81d0634f4fa933a2e4593119`:
  `engine/render/TerraWeaveHorizonScene.h` exposes read-only
  `settings()`, allowing the host tool to reuse the same manifest
  configuration used by runtime.
- `4e17b7139dcd5415163b3647831d07e6b1d3c75b`:
  `tools/horizon/main.cpp` CLI `aiverse_horizon_bake`:
  `--manifest <world.json> --out <dir> --cells minX minZ maxX maxZ`
  with `--force`, `--max-regions N`, `--quiet`.
  It reads the same Horizon manifest, resolves procedural seed
  inheritance from an optional near forest block without generating
  any near tree models, creates the output directory, uses stable
  <cellX>_<cellZ>.twh asset filenames, skips valid cached archives,
  and stages writes to a `.partial` file before rename.
  No runtime filesystem writes, GPU device or ECS changes.
- `a1e59600763933b02c9a10133d9ddba3b5466a0e`:
  `CMakeLists.txt` registers `aiverse_horizon_bake`
  for `AIVERSE_BUILD_TOOLS=ON`, non-Emscripten, linked against
  the existing model/world libraries; it is an ordinary optional
  build target, **not a new test**.

**Example (once built by the tester/other agent):**
```sh
./build/<config>/aiverse_horizon_bake \
  --manifest content/world.json \
  --out content/world/horizon-cache \
  --cells -4 -4 4 4
```
Pack the resulting `*.twh` files as normal game assets, and set
`terraweaveHorizon.archivePrefix` to `world/horizon-cache`.

**Scope limits:** CLI bakes *procedural* terrain/biomes only and
does not have access to independently authored collision tiles,
painted biomes or water/road maps. Native projects can use
`bakeHorizonBatch` with immutable `HorizonSurfaceSources`
and a nonzero `authoredContentHash` for that path.
Windows replacing a pre-existing `.twh` may require an explicit
file replacement policy (rename-over-existing has platform
differences); current implementation reports an error rather
than deleting the old asset.

**Actual verification:** GitHub commits and source review only.
No builds, tests, CLI execution or filesystem writes were
performed. The user delegated all testing to another agent.
Next functional stage is per-region selective invalidation
for authored modifications, avoiding full CPU atlas re-bake.

## Stage 10c — stable Horizon atlas tile ownership (CODE COMMITTED; NO TESTS)

- `9b25ec5ad8edf7c3ac4d3b6f656ff92a2a42e4c6` updates
  `engine/world/TerraWeaveHorizon.h`. Both mid/far cluster
  atlas subrectangles now use stable dense lattice slot keys:
  `(layerBase + iz*count + ix)*orientationViews`. Cluster
  presence/biome/road exclusion no longer shifts UVs for unrelated
  tiles. This is a prerequisite for partial atlas regeneration.
- `517d2cf3112f775a75c17f2e466eb6d5ef83a201` updates
  `engine/world/TerraWeaveHorizonArchive.h`: incompatible
  packed-atlas assignment advances archive wire magic/version to
  `TWH2` / version 2. Old TWH1 is deliberately rejected by the
  decoder and regenerated by the batch CLI/runtime fallback.
  File extension remains `.twh`; generator fingerprint changes.
- **Actual verification:** GitHub source committed only. NO
  compilation/test/benchmark/GPU run (explicitly delegated).
  Another agent must update tests expecting TWH1 and verify
  fixed tile positions across biome/road changes and negative keys.
- **Next:** implement `bakeHorizonPartial` using stable slots to
  rebuild only dirty atlas tiles and terrain vertices, then
  connect it to `HorizonScene::setSurfaceSources` and WorldScene
  changed tile bounds. Preserve prior GPU model until updated.


## Stage 10d — partial Horizon CPU bake and authored tile invalidation (CODE COMMITTED; NOT TESTED)

**Branch:** `feature/terraweave`, Draft PR #24.
**Commits/files:**
- `e287a8324d9c815a7ce8a88de819e6961103a203`:
  `engine/world/TerraWeaveHorizon.h`, new optional
  `bakeHorizon(..., source, previous, edited)` parameters.
  Compatible old bakes reuse the RGBA atlas and unchanged
  cluster records by stable `(x,z,tileSize)` identity,
  clearing only edited atlas slots and rewriting affected
  terrain vertices; if the old bake doesn't match the camera
  region, do a full deterministic bake. Dirty bounds are
  checked and nearby cluster samples are conservatively
  considered affected.
- `3b0d895b2d84e5ad9f925b4dadc6e23f48f76ee6`:
  `engine/render/TerraWeaveHorizonScene.h` captures a
  `shared_ptr<const HorizonBake>` baseline, region edits
  and immutable source revisions; background jobs use
  the partial CPU bake when eligible. Stale authored
  static archives are bypassed during localized updates.
  Current GPU model remains until the replacement uploads.
  `maxCachedRegions=0` avoids retaining the prior CPU bake.
  If a changed area is outside current horizon footprint,
  the model stays resident rather than immediately rebaking.
- `4ad8136bed69cf83013753747f5ec5e386ffa61b`:
  `engine/world/WorldScene.cpp` records changed authored
  heightfield/obstacle cells from worker publish/unload,
  converts them into bounded world-centimetre half-open
  rectangles and publishes those to Horizon alongside the
  immutable authored landscape/mask snapshot.
- Stage description and API contract added to
  `docs/terrain/TERRAWEAVE.md`.

**Changed behavior:** No per-tree ECS entities or new GPU
renderer. Near forest still uses revision-safe resident
chunks, while Horizon can now avoid full CPU canopy/height
re-evaluation for small landscape edits. A full combined
`render::Model`/texture upload is still required; no
partial GPU update is claimed. TWH2 stable atlas slots are
a prerequisite, so generator archive version was bumped in
Stage 10c.

**Testing:** NOT RUN by explicit user instruction.
**Separate testing agent's acceptance:** complete Clang/GCC
compiles, CTest and negative/positive region keys; compare
full bake vs partial bake byte-for-byte for same source;
verify edited road/water polygons, repeated dirty rectangles,
terrain vertex boundary normals and no-change updates;
teleport/cancel/resume jobs, archive source-hash invalidation,
GPU rollover and FPS/RTS replay neutrality; retain actual
logs/measurements/screenshots.

**Next function implementation:** avoid re-uploading the
entire atlas for small edits using existing RHI texture
subregion API (if supported across Metal/GL/Vulkan), or
introduce bounded tile paging with compatibility on
WebGL/mobile; do NOT fork graphics backend. Another
potential improvement is incremental tree rendering in
the existing editor and static-world asset bake integration.

## Stage 10 completion checkpoint — functions only, no verification

**Branch:** `feature/terraweave`, Draft PR #24; last verified remote
feature source head before this checkpoint: `00cda4672a3310c8dd289cf52438f4a92bbcf52f`.
The PR remains separate from the base branch; no merge into main.

**Features shipped in stage 10:** bulk offline native Horizon atlas
encoder and host executable, stable fixed atlas slots and TWH2
format revision, asynchronous region-source ownership, optional
partial CPU atlas/terrain rebuild for world edits, WorldScene
publication of changed authored tile bounds. The head includes
all docs and `skills/world/aiverse-terraweave/SKILL.md`
changes (latest skill commit `00cda4672a3310c8dd289cf52438f4a92bbcf52f`).

**Tests actually executed:** NONE. Tests and screenshots were
explicitly delegated to another AI agent; even a successful
source push is not evidence that the implementation compiles.
No CMake, ctest, CI, GPU render, asset roundtrip or perf
measurement was performed in this feature-implementation turn.

**Remaining limitations / next functional owner:** full GPU
model/atlas upload after partial CPU edits; preview/authoring
support for painted biomes and region-level road/water provider
beyond the existing resident collision-box masks; static-world
authored surface offline batch adapter; optional RHI texture
subregion upload or page-oriented HLOD resource model after
backend API parity review. A separate **test agent** must compile
and execute the full Stage 10 matrix listed above and record
actual failures/results without silently marking NOT RUN as PASS.

## Stage 11a — merge main into feature/terraweave before further work (COMPLETE)

**User request:** First bring latest `main` into the active feature branch,
without merging unfinished TerraWeave into `main`. Do not spend this
feature stage running tests (dedicated test agent owns validation).

- Branch before merge: `feature/terraweave` at
  `00cda4672a3310c8dd289cf52438f4a92bbcf52f`.
- Main at merge: `9ee6ff6fbf390bd45363bc92c838f492dfe92eee`.
  The branches were diverged, with 325 main-only and 258
  feature-only commits in the GitHub compare view.
- Integration PR **#30** (head `main`, base
  `feature/terraweave`) was merged using GitHub's merge method.
  Resulting merge commit:
  **`7f1cedde055a58e0b12a224a6e2f9b1dfd89b8d0`**.
- Independent verification from GitHub compare:
  `base=main`, `head=feature/terraweave` returned
  **status=ahead, behind_by=0**, merge base exactly
  `9ee6ff6fbf390bd45363bc92c838f492dfe92eee`.
  PR #24 remains Draft and continues to target
  `feature/universal-entity-inspector-fps`. Branch
  `main` is unchanged by this integration.
- Following the merge, inspected `tools/horizon/main.cpp`,
  `TerraWeaveHorizonBatch.h`, and preceding handoff. A
  parallel agent already implemented batch offline archive
  baking, TWH2 stable slots, and CPU partial region invalidation.
  **Do NOT duplicate those modules.** Next functionality is
  partial GPU resource update/paging where existing generic RHI
  supports it, or another independent high-value feature.

**Actually tested:** GitHub merge and ancestry compare only.
**CMake/CTest, app execution, GPU tests and performance not run**
(per explicit separate testing-agent ownership). No success
claim is justified. Do not change unrelated main features.

**Next agent:** fetch current HEAD before source writes because
other agents commit concurrently; preserve `TWH2` archive
semantics, keep `Horizon` renderer through standard
`gfx::Device`, update this handoff after the next functional
implementation stage.

## Stage 11b — GPU atlas subregion patches via existing gfx::Device (SOURCE COMMITTED; TESTS DEFERRED)

**Branch:** `feature/terraweave`, draft PR #24.
**Commit IDs and changed files:**
- `20fe5898a06ed0908f99a971bb313b8249e28a82`:
  new `engine/render/TerraWeaveHorizonGpuPatch.h`.
  Pure `horizonCanReuseModelGeometry(oldBake,newBake)`
  checks all far terrain vertices (including colour), clip center,
  atlas dimensions and all crossed-card geometry/UV metadata.
  `planHorizonAtlasUpload` finds changed fixed atlas tiles,
  coalesces adjacent runs, limits updates (default 6) and total
  updated pixel bytes (default 25% of atlas). Returns a nullopt
  for changes that require a full model swap.
  `applyHorizonAtlasUpload` validates every rectangle, checks
  resident RGBA8 image ownership and calls only existing
  `gfx::Device::updateTexture` on render owner thread.
- `6c34cbb513de0a62d63045bf2a912b226f3e7591`:
  `engine/render/TerraWeaveHorizonScene.h` integrates
  the opt-in patch fast path at publication. If the current
  resident GPU model and baked CPU baseline have identical
  geometry, only changed atlas subregions update; otherwise
  the preexisting full model upload/release path remains.
  New `HorizonRuntimeStats` fields:
  `atlasPatchCount`, `atlasPatchBytes`,
  `atlasPatchRegions` and `fullUploadCount`.

**Important architecture:** Existing native `gfx::Device`
already implements partial `updateTexture` and mipmap
regeneration on GL/Metal/Vulkan, confirmed by inspecting
their source. No new shader, backend specialization,
per-cluster draw submission, or gameplay ECS changes.
The old shared GPU geometry and one GPU atlas are reused
only if all visible geometry fields are unchanged.
Dirty road/biome edits that add/remove clusters or
change terrain height/vertex colour fall back to full upload.
CPU model preparation still executes on the worker; this
stage optimizes GPU upload volume and resident model lifetime.

**Actual verification:** GitHub merge ancestry/source reads
and successful source commits only. **NO CMake build, CTest,
native app run, GPU screenshot or benchmark performed**; user
assigned all testing to another agent. PR remains Draft.
The full-update fallback cannot be described as GPU-patched.

**Next test agent:** validate `planHorizonAtlasUpload` on
identical/changed/removed tiles, wrong geometry, density,
two-view layouts and zero changed bytes. Verify actual
`Device::updateTexture` row pitch/mipmap behaviour on
Metal/GL/Vulkan (especially 6 partial updates per frame),
GPU timing and correct appearance after landscape revisions.
Check exception/partial texture mutation behaviour and
fallback on changed terrain vertex/topology.

**Next feature stage:** optional GPU vertex partial update for
terrain height/colour changes, only if generic Model/GPU buffer
semantics allow safe use without introducing a new renderer.
This is NOT implemented; do not claim it is.

## Stage 12a — dedicated geometry ownership for mutable Horizon (COMMITTED; NOT TESTED)

**Branch:** `feature/terraweave`; PR #24 Draft. Latest main had previously been merged as `7f1cedde055a58e0b12a224a6e2f9b1dfd89b8d0`; refetch before any next edit because multiple agents work on this repository.

**Commits / source:**
- `a332bd8e7da8d4a4646236fdcbf1144be9f0c3c4` — `engine/render/Model.h`: adds `Model::batching.allowGeometryPool` (default `true`) as a generic native-model opt-out.
- `7baba8a08efa39455fe32b70e4369e8ae8d98f33` — `engine/render/GlobalGeometryPool.cpp`: `canPool` honors the opt-out before sharing a GPU arena page.
- `017b4652a8c6055a838a5762f4cac893bd29771c` — `engine/render/TerraWeaveHorizonModel.h`: sets `allowGeometryPool=false` on Horizon models that may need partial VB writes.

**Reason:** `uploadModel` may otherwise place any rigid, static Horizon vertex/index arrays in `GlobalGeometryPool` and set `gpuBaseVertex`/`gpuFirstIndex`, meaning a naive model-local `gfx::Device::updateBuffer` could overwrite OTHER models' geometry. Explicit opt-out retains existing per-model dedicated GPU VB/IB through the standard RHI. All non-Horizon models keep the default pooling policy; no graphics API or genre-specific engine core added.

**Actual verification:** GitHub commit/write success and static source inspection ONLY. User explicitly assigned all CMake, CTest, CI, sanitizer and GPU testing to another agent. No test results inferred.

**Next action:** implement a pure CPU vertex-patch planner which validates identical index topology, retained bounds, unchanged crossed-card vertices and a small modified terrain vertex fraction. Integrate a render-thread RHI `updateBuffer` path with a guaranteed full-upload fallback and document it as a distinct handoff stage.

## Stage 12b — topology-safe partial Horizon GPU terrain vertex updates (COMMITTED; NOT TESTED)

**Branch:** `feature/terraweave`, PR #24 Draft. The preceding Stage 12a added `Model::batching.allowGeometryPool=false` for mutable Horizon models; do NOT remove that safety invariant.

**Committed source:**
- `6035654a11afb0606eab1ff0498e5b45ac959e7e` — `engine/render/TerraWeaveHorizonGpuPatch.h`: splits `horizonCanReuseHorizonLayout(old,new)` from the original strict `horizonCanReuseModelGeometry` so an index/UV/cluster-compatible layout can accept changed terrain heights and vertex colours. Existing `planHorizonAtlasUpload` remains strict by default; a new final `allowTerrainVertexChanges=false` optional parameter allows combined CPU/GPU patch planning.
- `086dec4673aca360189df85a4ac1cfee9bc78086` — new `engine/render/TerraWeaveHorizonVertexPatch.h` providing `planHorizonVertexUpload`, `horizonCanApplyVertexUpload`, `applyHorizonVertexUpload`. Exact CPU comparisons use an independent packed-vertex/index snapshot. Planner rejects modified tree crossed-card vertices, changed indices, node/primitive topology, changed atlas slot geometry, pooled geometry, invalid bounds or updates above 20 disjoint ranges / 35% of terrain vertices. All current renderer bounds and per-primitive bounds must conservatively CONTAIN the new bounding boxes; otherwise fall back to full model swap rather than risk incorrect frustum culling.
- `1dc62908a47de28d1151f00cbb5fb00144c83876` — `engine/render/TerraWeaveHorizonScene.h` integrates combined atlas + terrain vertex GPU patch preflight and standard RHI `gfx::Device::updateBuffer` submissions on the owning render thread. After a successful combined patch, replaces the CPU vertex snapshot and `activeBake_` without creating/releasing another GPU model. On full upload, captures prepared CPU vertices/indices before `uploadModel` discards them; these snapshots are retained only when `maxCachedRegions>0`, preserving minimal/mobile low-memory choices. Empty Horizon and shutdown clear CPU snapshots. Adds runtime stats `vertexPatchCount`, `vertexPatchBytes`, `vertexPatchRanges`; atlas patches and full uploads still have their existing counters.

**Compatibility and invariants:**
- The original `Model`/RHI and genre-independent `WorldScene` APIs remain. Horizon models own dedicated VB/IB and cannot accidentally overwrite shared `GlobalGeometryPool` memory.
- Only the terrain vertex PREFIX changes. The distant forest crossed-card vertices, atlas/UV geometry, model index layout, primitive metadata and conservative bounds must remain compatible. An entire terrain grid is deliberately NOT uploaded if only a handful of vertices change and patch plan fits budget. New atlas pixels may be patched in the same publication.
- `gfx::Device::updateBuffer` is a void method in GL/Vulkan/Metal. It is not a transactional success-reporting API; device-loss/backend failures cannot be fully detected by this feature. The plan preflights all ranges, limits work, and falls back to a full upload BEFORE writes if the layout is unsafe, but does not guarantee rollback on GPU device loss. Real device validation is required.
- Even when the geometry buffer patch succeeds, some far-terrain edits may move silhouette/card heights or culling bounds; these deliberately take the full model path. The optimization is strict correctness-first, not a blanket dynamic mesh update.

**Actually performed:** Github connector source reads, verified RHI/global pool code inspection and successful source commits. **NO CMake, CTest, CI execution, GPU screenshots or performance measurements**; user has assigned all tests and defect triage to another agent. Do not mark any tests as passing. PR remains Draft.

**Tester agent exact next checks:** compile on GCC/Clang/AppleClang; verify `Model::Batching.allowGeometryPool` does not change existing model API/serialized data; validate dedicated Horizon VB/IB allocations even with global geometry pool on; test same-layout new height/normal/colour, atlas-only, combined atlas+terrain, changed triangle topology, changed cluster bounds, disabled CPU cache and negative coordinates; instrument GL/Metal/Vulkan `Device::updateBuffer` to verify size, offsets and frame synchronization. Record real results in this file. Check no renderer-cache or culling regressions when updating a static Model in place.

**Next functional work (not implemented):** efficient persistent atlas/mask paging across multiple regions and/or optional GPU-resident HLOD page cache, if justified by observed upload cost. Avoid unbounded GPU memory. Keep build/test responsibility with separate agent.

### Stage 12b follow-up — forest-only Horizon compatibility (COMMITTED; NOT TESTED)
- `3913e0cd31440fd3ca3261bc9c9e23cdd7dbc8f4`:
  `engine/render/TerraWeaveHorizonVertexPatch.h`: removed the
  premature rejection of a Horizon bake with **zero** far-terrain
  vertices. An opt-in `distantTerrain=false` scene must still be
  able to use the existing atlas-only GPU patch fast path; the
  planner now validates its unchanged crossed-card vertices and
  returns an empty terrain-vertex patch plan rather than forcing
  a needless full model upload.
- **Actual verification:** code commit and static review only.
  Separate testing agent must cover atlas-only scenes, mixed
  terrain/atlas patch, and no-change frames. No tests performed.

## Stage 13a — bounded Horizon GPU cache configuration (COMMITTED; NOT TESTED)

**Branch:** `feature/terraweave`, PR #24 Draft. Work is functions-first; the separate agent owns all builds/CTest/GPU benchmarking.

**Commits and files:**
- `1bc171e4b9790e553c01daa23bd2065315f7f1b2`: `engine/world/TerraWeaveHorizon.h` adds `maxCachedGpuRegions` (default 1, 0–3) and `gpuCacheBudgetMiB` (default 32, 8–96), including quality preset overrides. Minimal/Mobile disable additional GPU retention. High requests two cached regions but remains subject to a hard GPU-byte allowance.
- `9516e84ce5aa0c12de2066bd6c6746e8d1bca68b`: `engine/world/TerraWeaveHorizonManifest.h` accepts the same per-project JSON overrides.

**Budget contract:** the allowance applies only to **inactive cached GPU models** and not the single active GPU model or the already-bounded asynchronous upload/staging pipeline. Models are not copied; the cache will own their GPU resources by transferring the existing `std::unique_ptr<render::Model>`. These settings do not alter the deterministic bake fingerprint or physics/ECS behavior.

**Verification:** GitHub source commits only; tests NOT RUN by explicit division of work.

**Next:** implement the actual GPU-region LRU on the render owner thread, keep worker staging drained for cache hits, reject stale source revisions, release via `render::releaseModel(device,...)` on eviction/shutdown. Then append the next handoff stage.

## Stage 13b — bounded GPU-resident Horizon model LRU (CODE COMMITTED, TESTS NOT RUN)

**Branch:** `feature/terraweave` (Draft PR #24). Changes operate only when `terraweaveHorizon` is opted in; core C++ / render RHI remains genre independent. Other agents may have committed concurrently; fetch current GitHub HEAD/each file SHA before editing.

**Commit history / files:**
- `ca7a7ebe3b739f76831819725d71d5726af74037` — new `engine/render/TerraWeaveHorizonGpuCache.h`. Render-thread-only `HorizonGpuRegionCache` owns **already uploaded** `render::Model` instances with optional prior CPU bake/vertex/index snapshots. Cache keys are `world::Cell` and authored surface revision; `take` removes the desired region without new upload, `retain` stores previous GPU model with byte+count limits, LRU eviction and explicit `render::releaseModel(device,...)`, `prune` discards obsolete source revisions, and `clear` releases GPU assets before device shutdown. GPU estimate budgets texture level 0 + approximate mip chain + VB/IB and fixed overhead; not a driver-measured allocation figure.
- `580f26413ad4c1fd630fc4c4d4f8ff9e4d4ac7b3` — `engine/render/TerraWeaveHorizonScene.h`: adds owner-thread region handoff helpers `retireActive`, `adoptCached`, and `drainCanceledJobs`, `activeCell_` and `activeRevision_`, and `gpuCache_`. Prepared worker results record immutable source revision. `HorizonRuntimeStats` gains `gpuCachedRegions`, `gpuCachedBytes`, `gpuCacheHits`.
- `0e2cba94d839c604633b7879d4516ee6bc2d1947` — same scene: `setSurfaceSources` cancels pending stale async work regardless of whether authored edits touch the active displayed region. If edits are proven outside the active region, current model is allowed to continue unchanged at the new source revision. `update(device,camera)` enforces region key hysteresis, prunes stale GPU entries, instantly reuses an active or cached region, cancels previous stream request, and **drains** canceled job results so staging bytes never remain reserved indefinitely after a cache hit. Publication explicitly rejects prepared results whose source revision is no longer current.
- `eae64dea29e37f1ebee7b3975483e24c9728780c` — same scene: on successful publication of a new GPU model, retain the old model only if revision/memory/count budget permit; otherwise release on render thread. On successful in-place atlas/VB patches, update active key/revision. For empty regions, retire old active to the cache (if allowed), publish empty active residency. On `shutdown(device)`, release active and all inactive cached GPU models and CPU snapshots.

**Functional behavior:** On A→B transition, last successfully uploaded A remains visible while B is prepared/uploaded. Once B is published, A can enter the GPU LRU without copying GPU buffers. On B→A, A's stored model/atlas/vertex+index references are moved directly into active display; no CPU atlas bake or GPU upload is requested. Repeated cache activations keep the asynchronous staging queue draining on the owner thread, and a source/terrain revision prevents stale GPU reuse. Budget is **extra cached GPU residency only** and does not include the single active region or bounded asynchronous upload staging. Minimal/Mobile disable extra cached models. Default Balanced keeps up to 1 extra with 32MiB cap; High requests 2, still subject to 32MiB byte limit. Same generic `gfx::Device` and `render::Model` paths, no GL/Metal/Vulkan-specific code, no ECS mutations.

**Important limitations:** RHI resource sizes are estimated with 4/3 atlas mip overhead; drivers may allocate differently. No GPU crossfade is added: switching cached regions is instantaneous and may still have a visible change. GPU-resident models must be explicitly released through `WorldScene::shutdown(device)`; skipping the owning device's shutdown leaks them just as it leaks any other uploaded model. Cached-region GPU models may temporarily reference stale GPU assets until the next `update(device)` prune after `setSurfaceSources()`; shutdown also clears them. `updateBuffer` and `updateTexture` remain void backend operations; GPU device-loss recovery remains external.

**Verification actually performed:** inspected `AsyncChunkStream`, `render::Model`, `gfx::Device` source and committed code through GitHub connector. **NO CMake, CTest, sanitizer, real editor/world launch, GPU capture or performance benchmark run** — explicitly owned by separate testing agent. Keep Draft PR; no claim of correct runtime execution.

**Testing agent acceptance tasks:** build all relevant AIVERSE targets; exercise A→B→A GPU cache hit and ensure no extra `uploadModel`, verify cached GPU IDs preserved, A→B→C over-budget LRU eviction, and high-quality two-view cache byte accounting; test async late results after return to A, removed catalog cells and staging capacity (including WebGL no-thread mode); test source-revision invalidation while B is pending, edits inside vs outside visible region, no-cache mobile mode, model teardown/shutdown/device swap, memory and GPU framing on Metal/GL/Vulkan. Record pass/fail logs, not assumptions.

**Next implementation-only work:** Add optional GPU residency visualization/diagnostics in existing editor scene UI, if useful, and consider seamless camera-region proxy transitions or atlas paging only with real GPU measurements. No tests added/executed in this stage.


### Stage 13b follow-up — cache diagnostics and move-only ownership (SOURCE COMMITTED, TESTS NOT RUN)

- `bdd9edbe411d7c6ba2040177ce96efc729fa1c43`:
  `TerraWeaveHorizonGpuCache.h` returns move-only
  `HorizonGpuRegion` via explicit optional move and records
  GPU cache evictions caused by LRU budgets or stale source
  revisions. Cache hits transfer model ownership rather than
  incrementing ref counts on RHI handles.
- `daf20ef6059679b266c60a17d7975cbfd2bdbb3e`:
  `TerraWeaveHorizonScene.h` exposes
  `HorizonRuntimeStats::gpuCacheEvictions`.

**Actual validation:** GitHub source review and committed changes
only. **No CMake/CTest/platform GPU run**, by the user's
explicit separate-test-agent policy. Add eviction/revision and
GPU memory accounting tests to the acceptance list above.

## Stage 14a — neighboring Horizon GPU-region transition crossfade (CODE COMMITTED; TESTS NOT RUN)

**Scope / branch:** `feature/terraweave`, Draft PR #24; user delegated CMake/CTest, screenshots and benchmarks to a separate testing agent. Keep both the feature and its transitions generic and independent of RTS/FPS gameplay. Re-fetch current HEAD and per-file SHAs before editing because multiple agents work concurrently.

**Commits/files:**
- `c7151ab765e57e95df26eaac7886afc023ce07c8`:
  `engine/world/TerraWeaveHorizon.h`: adds
  `HorizonSettings::regionTransitionMs` (0–1000ms, default 240).
  Minimal and Mobile presets explicitly disable transitioning
  to avoid two models / alpha overdraw on constrained hardware.
  `validateHorizonSettings` bounds the new option.
- `27f718a76e539ae36d703b02d2bb4f76ff178806`:
  `engine/world/TerraWeaveHorizonManifest.h`: adds optional
  `terraweaveHorizon.regionTransitionMs` JSON key.
- `fb069ee868cb5b5902e88729813a203fd058195d`:
  `engine/render/TerraWeaveHorizonGpuCache.h`:
  `reserveTransient(device, estimatedGpuBytes,
  maxInactiveGpuRegions, gpuBudget)` retires old inactive LRU
  entries if necessary to accommodate ONE fade-out model inside
  the existing **extra GPU residency budget**. It does not
  allocate any new GPU buffers or alter the active model.
- `51befc1077fea0c051ca9b5a6fed716033e3918a`:
  `engine/render/TerraWeaveHorizonScene.h` stores at most
  one outgoing `HorizonGpuRegion`, with old source revision,
  cell and monotonic start time. Neighbor-only transitions
  require one-cell distance in X/Z, matching authored
  source generation, nonzero transition duration and the
  ability to reserve the extra GPU budget. A previous transition
  settles into the normal LRU (or releases stale GPU assets)
  before starting another. A return to the outgoing cell can
  settle/retrieve its cached GPU model without rebaking.
  `finishTransition(device)` runs on the render owner thread;
  shutdown forces resource release. Stats:
  `transitionStarts`, `transitioning`,
  `transitionProgressPermille`.
- `bde25305027deb0198cfd5daf2a1cf1f3c57ddfb`:
  `TerraWeaveHorizonScene::submit` now draws outgoing
  and incoming proxy with per-model tint alpha and the
  existing `kDrawTransparent|kDrawNoCastShadow|
  kDrawNoReceiveShadow` flags while the time-based
  smoothstep transition is active, then returns to the
  ordinary single opaque/static Horizon model. No new
  shader or backend implementation.

**Behavior:** A→B remains single model while B is baking;
when B is ready and GPU-resident, A fades out while B
fades in. Maximum overlap duration 1000ms, default 240ms.
The extra GPU fade model is accounted against the same
inactive residency count/byte budget as the regular GPU
LRU, rather than being a free third tier. Fast teleports
and source-revision changes intentionally skip the
transition to prevent misleading stale landscape visuals.
The transition is presentation-only: it neither changes
tree ECS IDs nor collision/navmesh/authoritative simulation.

**Important limitations:** This uses standard transparent
alpha blending and two model submissions for a short time.
On overlapping triangulated far terrain and alpha-masked
cluster cards, conventional sorting/depth policies can
still produce transparency artifacts. Avoid claiming true
order-independent transparency or perfect temporal
alpha-coverage. The outgoing model remains resident
until `finishTransition(device)` can retire it. GPU
driver cost/latency and visual quality are unmeasured.
`gfx::Device` still owns device-loss recovery.
Minimal/Mobile default transition off.

**Actually verified:** commits written via GitHub connector,
source contract inspected. **NOT RUN:** compiler, full
AIVERSE build, CTest, shader capture, renderer screenshots,
memory/FPS measurements, sanitizer or native runtime.
Dedicated testing agent owns these and must report real
results here before the Draft PR is merged.

**Tester acceptance:** move camera by one cell on
high/balanced and capture alpha overdraw; A→B→A
during transition; jump several region cells
(should hard-switch); disable transition; no GPU budget;
device shutdown halfway through; road/terrain source
revision while fading (old proxy must not render again);
vary frame pacing; validate resource counts/opacity on
Metal/Vulkan/GL and mobile default.

## Stage 15 — split Horizon terrain depth from forest-only crossfade (CODE COMMITTED; TESTS DEFERRED)

**Branch:** `feature/terraweave`; Draft PR #24. Follow current branch HEAD: other agents may edit adjacent modules. The user explicitly assigned build/CTest, GPU captures and performance testing to a different agent.

**Source changes (2026-11-18):**
- `af57039985d61ca27c355c96c5fecfe7d69a0c76`:
  `engine/render/Renderer3D.h` adds generic
  `ModelDraw::primitiveMaterialFilter` (`-1` means
  all source model primitives, non-negative value filters by
  original `Primitive::material` index).
- `84cf3bb8495e801869e5f3b1aea0656187a69fe8`:
  `engine/render/Renderer3DModels.cpp` honors the filter
  before preparing each `ModelItem`. Only filtered draw
  calls bypass existing aggregated shadow batches/material
  batches and the static cache; **unfiltered drawModel**
  continues through all pre-existing optimization paths.
  Filtered draws are not passed to whole-model light-bake
  capture as if all their mesh primitives had rendered.
- `a005ac31bc3eaad2c260c5c548e293e4b1068fef`:
  `engine/render/TerraWeaveHorizonScene.h`: the crossfade
  draws **incoming terrain material slot 0 exactly once,
  opaque**, while only source material slot 1 (forest cluster
  crossed cards) is alpha-blended from outgoing to incoming
  GPU model. The two models still share their own already
  uploaded GPU vertex/index/atlas resources (no copies or
  new shaders). Split transient draws use `persistentId=0`;
  the normal full single-opaque draw continues to use its
  stable scene ID.
- `775761b22564bc1663f6d857fe6512488e623393`:
  `HorizonScene` directly restores the old transition-slot
  GPU model on **A→B→A while the fade is ongoing**, before
  LRU insertion could evict it at `maxCachedGpuRegions=1`.
  Large-teleport stale-proxy visibility is bounded by the
  far-region footprint even when CPU bake caching is off.
  Pending worker requests are still cancelled/drained and
  RHI ownership remains single.
- `9cb2c048fe0a295cb5f0a0e18d1d342eb5348ea0`:
  `Renderer3D.h` moves the new material filter to the
  END of `ModelDraw`, so existing positional aggregate
  initializers in native projects don't have their field
  positions shifted.

**Presentation contract:** while blending the model,
`drawRegion(incoming, alpha=1, materialFilter=0)` is
opaque and depth-writing, then the departing/arriving
`materialFilter=1` canopy portions fade in the standard
transparent pass. This removes the previous **two overlapping
semi-transparent distant terrain grids** and significantly
reduces Z-fighting risk. In the normal steady state,
`materialFilter=-1` keeps one standard combined model call.
The source-material-index filter is a general Renderer3D
feature, NOT a TerraWeave-specific renderer fork.

**Limitations:** the incoming far-terrain ring replaces the
old terrain ring immediately; changing terrain outer
silhouettes may still show a sudden edge and require real
geomorph/skirt handling in the future. The two canopy
alpha-blended layers can still exhibit sorting/overdraw
artifacts and are not order-independent. A teleport outside
the old region's safe footprint now suppresses stale
Horizon submission until a new region is available; this
may produce a brief horizon gap by design rather than a
completely wrong outdated skyline. Normal opaque
`Renderer3D`/shadow and native game draw paths should not
change when `primitiveMaterialFilter=-1`.

**Actual verification:** GitHub source reads and successful
commits only. **NOT RUN**: CMake, CTest, CI workflow,
shader, GPU screenshots, platform performance, runtime
regressions. Separate testing agent must validate all of
these before PR #24 is made ready for review.

**Test-agent acceptance:** check single visible terrain
material during transition, alpha-mask forest-only
coverage and draw sorting at several camera angles;
non-Horizon material filtering (including variants,
shadow/material batch bypass, unfiltered MDI and static
light capture), native positional `ModelDraw` initializers,
A→B→A while fade is active, several-km teleport while a
worker is outstanding, cache-disabled Mobile/Minimal,
texture/vertex range patches after fade, LRU accounting,
WorldScene shutdown and GPU resource lifetime. Compare
Metal/GL/Vulkan footage and frametime/draw counts.

**Next implementation-only stage:** improve the far-terrain
outer-edge behavior via a skirt/terrain LOD region boundary
policy or use screen-space temporal forest coverage if real
visual testing proves existing transparent depth sorting
insufficient. Do not duplicate the generic renderer.
