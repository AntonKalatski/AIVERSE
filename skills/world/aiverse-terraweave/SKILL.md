---
name: aiverse-terraweave
description: Implement, configure or extend AIVERSE genre-independent procedural terrain, biomes, trees, vegetation placement and ecosystem streaming without breaking existing terrain or deterministic ECS rules.
---

# TerraWeave — AIVERSE procedural world

Read these in order:
1. `AGENTS.md` (engine contract; determinism and RHI).
2. `docs/terrain/TERRAWEAVE.md` (API, units and compatibility).
3. `docs/terrain/AGENT_HANDOFF.md` (latest verified stage, commits and next tasks).
4. `docs/terrain.md` and `skills/world/aiverse-terrain/SKILL.md` (existing terrain renderer/editor).
5. `docs/ecs-agent-handoff.md` when modifying ECS or parallel scheduler.

> **Current implementation:** Horizon source is at Stage 22; **TWH8** is the latest archive. Earlier TWH6/TWH7 sections below are historical. Feature source commits exist, but CMake/CTest/GPU benchmarks have **NOT RUN** in this stage; PR #24 must stay Draft pending a separate testing agent.

## First decisions
- Identify whether work is generation (`engine/world/TerraWeave.h`),
  existing heightfield rendering, editor operations, tree geometry, foliage
  instancing or gameplay simulation. Never silently mix ownership.
- Preserve the X/Z horizontal, Y-up convention. TerraWeave elevation and
  world positions are signed integer **centimetres**; existing RTS heightfield
  uses 1/16 map units. Conversion belongs to an explicit adapter.
- For gameplay-affecting state use deterministic integer or Fixed, seeded
  stable hashes, and proper rollback/replay/save integration.
- Cosmetic foliage belongs in patches/instance buffers; only interactable
  trees/plants require full entities. Per-blade ECS entities are prohibited.
- Workers evaluate pure generation jobs; ECS structural writes and GPU uploads
  use their owning thread/stage. Check generation revision before commit.
- GPU work only via `gfx::Device`. Every shading change includes GLSL+MSL
  and a mobile/web quality fallback.

## Working phase 1 recipe

```cpp
#include "engine/world/TerraWeave.h"
using namespace aiverse::terraweave;
TerrainSettings settings;
BiomeSettings biomes;
TerrainChunk chunk = generateChunk(settings, biomes, {2, -1}, 32, 100);
FoliageSpecies oak; oak.id = 1; oak.biomeMask = biomeBit(Biome::Forest);
auto foliage = scatter(settings, biomes, {}, {6400, -3200, 9600, 0}, {oak});
```

Ensure species IDs unique; bounds half-open; output positions in cm.
Existing maps must remain unchanged when TerraWeave is disabled.

## Verification and documentation
- Run `aiverse_terraweave_test` and CTest; record exact outcomes.
- For terrain integration, test legacy RTS/FPS sample games, checksums,
  headless/replays, scene screenshots, neighbour tile seam continuity,
  negative coordinates, changed chunk revisions and frame budgets.
- **After every implementation phase**, append accurate status to
  `docs/terrain/AGENT_HANDOFF.md` with SHA, API/files, actual tests,
  limitations and next agent instructions. Never claim tests pass without
  running them. This is part of the deliverable, not optional.


## Arbor BranchLOD and final billboard (Stage 2g–2h)

- Near geometry: use `makeArborBranchLodModel(profile, prototypeSeed, BranchLodSettings)`
  for one shared `render::Model` per species. Each `render::Node` is the
  original trunk/branch with an independent `MeshDef::lods` chain.
  `LeafCluster::parentBranch` associates foliage with its structural branch.
  Never regenerate complete trees per LOD or create ECS entities per branch.
- `buildArborBranchMesh(tree, branchIndex, BranchDetail)` builds local Full,
  Reduced and Minimal meshes. Default transition diameters are 96px / 30px
  *per branch*. Keep high-detail bark where visible while individually
  reducing nearby twigs and distant foliage.
- Far representation: `makeArborBillboardModel(profile, prototypeSeed)`
  generates a transparent RGBA8 tree silhouette and one camera-facing quad.
  This is a real alpha-masked impostor model, but single-view / low fidelity;
  a multi-angle texture atlas with normals is not implemented.
- `ArborForestAdapter::addPrototype` registers and reuses both models per
  species; `uploadModels` and `releaseModels` operate on both on the
  render thread. Use `buildFloraBatches` and `submit` to switch to the
  billboard only for `TreeLOD::Impostor`. Existing Renderer3D chooses
  individual branch mesh LODs and handles normal model batching.
- `ForestRenderOriginCm` and `ForestCameraCm` use integer centimetres;
  `forEachDraw(batches, origin, callback, camera)` can inspect the selected
  near/far model on the CPU. Pass the real camera position for accurate
  impostor yaw when calling directly. `submit` derives it from Renderer3D.
- Verify `aiverse_terraweave_model_test` on GCC+Clang and on real RHI.
  Do not assert CI passed before inspecting logs; use
  `docs/terrain/AGENT_HANDOFF.md` for the exact current verification state.
  The adapter is not yet connected to any editor/game WorldScene automatically.

## Editor and MCP operations (Stage 3a)

When the game uses a **flat RTS grid map**, open the editor's
`Рельеф → TerraWeave` section or use the shared Workspace/MCP tools:

1. `terraweave_preview` with `{"seed":42,"reliefCm":5000,"basePeriodCm":6400,"octaves":5}`.
   It samples 9x9 heights; map contents/revision remain unchanged.
2. `terraweave_generate` with the same arguments to bake the current
   heightfield using existing `MapDoc::corners` and create an Undo step.
   By default refuses to replace authored corners or detail surfaces.
   Set `"overwrite":true` **only** when replacement is explicitly intended.
3. Generator metadata is stored under map `terraweave` with
   `version:1`, but is not automatically re-evaluated on map load.

Do not invoke flat MapDoc operations for FPS or planet maps. The
generic C++ generation API works across genres but those formats
need separate scene adapters. Preview is currently numeric, not
a GPU screenshot. Before reporting success, verify the map can undo,
redo, save, reload, and display updated terrain; log all actual test
results in `docs/terrain/AGENT_HANDOFF.md`.


