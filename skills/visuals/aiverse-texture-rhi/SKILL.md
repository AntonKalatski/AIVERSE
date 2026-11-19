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

- In multi-domain mode call
  `TerraWeaveVirtualTextureFeedback::initializeForDomains(dev,renderer,
  depthAttachment=true)` instead of allocating a dummy legacy feedback
  target. All domain feedback passes must have the same depth mode as
  the installed model feedback programs. The manager validates this
  when attaching and adding domains.
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

### Explicit material Shader Graph VT node

- `sampleVirtualTexture` is a fragment-only, material-only graph
  op with one UV input. It compiles to `SG_SAMPLE_VIRTUAL(uv)`,
  and may specify `srgb:true` for the graph compiler's normal
  sRGB-to-linear conversion. It is not a generic graph texture
  path, and should not be generated for postfx/particles/vertex.
- `graph::Program::usesVirtualTexture` tells the model renderer
  to request an opt-in `ModelVirtualTextureBinding`.
  No mapping? `SG_SAMPLE_VIRTUAL` falls back to `SG_SAMPLE`
  (ordinary tex1) rather than sampling unbound 2D arrays.
- GL33/GLES3/Metal allow only texture slots 0..15 here.
  The graph's independent sources occupy slots starting 13;
  the VT physical atlas and page table need two more:
  0 graph sources -> tex13/tex14; 1 -> tex14/tex15.
  Reject two or more independent graph textures in a graph
  with a VT node. Do not collide with Shader Graph slots or
  hidden R8 mask texture 15 (graph+R8 is not supported).
- Do not silently mark all graph texture nodes as virtual.
  `sampleTexture` and `sampleTextureLod` remain regular;
  authors must explicitly use `sampleVirtualTexture`.
  Graph-backed GPU models stay on the CPU material submission
  path, not experimental Metal ICB, until an explicit graph
  argument-buffer resource ABI is implemented.
- See docs/shader-graph.md and docs/texture-rhi.md. No shader
  backend compile or GPU test has been run yet.

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


### Per-axis material VT wrapping (Stage 10L)

- The normal `ModelVirtualTextureBinding` defaults to clamped U/V for
  compatibility; opt in with `binding.addressU/addressV =
  gfx::SamplerAddressMode::Repeat` or `MirrorRepeat` independently.
- Keep the original source texture sampler's U/V policy consistent with the
  virtual mapping and pack wrapped tile-edge gutters into the immutable archive.
  `ClampToBorder` is explicitly unsupported; a binding with that mode fails
  `valid()` and must use the non-VT fallback.
- The shader U6 remains 16 bytes, containing layer, gutter, U-address and
  V-address. Shared GLSL/MSL helpers keep legacy clamp-only overloads;
  the model renderer and Shader Graph VT node use the addressing-aware ones.
- The wrap policy participates in exact GPU material-group identity and hash.
  Verify GL/Metal/Vulkan UV seams and sampler parity during the deferred GPU
  validation phase. These additions are source-only, not GPU certified.


### VT immutable archive wrap-aware packing (Stage 10M)

- Generate periodic foliage/ground textures with `tools/virtual_texture_pack.py
  --address-u repeat --address-v repeat`, or use `mirror` independently
  per axis. The default remains `clamp`, including legacy output manifests.
- The packer applies address modes both to packed edge gutters and to
  odd-size mip generation. A non-clamp `manifest.json` records `addressU`
  and `addressV` strings, but the renderer does not automatically infer
  them from the manifest: set the same `ModelVirtualTextureBinding`
  `gfx::SamplerAddressMode` values through the game's resolver.
- Preserve immutable archive versioning and rebuild the full page archive
  when wrapping changes. Test real GLSL/MSL periodic seams during the
  deferred validation phase. Python packer tests for these modes are
  not yet executed/added; do not claim real GPU fidelity.


### Native runtime VT addressing propagation (Stage 10N)

