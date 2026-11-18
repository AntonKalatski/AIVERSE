# TerraWeave — procedural terrain, biomes and vegetation (AIVERSE)

Status (2026-11-18): **Core + Arbor/Flora + hierarchical BranchLOD and far-tree billboard adapters implemented**; runtime scene/editor integration and GPU verification pending.
Implementation: `engine/world/TerraWeave.h`. Test: `tests/terraweave_test.cpp`.
Handoff: `docs/terrain/AGENT_HANDOFF.md`.

## Relationship to existing terrain

AIVERSE already has chunked RTS heightfield rendering, editor sculpt/paint operations,
`MapGrid` heightfield simulation, `modules/rts/gen/GenNoise.*` and water/roads.
**Do not duplicate or replace these**. TerraWeave supplies genre-independent,
pure CPU world-generation outputs. Genre adapters consume them without changing
legacy project, save, replay or networking contracts.

The existing RTS terrain uses map corners in 1/16 map units; TerraWeave uses
**integer world centimetres**. The adapter must explicitly convert units and
coordinate handedness/axis orientation, then update dirty chunks through the
existing surface API. Never silently cast centimetres into old map units.

## Phase 1 API

Namespace: `aiverse::terraweave`.

- `heightAtCm(TerrainSettings, worldXcm, worldZcm) -> int32_t`: signed terrain elevation in cm.
- `climateAt(TerrainSettings, BiomeSettings, xCm, zCm) -> ClimateSample`: temperature,
  humidity in [0,1000], and one categorical Biome.
- `generateChunk(TerrainSettings, BiomeSettings, ChunkKey, resolution, spacingCm)`
  returns `TerrainChunk` with (resolution+1)^2 signed heights and biome ids.
  Coordinates are absolute; adjacent chunk borders are identical.
- `scatter(TerrainSettings, BiomeSettings, ScatterSettings, RectCm worldBounds,
  vector<FoliageSpecies>, vector<RectCm> exclusions)` returns stable sorted
  row-major foliage instances. Prototype ids are uint32 and unique within
  the invocation. Candidate selection is anchored to a **global** lattice;
  chunk partitioning and prototype order cannot change the result.
- `slopePermilleAt` estimates local max-axis slope from signed height differences.
- `TerrainChunkComponent`, `VegetationPatchComponent` are small ECS-ready descriptors;
  these are not yet registered into an ECS scheduler.
- `FoliageInstance::key` is the stable hash of global lattice coordinates and
  scatter seed. Persist removals/harvests by key and world-generation revision,
  not by GPU buffer index.

Sample (C++20; no renderer or RTS dependency):

```cpp
#include "engine/world/TerraWeave.h"
using namespace aiverse::terraweave;

TerrainSettings terrain;
BiomeSettings climate;
auto tile = generateChunk(terrain, climate, {0, 0}, 32, 100);
FoliageSpecies oak;
oak.id = 1001;
oak.biomeMask = biomeBit(Biome::Forest) | biomeBit(Biome::Grassland);
auto trees = scatter(terrain, climate, {}, {0, 0, 3200, 3200},
                     {oak}, {{1000, 1000, 1600, 1600}}); // building exclusion
```

## Determinism and threading

All phase 1 outputs are integer-only, seeded and independent of call order.
No `rand()`, platform time, global mutable state, or `float`. Helpers use
stable 64-bit mixing and signed floor-division for negative world coordinates.
Pure generators can be called concurrently. The host job system schedules them;
the ECS main-thread/guarded commit order must remain deterministic. CPU outputs
are authoritative; GPU placement or rendering must not affect gameplay RNG or
collision. Changes to generator parameters, coordinate transforms or hashing
are **world-format version changes**, requiring migration or explicit revision.

Use one ECS entity per **terrain/vegetation patch** or interactive tree,
not per grass blade. Render-only millions of blades belong in instance buffers.
Decorative foliage may not have gameplay state. Harvestable foliage must have
a stable removal record, replicated and hashed by the game's simulation owner.

## Algorithm / limitations

- Height: 2D multi-octave signed value noise (Q16) in integer centimetres.
  No erosion, caves, stamps or mesh triangulation yet.
- Biomes: deterministic temperature/moisture samples with simple threshold
  classification, including Ocean/Tundra/Desert/Grassland/Forest/Wetland/Alpine.
  Not yet physical climate simulation or biome blend weights.
