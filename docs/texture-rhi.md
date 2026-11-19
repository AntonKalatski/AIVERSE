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

**VRAM and virtual texture caveat:** Ordinary TextureResidentStreamer
allocates the full mip chain up front and budgets *uploads* rather than
physically uncommitting mip memory. The separate VirtualTextureStreamer
now contains a fixed physical Texture2DArray atlas, an LRU physical-page
cache, a GPU-uploadable page table and reusable GLSL/MSL lookup helpers.
Hardware sparse textures, automatic GPU feedback, a disk I/O worker and
production GPU sampling validation are **not implemented/verified**.

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
- **Known missing pieces:** automatic GPU feedback/disk paging, sparse
  hardware residency, fully verified virtual texture shaders, integer/packed
  formats, typed color targets, Basis transcoding, compressed/tiled/multipart
  EXR, exotic DDS variants and real-device validation of MRT/MSAA/readback.
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

## Disk-backed virtual pages — source implementation (unverified)

\`engine/gfx/VirtualTextureDiskQueue.h\` provides an actual background
filesystem worker reading immutable packed RGBA8 page files from
\`m<MIP>/l<LAYER>/x<X>_y<Y>.rgba\` inside a trusted game asset root.
It validates dimensions/file size, deduplicates requests, supports
cancellation, and enforces outstanding request and RAM result budgets.
Polling and GPU work remain on the render thread.

\`engine/gfx/VirtualTextureFileStreamer.h\` combines this disk source
with the existing physical \`VirtualTextureStreamer\` atlas and GPU
page table. \`requestPage\` accepts explicit virtual tile requests;
\`pump\` stages ready disk pages to the GPU, handles backpressure,
and publishes shader mappings only after GPU upload completion.
A missing/truncated file never becomes a resident GPU page.
The game must still request pages via an application prefetch
policy or future GPU-feedback pass. Native GPU/compiler tests have
not yet been executed.

New CPU CTests \`virtual_texture_disk_queue\` and
\`virtual_texture_file_streamer\` exercise actual temporary files
and a mock GPU, respectively.

## Visibility-based virtual texture prefetch

`engine/gfx/VirtualTexturePrefetch.h` defines a CPU-only, genre-agnostic
UV visibility planner. A game passes normalized visible UV rectangles,
logical virtual layer, desired mip and priority. It validates/clamps
bounds, deduplicates overlapping regions, plans coarsest mip tiles
first, optionally pins fallback pages, and limits the work using
`maxRegions`, `maxCandidates`, `maxExaminedPages` and `maxRequestsPerFrame`.
The separate `maxExaminedPages` limit bounds CPU work including
already-resident/coarsely-loaded tiles, whereas `maxCandidates`
limits only tiles that still need uploading. This prevents a
large resident coarse mip from starving missing high-detail tiles.

`engine/gfx/VirtualTexturePrefetchAdapter.h` connects those plans
to the disk-backed `VirtualTextureFileStreamer` without putting
disk file reads or GPU uploads on the visibility thread. It uses
**exact** page residency and pending-request checks, not just
sampleability through a coarser fallback. Coarse pending requests
can be promoted to pinned fallbacks. `prefetchVisibleVirtualPages`
returns disk request tickets; the caller still invokes
`VirtualTextureFileStreamer::pump` once per render frame.

Visibility projection, asset UV-to-world mapping, per-surface
LOD selection and optional GPU-feedback passes remain game/editor
choices. The planner is presentation state, not authoritative ECS.
Coverage is source-level only: native C++/CTest/GPU validation remains
outstanding. The `virtual_texture_prefetch` CTest and the existing
`virtual_texture_file_streamer` CTest contain authored cases.

## Owned feedback render target

`engine/gfx/VirtualTextureFeedbackPass.h` is an opt-in,
genre-independent owner of the feedback color target and
the Stage 7AC async readback runtime. It builds one
Texture2D RGBA8 UNORM offscreen render target through
`RenderTargetSet`, no depth, no sRGB, no MSAA, one mip.
`begin()` clears to zero and configures the pass viewport;
the game binds its opaque one-color-output feedback shader
and draws the visible UV surfaces; `endAndCapture()` ends
the pass before requesting nonblocking readback. On a later
frame `submitVisiblePages(fileStreamer)` submits missing
pages through the generic UV prefetch and disk worker queue.
Call `fileStreamer.pump()` separately for GPU uploads.

This helper does not own a camera, geometry, render graph,
shader compilation or game ECS state. It must be destroyed
before `gfx::Device`; late callbacks are ignored using weak
state. The corresponding GPU-independent mock CTest is
`virtual_texture_feedback_pass`. It has NOT been executed
against native SDK builds or GPU drivers.

## Optional GPU feedback → prefetch source pipeline

`engine/gfx/VirtualTextureFeedback.h` adds a bounded 32-bit tile
feedback ID layout in a **linear RGBA8 UNORM** render target:
11 bits x + 11 bits y + 5 bits mip + 4 bits virtual layer + valid bit.
Zero is reserved for unused pixels. GLSL and MSL fragment helpers
encode the same ID, and `VirtualTextureFeedbackDecoder` refuses
non-RGBA8, malformed/truncated or over-budget readback results.
It groups visible pages by frequency and produces normalized
`VirtualTextureVisibleRegion` requests for the genre-independent
coarse-first prefetch planner.

`engine/gfx/VirtualTextureFeedbackRuntime.h` connects the optional
`Device::readTextureAsync` to the CPU decoder and then to
`prefetchVisibleVirtualPages`. It owns no render pass, shaders,
images or feedback draw commands. Game/editor render code must
generate feedback with **no sRGB write conversion, blending,
MSAA, dithering or postprocess**, end the feedback pass, call
`capture(feedbackTexture)` and later `submit(fileStreamer)` after
async callbacks have arrived on the graphics thread. Continue
calling `fileStreamer.pump()` to upload physical pages.
The runtime uses a weak callback state, allowing destruction
before a GPU command completes without dangling owner pointers.

This is an opt-in shader feedback path, **not** automatically
integrated in the editor, camera, material graph or Terra.
Only Metal/GL33/GLES3/Vulkan have source readback code; WebGL2
fails closed. Real shader compilation, per-backend render capture
and readback accuracy remain NOT RUN. The CPU contracts are
`virtual_texture_feedback` and `virtual_texture_file_streamer`.

## Live GPU texture inspector — source-only

`Device::textureResources()` now enumerates user-created live textures from
Metal, GL and Vulkan backend pools. The returned `TextureResourceInfo`
contains dimension, format, extent, mip/layer/sample counts, resource flags
and an approximate `estimateTextureFootprint` size. It is an estimate,
**not a driver VRAM allocation/residency query**; swapchain and internal
dummy resources are excluded.

In the editor, open **Графика → GPU Текстуры**. The window is closed by
default; it supports filtering, sorting and an optional async mip/layer/face/volume-slice
CPU preview on supported uncompressed formats (not depth/MSAA/blocks). Neither the editor
window nor the backend enumeration has been compiled or tested on a native
target in this environment.


## Native compressed sRGB formats

The shared RHI provides separate sRGB GPU variants for BC1/BC3/BC7,
ETC2 RGB/RGBA, and ASTC 4x4/6x6. When DDS or KTX2 metadata declares
sRGB and the device supports its compressed variant,
`uploadContainerTexture` selects the hardware-decoded sRGB format
without requiring shader-authored gamma conversion. Backend support
varies; GL/GLES/WebGL2 must probe the dedicated sRGB S3TC extension.
Unsupported compressed sRGB formats fall back to CPU RGBA8_sRGB if
a decoder exists. ASTC without native support still fails rather
than treating block bytes as RGBA pixels. BC1_RGB opaque blocks
are not silently reinterpreted as BC1_RGBA blocks.

`TextureUploadOptions.trustShaderSrgbDecode` remains a legacy
opt-in for callers that have explicit shader decode semantics;
it is not needed for the new true sRGB GPU block formats.
See KTX2 and TextureAsset CPU tests for the format contract.
**Native GPU and shader validation has not yet run.**

## Optional Basis Universal KTX2 bridge

`engine/render/Ktx2Transcode.h` accepts an externally supplied
`Ktx2Transcoder` callback for ETC1S/BasisLZ and UASTC KTX2.
The engine checks the original header/level index, rejects broken
dimensions and supercompression, and requires the transcoder to
return the same dimensions, layers, faces and number of mip levels.
Unknown native block formats, incorrect mip sizes and excessive
decoded memory fail closed. Regular KTX2 bypasses the callback.

This standalone callback is only an integration boundary. To use a
**real** Basis Universal LDR transcoder, opt in to the system Khronos
`libktx` adapter in `engine/render/Ktx2Libktx.h`:

```sh
cmake -S . -B build/texture-rhi -DAIVERSE_TEXTURE_WITH_LIBKTX=ON
```

Install the matching system `ktx.h` / `libktx` development package
first. `render::uploadKtx2WithLibktx(device,ktxBytes,options,error)`
selects BC7, ASTC 4x4, ETC2 RGBA or BC3 using the RHI capability
query; it otherwise falls back to RGBA8 if permitted. The adapter
is completely opt-in; no codec is vendored and default builds
still fail Basis input explicitly. For **UASTC HDR**, use the separate
`render::uploadKtx2UastcHdrWithLibktx(device,ktxBytes,error)`
API. It uses system libktx's `KTX_TTF_RGBA_HALF` path,
validates original mip/face/array/volume topology, and uploads
a full mip chain as linear RHI RGBA16F (never RGBA8). This
path requires a GPU with matching RGBA16F dimension/support.
Native BC6H or ASTC HDR compressed import is still separate.
The HDR code has a public-header compile fixture but **no**
production encoded HDR asset/GPU fidelity verification yet. Check the upstream
license and real GPU output before releasing.
Test source: `tests/render/ktx2_transcode.cpp` (`ktx2_transcode`).

## Optional OpenEXR 3 compressed HDR import

With `-DAIVERSE_TEXTURE_WITH_OPENEXR=ON` and a compatible
system OpenEXR 3 package, include `engine/render/ExrOpenExr.h`
and use `parseOpenExrWithLibrary(bytes,image,error)` or
`loadOpenExrWithLibrary(device,bytes,error)`. The external
library decodes scanline ZIP/ZIPS/PIZ and other supported
first-part RGBA files to linear RGBA16F; no exposure/tone
mapping is implicit. Multipart/deep workflows remain
unsupported by this adapter. Default builds still use
the narrow dependency-free uncompressed EXR parser.
CTest `exr_texture` includes conditional real compressed
image fixtures when OpenEXR is enabled, but they are
NOT RUN on a native OpenEXR SDK in this branch.

## Vulkan writable compute textures

`VulkanDevice` now has opt-in `Device::supportsCompute`,
`Device::supportsStorageTexture`, and the normal
`createCompute` / `beginCompute` / `bindComputeStorageTexture` /
`dispatch` / `endCompute` methods. Vulkan compute source is
GLSL450 **without** a `#version` directive; the shared
glslang compiler emits SPIR-V. Bindings: set 0, storage
images 0..3, sampled images 4..11, storage buffers 12..15,
uniform buffer 16. Storage images are conservatively
limited to RGBA8/RGBA16F and 2D/2DArray/3D devices that
advertise STORAGE_IMAGE and SAMPLED_IMAGE format features.
Default X workgroup size is 64, as on Metal. GLSL shader
bindings must match these declarations; neither Vulkan
nor GL silently emulates Metal shader source.

