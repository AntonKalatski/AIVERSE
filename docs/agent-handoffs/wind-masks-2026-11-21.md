# Hierarchical wind masks (2026-11-21)

**Branch:** `agent-wind-masks` (worktree `.worktrees/agent-physis-host`),
branched from `agent-physis-host` @ `054f6b546` (contains origin/main
7f3a9fbb4). Code commit: `414a568f8`. Not merged into main.

## What

Per-object, hierarchical, reusable control of how much each part sways:
mask library (one atlas per game) → per-class profiles → per-prototype rig
(16 B/part) → 4 B/vertex. See `docs/wind-masks.md` and skill
`skills/visuals/aiverse-wind-masks/SKILL.md`.

Changed outside new files:
- `engine/render/Renderer3DModels.cpp`: `SG_TEXEL<i>(x,y)` macro for vertex
  graph textures (texelFetch / `.read`). Additive.
- `engine/render/Model.cpp`: `loadModel` calls `wind::applyWindSidecar`
  (no-op without `<model>.wind.json`).
- `engine/world/TerraWeaveArborMesh.h`: `ArborVertex::owner` (default -1).
- `engine/render/TerraWeaveModelAdapter.h`: optional trailing
  `std::vector<int32_t>* vertexOwners` on `makeArborModel` /
  `makeArborBranchLodModel`.
- `tools/skills/catalog.py`, `skills/catalog.json`: new skill registered.

## Tests actually run (Linux VM, g++ 11.4, by hand, not CMake)

- PASS `wind_rig` (new): masks dedupe/JSON/atlas, hierarchy (anchored base,
  rigid attachment equality, child bend, arc length, rigid mask, flutter,
  deep chains), packing keys, model binding, node-tree rig, sidecars,
  explicit skeleton, Arbor branch-LOD and single-mesh trees.
- PASS `terraweave_arbor`, `terraweave_model`, `terraweave_forest_scene`.
- PASS glslang front-end compile of `windRigGraph()` GLSL inside the
  renderer's SG_VERTEX context: GLSL 330 core, ES 300, 450.
- `skills/catalog.py check`: 237 skills valid.

## NOT RUN / open

- MSL not compiled (no Metal toolchain); on-screen sway, shadow sway, iOS.
- macOS CMake build/CTest.
- Not yet used by `TerraWeaveForestAdapter` (forest still uses
  `canopyWindGraph`); grass keeps the single-level canopy sway.
- No editor UI for painting/inspecting masks (data via JSON sidecars).

## Next steps

1. Mac: build `aiverse_wind_rig_test` + editor, run `ctest -R '^(wind_rig|terraweave_)'`.
2. Forest: build prototypes with `makeArborBranchLodModel(..., &owners)` +
   `applyArborWind`, keep the existing per-draw `vertexParameters`.
3. Editor: rig debug view (colour parts by depth / mask value) and a
   Wind panel editing `wind/masks.json` curves + `.wind.json` rules.