- For native C++ TerraWeave archives, set
  `gfx::VirtualTextureArchivePackConfig::addressU/addressV` before
  `gfx::VirtualTexturePageArchiveWriter::pack`. Both axes default to
  `ClampToEdge`; choose `Repeat` or `MirrorRepeat` per source sampler.
- Packconfig address modes affect edge gutters, odd-size mip filtering,
  the immutable revision hash and the optional manifest fields. Do not
  reuse the prior content revision after changing wrap modes.
- `VirtualTextureArchiveVersion` carries these modes through
  TerraWeave's active session into both single- and multi-domain model
  sampling bindings; no separate per-model patch is needed.
- The Python packer only writes a manifest: a project using its pages
  must explicitly read/provide `addressU/V` to the typed version or
  resolver. There is no automatic manifest-to-version reader.
- `ClampToBorder` is unsupported and rejected. Native/GPU seam tests
  remain deferred; do not treat source-only integration as certified.


### Validated immutable archive manifest import (Stage 10O)

- Use `gfx::loadVirtualTextureArchiveManifest(directory, version, error)`
  before attempting to stream a prepacked Python or C++ page archive.
  The directory is caller-selected and must be trusted/immutable.
  The importer validates all expected page paths, byte sizes, tile/mip
  geometry and sampler U/V modes. It never mutates an active GPU atlas.
- `gfx::importVirtualTextureArchiveManifest(Json,...)` accepts an
  already parsed manifest and the same trusted archive directory.
  The engine's existing `engine/data/Json.h` handles parsing.
- The result is a typed `VirtualTextureArchiveVersion`; supply it to
  the normal TerraWeave session `prepare`/promote lifecycle. Do not
  infer content authenticity from manifest hashes alone.
- This is synchronous filesystem validation; do it during asset
  preparation, not on the render loop. Compile/CTest/GPU tests NOT RUN.


### Direct TerraWeave VT archive import (Stage 10P)

- Use `session.prepareFromArchiveDirectory(trustedDirectory)` for
  standalone sessions or `domains.prepareFromArchiveDirectory(domainId,
  trustedDirectory)` for multiple TerraWeave VT domains. This reads a
  Python/native `manifest.json`, validates page files and builds
  the candidate version; do not call every frame.
- On invalid input the current active atlas and domain stay unchanged.
  Read `session.lastError()` for diagnostics. The returned candidate
  is not active until normal bounded `pumpCandidate` /
  fallback-residency `promote` / GPU fence lifecycle.
- The active version carries U/V wrap policies into the model renderer.
  Never change a live archive in place or trust external path names.
- All C++/GPU validations for the new helper remain NOT RUN.


### GPU page-feedback address parity (Stage 10Q)

- Virtual page sampling and GPU feedback now share U/V addressing:
  `VirtualTextureMaterialMapping::addressU/addressV` follows
  `VirtualTextureArchiveVersion`, and the dedicated rigid/deformed
  feedback shader uniforms carry those modes.
- New `gfx::virtualTextureFeedbackPageAtUV` overload takes typed
  U/V modes. Old normalized-only overload remains for legacy code.
  Shader fragment helpers likewise have old and addressing-aware
  variants. Feedback uses unwrapped UV derivatives for mip selection.
- Rigid feedback U0 is now 192 bytes; deforming feedback U6 is 112.
  Do not reuse a binary shader/uniform contract compiled for the old
  lengths. Sample page IDs in linear RGBA8 without postprocessing.
- CPU visibility prefetch rectangles remain normalized; for wrapped
  large UV domains use GPU feedback or split regions before prefetch.
  No native compiler/driver tests have been run for this stage.


### Wrapped CPU virtual page prefetch (Stage 10R)

- Use `gfx::prefetchWrappedVirtualPages(streamer,planner,views,
  activeArchive.addressU,activeArchive.addressV)` when UV visibility
  crosses repeat/mirror boundaries. It splits at integer seams,
  bounds output to `planner.config().maxRegions` and reuses ordinary
  coarse-to-fine exact-page demand loading.
