# TerraWeave — procedural terrain, biomes and vegetation (AIVERSE)

Status (2026-11-19): **Core + Arbor/Flora, Horizon HLOD, async forest/terrain scene adapters, authored near/far Q16 stitching and Stage 22 diagnostics implemented in source**. GPU, editor, integration and performance verification remain pending; see the Draft PR and handoff for exact test status.
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


## Stage 6 — actual WorldScene terrain heights and footprint placement

The opt-in `WorldScene` path now uses a typed, immutable bridge from
loaded `world::ChunkCollision::terrain` to TerraWeave forest placement.
`engine/world/TerraWeaveWorldHeightSource.h` contains
`AuthoredHeightTileCm` and `AuthoredWorldHeightSnapshot`.
The snapshot's `sample(worldXcm, worldZcm)` returns an optional
signed **centimetre** elevation from loaded terrain tiles. It uses
`WorldAssets::cellSize` and negative-safe global floor division,
then **piecewise triangular interpolation** matching the exact
`readChunkCollision` triangles `(a,c,b)` / `(a,d,c)`, not
bilinear smoothing across the diagonal. Source height samples are
converted from `world::Fixed` metres to integer centimetres at
the worker preparation boundary. The difference from the underlying
Fixed collision surface is at most centimetre-scale sampling/rounding,
and the operation is presentation-only.

`WorldScene::Prepared` caches authored height samples and collision
box footprints **only when `terraweaveForest` was explicitly
enabled**. On terrain/footprint-bearing chunk publish or unload,
`WorldScene::update` assembles one immutable snapshot from all
resident tiles, plus an exclusion mask from resident collision-box
X/Z footprints. It calls the new
`TerraWeaveForestScene::setLandscapeSources(heightSource, mask)`
exactly once per update: one generation revision invalidation, with
old snapshots safely held by any in-flight asynchronous work.
The forest's own revision-safe async publisher discards stale results.

For areas with no currently loaded authored height tile, the adapter
falls back to TerraWeave's configured deterministic
`heightAtCm`. **This can create a seam between authored and procedural
terrain when loading/unloading tiles**; it is not a full world terrain
replacement or a solution for arbitrary physics triangle meshes.
The footprint mask conservatively excludes the X/Z projections of
all resident collision boxes, regardless of altitude. WorldSpace
`kWorldLimitCm` checks and explicit chunk CPU reservation accounting
remain in effect. Legacy scenes without `terraweaveForest` keep the
original code path and do not allocate these snapshots.

`FoliageExclusionMask` now buckets building rectangles into 64 m
spatial buckets for per-candidate checks. Large rectangles (>256
buckets) use a bounded per-shape overflow path rather than being
duplicated across tens of thousands of buckets. Road and polygon
queries preserve their prior behaviour.

## Stage 6b — stable removal/restore state for interactive trees

`TerraWeaveForestScene` also exposes
`ForestTreeIdentity {key, speciesId}` and
`removeTree(id)`, `restoreTree(id)`,
`setRemovedTrees(vector<ForestTreeIdentity>)`, and
`removedTrees()`. It tracks a bounded sorted set of identities
(up to 1,000,000) separately from deterministic scatter outputs.
`visibleBatches()` excludes removed trees immediately; residency
eviction/regeneration does not resurrect them. `ForestSceneStats`
adds `removed`. This allows a native game to serialize stable tree
removal state with its own save format or map an interactive tree to
a gameplay ECS entity **without allocating ECS entities for all
decorative foliage**.

The removed-tree identity is meaningful only under the matching
generator seed/version/species configuration. The host still owns
actual game save/load, authoritative harvesting, inventory,
network replication and collision/navmesh updates. This visual
filter alone does not implement those gameplay systems.

**Verification:** this stage was committed through the repository
connector; source inspection only. Builds, CTest, live GPU launch and
screenshots remain explicitly **not run**, per user request.

## TerraWeave Horizon — kilometre-scale landscape and forest HLOD

`engine/world/TerraWeaveHorizon.h` generates a **coarse forest landscape
snapshot without expanding distant trees to individual objects**.
`engine/render/TerraWeaveHorizonModel.h` packs the entire snapshot into
one standard AIVERSE `render::Model` with two materials/primitives:
(1) a very low resolution far-terrain heightfield, with biome vertex
colours and slope normals, and (2) hundreds of **cluster-level crossed
billboards** sharing one RGBA8 silhouette atlas. `Renderer3D` therefore
does not submit one draw per tree, nor one draw per 128/512-metre
forest cluster. Shadow casting is disabled on far impostors.

Unlike near tree LODs, the cluster silhouettes are generated directly
from low-frequency climate/heightfield samples; individual trees are
never generated at this tier. This makes the cost bounded by the
terrain grid and number of cluster tiles, not the number of simulated
or visually implied trees.

### Default range hierarchy

| Approximate distance | Representation |
| --- | --- |
| 0–250 m | Existing detailed trees and per-tree impostors (ForestScene) |
| 250–1,000 m | 128-metre cluster silhouettes |
| 1–6 km (roughly 8 km at diagonal) | 512-metre cluster silhouettes |
| Beyond 1,100 m terrain | One 48×48-quad geometry patch with a near cutout |

The ranges are adjustable through `HorizonSettings`. The nearest
range is coordinated with `ForestSceneSettings.view.cullRangeCm`
(default 240 m) to avoid duplicate detailed trees. The single large
proxy is **not** a substitute for collision, walkable ground, pathfinding,
close-up rocks or detailed tree meshes.

### Approximate *upper bounds*, not measured GPU performance

Default `estimateHorizonBudget(HorizonSettings{})`:
- 832 possible forest cluster slots in total (16×16 middle + 24×24 distant).
- One 1392×1392 RGBA8 texture, **7,750,656 bytes** before mipmaps.
- At most 9,253 vertices and 8,320 triangles (including outer skirts and two crossed
  quads per forest cluster; many clusters skipped in sparse biomes).
- One `Model` with at most **two material primitives**. Actual main-pass
  draw count varies by renderer/backends/visibility; shadows,
  reflections, render passes and hardware have not been benchmarked.

This is *much* cheaper geometrically than distant individual foliage
but not literally free: alpha overdraw, one-time CPU bake, texture
upload, memory and shadow/atmospheric composition still cost resources.

### Optional WorldScene manifest

```json
{
  "terraweaveHorizon": {
    "terrainSeed": 1,
    "biomeSeed": 2,
    "middleTileCm": 12800,
    "middleTilesPerSide": 16,
    "distantTileCm": 51200,
    "distantTilesPerSide": 24,
    "closeForestEndCm": 25000,
    "middleForestEndCm": 100000,
    "terrainBeginCm": 110000,
    "terrainSkirtDepthCm": 1200,
    "terrainResolution": 48,
    "atlasTilePixels": 48,
    "canopyHeightCm": 1800,
    "distantTerrain": true
  }
}
```

No `terraweaveHorizon` block means **no additional memory, GPU work
or change to the existing world**. If `terraweaveForest` is also in
the manifest, Horizon inherits the near forest's terrain/biome seeds
unless explicitly overridden. Use matching seeds and elevation
conventions to avoid near/far colour and silhouette mismatches.
`distantTerrain: false` draws only cluster forests, useful when an
existing world terrain renderer already covers far ground.

`engine/render/TerraWeaveHorizonScene.h` reuses
`AsyncChunkStream`: one queued/cancelable CPU region bake, bounded
staging memory, stale region rejection, render-thread RHI upload.
The previous uploaded proxy remains visible until the new region is
fully published. Region position snaps to a 128m camera grid so no
atlas is regenerated every frame. `WorldScene::update/draw/shutdown`
owns this automatically for an opted-in manifest.

### Known limitations and next work

- Cluster textures are **procedural silhouette/colour approximations**,
  not full multiview photographic or normal/depth-accurate atlases.
  Two crossed ribbons are a cheap all-azimuth fallback; a future
  offline baker should capture real forest materials and directional
  lighting, with atlas paging and mips/alpha coverage preservation.
- Mid-to-far transitions use distance ranges but not yet full
  alpha/dither crossfade. First GPU visual proof is required to
  identify popping and vegetation/ground colour discrepancies.
- Far terrain is a coarse mesh with a central hole (to avoid z-fight
  with nearby authored geometry), not a depth-aware mountain panorama.
  It must be disabled or coordinated with independently authored
  far-terrain patches to prevent overlap/double rendering.
- Rebuilding the atlas when the camera crosses a 128m cell runs on
  the existing background worker but is still CPU work; pack/cache
  prebaked assets for entirely static game worlds and profile the
  upload latency. The renderer and WorldScene still need screenshots
  and actual GPU measurements.

Tests added: `tests/terraweave_horizon_test.cpp`
(`aiverse_terraweave_horizon_test`, CTest `terraweave_horizon`).
Tests cover deterministic baking, atlas contents, card/terrain mesh
bounds, topology and budget, invalid settings, and constructing the
async host without an initialized GPU. **Tests are authored but not
confirmed passing**. See `docs/terrain/AGENT_HANDOFF.md`.

## Stage 8 — layered cluster crowns, stable transition bands and atlas quality tiers

`TerraWeaveHorizon.h` now rasterizes a cluster as a deterministic
**overlapping canopy** instead of a row of vertical green strips.
Each texture tile contains hashed broadleaf ellipses, occasional
tapered conifer crowns, variable heights/colours, gentle alpha-test
edge coverage and sparse exposed trunks. This is intentionally a
stylized **procedural approximation** of a forest silhouette, not a
render of real 3D foliage assets. The GPU still sees the same
two crossed cluster cards and one shared atlas.

`HorizonSettings` has these optional quality and LOD properties:

| C++ / JSON field | Default | Purpose |
| --- | --- | --- |
| `crownDetail` | 2 | 1–3: CPU-only canopy silhouette complexity; no extra GPU cards |
| `orientationViews` | 1 | 1: use the same atlas frame for both crossed cards; 2: bake separate X/Z projections |
| `nearBlendCm` | 6000 (60m) | Spread spatial HLOD entrance into the final 60m of detailed forest |
| `tierBlendCm` | 16000 (160m) | Stagger 128m→512m cluster coverage around the 1km boundary |

The band logic uses deterministic **spatial stochastic alpha masking**.
A cluster's canopy coverage ramps in/out through hashed pixel
thresholds based on distance to the *snapped bake camera*. The
near-to-mid layer starts partially visible before individual trees
stop, and mid/far clusters overlap at reduced coverage rather than
switching all cards at once. This has no additional per-cluster draw
calls or new GL/Metal shader. It is **not a continuous per-frame
alpha crossfade**: the atlas is still recomputed only when the
background Horizon region is updated. Camera movement across a
region boundary may cause residual small pops; only actual orbit/zoom
screenshot tests can establish perceived continuity.

When both opt-in `terraweaveForest` and `terraweaveHorizon`
blocks exist, the default `closeForestEndCm` is inherited from
the detailed forest's `FloraView::cullRangeCm` unless explicitly
overridden. This avoids the former 240m vs 250m gap in the default
near/far ownership. If `Horizon` is used alone, it retains its
250m close boundary. Existing scenes with no Horizon block do
not change.

`orientationViews=2` stores independent azimuth-orthogonal
projections of each cluster and uses a different UV subrectangle
for the corresponding crossed card. It still uses **one shared
image and one model with two primitives**, exactly the same card
geometry and draw submission topology. High-quality mode costs
more atlas memory/CPU bake work, but not more triangles.
`orientationViews=1` remains the mobile/low-memory default.

Approximate maximum atlas bytes, before mipmaps and image staging:
- 1 view: 1392×1392 RGBA8 = **7,750,656 bytes** (~7.39 MiB).
- 2 views: 1968×1968 RGBA8 = **15,492,096 bytes** (~14.77 MiB).

`estimateHorizonBudget` validates both options within the
16 MiB packed-atlas cap. For real performance use
`TerraWeaveHorizonScene::runtimeStats()` (cluster count,
uploaded atlas bytes, vertices, triangles, CPU preparation
milliseconds, render-thread upload milliseconds, pending/canceled
async tasks). These are measurements captured by the running
host **only once a scene is actually executed**, not benchmark
results supplied by this document.

Example opt-in quality settings within `terraweaveHorizon`:
```json
{
  "crownDetail": 3,
  "orientationViews": 2,
  "nearBlendCm": 6000,
  "tierBlendCm": 16000
}
```

Tradeoffs / unverified:
- The spatial mask can create gaps or partially doubled canopy
  in its overlap bands; better complementary per-pixel fade and
  material-driven frame transitions require GPU screenshots,
  alpha coverage testing and perhaps a dedicated shader pass.
