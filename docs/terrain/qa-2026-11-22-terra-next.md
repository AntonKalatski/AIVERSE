# TERRA next steps — Metal verification, automatic streaming, tree colliders, Studio generator (2026-11-22)

Branch `agent-terra-next` (from `origin/main` d677f8a2f). Mac Studio, AppleClang, Unix Makefiles
Release, `AIVERSE_ENABLE_PHYSICS=ON`, build dir `/Volumes/KINGSTON/AIVERSE-builds/terra-next/native`.
Only what is listed under "Verified" was actually executed.

## 1. GPU / editor / performance verification

### CTest (executed on the Mac)
* Baseline (main, before changes): 19/19 TerraWeave-related CTests passed — `camera_terrain_prepared_api`,
  `terraweave_review_fixes`, `physis_terraweave_fall_bridge`, `terraweave`, `terraweave_micro_nutrient`,
  `terraweave_native_micro_compile`, `terraweave_native_micro_physics_compile`, `terraweave_arbor`,
  `terraweave_flora`, `terraweave_model`, `terraweave_heightmap`, `terraweave_ops`, `terraweave_preview`,
  `terraweave_erosion_zones`, `terraweave_forest_scene`, `terraweave_horizon`, `terraweave_editor`,
  `terraweave_rocks`, `terraweave_landscape` (editor + tests built in 850 s; only pre-existing warnings
  in `AttachmentAuthoringWindow.cpp`, `FpsModule.h`, `RtsGroundFx.cpp`, `RtsView.cpp`).
* After this branch's changes: `terraweave_native_micro_compile`, `terraweave_native_micro_physics_compile`,
  `terraweave_preview`, `terraweave_forest_scene`, `terraweave_streaming` (new),
  `terraweave_streaming_physics` (new, real Box3D raycast against trunk capsules), `terraweave_horizon`: 7/7.
  Editor / Studio session results for the generator ops: see the end of this file.
* `lighting_terrain_gi_gpu` / `lighting_terrain_gi_gl` (need `AIVERSE_BUILD_HDR_TESTS`) were NOT run.

### Editor Metal frames (real `AiverseEditor`, agent link, `tools/editor/terra_perf_review.py`)
Harness: `tools/editor/terra_perf_review.py` (launch: `build/claude/terra-next-perf*.command` pattern).
Each row = median of 7 `geometry_stats` samples after two warm-up captures; range in brackets.
`frameIntervalMs` = wall clock (ImGui 60-frame average, vsync/drawable waits included) — the editor
loop is capped at ~10 ms, so 10.0 means "not the bottleneck". `completedGpuFrameMs` includes Metal
drawable waits and other agents' GPU load (the first live run measured 7–12 ms GPU for the same
scenes that measured 0.5–3 ms an hour later — the GPU is shared). Cameras: near = eye ~1.7 m above
ground, 12–14 m orbit; mid = 60–80 m; far = 220–260 m.

**(A) Live TerraWeave in a new 192 x 192 m map** (`terraweave_generate` seed 20261121, relief 14 m;
live Arbor forest overlay, PHYSIS grass, Horizon HLOD; project = copy of `templates/wildfire`):

| layers | view | frame ms | GPU ms | draws | camera tris | sun-shadow tris |
|---|---|---|---|---|---|---|
| terrain | near / mid / far | 10.0 / 10.0 / 10.0 | 0.7 / 0.5 / 0.7 | 1 | (legacy map surface, not counted) | 0 |
| + Arbor forest | near | 10.0 | 0.7 | 57 | 0.09M | 0.17M |
| + Arbor forest | mid | 10.7 (10.0–13.6) | 1.8 | 71 | 0.14M | 0.19M |
| + Arbor forest | far | 10.0 | 0.7 | 1 | 0 | 0 |
| + grass | near | 10.0 | 1.4 | 96 | 0.44M | 0.19M |
| + grass | mid | 10.0 | 3.0 | 145 | 0.77M | 0.21M |
| + grass | far | 10.0 | 0.8 | 165 | 0.34M | 0 |
| + Horizon (after fix) | near | 10.0 | 1.5 | 130 | 0.45M | 0.19M |
| + Horizon (after fix) | mid | 10.0 | 3.1 | 193 | 0.78M | 0.19M |
| + Horizon (after fix) | far | 10.0 | 0.8 | 247 | 0.36M | 0 |

**(B) AlpineValley review copy** (exported Terra assets as entities: ~1000 `TerraTree_*` native-flora
glTF trees, rocks, grass clumps/patches, NEREID river; copy under KINGSTON, original untouched):