The Vulkan test `vulkan_backend` now contains an authored
compute shader writing known RGBA8 colors and reading them
back asynchronously. All Vulkan shader compilation and
hardware/lavapipe test execution remain **NOT RUN** until
a runner actually executes them. GL33/GLES3/WebGL2 currently
retain explicit `supportsCompute=false` rather than
pretending to support 4.3+ compute.

## Editor volume texture inspection

The GPU Texture Resources window now has a per-mip depth
slice selector for Texture3D, in addition to existing
2DArray/Cube face/layer selection. Native Metal, GL and
Vulkan async readback code supports uncompressed 3D
RGBA8/sRGB/16F slices (GL still limited to RGBA8/sRGB),
subject to the 16MiB preview staging cap. The preview
remains a CPU thumbnail/diagnostic and is not a driver
VRAM residency query; compressed/depth/MSAA readback
still fails explicitly. WebGL2 has no native async
readback and should instead use CPU UV prefetch.

## Focused automated verification

`.github/workflows/texture-rhi.yml` defines an opt-in
OpenEXR OFF/ON Linux test matrix and a Vulkan CMake/CTest
job with uploaded logs. The existing
`tools/verify-texture-rhi.sh [--vulkan] [--editor]`
may also be run locally. A queued/cancelled CI run is not
a PASS. Vulkan CTest may skip the driver if no Vulkan
device is available; inspect the job logs before
claiming real storage-image validation.
## Native build and targeted tests (pending execution)

```sh
git switch feature/texture-rhi-unification
git pull --ff-only origin feature/texture-rhi-unification
cmake -S . -B build/texture-rhi -DAIVERSE_BUILD_TESTS=ON
cmake --build build/texture-rhi --target \
  aiverse_texture_contract_test aiverse_render_target_set_test \
  aiverse_ktx2_layers_test aiverse_ktx2_transcode_test \
  aiverse_dds_layers_test \
  aiverse_texture_asset_test aiverse_texture_streaming_test \
  aiverse_texture_mip_residency_test aiverse_texture_resident_streamer_test \
  aiverse_virtual_texture_pages_test aiverse_virtual_texture_page_table_test \
  aiverse_virtual_texture_streamer_test \
  aiverse_virtual_texture_disk_queue_test aiverse_virtual_texture_file_streamer_test \
  aiverse_virtual_texture_prefetch_test aiverse_virtual_texture_feedback_test \
  aiverse_virtual_texture_feedback_pass_test \
  aiverse_hdr_texture_test aiverse_exr_texture_test
ctest --test-dir build/texture-rhi --output-on-failure \
  -R '^(texture_contract|render_target_set|ktx2_layers|ktx2_transcode|dds_layers|texture_asset|texture_streaming|texture_mip_residency|texture_resident_streamer|virtual_texture_pages|virtual_texture_page_table|virtual_texture_streamer|virtual_texture_disk_queue|virtual_texture_file_streamer|virtual_texture_prefetch|virtual_texture_feedback|virtual_texture_feedback_pass|hdr_texture|exr_texture)$'
```

Build `aiverse_editor` and a native client to detect editor/renderer
ABI regressions. Build and run `aiverse_vulkan_tests`/`vulkan_backend`
with Vulkan SDK and a working GPU or Lavapipe; distinguish a real GPU
run from driver-unavailable skips. Metal, GL33, Android GLES3 and WebGL2
need separate native builds and visual/readback validation.
**No tests are claimed as passed for this feature branch.**

## References and handoff

- `engine/gfx/Gfx.h` — public resources, descriptors and inspection
- `engine/gfx/RenderTargetSet.h` — MRT/MSAA render-target owner
- `engine/gfx/TextureResidentStreamer.h` — bounded complete-mip streaming
- `engine/gfx/VirtualTextureStreamer.h` — bounded physical atlas streaming
- `engine/gfx/VirtualTextureDiskQueue.h` — asynchronous packed page file reads
- `engine/gfx/VirtualTextureFileStreamer.h` — disk-to-GPU physical atlas coordinator
- `engine/gfx/VirtualTexturePrefetch.h` — bounded normalized UV visibility planner
- `engine/gfx/VirtualTexturePrefetchAdapter.h` — disk-page request integration
- `engine/gfx/VirtualTextureFeedback.h` — bounded RGBA8 page feedback codec
- `engine/gfx/VirtualTextureFeedbackRuntime.h` — optional async readback-to-prefetch coordinator
- `engine/gfx/VirtualTextureFeedbackPass.h` — owned RGBA8 offscreen feedback pass
- `engine/gfx/VirtualTextureSampling.h` — GLSL/MSL virtual page lookup
- `engine/render/TextureAsset.h` — DDS/KTX2 GPU uploads
- `engine/render/Ktx2Libktx.h` — opt-in native Khronos libktx ETC1S/UASTC LDR adapter
- `engine/render/ExrOpenExr.h` — opt-in OpenEXR 3 compressed HDR reader
- `engine/gfx/vulkan/VkCompute.cpp` — Vulkan writable image and compute pipeline backend
- `apps/editor/windows/TextureResourcesWindow.cpp` — live GPU inspector
- `docs/agent-handoffs/texture-rhi-2026-11-18.md` — append-only handoff log

Every implementation stage requires a code commit and a **separate**
handoff commit with exact SHAs, actual test results and next steps.


## Native Renderer3D material feedback (new RHI program)

The public C++ implementation now includes
`engine/gfx/VirtualTextureFeedbackProgram.h` and
`engine/render/VirtualTextureModelFeedback.h`.
The former generates complete engine-RHI GLSL/MSL shader stages
and creates an RGBA8 position/UV pipeline with the real U0
(std140/Metal buffer 1) binding contract. The latter owns
eight rigid `ModelVertex` pipeline variants: alpha mask
on/off, LOD fade on/off, and single/double-sided culling.
It maps `Model::images[material.baseTex]`, alpha cutoff,
`uvScale`/`uvRotation`/`uvOffset` and LOD pixel-grid
parameters onto those pipelines automatically.

Example initialization and ownership:

```cpp
gfx::VirtualTextureFeedbackPass feedback;
const bool ok = feedback.initialize(dev, feedbackWidth, feedbackHeight,
                                    feedbackConfig, prefetchConfig,
                                    8, true); // enable depth
render::VirtualTextureModelFeedback modelFeedback;
if (ok && modelFeedback.initialize(dev, feedback.hasDepthAttachment())) {
    modelFeedback.connect(renderer,
        [](const render::Model& model,
           const render::Material& material,
           render::VirtualTextureMaterialMapping& map) {
            // Look up your project's VT allocation for this material.
            // If no VT is assigned return false: still draw depth, no page ID.
            if (!hasVirtualPages(model, material)) return false;
            map.baseTilesX = ...; map.baseTilesY = ...;
            map.layer = ...; map.mipLevels = ...;
            map.texelWidth = ...; map.texelHeight = ...;
            return true;
        });
    renderer.setVirtualTextureFeedback(&feedback, {}, {});
}
```

Call `Renderer3D::beginScene` and submit models normally;
`Renderer3D::render` invokes feedback automatically before
opening its main scene pass when the pass is ready and idle.
Call `feedback.updateStreaming(fileStreamer,budget,...)` from
the frame coordinator to consume readback requests; otherwise
pending readback intentionally blocks new capture.
Before destroying GPU resources, call
`renderer.clearVirtualTextureFeedback()`,
`modelFeedback.shutdown()` (disconnects model variants),
then `feedback.shutdown()`; do so before destroying `dev`.

The resolver is genre-agnostic. A model without VT allocation
still writes depth with zero feedback IDs, preserving occlusion.
The adapter creates and owns a 1x1 white alpha texture for
masked materials without a valid base image.

