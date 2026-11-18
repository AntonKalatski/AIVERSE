# AIVERSE texture resource unification — implementation stages

Base: `feature/universal-entity-inspector-fps` at `dd711f5e2a7f66ed25bd7330875e6e87b7109e55`.
Implementation branch: `feature/texture-rhi-unification`.

## Status — staged source implementation (not GPU-validated)

The append-only stage log covers substantial source work through 7M
(including stages 4R–4W, 5I–5O, 7A–7M and 8A). Stage numbers track feature
areas rather than chronological order. **No native C++ build,
GPU validation or CTest result has been confirmed passing for these changes.**
Do not call the entire texture stack production-ready merely because a C++ method exists.
Read the append-only `docs/agent-handoffs/texture-rhi-2026-11-18.md` for per-stage
SHA, unverified platforms and the next exact task.

- **Dimensions:** Metal/Vulkan have native sampled 1D/2D/3D/2DArray/Cube/CubeArray
  paths; the portable GL33/GLES3 subset has 2D/3D/2DArray/Cube, not 1D/CubeArray.
- **Texture views/samplers:** Metal and Vulkan have subresource views and independent
  samplers; GL has independent samplers but only *whole-resource identity* views.
- **Offscreen passes and MRT:** `RenderTargetPassDesc` selects mips/layers and
  1–4 color attachments, each with its own clear operation, plus optional depth.
  Metal, GL33/GLES3 and Vulkan have source implementations for 1–4
  color outputs, including mixed-format MRT with per-slot pipeline checks. Metal and Vulkan graphics pipelines expose explicit
  color output counts and additional per-slot formats. GLSL/MSL shaders must
  explicitly write the corresponding fragment output locations. Native
  cross-backend GPU validation is still outstanding.
- **MSAA:** the shared descriptor/resolve contract and Metal allocation, pipeline
  sample counts and per-color resolve operations are implemented in source.
  GL uses multisample renderbuffers with per-color framebuffer blit resolves;
  Vulkan has multisample images and renderpass resolve attachments. None
  of these paths has a confirmed passing cross-backend GPU result. Tests in `tests/render/vulkan_tests.cpp` include
  MRT clear/output and nonzero mip/layer rendering, but remain unexecuted.
- **Reusable native owner:** `engine/gfx/RenderTargetSet.h` provides a
  genre-agnostic owner of color/depth targets and optional MSAA resolve targets.
  `begin`/`end` wrap an explicit offscreen pass; allocation failure rolls
  back all created handles. `tests/render/render_target_set.cpp` is a CPU-only
  mock-device lifetime and rollback test registered as `render_target_set`.
  This is separate from the existing Lua `render::RenderTexture` paint canvas.
- **Import:** KTX2 and the new `DdsTexture.h::parseDdsTexture` both preserve
  array layers, cube faces and volume slices in a shared `CompressedImage`
  representation. DDS surface-major mip chains are reordered to mip-major.
  `TextureAsset.h::uploadContainerTexture` converts either format to
  GPU resources (or CPU RGBA8 fallback) with authored mipmaps, while
  `uploadKtx2Image`/`uploadDdsImage` remain convenience aliases. Existing
  model `sampler2D` imports intentionally keep their first 2D slice.
  A declared Texture3D stays a 3D resource even when the source volume
  contains only one depth slice (KTX2 pixelDepth>0 or DDS volume dimension).
  When native BC allocation fails, the RGBA8 CPU fallback clears compressed
  mip pointers/count and retains the authored mip chain; cube-array fallback
  also has 12-face regression coverage. Malformed layer-count overflow and
  contradictory 1D/3D metadata are rejected before resource creation.
  These are source-level fixes, not GPU-certified behavior.
  HDR RGBE and a limited uncompressed single-part scanline OpenEXR
  HALF/FLOAT RGB(A) importer are now available in native C++; BC6H signed
  and unsigned native HDR compression is mapped by backend. Complete EXR
  compression (ZIP/PIZ/etc.), DDS signed/exotic formats, non-tight pitch,
  BasisLZ/UASTC transcoding and all compressed-sRGB variants remain unavailable.
- **Compressed formats:** source-level native mapping now includes BC4,
  BC5 (RGTC) and EAC_R11 across Metal/GL/Vulkan, subject to GPU capability.
  Shared block-size helpers reject missing/truncated mip and face payloads
  before GPU allocation, and unsupported compressed formats fail closed.
  For BC5 normal maps, native GPU sampling produces RG without reconstructed
  blue normal Z; the default generic importer therefore uses CPU RGBA
  reconstruction. Set `TextureUploadOptions.shaderReconstructsBc5NormalZ=true`
  only when the consuming shader reconstructs tangent-space Z.
