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
