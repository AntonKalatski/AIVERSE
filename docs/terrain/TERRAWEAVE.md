# TerraWeave — procedural terrain, biomes and vegetation (AIVERSE)

Status (2026-11-18): **Phase 1 core implemented**, renderer/editor integration pending.
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