- **Typed data formats:** `RG8`, `R16F`, `RG16F`, `R32F`,
  `RG32F` are additionally mapped in Metal/GL/Vulkan, with shared
  CPU texel byte strides and subresource upload paths. These formats
  are currently **sample-only**; render target requests and unverified
  automatic mip generation are rejected. Explicit `mipLevels` and
  subresource updates can be used to upload authored mips. Float32
  sampled data conservatively uses nearest filtering on Metal/GLES3.


| Stage | Deliverable | Acceptance |
|---|---|---|
| 1 | Public TextureDimension/TextureDesc/TextureViewDesc/SamplerDesc/TextureUploadDesc, validation and backend fail-closed | Header compiles, descriptor tests pass, existing texture rendering unchanged |
| 2 | Native Metal + GL/GLES + Vulkan 1D/2DArray/3D/Cube/CubeArray allocation, upload per subresource, sampled bind | Real per-backend device tests; shader sampling on every requested dimension |
| 3 | Native views and independent sampler objects with matching bindings | Create/destroy/bind view, anisotropic and compare samplers; preserve existing setTexture |
| 4 | RenderTexture abstraction: layer/mip targets, MRT, depth, MSAA/resolve, storage images | Render pass and readback test for each backend; explicit resource transitions |
| 5 | Additional typed pixel formats (R/RG float/int, BC4/5/6H), full sRGB and HDR/EXR | Format capability checks and cross-platform fidelity tests |
| 6 | Complete KTX2 and DDS array/cubemap/3D payloads, transcoding, import rules | Container roundtrip, bounds/fuzz tests, material/glTF integration |
| 7 | Async texture upload/readback, streaming/budget/residency, optional virtual textures | Resource-lifetime stress tests, no read-after-free, deterministic fallback |
| 8 | Editor resource inspection + feature tests and docs | GPU validation (Metal/Vulkan/GL), mobile/web compatibility gate, README translations |

## Native streaming and HDR import — source-only additions

- `engine/gfx/TextureStreaming.h`: owned GPU texture allocation, deep-copied
  CPU staging, FIFO per-frame byte/update limits and GPU completion callbacks.
- `engine/gfx/TextureMipResidency.h`: generation-safe coarse-to-fine residency
  tracking per mip and array/cube face; no sampler until every coarsest face
  completes; clamp LOD to contiguous populated levels.
- `engine/gfx/TextureResidentStreamer.h`: combines both APIs into a
  `gfx::TextureResidentStreamer` native facade. `stageNext` obtains packed
  mip/layer/volume bytes from a caller-provided producer, and `drain`
  submits them on the render/device thread. `sampledTexture` never exposes
  incomplete or failed resources. `destroy` cancels pending uploads.
- `engine/render/HdrTexture.h`: `parseRadianceHdr` / `loadRadianceHdr`
  for Radiance RGBE in linear RGBA16F.
- `engine/render/ExrTexture.h`: `parseOpenExr` / `loadOpenExr`
  for single-part uncompressed scanline OpenEXR with HALFs and FLOATs.
  Compressed/tiled/deep/multipart EXR is deliberately rejected.
- `BC6H_UFloat` and `BC6H_SFloat` preserve signed/unsigned HDR block
  payloads for DDS/KTX2 and map them to capable Metal/GL/Vulkan hardware.

**GPU readback:** `Device::readTextureAsync` is an opt-in mip/face
readback API. Metal uses shared-buffer blit, GL33/GLES3 use PBO+
nonblocking fence polling, and Vulkan uses host-coherent staging
with completed frame serials. All accepted callbacks run on the
device owner thread in later frames. WebGL2 explicitly refuses
native async readback. No actual GPU backend readback tests have
completed yet.

**Virtual textures:** `gfx::VirtualTextureStreamer` composes the
`VirtualTexturePageCache`, `VirtualTexturePageTable` and GPU staging
queue. It allocates a fixed-size Texture2DArray physical atlas and
RGBA8 page table, invalidates old mappings before LRU reuse, and
publishes uploaded pages only after successful transfer. The
`VirtualTextureSampling.h` GLSL/MSL helpers expect pre-guttered
physical pages. This is not a sparse image API, feedback pass or
background disk worker; these remain separate work.

**VRAM caveat:** Ordinary streaming allocates the full texture mip chain
up front; it budgets and defers *uploads* rather than physically releasing
unresident mip memory. Real sparse/virtual textures with GPU page tables,
feedback and physical-page eviction are **not implemented**.