## Stage 3b–3e: multi-view Arbor and opt-in native WorldScene host

- Use `makeArborImpostorAtlas(profile, seed, settings)` for 4/8/16
  camera azimuth slices in **one atlas texture** and **one Model**.
  `ArborForestAdapter` selects the tree-local view via stable
  per-species `Material` overrides and `ModelDraw::materials`;
  preserve those vectors until Renderer3D finishes its frame.
- To reduce forest-wide pop without per-tree transparent overdraw,
  set `FloraView::impostorBlendWidthCm` to an allowed nonzero band;
  the radius is deterministic for each instance. This is **not** a
  per-tree fade.
- Native users can opt in via `TerraWeaveForestScene`: add species
  before first stream/upload, update with `streamAt(cameraCm)`,
  then use `uploadModels(gfxDevice)`, `submit(renderer, originCm)`
  and `releaseModels(gfxDevice)` on the GPU-owning thread.
  `setExclusions` invalidates cached patches.
- `WorldScene` now accepts an OPTIONAL root `terraweaveForest`
  JSON block (see `docs/terrain/TERRAWEAVE.md`) and handles
  chunk cache/update/model lifecycle/draw when explicitly enabled.
  Never apply it silently to existing maps; the legacy RTS
  MapDoc editor is a separate path.
- Scatter work is bounded across the entire scene; invalid density/
  spacing or excessive resident candidate budgets are rejected.
  This initial forest scene is still **synchronous** and uses the
  first observer only. Do not claim worker async streaming, authored
  heightmap matching, navmesh/collision, or GPU validation.
- **No tests ran at user request.** Record every implementation stage
  and exact unverified status in `docs/terrain/AGENT_HANDOFF.md`.

## Scene forest preview (Stage 4, opt-in)

In the editor Terrain window, generate a TerraWeave terrain (recipe
`map.extra.terraweave.version=1`) and enable
`Показывать процедурный лес в «Сцене»`. This previews two shared
Arbor species in the editor's existing `Renderer3D`, including
branch LOD and multiview impostor selection. It does **not** add
gameplay trees to the map, ECS world, collision or save data.
Close the toggle to release GPU resources.

Implementation: `apps/editor/TerraWeaveForestPreview.h` (bounded
camera-relative scatter) and `TerraWeavePlacementMask.h` (editor-only
road/structure/water exclusions and unit conversion).
Renderer callback: `Editor::renderScene()`, after
`Renderer3D::beginScene` and before `render`.
Legacy map cells are always one metre in editor display even if
the generation recipe's source cell size differs from 100 cm; use
the conversion helpers rather than directly multiplying raw
generator positions by 0.01.

DO NOT claim this is async streaming or ECS gameplay. The preview
may stutter on first prototype upload or camera tile changes.
Scene screenshots and GPU benchmark measurements must be collected
before considering this feature production ready. Report tests
actually run in `docs/terrain/AGENT_HANDOFF.md`.

## Authored world terrain and exclusions (Stage 5)

When a native project/WorldScene forest must sit on an already-authored
heightfield, do not rewrite TerraWeave terrain noise or duplicate a
genre's collision data. Use the optional generic ForestScene hooks:

- `forest.setHeightSource(fn)`: `fn(worldXcm,worldZcm)->int32 heightCm`.
  Use a **snapshot** from the project's terrain/physics provider,
  not direct unsynchronised ECS state.
- `forest.setPlacementFilter(fn)`: `fn(const FoliageInstance&)->bool`.
  Reject roads, water, structures and unsafe slopes as necessary.
  The filter receives final sampled tree height.
- Both callbacks invalidate cached forest chunks; call
  `forest.streamAt(observer)` on the safe owning CPU stage.
  Reuse stable per-instance keys for persistence.
- The callbacks are **visual**, not a substitute for deterministic
  authoritative gameplay placement or collision. `scatter` still
  uses its own procedural noise for initial biome/height/slope
  checks; explain this limitation in agent handoff.

See `docs/terrain/TERRAWEAVE.md` and the latest
`docs/terrain/AGENT_HANDOFF.md` stage. Verify on real scenes
before claiming trees match ground physics.


## Authored WorldScene terrain + persistent tree state (Stage 6)

- When a forest is hosted by `WorldScene` with the optional
  `terraweaveForest` block, the world-streamed authored
  `ChunkCollision::terrain` samples are converted to integer cm and
  interpolated along the **same diagonal** as its actual physics
  terrain triangles by `AuthoredWorldHeightSnapshot`. Do not
  re-interpolate a bilinear heightfield for forest positions.
- `WorldScene::update` publishes one immutable snapshot of resident
  authored height tiles and collision box footprints through
  `forest.setLandscapeSources(height, mask)`. That single operation
  invalidates async work once and preserves previously published
  snapshots through shared ownership for pending worker jobs.
  An absent/unloaded tile falls back to procedural TerraWeave height;
  document potential seam/pop when streaming.
- Collision boxes are projected to XY-free world X/Z exclusion
  footprints. `FoliageExclusionMask` uses 64 m buckets for building
  rectangles; roads/water/polygons still use their existing policies.
  Standalone `setPlacementMask` is now applied even when no other
  callback is provided, in both sync and async scatter paths.
- Gameplay interactions should use
  `ForestTreeIdentity { key, speciesId }` plus
  `removeTree` / `restoreTree` /
  `setRemovedTrees` / `removedTrees`. The forest only filters
  presentation; a native game owns versioned persistence,
  server authority, ECS health/resource mechanics, physics/navmesh
  and replication. Do not allocate ECS entities for every branch or
  blade of grass.
- No stage-6 compilation/GPU/CTest was run, at user request. Cite
  `docs/terrain/AGENT_HANDOFF.md` for exact commits, limitations
  and verification status.

## Kilometre-scale Horizon HLOD (new)

Use `terraweaveHorizon` in the **WorldScene manifest** only when the
project needs a visibly distant forest/mountain skyline. It is opt-in.
Read `docs/terrain/TERRAWEAVE.md` § TerraWeave Horizon before changing
limits, CPU generation or renderer integration.

- **Near** detailed tree models, **mid** 128m forest cluster cards,
  **far** 512m cluster cards; do not allocate individual tree ECS
  entities or individual tree impostor draw calls beyond the near
  forest streaming range.