- `dispatch.wrapRejected` means no request was sent; reduce UV
  coverage, subdivide scene visibility, or use GPU feedback rather
  than continuing with partial incorrect pages. Clamp-only callers
  may retain the old `prefetchVisibleVirtualPages` API.
- The helper is genre independent and does not discover camera
  geometry. Do not mix multiple VT domain IDs in one planner.
  Compilation and runtime CPU/GPU validation have NOT RUN.


### Metal Shader Graph VT source slot expansion — Stage 10S

- Material graphs may author up to four independent source textures
  alongside `sampleVirtualTexture`. Metal stores graph textures in
  tex13..16, then virtual atlas/table in tex17/18 for the four-source
  case, using sampler s1 for the physical VT page and direct table read.
- The renderer computes/validates these slots with
  `supportsModelGraphSourceTextures` and
  `modelGraphVirtualTextureSlots`. GL33/GLES3/Vulkan retain their
  conservative 16-slot cap; graph VT uses source tex1 fallback when
  VT arrays do not fit, and the fourth graph source fails closed.
- Keep graphs out of experimental Metal ICB until its argument buffer
  supports per-graph source and virtual texture descriptors. This
  stage modifies source only: C++/MSL/GLSL compile and device tests
  have not run.


### Fragment texture capacity query (Stage 10T)

- Query `gfx::Device::maxFragmentTextureSlots()` before allocating
  material/VT texture slot layouts. It reports addressable fragment
  *textures*, not separate sampler states. Portable default is 16;
  Metal advertises 32, Vulkan advertises actual descriptor capacity.
- `supportsModelGraphSourceTextures` and
  `modelGraphVirtualTextureSlots` consume that query.
  MSL extended slots require the Metal backend; on GL/Vulkan,
  source tex16 fails closed and a VT graph with insufficient
  remaining texture resources uses its existing tex1 fallback.
- Never reuse high-slot GLSL variants on a device with fewer
  addressable textures, and do not claim Metal ICB resource-table
  support. C++/MSL/GL/Vulkan builds remain NOT RUN.


### Model Shader Graph pipeline cache identity — Stage 10U

- `Renderer3D::ModelShaderKey` requires equality across GLSL/MSL
  fragment+vertex sources, texture paths, material outputs, VT flag,
  shader domain and quality selector, not only the number of
  authored source textures. Avoid weakening this comparison when
  extending the VT resource ABI or Metal ICB grouping.
- GPU material state hashing is a prefilter, not a substitute for
  exact resource/version equality. Keep resource lifetime checks
  at execution after cache lookup.
- Native renderer builds/GLSL/MSL compilers and GPU parity NOT RUN.


### Frame-safe GPU material resource staging (Stage 10V)

- Native C++ consumers may include
  `engine/render/GpuMaterialResourceTable.h` and fill
  `GpuMaterialResourceBinding` with up to four Shader Graph
  texture handles, optional alpha coverage and exact VT resource
  binding. Use `GpuMaterialResourceTable::beginFrame(frameSerial)`,
  `intern`, `matches`, `resolve`, then `reset` on teardown.
- Resource tickets include frame serial+generation+index; never
  use a raw index after a new frame, device/scene shutdown or atlas
  revision promotion. All bindings are compared exactly after
  hash-bucket prefilter; a hash hit alone does not authorize reuse.
- This table stages CPU identity only. It DOES NOT bind a Metal
  argument buffer or permit mixed ICB textures; keep existing
  homogeneous fallback until the GPU/encoder ABI is implemented
  and tested. C++/GPU tests NOT RUN.


### Renderer3D Metal ICB resource ticket safety (Stage 10W)