- Scatter: jittered one-candidate-per-world-lattice-cell placement; deterministic
  density/weighted species selection, biomes, height, slope, world-space rectangular
  exclusions and scale/yaw variance. It is *not* exact Bridson Poisson-disk,
  nor does it currently guarantee separation of full tree crowns.
- Limits: nonempty region up to 1,000,000 lattice cells per scatter call,
  max 1024 terrain quads per tile. Do not feed untrusted arbitrary 64-bit
  coordinates; future validation should enforce a supported world extent.
- No automatic spawn into ECS, GPU buffers, RHI draws, editor UI, road/water
  subscriptions, tree meshing, streaming residency, erosion, virtual textures,
  physics collisions, terrain navmesh updates or season/wind simulation yet.
  These are **future phases**, not features of the phase 1 header.

## Integrating a scene

1. A world config selects TerraWeave seed/settings and an explicit generator version.
2. Visible-world streaming chooses chunk keys; workers call pure `generateChunk`.
3. An adapter maps centimetres to the existing heightfield `MapGrid`/editor
   document or to a generic terrain mesh; changes go through the existing
   dirty-chunk pipeline, *never* a second competing terrain renderer.
4. Query roads, buildings, waterways and designer overrides as exclusion/stamp
   sources before scattering.
5. `scatter` returns instance descriptors for prototype render groups; only
   interactable objects become separate ECS entities.
6. All rendering uploads and GPU work go through `gfx::Device`, using GLSL+MSL
   parity and quality tiers; no direct graphics backend calls in generation.

## Tests

```sh
cmake -S . -B build/terraweave-test -DAIVERSE_BUILD_TESTS=ON
cmake --build build/terraweave-test --target aiverse_terraweave_test -j
ctest --test-dir build/terraweave-test --output-on-failure -R '^terraweave$'
```

The test covers adjacent tile edges, negative tile coordinates, stable seed,
partition-independent foliage, prototype input reordering, world exclusions,
invalid configs and worker-thread purity.

When terrain is mapped into the sim, also run the existing full determinism,
headless, replay and editor view suites per `AGENTS.md`.

## Future subsystem names

- **TerraWeave Terrain**: existing heightfield adapters, geomorph LOD, stamps,
  erosion, SDF/voxel overlays.
- **TerraWeave Biome**: climate layers, transition masks, species ecologies.
- **TerraWeave Arbor**: procedural trees / parameterized archetypes, branch/crown
  meshing, LOD and impostors.
- **TerraWeave Flora**: species asset registry, batches, GPU placement/culling.
- **TerraWeave Eco**: growth, wind, seasons, harvesting and gameplay systems.
- **TerraWeave Stream**: bounded streaming residency, chunk jobs and persistence.

See the handoff file for exact priorities and integration hazards.

## Stage 2a: Arbor procedural tree geometry

Added `engine/world/TerraWeaveArbor.h` (integer-authoritative tree skeleton)
and `engine/world/TerraWeaveArborMesh.h` (render-only CPU tessellation).
This is independent of any genre, and a native C++ module can include it.

```cpp
#include "engine/world/TerraWeaveArborMesh.h"
using namespace aiverse::terraweave;

TreeProfile oak;
oak.speciesId = 1001;
oak.shape = CrownShape::Broadleaf;
oak.primaryBranches = 22;
const TreeSkeleton skeleton = generateTree(oak, instance.key, TreeLOD::High);
const ArborMesh renderGeometry = buildArborMesh(skeleton, {6, true});
// Upload through gfx::Device (not implemented in this stage).
```

`TreeProfile` defines tree height range, trunk radius, crown start, primary
branches, branch reach/rise, leaf-cluster radius and broadleaf/conifer/columnar
shape. Tree seed is derived from stable foliage instance key + species ID; the
same inputs yield the same integer local-space structure regardless of worker
thread or scene partition. `TreeLOD` High/Medium/Low reduce branch counts
and leaf clusters. Impostor returns bounds only; actual billboard atlas baking
is **not implemented**.

`TreeSkeleton` contains a root trunk, child branch segments with parent
indices, leaf clusters and conservative height/crown radius bounds.
`buildArborMesh` converts branches to cylindrical bark tubes and clusters to
crossed alpha-card quads with UVs, normals and a bark/leaf material discriminator.
The returned mesh uses float only for **presentation**, not gameplay state.
Meshes have no tangents or GPU material binding yet. The first visual pass is
simple card foliage, not photorealistic SpeedTree-equivalent geometry.