- The Horizon CPU baker samples terrain/biome masks and creates one
  shared atlas. `makeHorizonModel` creates two material primitives
  (coarse terrain + forest clusters). The existing `Renderer3D` and
  `gfx::Device` remain the only render paths.
- `TerraWeaveHorizonScene` uses the existing `AsyncChunkStream` for
  one staged region at a time; publish/upload only on the owning render
  thread, keep the old model displayed until the next completes, and
  discard a canceled cell result. Never start an unbounded future per
  camera move.
- `estimateHorizonBudget` is a **theoretical** atlas/geometry bound;
  do not confuse it with measured GPU draw calls or frame times.
  Default maximum: ~7.75 MB RGBA atlas, <8k triangles, 832 cluster
  slots. Distant trees are implied by the atlas, not generated.
- `distantTerrain` creates a coarse terrain ring outside
  `terrainBeginCm` and must not overlap an independently authored
  far-ground renderer without an ownership/clipping policy.
- Verify CTest `terraweave_horizon`, a running WorldScene and near/mid/
  far screenshots on Metal and GL/Vulkan, shadow consistency, memory,
  camera snapping and transition popping before moving Draft PR to
  review. Document untested limitations in `AGENT_HANDOFF.md`.

Known limits: crossed silhouette cards approximate the forest but are
not baked photorealistic multiview normal/depth impostors; alpha
overdraw, atlas uploads and visible transitions are not literally free.

## Horizon LOD/canopy quality (Stage 8)

`terraweaveHorizon` accepts `crownDetail` (1–3),
`orientationViews` (1–2), `nearBlendCm` (default 6000)
and `tierBlendCm` (default 16000). Default 1-view
is the inexpensive fallback for mobile; 2 views bake
separate 90-degree silhouettes into one ~14.77 MiB
RGBA atlas before mipmaps and keep the same number of
cards/triangles. The one-view atlas remains ~7.39 MiB.
Never allocate per-tree cluster draw calls for this mode.

The HLOD transition is stable **spatial atlas pixel dithering**,
not true continuous camera-time alpha fade. If both
`terraweaveForest` and `terraweaveHorizon` are
enabled, the close distance defaults to the existing
ForestScene's `view.cullRangeCm` rather than a
mismatched constant.

Use `TerraWeaveHorizonScene::runtimeStats()` to
inspect the actual running region's CPU prepare/upload
time, atlas bytes, vertex/triangle counts and pending
job state. These numbers become meaningful only
after execution on a real GPU and do not substitute
for frame-capture screenshots or render-pass statistics.
See Stage 8 of `docs/terrain/AGENT_HANDOFF.md`
for exact unverified checks and next tasks.


## Horizon asset baking, authored surfaces and quality control (Stages 9a–9e)

The implementation agent should focus on shipping function/API changes;
a separate test agent owns all actual build/CTest/GPU verification
for these stages. Update `docs/terrain/AGENT_HANDOFF.md` on each
implementation stage, stating `NOT RUN` explicitly.

- Use `HorizonSurfaceSources` with snapshot-owned
  `heightAtCm`, `biomeAt` and `allowsCanopyAt`
  to align far trees/ground with authored surfaces and
  exclude roads, structures and water. In `WorldScene`, use
  `setSurfaceSources(shared_ptr<const...>)` only on
  landscape snapshot changes, not on every frame. Existing
  `AuthoredWorldHeightSnapshot` supplies resident height tiles,
  procedural height is the fallback outside resident tiles.
- `maxCachedRegions` (0–4) keeps bounded immutable CPU
  Horizon bakes and reports `cacheHits` in `runtimeStats()`.
  `regionHysteresisCm` reduces back-and-forth rebuilds at a
  camera grid boundary. CPU retention does not eliminate GPU
  upload cost.
- Select the `HorizonQualityPreset` minimal/mobile/balanced/high
  or `terraweaveHorizon.quality` manifest string, then override
  individual fields as appropriate. All presets reuse one
  standard renderer and **do not** spawn distant tree ECS
  entities.
- Static worlds can `encodeHorizonArchive(bake, fingerprint)`
  and `decodeHorizonArchive(bytes, fingerprint)` using
  `TerraWeaveHorizonArchive.h` (format `TWH3`,
  explicit little-endian, bounded bytes, source hash and
  noncryptographic payload checksum). Default runtime has
  NO filesystem access; optionally set `HorizonArchiveStore`
  before update, or use manifest `archivePrefix` for
  **read-only** assets `<prefix>/<cellX>_<cellZ>.twh`.
- For authored terrains/masks, archive loading requires
  `authoredContentHash` / `archiveContentHash` to be
  nonzero and to change whenever the authored source
  content changes. Never silently use a procedural archive
  for a newly sculpted terrain. Missing/stale/corrupt
  archives must fall back to bounded CPU regeneration.
- Stage 9e handoff lists concrete race/round-trip/corruption,
  Metal/GL/Vulkan and FPS/RTS tests for the **testing agent**.
  The feature agent must not claim measured FPS or validation
  without those results.

## Horizon offline bulk bake and selective updates (Stage 10)

When a static game needs many Horizon regions, **do not write a new
baker** for each project. Use the host-only
`aiverse_horizon_bake` CMake tool or the reusable
`engine/world/TerraWeaveHorizonBatch.h` API:

```sh
aiverse_horizon_bake --manifest content/world.json \
  --out content/world/horizon-cache --cells -4 -4 4 4
```

The runtime will look up `<x>_<z>.twh` using its opt-in
`terraweaveHorizon.archivePrefix`. For custom authored worlds
call `bakeHorizonBatch` with your immutable terrain/mask
`HorizonSurfaceSources`, build-system writer callback, and
a stable nonzero `authoredContentHash` bound to terrain/road/water
content; the generic CLI is for procedural worlds only.
Archive format is now **TWH2**, not TWH1, because fixed atlas
slots were introduced. Always invalidate and regenerate old files
when the baker format or generator fingerprint changes.

For edit-time terrain modification, call
`TerraWeaveHorizonScene::setSurfaceSources(snapshot, editedRects)`
with world-centimetre half-open rectangles; this allows selective
CPU atlas/terrain sample regeneration when an eligible previous
bake matches. `WorldScene` already passes loaded/unloaded
authored collision/obstacle tile bounds to the Horizon adapter.
Maintain a previous immutable CPU bake only for configured
`maxCachedRegions>0`, keep GPU model until publish, and let
the existing `AsyncChunkStream` own cancel/staging/thread
lifecycle. No new ECS entities or dedicated rendering backend.

