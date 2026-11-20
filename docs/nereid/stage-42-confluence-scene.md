# NEREID Stage 42 — immutable ECS confluence scene and visual-flow parity

**Date:** 2026-11-19
**Branch:** `feature/nereid-water-core`, Draft PR #23.
**Base:** `a98d5f70c219627eda61c9602957cda306e47b01` (Stage 41 + correction, main already an ancestor).
**Commit:** this implementation and its handoff are one atomic commit.

## Implemented / API

Added `engine/world/NereidConfluenceScene.h`, a presentation-only `NereidConfluenceSource` with copied `WaterConfluenceComponent` policy plus generation-safe stable IDs (`packed Entity + 1`) and immutable `NereidConfluenceScene`. Host publishes a bounded snapshot with `WorldScene::setNereidConfluences(std::vector<NereidConfluenceSource>)` after completing the same ECS tick as water-surface publication.

Added `engine/world/NereidConfluenceEcsPresentation.h`:
```cpp
// At an ECS read barrier; matching NEREID water scene snapshot already prepared:
scene.setNereidConfluences(
    aiverse::world::snapshotNereidEcsConfluences(sim.world()));
```
Only live complete junctions are copied; invalid active junction policies throw, missing linked water ECS entities suppress the entire record. No persisted presentation state; no independent water/cascade physics. The scene checks matching water snapshot IDs and footprint coverage at the mouth before contributing.

`NereidConfluenceScene::flowForWater(waterId,position,base,nereidWater)` resolves upstream local current/vortex fields from read-only `NereidWaterScene` and calls the SAME fixed-point `waterConfluenceMouthContribution` and `blendWaterConfluenceContributions` as gameplay. Map-sorted junction IDs retain the first 32 eligible sources; the strictest speed cap is only among those retained. Current is blended near mouths and then passed to existing `NereidCascadeScene::flowAtConnection` for cascade precedence, matching ECS order.

Updated `engine/world/WorldScene.h` ripple advection and `engine/world/WorldScene.cpp` per-tile current-driven rapid foam so both use confluence then cascade. Explicit opt-in: without `setNereidConfluences`, prior visuals are identical.

## Files and ownership

- NEW `engine/world/NereidConfluenceScene.h`
- NEW `engine/world/NereidConfluenceEcsPresentation.h`
- MODIFIED `engine/world/WorldScene.h`
- MODIFIED `engine/world/WorldScene.cpp`
- UPDATED `docs/nereid/HANDOFF.md`, `docs/nereid/ARCHITECTURE.md`, `skills/gameplay/aiverse-nereid-water/SKILL.md`, and this standalone handoff.

Source ECS `WaterConfluenceComponent` is canonical. The snapshot uses value copies only, and must not be retained beyond publication transaction assumptions. No GPU reupload or authored river mesh modification occurs to change flow; water tile flow remains an approximation sampled at tile centers.

## Actual verification / limits

**Performed:** GitHub source inspection of the ECS kernel, water and cascade presentation paths, guarded edits and atomic branch commit.
**NOT performed:** C++ compile, CTest, GLSL/MSL shaders, scene rendering, gameplay/rollback, Box3D/PHYSIS/FPS/RTS regressions or GPU benchmark. No claim of verified parity until tests run.

Scene snapshots must use the same stable water IDs as `NereidWaterScene::surfaceForId`; in particular, hand-authored manifests with legacy IDs require explicit matching. No automatic static JSON confluence loader is added, and there is no fluid mass conservation or topological river merge/mesh stitching. Maximum 128 published sources; at most 32 eligible are sampled; 2..4 upstreams each.

## Next agent

Stage 43: avoid VB/IB reuploads when only shader-side foam/quality/optics change. Split geometry-residency key from draw-only material fields, preserving mesh vertex tint semantics and reactive presentation state. Then add automated reference/indexed/scene confluence parity fixtures and run native/GLSL/MSL/fps/rts/PHYSIS regressions during dedicated verification phase. Keep PR Draft until verified.