The mesh is local-space in **centimetres**. Instance translation, rotation and
scale are supplied by `FoliageInstance` externally. If gameplay needs tree
collision/hitboxes, derive deterministic proxies from `TreeSkeleton`, not from
float render vertices. View LOD selection and GPU uploads must live in a
renderer adapter and not in lockstep ECS simulation.

Test source: `tests/terraweave_arbor_test.cpp`. CTest name:
`terraweave_arbor`. Check seeded determinism, all crown archetypes, branch
parent validity, LOD topology, independent parallel evaluation, render geometry
index validity, and invalid profiles. Compilation/CTest results are tracked
in `docs/terrain/AGENT_HANDOFF.md`.


## Stage 2b: Flora CPU visibility and instancing prep

`engine/world/TerraWeaveFlora.h` contains a presentation-only draw-list
organizer that does **not** add one ECS entity per grass blade or tree.

```cpp
#include "engine/world/TerraWeaveFlora.h"
using namespace aiverse::terraweave;
FloraView view;
view.cameraXcm = cameraXcm;
view.cameraZcm = cameraZcm;
auto batches = buildFloraBatches(placements, view);
for (const FloraBatch& batch : batches) {
    // Resolve species prototype + TreeLOD, pack instance transforms.
    // Upload/submit via gfx::Device in a future renderer adapter.
}
```

`FloraView` selects radial ranges for High/Medium/Low/Impostor mesh tiers,
a hard culling radius, and stable seeded far-density thinning.
`FloraBatch` groups copies of `FoliageInstance` by `(speciesId, lod)`.
Ordering is canonical regardless of worker/chunk completion order, and
overlapping identical placements are deduplicated within the resulting group.
This is draw-list preparation **only**, not GPU instancing/culling/indirect
drawing, frustum/Hi-Z occlusion or crossfade. Screen-space LOD,
camera-frustum culling and distance-field shadows remain future stages.

Never feed visibility decisions back into authoritative ECS state or
network simulation; gameplay placement continues to use `scatter`.
Test source: `tests/terraweave_flora_test.cpp`; CTest `terraweave_flora`.

## World extent validation (Stage 2c)

Core generator explicitly supports world positions
`[-1,000,000,000, +1,000,000,000]` **centimetres** on X/Z.
`heightAtCm`, `generateChunk`, `slopePermilleAt` and `scatter`
validate inputs before coordinate multiplication/offset sampling. Chunk
keys that would leave this world box are rejected, as are scatter bounds
that fall within the 100-cm boundary margin required by slope sampling.
This is intended to fail cleanly on malformed/extreme world coordinates,
not to impose a practical memory-residency or render-distance budget.

Do not remove these guards when introducing a streamed world. Large-world
support additionally needs camera-relative rendering, origin rebasing and
bounded chunk residency, which are not yet implemented.

## Stage 2d: standard AIVERSE render::Model adapter

`engine/render/TerraWeaveModelAdapter.h` is a thin render-only
adapter into the pre-existing `render::Model` and its
`prepareModelUpload` / `uploadModel(gfx::Device&, Model&)` path:

```cpp
#include "engine/render/TerraWeaveModelAdapter.h"
auto tree = aiverse::terraweave::makeArborModel(profile, instance.key);
// On the render-owning thread:
aiverse::render::prepareModelUpload(*tree);
aiverse::render::uploadModel(device, *tree);
```

The adapter builds explicit High/Medium/Low mesh variants in a single
`render::Model`, with mesh-0 LOD switches at 128 and 32 projected pixels.
It uses standard `render::ModelVertex`, `Primitive`, `Material`,
`Node` and `Model::finalize()`; positions convert from integer cm to
floating-point metres. Existing model renderer infrastructure owns the
shaders, LOD fade, frustum culling, backend upload and instanced draws.

The default bark and leaf materials have simple untextured PBR colours.
Leaf cards are **opaque rectangles** until a caller binds a real
alpha-cutout foliage texture and sets `AlphaMode::Mask`; do not
misrepresent this as production-quality foliage. Billboard atlas
generation, material/texture registry, wind deformation and actual
scene draw submission remain unfinished.

`tests/terraweave_model_test.cpp` checks three LOD meshes,
range/index correctness, bounds, and stable topology. It links
`aiverse_models`; the target `aiverse_terraweave_model_test` and
CTest `terraweave_model` are registered in CMake.
All four TerraWeave test targets explicitly undefine `NDEBUG`
on GCC/Clang so their assertions execute in Release builds.

Important: using `makeArborModel` per individual tree is not the
final scalable forest strategy. Generate a bounded catalogue of
species/variant prototypes and reuse meshes across instance transforms.
`buildFloraBatches` already groups placement data; the next renderer
adapter must submit these groups through existing model instancing.

