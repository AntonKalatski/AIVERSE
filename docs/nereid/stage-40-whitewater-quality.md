# NEREID Stage 40 — whitewater quality tiers

**Date:** 2026-11-19
**Branch:** `feature/nereid-water-core` (Draft PR #23)
**Starting parent:** `0417182ca16e01f8e6702224a85825b2f38f6f9c`; `main` was already included and need not be merged.
**Commit:** implementation and handoff share one atomic commit (inspect git history).

## Implemented

`WaterRapidDetail` is a presentation-only uint8 enum `{ Low=0, Medium=1, High=2 }` in `NereidWaterSurfaceMesh.h`. `WaterSurfaceStyle::rapidDetail` defaults to Medium, preserving Stage 39 behaviour. `NereidWaterManifest.h` accepts scene JSON `style.rapidDetail` as `"low"`, `"medium"`, `"high"`; invalid types/values throw JsonError. `NereidWaterRenderer.h` checks enum range and packs its numeric value in existing `waterRapidColor.a`; GLSL and Metal share exact quality branches. Low uses coherent directional streak foam and skips perturbing normals; Medium uses previous breakup/eddy math; High adds cross-flow micro-detail and normal variation. No U0 growth, resource allocation, texture, extra pass, or simulation API change. `NereidSurfaceResident.h` and `NereidWaterScene.h` include quality in style comparisons.

Example:
```json
{
  "nereidWater": {
    "surfaces": [{
      "id": 10, "kind": "river", "shape": "river",
      "surfaceY": 5, "bottomY": -2,
      "points": [[0,0,5],[20,0,5],[35,9,4]],
      "style": {
        "rapidFoamStrength": 0.85,
        "rapidFlowThreshold": 3,
        "rapidFoamColor": [0.76,0.9,0.94],
        "rapidDetail": "high"
      }
    }]
  }
}
```

## Files

- `libraries/include/aiverselib/water/NereidWaterSurfaceMesh.h`
- `engine/render/NereidWaterRenderer.h`
- `engine/render/NereidSurfaceResident.h`
- `engine/world/NereidWaterScene.h`
- `engine/world/NereidWaterManifest.h`
- `engine/render/Shaders.h`
- `docs/nereid/ARCHITECTURE.md`, `docs/nereid/HANDOFF.md`, `skills/gameplay/aiverse-nereid-water/SKILL.md` and this handoff.

## Verification and limitations

**Actually completed:** GitHub source reads, guarded code edits to both shader variants, atomic write to connected repository. **Not executed:** CMake compile, CTest, GLSL or Metal compilation, GPU captures/profiling, real native game, FPS/RTS/PHYSIS regression. No test pass is claimed. GPU driver may flatten dynamic branches; low-power savings require measurement. Stage 40 affects visual shader only, never ECS water samples, buoyancy or collision. Style changes still invalidate mesh residency, even shader-only quality; optimize later.

## Next feature stage

Stage 41: optional deterministic fixed-point confluence current blending across explicitly linked water bodies, same policy for reference/indexed samples and read-only scene presentation. No automatic geometry topology inference. Integrate with cascade blending order, cap contributors, preserve generation-safe entity ids; maintain a per-stage handoff. Following stage: avoid mesh reuploads on draw-only material changes, then implement/execute full verification. Keep Draft PR unmerged until CI validation.