- `Renderer3D::prepareGpuModelDraws` now stages a
  `GpuMaterialResourceTicket` for each accepted homogeneous GPU ICB
  group. It snapshots VT atlas/page table, layer, wrap/gutter and
  optional R8 alpha coverage. Full/invalid staging tables reject the
  ICB group and keep ordinary CPU fallback.
- `Renderer3D::renderModels` rechecks `GpuMaterialResourceTable::matches`
  for every submitted model, in addition to old exact VT/alpha
  compatibility. Stale generations and asset reloads cannot silently
  submit a reused GPU batch. `shutdownModels` resets the tickets.
- Do not claim per-command mixed Metal ICB resources: the new table is
  still CPU staging only. No native build/GPU shader validation RUN.


### Opt-in per-command Metal ICB VT fragment ABI (Stage 10X)

- RHI: `GpuIcbVirtualMaterial` contains VT atlas/page table handles,
  optional R8 alpha coverage and 16-byte U6 params. The optional
  `prepareGpuEncodedIndexedIcbWithVirtualMaterials` returns zero
  on unsupported backends and requires matching count/descriptors.
- Metal: under `AIVERSE_EXPERIMENTAL_ICB_PER_COMMAND_VT=1`
  *and* existing Metal GPU ICB flags, GPU encoded commands can point
  at per-command 4096-byte PBR argument-buffer slices and 256-byte
  U6 slices. VT resources are revalidated before execution.
- Renderer3D currently only calls this path for homogeneous groups
  with valid virtual textures; it has **not** relaxed grouping to
  allow mixed atlases. The old shared argument buffer and CPU fallback
  remain unchanged by default.
- This is not yet GPU-validated: native C++/Objective-C++, MSL
  compiler, actual Metal frame captures and perf tests NOT RUN.


### Mixed VT atlas groups in experimental Metal GPU ICB (Stage 10Y)

- Experimental only: both
  `AIVERSE_EXPERIMENTAL_ICB_PER_COMMAND_VT=1` and
  `AIVERSE_EXPERIMENTAL_ICB_MIXED_VT=1` are required, along with
  the existing GPU-authored ICB flags. Keep disabled in shipped games.
- `Renderer3D` now permits different VT atlas/table/layer/wrap
  bindings **only** when the underlying built-in PBR material GPU
  state, shader and geometry remain equivalent. Each command
  carries its own `GpuIcbVirtualMaterial` and
  `GpuMaterialResourceTicket`, and every entry is checked again
  before execution.
- `drawModelBatch` skips VT-homogeneity only for this
  prevalidated per-command GPU submission. CPU instancing/MDI
  must never inherit that exception. The first command's
  ordinary PBR materials remain shared, so Shader Graph and
  different conventional source textures are not mixed.
- No actual Metal/GL/Vulkan compile, GPU run, screenshot parity
  or performance evidence has been collected for this path.


### Mixed VT ICB diagnostics (Stage 10Z)

- Read `Renderer3D::gpuModelDrawStats().perCommandVtExecutions`
  and `.mixedVtExecutions` to see accepted GPU execution
  outcomes; these are not counts of prepared or attempted
  commands. A mixed submission must include at least two
  distinct VT resource bindings.
- Compare with `gpuIcbPrepared`, `gpuIcbRejected` and
  `gpuIcbExecuteFailed` when diagnosing CPU fallback.
  Still require Metal GPU capture and pixel A/B tests;
  source-level statistics do not prove shader correctness.
- Both experimental per-command/mixed flags remain
  release-disabled. Native and GPU tests are NOT RUN.


### Experimental Metal VT format gate (Stage 10AA)

- Per-command GPU ICB virtual materials must supply an
  `RGBA8` `Texture2DArray` physical atlas and page table.
  Optional full-resolution alpha cutout must be an `R8`
  `Texture2D`. The native Metal preflight also validates
  page-table virtual layer and gutter inset bounds.