| map variant | view | frame ms | GPU ms | draws | camera tris | sun-shadow tris |
|---|---|---|---|---|---|---|
| valley (all) | near | 16.7 | 14.6 | 204 | 2.24M | 1.28M |
| valley (all) | mid | 26.3 | 33.5 | 388 | 6.64M | 7.92M |
| valley (all) | far | 49.9 (49.3–53.2) | 65.1 | 3903 | 15.47M | 7.45M |
| valley-notrees | near / mid / far | 12.9 / 15.0 / 16.7 | 11.9 / 14.0 / 14.7 | 126 / 251 / 535 | 0.45M / 1.11M / 1.60M | 0.25M / 0.92M / 1.75M |
| valley-nograss | mid / far | 27.1 / 48.6 | 37.7 / 76.5 | 357 / 1529 | 6.28M / 12.52M | 7.25M / 6.66M |
| valley-norocks | mid / far | 26.8 / 49.0 | 38.0 / 69.9 | 291 / 1843 | 6.75M / 13.01M | 7.49M / 7.07M |
| valley-terrain | near / mid / far | 11.9 / 14.2 / 15.7 | 10.3 / 14.0 / 15.0 | 44 / 67 / 113 | 0.43M / 0.59M / 1.05M | 0.23M / 0.66M / 1.28M |

### What passes
* Live TerraWeave terrain + Arbor forest + grass + Horizon stay at the editor's 10 ms cap with
  <0.8M camera triangles and ≤250 draws at every distance; images look correct near and mid
  (textured bark, botanical crowns, bent grass, shadows).
* Exported-asset scene without trees (terrain, river, rocks, grass) stays ~12–17 ms at all distances.

### What is slow
* **Exported trees as entities** are the whole cost of the review scene: 6.6M camera + 7.9M shadow
  triangles at mid, 15.5M triangles / 3903 draws / ~50 ms at far (≈20 fps). `cameraLodNodes` stays
  `[0,0,0,0]` and `lodChains` 0 for them — the exported glTF trees are drawn without a
  distance LOD/impostor chain in this path, and the shadow pass draws them in full. Removing grass or
  rocks changes nothing measurable; removing trees brings far from 50 ms to 16.7 ms. Next step:
  give exported trees their `rtsLods` chain + impostor tier (or render them through the live
  ForestScene adapter, which already has High/Medium/Low/Impostor), and cap shadow-casting tree LOD.

### What was broken (and its state)
* **FIXED — Horizon switched the whole forest off in the editor.** `view_wind horizon=true` threw
  `TerraWeave Horizon atlas memory budget` (editor preview used 64 px atlas tiles x 2 views = 2624 px
  atlas side > 2048 limit); the editor catches it and disables the forest overlay, leaving only
  grass. Fix: `TerraWeaveForestPreview::editorHorizonSettings` = High preset (48 px, 2 views,
  crown detail 3) + map ownership, validated; regression in `terraweave_preview`. Re-measured above.
* **Open — far Horizon clusters float** outside a small map: in the 192 m map the far view shows
  HLOD crown cards beyond the map edge hanging over empty sky; no proxy ground is visible under them
  in the editor preview (`terrainBeginCm` = map half size, but the distant-terrain proxy is not drawn
  under the cards in this view). Needs a look by the Horizon owner.
* **By design but surprising — editor forest disappears beyond ~77 m** without Horizon (FloraView
  `cullRangeCm = 7700 * lodScale`): the far view shows grass only (`live-forest far`: 1 draw).
* Test-setup note: a map made with `map_new` inside a project whose rules have no terrain tiles
  (the AlpineValley copy) renders black legacy ground; with `templates/wildfire` it is green. Not an
  engine bug, but `terraweave_generate` on such a project gives an unlit-looking result.

## 2. Automatic streaming for large worlds

What already existed (read from code): `TerraWeaveForestScene::streamAt` (bounded resident chunks,
async jobs), `TerraWeaveHorizonScene::update` (async region bake + GPU LRU), and `WorldScene::update`
calling both with `observers.front()`; `TerrainMutationCoordinator::publishVisible(pinnedCells)` and
`publishGrass(area)` and the one-call `publishNativeTerraWeaveFrame(...)`. **Missing:** nothing chose
the pinned terrain/collision tiles or the grass rectangle — every native host had to compute them, and
only the first observer was ever used.

Added `engine/world/TerraWeaveStreaming.h` — `TerraWeaveStreamingPlanner`:
* any number of observers (camera, players, AI, server interest points; per-observer radius and
  priority); deterministic, observer-order independent result;
* pinned tile list ordered by (priority, distance, cell) so `publishVisible`'s per-frame upload budget
  fills the ground under observers first; hard cap `maxPinnedTiles` (≤512) with `overflowTiles` report;
* enter/leave hysteresis (`hysteresisCm`) — no thrash at ring boundaries;
* grass rectangle around the primary observer, snapped, re-centred only past a dead zone;
* `publishNativeTerraWeaveFrameAuto(...)` in `TerraWeaveNativeDriver.h` = planner + existing barrier.
Tests (`terraweave_streaming`, Mac PASS): nearest-first order, exact hysteresis leave/enter steps,
multi-observer union and order independence, budget overflow, grass dead zone, validation.