- Differing near forest density, custom species, authored roads
  and nonprocedural landscape still need production integration.
- The CPU image baker runs on existing background work threads
  and the atlas memory cap is an analytic bound. Neither GPU
  timings nor frame stability nor image quality has been
  verified on Metal, Vulkan or GLES in this stage.

## Stage 9a — authored landscape inputs for Horizon

`bakeHorizon(..., const HorizonSurfaceSources* source = nullptr)`
now accepts optional **immutable** host-provided functions:
`heightAtCm(x,z)`, `biomeAt(x,z)` and `allowsCanopyAt(x,z)`.
The old overload remains source-compatible and uses procedural
TerraWeave noise whenever a callback is absent.

This resolves the previous pure-noise-only limitation at the *CPU
bake* boundary: independently authored worlds can supply their
heightfield tiles, biome masks and road/shore/structure clearings.
A disallowed canopy sample is skipped before rasterization; it
does not require another draw call or an individual-tree entity.
Both forest/terrain vertex colours and their heights sample the
same source snapshot, so distant ground can be made consistent
with nearby collision/terrain.

The input pointer is **borrowed during a single bake**; asynchronous
hosts must hold a `shared_ptr<const HorizonSurfaceSources>`
or equivalent immutable snapshot while worker jobs are running.
Do not capture mutable ECS world objects in worker callbacks.
Callback source values and GPU visual agreement have **not**
been verified by tests or screenshots in this stage.

### Horizon snapshot connection in WorldScene (stage 9b)

`TerraWeaveHorizonScene::setSurfaceSources(
std::shared_ptr<const HorizonSurfaceSources>)` accepts an immutable
sampling snapshot from the owner thread. Internally the worker copies
the shared pointer *before* invoking `bakeHorizon`. Replacing a
snapshot cancels the selected asynchronous cell and queues a new
revision on the next `update()`; the last successfully uploaded
model remains available until a replacement succeeds.

`WorldScene` now populates authored `AuthoredWorldHeightSnapshot`
and `FoliageExclusionMask` whenever **either** an opt-in near forest
or an opt-in Horizon renderer is active. It supplies resident height
tiles to the far-terrain sampler and uses resident collision-box
footprints to clear canopy. It does not modify source collision,
gameplay ECS or deterministic placement IDs. Unloaded terrain tiles
fall back to the Horizon procedural height seed; this is not yet a
guarantee of seamless authored terrain beyond the resident world.

Do **not** call `setSurfaceSources` once per frame: it invalidates
the current pending region and would cause repeated CPU bakes.
Only publish an updated snapshot when the underlying authored
terrain/obstacle residency changes, as `WorldScene` currently does.

### Horizon cache and camera-cell hysteresis (stage 9c)

`HorizonSettings::maxCachedRegions` (default 2, range 0–4)
enables a **bounded CPU-side LRU cache of immutable baked Horizon
regions**. Re-entering a recently visited camera region reuses its
already rasterized forest/terrain snapshot, while a new GPU model
is still uploaded through the existing `Renderer3D` RHI path.
This saves CPU canopy baking, not GPU upload work. `0` disables
retention for constrained devices. A new authored landscape
`setSurfaceSources()` revision invalidates cached regions and
cancels the current pending publication. Old worker snapshots
continue to exist only as long as jobs need them.

`HorizonSettings::regionHysteresisCm` (default 3200 cm)
holds the selected 128-metre camera cell until the observer moves
a further 32 metres past its previous boundary, reducing repeated
streaming near grid edges. It must be less than half the
`middleTileCm` cell side. Both fields are configurable through
`terraweaveHorizon` JSON.

`TerraWeaveHorizonScene::runtimeStats()` now returns
`cachedRegions` and `cacheHits` in addition to previous
CPU/GPU/geometry stats. The default cache can retain two packed
RGBA atlases **in addition to** the current render model, so
memory grows predictably; cache capacity is not a hard ceiling
for all temporary worker/upload staging allocations.

No disk cache or runtime skip-GPU-upload was implemented by this
stage. Benchmarks and cache-key/invalidation/camera-boundary tests
are explicitly delegated to a separate test agent.

### Horizon quality presets (stage 9d)

Native C++ projects can call
`horizonSettingsForQuality(HorizonQualityPreset::Minimal|Mobile|Balanced|High)`
to receive validated, genre-independent Horizon settings. The
opt-in WorldScene `terraweaveHorizon` block accepts
`"quality": "minimal" | "mobile" | "balanced" | "high"`
(default `balanced`); **individual JSON fields override the preset**
without another rendering path or a shader fork.

| Preset | Atlas tile | Terrain grid | Crown detail | Atlas views | CPU regions cached |
| --- | ---: | ---: | ---: | ---: | ---: |
| minimal | 16 px | 16×16 | 1 | 1 | 0 |
| mobile | 24 px | 24×24 | 1 | 1 | 0 |
| balanced | 48 px | 48×48 | 2 | 1 | 2 |
| high | 48 px | 64×64 | 3 | 2 | 2 |

The same ~832 potential cluster slots apply; the atlas resolution
changes rather than the number of forest cluster cards. Minimal
and mobile deliberately avoid retaining additional CPU atlases.
The two-view high preset increases the texture budget to about
14.77 MiB **before mipmaps**, requiring real-device profiling
before being a recommended default. These are intended presets,
not measured FPS or validated platform quality settings.

### Prebaked Horizon archive assets (stage 9e)

Static worlds may avoid the expensive canopy rasterization across game
restarts by preparing a region offline and shipping a `.twh`
asset. `engine/world/TerraWeaveHorizonArchive.h` provides pure
portable C++20 APIs (no filesystem, OS or rendering dependencies):

```cpp
const uint64_t fingerprint =
    terraweave::horizonArchiveFingerprint(terrain, biomes, settings);
auto bake = terraweave::bakeHorizon(terrain, biomes, settings,
                                    regionCenterXcm, regionCenterZcm);
std::vector<uint8_t> bytes =
    terraweave::encodeHorizonArchive(bake, fingerprint);
// The game/editor build tool writes bytes into its own asset pack.

// At runtime, given the *same* settings and matching world content:
terraweave::HorizonBake decoded =
    terraweave::decodeHorizonArchive(bytes, fingerprint);
```

Archive wire format is currently versioned `TWH3`, explicit little-endian
integer fields, packed RGBA atlas, terrain vertices and cluster
metadata, with a 64-bit FNV payload checksum and 20 MiB maximum
serialized size. It is intended for *trusted application assets*;
the checksum is **not cryptographic authentication**. Decoder
checks version, fingerprint, sizes, atlas frame bounds, payload
integrity and trailing bytes before returning any bake.

`TerraWeaveHorizonScene::setArchiveStore(...)` accepts an optional
shared `HorizonArchiveStore` with `read` and/or `write`
callbacks and an optional stable `authoredContentHash`. Configure
it **before the first update()**, because callbacks are then
invoked by the bounded worker. The worker checks the in-memory
LRU first, then a prebaked archive, then falls back to a fresh
procedural bake. Failed/corrupt/stale archive reads and
write failures are best-effort cache misses, **not fatal**.
The runtime keeps only one uploaded Horizon GPU model, regardless
of how many CPU archives are available. `runtimeStats().archiveHits`
counts successful archive restores.

For `WorldScene` game manifests, optionally add:

```json
{
  "terraweaveHorizon": {
    "quality": "balanced",
    "archivePrefix": "world/horizon-cache"
  }
}
```

It looks for `world/horizon-cache/<cellX>_<cellZ>.twh`, where
X/Z are signed **middle-Horizon camera-grid keys**, through
the normal game `AssetReader`. No configured prefix means no
asset lookups. The WorldScene adapter is **read-only**; the build
pipeline is responsible for producing and packing the blobs.

Important for authored terrain/roads: if the async Horizon has an
external `HorizonSurfaceSources` snapshot, disk archive reuse is
disabled unless the caller provides a nonzero **stable content
fingerprint** via `HorizonArchiveStore::authoredContentHash`
(or manifest `archiveContentHash`). That value must change
whenever surface heights, biome paints, road/water/structure masks
or other region-affecting authored data change. This avoids
silently loading a stale canopy/terrain snapshot. Without an
external authored source, a zero hash denotes a purely
procedural world and archive lookup remains available.

**Verification explicitly delegated:** portability
(AppleClang/Clang/GCC/MSVC), archive round-trip, corrupt/truncated
bytes, stale fingerprints, missing pack asset fallback, alpha
atlas GPU upload and races with source invalidation. The authoring
agent has NOT built or run tests for this code.


## Stage 10 — bulk asset baking and selective Horizon regeneration

### Host-only bulk bake

The reusable `engine/world/TerraWeaveHorizonBatch.h` declares
`HorizonBatchBounds`, `HorizonBatchOptions`,
`HorizonBatchStorage` (game-supplied read/write/cancel callbacks),
`HorizonBatchProgress` and `bakeHorizonBatch(...)`.
It generates deterministic signed camera region keys in z-major,
x-minor order, using the **same grid and fingerprints as runtime**
HorizonScene. Up to 4,096 regions are allowed by default. Existing
valid TWH2 archives are skipped; wrong settings/version/CRC/missing
assets are regenerated. The tool never accesses the GPU or
authoritative ECS.

`aiverse_horizon_bake` is a host-only CMake target built with
`AIVERSE_BUILD_TOOLS=ON`, disabled for Emscripten. Example:

```sh
aiverse_horizon_bake \
  --manifest content/world.json \
  --out content/world/horizon-cache \
  --cells -4 -4 4 4
```

Each region produces `<x>_<z>.twh` in that output folder.
Pack the files as normal content and configure the same
`terraweaveHorizon.archivePrefix` in the runtime manifest.
`--force` bypasses skip-existing; `--max-regions N`
limits work. The executable builds *procedural* Horizon
surfaces only. Projects with authored collisions/roads/water
instead use `bakeHorizonBatch` directly with their immutable
`HorizonSurfaceSources` snapshot and a nonzero
`authoredContentHash`.

### Stable atlas slots and partial CPU updates

Before partial regeneration each cluster receives a **stable atlas
slot independent of visible cluster count**:
`(layerBase + row*rowCount + col) * orientationViews`.
Changes to road/shore mask, terrain height or biome in one
region do not move unrelated cluster UV subrectangles.
This atlas-layout change advanced the archive format to
**TWH2/version 2**; previous TWH1 blobs must be regenerated.

`bakeHorizon(..., source, previous, editedRegions)` can reuse
an immutable previous `HorizonBake` when its snapped camera,
atlas dimensions, terrain grid and region origin match and
`editedRegions` is nonempty. It copies the previous atlas,
reuses unchanged cluster records and their pixels, clears
only affected stable atlas tiles, redraws affected canopy
pixels, and recalculates only edited terrain vertices.
A conservative cluster-width margin captures affected
neighbouring canopy/slope samples. Incompatible previous
bakes or empty change bounds perform a full bake.

`TerraWeaveHorizonScene::setSurfaceSources(source, editedRegions)`
now captures old CPU bake and an immutable source generation
snapshot for the background job. The GPU model remains visible
during regeneration. Regions not intersecting the currently
displayed Horizon footprint do not need an immediate rebake.
On mobile/minimal configurations with `maxCachedRegions=0`,
the full-regeneration path avoids retaining an extra CPU
Horizon bake. Authored source changes bypass stale static
archives rather than silently restoring old foliage.

`WorldScene::update` records touched loaded/unloaded
authored height/obstacle cells as world-centimetre
`RectCm` bounds and passes them only to the Horizon
snapshot adapter. The near forest maintains its separate
revision-safe streaming model; no gameplay collision,
navmesh or ECS entities are modified.

**Scope limit:** partial updates reduce CPU atlas
rasterization but still rebuild and upload the combined GPU
model/atlas. There is no partial GPU texture update and no
dynamic material-driven temporal crossfade in this stage.
Both require actual backend quality/performance validation.

**Verification:** These are implemented source APIs only.
The separate testing agent is responsible for builds, CTest,
archive round trips, moving-camera snapshots, memory and GPU
captures. None were executed by the feature agent.

### Stage 11b — incremental GPU atlas subregion uploads

Horizon can now skip replacing an entire GPU `render::Model` for
**atlas-only** changes that preserve every terrain vertex
(including colour), camera-clipped near terrain topology, forest
cluster positions, heights, sizes and UV slots. This is a
conservative optimization, not a new shading backend:

- `engine/render/TerraWeaveHorizonGpuPatch.h`:
  `horizonCanReuseModelGeometry(oldBake,newBake)`,
  `planHorizonAtlasUpload(oldBake,newBake,atlasTilePixels)`,
  `applyHorizonAtlasUpload(gfx::Device&,Model&,newBake,plan)`.
