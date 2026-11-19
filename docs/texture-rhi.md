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

**Important alpha limitation:** existing Terra Horizon forest
cluster atlases and many Arbor impostors use `AlphaMode::Mask`
for silhouette clipping. They are deliberately NOT downsampled
by this stage: a coarse alpha mask would alter the visible
silhouette in the scene and shadow passes. Supporting them
without keeping full RGBA8 in VRAM requires a separate native
full-resolution alpha-only mask or another equivalent coverage
solution in every rendering pass. Until then the opaque-image
optimization is intentionally conservative.

Native builds, GPU allocation accounting and visual comparisons
have not been run yet. Verify actual driver residency and
alpha/LOD quality when the user's deferred test phase begins.