## Stage 2f: baking TerraWeave into existing editor MapDoc heightfields

`engine/world/TerraWeaveHeightmap.h` provides a pure
`generateLegacyCorners(TerrainSettings, widthCells, heightCells,
LegacyHeightfieldOptions)` adapter. Inputs use centimetres; output is the
existing MapData signed-int16 corner format, **1/16 map metre** per
sample. Coordinates and output ranges are validated; overflow/saturation
throws instead of corrupting map heights. Offset/spacing can be configured.

`apps/editor/core/TerraWeaveMapAdapter.h` provides:
```cpp
#include "apps/editor/core/TerraWeaveMapAdapter.h"
aiverse::editor::bakeTerraWeaveTerrain(
    mapDoc, terrainSettings, {0, 0, 100});
// For intentional regeneration replacing authored corner data:
aiverse::editor::bakeTerraWeaveTerrain(
    mapDoc, terrainSettings, {0, 0, 100},
    aiverse::editor::TerraWeaveExistingTerrain::ReplaceExplicitly);
```

The editor adapter writes through `MapDoc::beginEdit()` and
`MapDoc::touched()` after constructing all generated heights, preserving
undo and the existing view-revision notification. It never silently
overwrites manual sculpting, never changes `rules.terrain.model`, and
rejects planet maps (separate cube-sphere adapter needed). This is
**one-shot baking API**, not yet an editor button, regeneration graph,
streaming terrain or painted override stack.

Source test: `tests/terraweave_heightmap_test.cpp` (CTest
`terraweave_heightmap`) verifies signed heights, seam continuity,
legacy overflow rejection and invalid inputs. The inline `MapDoc` helper
is not yet exercised end-to-end in a running editor.


## Stage 2f — shared forest model submission adapter (CPU / RHI bridge)

`engine/render/TerraWeaveForestAdapter.h` exposes
`aiverse::terraweave::ArborForestAdapter`. It bridges deterministic
`FloraBatch` instances to the **existing** `render::Renderer3D::drawModel`
path. It does not own or replace Renderer3D, ECS, streaming or shaders.

- `addPrototype(TreeProfile, meshSeed, ArborMeshSettings)` builds **one**
  `render::Model` per species. Duplicate/zero species IDs are rejected.
  Changing the tree's mesh on every instance defeats batching; only transform,
  yaw and scale vary by `FoliageInstance`. All nodes use existing model
  projected-size LODs.
- `prototype(speciesId)` gives the stable shared model pointer, valid while
  the adapter lives. `uploadModels(gfx::Device&)` and
  `releaseModels(gfx::Device&)` MUST run on the GPU-owning render thread.
  Release GPU resources before destroying the device or the adapter.
- `forEachDraw(batches, ForestRenderOriginCm, callback)` is a GPU-free
  inspection/submission primitive, and `submit(Renderer3D&, batches, origin)`
  forwards directly to `Renderer3D::drawModel`. Call `submit` between
  `beginScene` and `render`, only after model upload.
- `ForestRenderOriginCm` is signed world **centimetres**, with fields
  `x,y,z`. For X/Z, subtract origin using int64 BEFORE float metre conversion,
  to preserve precision in large worlds. Camera and renderer must use the same
  rebased coordinate frame; model draw transforms are metres.
- `ModelDraw::persistentId` is a namespaced stable hash of
  `FoliageInstance::key` and species, independent of the draw index.
  Draw flags are `kDrawStatic`. An `Impostor` Flora batch now selects a separate camera-facing alpha-mask
  tree billboard model (one quad and one procedural RGBA silhouette), not the
  lowest geometry mesh. This is a single-view silhouette, not a multi-angle atlas.

Minimal native / host render integration sketch:

```cpp
#include "engine/render/TerraWeaveForestAdapter.h"
using namespace aiverse::terraweave;

ArborForestAdapter forest;
TreeProfile pine;
pine.speciesId = 21;
pine.shape = CrownShape::Conifer;
forest.addPrototype(pine, 0x5678);           // CPU / setup stage
forest.uploadModels(device);                 // render thread, check bool
auto batches = buildFloraBatches(placements, view);
renderer.beginScene(camera, width, height);
forest.submit(renderer, batches, {originXcm, originYcm, originZcm});
// ... other scene draws and renderer.render(pass)
forest.releaseModels(device);                // render thread at shutdown
```