Limitations: no skinned/baked/morph GPU feedback vertex program
is created here; use `setVirtualTextureDeformedModelFeedback`
for those until an exact deformation variant is available.
The program is not yet registered by Terra automatically.
For fixed material alpha/LOD/UV parity, special shader graphs,
material palettes, GPU-driven mesh LOD, and main-pass depth
must be reviewed during the later GPU test stage.
The isolated fragment source generators in
`VirtualTextureFeedback.h` are not themselves complete
`ShaderDesc` sources; use the new program builder for native RHI.


### Deformed model VT feedback: skinning, baked clips, morphs

`engine/render/VirtualTextureDeformedFeedback.h` provides the opt-in
`render::VirtualTextureDeformedModelFeedback` adapter. It uses the
same `ModelShaders.h` GLSL/MSL deformation functions as the main
model pass (rather than a simplified second implementation),
including `U3` joint palette, baked joint texture `tex7`,
`U4` morph weights and `tex8` morph deltas. The adapter creates
six deformation shader configurations, each with backface or
double-sided culling: rigid-dynamic, skinned, baked, morph,
skinned+morph, and baked+morph. Material alpha test and LOD fade
are controlled per draw using a shared U6 feedback block;
the fragment shader computes automatic mip before any discard.

Add the adapter after initializing the model feedback renderer:

```cpp
#include "engine/render/VirtualTextureDeformedFeedback.h"

render::VirtualTextureModelFeedback rigidFeedback;
render::VirtualTextureDeformedModelFeedback animatedFeedback;
rigidFeedback.initialize(dev, feedback.hasDepthAttachment());
animatedFeedback.initialize(dev, feedback.hasDepthAttachment());
rigidFeedback.connect(renderer, resolveVirtualMaterial);
animatedFeedback.connect(renderer, resolveVirtualMaterial);
renderer.setVirtualTextureFeedback(&feedback, {}, {});
```

`resolveVirtualMaterial` is the same
`VirtualTextureMaterialResolver` callback used by rigid models.
It fills virtual page tile counts, logical texture resolution,
mip level count, page layer and optional UV scale/offset.
Unmapped materials write zero feedback while retaining depth
occlusion. The deformed callback uses the shared `Model::vb/ib`,
including `gpuBaseVertex` and `gpuFirstIndex`, and matches
the main renderer's 60-byte `ModelVertex` vertex layout.

On teardown, clear the pass from `Renderer3D`, disconnect
`animatedFeedback` and `rigidFeedback`, then shut down both
adapters and the pass **before** destroying the RHI device or
renderer. Deformed feedback requires valid per-frame animation
state from normal `Renderer3D::beginScene` and `drawModel`.

Caveats: this is not a CPU skinning implementation; it issues
GPU vertex deformation using the same shader functions as
the visible draw. Shaders with custom material graphs and
vertex displacement require further parity work. Geometry
residency and alpha texture availability must be valid.
Deformed objects whose required bone/morph GPU state is
incomplete are skipped conservatively rather than drawn
undeformed. Multi-backend compilation and visual parity
have **not yet been tested**.


## TerraWeave integration (model queue, forest, impostors, Horizon)

`engine/render/TerraWeaveVirtualTextureFeedback.h` provides a
genre-independent VT material mapping bridge. The current
`feature/terraweave` implementation uses
`render::Renderer3D::drawModel()` for
`ArborForestAdapter::submit()` / `forEachDraw()` (near trees and
far impostor quads) and for `TerraWeaveHorizonScene::submit()`
(distant terrain + forest horizon). Therefore **no separate Terra
geometry renderer or duplicate feedback draw queue is necessary**.
The bridge depends only on `ModelDraw`, `Model`, `Material`,
`Renderer3D` and the generic gfx feedback program. It remains
compilable on `feature/texture-rhi-unification` *before* Terra's
separate branch is merged.

Initialize after the feedback target has been allocated:

```cpp
render::TerraWeaveVirtualTextureFeedback terraFeedback;
bool ok = terraFeedback.initialize(dev, feedbackPass, renderer);
// Optional: enable future skinned/morphed Terra prototypes with true.
// The existing Terra-generated forest/horizon models are rigid.
```

There are **two methods for assigning existing VT pages**:

1. Explicit registration before `Renderer3D::render()`:
   `terraFeedback.registerMaterial(model, material, mapping)`,
   `registerModel(model, mapper)` or `registerDraw(draw, mapper)`.
   `registerDraw` also registers per-azimuth impostor material overrides
   (`ModelDraw::materials`). A forest integration after the Terra branch
   is merged can submit the exact same draw once:

   ```cpp
   terraFeedback.beginFrame(); // invalidate stale pointer keys from last frame
   forest.forEachDraw(batches, origin, [&](const render::ModelDraw& draw) {
       terraFeedback.registerDraw(draw, lookUpExistingVirtualPages);
       renderer.drawModel(draw);
   }, cameraCm);
   // normal Renderer3D::render() performs feedback and scene rendering
   ```

2. For `TerraWeaveHorizonScene`, whose active/replacement models are
   intentionally private, call
   `terraFeedback.setLiveMaterialMapper(lookUpExistingVirtualPages)`.
   The feedback binder receives the *actual* Model and Material
   pointers when `Renderer3D` submits them; it can match active
   region revision, forest atlas/material, and exact VT allocation
   without modifying Terra's API or duplicating `submit(renderer)`.
   The callback runs on the render thread during GPU feedback
   submission. It must only **look up existing allocations**, not
   allocate GPU resources, generate pages or block on disk IO.

`VirtualTextureMaterialMapping` includes virtual base tile counts,
layer, mip policy, logical texture dimensions, and optional UV
scale/offset. The bridge rejects malformed/non-finite mappings.
Unregistered Terra materials still write **depth with zero feedback**
and correctly occlude virtualized surfaces behind them. This is
intentional: not every bark, foliage or impostor material needs VT.

The host owns the physical atlas, page-table lifetime, packed page
archives and `VirtualTextureFileStreamer`. The new bridge does not
automatically transcode Terra image atlases or assign virtual pages.
It only connects actual VT allocations to the existing shared
shader/readback/streaming pipeline. Continue calling
`feedbackPass.updateStreaming(streamer, budget)` when no render pass
is active, and consume its visibility result.

At teardown, call `terraFeedback.shutdown()` before destroying the
Renderer3D, `feedbackPass` or `gfx::Device`. The bridge clears the
renderer feedback configuration and destroys the shader resources
it owns, but does not delete the caller-owned feedback pass or
file streamer. `beginFrame()` must occur before re-registering
Forest/Horizon materials after asset reloads/region replacement.

**Branch coordination:** Terra's source currently lives in
`feature/terraweave`; the VT bridge lives in
`feature/texture-rhi-unification`. The Terra agent should use this
adapter after the two branches are merged. Do not duplicate the
VT system in `engine/world` or modify genre-specific RTS terrain
just to integrate Terra. Existing Terra `ModelDraw` visibility,
LOD, material overrides and the engine's virtual feedback queue
are the shared integration seam.

**Not yet verified:** native C++ build, GLSL/MSL/Vulkan shader
compilation, visual alpha/LOD parity, real packed archive
integration, distant Horizon region transitions, and GPU/IO
performance. Terra's independently developed custom material
paths or transparent transition draws may need additional
feedback policies beyond this rigid-model bridge.


### Runtime Terra atlas page files and Horizon revision switch

Use `engine/gfx/VirtualTexturePageArchiveWriter.h` for generic RGBA8
assets or `engine/render/TerraWeaveVirtualAtlasArchive.h` to pack
Terra-generated RGBA8 directly from retained CPU memory, before
`uploadModel` frees `Image::rgba`. For an active Horizon region
whose `HorizonBake::atlasRgba` is available:

```cpp
gfx::VirtualTextureArchivePackConfig settings;
settings.pageSize = 128;
settings.gutter = 2;
gfx::VirtualTextureArchiveVersion nextVersion;
std::string error;
render::TerraWeaveVirtualAtlasSource source{
    int(bake.atlasWidth), int(bake.atlasHeight),
    std::span<const uint8_t>(bake.atlasRgba)};
bool packed = render::packTerraWeaveAtlas(
    cacheRoot / "horizon-vt", horizonRevision, source,
    settings, nextVersion, error);
```

The writer generates the same `m<MIP>/l<LAYER>/x<X>_y<Y>.rgba`
page paths that `VirtualTextureDiskQueue` reads, applies
deterministic 2x2 RGBA box mip filtering and clamped page gutters,
and publishes the complete version via a same-filesystem
directory rename. Existing versions are immutable and stay
available until readers/GPU accesses have retired.

After packing, on the graphics-device thread, prepare the new
streamer while the previous one remains active:

```cpp
render::TerraWeaveVirtualAtlasSession atlasSession(dev);
if (atlasSession.prepare(nextVersion, /*physicalPages=*/128)) {
    // Called each frame while the new archive's disk reads and
    // GPU page uploads progress, not in a worker thread.
    atlasSession.pumpCandidate(8);
    if (atlasSession.candidateFallbackResident() &&
        atlasSession.promote(feedbackPass)) {
        // Bind the NEW active()->physicalAtlas() and
        // active()->pageTableTexture() in the virtual-sampling shader.
        // Then update the active VT resolver to the matching generation.
        terraFeedback.beginFrame(); // remove stale per-material page keys
    }
}
```

Promote only at a frame boundary with no outstanding old feedback
readback; it will refuse a switch while one is pending/unconsumed.
The old streamer is retained separately. After an **actual GPU fence**
confirms all prior commands no longer reference the old atlas/page
table, call `atlasSession.retireCompletedGpuWork()`. Never infer that
a frame delay alone is a GPU fence. Switching to the new streamer
changes page ID ownership: the application must swap its
material sampling bindings and feedback resolver in the same
frame. Continue `feedbackPass.updateStreaming(*atlasSession.active(),
budget)` thereafter. If the new archive or GPU allocation fails,
the previous active atlas remains unaffected.