- The pure CPU planner identifies changed fixed atlas slots,
  coalesces horizontally adjacent/vertically equal runs, and
  defaults to **at most six** rectangle updates totalling at
  most 25% of the current atlas RGBA bytes. Larger updates,
  invalid resident atlas textures or any changed geometry
  use the existing full `uploadModel` path.
- On the GPU owner thread, `gfx::Device::updateTexture`
  updates RGBA8 subregions. The engine's existing GL/Metal/
  Vulkan implementations regenerate mipmaps as required.
  No new graphics backend calls or per-tree draw calls.
- `TerraWeaveHorizonScene::runtimeStats()` exposes
  `atlasPatchCount`, `atlasPatchBytes`,
  `atlasPatchRegions` and `fullUploadCount`. The
  counters are meaningful only when an actual scene runs;
  no frame-rate claims have been made.

Only atlas pixels are patched; changes to cluster count,
geometry, base colour, far terrain vertices or authored
heightfield force a full model swap. This stage **does not**
provide partial GPU vertex buffers, continuous per-frame
crossfade, or eliminate the CPU worker's model preparation.
The user assigned validation to another agent: test partial
uploads including mips/row-pitch on Metal, GL, Vulkan and
real mobile/desktop workloads before marking production-ready.

### Stage 12 — GPU terrain vertex subrange updates (geometry-safe)

Horizon may now commit **small terrain height/normal/vertex-colour edits**
directly into the existing GPU vertex buffer, instead of reuploading an
entire distant forest/terrain Model. This uses the same cross-genre RHI:

- `render::Model::batching.allowGeometryPool` is a generic bool,
  default `true`. `TerraWeaveHorizonModel` sets it to `false`
  to guarantee a dedicated GPU vertex/index allocation. Never blindly
  call `updateBuffer` with model-relative offsets when
  `model.geometryPoolPage` is present: the pooled VB can contain
  several unrelated models with nonzero base offsets.
- `TerraWeaveHorizonGpuPatch.h` has
  `horizonCanReuseHorizonLayout()`: preserves source camera region,
  terrain X/Z positions, atlas footprint, all cluster crosscard/UV
  geometry and index topology while allowing changes in *terrain*
  height/colour/normal.
- `TerraWeaveHorizonVertexPatch.h` exports the pure CPU
  `planHorizonVertexUpload()`, and owner-thread
  `horizonCanApplyVertexUpload()` /
  `applyHorizonVertexUpload()`. Planning requires exact
  unchanged index data, node and primitive topology, unchanged
  crossed-card vertex bytes, and new computed model/primitive
  bounds contained in the resident conservative bounds.
  Other changes trigger the existing full RHI model upload.
- The default vertex patch budget is at most **20 contiguous ranges**
  and **35% of terrain vertices**. A successful patch issues
  `gfx::Device::updateBuffer` on the same owning thread that
  publishes the CPU forest atlas and renders models; changed atlas
  tiles can be patched in the same publication.
- The previous CPU `ModelVertex` and index snapshots are captured
  **after** `prepareModelUpload`, before `uploadModel` discards
  its CPU arrays. They are retained only with an enabled Horizon
  CPU cache (`maxCachedRegions>0`). This avoids keeping an extra
  7–15 MiB atlas CPU copy inside the resident GPU Model. Minimal
  and mobile presets keep full-upload fallback to minimize memory.
- `HorizonRuntimeStats` adds `vertexPatchCount`,
  `vertexPatchBytes`, and `vertexPatchRanges` alongside atlas
  patch/full upload counters.

**Important safety limits:** `gfx::Device::updateBuffer` is a
void/nontransactional device API; a backend or GPU device loss cannot
provide a partial-patch success guarantee. All geometry/data/range
validation happens before issuing GPU writes, but true rollback
requires general RHI device-loss recovery. Renderer culling bound
expansion is deliberately NOT part of this optimization; edits that
would grow any draw bounds use the full upload. This is a
correctness-first fast path. GPU/device/CTest testing is owned by
another agent and **has not been executed** in this development
stage.

## Stage 13 — bounded GPU residency for Horizon regions

`TerraWeaveHorizonScene` now reuses previously **uploaded**
region `render::Model` resources after the observer returns
to an earlier camera cell. This GPU residency is **distinct**
from the existing CPU `maxCachedRegions` atlas LRU and from
the streaming/staging worker budget.

`engine/render/TerraWeaveHorizonGpuCache.h` implements
render-thread-only `HorizonGpuRegionCache`. An inactive
entry owns its uploaded RHI `Model`, matching immutable
CPU bake when available, optional terrain CPU
vertex/index data for incremental updates, camera
coordinates, model bounds-related metadata and a
source-generation tag. `take` transfers model ownership
without copying or reuploading geometry; `retain` can
evict LRU entries by count and estimated GPU bytes;
`prune` rejects authored-terrain revisions, and
`clear(device)` explicitly destroys cached buffers
and textures.

Configurable `terraweaveHorizon` manifest properties:
- `maxCachedGpuRegions`: 0–3 additional GPU region
  models (Balanced default 1; Minimal/Mobile 0,
  High requests 2).
- `gpuCacheBudgetMiB`: 8–96 MiB for the **additional**
  cached regions (default 32 MiB). The current visible
  model and async upload staging are separate, bounded
  through their existing contracts. Approximate byte
  accounting includes vertex/index buffers, atlas RGBA8
  and 4/3 mip allowance, but not exact driver overhead.

Navigation A→B retains the visible A model while B
is prepared. On successful B publication, A can
enter the bounded GPU cache. On B→A cache hit, A
becomes active immediately with **no new CPU region
bake and no full GPU model upload**; meanwhile
canceled background jobs are still drained to
release staging reservations. Source revisions
invalidate all stale cache entries and reject late
CPU results from old landscape snapshots.

`TerraWeaveHorizonScene::runtimeStats()`
now reports `gpuCachedRegions`, `gpuCachedBytes`
(estimated extra GPU use), and `gpuCacheHits`.
These are instrumented counters, not measured FPS.

**Lifecycle and correctness:** all GPU resource
retirement goes through `render::releaseModel(device)`
on the owning RHI thread and explicit `shutdown(device)`.
No per-tree ECS entities, separate renderer,
backend-specific calls, or unbounded GPU caching.
A disabled/opt-out Horizon creates no new caches.
Quality presets and native projects can opt out by
setting `maxCachedGpuRegions=0`.

**Status:** source code was committed, but builds,
CTest, platform GPU screenshots, GPU memory tracking,
and frame-time measurements are the separate
validation agent's responsibility. Runtime GPU
reuse and lifecycle are not yet verified.


The GPU residency counters additionally include
`gpuCacheEvictions`, counting models explicitly released from
the inactive LRU due to count/byte limits or stale terrain
revisions. This excludes active-region replacement when no
cache could retain the old model and does not measure GPU
driver allocations or frame time.

## Stage 14 — temporal transition between GPU Horizon regions

Horizon now has an optional **neighbor-only temporal crossfade**
when the camera crosses a 128m-region key and the next proxy
has been uploaded (or found in the GPU residency LRU).
`HorizonSettings::regionTransitionMs` is a bounded
`0..1000` milliseconds, default `240`. It can be
overridden through `terraweaveHorizon.regionTransitionMs`.
`0` disables the fade for all platforms. The Minimal
and Mobile quality presets choose `0` automatically
to conserve GPU memory and avoid alpha-overdraw costs.

During the bounded transition only, the renderer draws
the outgoing and incoming already-uploaded `render::Model`
proxies with complementary smoothstep alpha using
**existing** `Renderer3D::ModelDraw.tint.w` and
`kDrawTransparent` rendering flags. Both are
non-shadow-casting during the effect. The normal
single, opaque, static model draw path resumes
immediately afterwards. There is no new shader,
per-tree foliage model, framebuffer or RHI backend.

The outgoing model occupies ONE of the existing
inactive GPU residency slots and is subject to
`maxCachedGpuRegions` and `gpuCacheBudgetMiB`.
`HorizonGpuRegionCache::reserveTransient` evicts
inactive LRU models as necessary. On completion,
the outgoing model is stored back into that GPU LRU
when the authored source revision still matches;
otherwise it is explicitly freed using
`render::releaseModel(device)`. Camera changes
across more than one grid cell, updates to terrain/
water/road masks or insufficient GPU budget skip
temporal blending to avoid stale/outlandish visuals.

New diagnostics:
`HorizonRuntimeStats::transitionStarts`,
`transitioning` and `transitionProgressPermille`.

**Important:** Conventional alpha blending can
produce sorting/depth artifacts when two terrain
rings overlap or when crossed forest billboards
intersect. This is a short, bounded visual
transition, **not** order-independent transparency
or a depth-aware geomorph. It is unverified on
real render backends. The user assigned all
CMake/CTest, GPU screenshots and performance
testing to a separate agent.

## Stage 15 — opaque far-ground ownership during Horizon transitions

The previous Horizon crossfade blended **two entire combined
terrain+canopy models**. On overlapping coarse ground grids
this could cause semi-transparent terrain to double-composite,
incorrect depth ordering or Z-fighting.

Now `render::ModelDraw::primitiveMaterialFilter` is an optional
**engine-generic source material selector** (`-1` default,
`0` only source material 0, `1` only source material 1).
The standard `Renderer3D::drawModel` skips unrelated
`Primitive` entries, disabling aggregate material/shadow
batches and static-cache acceleration only for these
filtered calls. All ordinary unfiltered model submissions
keep their existing batching, LOD and shadow logic. The new
field is placed at the end of `ModelDraw` to preserve
source compatibility with positional aggregate initializers.

Horizon has two established material slots in one GPU
model: **0 = distant terrain**, **1 = canopy cluster cards**.
During short `regionTransitionMs` crossfades:

1. Submit **incoming** terrain only, opaque, once, with
   `primitiveMaterialFilter=0`. It writes the depth surface.
2. Submit **outgoing** canopy material 1 at alpha `1 - t`
   through the existing transparent pass.
3. Submit **incoming** canopy material 1 at alpha `t`
   through that same pass.
4. Once the fade is over, go back to one full combined
   model draw with filter `-1`.

The GPU model is **not copied or split into multiple GPU
allocations**; draw calls reuse its existing buffers and
texture atlas. Temporarily drawing two forest materials
costs two transparent model submissions plus one opaque
ground submission. Shadow/light-bake capture is skipped
for partial transient draws. This is still alpha
blending and may show canopy sorting artifacts, but it
prevents simultaneous semi-transparent far-ground meshes.

Navigation also supports direct A→B→A reversal during
the fade: the old region is reclaimed from its temporary
transition slot rather than being evicted by an
over-subscribed LRU. When camera teleporting far outside
the current region's safe footprint, an obsolete proxy
is not submitted while the async worker prepares
the replacement, even on low-memory devices whose
`maxCachedRegions=0`.

Remaining functional/visual caveats:
- Far terrain changes instantly (instead of crossfading
  its geometry). Large mountain skyline changes may
  still pop at a region boundary.
- Transparent canopy cards may intersect/sort poorly;
  real Metal/GL/Vulkan recordings are needed to decide
  whether shared shader-driven dither is worthwhile.
- The safe-footprint teleport gate may temporarily leave
  no horizon when a new region is not ready. This is
  preferable to drawing a distant proxy around a
  camera tens of kilometres away.
- All compilation, CTest, GPU validation and FPS
  measurements belong to the separate test agent;
  code is currently **NOT RUN / UNVERIFIED**.

## Stage 16 — globally aligned Horizon terrain and edge skirts

Horizon terrain vertices now lie on a **globally anchored, signed
rational world lattice**, using `floorDiv(gridIndex * regionSide,
terrainResolution)`. Adjacent camera regions therefore sample the
same terrain positions even at High 64×64 or custom resolutions
where the 512m camera-region shift is not divisible by the
coarse grid spacing. Negative world coordinates remain stable.

Each `HorizonTerrainVertex` stores `slopeXPermille` and
`slopeZPermille`. Normals are obtained from height samples
one global grid step on either side of the vertex, including
outside the current region. Authoring a small terrain edit
also marks the surrounding one-grid-step normal footprint
dirty for partial CPU re-baking. This avoids the old
region-dependent one-sided lighting normals.

`terrainSkirtDepthCm` is an optional **downward, outer-edge
skirt** on the coarse terrain material, from 0 to 5000cm.
It preserves sampled top heights and adds no new material
primitive or separate draw call. Default Balanced/High
1200cm, Mobile 600cm and Minimal off. From a high vantage
point, downward geometry can be visible and should be
tuned with atmospheric fog; it is not terrain geomorphing.

