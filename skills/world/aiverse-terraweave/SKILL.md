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
