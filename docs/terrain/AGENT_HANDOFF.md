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
