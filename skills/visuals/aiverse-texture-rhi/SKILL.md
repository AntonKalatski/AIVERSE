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
| Budgeted deferred texture upload | `gfx::TextureStreamQueue` | Owns GPU texture and staged bytes; `drain()` on device thread |
| Safe coarse-to-fine sampling | `gfx::TextureMipResidency` | Only create/bind sampler when complete coarse-to-fine mips resident |
| Unified native streaming | `gfx::TextureResidentStreamer` | Owns queue+residency; gates sampling, tracks completions and retries |
| Radiance `.hdr` / limited EXR | `render::loadRadianceHdr`, `render::loadOpenExr` | Linear RGBA16F; EXR is uncompressed scanline HALF/FLOAT RGB[A] only |
| Virtual texture atlas | `gfx::VirtualTextureStreamer` | Fixed physical Texture2DArray pages, GPU page table, LRU and pinning |
| Disk page I/O worker | `gfx::VirtualTextureDiskQueue` | Deduplicated, cancelable, bounded filesystem page results |
| Disk-backed virtual atlas | `gfx::VirtualTextureFileStreamer` | Explicit page requests, render-thread pump and GPU page-table publication |
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

### Optional Basis Universal codec boundary

Include `engine/render/Ktx2Transcode.h` and supply a callback of type
`render::Ktx2Transcoder` when using ETC1S/BasisLZ or UASTC KTX2.
The callback must return all mip-major slices/faces with the same
source dimensions/layers/mip chain. It must select a format the
GPU supports, or return RGBA8 for the existing CPU-upload path.
Without a codec, Basis input fails explicitly. **No Basis encoder
or transcoder is bundled with AIVERSE.** Never claim a callback
is equivalent to implementing the codec. Check the license and
actual target GPU capabilities before importing production assets.

### Correctness and compatibility

- Keep existing model material shaders' `sampler2D` resources on the
  legacy first-face path. Only dimension-aware shaders sample arrays/cubes.
- Check `Device::supports` and actual handle validity for format availability.
  `PixelFormat::BC4/BC5/EAC_R11/BC6H` is not universally supported.
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

### Verification gates — don't invent passes

```sh
cmake -S . -B build/texture-rhi -DAIVERSE_BUILD_TESTS=ON
cmake --build build/texture-rhi --target \
  aiverse_texture_contract_test aiverse_render_target_set_test \
  aiverse_texture_streaming_test aiverse_texture_mip_residency_test \
  aiverse_texture_resident_streamer_test aiverse_virtual_texture_pages_test \
  aiverse_virtual_texture_page_table_test aiverse_virtual_texture_streamer_test \
  aiverse_virtual_texture_disk_queue_test aiverse_virtual_texture_file_streamer_test \
  aiverse_hdr_texture_test aiverse_exr_texture_test \
  aiverse_ktx2_layers_test aiverse_ktx2_transcode_test \
  aiverse_dds_layers_test aiverse_texture_asset_test
ctest --test-dir build/texture-rhi --output-on-failure \
  -R '^(texture_contract|render_target_set|texture_streaming|texture_mip_residency|texture_resident_streamer|virtual_texture_pages|virtual_texture_page_table|virtual_texture_streamer|virtual_texture_disk_queue|virtual_texture_file_streamer|hdr_texture|exr_texture|ktx2_layers|ktx2_transcode|dds_layers|texture_asset)$'
```

Build the editor and native client to catch API/layout regressions.
Run `vulkan_backend` on a real Vulkan device or Lavapipe **and inspect its
output for skipped execution**. Metal, GL33, GLES3, WebGL2 and GPU readback
require their own real device build/validation. Record each real test
command and result in the handoff after every stage; never claim passing
CI if it remains queued or cancelled.
