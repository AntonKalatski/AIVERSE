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