- Unsupported formats and invalid handles reject the entire
  ICB preparation. Do not reinterpret HDR, depth or arbitrary
  compressed textures as VT page-table/physical resources.
  CPU fallback remains the safe path. GPU tests NOT RUN.


### Experimental per-command opaque PBR pipeline grouping (Stage 10AB)

- For experimental Metal ICB only, use
  `equivalentGpuIcbPbrPipelineMaterial` and
  `gpuIcbPbrPipelineHash` to classify built-in opaque, non-graph
  materials. They keep flags that change shader/pipeline variants
  equal, including normal-map presence. Do not use the hash alone to
  authorize grouping or use this path for masked/transparent/graph
  materials.
- `GpuMaterialResourceBinding` can store `pbrEnabled`, six
  source texture handles and 36 U2 floats. Exact resource tickets
  include these values and must be checked again before ICB use.
- This stage is CPU contract only; no GPU ABI or native tests run.


### Per-command Metal ICB built-in PBR resource ABI (Stage 10AC)

- The opt-in `GpuIcbVirtualMaterial` may carry
  `hasPbr=true`, six source TextureHandles (slots 1..6),
  and 36 U2 floats (nine vec4 material rows). All six
  handles must be valid; use renderer fallback textures
  for missing optional maps.
- The Metal ICB GPU kernel can set fragment buffer(3)
  to a 256-byte-aligned per-command U2 and use each
  command's PBR textures and sampler states through its
  4096-byte argument-buffer slice. Legacy groups preserve
  original shared U2 / fragment bindings.
- Renderer3D integration and hardware shader validation
  are still required; do not treat this as an enabled or
  verified heterogeneous ICB batch. Tests NOT RUN.


### Heterogeneous opaque PBR in experimental Metal VT ICB (Stage 10AD)

- Opt in to `AIVERSE_EXPERIMENTAL_ICB_MIXED_PBR=1` only
  with existing mixed-VT/per-command-VT/Metal GPU-ICB flags
  on a test device. Never enable by default in production.
- `packGpuPbrMaterialU2` packs the same nine float4 U2 rows
  as the ordinary renderer. `gpuPbrSourceTextures` resolves
  the six fragment samplers with standard fallbacks.
  The GPU command resource ticket includes exact U2+textures,
  VT atlas/page table/layer/wrap and optional alpha coverage.
- Pipeline grouping is conservative: opaque non-graph materials
  only, with matching shader-sensitive normal-map presence and
  structural flags. Masked/transparent and Shader Graph are
  excluded; CPU MDI/direct batching remains unchanged.
- All native builds, shader compiler/device GPU tests NOT RUN.


### Mixed PBR ICB host-submission counters (Stage 10AE)

- Check `gpuModelDrawStats().perCommandPbrSubmissions`
  and `.mixedPbrSubmissions` after the scene pass to see
  host-accepted per-command GPU ICB work. Mixed means the
  group actually had differing U2 or PBR texture handles.
- These counters do not prove GPU completion or visual
  correctness; still require Metal command-buffer status,
  GPU capture, screenshot A/B and performance evaluation.
- Flags are experimental OFF by default; tests NOT RUN.


### Non-VT per-command Metal PBR ICB ABI (Stage 10AF)

- `gfx::GpuIcbPbrMaterial` supplies six valid PBR source
  TextureHandles plus 36 finite float U2 values, without
  any Virtual Texture. `prepareGpuEncodedIndexedIcbWithPbrMaterials`
  returns zero outside explicit backend support.
- Metal requires `AIVERSE_EXPERIMENTAL_ICB_PER_COMMAND_PBR=1`
  and existing GPU ICB safety flags. It writes independent
  PBR argument buffers and per-command U2 and selects the
  compatible ICB fragment shader variant; no VT array/U6.
- Renderer integration is not yet enabled in this stage.
  Never claim native GPU validation; all tests NOT RUN.


### Renderer3D non-VT mixed opaque PBR ICB integration (Stage 10AG)

