# NEREID Stage 39 — per-water-surface rapid foam colour

**Date:** 2026-11-19
**Branch:** `feature/nereid-water-core`
**Implementation commit:** `1d7857e68c9783d528c591229a91058115cc1864`
**Main sync:** `main=9ee6ff6fbf390bd45363bc92c838f492dfe92eee` already included in branch ancestry (0 behind prior to Stage 39). Do not modify main.
**PR:** Draft #23. Feature development prioritized; do not merge before verification.

## Implemented

- `WaterSurfaceStyle` has `rapidFoamR=.84`, `rapidFoamG=.93`, `rapidFoamB=.94`, three presentation-only linear RGB 0..1 controls. Defaults exactly match the shader tint from Stage 38.
- `NereidWaterManifest.h` accepts optional `style.rapidFoamColor: [r,g,b]` and enforces finite, normalized numeric RGB through the existing manifest parser. No NEREID ECS save schema change.
- `NereidWaterRenderer.h` checks new style fields and forwards a fourth draw parameter for RGB independent of shoreline vertex foam.
- `NereidSurfaceResident.h` and `NereidWaterScene.h` include new fields in style-difference comparisons so authoring changes cannot be dropped by snapshot and resident caching.
- `Renderer3D.h/.cpp`, `Renderer3DReflect.cpp`, and `Shaders.h` carry a new 16-byte `waterRapidColor` uniform: C++ U0, GLSL U0 and Metal U0 layouts all extend after `waterHydraulics`. Nonwater draw submissions remain zero initialized; the direct `drawNereidRippleMesh` interface has the old shade as an optional default.
- The Stage 38 GLSL and Metal animated rapid foam branches now read `waterRapidColor.rgb` instead of constants, while their speed, intensity, breakup and normals are unchanged.

Example scene JSON:
```json
{
  "nereidWater": {
    "surfaces": [{
      "id": 10, "kind": "river", "shape": "river",
      "surfaceY": 5, "bottomY": -2,
      "points": [[0,0,5],[20,0,5],[35,9,4]],
      "style": {
        "rapidFoamStrength": 0.9,
        "rapidFlowThreshold": 3.0,
        "rapidNoiseScale": 1.5,
        "rapidFoamColor": [0.76,0.9,0.94]
      }
    }]
  }
}
```

## Architecture and API

`WaterBodyComponent`, current fields and cascade transfer remain ECS-authoritative. The RGB is render-only configuration, not a component or replay input. Material state flows manifest -> immutable surface style -> resident draw -> `DrawItem` -> aligned U0 -> GLSL/MSL shader. The RGB parameter only affects a rapid-foam branch when the opt-in `rapidFoamStrength` yields intensity above zero. The non-rapid default color exactly matches Stage 38. Changes to palette do not change body geometry, simulation collision or natural-phenomenon ownership.

**Files changed in implementation commit:**
`libraries/include/aiverselib/water/NereidWaterSurfaceMesh.h`;
`engine/render/NereidWaterRenderer.h`;
`engine/render/NereidSurfaceResident.h`;
`engine/world/NereidWaterScene.h`;
`engine/world/NereidWaterManifest.h`;
`engine/render/Renderer3D.h`;
`engine/render/Renderer3D.cpp`;
`engine/render/Renderer3DReflect.cpp`;
`engine/render/Shaders.h`.

## Verification performed and deferred

Performed: GitHub source inspection, exact-pattern edit checks, atomic tree/commit/ref update on the feature branch. No local C++ repository is mounted and this environment cannot fetch the full project directly. **NOT performed:** CMake compilation, CTest, standalone native-project link, GLSL/MSL cross-compilation, in-engine runtime, visual capture, GPU timing, Box3D/PHYSIS/RTS/FPS regression. Treat code as experimental until run. In particular, the modified shared U0 layout must be cross-checked against any other shader/renderer declarations in the full project.

Recommended later validation:
```sh
cmake -S . -B build/ecs-test -DAIVERSE_BUILD_TESTS=ON
cmake --build build/ecs-test -j 4
ctest --test-dir build/ecs-test --output-on-failure -R '^(ecs_nereid_.*_test|fps|rts)$'
```
Also compile GLSL/Metal shader variants and inspect rapids before/after, both opt-out and custom RGB; verify render reflection uniforms and GPU U0 alignment.

## Limits and next agent action

- Palette is uniform per water draw/tile; per-pixel foam coloration, foam texture and bubble simulation are not added.
- The shared draw U0 is 16 bytes larger for every draw, including nonwater; test backend limits and pipeline compilation.
- Style-key changes may cause avoidable vertex/IB reupload despite this colour being shader-only. Later separate geometric and dynamic material residency keys, preserving safety for base/deep shoreline colors.
- Next **feature-first** stage: detail-quality options for whitewater and confluence current blending with deterministic, genre-free ECS policies; document any further stage and preserve Draft PR status. Test phase remains deferred until feature backlog is complete.