**Testing is assigned to another agent** by the user.
The function-implementation agent updates
`docs/terrain/AGENT_HANDOFF.md` after each stage and records
`NOT RUN`; do not add/invoke tests or state a pass without
external executed logs.

## Horizon incremental GPU atlas updates (Stage 11b)

When editing the Horizon GPU path, reuse
`engine/render/TerraWeaveHorizonGpuPatch.h`, not
new device APIs. The pure CPU
`horizonCanReuseModelGeometry` must gate all atlas
subregion updates; `planHorizonAtlasUpload` returns
a bounded plan or nullopt; only `gfx::Device` on
the render owner thread may apply the plan.
Geometry or terrain height/colour changes must
fall back to ordinary model upload; keep the old
proxy until the replacement has uploaded.
Do not claim partial **geometry** GPU updates:
this implementation changes texture subregions
only. The separate testing agent owns CTest,
GPU row-pitch/mipmap validation, screenshots and
performance benchmarks. Log all outcomes in
`docs/terrain/AGENT_HANDOFF.md`.

## In-place GPU Horizon terrain updates (Stage 12)

When an authored distant terrain tile changes, first try the
CPU partial Horizon bake and the **existing** GPU atlas patch path.
For a terrain vertex-only or mixed atlas+terrain edit, use
`engine/render/TerraWeaveHorizonVertexPatch.h`:

- The Horizon Model MUST have
  `batching.allowGeometryPool=false` prior to GPU upload.
  Never update a shared pool page using model-relative offsets.
- `planHorizonVertexUpload` rejects changed topology/index
  streams, altered cluster card geometry, relocated UV slots
  and any increase beyond existing conservative model/mesh/
  primitive bounds. It limits work to 20 contiguous ranges
  and 35% of terrain vertices by default.
- Check `horizonCanApplyVertexUpload` on the RHI-owning
  thread, then apply planned `gfx::Device::updateBuffer`
  calls. If the planner rejects the edit, do the safe full
  model upload rather than forcing an in-place change.
- Track `vertexPatchCount`, `vertexPatchBytes`,
  `vertexPatchRanges` and `fullUploadCount`. They do
  not establish measured performance until tests run.
- GL/Vulkan/Metal updateBuffer has a void return and GPU
  device loss isn't transactional. Test agent must validate
  offsets, bounds/culling and driver synchronization.

**Testing ownership:** Another agent runs all build/CTest,
GPU and performance checks. The current feature agent should
update `docs/terrain/AGENT_HANDOFF.md` with implementation
commits and `NOT RUN`, but must not claim success.

## GPU Horizon region cache ownership (Stage 13)

For forests spanning many kilometres, opt into
`HorizonSettings::maxCachedGpuRegions`
(0–3) and `gpuCacheBudgetMiB` (8–96)
to retain recent fully uploaded `render::Model`
regions without copying GPU resources. Use
`HorizonGpuRegionCache` from
`engine/render/TerraWeaveHorizonGpuCache.h`;
do not create per-species or per-tree GPU page
caches.

All cache `take`, `retain`, `prune`, and
`clear(device)` operations run on the
RHI owner thread. Do not cache a model from
an old authored `sourceRevision_`; never
submit a cached model after a terrain/mask
revision that could affect it. Keep the
last visible model while the new region
bakes and uploads, then retain the old
one if within the bounded LRU budget.
Always drain `AsyncChunkStream` canceled
results even when a GPU cache hit returns
without a new worker submission.

`WorldScene::shutdown(device)` MUST release
the active GPU model and every inactive
cached GPU model before the device dies.
Use `HorizonRuntimeStats` GPU cache fields
to diagnose retention; estimated GPU bytes
are NOT driver measured or GPU performance.
Build, sanitizer/CTest, Metal/GL/Vulkan,
WebGL, and benchmark execution are assigned
to the dedicated test agent. Record
`NOT RUN` until that agent provides logs.


The inactive GPU LRU also reports `gpuCacheEvictions`
alongside `gpuCacheHits`, `gpuCachedRegions` and
`gpuCachedBytes`. High eviction counts during
back-and-forth camera movement indicate an overly small
budget or frequent authored-source invalidations; adjust
`maxCachedGpuRegions`/`gpuCacheBudgetMiB` only after
the test agent collects real FPS and memory evidence.

## Horizon region crossfade (Stage 14)

The same `TerraWeaveHorizonScene` can use
`regionTransitionMs=0..1000` to interpolate
between two *already GPU-uploaded* Horizon
regions using standard `Renderer3D` model
tint alpha and its existing transparent pass.
Neighbor transitions are 240ms by default
in Balanced/High and OFF by default in
Minimal/Mobile. The old model must consume
an inactive GPU residency slot and fit
`gpuCacheBudgetMiB`; do not add an
unbounded transition buffer.

Call `finishTransition(device)` on the
owner RHI thread, keep authored-source
revision checks, and release the outgoing
model or return it to the GPU LRU when
the fade finishes. Fast teleports and
terrain revisions intentionally skip
blending. `WorldScene::shutdown(device)`
must also release any pending outgoing
model.

`HorizonRuntimeStats` tracks
`transitionStarts`, `transitioning`,
and `transitionProgressPermille`.
Tests and GPU screenshots remain the
separate testing agent's responsibility;
the feature agent must label results
NOT RUN until actual evidence exists.

## Filtered far-ground / canopy transition (Stage 15)

When modifying Horizon transitions, never blend two
full combined distant terrain meshes. Use the engine
generic `ModelDraw::primitiveMaterialFilter`:

- `-1` is the normal entire model (steady-state);
  default native project behavior remains unchanged.
- Source material `0` is one **opaque incoming**
  far-ground layer during crossfade; draw it only once.
- Source material `1` is the forest cluster canopy:
  fade outgoing/incoming models using existing
  `Renderer3D` transparent tint alpha. Partial draws
  bypass aggregate shadow/material batches and
  static cache; don't add a GPU model copy or
  separate GL/Metal/Vulkan implementation.
