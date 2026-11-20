# NEREID Stage 44 — static confluence JSON authoring and strict linkage

**Date:** 2026-11-19.
**Branch:** `feature/nereid-water-core` (Draft PR #23).
**Base/parent:** `804d232c7840ddb17cf6364d600803c4a686406b`. At stage start, `main=9ee6ff6fbf390bd45363bc92c838f492dfe92eee` is an ancestor of the water branch (0 behind).
**Implementation commit:** this document and code are in the same atomic commit; inspect branch history.

## User-facing JSON

```json
{
  "nereidWater": {
    "surfaces": [
      {"id":11,"kind":"river","shape":"box","center":[-6,5,0],"halfExtents":[12,6],"surfaceY":5,"bottomY":-2,"flow":[2,0,0]},
      {"id":12,"kind":"river","shape":"box","center":[6,5,0],"halfExtents":[12,6],"surfaceY":5,"bottomY":-2,"flow":[0,0,2]},
      {"id":13,"kind":"river","shape":"box","center":[0,5,8],"halfExtents":[12,12],"surfaceY":5,"bottomY":-2,"flow":[1,0,1]}
    ]
  },
  "nereidConfluences": {
    "sources": [{
      "id":101,
      "inletWaterIds":[11,12],
      "outletWaterId":13,
      "mouth":[0,5,0],
      "radius":8,
      "blend":0.75,
      "speedLimit":30,
      "enabled":true
    }]
  }
}
```

The mouth must be inside the X/Z footprint of **all** linked bodies. Each water body must have a stable, explicit `nereidWater.surfaces[].id`: synthetic array-index water IDs are intentionally not linkable. Up to 128 author-defined confluences, each with 2–4 distinct inlets and one distinct outlet. Confluence IDs and water IDs use positive integer scene IDs (1..2147483647). Invalid types, duplicates, out-of-range parameters, unknown water IDs, self-links, and mouths outside any source footprint fail parsing with `JsonError`. Disabled source definitions are validated but omitted from the active visual scene.

## Architecture and code

- **NEW** `engine/world/NereidConfluenceManifest.h`: `parseNereidConfluenceManifest(root,water.surfaces)` creates read-only `NereidConfluenceSource` with authoritative-looking Fixed flow configuration but **no fake ECS Entity IDs**. Water links are explicit stable presentation IDs.
- `libraries/include/aiverselib/water/NereidConfluences.h`: separate `validWaterConfluenceFlowPolicy` (link-independent numeric policy) from `validWaterConfluence` (full Entity-link validator for authoritative ECS). Existing `nereid.confluence` v1 archive schema and native caller API unchanged.
- `libraries/include/aiverselib/water/NereidConfluenceFlow.h`: shared pure `waterConfluenceMouthContribution` validates only flow policy. ECS `applyWaterConfluenceFlow` already checks the full Entity links first.
- `engine/world/NereidConfluenceScene.h`: read-only scene validation checks stable scene water links separately from the shared flow parameters. ECS-origin snapshots continue to carry true Entity refs; JSON-origin sources leave Entity link members null.
- `engine/world/WorldScene.cpp`: parse static confluences once during WorldScene construction **after** the water manifest; register via existing `NereidConfluenceScene::setConfluences`. Current-driven rapid shading and ripple flow already use Stage 42 confluence -> cascade ordering, so no new GPU pass or ECS mutation.

Read-only scene snapshots supplied later through `WorldScene::setNereidConfluences` override static manifest sources. No JSON ECS simulation loader is implied: native gameplay must still create `WaterConfluenceComponent` on actual Entity IDs or load its own authoritative data.

## Handoff / actual verification

**Performed:** GitHub inspection of real scene/manifest/Fixed APIs, guarded source edits, remote atomic commit and branch ref update.
**NOT performed:** local C++ compile, CTest, GLSL/MSL compilation, GPU render/capture, scene run, FPS/RTS/PHYSIS/Box3D regression, save/replay parity. The source is experimental until verified.

**Known limits:** This does not weld river surface meshes, solve hydraulic mass balance, infer junctions from terrain or change water physics. It is author-controlled local flow blending and depends on consistent mouth position and water IDs. Visual field is sampled per tile as before. Static JSON and ECS snapshots are intentionally separate sources; do not mix encoded Entity IDs and arbitrary scene IDs.

**Next stage:** begin deferred validation, starting with a small C++ manifest parse test (valid multi-inlet, disabled, unknown ID, duplicate, invalid mouth), matching reference/indexed ECS query parity and native persistence/rollback, then GLSL/MSL + GPU uploads and FPS/RTS/PHYSIS regression. After actual test failures are fixed, update this handoff and only then consider PR readiness.
