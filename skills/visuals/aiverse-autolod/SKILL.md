---
name: aiverse-autolod
description: Generate LOD chains for static glTF models in AIVERSE with meshoptimizer (aiverse_lod_simplify + tools/models/autolod.py), wire them into model packs without touching LOD0, and verify screen-size selection, dithered transitions and the frame statistics.
---

# autolod

Behaviour: `docs/autolod.md`. Tool: `tools/models/autolod.py` with the worker `aiverse_lod_simplify` (build with `-DAIVERSE_BUILD_AUTOLOD=ON`; meshoptimizer is vendored in `third_party/meshoptimizer`, tool-only, never in the runtime). Tests: `tools/models/test_autolod.py` (`AUTOLOD_WORKER=<worker>`).

1. Build the worker in your own build tree (`build/.temp/autolod`), not the shared `build/`.
2. Input: unpacked glTF with external buffers (GLB / data URI / compressed geometry: unpack first). Output: a new glTF, `.lods.bin`, a `.lods.json` report; LOD0, textures, materials, nodes and animations stay as they were. Package the derived files with their sources.
3. Levels are tried at 50 / 25 / 12.5 % of the triangles with error limits; a level that saves less than 20 % or breaks winding is dropped. Animated models are skipped.
4. In a pack install script run AutoLOD after texture sharing (AGENTS.md "Model packs: every texture once"), so a rebuild restores both; keep a hash check of LOD0.
5. Runtime: `meshes[].extras.rtsLods` picks a level by projected size; transitions dither in a band (`Renderer3D::setModelLodFadeWidth`, 0 = off). `setModelLodEnabled` / `setModelCullingEnabled` / `setModelInstancingEnabled` toggle each optimisation; frame stats `modelLodNodes`, `lodTransitionNodes`, `submittedModelTriangles`.
6. Verify with screenshots near and far and the renderbench numbers before / after; state what was not measured on a real GPU.

Runtime library: none (tool + renderer feature).
