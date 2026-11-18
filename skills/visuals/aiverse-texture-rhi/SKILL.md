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
| Budgeted deferred texture upload | `gfx::TextureStreamQueue` | Owns GPU texture and staged bytes; `drain()` on device thread |
| Safe coarse-to-fine sampling | `gfx::TextureMipResidency` | Only create/bind sampler when complete coarse-to-fine mips resident |

### Example: mip-streamed Texture2DArray

```cpp
#include "engine/gfx/TextureStreaming.h"
#include "engine/gfx/TextureMipResidency.h"

gfx::TextureStreamQueue streamer(device);
gfx::TextureDesc desc;
desc.width = desc.height = 256;
desc.dimension = gfx::TextureDimension::Texture2DArray;
desc.arrayLayers = 4;
desc.mipLevels = 9;
const auto id = streamer.createTexture(desc);
gfx::TextureMipResidency residency;
if (id.valid() && residency.initialize(desc)) {
    // First claim is mip 8, array layer 0 (coarsest first).
    gfx::TextureMipRequest request = residency.claimNext();
    // The asset loader provides the full mip/layer payload and dimensions.
    // Do not read texture bytes from disk on the GPU render thread.
    auto upload = gfx::TextureUploadDesc{};
    upload.mipLevel = request.mip;
    upload.arrayLayer = request.layer;
    upload.width = std::max(1, desc.width >> request.mip);
    upload.height = std::max(1, desc.height >> request.mip);
    // Upload only if CPU data exists; use exactly the RGBA8 source byte count.
    auto ticket = streamer.enqueue(id, upload, assetBytes.data(), assetBytes.size());
    if (!ticket.valid()) residency.complete(request, false);
    streamer.drain([&](gfx::TextureUploadTicket done, gfx::TextureStreamId resource, bool ok) {
        if (resource == id && done == ticket) residency.complete(request, ok);
    });
    gfx::SamplerDesc sampler, clamped;
    if (residency.residentSampler(sampler, clamped)) {
        // Create and bind a sampler with clamped LOD, never sample absent mips.
        auto nativeSampler = device.createSampler(clamped);
        // Use nativeSampler with a compatible dimension-aware texture/view.
        if (nativeSampler.valid()) device.destroySampler(nativeSampler);
    }
    streamer.destroy(id);
}
```

The snippet shows one upload; real schedulers track **every** ticket to its
`TextureMipRequest`, issue all layers of each mip and create a new sampler
only after a complete mip range becomes safe. A completion callback runs
after a `drain` loop on the render thread. If a ticket is cancelled,
call `residency.complete(request, false)` yourself.

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
  aiverse_ktx2_layers_test aiverse_dds_layers_test aiverse_texture_asset_test
ctest --test-dir build/texture-rhi --output-on-failure \
  -R '^(texture_contract|render_target_set|texture_streaming|texture_mip_residency|ktx2_layers|dds_layers|texture_asset)$'
```

Build the editor and native client to catch API/layout regressions.
Run `vulkan_backend` on a real Vulkan device or Lavapipe **and inspect its
output for skipped execution**. Metal, GL33, GLES3, WebGL2 and GPU readback
require their own real device build/validation. Record each real test
command and result in the handoff after every stage; never claim passing
CI if it remains queued or cancelled.