With default 48×48 terrain and 832 possible forest clusters,
analytic worst-case geometry is **9,253 vertices and 8,320
triangles**, including 196 outer-skirt vertices and 384 skirt
triangles. The 1392×1392 shared one-view RGBA atlas still
uses 7,750,656 bytes before mipmaps. Top terrain and
forest remain in the same standard GPU Model.

**Archive format:** TWH3 (version/magic 3) stores both global
slopes and skirt depth; its fingerprint also changes to reflect
the new lattice. Previous TWH1/TWH2 binaries are deliberately
rejected. Use `aiverse_horizon_bake` to rebuild static-world
`.twh` assets or rely on the existing runtime regeneration
fallback. Earlier TWH1/TWH2 references in this document describe
historical stages, **not the current on-disk format**.

**Limitations:** matching border geometry requires the same
immutable source heightfield in the overlapping area. Authored
terrain streaming changes may still require incremental
re-baking; a source swap can visibly change the true landform.
The skirt hides small outer-edge cracks, not abrupt skyline
changes or inner near-terrain cutout mismatches. GPU
screenshots, builds, CTest and platform performance have
**not been run by this agent**; testing belongs to the
dedicated validation agent. See Stage 16 handoff.

### Public world-lattice helper

`makeHorizonTerrainLattice(settings,cameraXcm,cameraZcm)` is a
reusable, genre-independent helper for third-party native
terrain producers and offline tools. The returned
`HorizonTerrainLattice` provides `worldX(i)` and
`worldZ(i)` for vertex indices `0..terrainResolution`
(and ghost indices `-1`, `N+1`) plus the nominal
grid step. The Horizon CPU baker calls that same API.
It avoids reimplementing camera-local rounding in other
engine/world modules. All API and rendering tests are
delegated to the separate test agent.

## Stage 17 — near-ground ownership, inner seam and stable outer skyline

`HorizonSurfaceSources` exposes an optional
`ownsNearTerrainRect(minXcm,minZcm,maxXcm,maxZcm)`
callback. It must return `true` **only when a full
half-open coarse world rectangle is covered by live
near-terrain geometry**. `WorldScene` supplies it from
`AuthoredWorldHeightSnapshot::coversRect`, which
checks all resident near ground tiles and returns
false if any are absent. The generator stores a
`HorizonBake::nearOwnedQuads` byte map
(`terrainResolution × terrainResolution`).

During `makeHorizonModel`, far-ground quads
are omitted where the near mesh owns the
**whole** coarse cell. With a callback present,
this replaces the old uniform radial cutout,
so a missing/unloaded near tile is filled by
far Horizon ground instead of exposing sky.
Without any callback, existing procedural
Horizon settings continue to use the legacy
radial `terrainBeginCm` cutout.

The optional `innerTerrainLipCm` (default **40cm**,
valid 0–200cm) makes a tiny downward seam
along near-owned/far-owned quad boundaries.
At most **512 segments** are generated. Each
segment adds 2 bottom vertices and 2 triangles,
using existing material 0; above that cap the
inner lip is omitted without losing the near
ownership mask. This is not exact polygon
clipping: far quads overlapping only part of
an authored near tile remain rendered and may
still exhibit coplanar depth overlaps.

`terrainGridSnapSteps` (1..16; default **4**)
stabilizes mountain silhouettes. Terrain
sample positions snap to the **nearest**
coarse global lattice anchor. Forest atlases
can still stream on their 128m keys without
moving the distant terrain grid every time.
Minimal preset uses 1, Mobile 2 and
Balanced/High 4. When distant terrain is on,
the outer forest radius is conservatively
limited to the region still guaranteed to
have distant ground at any allowed snap
offset, typically **5+ kilometres**; without
distant ground, forest keeps its original
far radius.

New analytic **default worst-case** with outer
skirt and maximum optional inner seam:
**10,277 vertices / 9,344 triangles** across
terrain and up to 832 cluster cards. This
includes the conservative 512-segment inner
seam cap. A procedural scene with no near
ownership needs fewer vertices and has no
inner seam. One shared forest atlas and
at most two material primitives remain.

### TWH4 archive migration

Current `TerraWeaveHorizonArchive.h` is
**TWH4, version/magic 4**. Its wire payload
adds `innerTerrainLipCm` and the optional
byte-per-cell near-ground ownership map;
its fingerprint includes
`terrainGridSnapSteps` and inner lip depth.
Old TWH1, TWH2 and TWH3 files are **not**
compatible; rebuild with `aiverse_horizon_bake`
or allow the existing runtime worker fallback.
Older sections of this document describing
TWH1–TWH3 reflect past implementation phases.

For full migration in a native project
with authored terrain, bake assets with
the same `ownsNearTerrainRect`, height
snapshot and stable authored-content hash
used at runtime. The generic batch CLI
still supports procedural-only content
and cannot invent independently authored
near-terrain masks.

**Not verified:** zero Z-fighting, truly
seamless mountain skyline, TWH4 decode
or GPU performance. Real builds/CTest
and captures are assigned to the separate
test agent. See Stage 17 of the handoff.

## Stage 18 — precise partial near-ground tile clipping (TWH5)

Stage 17 omitted a far-ground **whole coarse quad** only when
every portion was owned by resident authored near terrain.
The gap-free fallback avoided missing geometry but a *partially*
covered 256m cell could still render an overlapping distant triangle.

The new optional `HorizonSurfaceSources::collectNearTerrainRects`
returns at most 1024 immutable, world-centimetre, half-open
resident near-ground rectangles for the currently prepared Horizon
footprint. `AuthoredWorldHeightSnapshot::coveredRectsIn`
exposes this directly to `WorldScene`, using deterministic
geometric ordering. The CPU `HorizonBake` stores these
rectangles in `nearClipRects` alongside the existing full-quad
`nearOwnedQuads` mask.

`engine/render/TerraWeaveHorizonClip.h` implements
`planHorizonNearGroundClipping(bake)`:
- Each affected coarse far-ground cell is split into the
  same two original triangles.
- Subtract overlapping near-tile rectangles using convex
  half-plane polygons; retain only regions not owned by
  near terrain. Convert them back into bounded triangles
  with their original winding and source-triangle plane.
- Reconstruct clipped vertex positions, normals, UVs
  and RGBA8 colors through barycentric interpolation of
  the original far-terrain triangle, so the new mesh does
  not invent a second, divergent far-ground surface.
- Keep all terrain triangles in the **same material 0
  primitive** and existing shared GPU model. No new
  shader, per-tile draw calls, FPS/RTS module or parallel
  renderer is introduced.
- Enforce a global per-region cap; on excessive geometric
  fragmentation, the planner returns no plan, and the
  entire bake falls back to conservative Stage 17
  full-quad ownership. Invalid or unbounded near input
  never produces an unbounded GPU mesh.

`HorizonSettings::maxNearClipTriangles` controls the maximum
new clipped triangles (0=disabled, 0–8192 accepted).
Default presets are:

| Preset | Near clipped triangles |
| --- | ---: |
| Minimal | 0 |
| Mobile | 512 |
| Balanced | 2048 |
| High | 4096 |

With default Balanced, the worst-case analytic bound
becomes **16,421 vertices and 11,392 triangles**, including
the prior outer skirt, inner 512-segment lip cap and
the extra 2,048 clipped triangles. Actual forests typically
use substantially fewer. The shared atlas budget,
additional GPU cache limits and usual RHI path do not
change.

Diagnostic fields `HorizonRuntimeStats::nearClipQuads`,
`nearClipTriangles` and `nearClipFallback` identify
how many coarse cells were partially clipped, resulting
triangles, and whether the safe budget fallback was
triggered. Values persist through GPU-region caching.

**Archive migration:** `TerraWeaveHorizonArchive.h` is
now **TWH5**, not TWH4. The wire format stores bounded
`nearClipRects` and `maxNearClipTriangles`; the
generator fingerprint includes clip triangle budget
and archive version. Existing TWH1–TWH4 assets must be
regenerated with the offline `aiverse_horizon_bake`
or the existing asynchronous runtime fallback.

**Limits:** This is precise **horizontal X/Z coverage**
against axis-aligned near terrain tile rectangles, NOT
vertical geomorphing. If the authored near terrain lies
higher/lower than its coarse distant representation,
the clipped boundary may still have a vertical crack.
The previously existing 40cm inner lip covers only
whole coarse ownership boundaries, not every new partial
cut edge. Real GPU visual acceptance and any height-aware
stitching remain separate work. Tests, compilation,
Metal/GL/Vulkan screenshots, archive roundtrips and
performance runs have **NOT RUN** in this functional
implementation stage; a different agent owns them.

## Stage 19 — near/far terrain height-welded clip boundaries (TWH6)

Following Stage 18's **exact 2D subtraction** of authored near-tile rectangles
from distant Horizon triangles, the clipped perimeter can now be snapped
to the *actual authored near-terrain height*, leaving the triangle interior
on the existing far-ground surface. This removes a vertical boundary height
gap when the source terrain is resident and the optional sampler is supplied.

`HorizonSurfaceSources::nearGroundHeightAtCm(worldXcm,worldZcm)` returns
`optional<int32_t>` (near height in signed centimetres); `WorldScene`
connects it to the **same immutable resident**
`AuthoredWorldHeightSnapshot` used for near coverage rectangles and
terrain sampling. The CPU helper
`populateHorizonNearSeamSamples(HorizonBake&,source)` finds clipped
polygon vertices along near-tile boundaries, deduplicates them in
world-centimetre Q16 space (fractional positions preserved) and samples
at most **4096** unique near-ground heights. A half-open tile's high
edge is sampled at most 1cm inside that already resident tile to avoid
reading an unloaded neighbor. If a required point cannot be sampled,
the entire seam-height table is withheld and the prior Stage 18 exact
horizontal clipping remains available without half-welded geometry.

`HorizonBake::nearSeamSamples` is an immutable, serialized list of
`{xQ16cm,zQ16cm,nearHeightCm}`. During `makeHorizonModel`,
only clipped-edge **position.y** is replaced with the matching
authored near-surface height. Far-interior vertices stay on their
coarse parent triangle. No new GPU vertex topology, material, draw
call, graphics backend, gameplay ECS entity or collision modification
is introduced by welding.

Both the runtime CPU worker (`TerraWeaveHorizonScene`) and native
`bakeHorizonBatch` offline API populate these samples **before**
storing/serializing the immutable `HorizonBake`. Reading an archive
or GPU model cache restores the same data without consulting the
mutable current WorldScene from the renderer.

New diagnostics:
`HorizonModelBuildStats::weldedVertices/maxWeldHeightDeltaCm`,
`HorizonRuntimeStats::weldedNearVertices/maxNearWeldDeltaCm`.
They survive GPU-cache region restoration. The maximum delta measures
the absolute Y correction applied to a clipped vertex in centimetres.
No measured GPU timings or screenshot-quality guarantees are implied.

### Archive version and limits

**TWH6** is now the current `.twh` wire format and generator
fingerprint. TWH6 includes a bounded count and Q16 X/Z + i32 near height
per sample (20 bytes each; up to 4096 samples, ~80KiB before in-memory
container overhead). Existing **TWH1–TWH5** files are incompatible and
must be regenerated using `aiverse_horizon_bake` or the worker fallback.
The earlier TWH1–TWH5 sections in this document are retained as
historical implementation phases, not current format guidance.

The height-aware weld reduces **vertical cracks**, not necessarily
all visual artifacts: large height differences may produce a steep
sloped polygon near the cut; near-terrain normals can differ from
interpolated coarse normals, and neither fog nor temporal geomorphing
is changed. The default forest atlas and GPU geometry limits remain
unchanged. To verify perceptual quality, test the actual authored
scene with steep terrain, negative coordinates, tile boundaries and
GPU draw sorting. **Tests/build/GPU visual runs were not performed**
by the functionality agent; another agent owns them.

## Stage 20 — slope-safe height welding and shared seam normals (TWH7)

Stage 19 welded the exact near/far clipped-edge Y to the authored
near terrain. This could create a nearly vertical far triangle if the
coarse original height differed sharply across a tiny clipped polygon.
**A normal-map fix alone cannot change the actual steep wedge.**

Now `HorizonSettings` exposes two bounded, optional C++/manifest settings:
- `maxNearWeldSlopePermille` (0–8000; default **2500**).
  `0` disables the slope guard. The CPU seam sampler preflights
  each proposed welded triangle using its actual barycentric original
  far heights and near-boundary heights. A newly induced triangle
  gradient above the limit rejects the **ENTIRE optional height weld**
  for that Horizon bake, retaining the exact Stage 18 X/Z clipping
  and setting `nearWeldSlopeFallback=true`.
  Existing naturally steep far terrain is not flattened.
  This deliberately can leave a vertical seam; no algorithm can
  simultaneously keep an arbitrarily high near edge, an unchanged
  nearby far vertex, zero additional horizontal distance and a
  strict slope cap. Avoid lying about a perfectly seamless result.