- Transient material-filtered draws use no persistent
  scene identity or static light-bake capture. The
  steady state returns to full unfiltered static draw.
- A→B→A while fading should reclaim the old GPU
  model directly from transition ownership; don't
  force it through a one-slot LRU that could evict it.
  Teleports outside the old far-footprint skip
  stale proxy submission until a new one arrives.
- The existing GPU cache/revision/budget checks and
  render-thread release remain mandatory.
  Document alpha/skyline limitations and leave
  CMake/CTest/device validation to the testing agent.

## Horizon far-terrain global lattice and outer skirts (Stage 16)

Never return to camera-local height sampling with
`origin + x*side/resolution`; that changed height
sampling phase between neighboring 512m camera regions
for High 64×64 terrain and non-dividing resolutions.
Always use the globally anchored signed rational
grid `floorDiv(globalIndex*regionSide,resolution)`,
also for negative world positions.

`HorizonTerrainVertex` now records
`slopeXPermille/slopeZPermille` sampled from
both neighboring globally aligned height values,
including ghost samples beyond the current patch.
For authored edits, invalidate an extra grid-step
band so dependent border normals are refreshed.

`HorizonSettings::terrainSkirtDepthCm` (0..5000)
adds a downward-facing outer skirt **inside
material slot 0** and without a new draw call.
Minimal quality disables it; Mobile defaults to
600cm and Balanced/High to 1200cm. Include
the added 4(N+1) vertices / 8N triangles in
analytic geometry budgets; changing skirts can
trigger safe full-GPU-model fallback.

Current static-content archive is **TWH3**;
TWH1/TWH2 files are obsolete and must be
rebuilt via `aiverse_horizon_bake` or runtime
fallback. World-origin agreement only holds
when the two bakes sample the same immutable
authored terrain snapshot. No CMake/CTest/GPU
results are claimed in this stage; the dedicated
testing agent owns regression and visual checks.

For standalone native terrain/HLOD producers, call
`makeHorizonTerrainLattice(settings,cameraXcm,cameraZcm)`
and `worldX(i)/worldZ(i)` (including ghost indices)
instead of duplicating grid arithmetic. The same helper
is used internally by `bakeHorizon` and protects against
invalid zero-resolution division.

## Horizon near-ground ownership and TWH4 (Stage 17)

Do not delete far Horizon quads just because
they are within a hard-coded radius. Prefer
`HorizonSurfaceSources::ownsNearTerrainRect`
when resident near-ground geometry is
available. `WorldScene` uses immutable
`AuthoredWorldHeightSnapshot::coversRect`
to assert **full** half-open rectangle
coverage. Missing near terrain must retain
far-ground geometry; a partial overlap must
not be falsely classified as fully covered.

`HorizonBake::nearOwnedQuads` carries
one 0/1 flag per far cell. `makeHorizonModel`
removes fully owned quads, and optionally
adds a short `innerTerrainLipCm` (0–200cm,
default 40cm), bounded to 512 seam
segments and **no extra material or
draw call**. Incomplete coarse cells can
still overlap near meshes; don't claim
exact polygon clipping.

The far-terrain lattice uses
`terrainGridSnapSteps` (1..16, default 4)
and nearest signed global-grid alignment
to reduce mountain silhouette movement
during 128m forest-atlas updates.
Minimal=1, Mobile=2, Balanced/High=4.
The distant forest radius must remain
inside the guaranteed terrain coverage
for those snap offsets (roughly 5+ km).

Current archive is **TWH4**. Near ownership
maps, inner seam depth and snapped grid
settings must be reflected in archive
version and fingerprint; old TWH1–TWH3
assets must be rebaked. Keep settings
and bake extensions trailing so native
C++ aggregate initializers stay usable.
GPU index topology changes invalidate
partial VB/atlas reuse and trigger
full upload. All validation is owned
by another test agent; mark NOT RUN.

## Exact near-ground rectangle clipping — TWH5 (Stage 18)

For `WorldScene` worlds with loaded near terrain,
implement `HorizonSurfaceSources::ownsNearTerrainRect`
AND `collectNearTerrainRects` using a single immutable
height snapshot. The second callback returns sorted,
non-overlapping half-open world-centimetre rectangles;
the core accepts at most 1024 per far region.

`planHorizonNearGroundClipping(bake)` in
`engine/render/TerraWeaveHorizonClip.h` uses only CPU
polygon subtraction, preserves the two original coarse
triangle planes and their winding, and emits no more
than `maxNearClipTriangles` partial triangles. The
`makeHorizonModel` stage interpolates vertex height,
normal, UV and colour barycentrically on those planes.
All geometry remains on material slot 0, and the
existing forest material remains slot 1. Do not
create one material/model/draw per near tile.

Quality caps: Minimal 0 (feature off), Mobile 512,
Balanced 2048, High 4096; manifest accepts 0..8192.
A geometric budget overflow triggers a complete
fallback to coarse Stage 17 full-quad ownership,
not partially applied clipping. UI diagnostics:
`nearClipQuads`, `nearClipTriangles`,
`nearClipFallback`. In-place GPU patch reuse
MUST compare stored clip rectangles/settings and
fall back to a full model upload on topology changes.

Current static atlas archive is **TWH5**. It stores
bounded near coverage rectangles and a cap with a
new fingerprint; TWH1–TWH4 must be regenerated,
not silently interpreted as current data. The cut
is exact in X/Z, not a height-aware vertical weld;
keep that documented until real GPU tests prove
a stitched seam. Separate test agent owns all
build/CTest/platform rendering/performance work.

## Height-aware Horizon near/far seam welding (Stage 19 / TWH6)

For authored near terrain, supply **all three callbacks from the same
immutable snapshot**: `ownsNearTerrainRect`,
`collectNearTerrainRects`, and
`nearGroundHeightAtCm` (optional signed-centimetre surface
height, nullopt outside resident near-ground tiles).

The CPU worker or offline `bakeHorizonBatch` must call
`populateHorizonNearSeamSamples(bake,source)` after
`bakeHorizon` and before converting to a render Model,
caching or archiving it. Do not query mutable collision/ECS
data from `makeHorizonModel` or the renderer. The sample
table uses Q16 world-centimetre X/Z keys and max 4096
unique near-edge positions; a missing required height
must disable the *whole* optional height-weld table rather
than leaving a half-welded seam.

