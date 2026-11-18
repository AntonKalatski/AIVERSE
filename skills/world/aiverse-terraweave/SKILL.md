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