- `nearSeamNormalBlendPercent` (0–100; default **65**).
  While constructing an already existing partial near-clip
  triangle mesh, `makeHorizonModel` computes upward-facing,
  **area-weighted geometric face normals** after any valid
  near-height weld. It groups shared clipped corners by signed
  Q16 world X/Z and height to average adjacent face normals.
  Near-edge vertices blend 65% of the repaired normal, interior
  near-border vertices blend 16.25% by default. `0` leaves
  the original far-grid interpolated normals. This changes
  only the CPU-built existing ModelVertex normal attributes:
  **no extra geometry, shader, material, GPU buffer or draw call**.

The CPU worker, offline bake and runtime Model all consume
the exact same immutable seam-height snapshots as in Stage 19.
`HorizonRuntimeStats` adds `smoothedNearNormals` and
`nearWeldSlopeFallback`; these metrics are also preserved
when the GPU LRU restores a previously uploaded region.

### Archive migration: TWH7

`TerraWeaveHorizonArchive.h` is now **TWH7**, version/magic 7.
It adds the seam slope limit, normal blend percentage and
per-bake slope-fallback flag to the binary payload.
The fingerprint includes both configuration settings.
Earlier TWH1–TWH6 `.twh` files are intentionally rejected
and must be regenerated by `aiverse_horizon_bake` or the
existing runtime fallback.

**Known limits:** the default 2500 permille is a *guard for
new weld-induced slopes*, not an upper bound on all terrain
gradients. On extremely narrow clip polygons the guard may
reject a weld for the full region, preserving accurate X/Z
ground ownership but allowing a vertical height mismatch.
Normals are smoothed only on clipped triangles, not on the
authoritative near mesh; this is not shared tessellation or
true normal-field matching. Captured GPU images and geometry
regression tests are REQUIRED before claiming the terrain
is visually seamless.

Implementation stages and exact commit hashes are in
`docs/terrain/AGENT_HANDOFF.md` Stage 20a/b. No CMake,
CTest, platform screenshots or CPU/GPU performance tests
were executed by the feature agent; a separate agent owns
those validations.

## Stage 21 — precise Q16 seam sampling and authored near normals (TWH8)

Stage 20 already implemented an **all-or-none welded-slope guard** and
area-weighted geometric normal smoothing. The current stage improves the
precision and source agreement of the underlying near terrain data.

`AuthoredWorldHeightSnapshot::sampleQ16(xQ16cm,zQ16cm)` samples the
same immutable authored two-triangle height tiles as the existing
integer-centimetre `sample` method, but at signed fractional
world-centimetre Q16 coordinates. Integral inputs retain the exact
old integer-height result. Fractional points use bounded fixed-point
barycentric weights, not floating GPU interpolation.

`HorizonSurfaceSources::nearGroundHeightAtQ16Cm` is **optional**.
`WorldScene` supplies it from the same immutable near-ground snapshot
that provides `collectNearTerrainRects` and
`ownsNearTerrainRect`. A native authored provider may omit it and
retain the legacy whole-centimetre height path.

During `populateHorizonNearSeamSamples`, exact clipped polygon
coordinates are sampled at Q16 precision. At the high side of a
half-open tile the query moves only **one Q16 unit**
(1/65536 cm) into the already resident tile, rather than shifting
by up to 1 cm. A bounded +/-200cm stencil entirely inside the
same tile derives near-surface `slopeXPermille` and
`slopeZPermille`. Missing derivative data merely marks the
normal unavailable; missing height still disables the
entire seam height weld for that region.

Each baked `NearSeamSample` now contains:
- `xQ16cm`, `zQ16cm` (the same seam identity as Stage 19);
- `nearHeightCm` (exact authored Q16-sampled near height,
  rounded to signed centimetres);
- `nearSlopeXPermille`, `nearSlopeZPermille`, plus
  `nearNormalValid`.

`makeHorizonModel` continues using the existing bounded
clipped far triangles. At a welded point with
`nearNormalValid=true`, lighting normals blend toward
the **authored near-source normal**, under the existing
`nearSeamNormalBlendPercent` setting. Interior clipped
far triangles retain the Stage 20 area-weighted face-normal
blending. Neither clipping topology, vertex/triangle
counts, materials nor draw submissions increase.

`HorizonRuntimeStats::matchedAuthoredNearNormals` counts
the clipped render vertices where an authored Q16 surface
normal was actually blended. Existing
`smoothedNearNormals` includes the geometric normal
path. Both counters survive GPU LRU restoration.
The strict atlas-only patch planner refuses modified
seam heights/gradients; safe index-compatible edits can
still use the combined partial vertex upload or
full-model fallback.

**Current archive: TWH8**. The `.twh` payload now stores
32 bytes per near-seam point (Q16 X/Z, height, two
slopes and encoded validity). The bounded 4096-point
table adds at most 48KiB of normal metadata compared
to TWH7; no new GPU memory allocation is required
beyond existing model vertex attributes. TWH1–TWH7
archives are intentionally stale and should be
rebaked with `aiverse_horizon_bake` or regenerated
through the existing worker fallback. Old version
sections below/above are historical only.

**Not yet validated:** compiled native targets, TWH8
roundtrip, 0/65/100% normal interpolation,
device-specific lighting captures, cross-compiler Q16
rounding or GPU performance. All are owned by
the dedicated testing agent; the feature source
has been committed but testing is **NOT RUN**.


## Stage 22 — neighbor-aware seam normals and live Horizon diagnostics