`makeHorizonModel` replaces only exact clipped-edge Y
with the baked near-tile surface height and retains the
far-source plane on interior clipped vertices. This is
still the **same** bounded triangles and single
material-0 terrain primitive. Observe
`weldedNearVertices` and `maxNearWeldDeltaCm`
in `HorizonRuntimeStats` when deciding whether extra
normal/slope smoothing is needed.

The current archive is **TWH6**, not TWH5. It persists
all bounded seam samples and changes the generator
fingerprint; TWH1–TWH5 static assets must be
rebaked. Tests, CPU clipping fixtures, CTest, MSVC/Clang,
Metal/GL/Vulkan captures and benchmarks belong to the
dedicated testing agent; do not mark them passed from
source code review alone.

## Horizon seam slope and normal policy (Stage 20 / TWH7)

When building precise near/far terrain cuts, use
`maxNearWeldSlopePermille` (0–8000, default 2500)
and `nearSeamNormalBlendPercent` (0–100, default 65)
from shared `HorizonSettings`. Both are configured
in the opt-in `terraweaveHorizon` manifest.

`populateHorizonNearSeamSamples` must preflight the actual
3D triangle wedge BEFORE committing any near-height samples.
If a weld newly exceeds the slope limit, **clear the entire
near-height sample table** and set
`HorizonBake::nearWeldSlopeFallback`. Preserve the accurate
X/Z near rectangle clipping; do NOT silently clamp a seam
vertex's Y and describe it as welded. A large near/far
height difference may need a future wider transition mesh.

`makeHorizonModel` groups clipped corner positions by
world X/Z plus resulting Y and uses area-weighted repaired
face normals. Only the existing clipped vertex normal
attributes are blended; the near collision or authoritative
terrain never changes. Avoid extra tree draws or new GPU
geometry solely for normal shading.

`HorizonRuntimeStats::smoothedNearNormals` and
`nearWeldSlopeFallback` survive GPU model cache reuse.
Current archive is **TWH7**; all previous TWH1–TWH6 assets
must be rebaked after the new version/fingerprint. Keep
the per-stage handoff and explicitly mark tests NOT RUN
until the separate test agent supplies compiler, CTest,
and real Metal/GL/Vulkan evidence.

## Q16 authored seam normals and TWH8 (Stage 21)

Use `AuthoredWorldHeightSnapshot::sampleQ16` for precise
fractional near-terrain X/Z, preserving the old `sample`
API for integral world-centimetre callers. Provide
`HorizonSurfaceSources::nearGroundHeightAtQ16Cm` from
the SAME immutable authored source as
`collectNearTerrainRects` and `nearGroundHeightAtCm`
where possible. Native games may omit the Q16 callback
to retain legacy integer-centimetre seam behavior.

`populateHorizonNearSeamSamples` samples exact clipped
seam positions (Q16), clamps high half-open tile
boundaries by only one Q16 unit, and computes optional
bounded slope X/Z with a +/-200cm tile-local stencil.
Missing derivative input marks `nearNormalValid=false`;
never fabricate a normal or partially weld a missing
height. The Stage 20 `maxNearWeldSlopePermille`
all-or-none safety guard remains mandatory.

In `makeHorizonModel` use authored near-surface
normal targets at welded edges when available; use
Stage 20 area-weighted face normals otherwise.
`HorizonRuntimeStats::matchedAuthoredNearNormals`
reports actual authored-normal matches; it is distinct
from `smoothedNearNormals`. Neither changes GPU
geometry allocations or draw calls.

**TWH8 is the current archive version.** It stores two
signed normal slopes and a validity bit alongside each
height sample; existing TWH1–TWH7 binaries are stale.
Atlas-only patch eligibility must compare all seam
normal/height fields; a vertex patch can apply if the
index layout and bounds are still safe. The test agent
owns all CMake/CTest, archive roundtrips, cross-platform
GPU screenshots and profiling. Label them NOT RUN
until logs exist.


## Stage 22 — current Horizon seam and diagnostics recipe

These rules apply when implementing **or consuming** TerraWeave
Horizon in native C++ games, the generic WorldScene adapter or editor
preview. Do not introduce genre-specific FPS/RTS dependencies.

1. **Capture one immutable authored height snapshot per bake.**
   Keep `HorizonSurfaceSources::nearGroundHeightAtCm` and optional
   `nearGroundHeightAtQ16Cm` from the SAME snapshot used by
   `collectNearTerrainRects`. If available, also supply
   `nearGroundContinuousSlopeAtQ16Cm` with signed Q16-centimetre
   arguments and an `optional<pair<int32_t,int32_t>>` result.
   `WorldScene` already wires these callbacks. Native providers may
   omit optional callbacks without breaking older terrain.

2. **Use neighbor-aware normals only across continuous authored tiles.**
   `AuthoredWorldHeightSnapshot::continuousSlopeQ16` uses a bounded
   200cm default radius and 1cm default boundary-height tolerance.
   It returns signed X/Z permille slopes, or nullopt when a tile is
   missing or has a real seam discontinuity. In
   `populateHorizonNearSeamSamples` prefer the callback, then the
   existing tile-local Q16 derivative, then
   `makeHorizonModel`'s area-weighted face-normal fallback.
   Never invent missing normals; do not alter authoritative physics,
   collision, triangle topology, weld slope limits or RHI layouts.

3. **Read `HorizonRuntimeStats` as a state machine, not as raw numbers.**
   It is returned by `TerraWeaveHorizonScene::runtimeStats()`.
   Use these conditions:
   - `requestedRegionPresent` before reading `requestedCellX/Z`.
   - `activeRegionResolved` before reading `activeCellX/Z`.
   - `activeRegionEmpty` means an accepted no-model bake, not a job still running.
   - `activeRegionPresent` means drawable model only.
   - `requestedRegionPending` means selected target is unresolved,
     different from active cell or at an old source revision; consult
     `preparing` separately for actual CPU worker activity.
   - `staleProxy` means `activeRevision != sourceRevision` for a resolved
     active cell, including resolved empty cells; it does not by itself
     report a wrong-camera-cell teleport.
   - Cell X/Z values are middle-tile grid coordinates, not centimetres.
     A requested region is chosen after camera hysteresis.