For an unrotated material whose UVs select a subrectangle of
a shared Terra impostor atlas, use
`terraWeaveAtlasMappingForMaterial(version,material,mapping,layer)`.
It includes `Material::uvScale` and `uvOffset` in the virtual UVs.
Nonzero `uvRotation` currently returns false deliberately because
the compact feedback U0 virtual-UV mapping does not yet support
rotation. This avoids silently loading unrelated atlas pages.
A `setLiveMaterialMapper` callback can return the mapping for
the active Horizon atlas model (material with `baseTex=0`);
it must distinguish old/transition models using real model
identity, not just `Model::path`. Forest impostors use their
actual per-azimuth `ModelDraw::materials` overrides.

The writer works on one VT *sampling domain* at a time: all
image layers in one archive must have identical width/height.
Different-sized forest atlases may use separate VT domains,
or a build-time common atlas layout with explicit UV scale.
`TerraWeaveVirtualAtlasSession` handles one active domain;
it does not combine independent page-table resources.

The archive writer and session do **not** modify the main model
shader to sample virtual data. Actual page-table sampling must
be bound by the game's material/shader integration, using
`VirtualTextureSampling.h`. Until this step, the feedback
and disk streaming pipeline may operate but the main draw
still samples the regular material texture. The implementation
has not been compiled or exercised on the GPU yet.


## Opt-in virtual texture sampling in the main Model renderer

The VT page loader and feedback pipeline now connect to the **normal**
`Renderer3D` material fragment shader, rather than only rendering
feedback page IDs. New `engine/render/ModelVirtualTextureSampling.h`
reuses `engine/gfx/VirtualTextureSampling.h`'s existing physical
page lookup, page-table format and mip fallback implementation.

Call `Renderer3D::setModelVirtualTextureResolver(resolver)` to
provide a valid `ModelVirtualTextureBinding` for the exact
`Model` and `Material`. It contains:
`physicalAtlas`, `pageTable` (RGBA8 2D arrays),
`layer` and `gutterPixels`. Only a valid binding creates the
`kKeyModelVirtualTexture` variant of the regular model shader.
This shader reads the page table from `tex14`, the physical atlas
from `tex13`, and a single U6 vec4 holding virtual layer/gutter.
Both resources are selected at each draw, not cached by material
pointer across Horizon revisions.

The primary fragment shader's normal `tex1` base-colour sample
becomes `aiverseSampleVirtual(tex13,tex14,uv,layer,gutter,tex1Fallback)`
in GLSL or `aiverse_sample_virtual(...)` in Metal. It retains
the original material texture `tex1` as fallback if no virtual
page is resident, so regular materials, alpha masking, lighting,
team-colour blending, tint and LOD coverage remain unchanged
while streaming loads. **Because tex1 remains a fallback,
this first integration does not yet reclaim source texture
VRAM**; a separate fallback policy/resident coarse mip texture
will be needed before claiming VT memory savings.

