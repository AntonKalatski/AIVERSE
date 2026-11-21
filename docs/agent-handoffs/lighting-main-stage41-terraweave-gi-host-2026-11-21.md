# Lighting main stage 41 — live TerraWeave tiles into dynamic GI (2026-11-21)

Branch `feature/aiv20261121_fix_release_link` (also carries two main fixes, see below).

- `terraweave::TerrainMutationCoordinator::setPatchListener({published, retired})`:
  optional observer called on the Device thread after a tile's model and
  collision were committed (with the published `TerrainPatch`) and after a
  tile left residency (also on `shutdown`). Must not throw.
- `world::GiTerraWeaveResidentBridge::attach(coordinator, giScene, gpuBridge)`
  installs that listener (exceptions counted in `listenerErrors()`), and
  `retry(giScene, gpuBridge)` re-attempts a publication GI refused (call per
  frame while `!safeToBind()`; renderer must not bind GI meanwhile).
- Test: `terraweave_review_fixes` → `terrainTilesFeedDynamicGi` (real
  coordinator + fake GI target: publish, dig with a refused then retried
  regional update, unload on move and on shutdown).

Fixes on the same branch:
- `aiverse_terraweave_preview_test` links `aiverse_world` (Release link of
  main failed: `AsyncChunkStream` undefined).
- VT + dynamic GI: `injectModelVirtualTextureSampling` anchors on the
  `opticalUv` base fetch introduced by the VIVID merge (bisected to
  9aa56dde3); `lighting_virtual_material_gi_gpu` failed since then. New
  CPU test `lighting_model_vt_shader_injection`.

Not done: an application that actually runs TerraWeave terrain with GI
(Studio / renderbench host).
