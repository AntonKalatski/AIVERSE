# AIVERSE texture resource project — append-only agent handoff

**Date:** 2026-11-18 (time of execution not measured)
**Base:** `feature/universal-entity-inspector-fps` @ `dd711f5e2a7f66ed25bd7330875e6e87b7109e55`
**Feature branch:** `feature/texture-rhi-unification`
**Scope:** all texture dimensions/formats/render targets/imports; work is strictly incremental
and existing RTS/FPS resource usage must remain compatible.

Read `AGENTS.md` (mandatory handoff protocol) and `docs/texture-rhi.md`.
Before continuing, fetch the **latest branch HEAD**, preserve changes from other agents,
and use a separate implementation commit followed by a separate handoff commit after
**every** stage, including any failed/partial stage.

## Stage 1 — Public descriptor contract and fail-closed backends

**Code commits:**
- `5fa2ee589f33a243f72772801ec1bebc95f58020`: descriptor types + validation + backend guards + test + roadmap.
- `9f78184258db136b730ac2d020cb7d2b58097530`: fix default depth argument to mip-count helper.

**Paths changed:**
- `engine/gfx/Gfx.h`: new `TextureViewHandle`, `SamplerHandle`,
  `TextureDimension` (1D, 2D, 3D, 2DArray, Cube, CubeArray),
  `SamplerAddressMode`, `SamplerMipFilter`, `CompareFunction`,
  `TextureAspect`; extend `TextureDesc` with dimension/depth/arrayLayers/
  mipLevels/sampleCount; typed `TextureViewDesc`, `SamplerDesc`,
  `TextureUploadDesc`, `TextureDescError`, `validateTextureDesc`,
  `maxTextureMipLevels`, `textureDescErrorString`. Extend `gfx::Device`
  with `supportsTextureDimension`, `supportsTextureViews`,
  `createTextureView/destroyTextureView`, `createSampler/destroySampler`,
  `updateTextureRegion`, `bindTextureView` as **unsupported defaults**.
- `engine/gfx/gl/GfxGL.cpp`, `engine/gfx/metal/GfxMetal.mm`,
  `engine/gfx/vulkan/VkResources.cpp`: reject invalid/unsupported
  non-2D, array, MSAA and explicitly requested mip allocations rather than
  quietly converting those requests into a plain 2D texture.
- `tests/render/texture_contract.cpp`: CPU descriptor checks.
- `CMakeLists.txt`: `texture_contract` CTest registered for non-Web builds.
- `docs/texture-rhi.md`: implementation stages and acceptance gates.

**Compatibility:**
Existing `TextureDesc` aggregate fields are unchanged and new fields appended
with 2D-compatible defaults. The existing `gfx::TextureHandle`,
`createTexture`, `updateTexture`, `setTexture`, `PassDesc` and
`render::RenderTexture` signatures remain unchanged. Existing 2D content
should continue to use the same allocations. This step does not modify game
files, network messages, simulation state, saves or ECS components.

**Not yet implemented:**
Creating 1D/3D/array/cube GPU resources is **NOT** supported (the descriptor
contract validates them but backends intentionally return invalid handles).
The new native views/samplers/subresource upload methods also return an
invalid handle/false until subsequent implementation stages. No MRT,
array/mip render targets, MSAA, expanded formats, KTX2 transcoding, DDS
array import, EXR/HDR, async/streaming/virtual textures in this stage.

**Verification evidence (do not overstate):**
- GitHub API source reads / branch operations: successful.
- C++ compile: **NOT RUN** (no checked-out native repository in this
  session; raw.githubusercontent.com DNS failed from the execution container).
- `cmake`, `ctest`, Metal/GL/Vulkan GPU validation: **NOT RUN**.
- GitHub Actions query for the feature branch: **0 workflow runs** at
  check time. GitHub commit status state: `pending`, with no statuses.
- This is a committed *source change*, not a verified production-ready feature.

