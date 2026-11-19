---
name: aiverse-texture-rhi
description: Reuse AIVERSE native texture dimensions, offscreen MRT/MSAA, DDS/KTX2 imports and frame-budgeted streaming rather than generating a custom texture backend.
---

# AIVERSE Texture RHI — native agent recipe

Read `docs/texture-rhi.md` for actual backend capability differences,
`engine/gfx/Gfx.h` for the typed contract, and
`docs/agent-handoffs/texture-rhi-2026-11-18.md` for the most recent code SHA,
tests actually run and next gates. **This implementation branch is unverified:
never equate source presence with a passing GPU test.**

## Choose an existing API

| Goal | Public native C++ entry | Notes |
|---|---|---|
| 1D, 2D, 3D, array, cubemap | `gfx::TextureDesc`, `Device::createTexture` | `dimension`, `depth`, `arrayLayers`, `mipLevels` |
| Sample subresource | `TextureViewDesc`, `Device::createTextureView` + `bindTextureView` | GL33/GLES3 whole-resource identity views only |
| Custom filtering | `SamplerDesc`, `Device::createSampler` | Compare, LOD clamp, anisotropy per backend |
| Offscreen 1–4 color targets | `gfx::RenderTargetSet` | `init`/`begin`/`end`/`shutdown`, optional depth |
| Low-level mip/face render | `RenderTargetPassDesc`, `Device::beginRenderTargets` | Call `endPass()` only on successful begin |
| DDS DX10, KTX2 arrays/cubes/volumes | `parseDdsTexture`, `parseKtx2`, `uploadContainerTexture` | `CompressedImage` keeps all faces/mips |
| Optional BasisU KTX2 import | `render::parseKtx2WithTranscoder`, `render::uploadKtx2WithTranscoder` | Inject a real licensed codec; exact mip/face validation |
| Real BasisLZ/UASTC LDR | `render::makeLibktxBasisTranscoder`, `render::uploadKtx2WithLibktx` | Requires `AIVERSE_TEXTURE_WITH_LIBKTX=ON` and system Khronos libktx |
| Compressed OpenEXR 3 HDR | `render::parseOpenExrWithLibrary`, `render::loadOpenExrWithLibrary` | Requires `AIVERSE_TEXTURE_WITH_OPENEXR=ON` and system OpenEXR 3 |
| Budgeted deferred texture upload | `gfx::TextureStreamQueue` | Owns GPU texture and staged bytes; `drain()` on device thread |
| Safe coarse-to-fine sampling | `gfx::TextureMipResidency` | Only create/bind sampler when complete coarse-to-fine mips resident |
| Unified native streaming | `gfx::TextureResidentStreamer` | Owns queue+residency; gates sampling, tracks completions and retries |
| Radiance `.hdr` / limited EXR | `render::loadRadianceHdr`, `render::loadOpenExr` | Linear RGBA16F; EXR is uncompressed scanline HALF/FLOAT RGB[A] only |
| Virtual texture atlas | `gfx::VirtualTextureStreamer` | Fixed physical Texture2DArray pages, GPU page table, LRU and pinning |
| Disk page I/O worker | `gfx::VirtualTextureDiskQueue` | Deduplicated, cancelable, bounded filesystem page results |
| Disk-backed virtual atlas | `gfx::VirtualTextureFileStreamer` | Explicit page requests, render-thread pump and GPU page-table publication |
| Bounded UV visibility prefetch | `gfx::VirtualTexturePrefetchPlanner`, `prefetchVisibleVirtualPages` | Clips and deduplicates visible UV rectangles; requests coarse mips first |
| Optional GPU page feedback | `gfx::VirtualTextureFeedbackRuntime`, `VirtualTextureFeedbackDecoder` | Async linear RGBA8 readback decoded into bounded visible UV regions |
| Owned GPU feedback render target | `gfx::VirtualTextureFeedbackPass` | RGBA8 offscreen pass + async capture; app supplies opaque feedback shader |
| GPU subresource readback | `Device::readTextureAsync` | Metal/Vulkan/GL33/GLES3 source; unsupported on WebGL2 |
| Live texture inventory | `Device::textureResources()` | Optional live GPU resource snapshot; byte size is estimated |

### Example: mip-streamed Texture2DArray (native C++)