The adapter **does not register with WorldScene or the editor** yet. It
does not provide per-tree geometry variation, guaranteed indirect draw
counts, actual impostors, material cutout textures, scene screenshots,
GPU validation, or an opt-in manifest format. Existing projects remain
untouched. Validation is added to `tests/terraweave_model_test.cpp`;
see `AGENT_HANDOFF.md` for observed test results and next task.


## Hierarchical BranchLOD — independent tree parts (Stage 2g)

The new `engine/world/TerraWeaveBranchLod.h` and
`makeArborBranchLodModel(profile, seed, BranchLodSettings)` in
`engine/render/TerraWeaveModelAdapter.h` build a reusable native
`render::Model` containing **one render node per tree branch**. Each node
retains `TreeBranch::parent` and has its own three `MeshDef` variants.
`Renderer3D` already chooses a node's LOD by its projected size using
its existing `selectModelLod` path, considering camera and shadow views.
This allows, e.g., a large near-side trunk to remain fully detailed while
individual smaller background branches switch to their cheapest shape.
There is no whole-tree mesh switch for these three geometric tiers.

Per-branch geometry levels:
- `BranchDetail::Full`: full radial bark mesh, local foliage clusters
  with crossed leaf cards.
- `BranchDetail::Reduced`: about half the bark radial segments, one
  distal local foliage cluster, crossed cards.
- `BranchDetail::Minimal`: three bark radial sides, one distal cluster
  represented by one card plane.
- `BranchLodSettings::reducedPixels` (default 96) and `minimalPixels`
  (default 30) set independent projected screen-diameter thresholds.
  The renderer's own LOD fade settings control transition blending.

`LeafCluster::parentBranch` is the original high-detail structural
branch index (new metadata). It is used by
`buildArborBranchMesh(tree, branchIndex, BranchDetail, ArborMeshSettings)`.
No branch is dropped, and tree skeleton generation/gameplay remains
deterministic; branch geometry and resolution choices are presentation
only. A single prototype is shared by all instances of a species; do
not create a distinct `render::Model` for each tree. Tree silhouette
geometry still uses the existing opaque leaf material if no project
alpha-cutout texture is assigned.

## Whole-tree final billboard (Stage 2h)

`engine/render/TerraWeaveArborBillboard.h` provides
`makeArborBillboardModel(TreeProfile, seed, ArborBillboardSettings)`.
It projects the procedural bark/leaf silhouette to an RGBA8 image
(default 128x256), creates a full-tree XY quad, and assigns the image
to a standard `render::Material` with `AlphaMode::Mask`.
Transparent pixels are genuine alpha holes, not a green rectangle;
no new GLSL, MSL or custom rendering backend is required.

`ArborForestAdapter` now caches two models per species: a hierarchical
near model via `prototype(speciesId)`, and a full-tree far quad via
`billboardPrototype(speciesId)`. `uploadModels(device)` and
`releaseModels(device)` manage both through the engine's existing
GPU model uploader. `forEachDraw` selects the billboard model only for
`FloraBatch::lod == TreeLOD::Impostor`, while near/medium/low batches
use the per-branch LOD chain. The far quad's rotation faces the
horizontal camera direction; the native `submit` path reads
`Renderer3D::cameraPos()`. For CPU draw-list tests / custom callers,
pass `ForestCameraCm` to the optional fourth argument of
`forEachDraw(batches, origin, callback, camera)`.
Billboard and near representations retain the same stable instance ID.

The billboard image is intentionally a **single-view, deterministic
silhouette**, not yet a high-quality baked 8/16-view albedo+normal/depth
impostor atlas. Near-to-far switching presently follows `FloraView`
distance buckets and has no whole-tree temporal crossfade; the
per-branch transitions are handled independently by Renderer3D.
The billboard currently disables cast shadows to avoid fake flat
shadows. Test screen-space pop, alpha halo/mipmaps and multi-angle
visual quality on actual GPUs before claiming production readiness.

**Tests added but not confirmed executed:**
`tests/terraweave_model_test.cpp` now verifies branch counts, parent
graph, three independent geometry tiers, index bounds, deterministic
mesh and silhouette image, material cutout, transparent/bark/leaf pixels,
model switching, camera orientation and stable identity. Isolated CI
contains this target. See `docs/terrain/AGENT_HANDOFF.md` for commands
and exact verification status.

## Stage 3a — editor and MCP integration (opt-in heightfield baking)

The legacy RTS `TerrainWindow` now has a **TerraWeave** section with
seed, average height, relief, noise period and octaves. It exposes two
actions using the same `Workspace::call` operations as agents:

- `terraweave_preview`: returns an integer-cm 9×9 height sample grid
  plus `minCm` and `maxCm`; does **not** edit the map.
- `terraweave_generate`: explicitly bakes a full heightfield into the
  existing `MapDoc::corners`, creates an undo snapshot, calls
  `MapDoc::touched()`, queues a `Change::Map` invalidation and writes
  sanitized generator metadata into `map.extra.terraweave` (version 1).

Both operations are registered in
`apps/editor/core/TerraWeaveOps.h` via `registerTerrainOps`
and are reachable through the existing editor/MCP registry. Example:

```json
{
  "seed": 42,
  "baseHeightCm": 0,
  "reliefCm": 5000,
  "basePeriodCm": 6400,
  "octaves": 5,
  "originXcm": 0,
  "originZcm": 0,
  "cellSizeCm": 100,
  "overwrite": false
}
```

**Safety/compatibility:** Without explicit `overwrite: true`, the
adapter refuses to replace either an existing corner heightfield or an
authored detail surface. With explicit replacement, the previous
detail layer (including splat/file/scale) is cleared *after* recording
the undo snapshot so it cannot mask the new corners in the renderer.
Planet and FPS maps are rejected by this legacy `MapDoc` adapter;
TerraWeave's generic C++ world generator and ForestAdapter still work
independently of genre. The operation does **not** silently change
`rules.terrain.model`; game authors choose whether corners influence
simulation. No project changes occur until the user calls Generate.

**Limitations:** `preview` is a 9×9 numeric sample, not a live 3D
preview. Generation is a one-shot bake, not procedural streaming,
a persistent non-destructive graph or a paintable biome editor.
The `terraweave` map block records generator inputs but is not yet
automatically re-evaluated on load.

**Testing:** `tests/terraweave_ops_test.cpp` checks param defaults,
bounds rejection, versioned metadata and deterministic preview.
The test does not construct a live `Workspace` or render screenshots;
an end-to-end editor/GUI/MCP regression remains mandatory.


## Stage 3b — multi-azimuth Arbor impostor atlas

`engine/render/TerraWeaveImpostorAtlas.h` adds
`ArborImpostorAtlasSettings`, `ArborImpostorAtlas`,
`makeArborImpostorAtlas(profile, seed, settings)` and
`selectArborImpostorAzimuth(instanceYawMilliDegrees, treeXcm, treeZcm,
cameraXcm, cameraZcm, views)`.

The CPU baker uses **one deterministic tree skeleton** to render 4, 8
(default) or 16 azimuth silhouettes into **one** RGBA8 horizontal atlas
(default 8 × 128 × 256). One `render::Model` and one atlas image are
reused across all trees of a species. Each of the `views`
`std::vector<render::Material>` overrides references the same
`baseTex` but has a different `uvOffset`/`uvScale` with a half-texel
inset. Renderer3D already handles those overrides via
`render::ModelDraw::materials`; no new shader or GPU API is added.