4. **Never interpret unknown provenance as zero.**
   Only present `continuousNearNormalSamples` and
   `localFallbackNearNormalSamples` as measurements when
   `nearNormalProvenanceAvailable` is true. They count **unique seam
   sample points**, not pixels, GPU vertices or draw calls.
   `matchedAuthoredNearNormals` is a **render-vertex** count.
   TWH8 archives store sampled slopes/validity but intentionally do NOT
   store provenance; archived bakes can report unknown counters.
   A fresh bake may legitimately have valid zero counters.
   GPU LRU retention/restoration propagates provenance and availability.

5. **Preserve lifecycle and patch semantics.**
   The CPU worker produces bounded immutable bakes; only the owning
   render thread may change RHI state. The Horizon scene may retain
   the previous GPU proxy when the authored source revision advances.
   `shutdown(device)` clears active/requested cells and diagnostic
   stats. Atlas-only patch checks must reject altered seam normal/height
   fields; use the existing compatible partial vertex update or full
   upload fallback. The current **TWH8** layout/fingerprint must remain
   unchanged unless a deliberate format migration is documented.

6. **Handoff after every code or documentation stage.**
   Record the exact branch and source commit hashes, files/API touched,
   actual verification (do not claim CTest/GPU validation from a GitHub
   commit), limitations and the next implementation/test task in
   `docs/terrain/AGENT_HANDOFF.md`. Keep PR #24 Draft until separately
   verified.

### Practical host-side status check

```cpp
const auto stats = horizon.runtimeStats();
if (stats.requestedRegionPending) {
    // Not resolved for selected camera cell/revision. Check stats.preparing.
}
if (stats.activeRegionResolved && stats.activeRegionEmpty) {
    // Resolved empty region: do not keep requesting a mesh just because
    // activeRegionPresent is false.
}
if (stats.staleProxy) {
    // Rendered/resolved result reflects an older source revision.
}
if (stats.nearNormalProvenanceAvailable) {
    reportSeamNormalSamples(stats.continuousNearNormalSamples,
                            stats.localFallbackNearNormalSamples);
} else {
    reportUnknownSeamNormalProvenance();
}
```

The reporting functions above are pseudocode host callbacks, not
AIVERSE APIs. For the exact field semantics and source callback
contracts, see `docs/terrain/TERRAWEAVE.md` Stage 22.

### Required independent test-agent acceptance

- C++20 build for target platforms; no transitive include assumptions.
- Q16 negative coordinates, integer/fractional heights, adjacent
  continuous/discontinuous/missing tiles and half-open corners.
- Source-revision changes while GPU proxy remains, cache hit, partial
  VB/atlas/full GPU paths, empty model, teleport, camera hysteresis
  and shutdown.
- TWH8 archive hit vs newly generated provenance availability,
  exact old-format rejection and never asserting unknown counts
  as observed zeros.
- Verify unchanged draw/index/triangle budget, capture real
  Metal/GL/Vulkan screenshots and benchmark overdraw/frame time.

**Feature-agent validation state: NOT RUN** (code/docs committed only).

## Stage 24 — forest harvesting and regrowth lifecycle rules

**Source feature committed; tests intentionally deferred.** Native projects can use the existing `ForestTreeIdentity{key,speciesId}` and the new sparse `ForestRegrowthSchedule` to restore harvested decorative trees without allocating one ECS entity per tree, rebuilding scatter patches or uploading new models.

- `TerraWeaveForestScene::removeTree(id)` hides a tree and cancels its regrowth schedule. `restoreTree(id)` clears both hidden and scheduled state; `setRemovedTrees(ids)` replaces saved removal data and overrides any matching schedules.
- `scheduleRegrowth({id,regrowAtTick,matureAtTick,initialScalePermille})` atomically replaces that key's schedule and clears its explicit removal. The tree is not rendered until `regrowAtTick`; thereafter its *presentation-scale copy* interpolates from 1..1000 permille to the generator's full scale. `matureAtTick==regrowAtTick` is instant.
- `setPresentationTick(gameplayTick)` uses authoritative simulation/replay time; do **not** use wall time, worker elapsed milliseconds, global RNG, floats or genre-specific components. Tick changes do not invalidate async chunks. Persist sorted `regrowthSchedules()` separately from `removedTrees()` in native save/replication; `setRegrowthSchedules` validates/restores bulk schedules.
- Bounded max 1,000,000 records and max 1e12 tick interval prevent unbounded work and integer arithmetic overflow. Duplicate saved keys, invalid scales/ticks and zero species must be rejected.
- `world::WorldScene::forestScene()` returns the optional generic forest pointer (null when the world manifest did not opt in). For host-side manipulation include `engine/world/TerraWeaveForestScene.h` explicitly; avoid genre adapters in engine/world.
- `ForestSceneStats::scheduledRegrowth` and `presentationTick` are trailing diagnostics. Regrowth is **visual**: the gameplay authority owns resources, collision, destruction, navmesh, persistence and replication. Far Horizon cluster HLOD does not yet reflect per-tree removals; do not claim forest-wide harvest parity.

**Next functional work** before the separate test phase: add bounded harvested-area masks / multi-tree clearings in Horizon from authoritative world patches, or a compatible seasonal growth/dormancy source shared by Flora and Horizon. Do not add tests or run CTest in this feature-first phase unless the user changes priorities. Always append an exact per-stage handoff record after coding.

## Stage 25 — authoring runtime forest clearings across LODs

Use `world::WorldScene::setVegetationClearings(std::vector<terraweave::RectCm>)` to opt in to whole-area forest removal or restoration. This is **not** the same as the sparse per-tree regrowth overlay in Stage 24: rectangle clearings are incorporated into a shared, immutable `FoliageExclusionMask` used by both `TerraWeaveForestScene` and `HorizonSurfaceSources::allowsCanopyAt`. Changed and removed clearings must rebake only via the existing async source-revision invalidation, not by mutating GPU models or a worker-captured callback in place.