**Testing caveat:** Headers and test fixtures have been committed, but
native compilation, CTest, API validation and GPU sampling remain
**NOT RUN**. Keep PR #21 Draft until all verification gates pass.

## Current compatibility constraints

- `gfx::TextureDesc` and `PipelineDesc` new fields are appended with legacy 2D
  and 1x-sample defaults. Existing `PassDesc`, `beginPass`, `setTexture`
  and `render::RenderTexture` remain available.
- A successful `beginRenderTargets` is ended with `endPass()`. It is offscreen
  only. Check its boolean return before issuing draw commands; unsupported
  MRT/resolves must fail explicitly, never downgrade.
- Render-target textures need `renderTarget=true`; color/depth attachments must
  have matching extent and sample counts at the requested mip.
- Metal MSAA pipelines explicitly set `PipelineDesc.sampleCount` to match
  multisample attachments; default 1x pipelines cannot draw into MSAA targets.
- New GPU texture handles are presentation state only, not ECS-authoritative.
  No changes to serialization, game saves or replication.
- **Known missing pieces:** mixed-format MRT and independent blend policies,
  GL/Vulkan MSAA creation/resolve, virtual/streamed textures, integer/packed
  formats, typed color targets, Basis transcoding, exotic DDS variants,
  HDR/EXR and real-device validation.
- The old `TextureCache` and `render::RenderTexture` still use 2D-only paths.

## Example: selected array layer and mip

```cpp
gfx::TextureDesc tex;
tex.width = 512;
tex.height = 512;
tex.dimension = gfx::TextureDimension::Texture2DArray;
tex.arrayLayers = 4;
tex.mipLevels = 3;
tex.renderTarget = true;
gfx::TextureHandle target = device.createTexture(tex);

gfx::RenderTargetPassDesc pass;
pass.colorCount = 1;
pass.colors[0].texture = target;
pass.colors[0].mipLevel = 1;   // 256x256
pass.colors[0].arrayLayer = 2;
pass.colors[0].clearColor[3] = 1.0f;
if (device.beginRenderTargets(pass)) {
    // Set a compatible pipeline and issue draw calls.
    device.endPass();
}
device.destroyTexture(target);
```

The client must keep the texture alive until all GPU work completes and must
never sample a subresource while simultaneously writing to that subresource.
Use a matching `TextureViewDesc` and independent sampler for post-pass reads
where supported. The code is a usage example, not a claim of verified GPU
execution on this branch.

## Native offscreen render target owner

```cpp
#include "engine/gfx/RenderTargetSet.h"

gfx::RenderTargetSet targets;
gfx::RenderTargetSet::Config cfg;
cfg.width = 1024;
cfg.height = 512;
cfg.colorCount = 2;                  // two same-format color targets
cfg.depth = true;
cfg.mipLevels = 1;
if (targets.init(device, cfg)) {
    if (targets.begin(0, 0, true, true, {0, 0, 0, 1})) {
        // Use a matching PipelineDesc.colorAttachmentCount=2 and a shader
        // writing both fragment color outputs at locations 0 and 1.
        targets.end();
    }
    // Sample via targets.sampledColor(0/1) in later render passes.
    targets.shutdown();  // required before the Device is destroyed
}
```

For MSAA, choose `cfg.sampleCount = 4`; the owner allocates a matching
single-sample resolve target per color. If the backend cannot implement
the selected render pass, `targets.begin` returns false, not a fallback.
Always check `init` and `begin` results. These APIs are under native
verification and should not be assumed to run on every device.

## KTX2 arrays, cubemaps and 3D volumes

```cpp
#include "engine/render/TextureCodec.h"
#include "engine/render/TextureAsset.h"

// ktx2Bytes are the encoded KTX2 container bytes read by the caller.
aiverse::render::CompressedImage source;
std::string error;
if (aiverse::render::parseKtx2(ktx2Bytes, source, error)) {
    aiverse::render::TextureUploadOptions opt;
    // For native compressed sRGB GPU paths ONLY when the material shader
    // explicitly decodes sRGB; otherwise use the RGBA8_sRGB CPU fallback.
    opt.trustShaderSrgbDecode = false;
    auto tex = aiverse::render::uploadKtx2Image(device, source, opt, error);
    if (tex.valid()) {
        // Bind only with a shader sampler matching the imported dimension.
        // Destroy the texture when the consuming resource ends its lifetime.
        device.destroyTexture(tex);
    }
}
```