`ArborForestAdapter` now caches near BranchLOD geometry, a far atlas
model and per-view material overrides per species.
`billboardAzimuthCount(speciesId)` returns the atlas view count.
`forEachDraw(..., ForestCameraCm)` selects the nearest tree-local
azimuth (camera world heading minus each instance's own yaw), and
`submit(renderer, ...)` uses the real renderer camera position.
Storage for the material override vectors must outlive the frame
until `Renderer3D::render()` returns. The billboard is camera-facing
but depicts the corresponding view of the tree, avoiding a fixed
flat silhouette during orbit.

The older single-view `makeArborBillboardModel` API remains intact.
The new atlas is silhouette/albedo **only**: no depth, normal or
parallax impostor, no interpolation between adjacent views, and no
whole-tree temporal alpha transition. The source assertions have been
updated for later execution; **CMake/CTest and GPU verification have not
been run by request**.


## Stage 3c — stable spatial transition to impostor

`FloraView::impostorBlendWidthCm` (default **0**, preserving legacy
hard-ring selection) spreads the `TreeLOD::Low` →
`TreeLOD::Impostor` threshold over a bounded interval around
`FloraView::lowRangeCm`. Each `FoliageInstance` selects a stable
threshold based on its persistent key and species ID. The same tree
does not flicker when camera position changes within the band, and
all trees no longer swap on one circular boundary. The interval is
validated to stay between `mediumRangeCm` and `cullRangeCm`.

Example: `view.lowRangeCm = 18000;`
`view.impostorBlendWidthCm = 4000;` distributes transitions over
160–200 metres. Because thresholds are stable, this is **population
dithering, not a true per-object temporal or alpha crossfade**.
No additional draw calls or extra transparent meshes are required.
`tests/terraweave_flora_test.cpp` has deferred assertions; test
execution is intentionally postponed.


## Stage 3d — opt-in streaming forest host and WorldScene manifest

`engine/world/TerraWeaveForestScene.h` introduces the reusable,
genre-independent native C++ `TerraWeaveForestScene` and
`ForestSceneSettings`. The scene owns:
- per-species shared near BranchLOD and far impostor atlas prototypes
  through `ArborForestAdapter`;
- integer-centimetre, globally keyed foliage placements and stable IDs;
- an explicitly bounded `maxResidentChunks` patch cache;
- `streamAt(ForestCameraCm)`: generate nearest resident chunks and
  release patches outside the current budget/radius;
- `setExclusions(vector<RectCm>)`: regenerate for road/building/clearing
  rectangles, with cache revision invalidation;
- `visibleBatches()`: per-view Flora LOD/culling, including staggered
  impostor transition; `uploadModels(gfx::Device&)`,
  `submit(Renderer3D&, ForestRenderOriginCm)` and
  `releaseModels(gfx::Device&)` for the existing RHI lifecycle;
- `stats()`: number of resident chunks, placements, prototypes and
  generation revision (not actual GPU draw counters).

Native projects can instantiate it independently of an RTS/FPS
module. `addSpecies(TreeProfile, FoliageSpecies, prototypeSeed)`
must be called **before** streaming/upload. `streamAt` is currently
synchronous CPU generation, bounded by the cache limits and current
scatter settings. Do not invoke it concurrently with `submit`,
`setExclusions` or `releaseModels`. Worker-stage streaming,
cancellation and generation revision commit validation are future work.

`engine/world/TerraWeaveForestManifest.h` implements an optional
JSON adapter, `world::forestSceneFromManifest`, and
`WorldScene` owns/updates/submits/shuts down the forest **only when**
`terraweaveForest` exists at manifest root. No existing
`WorldScene` manifest without this key is changed. The opt-in host
uses the **first** observer from `WorldScene::update`; it does not
currently union multiple observer budgets.

Example root manifest fragment (all coordinates in cm inside the
`terraweaveForest` object; existing world mesh/camera coordinates
outside this block still use WorldScene's normal metre units):

```json
{
  "terraweaveForest": {
    "terrainSeed": 42,
    "biomeSeed": 143,
    "scatterSeed": 55,
    "baseHeightCm": 0,
    "reliefCm": 6000,
    "basePeriodCm": 6400,
    "octaves": 5,
    "spacingCm": 450,
    "densityPermille": 650,
    "chunkSizeCm": 6400,
    "radiusChunks": 4,
    "maxResidentChunks": 64,
    "highRangeCm": 3000,
    "mediumRangeCm": 9000,
    "lowRangeCm": 12000,
    "cullRangeCm": 24000,
    "impostorBlendWidthCm": 3000,
    "species": [
      {
        "id": 1,
        "shape": "broadleaf",
        "minHeightCm": 900,
        "maxHeightCm": 1400,
        "branches": 24,
        "weight": 4,
        "prototypeSeed": 1001
      },
      {
        "id": 2,
        "shape": "conifer",
        "minHeightCm": 1100,
        "maxHeightCm": 2100,
        "branches": 32,
        "weight": 2,
        "prototypeSeed": 2002
      }
    ],
    "exclusions": [[0, 0, 1200, 1500]]
  }
}
```

The fragment is added to an otherwise valid existing
`WorldScene` root manifest with `async`, `assets`, and
`chunks` populated according to `docs/large-worlds.md`.
No new world format is implied for legacy RTS `MapDoc` maps;
their editor generator is in `TerraWeaveOps.h` and separate.
The forest here is **render-only**: it does not create authoritative
colliders, navmesh obstacles, harvested resource entities or changed
RTS/FPS simulation checksums. Terrain heights derive from the
TerraWeave procedural seed; until a heightmap provider adapter is
implemented, they may not match an independently authored terrain
surface. `WorldScene` releases the forest's models through its
existing `shutdown(device)` path. GPU upload is on the same update
thread as the other world-model uploads.

**Status:** code committed but no local compiler, CTest, real
`WorldScene` launch or GPU screenshot executed, in accordance with
the request to defer tests. Do not treat this as a production-ready
asynchronous forest streaming subsystem. Future work: worker jobs
with revision checks, explicit camera/observer streaming priorities,
sampled-world-terrain alignment, collision/navmesh, editor scene
preview and GPU resource/performance validation.

## Stage 4 — opt-in forest Scene overlay and authored-map exclusions

**Scene integration**: `apps/editor/TerraWeaveForestPreview.h` owns a
persistent `ArborForestAdapter`. The existing
`Editor::renderScene` calls it after the normal terrain/objects draw
but before `Renderer3D::render` (no extra render pass).
`TerrainWindow` has a checkbox to show the generated forest;
it is **off by default**, and enabled only when
`MapDoc::extra.terraweave.version == 1`. The Game window and
simulation remain unchanged.

Preview generates a bounded 5×5 window of 32-metre source tiles around
the camera; placements are global-lattice deterministic even across tile
boundaries. They are regenerated when the active camera tile,
MapDoc revision or saved TerraWeave recipe changes. Two procedural
tree species are preconfigured (broadleaf + conifer) and generated only
once per editor preview lifetime; both geometry and multi-view impostors
are shared via `ArborForestAdapter`. The renderer chooses actual
per-branch LOD and reuses existing GPU instancing/batching/shadows.

**Coordinate space**: procedural `scatter` uses absolute world cm
(e.g. optional generator `originXcm`), but the editor/renderer uses
map-relative metres. The overlay subtracts the recipe origin BEFORE
building draw transforms and samples the *current* editor heightfield
so subsequent sculpt/undo operations move tree roots with the terrain.
It changes no map data or gameplay entities.

**Authored map exclusions**:
`apps/editor/TerraWeavePlacementMask.h` rejects placements on
water/rock/block/road map cells, within a configurable rectangular
clearance around `MapData::entities`, and within a segment-distance
clearance around legacy `MapData::roads`. Mask is presentation-only:
long-double distance arithmetic is allowed here but must not be used
for authoritative network/lockstep placement. It currently does NOT
parse `roadNetwork` splines, building footprints, `waterNetwork`,
or the dynamic collider/navmesh world; those need real provider adapters.

**Resource lifecycle:** an explicit unchecked preview releases GPU
tree prototypes through the existing RHI. Project/map changes or
editor shutdown release preview resources. Browser/mobile runtime and
production world streaming are not part of this editor-only feature.

**Testing:** `tests/terraweave_preview_test.cpp` smoke-compiles the
preview header, checks opt-in toggle and authored placement masks.
The isolated GCC/Clang workflow also builds `terraweave_preview`.
No evidence of a passing editor build, renderer screenshot or measured
GPU performance exists at this stage; maintain PR #24 as Draft.

## Stage 5 — authored terrain and placement providers for generic forests

`engine/world/TerraWeaveForestScene.h` now supports two optional host
callbacks, independent of RTS, FPS and the actual terrain storage:

```cpp
TerraWeaveForestScene forest(settings);
forest.addSpecies(profile, species, prototypeSeed);

// A snapshot from a project/engine physics terrain provider, integer cm:
forest.setHeightSource([&surface](int64_t xCm, int64_t zCm) -> int32_t {
    return surface.heightAtCm(xCm, zCm);
});

// Exclusion/footprint/water provider; evaluated after height sampling:
forest.setPlacementFilter([&surface](const FoliageInstance& tree) -> bool {
    return surface.allowTree(tree.xCm, tree.zCm, tree.heightCm);
});

forest.streamAt({cameraXcm, cameraZcm});
```

Providers are invoked only during `streamAt()` on the owning CPU
work phase. Any replacement automatically invalidates resident
patches and increments the host revision. The deterministic placement
IDs do not change when elevation or filtering changes; only final
`heightCm` and presence can change. An invalid returned height outside
the supported forest world extent throws before GPU submission.

**Important:** callbacks must read an immutable terrain/collision
snapshot for the duration of a regeneration. Do not capture mutable
ECS data and race it against asynchronous workers. The current host
is still synchronous, and these callbacks are presentation-only.
They do **not** change the biome/slope evaluation in the underlying
`scatter()`: if a project has independently authored very steep or
underwater terrain, its `PlacementFilter` must reject unsuitable
candidates. A later core API can support complete terrain-surface
sampling for both selection and final height.

Tests: `tests/terraweave_forest_scene_test.cpp` and CTest target
`terraweave_forest_scene` (height override, stable keys, filter,
revision invalidation, invalid height). Tests have been authored,
but are not claimed to have executed.