```cpp
#include "engine/gfx/TextureResidentStreamer.h"

gfx::TextureResidentStreamer stream(device);
gfx::TextureDesc desc;
desc.width = desc.height = 256;
desc.dimension = gfx::TextureDimension::Texture2DArray;
desc.arrayLayers = 4;
desc.mipLevels = 9;
const auto resource = stream.create(desc);
if (resource.valid()) {
    // Demo producer. Replace with pre-decoded packed mip/layer data from
    // your game's asset loader; avoid file I/O on the GPU render thread.
    auto ticket = stream.stageNext(resource,
        [](gfx::TextureMipRequest, int w, int h, int depth,
           std::vector<uint8_t>& out) {
            out.assign(size_t(w) * size_t(h) * size_t(depth) * 4u, 255);
            return true;
        });
    if (ticket.valid()) stream.drain();
    gfx::SamplerDesc requested, resident;
    const auto sampled = stream.sampledTexture(resource);
    if (sampled.valid() && stream.residentSampler(resource, requested, resident)) {
        // Create independent GPU sampler using the returned safe mip limits.
        // Bind it with an identity/full-resource TextureViewDesc on GL or
        // a compatible subresource view on Metal/Vulkan.
        auto sampler = device.createSampler(resident);
        if (sampler.valid()) device.destroySampler(sampler);
    }
    stream.destroy(resource);
}
```

The first request contains **one** face of the coarsest mip. Stage
remaining faces and finer mips during subsequent frames. `drain()`
submits bounded updates on the render thread; it does not load assets
from disk asynchronously. For per-face/full-volume data you must provide
a producer that fills exactly the requested packed byte count. Bind
nothing until `sampledTexture` becomes valid. The facade owns its GPU
texture handles and must be destroyed before `gfx::Device`.

### Virtual texture page streaming

Use `gfx::VirtualTextureStreamer` with `gfx::VirtualTextureStreamConfig`.
Initialize physical atlas capacity and tile dimensions, then stage a
complete guttered RGBA8/sRGB page using `stage(VirtualPageKey, bytes, size)`.
`drain()` submits bounded GPU page copies, publishes the page table
after successful completion, and only then exposes updated mappings.
`physicalAtlas()` and `pageTableTexture()` are the shader inputs.
`engine/gfx/VirtualTextureSampling.h` contains GLSL and Metal helpers.
Stale mappings are invalidated before LRU reuses a physical slot;
a failed table/upload quarantines the entire atlas. This is not hardware
sparse residency, automatic GPU feedback, or disk streaming.

`Device::readTextureAsync` returns false for unsupported source formats
or backends, and otherwise delivers a packed subresource on the graphics
owner thread in a later frame. Metal supports RGBA8/sRGB/RGBA16F;
GL33/GLES3 use PBO+fence for RGBA8/sRGB; Vulkan uses fence-completed
host staging for RGBA8/sRGB/RGBA16F. WebGL2 refuses native async
readback. None of those paths has passing GPU verification yet.

### File-backed virtual pages — native integration