`CompressedImage::levels` keeps all faces/layers/slices for every mip.
`decodeBlocksSlice(source, level, index, rgba)` retrieves a specified
2D slice for CPU processing. The generic uploader does **not**
automatically convert a glTF material's existing 2D sampler into an
array/cubemap shader. Native SDK builds and GPU tests are still pending.

## DDS DX10 arrays/cubes/volumes

```cpp
#include "engine/render/DdsTexture.h"
#include "engine/render/TextureAsset.h"

aiverse::render::CompressedImage dds;
std::string err;
if (aiverse::render::parseDdsTexture(ddsBytes, dds, err)) {
    aiverse::render::TextureUploadOptions options;
    // BC5 default: CPU reconstructs normal Z into RGBA8.
    // Opt in to native BC5 ONLY if the texture shader reconstructs Z.
    options.shaderReconstructsBc5NormalZ = false;
    auto handle = aiverse::render::uploadDdsImage(device, dds, options, err);
    if (handle.valid()) {
        // Bind a matching shader sampler; release after GPU usage ends.
        device.destroyTexture(handle);
    }
}
```

The old `decodeDds` material adapter still reads the first face and uses
the 2D model-shader path. The new `parseDdsTexture` is the separate
dimension-aware container API; it intentionally does not support every
historical DDS pixel format. DDS layout is reordered from per-surface
mip chains into complete mip levels for the RHI. No GPU tests have been
run on this branch.

## Native build and targeted tests (pending execution)

```sh
git switch feature/texture-rhi-unification
git pull --ff-only origin feature/texture-rhi-unification
cmake -S . -B build/texture-rhi -DAIVERSE_BUILD_TESTS=ON
cmake --build build/texture-rhi --target \
  aiverse_texture_contract_test aiverse_render_target_set_test \
  aiverse_ktx2_layers_test aiverse_dds_layers_test \
  aiverse_texture_asset_test aiverse_texture_streaming_test \
  aiverse_texture_mip_residency_test aiverse_texture_resident_streamer_test \
  aiverse_virtual_texture_pages_test aiverse_virtual_texture_page_table_test \
  aiverse_virtual_texture_streamer_test aiverse_hdr_texture_test \
  aiverse_exr_texture_test
ctest --test-dir build/texture-rhi --output-on-failure \
  -R '^(texture_contract|render_target_set|ktx2_layers|dds_layers|texture_asset|texture_streaming|texture_mip_residency|texture_resident_streamer|virtual_texture_pages|virtual_texture_page_table|virtual_texture_streamer|hdr_texture|exr_texture)
```

On Vulkan-enabled Linux/Windows hosts, also build
`aiverse_vulkan_tests` and run `ctest -R '^vulkan_backend$'` with
a working Vulkan driver (Mesa Lavapipe is acceptable). Distinguish
real GPU execution from a driver's-unavailable skip. macOS Metal,
Windows GL33, Android GLES3 and WebGL2 require separate builds
and actual visual/capture tests. **None of these results is claimed
as passed here.**

## References and handoff

- `engine/gfx/Gfx.h` — public resource contract and descriptor validation
- `engine/gfx/gl/GfxGL.cpp`, `engine/gfx/metal/GfxMetal.mm`,
  `engine/gfx/vulkan/VkResources.cpp` — fail-closed allocation until backend support
- `tests/render/texture_contract.cpp` — CPU-only descriptor checks
- The append-only stage log is `docs/agent-handoffs/texture-rhi-2026-11-18.md`.

Keep code stage commits separate from handoff commits, record exact SHA and test statuses
in the handoff **after every stage**, and preserve upstream inspector/ECS changes.

```

On Vulkan-enabled Linux/Windows hosts, also build
`aiverse_vulkan_tests` and run `ctest -R '^vulkan_backend$'` with
a working Vulkan driver (Mesa Lavapipe is acceptable). Distinguish
real GPU execution from a driver's-unavailable skip. macOS Metal,
Windows GL33, Android GLES3 and WebGL2 require separate builds
and actual visual/capture tests. **None of these results is claimed
as passed here.**

## References and handoff

- `engine/gfx/Gfx.h` — public resource contract and descriptor validation
- `engine/gfx/gl/GfxGL.cpp`, `engine/gfx/metal/GfxMetal.mm`,
  `engine/gfx/vulkan/VkResources.cpp` — fail-closed allocation until backend support
- `tests/render/texture_contract.cpp` — CPU-only descriptor checks
- The append-only stage log is `docs/agent-handoffs/texture-rhi-2026-11-18.md`.

Keep code stage commits separate from handoff commits, record exact SHA and test statuses
in the handoff **after every stage**, and preserve upstream inspector/ECS changes.