- For test-only per-command built-in PBR without virtual textures,
  set `AIVERSE_EXPERIMENTAL_ICB_PER_COMMAND_PBR=1` and
  `AIVERSE_EXPERIMENTAL_ICB_MIXED_PBR=1` (plus the existing
  `AIVERSE_GPU_AUTHORED_DRAWS`, Metal ICB safety flags).
  These are OFF by default.
- The renderer groups only opaque built-in materials whose
  pipeline traits match, sends their six actual/fallback
  TextureHandles and U2 to the optional non-VT RHI API,
  and compares exact per-command generation tickets
  immediately before GPU submission.
- VT groups still use `prepare...WithVirtualMaterials`
  and require the additional VT flag. Masked/transparent
  and Shader Graph material groups retain existing CPU
  or homogeneous GPU fallback; no release GPU validation.


### GPU ICB Shader Graph resource identity — Stage 10AH

- The optional RHI per-command PBR/VT material descriptor can hold
  `graphTextureCount=0..4` and corresponding valid source texture
  handles. Unused slots must be empty; graph sources require U2/PBR
  binding and cannot be supplied in base-only mode.
- Metal snapshots native 2D texture resources **and samplers** with
  each command. This does not yet mean shaders read those slots:
  fragment argument-buffer code and renderer integration follow.
- Maintain 16 native sampler limit; tex16 requires constexpr
  sampler if used. No C++/Metal/GPU tests RUN.


### Shader Graph Metal ICB shader argument layout (Stage 10AI)

- Only **fragment-only** graph shader variants may opt into
  `AIVERSE_SG_ICB_GRAPH`; no graph vertex output, deformation,
  transparent, cutout, palette, or unsupported shader permutation.
- Graph sources use tex13..16; tex16 must use constexpr sampler s16.
  Per-command VT arrays start at tex13+N/tex14+N, N=graph count.
  In the Metal ICB variant, fragment graph arguments are read
  from `IcbPbrTextures`, not direct texture/sampler function
  parameters; legacy source shaders are unchanged.
- Metal validates each graph source handle+native sampler against
  the captured descriptor and shader count. This is not yet an
  enabled Renderer3D graph batching path. No native/GPU tests RUN.


### Experimental fragment-only Shader Graph GPU ICB (Stage 10AJ)

- Opt-in: `AIVERSE_EXPERIMENTAL_ICB_SHADER_GRAPH=1`
  alongside per-command/mixed PBR flags and existing Metal
  GPU ICB gates. For VT graphs also enable the per-command VT
  gate. Default is disabled.
- Only opaque fragment-only material graphs (<=4 source
  textures, no vertex outputs or deformation) are eligible.
  The renderer groups graphs only if their actual compiled
  shader source/output mapping/texture count and structural
  pipeline traits match. Different source paths/textures are
  allowed when the shader program itself is equivalent.
- Every command stages its four-or-fewer graph source handles
  together with U2, PBR sources and optional VT identity.
  `GpuMaterialResourceTable` and Metal native resource checks
  reject changes since preparation. Vertex graph textures
  remain on the safe ordinary draw route.
- No C++/Objective-C++ builds, MSL compiler or Metal GPU
  execution tests have been run. Keep Draft and release OFF.


### Shader Graph Metal ICB host counters (Stage 10AK)

- `Renderer3D::gpuModelDrawStats().shaderGraphIcbSubmissions`
  counts host-accepted graph ICB submissions.
  `.mixedGraphSourceSubmissions` counts accepted groups where
  actual command-local graph source handles differ, not
  groups with only different PBR U2.
- Compare with `gpuIcbPrepared`, `gpuIcbRejected` and
  `gpuIcbExecuteFailed` for fallback diagnostics. These
  counters do not prove GPU completion or pixel correctness.
- All experimental graph and PBR flags remain disabled by
  default and have NOT been exercised on a Metal device here.