Still missing (not done here): ForestScene/Horizon stream from ONE camera (`WorldScene` uses
`observers.front()`; multi-observer forest residency would need a union in `ForestScene::streamAt`);
`publishGrass` sorts the whole grass registry snapshot every call (O(N log N) for very large worlds —
needs a spatial index); `WorldScene` itself does not yet call the planner for terrain tiles (native hosts
via `publishNativeTerraWeaveFrameAuto` do). No GPU/editor run of the automatic path was made — it is
covered by CPU tests and compiled in the native smoke only.

## 3. Tree collisions

Added `engine/world/TerraWeaveTreeColliders.h` (+ `TerraWeaveTreeCollidersBox3D.h`):
* `treeTrunkShape(profile, prototypeSeed)` — trunk of the same Arbor skeleton the mesh uses
  (branch 0: base radius `trunkRadiusCm`, to the tree top); scaled by the instance `scalePermille`
  exactly like the render adapter;
* `collectTreeTrunkColliders(forest, rect, shapes, settings)` — resident, visible trees only (harvested
  / regrowing trees excluded), sorted by `ForestTreeIdentity`; settings for radius/height scale,
  min height (saplings), sink below ground;
* `TreeTrunkCollisionPublisher` — per-tile AABBs in `world::SpatialWorld` (what `FpsMotor`,
  `MotionModule`, sweep/hitscan/projectile systems query), host-reserved disjoint ID ranges, transactional,
  `slotOf(id)` to map a hit back to the tile, `retainOnly(planner.pinnedCells)`;
* `TreeTrunkBox3DPublisher` — static capsules in `physics::Scene` (create-before-destroy).
* `treeTrunkFootprint` gives RectCm blockers for navigation/placement.
Tests (Mac PASS): shape determinism, scale, sorted ids, SpatialWorld sweep hits a trunk, republish after
harvest, ID overlap/occupied/unsorted/capacity rejected without mutation, Box3D ray hits a capsule.
Not done: wiring into a running host (no game/editor uses it yet), RTS grid/navmesh obstacles from
trunks (footprints exist, adapter not written), upper-crown/branch collision (trunk only).

## 4. Studio generator panel

* Workspace ops `terraweave_forest_get` / `terraweave_forest_set` (map `extra.terraweaveForest`:
  enabled, spacing, density, max slope, seed offset; one undo step; defaults reproduce the previous
  hard-coded overlay exactly). The editor forest overlay now reads this recipe.
* Studio tool group `terraweave_generator` (map_info, terraweave_preview, terraweave_generate,
  terraweave_forest_get/set) and the panel `apps/studio-web/src/panels/generated/TerraweaveGeneratorPanel.tsx`
  (generated by `tools/studio/new_panel.py`, Studio UI kit only): landscape parameters, 9x9 preview,
  generate with refuse / overwrite / keep-edits, forest recipe. Pure form logic in
  `terraweaveGeneratorLogic.ts`.
* Verified: `tsc --noEmit` (VM) and `npm run build` (Mac) pass; Python UI contracts
  `test_design_system`, `test_new_panel`, `test_content_layout`, `test_migration_audit`, `test_web_bundle`
  pass; `test_build_config`, `test_reference_pin_zoom`, `test_scene_tools` fail identically WITHOUT this
  branch's changes (pre-existing). Studio native session test and editor op test: see below.
* Not done: the Studio Metal viewport does not draw the TerraWeave forest overlay (only the editor and
  native hosts do) — the panel says so; no macOS screenshot of the panel was taken.

### Final Mac run (after the Studio panel commit)
`npm run build` (tsc + vite) PASS; `aiverse_editor`, `aiverse_studio` (AIVERSE_BUILD_STUDIO_HTML5=ON) and
tests built in 316 s; CTest 6/6 PASS: `terraweave_ops`, `terraweave_preview`, `terraweave_streaming`,
`terraweave_streaming_physics`, `terraweave_editor` (forest recipe ops: defaults, merge, rejection without
mutation, undo), `studio_session` (panel op group, read-only get, set is a History step, invalid rejected).

### After merging origin/main (4657a4744) — commit 5f95666d5
Main removed `Project::isFps()` (no-genre rule); the forest op now uses `worldSpaceMaps()` like the
other TerraWeave ops. Full rebuild of the TerraWeave tests, `aiverse_studio_session_tests` and
`aiverse_editor` (677 s, no new warnings from this branch) and CTest **22/22 PASS**: the 19 TerraWeave
tests listed above + `terraweave_streaming`, `terraweave_streaming_physics`, `studio_session`.
The editor perf tables above were measured BEFORE this merge (on d677f8a2f + branch commits).