Only ordinary built-in model shaders use the opt-in slots
`tex13/tex14` (bounded by GL's 16 texture units). Shader Graph
programs also use slots from 13 onward and therefore remain on
their existing sampling path, as do shadow and outline passes,
GPU ICB/indirect variants and per-vertex material-palette
batches. Model batching separates submissions if their
external VT resource pair/layer/gutter differs. To avoid
binding the wrong page table in existing GPU encoded commands,
a frame with any VT material falls back to CPU model submissions;
mixed CPU multi-draw indirect batches are also rejected.
No game genre is referenced in the shared sampler.

For the current TerraWeave integration, initialize the existing
`TerraWeaveVirtualTextureFeedback` bridge and
`TerraWeaveVirtualAtlasSession`, then install a resolver
that always reads the currently active revision:

```cpp
render::connectTerraWeaveVirtualTextureSampling(
    renderer, terraFeedback, atlasSession);
```

The helper is in
`engine/render/TerraWeaveVirtualAtlasArchive.h`. It confirms
that a material has an actual VT allocation in the feedback
bridge, checks that it matches the active archive's virtual
page dimensions, and returns `atlasSession.active()->physicalAtlas()`
plus `pageTableTexture()` as a *single consistent domain*.
Calling `atlasSession.promote(feedbackPass)` at the correct
frame boundary changes both bindings atomically for new draws.
Never release the old GPU atlas until its final fence is done.
The `TerraWeaveVirtualTextureFeedback::shutdown()` path also
clears its installed sampling resolver. Ensure the bridge
outlives any renderer callbacks that capture its reference,
and explicitly clear callbacks when replacing ownership.

**Unsupported or deferred:** custom Shader Graph VT sampling,
per-command VT identity for GPU ICB/MDI, fully virtualized
source texture residency, opaque-to-transparent transition
feedback, multiple independent Terra atlas domains in one
session, and validated texture/colour parity on every backend.
The existing source texture fallback is intentional for
correctness. Native build, CTest, GL shader compilation,
Metal compilation and actual Vulkan/GPU tests have not been run.


### Reduced source-texture fallback: actually saving GPU memory

`engine/render/ModelVirtualTextureFallback.h` introduces an opt-in,
pre-upload asset policy. Once a full-resolution source image has been
packed into an **immutable** VT page archive, a compatible model base
image may upload a much smaller regular `tex1` fallback rather than
the original full-resolution RGBA8 texture. The regular model shader
uses this small fallback until the virtual atlas has resident pages.
No change is needed in `ModelShaders.h`'s physical sampling function.

Use **before `render::uploadModel()`**, while the source RGBA8
pixels remain in `Image::rgba` or `Image::sharedRgba`:

```cpp
#include "engine/render/ModelVirtualTextureFallback.h"
// Or include TerraWeaveVirtualAtlasArchive.h for its Terra helper.

render::VirtualFallbackSavings estimate;
std::string reason;
bool reduced = render::configureModelVirtualFallback(
    model, sourceImageIndex, publishedArchive,
    /*maxDimension=*/64, &estimate, &reason);
if (!reduced) {
    // Keep the normal full-resolution upload; do not silently degrade
    // a material with alpha cutouts or shared non-base texture usage.
}
bool uploaded = render::uploadModel(dev, model);
```

For a Terra-generated model whose path begins with
`terraweave/`, the corresponding convenience method is
`configureTerraWeavePackedImageFallback(model,imageIndex,
archive,64,&estimate,&reason)`.

The policy verifies the VT archive's source dimensions, bounds
the fallback resolution (4–1024 pixels on the longest edge),
requires retained full RGBA8 input and forbids modification
after a native GPU or shared-cache image upload. It rejects
images reused as roughness, metal, normal, occlusion, emissive,
team-mask or Shader Graph input. All materials referencing the
same base image must be **opaque**, non-graph, alpha-independent
and not use `TeamMode::BaseAlpha`. The caller must separately
ensure dynamically supplied `ModelDraw::materials` overrides
do not require the original image. The fallback texture is
created as a standalone model-owned resource; it cannot poison
`ModelTextureCache`'s full-resolution shared GPU entry.

The fallback is downsampled by deterministic edge-clamped RGBA8
2×2 box filtering until `max(width,height) <= maxDimension`.
For example, a 2048×2048 RGBA8 base level is 16 MiB; a 64×64
base is 16 KiB before any GPU-generated mip/driver overhead.
The reported `VirtualFallbackSavings` is an **estimate of base
level byte counts**, not measured GPU VRAM or overall residency.
The full immutable source is in the disk VT archive; the GPU
owns only the coarse fallback plus the bounded physical page
atlas and page table (the latter are managed independently).

If the backend rejects the smaller fallback texture,
`uploadModel()` returns failure for the opted-in image and
retains its CPU source, so a retry or explicit fallback policy
change is possible. The non-VT image loading path is unchanged.
`releaseModel()` destroys the model-owned coarse image like any
other uploaded texture.

**Historical Stage 10F restriction (now superseded):** the
original coarse fallback implementation excluded masked Terra
Horizon forest clusters and Arbor impostors because their source
RGBA8 alpha supplied the cutout silhouette. The later section
**Full-resolution cutout alpha with reduced VT colour fallback**
adds a separate full-resolution R8 alpha resource and shader
variants for colour/shadow/selection/feedback. Masked images can
now use the explicit opt-in preserve-alpha policy, subject to
its material-compatibility checks and deferred GPU validation.

Native builds, GPU allocation accounting and visual comparisons
have not been run yet. Verify actual driver residency and
alpha/LOD quality when the user's deferred test phase begins.


### Full-resolution cutout alpha with reduced VT colour fallback

`engine/render/ModelVirtualTextureFallback.h` now supports
**alpha-preserving fallback** for opaque cutout models
(`Material::alpha == AlphaMode::Mask`). This closes the earlier
restriction that Terra Horizon/Arbor impostor atlases always had to
retain the full source RGBA8 GPU image.

After writing the full-resolution source RGBA8 to an immutable VT
archive, configure the model **before** calling `uploadModel()`:

```cpp
render::VirtualFallbackSavings estimated;
std::string reason;
bool reduced = render::configureTerraWeavePackedMaskedImageFallback(
    horizonModel, /*imageIndex=*/0, archiveVersion,
    /*maxDimension=*/64, &estimated, &reason);
if (reduced) {
    // Image::tex becomes reduced RGBA8 fallback, and
    // Image::virtualAlphaMask becomes full-resolution R8 coverage.
    bool uploaded = render::uploadModel(dev, horizonModel);
    // Check uploaded before publishing this model for rendering.
}
```

Generic models can use
`configureModelVirtualMaskedFallback(model,imageIndex,archive,...)`,
or `configureModelVirtualFallback(...,reason,true)` to request
the R8 mode explicitly. The default plain
`configureModelVirtualFallback()` still rejects alpha-masked
images unless preserve-alpha is enabled.

`Image::virtualFallbackMaxDimension` and
`Image::virtualFallbackPreserveAlpha` are opt-in CPU upload policies.
`uploadImage()` extracts the original full-size alpha channel into
a separate R8 GPU texture, including the image's existing repeat,
filter and mip policy. It independently downsamples the source
RGBA8 pixels via edge-clamped 2×2 box filtering into the small
`Image::tex` fallback. A failed R8 or coarse-texture GPU
allocation aborts the opted-in upload without discarding the
CPU source; any newly created R8 resource is cleaned up when
the coarse allocation fails. `releaseModel()` destroys both
model-owned handles.

The built-in model shader's new `VIRTUAL_ALPHA_MASK` permutation
uses `tex15.r` for original-resolution cutout coverage while its
base-colour `tex1` and VT `tex13/tex14` paths can use much smaller
colour resources. Both GLSL and MSL perform alpha clipping with
R8 for colour, sun/spot shadow and selection-mask passes. The
same source alpha is used for LOD-fading fragment rejection.
`VirtualTextureModelFeedback` has dedicated R8 red-channel
feedback pipelines; `VirtualTextureDeformedModelFeedback`
also reads full-resolution tex15.r for skin/baked/morph paths.
`GpuMaterialState` accounts for distinct alpha handles when
grouping batches. GPU ICB submissions are conservatively
disabled when an in-view model uses an external R8 mask,
because their per-command bindings do not encode tex15 yet.

For a 2048×2048 RGBA8 source, original base level = 16 MiB.
A 2048×2048 R8 alpha mask = 4 MiB; a 64×64 RGBA8 colour
fallback = 16 KiB. That is approximately **75% base-level
source-texture reduction**, excluding RHI mip chains, driver
alignment, the bounded physical VT atlas, page table, and
actual virtual pages resident in GPU memory. The
`VirtualFallbackSavings::fullResolutionAlphaBytes` field
records the calculated base-level R8 cost; these estimates
are **not measured VRAM statistics**.

Restrictions: `configureModelVirtualMaskedFallback()` accepts
masked but nonblended, non-graph source materials. It excludes
shared non-base textures, `TeamMode::BaseAlpha` and
`TeamMode::VertexAlpha`, and requires the full RGBA8 CPU
source before any existing GPU upload/cache lease. Dynamically
overridden `ModelDraw::materials` must follow the same
invariants; model-local validation cannot inspect unknown
future overrides. Material shader graphs occupy texture slots
13–15 and are deliberately not virtualized through this
built-in shader permutation. A valid VT mapping and packed
source archive are still required for high-detail colour.
Without them, the main scene retains a visually coarse
colour fallback while keeping the full-resolution cutout
silhouette. For alpha-only silhouette equivalence, compare
coverage with the original texture at multiple mip levels
on each GPU/backend; R8 and RGBA GPU mip filters may have
implementation-dependent rounding.

**Verification deferred:** GitHub source modifications committed;
native C++ compilation, GLSL/MSL shader compilation, Metal/
OpenGL/Vulkan rendering, shadow/selection silhouette tests
and actual driver VRAM accounting have NOT been run.


### Metal GPU-encoded ICB and CPU multi-draw for VT materials

The experimental native model batching paths can now handle **homogeneous**
VT material groups without disabling GPU-authored model draws for the
entire scene. The implementation builds on `GpuSceneDrawCommands` and
the existing Metal argument-buffer PBR ICB variant; it does **not**
implement a distinct VT render submission API or copy the atlas for
each command.

A homogeneous group requires the same built-in material GPU state,
vertex/index buffer bindings and an **exact match** for:
`ModelVirtualTextureBinding::physicalAtlas`,
`pageTable`, `layer` and `gutterPixels`.
The VT identity participates in the GPU material hash and cached group
signature, but hash collisions are still checked with exact binding
equality. R8 masked-cutout eligibility requires
`Image::virtualAlphaMask` and the same full-resolution R8 handle;
other alpha/blend and Shader Graph variants retain CPU fallback.

Metal's PBR ICB fragment argument buffer now includes the physical
VT `texture2d_array` (argument ID 26, formerly direct tex13) and
`texture2d_array` page table (argument ID 28, direct tex14), plus an
optional R8 alpha texture and sampler (IDs 30/31, direct tex15).
The physical atlas uses the existing material sampler `s1`; page
table sampling uses `read()`, so **no extra samplers** are encoded
for VT slots 13/14. The VT-specific fragment shader also receives
U6 (Metal buffer 7), holding layer and gutter. These external
textures are bound once for the complete compatible ICB group,
not individually for every GPU-encoded indexed command. GPU instance
matrix U0 remains per-command as before.

`GpuEncodedBatch` snapshots the external binding and R8 alpha
handle during compute preparation, and revalidates them immediately
before ICB execution. If a Horizon region switches its page-table
or physical atlas in between, that group is rejected and falls
back to normal CPU draws; cached groups are also invalidated when
VT identity changes. Homogeneous CPU MDI groups can likewise use
the VT material variant with shared fragment arguments; mixed groups
return to direct/instanced submission. An incompatible source
shader or unavailable Metal ICB PBR argument-buffer pipeline is
still a safe rejection, not a partial virtual draw.

**Experimental enabling:** the existing model batching switches and
`AIVERSE_GPU_AUTHORED_DRAWS=1`,
`AIVERSE_GPU_ENCODED_ICB=1` and
`AIVERSE_EXPERIMENTAL_METAL_ICB=1` (plus
`AIVERSE_METAL_CPU_MDI=1` for an independent CPU MDI experiment)
remain in effect. GPU-authored draws also require the renderer's
`setGpuAuthoredDrawEnabled(true)`, Metal compute support, and
supported Apple GPU argument buffers. Do not enable these options
by default or assume driver support from a successful source commit.

The reenabled GPU route currently targets only valid rigid
opaque or R8 cutout model primitives with conventional material
state and no shader graphs, GI, skinning, morph, transparency,
active dissolves or LOD fade. Its fallback maintains the existing
`Renderer3D` path on other platforms and for unsupported draws.
A fully general **per-command** VT resource index, heterogeneous
multi-domain ICB groups and Shader Graph VT are still future work.

**Verification deferred:** native compilation, Metal ICB PBR shader
compilation, actual GPU encoding/execution, visual comparison and
performance profiling have NOT run. There is no measured speedup
claim; this is a restored opt-in code path with conservative
resource identity checks.


## Multiple TerraWeave VT archives in one scene

`engine/render/TerraWeaveVirtualAtlasDomains.h` manages separate
virtual-texture domains (for example Horizon terrain, individual forest
impostor atlases, and other procedurally generated texture sets).
Each domain owns a `TerraWeaveVirtualAtlasSession` (immutable archive
revision and independent disk/GPU streamer), plus its own RGBA8
`VirtualTextureFeedbackPass` configured to that archive's virtual
tile dimensions. **Do not submit page IDs from different domains to
one feedback decoder:** the existing 32-bit page ID has coordinates,
mip and layer, but no atlas/domain field.

Initialize model feedback programs once using the pass-free
`initializeForDomains` entry point, then register independent
archives. No dummy legacy `VirtualTextureFeedbackPass` is required.
The model feedback programs and all per-domain passes **must use
the same depth-attachment mode**. Legacy `initialize(pass,...)`
continues to work for single-domain applications.

```cpp
#include "engine/render/TerraWeaveVirtualAtlasDomains.h"

// Render-thread objects: device and renderer outlive domains.
render::TerraWeaveVirtualTextureFeedback feedback;
feedback.initializeForDomains(dev, renderer, /*depthAttachment=*/true);
render::TerraWeaveVirtualAtlasDomains domains(dev);
render::TerraWeaveVirtualAtlasDomains::Options options;
options.feedbackWidth = 256;
options.feedbackHeight = 256;
options.feedbackDepth = true;
domains.addDomain(1, options);  // Horizon
domains.addDomain(2, options);  // Forest atlas

// After publishing immutable archive files:
domains.prepare(1, horizonVersion);
domains.prepare(2, forestVersion);
// Each graphics frame, before a pass is open, pump candidate streamers
// until their coarsest mip pages are resident.
domains.pumpCandidate(1, 8);
domains.pumpCandidate(2, 8);
if (domains.session(1)->candidateFallbackResident()) domains.promote(1);
if (domains.session(2)->candidateFallbackResident()) domains.promote(2);

// Install callbacks once, with active archives available.
domains.attach(renderer, feedback);

// Once per scene frame, after Device::beginFrame:
domains.pumpActive(8);
domains.beginFrame();
// Forest/Impostor: register the exact material override before submitting
// the same ModelDraw through the ordinary shared Renderer3D queue.
domains.registerDraw(forestDraw, 2);
renderer.drawModel(forestDraw);
// HorizonScene's active Model is private: assign its live DomainId
// through domains.setLiveDomainLookup(...) during scene setup.
renderer.render(...); // runs at most one archive's GPU feedback pass
```

The scheduler overrides the legacy single-pass slot only while
attached. Every `Options::feedbackDepth` value must match
`feedback.feedbackDepthAttachment()`; mismatches are rejected
instead of rendering feedback with incorrect depth state.

The render callback runs after shadow passes and before the main
colour pass. It selects one **idle** domain per frame by round-robin
order; only that domain's models output page IDs. All other models
still rasterize into feedback depth, so they occlude virtual pages
behind them correctly. Different domains may be sampled during
the **same primary scene frame**: `Renderer3D` resolves the
model's domain to its active physical atlas/page-table and binding
identity. CPU/Metal ICB batching stays valid for homogeneous
resource groups and separates different VT domains.

Use `domains.setLiveDomainLookup(fn)` for private Horizon region
models that cannot be registered by `registerDraw()`; its
callback receives the actual Model and Material and returns
`DomainId` and the domain-local layer. The stored per-frame
assignments and live lookup refer to real objects, not strings
like `terraweave/horizon` that may be reused across revisions.
The callback must not allocate GPU resources or read files.

`domains.pumpActive(budget)` submits **each completed domain
readback only to that domain's own** `VirtualTextureFileStreamer`,
then pumps bounded GPU uploads. The caller may explicitly request
coarse/visible pages with
`domains.requestPage(domainId,VirtualPageKey{...},pin)`.
A domain update uses `prepare(id,version)`,
`pumpCandidate(id,budget)`, then `promote(id)`. Promotion
creates a new feedback decoder for the new page-table geometry,
keeps old GPU atlas resources until explicit
`retireCompletedGpuWork(id)` after a real graphics fence,
and rejects promotion while that domain's readback is pending.
Other domains remain unaffected.

Disconnect with `domains.detach()` **before destroying**
Renderer3D/feedback; call `domains.shutdown()` only once
all GPU work using its resources is finished. The feedback
adapter still owns the shader programs and requires its
separate `shutdown()`. Multi-domain support does not require
modifying Terra's separate `feature/terraweave` branch.

Limitations: there is one VT feedback capture per scene frame by
default, not one per domain simultaneously. Domains not selected
for that frame may use manual CPU prefetch/previously resident
pages. The main shader still needs ordinary source-texture fallback
until its pages arrive. Multi-domain grouping remains homogeneous within any one GPU
ICB/MDI batch; a heterogeneous single-command resource table remains
future work. Shader Graph now supports an explicit material-only
`sampleVirtualTexture` node with a bounded one-extra-texture budget,
documented below. Native build, shader compilation, CTest and GPU tests
for this new manager have **not run**.


## Shader Graph virtual texture node

The normal material graph compiler now offers
`sampleVirtualTexture(uv)` as a material-only fragment operation.
Its `Program::usesVirtualTexture` flag selects a shader permutation
that uses the same `ModelVirtualTextureBinding` and physical
page-table lookup as normal `Renderer3D` materials.

`graph::compile` counts the node toward the existing sample
limit, rejects it in vertex/postprocessing/particle graphs, and
enforces an explicit **16-fragment-texture-slot** budget:
one or zero independent graph textures may coexist with the
two VT arrays. With zero additional graph textures the virtual
atlas/page table are `tex13/tex14`; with one, `tex14/tex15`.
These are bound per material draw by the existing resolver.
When the resolver returns no valid VT allocation, generated
`SG_SAMPLE_VIRTUAL(uv)` falls back to ordinary `tex1`
sampling so the shader remains usable while streaming or for
non-virtual materials.

The graph node may specify `"srgb":true` to request the existing
compiler's sRGB-to-linear approximation. Raw VT pages are
RGBA8 without any implicit shader-colour reinterpretation.
ShaderGraph GPU material equivalence/hash includes the new
flag. It is not available with the separate R8 reduction
policy for graph assets, and graph-backed models still use
direct/CPU submission rather than experimental Metal ICB.
The graph may continue sampling its own non-VT `sampleTexture`
resources independently.

See `docs/shader-graph.md` for the graph JSON/API example.
This is an opt-in **explicit node**, not automatic conversion of
every Shader Graph texture input into virtual storage.
Native C++ builds, MSL/GLSL compilers and GPU validation have
not yet run for the new material graph path.


## Per-axis virtual-texture address modes (Stage 10L, source only)

`render::ModelVirtualTextureBinding` now exposes `addressU` and `addressV`
using `gfx::SamplerAddressMode`. Defaults remain `ClampToEdge` on both axes
for binary/source-compatible legacy material resolvers. `Repeat` and
`MirrorRepeat` are supported independently along U/V in the shared GLSL/MSL
virtual-page lookup; the original tex1 fallback continues to use its normal
RHI sampler. The wrap policy is part of exact binding equality and the
GPU batching hash to prevent mixing incompatible material groups.

For repeat/mirror sampling, the archive must contain edge-safe gutters for
tiles at wrapping boundaries and the source material's ordinary sampler must
use the corresponding U/V modes. Updating only the virtual resolver without
updating the texture sampler/packer does not guarantee filtering parity.
`ClampToBorder` is intentionally rejected by `binding.valid()` because the
current RGBA8 page-table shader cannot reproduce an arbitrary border colour.

`ModelVirtualTextureUniforms` retains its existing 16-byte U6 layout:
`(layer, gutterPixels, addressU, addressV)`. The existing five-argument
lookup/six-argument sample shader helpers still compile as clamp-only wrappers;
opt-in callers pass both address modes as the final arguments. Built-in and
Shader Graph VT material variants now forward U6.zw, so wrap modes are honored
without allocating additional sampler slots.

**Validation:** source-level changes committed; C++/MSL/GLSL compilation,
GL/Metal/Vulkan renders, wrap-boundary filtering parity, batch identity tests
and GPU profiling remain **NOT RUN** in the current environment.


## Periodic and mirrored immutable VT page packing (Stage 10M)

The standard-library `tools/virtual_texture_pack.py` CLI now accepts
`--address-u clamp|repeat|mirror` and `--address-v clamp|repeat|mirror`
(default: clamp). Its `pack_pages(..., address_u=..., address_v=...)`
interface and `make_page`/`downsample_rgba` helpers apply the selected
per-axis modes to gutters **and** to odd-size mip box filtering.
For mirror, the edge texel is repeated at each reflection boundary.

An opt-in manifest contains `addressU` and `addressV` as strings;
legacy clamp/clamp output manifests are byte-for-byte unchanged in structure.
The archive reader / material resolver **does not yet automatically import
these modes** into `ModelVirtualTextureBinding`: configure the matching
`gfx::SamplerAddressMode` in your project and use the same conventional
source sampler mode for fallback tex1. Changing wrap policy changes archive
content; rebuild immutable pages and bump the archive version. This is not
a sparse-GPU page table implementation.

Example: `python3 tools/virtual_texture_pack.py --layer grass.rgba
--width 1024 --height 1024 --page-size 128 --gutter 2 --address-u repeat
--address-v repeat --output grass-pages`.

**Execution:** only source review; Python unit tests, CLI fixture, C++ native
build and GPU seam parity NOT RUN for this newly added behavior.


## Native C++ archive addressing and Terra propagation (Stage 10N)

`gfx::VirtualTextureArchivePackConfig` now has independent `addressU` and
`addressV` (`gfx::SamplerAddressMode`), both defaulting to ClampToEdge.
The native `VirtualTexturePageArchiveWriter` applies Repeat or MirrorRepeat
while building gutters and odd-dimension mips just like the standalone
Python packer. Explicit `ClampToBorder` is invalid: it would misrepresent
the source border color in the current physical page shader.

`VirtualTextureArchiveVersion` carries the addressing metadata through
`TerraWeaveVirtualAtlasSession::prepare/promote`, and the single-domain
`connectTerraWeaveVirtualTextureSampling` and multi-domain
`TerraWeaveVirtualAtlasDomains::resolveSampling` both forward active
version address modes to `ModelVirtualTextureBinding`.
Archive content-hash and manifest include address mode changes, so
one revision cannot silently reuse clamped bytes for repeating textures.
Legacy clamp-only archive hashes/manifests retain their former layout.

This automatic propagation applies to **C++ packed archives passed as
typed `VirtualTextureArchiveVersion` values**, not to Python-generated
manifest.json auto-loading. Projects using the standalone Python packer
must still read `addressU/V` themselves and assign matching typed
`VirtualTextureArchiveVersion` fields or binding modes explicitly.
Source texture samplers must agree. A live active archive is never
mutated; prepare a new immutable version and promote only after fallback
page residency and GPU work safety gates.

**Verification deferred:** C++ build, CTest, GPU page filtering and
cross-backend sampler parity NOT RUN for this stage.


## Validated immutable page archive import (Stage 10O)

`engine/gfx/VirtualTextureArchiveManifest.h` provides
`gfx::importVirtualTextureArchiveManifest(Json, directory, out, error, ...)`
and `gfx::loadVirtualTextureArchiveManifest(directory, out, error, ...)`.
Both accept native C++ archive-writer and standalone Python packer
`manifest.json` records (schema `aiverse.vt.disk-pages.v1`).
The reader uses the existing `engine/data/Json.h` implementation and
does not add another JSON library.

Only a caller-selected trusted directory is used: JSON cannot provide
paths. Required source dimensions, physical tile size/gutter, layers,
mip-level geometry and optional `pageCount`/`archiveBytes` must agree
with computed values and configured upper limits; every expected page
must be a regular non-symlink file of exactly `pageSize² * 4` bytes.
Mip/layer directories are also checked against symlinks. Optional
`addressU/V` import clamp/repeat/mirror as typed sampler policy.
Unsupported `ClampToBorder`, malformed metadata, incomplete archives
and invalid source hash shape fail closed. The optional source SHA-256
list and native FNV-64 are metadata, **not cryptographic authentication
of page files**; an external signed/immutable content distribution
policy is still required.

The importer fills `VirtualTextureArchiveVersion` only on complete
success. Existing active GPU state remains untouched; callers use the
returned version in their normal prepare/pump/promote sequence.
The importer performs synchronous filesystem metadata reads and must
not run every graphics frame or concurrently with archive mutation.
The Python manifest has no native revision/hash: callers supply a
trusted `importedRevision` if needed. Native writer manifests carry
their own revision.

**Verification:** source commits only; C++ compilation and CPU/GPU
integration tests NOT RUN.


## TerraWeave archive-directory prepare (Stage 10P)

`TerraWeaveVirtualAtlasSession::prepareFromArchiveDirectory(directory,
physicalPages, maxBufferedPages, importedRevision, maxArchiveBytes,
maxPages)` imports a trusted immutable archive using the bounded
`gfx::loadVirtualTextureArchiveManifest` validator and then runs
the existing candidate preparation path. A parse/metadata/disk error
leaves any active/candidate resources unchanged and is available via
`session.lastError()`; it does not promote an invalid archive.

For multiple archives use
`TerraWeaveVirtualAtlasDomains::prepareFromArchiveDirectory(domainId,
directory, importedRevision, maxArchiveBytes, maxPages)`.
Each domain retains its configured physical-page/CPU staging budgets.
After success call the existing `pumpCandidate`, wait for fallback
pages, `promote` and eventually retire old GPU resources **only after
a real fence**. The model sampling resolver uses active-version wrap
policy automatically; no manual U/V sampler reconstruction is needed
for these imported archives. The original source tex1 fallback sampler
must still match the source addressing.

This is a *synchronous staging API*, not a GPU streaming/background
filesystem mount. The source remains uncompiled and untested on native
devices in this conversation.


## Wrap-aware GPU virtual texture feedback (Stage 10Q)

The **virtual page feedback** path now shares the same per-axis
ClampToEdge/Repeat/MirrorRepeat addressing used by physical VT sampling.
`gfx::virtualTextureFeedbackPageAtUV(u,v,mip,layer,tilesX,tilesY,
addressU,addressV)` is the CPU selection equivalent; its historical
six-argument overload retains normalized-only rejection for backward
compatibility. Both GLSL and Metal `aiverseMaterialFeedback` helpers
also retain their original five-argument functions while adding
per-axis address-mode arguments.

`VirtualTextureMaterialMapping::addressU/addressV` default to clamp.
TerraWeave's single- and multi-domain archive mapping inherits these
from the active typed `VirtualTextureArchiveVersion`. The rigid model
feedback U0 now has an appended 16-byte `addressModes` vector
(total `sizeof(VirtualTextureFeedbackUniforms)==192`). Deformed model
feedback U6 appends the same vector (total 112 bytes). Its GLSL/MSL
shader implementations use those modes when calculating page feedback
IDs. Unsupported border modes fail closed instead of indexing a
different physical page.

Automatic mip derivation intentionally uses the **unwrapped** UV
derivatives to avoid discontinuities in `fract` at tile borders;
addressing is applied only to page-ID selection afterward. This is
crucial for UVs greater than one, negative UVs, and mirrored repeats.
Shader ABI changes affect only the dedicated feedback pipeline, not
authoritative ECS or the regular material uniforms. The existing
legacy helper overloads remain compatible, but native shader
recompilation for the new U0/U6 layouts is required.

**Known scope limit:** direct CPU visible-region prefetch still assumes
normalized 0–1 UV rectangles; arbitrary wrapped UV rectangles require
an upstream split/wrap visibility adapter. GPU feedback captures use
the new policy. Native builds, GLSL/MSL compilation and readback/GPU
fidelity remain **NOT RUN**.


## Bounded wrapped CPU VT visibility/prefetch (Stage 10R)

The standalone header `engine/gfx/VirtualTexturePrefetchWrap.h` adds
`gfx::normalizeWrappedVirtualTextureRegions(sourceRegions,
addressU,addressV,output,maxOutputRegions)`.
The helper splits each potentially negative/out-of-range visibility
rectangle on repeat/mirror integer UV seams, maps each monotonic piece
into normalized [0,1], and returns only bounded half-open
`VirtualTextureVisibleRegion` rectangles. Full repeated periods
collapse to one [0,1] interval, preventing unbounded CPU expansion.
Clamp retains the original normalized-range clipping behavior.

`gfx::prefetchWrappedVirtualPages(streamer,planner,views,addressU,addressV)`
in `VirtualTexturePrefetchAdapter.h` runs the normal exact-residency,
coarse-first budgeted disk prefetch only after successful expansion.
On invalid/non-finite coordinates, unsupported address modes, excessive
raw UV magnitude or output region-count overflow, it submits *no*
partial work and returns `VirtualTexturePrefetchDispatch::wrapRejected`.
The existing `prefetchVisibleVirtualPages` entry point and result
fields remain compatible.

This is independent of camera/world/game genre. Callers must provide
actual projected UV rectangles for a known archive domain and use its
active version U/V policy. One domain's regions must not be sent to
another domain's streamer. The helper does not fetch files on worker
threads and never updates ECS authoritative game state.

**Validation:** source changes committed; C++ build/CTest and
project-level wrapped UV visibility tests NOT RUN. Limitations include
CPU-side rectangular overfetch for highly rotated/nonrectangular visible
surfaces and no GPU filtering/seam parity proof yet.


## Shader Graph virtual textures with up to four source textures on Metal — Stage 10S

The material graph compiler now permits the original four independent
`sampleTexture` / `sampleTextureLod` source paths together with the
explicit `sampleVirtualTexture` node. Graphs remain backend-neutral;
their actual fragment texture binding budget is resolved by the renderer,
never assumed by source JSON.

Metal's existing RHI supports fragment texture indices 0..31 while
fragment sampler indices 0..15 remain separate. The four authored graph
textures bind at tex13..tex16; tex16 uses the existing constexpr sampler.
The two virtual atlas/page-table arrays then use tex17/tex18, reusing
material sampler s1 for the physical atlas and a texel read for the table.
This requires no new Metal fragment *sampler* slot. Graphs with zero to
three authored textures use lower free indices as appropriate.

For GL33, GLES3 and Vulkan, the current RHI binding budget stays at
16 texture slots. An explicit graph virtual sample retains its
conventional tex1 fallback if the additional two resources cannot be
bound, while the four-source graph (which needs tex16) fails closed
rather than silently dropping its fourth texture. Shader Graph's
non-VT behavior, material equivalence/hashing, original texture
bindings and the conservative non-ICB graph drawing path are retained.

The same layout helper is used for pipeline permutation selection
and for actual `setTexture`/U6 draw bindings to avoid conflicting
atlas/table slots. The injection helper now requires an explicit Metal
extended-slot opt-in to permit a page table beyond tex15. It still
rejects unsupported addresses, malformed shader anchors and other
unrepresentable bindings.

**Verification:** source-only change committed. No native C++ build,
MSL/GLSL compiler, GPU capture or cross-backend image parity has
been run. This is not a bindless material system, arbitrary source
texture count, or heterogeneous per-command ICB descriptor table.


## RHI fragment texture resource capacity — Stage 10T

`gfx::Device::maxFragmentTextureSlots()` now exposes how many
independently bound **texture resources** a renderer may address
with `setTexture(slot, handle)`, not how many dynamic sampler
objects it may bind. The default portable RHI contract remains 16.
Metal advertises 32 texture indices, while its dynamic sampler
indices still stop at 15 (high-slot material graphs use a
compile-time sampler). Vulkan reports the selected device's
actual descriptor texture capacity, which can be below its
compiled-in upper bound. GL33/GLES3/WebGL2 retain the portable
16-slot default.

The Shader Graph material VT allocator now uses this RHI
capability directly instead of testing a hard-coded backend name.
The first four normal graph texture sources are still authored
at tex13..16; a device capable of binding only 16 resources
rejects the unrepresentable fourth source. A VT graph requests
two additional independent texture slots and falls back to
ordinary tex1 when those two slots cannot be allocated.
Only a Metal RHI with the extended slot capacity opts into
the MSL virtual-resource injection above tex15.

This is a non-breaking, read-only Device API; consumers should
query it during pipeline material layout selection, and must not
confuse texture resource indices with sampler indices.
The per-command Metal ICB descriptor table is still not
implemented. C++ compilation and native backend tests for
this stage are NOT RUN.


## Shader Graph shader cache identity correctness — Stage 10U

`Renderer3D::ModelShaderKey` now compares all shader-generating
graph properties, not just GLSL fragment/vertex source, source texture
**count** and output map. The equality path now covers material
domain, GLSL/MSL fragment sources, GLSL/MSL vertex sources, exact
source texture paths, output connections, the `usesVirtualTexture`
feature flag, and the material quality selector. Pointer-identical
immutable graphs still use the existing quick path.

This prevents an MSL-only difference, source texture set change or
VT-material permutation from aliasing an unrelated shader cache
entry on another backend. The safety improvement does not add a
dynamic GPU material descriptor table or enable graphs in Metal ICB;
it protects the existing direct-rendering shader and pipeline
cache. Any cached shaders remain GPU resources and must be retired
according to their usual device lifetime.

**Verification:** source inspection and commit, no native compiler,
shader compiler or GPU tests run for this change.


## Frame-scoped heterogeneous material resource identities — Stage 10V

`engine/render/GpuMaterialResourceTable.h` adds a genre-neutral,
header-only, **CPU staging** `render::GpuMaterialResourceTable` for
future heterogeneous material/Metal ICB command resources. It
is directly accessible from native game projects; no Lua runtime or
genre dependency is required.

`GpuMaterialResourceBinding` stores up to four ordinary Shader Graph
source handles, optional full-resolution alpha coverage and an
optional exact `ModelVirtualTextureBinding` (physical atlas and
page-table handles, virtual layer, gutter and per-axis address modes).
`table.beginFrame(frameSerial)` increments a table generation and
invalidates stale resource tickets even when the caller begins another
stage with the same frame serial. `table.intern(binding)` deduplicates
only exact bindings after hash-bucket lookup. The table is bounded
(default 4096, max constructor limit 65536), fails closed on invalid
handles and returns a typed `GpuMaterialResourceTicket` containing
frame serial, generation and index.

`table.matches(ticket, current)` must be called before GPU execution,
so archive promotion, source texture reloading, alpha mask or wrap
changes cannot silently reuse the original descriptor identity.
`table.resolve(ticket, out)` copies an immutable resource snapshot,
never exposing unstable vector pointers. Neither the table nor its
tickets hold GPU resources alive; fences and texture lifetime are
still owned by the renderer. Call `reset()` on device/scene shutdown
and invalidate any cached command snapshots.

**This is NOT yet an executable heterogeneous Metal ICB GPU argument
buffer.** The existing ICB path remains homogeneous with CPU fallback.
The header establishes a bounded identity and validation contract
before extending GPU-side per-command resource indices and the MSL
material shader ABI. No native builds, CTest, GPU compilations or
device tests have been run for this new header.


## GPU material resource ticket validation at ICB submission — Stage 10W

The experimental `Renderer3D` Metal ICB workflow now uses the existing
`GpuMaterialResourceTable` as a **CPU safety gate** for its *homogeneous*
command groups. `prepareGpuModelDraws` starts a new staging generation
for every scene-frame preparation (including ICB-disabled paths).
After GPU command encoding, a group interns its exact captured
`ModelVirtualTextureBinding` and optional R8 alpha coverage and retains
the generation-qualified ticket. Invalid/capacity-exhausted entries
are rejected: prepared ICB commands are never submitted and the regular
draw path remains eligible.

Immediately before executing a prepared group, `renderModels`
resolves the current VT archive resources and alpha coverage for **every**
model and calls `GpuMaterialResourceTable::matches` in addition to the
pre-existing strict group compatibility checks. Any archive swap, page
table change, wrap mode change, alpha-mask change, stale ticket or
new staging generation rejects GPU submission, preserving CPU fallback.
`shutdownModels` invalidates tickets before model GPU handles retire.
The table does not own textures or alter GPU command shader layouts.

**No heterogeneous Metal ICB is enabled yet.** Different materials still
require compatible parent fragment state; real per-command texture/VT
resource indexing requires a new Metal shader and argument-buffer ABI.
This stage makes lifecycle and identity verification explicit before
that extension. **C++ compilation/CTest/GPU tests NOT RUN.**


## Experimental GPU-encoded Metal ICB per-command VT argument buffers — Stage 10X

The RHI now exposes `gfx::GpuIcbVirtualMaterial` and the optional
`Device::prepareGpuEncodedIndexedIcbWithVirtualMaterials(..., materials)`
entry point. The descriptor carries native TextureHandles for the
physical VT atlas, page table, optional full-resolution R8 alpha mask,
and the four-float U6 (virtual layer, gutter, U/V address modes).
Unsupported backends explicitly return an invalid token (zero);
the original `prepareGpuEncodedIndexedIcb` signature and path
remain unchanged.

The Metal backend implements a source-level **opt-in** variant gated by
`AIVERSE_EXPERIMENTAL_ICB_PER_COMMAND_VT=1`, in addition to the
existing GPU ICB/Metal experimental safety flags. A compute-encoded
ICB command now supports a per-command PBR argument-buffer offset
(4096-byte aligned stride) and a per-command U6 offset (256-byte
stride). When this mode is disabled, both strides are zero and the
legacy shared PBR/U6 buffers are used. Each native VT atlas/table is
checked against its original handle before execution; it is bound
through the existing `IcbPbrTextures` IDs 26/28, with optional alpha
at ID 30 (sampler ID 31). This does not require a new material shader
permutation or additional fragment sampler slots. All referenced
textures must remain alive through the actual GPU fence.

`Renderer3D::prepareGpuModelDraws` optionally supplies per-command
descriptors for already-compatible VT material groups. Grouping still
requires the exact same original material state and external VT
binding as before: **this stage exercises the per-command Metal
argument-buffer ABI only, and does not yet batch heterogeneous VT
materials**. Stage 10W's CPU ticket revalidation remains in place,
and stale/invalid descriptors reject execution through the original
CPU fallback. The legacy path remains selected by default.

**Verification:** only GitHub source inspection and commits;
Objective-C++ compilation, native MSL compiler, real Metal ICB
execution and visual/performance checks **NOT RUN**. Treat this GPU
path as experimental/uncertified, and do not enable the environment
flag in shipped games.


## Explicit opt-in mixed virtual-atlas Metal ICB groups — Stage 10Y

`Renderer3D` can now *in source* assemble a Metal GPU-encoded ICB
group with differing **VT atlas/page-table bindings** when all draws
otherwise share the same equivalent built-in PBR material state,
geometry buffers, shader variant, and render flags. Two independent
experimental flags are required:

```sh
AIVERSE_EXPERIMENTAL_ICB_PER_COMMAND_VT=1
AIVERSE_EXPERIMENTAL_ICB_MIXED_VT=1
```

These flags do not replace the existing
`AIVERSE_GPU_AUTHORED_DRAWS`, `AIVERSE_GPU_ENCODED_ICB` and
`AIVERSE_EXPERIMENTAL_METAL_ICB` safety gates. **The feature is
disabled by default.** Ordinary CPU MDI/instancing and direct draw
paths retain exact VT resource compatibility.

In mixed mode, batch preflight uses normal `equivalentGpuMaterial`
for exact PBR state and material shader compatibility, but permits
different VT resource identities between commands that *both*
require valid VT sampling. CPU hash prefilters omit only the VT
archive identity while preserving the no-VT/VT distinction. Every
GPU command records its own `GpuIcbVirtualMaterial` descriptor
(physical atlas, page table, layer, gutter, U/V wrap, optional alpha
coverage) and its own generation-safe
`GpuMaterialResourceTicket`. Before execution, every live
per-command binding is re-resolved and checked against its captured
ticket. Failed or stale groups remain eligible for ordinary CPU
draw submission; the GPU path cannot silently inherit the first
command's atlas.

The `drawModelBatch` homogeneous VT check is bypassed **only**
while an already-prevalidated opt-in per-command GPU ICB submission
is active. Normal CPU batches still reject mismatched atlases.
Metal's optional per-command fragment argument-buffer/U6 ABI from
Stage 10X supplies the distinct VT resources at GPU execution.

**Important limits:** this supports heterogeneous VT bindings,
*not* arbitrary different source PBR textures or Shader Graph
materials in one command group. Full shader/material-state
heterogeneity needs additional per-command U2/source texture ABI.
Metal command-encoder compilation and real GPU execution are
**NOT RUN**; do not enable these flags in release builds or
treat this as a verified performance optimization.


## Executed mixed-VT ICB diagnostics — Stage 10Z

`Renderer3D::gpuModelDrawStats()` now includes two additional
execution-only counters:
`perCommandVtExecutions` and `mixedVtExecutions`.
They count successful GPU ICB submissions after the device's
`executeGpuEncodedIndexedIcb` call reports success, **not**
mere candidate groups, buffer preparations or fallback draws.
The mixed counter increments only when that accepted command
group contains two or more non-identical
`ModelVirtualTextureBinding` values. The per-command counter
includes both mixed and homogeneous opt-in GPU batches.

The renderer resets these counts each `prepareGpuModelDraws`
cycle. Existing `gpuIcbPrepared`, `gpuIcbRejected` and
`gpuIcbExecuteFailed` remain available to distinguish
staging, validity gate rejection and actual execution failures.
Reading a nonzero new counter is NOT evidence of image
correctness or GPU performance improvement: a real backend
A/B capture is still required.

This stage is a diagnostics/API addition only. No native C++
compilation, Metal shader compilation, real ICB execution or GPU
performance tests were run in this environment.


## Fail-closed per-command Metal VT native resource validation — Stage 10AA

Before encoding any experimental per-command VT ICB, the Metal
backend now requires both the physical page atlas and the page
table to be actual `Texture2DArray` `RGBA8` resources. The
optional cutout coverage must be an `R8` `Texture2D`.
It also checks native slice counts, the requested logical
virtual layer against the page-table layer count and physically
feasible gutter inset based on the atlas dimensions.

An incompatible resource rejects the whole GPU group before
encoding rather than passing mismatched texture types to the
shader. The ordinary CPU draw fallback and existing legacy
ICB resource binding are unchanged. This restricts the
experimental path to the currently implemented RGBA8 VT
archive contract; typed HDR/alternative formats require an
explicitly compatible MSL shader variant in a later stage.

**Verification:** source review and GitHub commit only; native
Objective-C++, Metal shader compiler and actual GPU execution
NOT RUN. Leave experimental flags disabled in production.


## Conservative opaque-PBR pipeline key and per-command resource signatures — Stage 10AB

`render::equivalentGpuIcbPbrPipelineMaterial(a,b)` and
`render::gpuIcbPbrPipelineHash(material)` define a conservative
experiment-only grouping key for future per-command PBR resources.
Only built-in opaque materials without Shader Graph or transparency
are admitted. Double-sided/cast/receive-shadow and the normal-map
presence bit remain part of pipeline compatibility, while ordinary
PBR texture content/handles and U2 factors are intended to vary per
GPU command. Hashes are only prefilters: exact pipeline comparison
must still gate grouping.

`GpuMaterialResourceBinding` now optionally contains
`pbrEnabled`, six texture handles corresponding to fragment slots
1..6, and 36 floats representing the exact nine float4 rows of
ModelShaders' U2. It checks finite U2 values and valid texture handles,
and includes the full source handles/U2 in the resource-table hash
and exact equality. Existing VT/alpha and frame-generation ticket
semantics remain intact. These are CPU-only primitives; they do not
yet change GPU command submission.

Only source inspection and GitHub commits were performed. Native
build, tests and Metal GPU execution remain NOT RUN.