Create immutable RGBA8 files under the trusted game root, e.g.
\`m2/l0/x0_y0.rgba\`, each exactly \`pageSize*pageSize*4\` bytes
including image gutters. The two configs must have matching
virtual tiles, layers, mips and physical page size:

\`\`\`cpp
#include "engine/gfx/VirtualTextureFileStreamer.h"
gfx::VirtualTextureStreamConfig atlas;
atlas.tilesX = atlas.tilesY = 256;
atlas.mipLevels = 9; atlas.virtualLayers = 1;
atlas.physicalPages = 64; atlas.physicalPageSize = 128;
gfx::VirtualDiskPageConfig disk;
disk.assetRoot = "assets/virtual_terrain";
disk.tilesX = atlas.tilesX; disk.tilesY = atlas.tilesY;
disk.mipLevels = atlas.mipLevels;
disk.virtualLayers = atlas.virtualLayers;
disk.pageSize = atlas.physicalPageSize;
gfx::VirtualTextureFileStreamer streaming(device);
if (streaming.initialize(atlas,disk)) {
    auto ticket = streaming.requestPage({8,0,0,0},true);
    // Call each frame on GPU owner thread, not on the file worker.
    streaming.pump(4,[](gfx::VirtualDiskPageTicket,
                        gfx::VirtualPageKey,bool ok,
                        const std::string& error) {
        // Respond to page success/failure in presentation state only.
    });
    streaming.shutdown();
}
\`\`\`

The I/O worker never calls gfx::Device. Pin coarsest mip
fallbacks, request missing detail pages explicitly and use
the existing GLSL/MSL virtual texture lookup snippets.
Automatic GPU-feedback-driven page requests, file packing
and real-device shader validation are still separate work.
No native GPU tests have been confirmed passing.

### Visibility-driven prefetch without a genre dependency

On the graphics/streaming owner thread, include
`engine/gfx/VirtualTexturePrefetchAdapter.h`. A game projects
its visible surfaces (terrain, object, UI atlas, etc.) to normalized
UV rectangles. The planner never depends on a camera component,
FPS/RTS mechanic or ECS authority:

```cpp
gfx::VirtualTexturePrefetchPlanner planner;
gfx::VirtualTexturePrefetchConfig prefetch;
prefetch.tilesX = atlas.tilesX;
prefetch.tilesY = atlas.tilesY;
prefetch.mipLevels = atlas.mipLevels;
prefetch.virtualLayers = atlas.virtualLayers;
prefetch.maxCandidates = 1024;
prefetch.maxExaminedPages = 4096; // includes already loaded/queued tile lookups
prefetch.maxRequestsPerFrame = 16;
if (planner.initialize(prefetch)) {
    gfx::VirtualTextureVisibleRegion visible;
    visible.u0 = .25; visible.v0 = .25;
    visible.u1 = .50; visible.v1 = .50;
    visible.desiredMip = 1;
    visible.layer = 0;
    const auto plan = gfx::prefetchVisibleVirtualPages(
        streaming, planner, {visible});
    // Plan submits disk tickets only; `pump` performs GPU uploads.
    streaming.pump(8);
}
```

`prefetchVisibleVirtualPages` skips exact resident or already
pending virtual pages, not just pages with a coarser resident
ancestor. It upgrades coarsest pinned fallbacks, respects the
planner's finite region/candidate/request budgets, and stops
on streaming-queue backpressure. It does NOT implement hardware
GPU feedback, sparse images, occlusion queries or camera projection;
the game owns UV visibility and desired mip determination.

### Owned offscreen page-ID feedback pass

`engine/gfx/VirtualTextureFeedbackPass.h` owns the generic
`RenderTargetSet` and the existing async feedback runtime.
It avoids creating a special FPS/RTS/Terra render-target manager.

```cpp
gfx::VirtualTextureFeedbackPass feedbackPass;
gfx::VirtualTextureFeedbackConfig fb;
gfx::VirtualTexturePrefetchConfig pf;
fb.tilesX = pf.tilesX = atlas.tilesX;
fb.tilesY = pf.tilesY = atlas.tilesY;
fb.mipLevels = pf.mipLevels = atlas.mipLevels;
fb.virtualLayers = pf.virtualLayers = atlas.virtualLayers;
if (feedbackPass.initialize(device, 256, 256, fb, pf)) {
    // Inside the game's graphics frame, outside other passes:
    if (feedbackPass.begin()) {
        // Bind an Opaque, single-attachment RGBA8 pipeline and draw
        // visible virtual-texture surfaces to the page-ID target.
        // GLSL/MSL helpers: engine/gfx/VirtualTextureFeedback.h
        feedbackPass.endAndCapture();
    }
    // On a later graphics frame, after beginFrame callback dispatch:
    auto dispatched = feedbackPass.submitVisiblePages(streaming);
    streaming.pump(8);
    feedbackPass.shutdown(); // before graphics Device teardown
}
```

The helper allocates one *linear RGBA8 UNORM*, single-sample
Texture2D, clears all bytes to zero, sets viewport and scissor,
and refuses another capture while the prior GPU readback is
pending or awaiting submission. It never creates a shader,
draws a camera, inserts a render graph pass or configures an
ECS entity. Keep blending, sRGB conversion, MSAA, dithering
and postprocessing disabled in the feedback shader/pipeline.
Failures in readback are reported; WebGL2 remains unsupported.

### Optional feedback-buffer-driven page requests

`engine/gfx/VirtualTextureFeedback.h` defines the exact RGBA8 page ID
encoding and GLSL/MSL helpers (`kVirtualTextureFeedbackGLSL`,
`kVirtualTextureFeedbackMSL`). Draw them in a **separate offscreen**
linear RGBA8 UNORM render target with no blending, sRGB conversion,
dithering, MSAA resolve or postprocessing. Clear unused pixels to zero.
Source key layout is 11-bit x, 11-bit y, 5-bit mip, 4-bit virtual
layer and a valid bit: maximum 2048×2048 tile grid, 16 layers.

`engine/gfx/VirtualTextureFeedbackRuntime.h` coordinates optional
native `Device::readTextureAsync` and the existing file-streamer:

```cpp
gfx::VirtualTextureFeedbackRuntime feedback;
gfx::VirtualTextureFeedbackConfig feedbackCfg;
feedbackCfg.tilesX = atlas.tilesX;
feedbackCfg.tilesY = atlas.tilesY;
feedbackCfg.mipLevels = atlas.mipLevels;
feedbackCfg.virtualLayers = atlas.virtualLayers;
gfx::VirtualTexturePrefetchConfig prefetchCfg;
prefetchCfg.tilesX = atlas.tilesX;
prefetchCfg.tilesY = atlas.tilesY;
prefetchCfg.mipLevels = atlas.mipLevels;
prefetchCfg.virtualLayers = atlas.virtualLayers;
if (feedback.initialize(device, feedbackCfg, prefetchCfg)) {
    // After the application's feedback render pass has ended:
    feedback.capture(linearRgba8FeedbackTarget);
    // On a later frame, after Device::beginFrame delivers callbacks:
    feedback.submit(streaming);
    streaming.pump(8);
    feedback.shutdown();  // before Device and streaming teardown
}
```

Do not treat `capture()` as synchronous: it can fail when the
backend does not support the requested readback, and WebGL2
currently rejects this native async path. A helper destroyed
before GPU completion drops its weak callback safely. No
camera pass, shader attachment, framebuffer allocation or
automatic render-graph node is created by this header.
The app supplies an actual feedback shader and render target.

### Optional Basis Universal codec boundary

Include `engine/render/Ktx2Transcode.h` and supply a callback of type
`render::Ktx2Transcoder` when using ETC1S/BasisLZ or UASTC KTX2.
The callback must return all mip-major slices/faces with the same
source dimensions/layers/mip chain. It must select a format the
GPU supports, or return RGBA8 for the existing CPU-upload path.
Without a codec, Basis input fails explicitly. A real **external**
Khronos `libktx` adapter now exists in `Ktx2Libktx.h`, but
requires an explicit system library build:

```sh
cmake -S . -B build/texture-rhi -DAIVERSE_TEXTURE_WITH_LIBKTX=ON
```

Call `render::uploadKtx2WithLibktx(device,encoded,options,error)`.
The adapter prefers native BC7/ASTC/ETC2/BC3 if the GPU supports
the matching linear/sRGB format; it can fall back to RGBA8.
LDR imports use the normal KTX2 container callback. For HDR UASTC,
call `render::uploadKtx2UastcHdrWithLibktx(device,encoded,error)`
instead: it requests the real `KTX_TTF_RGBA_HALF` decoder,
preserves all mips/faces/slices and imports an RGBA16F GPU
resource (if supported). This is not native BC6H/ASTC_HDR.
Real HDR codec and GPU readback tests are still required.
No codec is bundled; license/packaging and GPU fidelity
must be verified with the installed upstream package.

### Compressed OpenEXR import and Vulkan compute images

When using `render::parseOpenExrWithLibrary` from
`engine/render/ExrOpenExr.h`, enable the explicit
`AIVERSE_TEXTURE_WITH_OPENEXR=ON` CMake option and install
OpenEXR 3 first. This supports ZIP/ZIPS/PIZ and other
OpenEXR-library scanline RGBA sources, but not deep/multipart.
Without it, the existing uncompressed HALF/FLOAT parser remains.

Vulkan now implements the optional compute/storage texture
methods. `ComputeDesc.glsl` is a GLSL450 body with
`layout(set=0,binding=0,rgba8) uniform writeonly image2D image0;`
for writable output. The binding layout is storage images
0..3; sampled images 4..11; storage buffers 12..15; UBO 16.
Use `Device::supportsCompute`,
`Device::supportsStorageTexture(PixelFormat::RGBA8,TextureDimension::Texture2D)`,
and check every returned handle and bool before dispatch.
Metal already has its own MSL compute implementation; GLES3/GL33/
WebGL2 do not silently emulate compute. Actual GPU tests
remain pending.

Feedback visibility regions may be reused for up to eight
later frames if detail requests exceed the prefetch budget;
the runtime drops stale regions when a fresher GPU feedback
result arrives and never queues these into authoritative ECS.

### Correctness and compatibility

- Keep existing model material shaders' `sampler2D` resources on the
  legacy first-face path. Only dimension-aware shaders sample arrays/cubes.
- Check `Device::supports` and actual handle validity for format availability.
  `PixelFormat::BC4/BC5/EAC_R11/BC6H` is not universally supported.
- Compressed sRGB BC1/BC3/BC7, ETC2 and ASTC have native RHI variants
  for capable GPUs. Prefer hardware sRGB sampling (no shader gamma
  conversion) when the source KTX2/DDS declares it; unsupported
  combinations fall back to RGBA8_sRGB where CPU decoding exists.
  BC1_RGB keeps opaque-index semantics instead of being forced into
  BC1_RGBA. WebGL2 requires explicit compressed sRGB capability probing.
- The BC5 native GPU path returns RG only; enable
  `TextureUploadOptions.shaderReconstructsBc5NormalZ` **only** if the shader
  reconstructs Z. Otherwise keep the safe CPU-decoded normal map.
- `RenderTargetSet` is not the old paint canvas `render::RenderTexture`.
  MSAA requires a matching `PipelineDesc.sampleCount`, separate resolved
  single-sample texture and a backend supporting the requested sample count.
- The streaming queue is **deferred frame-budgeted submission**, not
  background GPU readback. It owns every `TextureHandle` it creates and
  must be destroyed before `gfx::Device`.
- Never use GPU float/resource state in authoritative ECS tick/checksum.

### Masked VT with real VRAM savings (post Stage 10G)

- Before upload, pack the Terra/Model source RGBA8 into immutable pages.
  For `AlphaMode::Mask` models, configure
  `render::configureModelVirtualMaskedFallback(model,image,archive,...)`
  or `configureTerraWeavePackedMaskedImageFallback(...)`, then
  `render::uploadModel(dev,model)`. This creates a downsampled RGBA8
  `Image::tex` plus a **full-resolution R8** `Image::virtualAlphaMask`
  with the source image's wrap/filter/mip settings.
- `ModelShaders.h` uses the `VIRTUAL_ALPHA_MASK` variant and
  `tex15.r` for full-resolution alpha clipping in colour, shadow,
  selection and LOD paths. Rigid feedback uses the red-channel
  `VirtualTextureFeedbackProgram` variant; deformed feedback
  selects tex15.r through its U6 policy. Never downsample a cutout
  and then test alpha from the coarse RGBA8 fallback.
- This optimization is **off by default** and excludes Shader Graph,
  blended/transparent materials, team modes dependent on base alpha,
  non-base uses of the same image and images already uploaded/cached.
  Audit dynamic `ModelDraw::materials` overrides separately.
  GPU ICB is conservatively disabled for scenes requiring external R8
  mask bindings because the ICB command ABI does not encode tex15.
- `VirtualFallbackSavings` estimates base-level source/fallback/R8
  bytes, not measured VRAM. The full source image is stored in the
  immutable disk page archive. Model::releaseModel releases both
  model-owned GPU handles after their safe lifetime.
- Deferred verification must cover exact alpha silhouette across
  main/shadow/selection/feedback (GL, Metal, Vulkan), R8 mip
  filtering, sampler address semantics and resource lifetime.
  No native build/GPU test was run for this feature stage.

### Experimental VT batch grouping on Metal

- Use `ModelVirtualTextureBinding` as an exact GPU resource
  identity: physical atlas, page-table handle, virtual layer and
  gutter. The `modelVirtualTextureBindingHash` is only a fast
  grouping prefilter; `compatibleModelVirtualTextureBinding`
  must also pass for every group member. A Horizon archive
  revision switch invalidates existing GPU batch signatures.
- CPU MDI and compute-encoded Metal ICB can share VT fragment
  resources **only** inside a homogeneous group. Each GPU ICB
  command keeps its own transform/indices; all commands inherit
  one group atlas/page-table/U6 and source material binding.
  This is not yet a per-command heterogeneous VT material table.
- Metal PBR argument buffer includes optional VT texture slots
  13/14 and R8 alpha slot 15 when the corresponding shader
  permutation is enabled. VT uses the standard material sampler
  s1; the page-table lookup reads an integer texel and needs no
  extra sampler. Never bind empty/surplus sampler IDs 27/29.
- GPU ICB groups capture external textures at preparation and
  compare current handles/parameters just before execute.
  A changed Horizon revision/mask rejects ICB and falls back
  to CPU submission. Unsupported skinned/morphed/graph/animated
  materials remain on CPU. Full Metal PBR ICB remains
  **experimental opt-in**; never enable by default.
- User deferred native/CTest/GPU tests. Do not claim a speedup
  or visual correctness until targeted Metal ICB and CPU MDI
  shader compilation, output-parity and perf data exist.

### Multiple independent Terra VT domains

- Use `render::TerraWeaveVirtualAtlasDomains` for separate Horizon,
  Forest and impostor atlases. Register stable numeric DomainIds,
  each with a matching immutable versioned page archive and its own
  `TerraWeaveVirtualAtlasSession` / GPU feedback pass.
- **Never decode combined 32-bit page IDs from two archives in one
  GPU readback.** The codec has tile/mip/layer but no domain index.
  The manager filters one domain per capture, preserves occluder
  depth from all other models, and submits readback only to its
  matching streamer via `pumpActive()`. Other domains can use
  `requestPage(domain,key,pin)` for CPU visibility prefetch.
- `beginFrame()` clears previous model/material pointer registrations;
  call `registerDraw` before ordinary `Renderer3D::drawModel`.
  Use `setLiveDomainLookup` to resolve private Horizon models,
  using their real object/revision identity, not their path alone.
- `promote(domain)` requires the candidate fallback pages resident,
  a compatible new feedback decoder, and no pending/unconsumed
  domain readback. The application controls the GPU fence before
  `retireCompletedGpuWork(domain)`. Call `detach()` before
  destroying Renderer3D/feedback objects; the manager's callbacks
  capture its address.
- The manager installs `Renderer3D::setVirtualTextureFeedbackScheduler`
  and uses the original shared model feedback shader programs.
  Independent domains can sample their own VT atlas in one colour
  scene; GPU ICB/MDI still require homogeneity within each group.
- Historical code fix: `injectModelVirtualTextureSampling` must match
  current `ModelShaders.h` Metal signature, including the optional
  R8 `VIRTUAL_ALPHA_MASK` arguments after `GI_FARGS`.
  Do not restore the obsolete `GI_FARGS) {` assumption.
- New functionality is committed but uncompiled; compile C++,
  validate Metal shaders and GPU readback routing during the
  explicitly deferred test phase, not before the user requests it.

### Verification gates — don't invent passes

```sh
cmake -S . -B build/texture-rhi -DAIVERSE_BUILD_TESTS=ON
cmake --build build/texture-rhi --target \
  aiverse_texture_contract_test aiverse_render_target_set_test \
  aiverse_texture_streaming_test aiverse_texture_mip_residency_test \
  aiverse_texture_resident_streamer_test aiverse_virtual_texture_pages_test \
  aiverse_virtual_texture_page_table_test aiverse_virtual_texture_streamer_test \
  aiverse_virtual_texture_disk_queue_test aiverse_virtual_texture_file_streamer_test \
  aiverse_virtual_texture_prefetch_test aiverse_virtual_texture_feedback_test \
  aiverse_virtual_texture_feedback_pass_test \
  aiverse_hdr_texture_test aiverse_exr_texture_test \
  aiverse_ktx2_layers_test aiverse_ktx2_transcode_test \
  aiverse_dds_layers_test aiverse_texture_asset_test
ctest --test-dir build/texture-rhi --output-on-failure \
  -R '^(texture_contract|render_target_set|texture_streaming|texture_mip_residency|texture_resident_streamer|virtual_texture_pages|virtual_texture_page_table|virtual_texture_streamer|virtual_texture_disk_queue|virtual_texture_file_streamer|virtual_texture_prefetch|virtual_texture_feedback|virtual_texture_feedback_pass|hdr_texture|exr_texture|ktx2_layers|ktx2_transcode|dds_layers|texture_asset)$'
```

Build the editor and native client to catch API/layout regressions.
Run `vulkan_backend` on a real Vulkan device or Lavapipe **and inspect its
output for skipped execution**. Metal, GL33, GLES3, WebGL2 and GPU readback
require their own real device build/validation. Record each real test
command and result in the handoff after every stage; never claim passing
CI if it remains queued or cancelled.