**Suggested immediate verification (next agent):**
```sh
git fetch origin
git switch feature/texture-rhi-unification
git pull --ff-only origin feature/texture-rhi-unification
cmake -S . -B build/texture-rhi -DAIVERSE_BUILD_TESTS=ON
cmake --build build/texture-rhi --target aiverse_texture_contract_test -j 4
ctest --test-dir build/texture-rhi --output-on-failure -R '^texture_contract$'
```
Then build the actual `aiverse_editor` and an RTS/FPS client with the
selected RHI to ensure pre-existing paths compile and render unchanged.
Do not report unsupported platforms as validated.

**Next implementation stage:** Stage 2 — implement native allocation and
subresource updates/binding for 1D, 3D, 2DArray, Cube, CubeArray in
Metal/GL/Vulkan, including dimension-aware GPU storage and shader sampling.
Keep `setTexture` on its legacy 2D compatibility path; use
`bindTextureView` or an explicit dimension-aware binding for new resource
types, not implicit sampler2D reinterpretation. Register per-backend
RHI tests, cap checks, and update this handoff in a separate commit.

## Stage log for future agents

Append **one section per stage** here: date, branch, code commit SHA(s),
changed files, what is implemented vs only declared, binary/API/save
compatibility, tests actually executed and results, CI links/status,
known risks, blockers, and next actionable stage. Never delete this log
or rewrite another agent's entry.

## Stage 2A — Metal sampled dimensions and subresource upload (2026-11-18)

**Code commits:** `d50a8abf1806703fbc5ba09462f9eb598dc3098a`,
`414dc591377a2d5017cfaa4e8062f5c80fd81ff4`.
**Path:** `engine/gfx/metal/GfxMetal.mm` (only).

**Implemented in source:** Metal texture storage now maps `TextureDimension` to native
`MTLTextureType1D/2D/3D/2DArray/Cube/CubeArray`; uses depth/arrayLength/mip
levels when allocating, handles contiguous uncompressed face/slice/volume data,
and accepts compressed mip payloads across all array/cube faces for GPU-compressed
2D/2DArray/Cube/CubeArray types. `updateTextureRegion` supports uncompressed
mip/array-layer/volume subregions with bounds/pitch checks. Existing
`setTexture` can bind these native resources to matching MSL texture
types; `supportsTextureDimension` returns true on the Metal backend.

**Explicitly not implemented:** `TextureView` and independent
`SamplerHandle` still return unsupported defaults. Multisample textures
and non-2D render attachments still reject creation, pending render-pass/view
work. Texture containers (DDS/KTX2) still discard layers/faces; external assets
cannot leverage these new Metal capabilities until importer stages. GL/GLES and
Vulkan still reject non-2D requests. No explicit per-slice compressed updates;
compressed sources require a full level payload. 1D/volume compression is
rejected. Metal device-family-specific limits are not queried.

**Compatibility:** Existing 2D callers keep their legacy descriptor defaults;
native 2D, atlas and render canvas paths should remain the same. No changes to
gameplay/ECS/saves/assets. The Metal implementation chooses native texture
dimensions only when explicitly requested in `TextureDesc`.

**Validation:** Source change committed. Native macOS/iOS compilation: **NOT RUN**;
GPU correctness, array/cubemap sampling, mip generation, layered texture upload
and Metal validation: **NOT RUN**. No successful CI result is asserted.

**Next actionable step:** Build `aiverse_editor` and run an explicit Metal
device test creating a `Texture3D`, `Texture2DArray`, `Cube` and `CubeArray`,
binding to matching MSL shader texture types, reading back rendered colour,
testing mip levels and all slices. Fix any Objective-C++ SDK errors before
proceeding. Then implement GL/GLES native dimensions with explicit feature
queries (GL33/GLES3/WebGL2 have different array/cube capabilities); follow
with Vulkan image/view types, layers and barriers. Keep these as individually
handed-off stages, with exact code commits and actual test outcomes.