- Coordinates: signed world centimetres, half-open min/max X/Z. Rectangles must be nonempty, within `kWorldLimitCm`, <=4096 input shapes. The setter sorts and deduplicates, making unchanged calls no-ops.
- Keep previous **and new** rectangles in the Horizon edited bounds when changing/clearing masks. Otherwise older canopy atlas cards can stay visible after restoring an area. Use the existing bounded full-rebake fallback for oversized edits.
- `WorldScene::update()` is the source publication point. Native hosts call the setter on the scene-owning update thread, never concurrently with a renderer draw or async callback.
- Save `vegetationClearings()` and restore via the setter; gameplay still owns authoritative construction, resource harvesting, collision/navmesh, network and save versioning. Do not introduce RTS/FPS-specific dependencies or per-tree cosmetic ECS entities.
- The far HLOD atlas currently samples mask positions rather than tracking every individual generated near-tree identity. Rectangular clearings suppress far canopy; Stage 24 individual regrowth is near-only. Fully empty cluster cards and visual alpha transitions still require future optimization, not an unverified claim of perfect GPU performance.

**User preference:** finish functional features before running builds/CTest. After each coding stage append an agent handoff with exact commits, changed paths, public API, *actual* checks, limitations and next feature. All GPU/CI tests remain assigned to the later testing stage.

## Stage 26 — native tree selection without per-tree ECS entities

For gameplay interactions that require identifying a nearby planted tree, call `TerraWeaveForestScene::nearestTreeAt(xCm,zCm,radiusCm)` or `treesInRect(RectCm,maxResults)`. They query *already-resident* CPU foliage patches and share the exact removal/regrowth filtering of `visibleBatches()`. Avoid rebuilding procedural chunks merely to select one tree. Use `ForestTreeIdentity{key,speciesId}` as the stable handle, never the position of an instance in a render batch.

Queries use signed world-centimetre coordinates and are bounded: a nearest radius of 1..10 million cm, or up to 100,000 returned rectangle results (caller should normally request far fewer). Tied distance sorting is deterministic by species/key; rectangle queries sort the result by the same identities. The source does not include remote/unloaded trees. The authoritative game retains collisions, harvesting decisions, inventory, pathfinding, networking and save state; no RTS/FPS-specific runtime is required.

**Features-first policy:** do not start a CTest/GPU troubleshooting loop until feature implementation is complete or the user reprioritizes. Append exact code/docs commits and next step to `docs/terrain/AGENT_HANDOFF.md` at every implementation stage.

## Stage 27 — seasonal canopy policy shared with Horizon

Use the reusable `engine/world/TerraWeavePhenology.h` POD `CanopyPhenology` for biome/year/weather-driven **presentation**. It defines broadleaf/evergreen cover [0,1000] and RGB tint multipliers [0,2000], all integer permille. A fully default-constructed value MUST preserve the previous green, full-canopy output.

- Native `WorldScene::setVegetationPhenology(value)` or directly `TerraWeaveForestScene::setPhenology(value)` applies per-species Arbor leaf-material and shared impostor-override appearance on the caller's render/update thread. No near scatter resample or per-tree ECS entity is needed. Conifers use evergreen cover; other TreeProfiles use broadleaf cover. Bark stays unchanged on near geometry.
- `HorizonSurfaceSources::phenology` is an immutable value in the same worker snapshot as authored heights, biomes, masks and near seam sources. Horizon CPU baker deterministically thins sampled broadleaf/conifer crowns and adjusts leaf RGB in the **existing** shared atlas; near-ground height and terrain mesh remain unchanged.
- Changing seasonal policy in `WorldScene` queues one Horizon source revision on the next update, not a terrain edit, and does not cancel Flora workers. Do not mutate `ModelDraw::materials` borrowed vectors after draw submission until the frame finishes.
- Since TWH8 stores **no season key**, its static disk archives MUST NOT be used for a nondefault policy. Keep the nondefault-season bypass until a deliberate multi-season fingerprint/archive asset migration is implemented. Default archive reuse continues.
- Seasonal alpha blending can increase GPU overdraw. An impostor billboard combines trunk and leaves in one atlas, so season fade applies to the whole impostor; do **not** claim pixel-accurate leaf-only seasonal impostors or identical near/far morphology. Future improvements need dedicated trunk/leaf channels or an explicit shared shader.
- PHYSIS/weather/calendar modules are upstream **providers only**. Never introduce direct PHYSIS-to-renderer dependencies or genre-specific seasons inside TerraWeave. The host chooses values and persistence/replication policy.

**Validation intentionally deferred:** no CMake, CTest, graphics backend screenshots or measurable perf evidence yet. User requested features first. Update `docs/terrain/AGENT_HANDOFF.md` after each functional step.

## Stage 28 — PHYSIS-neutral per-tree wind presentation

Use `engine/world/TerraWeaveWind.h` and `ForestWindState` to pass optional wind from a game's PHYSIS/weather adapter; do not link TerraWeave directly against any genre or physical-weather solver. The caller provides horizontal world X/Z velocities in **cm/s** and a simulation tick. Reject magnitude beyond 5000cm/s, gust outside [0,1000], cycle outside [8,4096] ticks or max lean beyond 3000 millidegrees. Default zero velocity preserves previous rendering.

- Native `WorldScene::setVegetationWind(value)` and `setVegetationPresentationTick(tick)`, or direct `TerraWeaveForestScene::setWind` / `setPresentationTick`, feed `ArborForestAdapter`.
- `forestTreeLean(state,tick,stableKey)` returns bounded signed milli-degree X/Z lean using integer piecewise-linear waves with stable hashed phase. Root sway transforms are applied in world space before local tree yaw, after origin rebasing. Do not feed renderer sway transforms into authoritative collision, navmesh, physics, networking, replay checksums or harvest calculations.
- This is **whole-tree cosmetic lean**, not animation of individual branches, leaves or trunk deformation. Impostor quad leans as a unit; far Horizon is intentionally static. Zero-wind path is unchanged; no new GPU models, passes, procedural chunk rescatter or Horizon archive migration.
- Host must not update wind state during `Renderer3D` submission/rendering. Use deterministic tick values and make scene setup/update single-thread-owned. Tree-model GPU buffers are only touched by the existing upload/release lifecycle.

**Testing is postponed by user instruction.** The later agent should benchmark large forest draw rates, camera motion and origin rebasing, verify zero-wind parity, amplitude bounds, negative wind, different tree keys and native PHYSIS integration, plus real Metal/GL/Vulkan visuals. After each feature commit, record exact source SHA, API, actual checks, limitations and next agent task in `docs/terrain/AGENT_HANDOFF.md`.