This section describes the **current** source contract for
`feature/terraweave` (PR #24); earlier TWH6/TWH7 descriptions in this
file are historical. The current `.twh` archive version is **TWH8**.
All Stage 22 fields below are CPU/runtime metadata; no new RHI buffers,
draw submissions, shader variants, ECS components or archive fields were
added. Builds/CTest and Metal/GL/Vulkan screenshots have **NOT RUN**
under this feature stage.

### Seam sampling across resident authored terrain tiles

`AuthoredWorldHeightSnapshot::continuousSlopeQ16(xQ16cm,zQ16cm,
radiusCm=200,toleranceCm=1)` returns
`std::optional<std::pair<int32_t,int32_t>>` with signed X/Z slopes in
**permille** (`-dY/dX`, `-dY/dZ`), or `nullopt` if a neighboring tile
is unavailable or the crossed tile-edge heights disagree. Input X/Z is
signed **Q16 world centimetres** (multiply whole centimetres by 65536).
The bounded stencil radius accepts 1–4096cm; discontinuity tolerance is
0–100cm (default 1cm). The snapshot probes the original authored
two-triangle heightfields, including negative world tile coordinates,
and never changes collision or authoritative terrain.

`HorizonSurfaceSources::nearGroundContinuousSlopeAtQ16Cm` is an optional
CPU-bake callback supplied by `WorldScene` from the same immutable
`AuthoredWorldHeightSnapshot` used for authored near-ground heights.
Native projects may provide this callback alongside
`nearGroundHeightAtQ16Cm`; omit it to keep the existing local normal
algorithm. During `populateHorizonNearSeamSamples`, the worker prefers
a continuous neighbor-aware slope for each unique Q16 clipped perimeter
point. When that query returns `nullopt` it **falls back** to the
previous tile-local +/-200cm derivative when that data is available.
If neither yields a valid normal, `nearNormalValid` remains false and
the existing area-weighted clipped-face normal path is used.

This affects **render shading only**. The Stage 20 all-or-none
`maxNearWeldSlopePermille` height-weld guard, near X/Z clipping,
TWH8 `NearSeamSample` layout and existing atlas/VB/full model patch
selection remain in effect. The normal stencil must not smooth a
genuine cliff across independently authored tiles.

### Reading live region state

After updating the scene on its owning thread, a native host/editor can
read `TerraWeaveHorizonScene::runtimeStats()`:

```cpp
const auto h = horizon.runtimeStats();
if (h.requestedRegionPending) {
    // A requested region is not yet resolved at the current source revision.
    // h.preparing is a separate worker status; "pending" != "CPU is running".
}
if (h.activeRegionResolved && h.activeRegionEmpty) {
    // Accepted bake for the active cell, intentionally no drawable GPU model.
}
if (h.staleProxy) {
    // Active resolved cell is from an older authored source revision.
}
if (h.nearNormalProvenanceAvailable) {
    // Counts are unique sampled seam points, NOT vertices or draw calls.
    auto neighbor = h.continuousNearNormalSamples;
    auto local = h.localFallbackNearNormalSamples;
    (void)neighbor; (void)local;
}
// Only inspect requestedCellX/Z if requestedRegionPresent is true.
// Only inspect activeCellX/Z if activeRegionResolved is true.
```

| Field | Contract |
| --- | --- |
| `sourceRevision` / `activeRevision` | Current immutable source revision / accepted active-region revision. |
| `requestedRegionPresent`, `requestedCellX/Z` | A camera-selected target exists; coordinates are **middle-tile grid indices**, not centimetres; camera hysteresis applies. |
| `activeRegionResolved`, `activeCellX/Z` | A region result was accepted (including an intentionally empty result); active coordinates are defined only when resolved. |
| `activeRegionPresent` | Accepted region currently has a drawable GPU model; **false does not imply pending**. |
| `activeRegionEmpty` | A resolved region intentionally has no GPU model (e.g. empty ocean Horizon). |
| `requestedRegionPending` | Target exists and is unresolved, differs from active cell, or active revision is stale. An empty resolved same-cell/same-revision region is **not pending**. |
| `staleProxy` | Active resolved region revision differs from `sourceRevision`; it can also describe a stale **empty** region. Wrong-camera-cell status without source edits belongs to `requestedRegionPending`, not this flag. |
| `preparing` / `discarded` | Async stream state; check independently of `requestedRegionPending`. |
| `continuousNearNormalSamples` | Unique Q16 near-seam samples with valid neighbor-aware slopes in the CPU bake. |
| `localFallbackNearNormalSamples` | Unique Q16 near-seam samples using the local-height derivative because neighbor-aware results were unavailable/invalid. |
| `nearNormalProvenanceAvailable` | Counts above have a known CPU sampling origin. When false, treat both counts as **unknown**, even if numerically zero. |
| `matchedAuthoredNearNormals` | Number of **render vertices** matched to an authored near-source normal; do not add to sample counts. |

**Archive behavior:** TWH8 stores the near-seam height, slope and
normal-valid bit but **not** the provenance counters or availability
flag. A disk archive cache hit can legitimately report
`nearNormalProvenanceAvailable=false` and zero provenance counts even
when stored normals are valid; this is **unknown**, not measured zero.
A fresh bake can report valid zero counts when no seam sample qualifies.
All-or-none weld fallback may discard samples and zero counters while
the CPU sampling-path availability flag remains true.

**Lifecycle:** `shutdown(device)` clears current region, requested
region, geometry metadata and diagnostic counters. GPU-cache
retirement/restoration carries provenance counts and their availability.
A source revision update can temporarily keep an older GPU proxy
until an updated bake succeeds. `activeRegionPresent` only reports
model existence, while `requestedRegionPending` reports unresolved
selection. A large teleport may be hidden by the separate camera
coverage check in `submit()`; this is not encoded by `staleProxy`.

**Validation delegated to another agent:** compile with C++20 on target
platforms; check adjacent positive/negative tiles, sloped and
discontinuous boundaries, missing neighbors, Q16 corner rounding,
all-or-none slope fallback, empty region and shutdown, GPU LRU restore,
source revision invalidation, archive hit/miss, and atlas/vertex patch
safety. Collect actual GPU captures and frame-time/overdraw measures
before claiming production-ready seams or negligible GPU cost.

## Stage 24 — sparse deterministic forest regrowth and presentation ticks

**Status:** native C++ source committed, runtime/compiler/CTest/GPU verification intentionally deferred until the separate testing phase. This feature extends the already-existing persisted `ForestTreeIdentity` removals; it does **not** introduce per-tree ECS entities, a new shader, another RHI backend, a new save file format, or a second GPU forest model.

A `TerraWeaveForestScene` now supports `ForestRegrowthSchedule`, a sparse value type:

```cpp
struct ForestRegrowthSchedule {
    ForestTreeIdentity tree;        // {key, speciesId}: stable across chunks
    uint64_t regrowAtTick;          // hidden before this tick
    uint64_t matureAtTick;          // full generator scale at/after this tick
    uint32_t initialScalePermille;  // 1..1000, defaults to 150
};
```

Use the **game's own deterministic simulation tick** — not wall-clock time — with `setPresentationTick(uint64_t)`. `scheduleRegrowth(schedule)` removes the tree from the persistent `removedTrees` set and hides it before `regrowAtTick`. For ticks between appearance and maturity, `visibleBatches()` modifies the **presentation copy** of `FoliageInstance::scalePermille` using bounded integer interpolation, from `initialScalePermille` to 1000 permille of its original generated scale. `matureAtTick == regrowAtTick` means instant restoration. Nothing changes in the authoritative scatter keys, heights, seed, collision, navmesh or GPU prototype models. On later camera movement/re-streaming the same identity gets the same scale for the same tick.

```cpp
#include "engine/world/TerraWeaveForestScene.h"
using namespace aiverse::terraweave;

// Use an existing configured scene and a stable ID obtained from foliage.
ForestTreeIdentity oak{stableTreeKey, 1001};
forest.removeTree(oak); // optional gameplay-driven harvest
forest.scheduleRegrowth({oak, 6000, 12000, 120});
forest.setPresentationTick(gameplayTick);
const auto batches = forest.visibleBatches();
// Host still calls uploadModels()/submit() on the renderer owner thread.
// Persist forest.removedTrees() and forest.regrowthSchedules() in game saves.
```

Here `stableTreeKey` and `gameplayTick` are native-game values, not engine globals. `setRegrowthSchedules(...)` atomically validates and replaces a saved sorted list; `regrowthSchedules()` exposes a stable sorted snapshot for persistence/replication. `cancelRegrowth(id)` cancels a schedule without automatically hiding or removing the tree. `removeTree(id)` cancels any pending regrowth and hides the tree; `restoreTree(id)` clears both hidden and scheduled states; `setRemovedTrees(...)` clears any overlapping regrowth schedules so explicitly removed trees remain hidden. Use the caller's save/network authority for lifecycle decisions.

The implementation limits regrowth to at most **1,000,000** sparse records, rejects zero species, duplicate replacement keys, `initialScalePermille` outside 1..1000, backward schedules, and maturity intervals greater than **1e12 ticks**. These bounds keep `elapsed * (1000 - minScale)` within signed/unsigned 64-bit integer range without platform-specific 128-bit arithmetic. Even with asynchronous forest generation, tick changes are evaluated at presentation time and do **not** cancel/rebuild chunk workers.

`ForestSceneStats` exposes the additional trailing fields `scheduledRegrowth` and `presentationTick`, without breaking earlier aggregate uses of its existing members.

### Access from WorldScene

An opt-in `world::WorldScene` exposes
`terraweave::TerraWeaveForestScene* forestScene() noexcept` (and the const overload). It returns null if the world manifest does not enable `terraweaveForest`. Native games can include both `engine/world/WorldScene.h` and `engine/world/TerraWeaveForestScene.h` and feed game time or saved lifecycle records directly without any genre-specific API:

```cpp
if (auto* forest = worldScene.forestScene()) {
    forest->setPresentationTick(gameplayTick);
    forest->scheduleRegrowth({oak, 6000, 12000, 120});
}
```

**Ownership and limitations:** this is *cosmetic presentation* only. An authoritative game layer is responsible for deciding when harvests occur and for modifying collision/navmesh/resources, save versioning, replay/network replication and any wider ecosystem simulation. Harvesting a single tree does not yet punch an individual hole in the **far Horizon HLOD cluster silhouettes**; coordinate harvested-area masks with Horizon in a later stage to avoid near/far inconsistencies. The implementation has not been built or visually tested in this feature-first phase.

## Stage 25 — runtime forest clearings shared by near Flora and Horizon HLOD

**Source implemented, tests deferred** at user request. A native `world::WorldScene` now exposes:

```cpp
void setVegetationClearings(std::vector<terraweave::RectCm> clearings);
const std::vector<terraweave::RectCm>& vegetationClearings() const;
```

Clearings are **signed world-centimetre half-open X/Z rectangles**, not RTS map-grid units. A game can use them to clear whole construction areas, timber harvest zones, or landscape patches (unlike the individual per-tree `ForestRegrowthSchedule` in Stage 24). All input rectangles must be nonempty and inside TerraWeave's `+/-kWorldLimitCm` extent; at most 4096 are accepted. The setter normalizes order and removes exact duplicates; passing an identical canonical set is a no-op. The returned list is suitable for caller-owned save/replication; the engine does **not** create authoritative gameplay events, collision edits, or a new save schema.

```cpp
using aiverse::terraweave::RectCm;
worldScene.setVegetationClearings({
    RectCm{siteX0cm,siteZ0cm,siteX1cm,siteZ1cm}
});
// Call worldScene.update(device, observers, radius) from the owner thread.
// Later: worldScene.setVegetationClearings({}) to restore the area.
```

The `site*cm` values in this example are game-provided coordinates.

The next `WorldScene::update` captures one immutable `FoliageExclusionMask` containing both active clearing rectangles and resident authored object/terrain footprints. The **same** snapshot is shared with:

- `TerraWeaveForestScene::setLandscapeSources` for near Flora placement. Its bounded async patches are invalidated by a single source revision and rebuilt safely after their captured callbacks are released.
- `HorizonSurfaceSources::allowsCanopyAt` for distant forest silhouette atlas sampling. It suppresses crowns at masked positions during the normal bounded CPU region bake and reuses the existing Horizon async/GPU update path. Existing terrain mesh and near/far height welding remain unchanged.

Clearing updates mark **both previous and new** rectangles as edited, so moving or removing a clearing restores vegetation in the old location rather than preserving stale distant silhouettes. Multiple setter calls before a scene update retain only the first pre-update clearing state and the final replacement; intermediate states never become published. Large edited lists take the Horizon full-rebake fallback rather than unbounded incremental work. Unlike per-tree regrowth, this area mask **does affect** the far HLOD canopy, while not adding a new shader, draw pass, GPU buffer layout or component type.

**Caveats:** The mask suppresses individual CPU canopy samples and may leave empty far cluster cards if all samples in that cluster are excluded; this is a rendering efficiency follow-up. An authored forest/city streaming update can also trigger shared mask republishing, independent of gameplay clearings. No shape rotation, polygon harvesting, sector replication, authoritative resource/collision modifications, or weather/season coupling is included yet. Runtime re-bake, cross-boundary exclusions, visual transitions and performance budgets have **NOT** been verified; another agent owns those tests.

## Stage 26 — resident tree spatial queries for native gameplay

TerraWeave now supports *presentation-resident* tree selection without forcing every decorative tree into ECS:

```cpp
// TerraWeaveForestScene forest already configured / streamed.
const auto closest=forest.nearestTreeAt(playerXcm,playerZcm,500);
if(closest) {
    forest.removeTree({closest->key,closest->speciesId});
    // The authoritative game must still update lumber, collider and save.
}

const auto trees=forest.treesInRect(
    {plotMinXcm,plotMinZcm,plotMaxXcm,plotMaxZcm},1024);
```

The coordinates above are game-provided signed **world centimetres**.

- `nearestTreeAt(xCm,zCm,radiusCm=500) -> optional<FoliageInstance>`: searches resident CPU scatter patches, using an AABB precheck and circle distance. Radius must be in 1..10,000,000cm; out-of-world centres are rejected. Equal-distance ties select the lowest stable `ForestTreeIdentity{speciesId,key}`, independently of map iteration order. It is not camera/frustum/LOD dependent.
- `treesInRect(RectCm,size_t maxResults=1024) -> vector<FoliageInstance>`: checks only resident patches intersecting a nonempty half-open rectangle, excludes removed/pending-regrowth trees and sorts results by species ID, key then X/Z. The result budget is 1..100,000; overflow throws `std::length_error` instead of silently returning an arbitrary truncated set.
- Both use the exact same `presentationInstance` lifecycle filter as `visibleBatches()`; harvested trees are invisible to queries; a scheduled tree is hidden until its `regrowAtTick`, and returned growing trees carry the current visual scale. Queries never re-scatter terrain, modify the GPU, sample external game physics or use a platform clock.
- Results cover only **loaded forest patches**. An unloaded tree or a tree outside the configured residency area cannot be found through this presentation-only API; collision, harvesting at long range and persistent jobs remain responsibilities of the authoritative gameplay/world systems.

The generic `WorldScene::forestScene()` accessor from Stage 24 gives native projects access to these methods for opt-in worlds. For huge worlds, if frequent queries become hot, consider an optional per-chunk spatial index after profiling; no claimed performance benchmarks or compiler/CTest/GPU validation have been performed in this features-first stage.

## Stage 27 — shared canopy phenology for Flora and Horizon

**Source committed; compilation, CTest, screenshots and GPU benchmarks intentionally deferred.**
The engine now accepts a **host-owned seasonal appearance state**, not a built-in calendar or weather simulation. PHYSIS and game-specific time-of-year systems can supply the same small POD via a genre-independent bridge:

```cpp
#include "engine/world/TerraWeavePhenology.h"
// A non-default fall/autumn profile; choose values in the host.
aiverse::terraweave::CanopyPhenology autumn;
autumn.broadleafCoverPermille = 480;
autumn.evergreenCoverPermille = 960;
autumn.tintRPermille = 1350;
autumn.tintGPermille = 810;
autumn.tintBPermille = 580;
worldScene.setVegetationPhenology(autumn);
```

The above values are an **example**, not a baked climate forecast. Each cover value is in 0..1000; RGB tint multipliers are integer permille 0..2000; 1000 = identity. `validateCanopyPhenology`, `canopyCoverPermille` and `tintCanopyChannel` are pure C++20 helpers in `engine/world/TerraWeavePhenology.h`. They do not include PHYSIS, RTS, FPS, GPU or platform timing. The caller owns climate, seasonal chronology, persistence and replication.

The opt-in `world::WorldScene::setVegetationPhenology(state)` stores the canonical policy, forwards it to `TerraWeaveForestScene::setPhenology`, and queues a Horizon source revision for the next `WorldScene::update`. Unchanged state is a no-op. Near Flora updates shared *per-species material overrides* in `ArborForestAdapter` without rescatters or async worker cancellation. Conifer prototypes use the evergreen coverage; other shapes use broadleaf coverage. Bark material stays intact, while the leaf material receives RGB tint and optional blended alpha when canopy cover is below full. Overrides must not be changed after submitting draws to Renderer3D but before that frame is rendered, because `ModelDraw::materials` stores a borrowed pointer.

For near azimuth impostors, the same policy updates the **existing shared per-view billboard material overrides**; the source atlas includes bark and foliage on one quad, so coverage alpha fades the entire billboard rather than just its leaves. This is a known LOD-parity limitation, **not** a leaf-only billboard mask or per-pixel seasonal foliage solution. It adds no atlas, mesh or extra draw pass by design. Partially transparent canopy may incur standard blended-render overdraw; benchmark in the later test phase.

Far Horizon uses the same immutable `CanopyPhenology` captured in `HorizonSurfaceSources::phenology`: each procedural broadleaf/conifer canopy crown is **deterministically thinned** by the corresponding cover value, and resulting leaf RGB pixels are tinted before writing the existing shared RGBA atlas. Occasional exposed trunks remain untouched. Heightfields, terrain geometry, physics, material count, triangle budgets and near-seam welding do not change. `HorizonSurfaceSources` appends the policy to its existing optional callbacks; native projects that do not supply it retain default summer-like output. Seasonal changes request a bounded worker re-bake; old GPU proxy stays visible until replacement succeeds.

**TWH8 cache safety:** the current static archive format/fingerprint does *not* serialize the season. The Horizon worker therefore deliberately **disables static disk atlas read/write while a non-default phenology is active**, avoiding false summer archive hits. Default state retains the existing TWH8 path. No archive version/layout migration occurs. Native projects wanting prebaked multiple seasons would need an explicit future archive key and asset naming policy.

**Remaining limitations:** Per-tree `ForestRegrowthSchedule` still controls individual growth scale, independent from seasonal leaf cover. Spring/autumn transitions are immediate material/atlas policy changes rather than continuous blended dual-atlas animations; near impostor cards fade trunk as well as canopy; distant HLOD crown types are procedural/bio-climate approximations, not identities of actual nearby species. Individual harvested trees remain near-only until far HLOD supports individual tree metadata, while rectangular clearings are already shared across near/far. This stage is **features first** — no compiler, CTest, GPU screenshot or performance pass was observed.

## Stage 28 — genre-neutral wind presentation for Arbor forest

**Code committed, tests intentionally deferred.** The wind motion API is a presentation-only adapter: games or PHYSIS supply wind vectors and simulation ticks; TerraWeave does not run its own weather, physics forces, collision, ECS or platform-time timers.

The new `engine/world/TerraWeaveWind.h` defines:

```cpp
struct ForestWindState {
    int32_t xCmPerSec = 0, zCmPerSec = 0;
    uint32_t gustPermille = 350;
    uint32_t cycleTicks = 120;
    uint32_t maxLeanMilliDegrees = 1500;
};
ForestLeanMilliDegrees forestTreeLean(
    const ForestWindState& state, uint64_t tick, uint64_t stableTreeKey);
```

Both wind components use signed **world X/Z centimetres per second**. A state is valid when the horizontal magnitude is no more than 5000cm/s (50m/s), `gustPermille` in 0..1000, `cycleTicks` in 8..4096 and `maxLeanMilliDegrees` in 0..3000. The helper computes a bounded integer triangle-wave phase using `mix64(stableTreeKey)`; each tree has a stable offset instead of the entire forest tilting in lockstep. The zero-wind default preserves identical model transforms.

```cpp
aiverse::terraweave::ForestWindState wind;
wind.xCmPerSec = 900;   // 9 m/s toward +X
wind.zCmPerSec = -300;
worldScene.setVegetationWind(wind);
worldScene.setVegetationPresentationTick(gameplayTick);
```

`WorldScene` forwards those values to its optional `TerraWeaveForestScene`; native projects using the forest independently may call `forest.setWind(wind)` and `forest.setPresentationTick(tick)` directly. For an opt-in renderer, the `ArborForestAdapter` computes a small world-space lean about the base of the model, before the tree's local yaw, then uses existing `Renderer3D::drawModel`. Stable tree identity, origin-rebased centimetres, per-species LOD, shared meshes and billboard atlas remain unchanged. Per-tree phases and angles are computed on the presentation side; changes to wind/tick do **not** rescatters, restart workers, or force Horizon atlas uploads.

**Important limitations:** this is a low-cost root/whole-tree sway approximation, not branch/leaf skinning or vertex-shader wind deformation. Distant Horizon HLOD remains static (moving 12km forest atlases per tick would be excessive); the very far treeline therefore does not individually sway. Billboard impostors lean as one quad. The final float transform is cosmetic and not guaranteed bit-for-bit identical between rendering backends; authoritative physics or harvest logic must never use it. PHYSIS can map its velocity field into this POD from the owning game host without linking the TerraWeave engine to PHYSIS.

**Performance and correctness:** no new model geometry or draw calls are introduced; however CPU transform math is performed per submitted tree when wind is nonzero, so actual frame cost and visual quality still require the separately scheduled benchmarking and GPU test phase. A future optional vertex deformation shader could preserve stable roots while bending crowns more naturally. Neither compiler nor GPU/CTest has been run during this features-first stage.

## Stage 29 — bark/foliage-separated multi-azimuth impostor atlas

**Code committed; no build/CTest/GPU validation yet** per the feature-first instruction. This stage replaces the combined all-season RGBA impostor image produced by `makeArborImpostorAtlas`. A near Arbor tree and its billboard can now lose leaves while retaining an intact bark silhouette.

### Atlas and render layout

`engine/render/TerraWeaveImpostorAtlas.h` generates **one GPU RGBA8 texture per species** with two horizontal sets of equal-sized view tiles:

- The first `azimuthViews` tiles contain bark/trunk/branch strokes only.
- The following `azimuthViews` tiles contain foliage/leaf-cluster strokes only.

Both sets are projected from the **same stable TreeSkeleton** for each view angle and retain per-tile gutters. The atlas is `2 * tile.width * azimuthViews` pixels wide by `tile.height` high (maximum width 8192, hence previously accepted ultra-wide combinations may now be rejected). Default 8-view, 128x256 atlas becomes 2048x256 RGBA8 instead of 1024x256, doubling raw atlas bytes but still using ONE texture object.

The common `render::Model` contains one node, one mesh, **8 vertices, 12 indices and two alpha-masked primitives** (`Primitive.material=0` bark, `=1` foliage). The foliage quad is offset +2cm toward its camera-facing model +Z to avoid depth fighting. Both material slots reference the same `baseTex=0` with independent per-view UV transforms; `ArborImpostorAtlas::viewMaterials` is still indexed by azimuth but now contains exactly **two materials** for each view, in bark/foliage order. No new shader, RHI backend, ECS components, model per instance, or separate atlas upload pass.

### Seasonal policy

`ArborForestAdapter::rebuildPhenologyMaterials()` copies per-species near and impostor material variants only on policy changes. Material 0 (bark) remains untouched; only material 1 (foliage) gets tint and alpha. `canopyCoverPermille` chooses evergreen versus broadleaf by the registered profile. At **zero** canopy cover, existing `ModelDraw::primitiveMaterialFilter=0` submits bark primitives only for near BranchLOD and impostors, skipping invisible leaf draws. At partial cover, foliage uses the existing alpha-blend material policy; bark remains alpha-masked and fully opaque. Existing wind lean and root-anchored camera-facing orientation are shared by both impostor layers.

**Cost/quality caveats:** full-canopy impostors now have two primitives instead of one, so draw submissions/material batching and texture bandwidth can increase. Cutout bark and blended foliage may differ slightly in ordering/coverage from the previous single-layer approximation. Far Horizon still uses its separate aggregated silhouette atlas and is unaffected by this near-impostor format. Partial transparency is a simplified seasonal representation, not deterministic individual leaf shedding; GPU/backend captures are needed to confirm blend order, shadow behavior, LOD transition quality and precise visual parity.

**Backward compatibility:** `makeArborBillboardModel` (the standalone *single-view* utility) retains its original one-quad format. Only the multi-azimuth `makeArborImpostorAtlas` output changes shape; any native consumer assuming 4 vertices, 6 indices, single material or original atlas width must migrate. Existing `tests/terraweave_model_test.cpp` has expectations for that old layout and is intentionally **not updated or executed** in this features-first phase. The later testing agent must revise these assertions to the two-layer contract, then run GCC/Clang CTest, GPU screenshot and benchmark checks. No TWH8 distant-Horizon disk archive version change is needed because its format is unrelated to this near-impostor atlas.

## Stage 30 — bounded hierarchical BranchLOD wind motion

**Functional source committed, independent testing deferred.** This stage extends Stage 28's PHYSIS-neutral root lean into more organic, articulated wind motion for **near Arbor BranchLOD** trees, without reskinning meshes or requiring a new render shader.

`ForestWindState` appends `branchFlexPermille` (default 600, bounded 0..1000); 0 preserves the exact previous low-cost whole-tree tilt. `ArborForestAdapter` regenerates the already-deterministic high-detail skeleton ONCE at prototype registration to cache per-branch root pivots (metres relative to original tree root). It then uses the existing branch node hierarchy and Renderer3D's `ModelDraw::nodeWorld` rigid-model transform path to compute a per-visible-tree vector of cumulative model-space rotations around each branch's own start pivot. The trunk node itself remains at its original pose while global `ModelDraw::world` supplies the original whole-tree sway. Children inherit parent movement and have a stable per-node phase derived from the tree key, species and branch index.

No tree ECS entities, animation clips, skinned matrices, GPU mesh mutation, shader variants, atlas rebuilds or extra draw passes are added. The original Arbor mesh geometry, `TreeProfile`, branch LOD simplification and canonical procedural tree seeds remain unchanged. View-distance tiers attenuate additional branch sway (High 100%, Medium 65%, Low 25%, Impostor 0%) to limit near-to-billboard discontinuity. Distant statistical Horizon remains static as previously documented; it is intentionally NOT rebuilt per wind tick.

The private temporary pose arena is a `std::deque<std::vector<render::mat4>>` so references in `ModelDraw::nodeWorld` remain stable during a forEachDraw call; the arena is cleared at the next forEachDraw. `Renderer3D::drawModel` reads node transforms synchronously and copies them into render items. **Native callbacks collecting ModelDraws may only use their nodeWorld pointers until the next forEachDraw invocation**, and must not use the forest adapter concurrently on multiple threads. `ArborForestAdapter::setBranchPoseBudgetBytes(bytes)` (forwarded through `TerraWeaveForestScene`) accepts 0..8MiB and defaults to **4MiB**. A draw whose node matrix snapshot would exceed the remaining budget uses root-only sway instead. The budget is for CPU temporary matrices, not new GPU residency.

Example (on the owning host/update thread):

```cpp
if(auto* forest = worldScene.forestScene()) {
    forest->setBranchPoseBudgetBytes(4u * 1024u * 1024u);
}
aiverse::terraweave::ForestWindState wind;
wind.xCmPerSec=700;
wind.gustPermille=450;
wind.branchFlexPermille=850; // optional, 0 = whole-tree-only
worldScene.setVegetationWind(wind);
worldScene.setVegetationPresentationTick(gameplayTick);
```

A real PHYSIS solver may supply the world X/Z velocity components; this bridge does not require PHYSIS headers, does not alter collision/navmesh/authoritative physics and does not put wind in an ECS component. `WorldScene` continues forwarding the same validated wind state and tick, so native code uses the existing public methods.

**Limitations/QA debt:** Branches are rigidly rotated at their joints rather than continuously bent as a skinned elastic curve. At high winds thin joints can show gaps. The source uses integer hashed wind phases but converts degrees to float matrices in renderer presentation; bitwise cross-GPU determinism is not promised. Very dense near forests can hit the CPU pose budget and switch to root-only motion. Source `forEachDraw` collectors must respect pose borrowing lifetime. No CMake, CTest, GPU screenshot, per-frame CPU benchmark or cross-platform rendering check has been executed. Dedicated test agent should validate hierarchy/pivot continuity, season + wind, LOD transitions, budget fallback, material/draw pointers and native PHYSIS integration before marking production ready.

## Stage 31 — shared GPU crown wind for near Arbor and Horizon

**Source implementation committed in `feature/terraweave`; independent compilation/CTest/GPU QA NOT RUN.** Stage 31 extends Stage 28/30's existing root and rigid-branch sway with an optional GPU vertex-layer. It uses **Renderer3D's existing Material Shader Graph `vertexOffset` hooks** for GLSL and MSL, with no compute jobs, new render backend, separate shaders pipeline, GPU vertex buffers, extra atlas or re-bake-per-tick.

### First-party vertex graph and instance payload

The new `engine/render/TerraWeaveVertexWind.h` provides `canopyWindGraph()`, one immutable engine-authored `render::graph::Program` containing both GLSL and Metal vertex code, plus `canopyWindVertexParameters(wind,tick,yaw,strength)`. The graph preserves the fragment's original material/shading (`outputs["color"]="sg_input"`) and emits bounded `vertexOffset`: wind vector * squared 0..1 height weight * low-frequency, spatially staggered sine wave with bounded higher-frequency gust. Crown roots remain fixed. It uses **`I.anim`**, already present in rigid model instance uniform U0, instead of allocating a new uniform buffer. The phase derives from the host's `setVegetationPresentationTick` (tick modulo `ForestWindState::cycleTicks`), **not** uncontrolled wall-clock shader time. X/Z velocities come from the same PHYSIS-neutral `ForestWindState` as CPU root sway; near instance yaw is inverted before passing the local vector, so wind remains oriented in world space.

This is a trusted engine-generated program, not a user-authored shader source injection API. `canopyWindGraph()` returns a shared, immutable program to avoid creating a new shader-pipeline identity every tick or every tree. A zero wind vector or zero branch-flex/amplitude leaves the feature disabled and preserves existing materials.

`render::ModelDraw` appends generic `useVertexParameters`, `vertexParameters` (float4) and `maxVertexOffsetMetres`. `Renderer3D::drawModel` places those four parameters in U0 `Inst.anim` **only for eligible rigid, non-baked, non-lightmapped instances** whose material has a graph. Baked animations and lightmap UV transforms retain priority; no U0 binary layout change was made. Other graph-using native projects may reuse the generic contract, but callers must not request the same U0 payload for baked skin/lightmapped models. Unknown/unbounded shader vertex offsets retain the renderer's conservative no-cull behavior; finite positive explicit bounds allow normal model/node frustum rejection and per-node LOD, with expanded geometry bounds. The TerraWeave graph sets a conservative **0.65m local-space maximum offset**.

### Near Arbor

`makeArborBranchLodModel` encodes each foliage vertex's source height relative to the procedural tree skeleton as a normalized RGBA8 alpha **wind weight**. Foliage `Material::vertexColors=false` keeps this auxiliary weight from accidentally tinting/cutting out the leaf texture. Bark geometry/materials remain static under this vertex shader; wind still rotates the tree root and optional rigid branch nodes via the earlier Stage 28–30 paths.

`ArborForestAdapter` applies the single graph only to foliage material slot 1 in shared per-species and per-azimuth override vectors. The shared program is attached when nonzero validated wind, `branchFlexPermille>0` and `maxLeanMilliDegrees>0`; changing numerical wind/tick values updates per-instance parameters without creating another graph program. GPU flutter amplitude diminishes by LOD (High 100%, Medium 75%, Low 50%, Impostor 35%). If foliage cover is 0 during winter, the old bark-only primitive filter remains active; no hidden leaf vertex pass is submitted. Stage 29's split impostor geometry encodes zero weight at its lower foliage vertices and full weight at the top, so its root is anchored.

### Far Horizon

`makeHorizonModel` keeps **one combined model**, the original terrain primitive and shared forest-cluster primitive/atlas. Forest billboard vertex alpha now encodes a weight of 0 at the base and 1 at the canopy top; the forest material ignores vertex alpha in fragment shading so it cannot accidentally erase half the forest. Only its canopy material slot 1 opts into the same graph. `TerraWeaveHorizonScene::setWind` and `setPresentationTick` accept the same POD/tick as near Arbor. In `submit`, current and crossfading forest-region materials receive the shared graph and a restrained **30% GPU wind strength**. The terrain material and near/far seam vertices are unchanged. Static TWH8 archives and partial atlas/VB uploads remain untouched; the per-frame motion is a GPU vertex offset, not an atlas redraw. Source snapshot revisions are **not** advanced merely because the wind changed.

`WorldScene::setVegetationWind` and `setVegetationPresentationTick` forward the policy/time to **both** near ForestScene and far HorizonScene, preserving null opt-in module behavior.

**API example:**

```cpp
aiverse::terraweave::ForestWindState wind;
wind.xCmPerSec = 650;
wind.zCmPerSec = -250;
wind.branchFlexPermille = 700; // 0 restores root-only sway
worldScene.setVegetationWind(wind);
worldScene.setVegetationPresentationTick(gameplayTick);
```

**Boundaries/limitations:** This is *procedural render-only* crown flutter, not physical leaf/branch simulation; wind affects shader vertex positions but not collision, navmesh, authoritative tree positions or gameplay ECS. The effect intentionally retains existing root/branch transforms and only adds a small extra vertex displacement. Spatial phases are deterministic functions of the host tick and model-space/rebased world coordinates but GPU sine/floating math is not promised bitwise-identical across devices. Bounded graph models now continue to participate in node/LOD culling; confirm conservativeness with high-wind screenshots. Raw `ModelDraw::vertexParameters` is incompatible with concurrently using the packed `Inst.anim` slot for baked clips or lightmaps. The far forest motion is whole-card canopy shear, not individually simulated distant branches. Renderer pipeline/material changes, shadows, PHYSIS integration, large-area transition and CPU/GPU timings are **not yet validated**.

**Tests intentionally deferred:** The independent testing agent must update Stage 29 impostor expectations, compile GCC/Clang and backend shader pipelines, validate U0 payload/vertex colors on Metal, Vulkan and GL, test 0 and maximum wind, season + GPU flutter, crossfade compatibility, vertex bounds and culling, and measure material pipeline counts/frame cost. Do not treat source commits as passing tests.

## Stage 32 — spatial wind grid and near-to-Horizon transition

**Feature source committed, build and tests deferred** per the project owner's feature-first priority.

Native games/PHYSIS can publish one bounded, **immutable spatial wind snapshot** for near Arbor trees and far Horizon, without linking PHYSIS to the forest renderer. The new `engine/world/TerraWeaveWindField.h` defines `ForestWindField` as a regular X/Z lattice of `ForestWindSample {xCmPerSec,zCmPerSec,gustPermille}`, all in signed **world centimetres** and cm/sec. The caller owns sampling from its actual PHYSIS wind/weather field; TerraWeave performs only the render-side interpolation:

```cpp
using namespace aiverse::terraweave;
auto mutableField=std::make_shared<ForestWindField>();
mutableField->originXcm=-12800;
mutableField->originZcm=-12800;
mutableField->spacingCm=6400;
mutableField->columns=5;
mutableField->rows=5;
mutableField->samples.assign(25,{800,0,400});
// Optionally fill specific nodes with different PHYSIS velocities.
mutableField->samples[12]={-100,550,750};
mutableField->validate(); // reject invalid geometry/oversized winds
std::shared_ptr<const ForestWindField> published=mutableField;
worldScene.setVegetationWindField(published);
worldScene.setVegetationPresentationTick(gameplayTick);
// Publish a NEW immutable object when the weather field changes:
worldScene.setVegetationWindField({});
```

**Do not mutate** a field after publishing it, even if a mutable shared pointer alias still exists. Create and publish a new snapshot for each update. `WorldScene::vegetationWindField()` returns the active const shared snapshot (or null). Native projects using the forest/Horizon directly can call their `setWindField(shared_ptr<const ForestWindField>)` methods. The uniform fallback still comes from the existing `ForestWindState` (`setVegetationWind`) and supplies the authoritative wind animation period, strength/flex, and limits. The grid overrides only local X/Z wind velocities and gust intensity.

### Sampling and transition semantics

- Limits: either empty/default field or 2..64 nodes per axis, at most **4096** node samples; spacing 100..10,000,000cm; all node coordinates within `+/-kWorldLimitCm`. Each node is validated using `validateForestWind` (horizontal magnitude <=5000cm/s; gust 0..1000). Invalid input fails before replacing the active world snapshot.
- `sampleForestWindField(field,xCm,zCm,fallback)` uses Q16 bilinear interpolation in absolute signed world centimetres, including negative positions. Outside the lattice it returns `fallback`; a half-node-spacing **interior feather** blends border values back to the fallback to prevent sudden direction/speed jumps at the field edge. No shader texture, RHI buffer, global mutable state or wall-time dependency.
- `blendForestWind(local,macro,macroPermille)` is a bounded integer interpolation of velocity and gust. Branch flex, period and lean maximum remain those of the fallback source.
- The near `ArborForestAdapter` samples the immutable field at **each resident tree's stable world position**. Its geometric root lean, hierarchical CPU branch poses, and Stage 31 GPU leaf displacement all use that same effective per-tree wind. For consistency near distant render tiers, wind is blended toward the **observer-position macro sample**, continuously between `FloraView.mediumRangeCm` and `FloraView.cullRangeCm` (using a conservative X/Z Chebyshev distance and bounded permille interpolation). The forest scene configures these boundaries once; an unchanged/no-field setup retains its old execution path.
- The far `TerraWeaveHorizonScene` takes one observer-space macro wind sample in `submit`, shared by active and outgoing region draws during crossfade. This prevents a wind-vector discontinuity when the Horizon region changes and keeps HLOD geometry/atlas batched. It does **not** yet produce a separate PHYSIS wind vector for every statistical distant forest cluster. A later optional coarse GPU wind field or per-cluster baked vector feature can improve microvariation at kilometre scale if warranted.
- `WorldScene::setVegetationWindField` forwards the **same** `shared_ptr<const ForestWindField>` to its optional forest and Horizon modules, with no terrain worker invalidation or TWH8 archive changes. Repeated pointer identity is a no-op; passing null clears the field so only uniform wind remains.

**Performance/ownership:** the spatial field adds a handful of bounded CPU integer interpolations per **submitted near-tree**, not per ECS entity or terrain scatter candidate. Horizon performs one sample per submission rather than one per billboard cluster. Live weather updates need a new bounded field snapshot but do not allocate new vertex/texture buffers or recompile ShaderGraph programs. The sampler assumes the caller has validated the immutable field on publication; direct native callers MUST invoke `field.validate()` before sampling, rather than mutating the structure concurrently. Inputs from a high-resolution PHYSIS simulation should be downsampled by the host to this bounded presentation grid.

**Not verified:** Neither C++20 compiler/CMake/CTest nor GPU/PHYSIS integration, visual transition captures, pathological negative-boundary samples, or performance profiling was run at this feature-only stage. Remaining known test debt includes the Stage 29 two-primitive impostor migration and Stage 31 GLSL/Metal Shader Graph rendering.

## Stage 33 — smoothly varying far-Horizon forest-cluster wind

**Source code committed; build/CTest/real GPU checks are deferred.** The far Horizon no longer needs to use one visually identical wind vector for every forest card in a multi-kilometre region. Each baked cluster receives a smooth, signed-world-coordinate variation in direction, gust strength and phase **without adding draw calls, atlas textures, per-cluster ECS entities, or per-tick GPU uploads**.

### What is encoded per cluster

`engine/render/TerraWeaveVertexWind.h` adds `horizonSpatialWindVertexColor(worldXcm,worldZcm,terrainSeed,canopyTop)` and `horizonSpatialWindGraph()`. The former evaluates **three deterministic 2.4 km wavelength world-space noise channels** at a cluster's fixed X/Z centre and packs them into RGB8 in the already-existing `render::ModelVertex::color`. Alpha still holds the Stage 31 crown-weight: zero at the bottom edge, 255 at the top. Each pair of crossed silhouette cards for a cluster uses the **same RGB wind sample**, so the cards cannot diverge. The sampling is independent of observer cell and of how the Horizon region is streamed, including negative world coordinates and transitions between GPU-resident regions. No additional ModelVertex fields or static archive changes.

At runtime, the first-party **GLSL + Metal** `horizonSpatialWindGraph()` uses the same inherited `I.anim` instance data as the Stage 31 crown graph. One shared macro wind vector (from the existing `ForestWindField` sampled at the observer location, or the fallback uniform wind) is rotated by at most **30 degrees** per cluster, scaled between **0.75 and 1.25**, and given a coherent but cluster-dependent phase offset. The exact per-cluster coefficients are fixed by the baked vertex RGB metadata and never require new CPU atlas work when the wind tick changes. Roots stay fixed because alpha is still zero at the card bottom. Direction, amplitude and phase vary *smoothly* across neighbouring clusters in world space rather than jumping between camera tiles.

`makeHorizonModel` writes this metadata only to the distant forest vertices, not authored/coarse terrain. The far forest material already has `vertexColors=false`, ensuring these packed RGB8 values do not unexpectedly tint the foliage or modify opacity. No changes were made to near authored terrain, clipping, Q16 height welding, canopy atlas pixels or TWH8 serialized data.

### Native API and quality switch

```cpp
// Already supported PHYSIS/game wind setup:
worldScene.setVegetationWind(baseWind);
worldScene.setVegetationWindField(immutableSpatialWind);
worldScene.setVegetationPresentationTick(gameplayTick);

// New optional comparison/quality switch. True by default:
worldScene.setVegetationHorizonSpatialWind(true);

// Return to Stage 31's identical macro wind for every far card:
worldScene.setVegetationHorizonSpatialWind(false);
```

Standalone `TerraWeaveHorizonScene::setSpatialWindVariation(bool)` and `spatialWindVariation()` expose the same switch, while `WorldScene::vegetationHorizonSpatialWind()` returns the current choice. The setting is presentation-only and should be set on the render/update owner thread before calling `submit()`; toggling changes only the *shared ShaderGraph program selection*, not the mesh, atlas, CPU bake or source revision. First use of a variant may incur the renderer's existing pipeline warm-up cost, not shader recompilation every simulation tick. The default is enabled; if zero wind is configured, neither the old nor new wind graph is active.

**Important scope distinction:** The Stage 32 field is sampled exactly at the *near individual tree* and at the *Horizon observer*. Stage 33's independent **far cluster variation is procedural**, applied to that sampled macro vector. It is **not** a direct sample of the upstream PHYSIS field at every far cluster, and therefore is designed to make a 1–12 km treeline feel alive, not to simulate exact kilometre-scale weather boundaries or tornado trajectories. Exact per-cluster PHYSIS fields would require dynamic vertex metadata updates or an extra bounded GPU wind-grid resource, both outside this no-extra-resources mode.

**Bounded visual effect:** The base Horizon GPU crown displacement strength remains 30% of the near-tree graph; the per-cluster amplitude is at most 1.25 and the graph's sine/gust multiplier is bounded. The current `ModelDraw::maxVertexOffsetMetres = 0.65f` remains conservative for the validated host wind bounds. GPU cost is extra vertex shader ALU and shared shader variant, not an additional draw or material per cluster. Real performance, frustum bounds, visual appearance in moving cameras, GLSL/Metal shader compilation and accurate material batching **have not been tested yet**.

**Future work:** A dedicated QA agent must test this against Stage 31 default/no-wind parity, near-to-far velocity continuity, camera origin rebasing, negative coordinates, region crossfades, seasonal forest alpha cutout, Vulkan/GL/Metal render captures and batching. The Stage 29 impostor model test still assumes the old single-layer geometry and must be migrated during the separate testing phase.
