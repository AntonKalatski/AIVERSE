# NEREID Stage 43 — draw-only materials without VB/IB reupload

**Date:** 2026-11-19
**Branch:** `feature/nereid-water-core`, Draft PR #23; parent `e10c005da066d5859139760cc0ea21cb680d41bd`.
**Commit:** implementation and this handoff are in the same atomic branch commit.

## Implemented

`engine/render/NereidSurfaceResident.h` defines `sameNereidWaterVertexStyle` for the three baked vertex-color controls: deep RGB, foam RGB, and `foamStrength`. New `sameNereidSurfaceUploadKey` compares the existing mesh origin/patch/LOD/geometry revision, but ignores draw-only style. The old `sameNereidSurfaceKey` retains FULL presentation equality for consumers. `needsUpdate` and `uploadPrepared` now use upload identity. Resident `setDrawStyle` can replace draw-only parameters on a live mesh, rejecting invalid materials, absent residency or any changed baked vertex tint.

`engine/world/NereidWaterScene.h` introduces `sameNereidSceneMeshSource` so scene revision rises only for geometry/mesh/LOD/baked vertex-color edits, not water current, flow, priorities or fragment shader-only style. Update path calls `setDrawStyle` when existing mesh upload identity matches. Async staging and prepared-key comparison ignore shader-only differences; after upload, the resident receives the latest valid draw style from the scene source. No CPU mesh or GPU VB/IB churn on rapid foam colour/quality/strength, absorption, depth tint, underwater effects, opacity, specular or refraction edits.

**Not optimized:** deepColor, foamColor and foamStrength (vertex baked), body/shore geometry, mesh LOD/UV, ocean patch basis or other mesh changes. They still rebuild GPU mesh as before. External native callers should use `setDrawStyle` and keep `sourceRevision` for actual mesh source changes; deliberately incrementing it still forces an upload.

## Changed files

`engine/render/NereidSurfaceResident.h`; `engine/world/NereidWaterScene.h`; `docs/nereid/HANDOFF.md`; `docs/nereid/ARCHITECTURE.md`; `skills/gameplay/aiverse-nereid-water/SKILL.md`; and this document.

## Validation status and next steps

Performed: connected GitHub source read, exact-pattern guarded modifications, atomic branch commit and ref update. **NOT run:** native C++ compiler, CTest, GLSL or MSL, graphics device, upload-count benchmarks, save/replay, FPS/RTS/PHYSIS regressions. Do not claim performance measurements or green CI.

Risks to verify: geometry origin/LOD rebuild, shader-only edits during pending async preparation, stale worker completions after source replacement, resident draw style reflect newest source after upload, baked shoreline vertex colour changes still force rebuild. Validate both `NereidSurfaceResident::update` and `NereidWaterScene::setSurfaces/update` paths.

Specific next phase: complete any remaining optional confluence JSON authoring and scene-quality policy if required, then run validation suite for Stages 40–43 (reference/indexed confluences, ECS persistence/rollback, scene parity, GLSL+MSL, GPU upload counts, FPS/RTS/PHYSIS). Keep Draft PR unmerged until builds pass.
