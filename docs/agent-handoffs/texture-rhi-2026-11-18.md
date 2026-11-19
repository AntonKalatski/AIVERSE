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

## Stage 2B — GL/GLES native 3D, 2D arrays and cubemaps (2026-11-18)

**Code commit:** `5fd11d2f638d77ae281c24a7e2bc91f50f8f19fa`.
**Paths:** `engine/gfx/gl/GfxGL.cpp`, `engine/gfx/gl/GLApi.h`.

**Implemented in source:** Keep the original 2D allocation code; new
`GLDevice::createDimensionalTexture` handles `Texture3D`,
`Texture2DArray`, `Cube` using native `GL_TEXTURE_3D`,
`GL_TEXTURE_2D_ARRAY`, `GL_TEXTURE_CUBE_MAP`. For arrays/volumes
the API uploads contiguous RGBA/R8/HDR payloads, optionally generates
mipmaps and accepts explicit allocation levels. Compressed 2D array/cube
levels can be passed as complete contiguous per-level payloads when the
driver supports the block compression format. `GLTexture` tracks
target/dimension/layers, `setTexture` uses the correct target,
and `updateTextureRegion` handles array face/mip/volume updates
with row-by-row upload of non-tight row pitches.
Windows GL loader adds `glTexImage3D`, `glTexSubImage3D`,
`glCompressedTexImage3D` function pointers and required core constants.

**Explicitly unsupported:** GL/GLES/WebGL universal `Texture1D` and
`CubeArray` (these need platform-specific branching or extensions).
Non-2D render targets, any GL storage image, MSAA and the new
`TextureViewHandle`/`SamplerHandle` API. GL code paths do not yet
provide independent sampler objects. Vulkan remains 2D-only. Some
compressed-array driver combinations may reject allocation via
`glGetError`; GPU support is runtime-dependent.

**Compatibility:** 2D legacy texture path retained, except a new
fail-closed check for previously ignored `storage` or unsupported
MSAA/explicit allocation. Sampler dimension in GLSL must match the
created GL texture target. The editor, game format and ECS are unchanged.

**Validation:** Windows, Linux/GLES, Android and Emscripten compile:
**NOT RUN**. OpenGL GPU validation: **NOT RUN**. No CI passing claim.

**Next:** Ensure Windows GL loader links/initializes all new pointers and
GLES3/WebGL2 headers expose the functions, then add dimension-sampling
GPU tests in the GL context test runner. Implement Vulkan image types,
array layer barriers, sampled image views and full subresource staging
next; do **not** copy only `createImage` without checking all upload,
clear, mip generation and render pass transitions.

## Stage 2C — Vulkan sampled dimensions, layers and barriers (2026-11-18)

**Code commits:** `ddb536106ffd941f80c797dad3d231d0c07e21f2`,
`daa721ef0ce9ebb0aa16ed1c1a34c05711295aca`.
**Paths:** `engine/gfx/vulkan/VkDevice.h`,
`engine/gfx/vulkan/VkResources.cpp`, `engine/gfx/vulkan/VkDraw.cpp`,
`engine/gfx/vulkan/GfxVulkan.cpp`.

**Implemented in source:** `VTexture` tracks dimension, 3D depth and image
array layers. Vulkan image/image-view creation uses matching
`VK_IMAGE_TYPE_1D/2D/3D` and `VK_IMAGE_VIEW_TYPE_1D/2D/3D/2D_ARRAY/CUBE/CUBE_ARRAY`.
Cube-compatible create flags and six-face layer counts are applied.
Memory transitions and clear operations now cover the whole array layer range;
mip blits handle all layers and volume depth. Staging uploads can specify
`z/depth/baseArrayLayer/layerCount`, including full compressed array/cube
mip data and uncompressed 3D data. `updateTextureRegion` accepts
uncompressed volume/slice/mip writes, repacking strided inputs as needed.
Regular texture binding works through the default dimension-aware image view.
`imageCubeArray` is enabled only if available and
`supportsTextureDimension(CubeArray)` reflects that feature.

**Compatibility:** Legacy 2D Vulkan image creation and descriptor binding
retain the same default types/shape and existing `setTexture` path.
`copyColor` now explicitly rejects non-2D images rather than issuing
an invalid 2D blit. Non-2D depth targets, layered render attachments,
MSAA, storage image writes and explicit `TextureView`/`SamplerHandle`
are NOT implemented. RHI source changes do not affect native ECS or saves.

**Risks:** Vulkan's staging allocator reports failure via an existing
void-return `uploadImage` helper; texture-creation callers do not yet
propagate staging allocation failure as an invalid texture handle.
The new layout transition and mip blit paths must be run through Vulkan
validation layers (including partial mip/slice updates and cube-array views).
Compressed 1D and 3D are explicitly rejected; malformed/truncated
mip-level payloads now fail before GPU allocation. Device feature support,
compressed format capabilities and out-of-memory paths need GPU tests.

**Validation:** Source/GitHub commits succeeded. Native Vulkan compilation,
CTests, Lavapipe and validation layers: **NOT RUN**. No passing CI is
asserted. The current code is not production-certified until these gates pass.

**Next actionable step:** Build `aiverse_gfx_vulkan` and
`aiverse_vulkan_tests`, add automated real-GPU/lavapipe sampling tests
for 1D/2DArray/3D/Cube/CubeArray including upload and mip sampling.
Then validate GL/Metal on supported devices and fix failures.
Only afterwards advance to Stage 3 (views/independent samplers).
Use a **new code commit and separate handoff commit** for each substage.

## Stage 2D — Vulkan dimension-sampling integration test source (2026-11-18)

**Code commit:** `5392904f281c2bfdcf282d1bb70e89ed32c77be4`.
**Path:** `tests/render/vulkan_tests.cpp` only.

**Added:** Integration checks for sampled `Texture1D`, `Texture3D`,
`Texture2DArray`, `Cube`, and conditionally `CubeArray`. Each uses
an actual Vulkan texture resource, a GLSL shader with the matching
`sampler1D/3D/2DArray/Cube/CubeArray` type, a screen draw, capture and
colour assertion. Cube arrays are skipped only if the native Vulkan
feature query reports unsupported. The checks run inside existing
`vulkan_backend` CTest when that target is built; no new executable.

**Test status:** test source committed, but **NOT RUN** (native Vulkan
build and driver unavailable in this session). It is NOT correct to
claim the tests passed. Existing Vulkan test runner skips GPU work when
no Vulkan driver can be created; inspect its output to distinguish
a real GPU pass from skipped execution.

**Immediate next step:** Run `cmake --build build/texture-rhi --target
aiverse_vulkan_tests` then `ctest --test-dir build/texture-rhi
--output-on-failure -R '^vulkan_backend$'` on Vulkan SDK+Lavapipe or
native GPU. Fix compiler errors or colour mismatches as needed and
record the actual results in the next handoff. Verify array-layer
readback/update separately. Then continue stage 3 texture views
and independent samplers.

## Stage 3A — CPU view/sampler contract validation (2026-11-18)

**Code commit:** `bee8464f0bbffefde854ff2b13fdb60df4ddf74b`.
**Paths:** `engine/gfx/Gfx.h`, `tests/render/texture_contract.cpp`.

**Implemented in source:** `textureEffectiveMipLevels(TextureDesc)`,
`validateTextureViewDesc(TextureDesc, TextureViewDesc)` and
`validateSamplerDesc(SamplerDesc)`. Bounds checks include valid mip
and array-layer intervals, image aspect (color vs depth), 2D array
single-slice 2D views, and cube-aligned CubeArray subset views.
Sampler limits reject nonfinite LOD/bias/anisotropy, negative minLod,
maxLod below minLod, or anisotropy below 1. Added positive/negative
cases to CPU-only `texture_contract` CTest.

**Compatibility:** Existing public API signatures and 2D defaults
preserved. Only new helpers and tests added; no backend behavior changes
and no game save/network/ECS changes. `TextureViewDesc` still has
mipLevelCount=1 by default: callers requesting all mips must set it.

**What is NOT implemented:** Native `TextureViewHandle`/
`SamplerHandle` objects or binding; the validators are not a GPU
capability claim. Metal, GL/GLES and Vulkan still return unsupported
defaults for view/sampler resource calls.

**Verification:** GitHub source commit succeeded; all four GitHub
workflow runs on prior commit `1640d5...` were **queued**, not
passed at read time. Local compiler/CTest/GPU execution: **NOT RUN**
because the working runtime cannot resolve `github.com` to clone
the full repository. Do not report these tests as passing.

**Next stage:** Implement independent texture views and sampler
objects in one backend at a time, beginning with Metal. Validate
requested view dimensions against *actual* mip/layer counts in
addition to CPU descriptor helpers. Destroy dependent views safely
when parent texture dies and explicitly invalidate stale handles.
Add native tests, verify no regressions in legacy setTexture path.
Commit each backend stage and a separate handoff.

## Stage 3B — Metal native texture views and samplers (2026-11-18)

**Code commit:** `7984d46490ab3f8e59acaa00ad69fd62fadfcfe3`.
**Path:** `engine/gfx/metal/GfxMetal.mm`.

**Implemented in source:** `MetalDevice::createTextureView` validates a
requested mip/slice/aspect against the actual native texture's mip levels,
creates a Metal texture view with `newTextureViewWithPixelFormat:
textureType:levels:slices:`, and tracks its parent by texture ID.
`createSampler` maps min/mag/mip filters, U/V/W address modes,
depth-compare function, LOD range and anisotropy (capped at 16)
to `MTLSamplerDescriptor`. Metal has no independent sampler LOD bias
in this API; nonzero `lodBias` is rejected instead of ignored.
`bindTextureView` binds the selected view + selected sampler (or the
texture's original sampler if none supplied) to vertex and fragment
stages for slots 0..15. Parent texture destruction invalidates all
dependent view handles before the texture ID can be reused; views and
independent samplers have explicit destroy operations.

**Compatibility:** Existing `setTexture` still uses a texture's original
sampler. The new APIs are opt-in; no existing shaders need rewriting.
This does not add new save/network/ECS state. The generic RHI allows
views of selected array slices/mips; Metal implementation additionally
checks physical backing allocation.

**Not done:** No non-2D render-target attachments yet. View binding
does not rewrite `PassDesc`, and storage/compute binding of an explicit
view is not exposed. Independent samplers are exposed only for direct
render encoder bindings; the ICB path needs separate verification.
No Metal GPU tests are added or executed in this stage.
Other backends still have the default unsupported view/sampler methods.

**Validation:** GitHub commit succeeded; actual macOS/iOS compilation,
GPU texture view/sampler sampling and existing scene regression tests:
**NOT RUN**. No successful CI result claimed.

**Next:** Compile Metal (especially SDK availability of border
addressing and texture view APIs), run MSL shaders sampling nonzero
array layers and mip levels with separate samplers, test that destroying
parent invalidates views, and review view handle recycling in long
sessions. Next implement native Vulkan and GL sampler/view support;
GL ES3/WebGL2 lacks general texture-view extensions, so report
unsupported there rather than silently creating aliases.

## Stage 3C — GL33/GLES3 independent sampler objects + identity views (2026-11-18)

**Code commit:** `b945c6a6c24d571e29e31f70a7534cafca13e7ee`.
**Paths:** `engine/gfx/gl/GfxGL.cpp`, `engine/gfx/gl/GLApi.h`.

**Implemented in source:** Windows GL33 loader gains the core 3.3
`glGenSamplers/glDeleteSamplers/glBindSampler/glSamplerParameteri/
glSamplerParameterf` entry points and relevant enum values.
`GLDevice::createSampler` exposes independent min/mag/mip filtering,
U/V/W address modes, compare function, min/max LOD and optional
anisotropic sampling where an extension is available. Desktop-only
LOD bias and clamp-to-border are rejected in GLES3/WebGL2.
`bindTextureView` binds a supported identity view of the parent
texture and an independent sampler object; legacy `setTexture`
explicitly unbinds any prior custom sampler for predictable
compatibility. Destroying the parent removes dependent identity views.

**Portable limitation (important):** OpenGL 3.3 / GLES3 / WebGL2
cannot provide a general subresource alias with the portable API.
`createTextureView` therefore **only** accepts an identity view
covering the full native mip range and all layers. Requests for
per-face or per-mip views fail, not silently collapse to the parent.
`supportsTextureViews()` reports false to communicate that full
subresource views are absent, although explicit identity view handles
are available for independent sampler binding. Full GL texture views
would require GL 4.3 / ARB_texture_view / platform-specific support
and separate resource allocation rules.

**Compatibility:** Existing 2D texture binding still uses
`setTexture`; its sampler reset prevents a previous explicit
`bindTextureView` from leaking state into legacy renderer calls.
`Texture2D` resources record actual requested mip count for
identity-view validation. No data format, save or ECS changes.

**Verification:** Source commit recorded. Windows GL loader,
Linux GLES, Android and WebGL2 compilation: **NOT RUN**.
Native GPU creation/bind/filter tests: **NOT RUN**.
Do not mark backend support production-complete without these tests.

**Next:** Implement native Vulkan `TextureView` + `SamplerHandle`
with descriptor-set binding overrides and GPU-safe deferred destruction,
and expand Vulkan tests to select a nonzero array slice/mip with a
custom sampler. Confirm GLES3 compile on Windows loader and device
lifetime semantics, especially sampler unbinding and anisotropic
feature query. Keep handoff and source as separate commits.

## Stage 3D — Vulkan native subresource views and independent samplers (2026-11-18)

**Code commit:** `0d32b65c464e1e4c768253a412fa3176f347227d`.
**Paths:** `engine/gfx/vulkan/VkDevice.h`,
`engine/gfx/vulkan/VkResources.cpp`,
`engine/gfx/vulkan/VkDraw.cpp`,
`engine/gfx/vulkan/GfxVulkan.cpp`.

**Implemented in source:** `createTextureView` selects a validated
mip/layer range with a real `VkImageView` matching 1D/2D/3D,
array and cubemap dimensions. `createSampler` exposes separate
`VkSampler` resources with filtering, address modes,
compare op, LOD range, bias and anisotropy (enabled only when
physical-device feature is present). `bindTextureView` overrides
the combined image/sampler descriptor for a selected texture slot
without affecting the legacy `setTexture` API. Parent texture
deletion destroys all child views, and explicit view/sampler deletion
invalidates active bindings. Native Vulkan destruction is deferred
through the existing frame-completion queue for GPU lifetime safety.
Device shutdown destroys extra views before parent images and
independent samplers before the Vulkan device.

**Compatibility:** Descriptor layouts unchanged (combined sampler).
`setTexture` clears any prior view/sampler override so existing
RTS/FPS render paths continue to use their default resources.
No save, ECS, serialization or project content changes.

**Limitations:** RHI permits view bindings for direct render passes,
not layered render-target attachment views. No MSAA/resolve, MRT,
storage image or async texture readback. Actual GPU feature support
still needs validation. The Vulkan backend uses combined
image-sampler descriptors, so separate samplers are selected by
rebinding a view, not by an independent sampler-only command.

**Verification:** GitHub code commit succeeded; native Vulkan
compiler, Vulkan validation layers and real GPU tests: **NOT RUN**.
The prior workflow executions were queued; do not mark green.
Check C++ declaration/definition linkage (especially new
`bindTextureView` override) in a native build.

**Next actionable verification:** Extend
`tests/render/vulkan_tests.cpp` to test nonzero array-layer,
explicit mip and separate sampler bindings, then build/execute
`aiverse_vulkan_tests` on Vulkan SDK+Lavapipe. Also test parent
destruction invalidation and sampler destruction during a frame.
After that, validate Metal and GL native tests, then tackle layered
render targets/MSAA and format/importer stages.

## Stage 3E — Vulkan explicit view/sampler integration regression test (2026-11-18)

**Code commit:** `867e4cf999521a1ff3bca683c616b210d9c8790c`.
**Path:** `tests/render/vulkan_tests.cpp`.

**Added:** Real GPU Vulkan fixture creates a three-layer
`Texture2DArray`, then views two different array layers as
`Texture2D`, binds the selected views with a separately created
nearest sampler to one GLSL `sampler2D` slot and asserts blue/green
screen output. Explicitly tests that a destroyed sampler cannot be
rebound and destroying the parent invalidates its child view handles.

**Validation:** New test code committed but **NOT RUN**: this
execution environment cannot clone GitHub, and the native Vulkan
compiler/driver are unavailable here. Existing GitHub CI workflows
were still queued on the branch at last check; no passing result is
claimed. Real colour results depend on driver execution.

**Next:** Run `aiverse_vulkan_tests` on a GPU/Lavapipe machine
with validation layers and fix any shader/compiler/descriptor
validation errors. Build `aiverse_texture_contract_test`;
compile all major client/editor targets with Metal and GLES3 to
check cross-platform RHI changes, then proceed to layered
RenderTexture/MRT/MSAA implementation in independently documented
stages.

## Stage 4A — explicit offscreen attachment-pass contract (2026-11-18)

**Code commit:** `7f05e9eccf0b01897f7cc61d7fa17efee34ba46e`.
**Paths:** `engine/gfx/Gfx.h`, `tests/render/texture_contract.cpp`.

**Source changes:** New `RenderTargetAttachment` (texture, mipLevel,
arrayLayer/face, clear, RGBA clear value) and `RenderTargetPassDesc`
(up to four color attachments, optional depth, depth clear), plus
`validateRenderTargetPassDesc`. The CPU-only helper rejects empty
passes, invalid handles and negative coordinates, duplicate writes
to the same subresource, the same resource as color and depth,
and invalid clear values. The public RHI has opt-in
`supportsRenderTargetPasses` and
`beginRenderTargets(RenderTargetPassDesc)` returning false by
default. A successful call opens a pass ended by existing
`endPass()`; errors must leave no active pass and must never fall
back to an unintended target. New structural contract tests added.

**Compatibility:** Existing `PassDesc` and `beginPass` are unchanged.
The new API is offscreen-only, never implicitly the swapchain.
No gameplay/ECS, save or external game format changes.

**NOT implemented yet:** Native Metal/GL/Vulkan implementation of
`beginRenderTargets`; MRT, mip/layer rendering, MSAA and resolves
are still unsupported by this initial contract.

**Validation:** Source commit confirmed through GitHub connector.
Native compile, CTest and GPU test: **NOT RUN**.
GitHub Actions for prior HEAD were in `queued` state; no CI pass
verified. Do not claim the contract is production ready.

**Next stage:** 4B — Metal native single color + optional depth,
selecting mip/layer using Metal render pass descriptors, validating
actual allocation usage/format/sizes, then add Metal multi-color
targets and capability-specific pipeline variants. Make a separate
code commit and immediately append a handoff commit.

## Stage 4B — Metal color/depth rendering into selected mips and layers (2026-11-18)

**Code commit:** `dfbc5421d4da59e533cf9a9cc8e07121914f8a46`.
**Changed:** `engine/gfx/metal/GfxMetal.mm`.

**Implementation in source:** Metal `createTexture` permits texture
2D arrays/cubes/cube arrays with `renderTarget=true`, preserving
Metal's explicit native `MTLTextureUsageRenderTarget` allocation.
`supportsRenderTargetPasses()` reports true.
`beginRenderTargets` validates the requested subresource against
the actual native texture's mip levels, layers/faces, dimensions,
render-target usage, pixel format, and samples; it requires matching
extent for color/depth. On success the Metal render-pass descriptor
uses the requested `level`/`slice` and per-attachment clear/load/store,
then opens an encoder ended via existing `endPass`.
Both color-only and depth-only are supported. The existing screen
and legacy 2D `beginPass` remain unchanged.

**Explicit limits:** Maximum **one** color attachment is supported
by this stage; multi-target passes return false with a clear error,
since current Metal pipeline descriptors only configure color slot 0.
3D slice and 1D render targets are rejected. Only color RGBA8,
RGBA8_sRGB and RGBA16F are allowed, matching supported pipeline
pixel formats; depth format uses existing `Depth`.
No MSAA or resolve operation. The high-level
`render::RenderTexture` remains a legacy RGBA8 canvas.

**Compatibility:** Adds opt-in offscreen rendering only.
No existing render path or save/network/ECS file is changed.

**Verification:** GitHub code update committed; native
macOS/iOS compilation and execution **NOT RUN**.
Render-to-nonzero-mip/layer GPU fidelity and layout/resource
lifetime validation are still required. No CI pass claimed.

**Next:** Stage 4C — add GL33/GLES3 selected mip/layer color/depth
FBO passes, with strict checking before changing active render
state. Then validate native API calls and attach-layer path
using offscreen GPU tests. Stage 4D adds multi-color pipeline
output formats/MRT, Stage 4E Vulkan and MSAA/resolve.
Commit source and handoff separately after each.

## Stage 4C — GL33/GLES3 single attachment mip/layer offscreen passes (2026-11-18)

**Code commit:** `962e4b7fdaa9f1be51d8b24be19cbaebaeeba71c`.
**Changed:** `engine/gfx/gl/GfxGL.cpp`, `engine/gfx/gl/GLApi.h`.

**Source implementation:** `GLDevice::beginRenderTargets` checks
actual texture usage, target format, mip/layer and matching
color/depth extents, rejects nested or multi-color passes, attaches
the chosen `Texture2D` mip, cubemap face, `Texture2DArray`
layer or `Texture3D` depth plane to a temporary framebuffer.
The Windows GL3.3 loader now exposes `glFramebufferTextureLayer`.
Only after framebuffer completeness is verified does it update
renderer pass state, viewport and color/depth clear state.
The framebuffer is deleted on `endPass`; invalid attachment
requests return false rather than rendering to an unintended
target. Existing `beginPass` continues to function.

`createDimensionalTexture` now permits color render-target
allocation, plus depth arrays/cubes (not depth 3D).
Legacy 2D allocation now allocates all explicitly requested
mip levels, rather than quietly ignoring `mipLevels > 1`,
with its own maximum mip and filter settings.

**Limits:** MRT (more than one color attachment) rejected;
GL multisample texture allocation/resolve and storage textures
still unsupported; cube array/1D not available on portable
GL33/GLES3. The new pass builds a temporary FBO per invocation
rather than caching it; this is correctness-first and may
need a performance pass. No texture readback API added.

**Compatibility:** Existing GL texture binding and legacy
2D passes remain available. The GL new-pass guard tracks
`inPass_`, so callers should always use `endPass`.
No gameplay/saves/network/ECS changes.

**Verification:** Source commit completed through GitHub connector;
Linux/Android/WebGL2/Windows GL compiler and actual framebuffer
completeness tests **NOT RUN**. CI previously queued, no
passing outcome asserted.

**Next:** Implement explicit `beginRenderTargets` for Vulkan
with view selection and renderpass/framebuffer lifetime,
then add GPU tests to verify nonzero mip/layer and depth.
MRT requires pipeline format/layout-aware variants in
Metal/Vulkan; MSAA/resolve requires sample-count and
multisample resource allocation. Implement in separate
code/handoff stages. Fix any native failures first.

## Stage 4D — Vulkan explicit mip/layer color/depth passes (2026-11-18)

**Code commit:** `bda67dfc5cd3b586aa03ed9590b3cd1458c0f7c6`.
**Paths:** `engine/gfx/vulkan/VkDevice.h`,
`engine/gfx/vulkan/VkResources.cpp`,
`engine/gfx/vulkan/VkDraw.cpp`.

**Implemented in source:** `beginRenderTargets` accepts one color
attachment plus optional depth, or depth-only. It checks physical
resource usage/format/subresource bounds, mip-sized color/depth extent
parity and attachment type before creating a render pass.
For `Texture2D`, `Texture2DArray`, `Cube`, or `CubeArray`,
a 2D `VkImageView` selects the requested mip and layer/face,
and a one-layer framebuffer is allocated and used for drawing.
The new pass reuses existing Vulkan single-color renderpass/pipeline
format compatibility and custom clear colors. Temporary framebuffer
and image views are deferred for destruction after
`vkCmdEndRenderPass` and GPU completion. Vulkan resource creation
now permits 2D array/cube/cube-array color and depth render-target
allocation. All legacy `beginPass` paths remain unchanged.

**Explicit limitations:** maximum one color attachment; no MRT
pipeline layout, MSAA or resolve yet. Vulkan does not support the new
pass for `Texture3D` depth planes or `Texture1D`, since appropriate
image-view compatibility flags are not yet configured. Storage images
and high-level RenderTexture changes are pending.

**Compatibility:** no changes to game assets, ECS authority,
save/network data, or existing `PassDesc`. This is an opt-in
offscreen API only.

**Verification:** Source commit succeeded. Vulkan SDK compile,
validation layers, lavapipe/GPU render pass tests **NOT RUN**;
CI has not reported a confirmed pass. A real Vulkan test should
render into array layer 1 / mip 1, then sample a dedicated view;
also verify depth-only array and mismatched attachment sizes.

**Next:** Add regression tests for new attachment pass and
failure invariants; then implement MRT color-output pipeline
format matching, MSAA/resolve and storage texture support in
separate documented stages. Check existing single-color
pipelines and frame transitions on Vulkan validation layers.

## Stage 4E — Vulkan mip/layer attachment GPU test added (2026-11-18)

**Code commit:** `06fe04ebb2560657cabc1a983c13748cc315b290`.
**Changed:** `tests/render/vulkan_tests.cpp`.

**Test source added:** `vulkan_backend` now contains an integration
case that allocates color and depth `Texture2DArray` resources
with three mips, starts an offscreen render-target pass targeting
mip 1 / array layer 1, clears color and depth, ends it, samples
the selected layer/mip through a 2D `TextureView` and custom
nearest sampler, then checks screen pixels. It also asserts that
an invalid mip is rejected without starting a new pass.

**Validation status:** GitHub commit succeeded. The test
**HAS NOT BEEN COMPILED OR RUN** (no native Vulkan environment
in this turn); do not infer Vulkan correctness from the test's
presence. CI workflows for earlier commits were queued/cancelled,
not confirmed passing. Check a real Vulkan+validation layer run
before marking stage production ready.

**Next:** Run the actual compiler/tests; debug Vulkan image view
creation, layout transitions and pipeline compatibility errors.
After GPU validation, implement MRT with color formats and multiple
pipeline attachment slots, then multisampled textures and resolve
operations. The current native APIs still support only one color
target and no MSAA resolve.

## Stage 4F — MSAA resource and resolve contract (2026-11-18)

**Code commit:** `ffe532d8f6deb2d8884b4c687fb867fbd478708f`.
**Paths:** `engine/gfx/Gfx.h`, `tests/render/texture_contract.cpp`.

**Implemented in source:** Extended `PipelineDesc` with an
appended `sampleCount=1` field (legacy-compatible), and
`RenderTargetAttachment` with optional resolve destination,
resolve mip and layer. CPU descriptor validation now permits only
sampleCount 1/2/4/8/16 and rejects multisample compressed formats,
depth-compare multisample textures, and invalid MSAA usage.
`validateRenderTargetPassDesc` rejects resolve to the same
source image, invalid resolve coordinates, and portable-unsupported
depth resolves. Contract test cases added.

**What is NOT implemented:** MSAA GPU allocations, resolve
load/store actions and sample-count-aware graphics pipelines
are not implemented by this contract alone. Pixel format,
extent, sample-count compatibility must be checked by backends.
Native Metal implementation follows, others will still reject.

**Compatibility:** `PipelineDesc` gains only a default-initialized
final field. Old `TextureDesc.sampleCount=1` and no-resolve passes
remain unchanged. No game content/save/network/ECS impact.

**Validation:** GitHub source commit recorded. C++ compile,
CTest and GPU validation **NOT RUN**. CI previous jobs queued
or cancelled, no passing status reported.

**Next:** Stage 4G — Metal 2D MSAA resources, pipeline raster
sample count and color resolve in `beginRenderTargets`.
Then add a device-driven MSAA rendering/resolve test, verify
samples 2/4/8 on supported GPU and document fallback.
Do not silently enable sample count on GL/Vulkan before their
pipeline/renderpass implementation is ready.

## Stage 4G — Metal multisampled resources and color resolves (2026-11-18)

**Code commit:** `b063ab889a91ccc99f1e35e6718f1800b8fc04ee`.
**Paths:** `engine/gfx/metal/GfxMetal.mm`,
`engine/gfx/gl/GfxGL.cpp`, `engine/gfx/vulkan/VkDraw.cpp`.

**Implemented in source:** Metal checks
`[device supportsTextureSampleCount:]` before allocation,
creates 2D multisample resources with `MTLTextureType2DMultisample`,
explicit `sampleCount`, private storage and render-target usage.
Metal pipeline descriptors set `rasterSampleCount` from the newly
appended `PipelineDesc.sampleCount`.
`beginRenderTargets` now checks matching color/depth MSAA sample
counts; with a valid single-sample destination matching format
and extent it writes a native `resolveTexture/resolveLevel/
resolveSlice` attachment and selects
`MTLStoreActionMultisampleResolve`.
The active pass enforces `PipelineDesc.sampleCount` when binding.
Metal `TextureView` explicitly rejects multisampled sources until
a compatible MS texture view contract exists.
GL and Vulkan `createPipeline` now explicitly reject
`sampleCount != 1` instead of silently using a 1x pipeline.

**Compatibility:** All legacy pipelines still default to sample
count 1, old 2D offscreen passes are unchanged. Only opt-in
MSAA render target and resolve paths use the new descriptors.
No save/network/ECS changes.

**Limits / risks:** No GL/Vulkan MSAA allocation or resolve yet.
Metal depth resolve is not portable and deliberately unsupported.
MRT is still limited to one color attachment.
The Metal sample-count pipeline and resolve path **MUST**
be tested on actual macOS/iOS devices with Metal API Validation;
no compile or GPU test was run in this session.
Verify SDK availability of `rasterSampleCount`,
`supportsTextureSampleCount:`, `MTLTextureType2DMultisample`,
and `resolveLevel/resolveSlice` across deployment targets.
Check command-buffer lifetime and exact clear/resolve semantics.

**Verification:** GitHub source commit complete.
Native compilation, Metal validation, 2x/4x/8x MSAA
render-to-resolve tests: **NOT RUN**. CI still not confirmed green.

**Next:** Add a device-level Metal test with 4x MSAA render target,
pipeline `sampleCount=4`, an RGBA8 single-sample resolve target,
and post-pass sampling. Then implement Vulkan MSAA images,
compatible renderpass/pipeline and explicit resolve attachments
as separate stages; GL GLES3/GL33 needs corresponding
multisample textures and resolve blits. Document each separately.

## Stage 4H — fail closed on unavailable GL/Vulkan resolves (2026-11-18)

**Code commit:** `43709245696c5371cc11e5fbc6aee5e0ad5f7153`.
**Paths:** `engine/gfx/gl/GfxGL.cpp`, `engine/gfx/vulkan/VkDraw.cpp`.

**Reason/implementation:** After adding a portable optional
`resolveTexture` field to the pass contract, the GL and
Vulkan new render-target pass methods would otherwise have
ignored a resolve request and returned success without resolving.
They now explicitly return false and set a backend error when
a resolve target is requested. MRT still returns false on
these backends. Metal has the separate native MSAA+resolve path.

**Compatibility:** Only erroneous/unimplemented new-API requests
change behavior (fail instead of silent no-op).
Legacy screen/2D passes and game formats are unchanged.

**Validation:** Source commit succeeded. Native builds, tests,
GPU validation **NOT RUN**; no CI pass claimed.

**Next:** Before marking feature complete, implement MRT and
GL/Vulkan MSAA+resolve with actual GPU tests. Verify the
Metal path and all earlier texture stages through compiler
and real-device tests first; do not stack untested features
on broken backends. Continue append-only handoff with separate
implementation and docs commits per stage.

## Stage 4I — refresh canonical texture RHI architecture docs (2026-11-18)

**Documentation commit:** `fc1886681cc2909e24f3c85a0a742bb5d5f51c4f`.
**Changed:** `docs/texture-rhi.md`.

**Work:** Replaced obsolete "Stage 1 only" document status
with precise staged source progress through 4H. Recorded
Metal/GL/Vulkan dimension differences, texture views/samplers,
one-color-pass limitation, Metal-only source MSAA/resolve,
unimplemented MRT/import/streaming, legacy API compatibility and
an example for rendering into a Texture2DArray mip/layer.
Every feature remains explicitly **UNVERIFIED** until native
compiler and GPU testing is observed.

**Validation:** Documentation update committed. No native code
changed by this stage; renderer builds and tests were **NOT RUN**.
No CI results claimed.

**Next:** Run native builds first (Metal/GL/Vulkan and
`texture_contract`/Vulkan tests) and fix any errors. Then
complete MRT with format-aware pipeline variants,
GL/Vulkan multisampling/resolve and high-level RenderTexture,
followed by extended formats, KTX2/DDS imports and streaming.
Always append a new handoff after each bounded source stage.

## Stage 4J — GL33/GLES3 2–4 color MRT passes (2026-11-18)

**Implementation commit:** `fbb4f3c11eaa45ada4c21dff904a3300ab8db163`.
**Paths:** `engine/gfx/gl/GfxGL.cpp`, `engine/gfx/gl/GLApi.h`.

**Source implementation:** GL `beginRenderTargets` now accepts up to
`kMaxRenderTargetColors=4` distinct color subresources, optionally with
one depth subresource. It checks `GL_MAX_COLOR_ATTACHMENTS` and
`GL_MAX_DRAW_BUFFERS`, validates all mip-level dimensions/array layers,
requires equal color attachment pixel formats (the existing HDR/linear
shader policy is shared) and equal extents for all active targets.
It attaches each color to a separate GL FBO slot, configures all
`glDrawBuffers`, and uses `glClearBufferfv(GL_COLOR, slot, rgba)`
so clear values do not overwrite other color attachments.
Windows GL33 loading adds `glClearBufferfv` and needed enums.
Explicit resolve targets still fail closed rather than silently
dropping their requested resolve. Legacy `beginPass` remains intact.

**Limits / risks:** GL shaders must explicitly declare fragment
outputs at locations 0..N-1 to write each target; the new RHI
does not generate or patch shader outputs. Mixed-format MRT, MSAA
and resolves are still unsupported. This is not a shared
`PipelineDesc` color-format system across Metal and Vulkan.
No FBO cache: a fresh framebuffer is validated/created per
explicit render-target pass and deleted at `endPass`.
Before native use, verify GL `glClearBufferfv` entry-point loading
and framebuffer completeness on Windows GL33, Android GLES3,
WebGL2 and Linux EGL. Test depth-only cases and nonzero mip/layer.

**Verification:** GitHub code commit and source update confirmed;
native C++ build, GPU execution and CI green result: **NOT RUN**.
At start of this stage, latest CI workflows on branch
`9f175e5...` were queued, previous runs had cancelled results.
Do not mark the feature production-ready.

**Next actionable stage:** Add GPU test with two different
fragment outputs (`layout(location=0)` and `layout(location=1)`),
different clear values, and readback of both attachments.
Then implement corresponding Metal MRT pipeline/attachments and
Vulkan MRT render-pass/pipeline variants in bounded stages.
Run builds before merging and continue appending exact SHA/tests.

## Stage 4K — Metal 2–4 color MRT pipeline/output and per-attachment resolve (2026-11-18)

**Implementation commit:** `da0cdbdfcc44ded5947d04d8a7e0f43a7e6e6b1d`.
**Paths:** `engine/gfx/Gfx.h`, `engine/gfx/metal/GfxMetal.mm`,
`tests/render/texture_contract.cpp`.

**Source implementation:** Append `PipelineDesc.colorAttachmentCount=1`
and `additionalColorFormats[3]` to the existing color-format and sample
count contract: slot zero remains `colorFormat`. Metal pipeline creation
now configures native color attachment pixel formats, write masks and
blend modes for each declared color output. The new explicit Metal
`beginRenderTargets` accepts up to four active color attachments,
validates all mip sizes, sample counts and pixel formats, supports
a depth target and separate optional resolve target per color slot,
and creates a single Metal encoder containing those attachments.
The current implementation intentionally requires all color
attachments to share the same format; heterogeneous MRT requires
a shader/HDR-policy integration stage. An active explicit pass
checks the pipeline's color output count, attachment formats and
MSAA sample count on `setPipeline` and rejects mismatches.
Legacy one-color/default pipeline behavior is preserved by defaults.

**Requirements:** Shader authors must supply `fs_main` outputs at
Metal `[[color(0)]]`..`[[color(N-1)]]` to write every attachment.
The generic RHI does not rewrite shader entry points. Existing
`beginPass` remains the legacy path and is not subject to explicit
MRT validation.

**Limits and risks:** No native compilation or GPU validation has
run. Verify the Objective-C++ pipeline descriptor loop, Metal API
availability and correct MSL multiple-output pipeline compilation.
The ICB/PBR optimized path must remain disabled for MRT until
explicitly validated. HDR/mixed formats are not fully implemented.
No GL/Vulkan multisample implementation, no unified MRT pipeline
metadata validation on those backends.

**Verification:** GitHub source commit confirmed. `cmake` build,
`ctest`, Metal capture, runtime pipeline tests: **NOT RUN**.
No CI passing status observed.

**Next:** Add a Metal MRT integration test with two outputs and
distinct per-target clears/readback, including MSAA resolve. Run
native macOS build and validation. Complete Vulkan MRT renderpass
and pipeline layouts before claiming cross-backend support.
Commit code and handoff independently per stage.

## Stage 4L — GL explicit MRT pipeline count/format validation (2026-11-18)

**Implementation commit:** `58d1401ad5c5581e2974691c85f2d858b73b223c`.
**Path:** `engine/gfx/gl/GfxGL.cpp`.

**Source implementation:** Validate `PipelineDesc.colorAttachmentCount`
at GL pipeline creation (1..4). Track the active explicit MRT
pass's color count and common pixel format, and on `setPipeline`
reject mismatched depth-only/color-count/color-formats instead
of silently using the wrong output layout. The legacy
`beginPass` clears this MRT-only validation state and stays
compatible with ordinary single-color scenes. HDR variants
automatically propagate `RGBA16F` to additional color slots,
maintaining parity with the new Metal MRT pipeline policy.
`endPass` releases MRT-specific pass validation state.

**Compatibility:** This affects only explicit `beginRenderTargets`
and the new `PipelineDesc.colorAttachmentCount>1` use path.
The original legacy one-color paths remain available.

**Risks / limits:** GL does not reflect fragment shader output
locations against `PipelineDesc`; fragment outputs must be
authored at locations 0..N-1. Mixed attachment formats remain
unsupported by the common HDR policy. GL separate target
blending is not exposed by this implementation.

**Verification:** GitHub code commit confirmed; no native compiler,
GL context, CTest or successful CI result available. **NOT RUN**.
Do not claim production-ready MRT.

**Next:** Add a headless GL MRT GPU fixture with two GLSL output
locations and per-color readback (including clearing only one
color). Then implement Vulkan MRT with render pass + pipeline
compatibility and source test. Follow the mandatory separate
code and handoff commit protocol.

## Stage 4M — Vulkan 2–4 color MRT pipeline/renderpass source (2026-11-18)

**Implementation commit:** `4ceab89b0742143418bf84f8e15613638d66dd9c`.
**Paths:** `engine/gfx/vulkan/VkDevice.h`,
`engine/gfx/vulkan/VkDraw.cpp`, `engine/gfx/vulkan/GfxVulkan.cpp`.

**Implemented in source:** Add a dedicated cached Vulkan
`renderPassForMrt` with 2–4 color attachments sharing a VkFormat,
per-attachment `clearMask` and optional depth, without changing
the existing single-color renderpass cache. Rewrite the explicit
`beginRenderTargets` path to validate all array/mip attachment
extents, color formats, and depth, allocate 2D VkImageViews
for the selected subresources, and build an MRT VkFramebuffer.
Multiple per-slot clear values flow into VkRenderPassBeginInfo.
New `VPipeline::Variant.colorCount` differentiates pipeline
variants by number of render-target attachments.
`pipelineFor` uses a Vulkan blend-state array and compatible
MRT renderpass when count > 1. Active pass count is tracked
and checked on `setPipeline`. Scoped framebuffer/views
are destroyed after the GPU completion fence. Shutdown also
destroys the dedicated MRT renderpass cache.

**Constraints:** Same color format across all active MRT slots,
matching extents, sample count 1, at most four color targets.
GLSL fragment shaders must declare outputs at locations 0..N-1.
GL/Vulkan MSAA resolve and mixed-color formats are still
unimplemented. No new ECS/save/network data.

**Validation:** Source commit confirmed through GitHub connector.
Native Vulkan compiler, Lavapipe, GPU validation layer and
vulkan_backend CTest **NOT RUN**; no green CI can be asserted.
Pay particular attention to Vulkan pipeline renderpass
compatibility, the newly added variant cache key and color-only
versus depth-only passes.

**Known possible regression to fix next:** The new
`setPipeline` color-count/format validation currently runs
for both explicit MRT and **legacy** single-target passes.
Legacy depth-only, sRGB or HDR-compatible calls may previously
have used a pipeline with a different colorFormat/count.
Scope these strict checks to explicit MRT only; the legacy
`beginPass` semantics must remain unchanged. This is the
immediate next code change before claiming backward compatibility.

**Next:** Implement the legacy/explicit policy separation,
add a Vulkan test with two fragment outputs and distinct
color clears, then compile and execute on a validation-layer
GPU. Keep code and handoff as separate commits.

## Stage 4N — restore permissive legacy Vulkan pipeline binding (2026-11-18)

**Implementation commit:** `a207beab197b5d2c2556d94072b9f653d3bac8df`.
**Paths:** `engine/gfx/vulkan/VkDevice.h`, `engine/gfx/vulkan/VkDraw.cpp`.

**Fix:** Strict color output count/format validation introduced for
new MRT passes in Stage 4M was also running on legacy
`beginPass(PassDesc)`. That could reject existing depth-only
rendering, sRGB render targets or HDR shader variants.
Track a separate `explicitMrtPass_` flag, set only by
successful `beginRenderTargets`, reset by legacy
`beginPass` and `endPass`. Enforce exact count/format
checks only while that flag is set. Legacy pipelines still
select their variants using the actual number of color
attachments in the legacy pass, preserving old permissive
shader/pipeline policy.

**Compatibility:** No data format, saves or gameplay changes.
Existing screen/legacy shadow/depth passes retain their
previous source semantics while new MRT correctly enforces
explicit format/count contracts.

**Verification:** GitHub code commit confirmed; native Vulkan
build, lavapipe, integration GPU tests and CI: **NOT RUN**.
This is a source-level regression fix pending a real compiler.

**Next:** Add MRT test that creates two RGBA8 color textures,
renders two distinct fragment outputs, and samples both
textures afterward. Check Vulkan validation layers for
VkRenderPass/graphics pipeline compatibility. Then address
MSAA/resolve for GL and Vulkan and update handoff separately.

## Stage 4O — Vulkan MRT multi-output renderpass GPU test source (2026-11-18)

**Implementation commit:** `025ede7251cf2230462345ebcdca3f30ca6356f1`.
**Path:** `tests/render/vulkan_tests.cpp`.

**Source additions:** GPU fixture creates two RGBA8 color targets
and an explicit two-color `RenderTargetPassDesc`. First it clears
target0 red and target1 green, samples each into a different
screen half with a texture shader, and checks pixel values.
Then it draws a GLSL fragment shader with two independently
declared outputs (`layout(location=0)`, `layout(location=1)`)
through a `PipelineDesc.colorAttachmentCount=2`, with
distinct blue/yellow results. It verifies each target via
another rendered screen capture. This test catches MRT
renderpass clear policy, pipeline attachment count and
fragment output location binding.

**Verification:** GitHub test source commit confirmed. Native
Vulkan compilation, Lavapipe execution, validation layers and
image comparisons: **NOT RUN** in this environment (GitHub
repository cannot be cloned by the local execution container).
The fixture does not constitute a pass until it runs on a GPU.

**Next:** Compile `aiverse_vulkan_tests` and execute
`ctest --test-dir build/texture-rhi --output-on-failure -R
'^vulkan_backend$'` on an actual Vulkan device / Lavapipe.
Investigate shader output mapping, pipeline renderpass variant
cache and attachment load/store synchronization. Add matching
Metal/GL MRT fixtures; then continue MSAA/resolve.

## Stage 4P — native offscreen RenderTargetSet owner and CPU tests (2026-11-18)

**Implementation commit:** `83d96d5b93e95cb6deab9fd4eab6769b2c3b2116`.
**Paths:** `engine/gfx/RenderTargetSet.h`,
`tests/render/render_target_set.cpp`, `CMakeLists.txt`.

**Source implementation:** New header-only, genre-agnostic
`gfx::RenderTargetSet` wraps creation/ownership of up to four
same-format color `TextureHandle` objects plus an optional
depth attachment. Configurable 2D/2DArray/Cube/CubeArray,
mips, layers, formats and sample count. It owns an extra
single-sample resolve target per color when MSAA is requested.
`begin(mip,layer,...)` populates `RenderTargetPassDesc` and
only becomes active if `Device::beginRenderTargets` succeeds.
`end` closes the encoder; `shutdown` releases every GPU
resource, including cleanup after a partially created set.
`sampledColor(i)` returns the resolve target for MSAA or
the original texture for 1x. This utility does not replace
the existing `render::RenderTexture` paint canvas.

**Tests in source:** `aiverse_render_target_set_test`
is a GPU-independent mock Device CTest target verifying
multi-color/depth allocation, selected mip/layer submission,
nested-begin rejection, invalid index rejection, MSAA
resolve ownership, active pass shutdown and transactional
rollback on failed allocation. Tests consume only public
`gfx::Device` headers and the new utility header; no
renderer/gameplay dependencies.

**Compatibility:** Independent optional wrapper; no changes to
old `RenderTexture`, `PassDesc`, save files or ECS.
Native projects may include `engine/gfx/RenderTargetSet.h`.
The caller must destroy the set before the gfx::Device
lifetime ends; object destruction invokes `shutdown`.

**Unimplemented:** GL/Vulkan MSAA resource/resolve support;
mixed-color-format MRT; async readback, texture streaming.
Device GPU support is always queried via actual allocation
and beginRenderTargets status; no silent downgrade.

**Verification:** GitHub source commit succeeded.
Local compiler/CTest, native GPU tests, CI green outcome:
**NOT RUN** (container cannot resolve github.com for a clone).
Do not describe mock test as passed until actually executed.

**Next:** Build and run `aiverse_render_target_set_test` on
an actual clone, check native C++20 syntax and existing
editor/client consumers, then add metal/GL/Vulkan MRT
render/resolve device tests. After that continue GL/Vulkan
MSAA and the texture format/importer waves in separate
code and handoff commits.

## Stage 4Q — update canonical MRT + RenderTargetSet documentation (2026-11-18)

**Documentation commit:** `8c366e0a22b57636de26e1bae6285324a309f38e`.
**Changed:** `docs/texture-rhi.md`.

**Documentation:** Replace obsolete "only one color attachment" status
with the actual source-only MRT implementation in GL/GLES3, Metal
and Vulkan, including same-format-only and shader-output-location
requirements. Document Metal-only per-color MSAA resolves,
source-only Vulkan GPU regression fixtures, and the new
genre-independent `gfx::RenderTargetSet` with example usage
and explicit resource lifecycle. List remaining missing mixed
formats, GL/Vulkan MSAA, native GPU execution, importers and
streaming. Keep the unrelated Lua brush `RenderTexture`
separate.

**Validation:** Doc source commit confirmed. No C++ files
changed during this stage; native builds/tests **NOT RUN**.
No GitHub Actions green outcome can be asserted without
a completed successful job.

**Next:** First build the current branch and run
`texture_contract`, `render_target_set`,
`vulkan_backend` on a proper Vulkan/Metal/GL toolchain;
fix compilation and GPU validation errors. Only then finish
GL/Vulkan MSAA/resolve, mixed formats, DDS/KTX2 and
async/streaming improvements. Append precise results and
new stage handoffs.

## Stage 6A — full KTX2 array, cubemap and volume payload preservation (2026-11-18)

**Code commit:** `dbb6dfd1f32b40d4b002fd184c5db11be28d13f1`.
**Files:** `engine/render/TextureCodec.h`,
`engine/render/TextureCodec.cpp`, `engine/render/Model.cpp`,
`tests/render/ktx2_layers.cpp`, `CMakeLists.txt`.

**Source implementation:** `CompressedImage` now retains
`depth`, `arrayLayers`, `faces` and `oneDimensional`
metadata; the KTX2 reader preserves all layer/face/3D
z-slices within every mip, in order, for both raw and
Zstandard-supercompressed payloads. It checks dimensional
combinations, mip count, file bounds, expected lengths and
a 512 MiB per-level allocation ceiling. CPU API adds
`levelDepth`, `levelImageCount` and `decodeBlocksSlice`;
the existing `decodeBlocks` continues to decode slice 0.
Model texture upload stays **2D** for shader compatibility:
when a model references layered/cube KTX2 data it submits
only the first 2D slice of each compressed mip. Other
layers remain available in CPU memory to future generic
texture asset importers.

**Tests:** New GPU-independent `ktx2_layers` CTest,
linked against existing models + zstd decoder,
synthesizes KTX2 cube array and 3D RGBA8 payloads and
checks metadata, slice decoding, legacy first-slice
fallback, 1D metadata and corrupted/truncated inputs.

**NOT done:** Generic runtime KTX2 array/cubemap/volume
asset creation and binding to matching shaders; DDS
multi-layer parity; BasisLZ/UASTC transcode; HDR/EXR.
Parser does not automatically reinterpret glTF material
texture2D references as cubemaps. No changes to ECS/saves.

**Validation:** GitHub code commit successful. Native
C++ build and `ktx2_layers` CTest: **NOT RUN**.
GitHub Actions success not confirmed. Before compiling,
check that the new test source explicitly includes
`<algorithm>` for `std::max`; the current fixture
may need that include (identified during static review).
Do not claim test success.

**Next:** Fix the include, build/test KTX2 parser on
a proper clone, then implement generic dimension-aware
TextureAsset resource upload for KTX2 layered payloads
independently of the 2D-only model material path.
Continue with DDS cubemap/array decoding and BasisU
transcoding in separately documented stages.

## Stage 6B — KTX2 test standalone include correction (2026-11-18)

**Code commit:** `7a70493331e54a974d4125aa3ff9159c3a141fba`.
**Path:** `tests/render/ktx2_layers.cpp`.

**Fix:** Add `#include <algorithm>` explicitly; the test uses
`std::max` when calculating synthetic KTX2 slice offsets.
This avoids relying on transitive standard-library includes.

**Verification:** GitHub source commit confirmed. C++ compiler
and the `ktx2_layers` CTest **NOT RUN**. Still requires a
normal clone/native build; no passing result asserted.

**Next:** Add a dimension-aware KTX2 GPU importer that can
create actual texture array/cube/3D resources using the full
retained payload. Keep model-material sampler2D compatibility
separate. Validate import on all actual backends before
describing it as production support.

## Stage 6C — dimension-aware KTX2 GPU resource uploader (2026-11-18)

**Code commit:** `a300050dfb4eab58ce3d8c384e909f42154b7b29`.
**Paths:** `engine/render/TextureAsset.h`,
`tests/render/texture_asset.cpp`, `CMakeLists.txt`.

**Source implementation:** Header-only `uploadKtx2Image`
maps the extended CPU `CompressedImage` metadata to real RHI
`TextureDimension` values (1D/2D/3D/2DArray/Cube/CubeArray),
including all array layers/faces/volume slices. For supported
block-compressed images it hands the entire authored mip chain
to `gfx::Device::createTexture`. Unsupported BC4/EAC_R11/
BC1_RGB and other formats fall back through
`decodeBlocksSlice` to RGBA8 (or RGBA8_sRGB), allocate
the image and upload all nonzero authored mip slices with
`updateTextureRegion`. A failure destroys the partially
created texture and provides a readable error. Native
compressed sRGB sampling needs an explicit opt-in acknowledging
that the caller's shader performs sRGB decoding; by default
the loader chooses a CPU RGBA8_sRGB fallback where possible.
ASTC has no CPU decoder, so native ASTC support is required.
Caller retains ownership of the returned `TextureHandle`.

**Compatibility:** Existing model sampler2D materials still
use the old first-slice upload path. The new function is
opt-in and does not change assets, saves, ECS or legacy
material shader assumptions.

**Tests:** New CPU mock-device CTest `texture_asset`
checks CubeArray dimension/layer count, uploading twelve
separate mip-1 faces, 3D allocation, failed mip upload
cleanup, invalid input rejection. This test is registered
under `AIVERSE_BUILD_TESTS` alongside `ktx2_layers`.

**Validation:** Source commit successful. Actual C++ build,
unit test execution and GPU validation: **NOT RUN**.
Static review notes `TextureAsset.h` should explicitly
include `<algorithm>` for `std::copy`; next stage
should add this before compilation. No CI pass claimed.

**Next:** Fix missing standalone header include; build
`aiverse_texture_asset_test`/`aiverse_ktx2_layers_test`,
and run Vulkan/Metal/GL native sampling of cube and array
assets. Add DDS multi-face import parity, BasisU transcode
and compressed sRGB variants in separate documented stages.

## Stage 6D — standalone TextureAsset include fix (2026-11-18)

**Code commit:** `4893edd6a155f8e60e866cc79f730d67b7684bee`.
**Path:** `engine/render/TextureAsset.h`.

**Change:** Add direct `#include <algorithm>` for
`std::copy`, rather than relying on transitive includes
from RHI or standard containers.

**Verification:** GitHub source commit confirmed.
Standalone header/native target compilation and tests
**NOT RUN**. No completed successful CI asserted.

**Next:** Run the parser and upload CPU tests and
cross-platform GPU tests, then continue DDS 3D/array
support and remaining format/back-end completeness.

## Stage 6E — document layered KTX2 import and native upload API (2026-11-18)

**Documentation commit:** `6c4e1de4e032aa247af1766bb0dd38552a0c9967`.
**Path:** `docs/texture-rhi.md`.

**Updated architecture:** Replaced obsolete "KTX2 drops array
layers and cube faces" description with actual parser and
generic GPU uploader source status, including authored mip
preservation, first-slice legacy material compatibility,
`decodeBlocksSlice`, compressed sRGB caveats and a native
usage example. DDS still drops layers/faces; BasisLZ/UASTC
transcoding, HDR/EXR and full color-management remain
pending. Distinguishes source implementation from
platform-tested support.

**Validation:** Documentation commit completed;
native builds, CTest, GPU validation **NOT RUN**.
No CI pass asserted.

**Next:** Verify the new `ktx2_layers` and
`texture_asset` tests on a native clone, debug any
C++ compiler issues and test actual GPU sampling.
Then migrate DDS with multi-face/array metadata and
complete the missing render-target and format features.

## Stage 6F — DDS arrays/cubemaps/volumes preserved by a new container reader (2026-11-18)

**Code commit:** `c5faf209d8f7c82e9a3a276337de5219078fcb30`.
**Working branch:** `feature/texture-rhi-unification` (draft PR #21).
**Changed paths:** `engine/render/ImageDds.cpp`, new
`engine/render/DdsTexture.h`, new `tests/render/dds_layers.cpp`,
`CMakeLists.txt`.

**Implementation:** New public `parseDdsTexture(bytes, CompressedImage&, err)`
supports DX10 DDS 2D arrays, cubemaps/cubemap arrays and volume textures,
plus complete legacy cubemaps and 2D cases. It retains all faces/layers/z
slices/mips in `CompressedImage` (mip-major, slice-major within each mip),
transposing native DDS **surface-major mip order** to the shared KTX2
representation. BC1/BC3/BC4/BC7 remain compressed; BC2/BC5 are decoded
per slice into RGBA8, as are common legacy and DX10 8/16/24/32-bit masked
RGBA/BGRA formats. Handles sRGB DX10 variants and BC1 RGB-vs-RGBA
sampling semantics. Enforces dimension/cube-face consistency, mip count,
byte bounds and 512 MiB max decoded allocation per mip. No file format
or save/network/ECS changes; `decodeDds(Model::Image&)` remains the
old first-face 2D material pathway.

**Test source:** Registered `dds_layers` CTest with synthetic DX10 BC1
two-cube array (checks 12 faces and per-mip surface-to-mip reordering),
three-layer RGBA8 array, 3D RGBA8 volume with shrinking depth,
and malformed/partial cubemap/truncated payload rejection.

**Compatibility:** New opt-in container API; existing model importer and
sampler2D textures remain unchanged. Caller may pass the resulting
`CompressedImage` to `uploadKtx2Image` (format-agnostic uploader) for
native dimensional GPU resources, subject to backend capabilities.

**Unsupported:** DDS signed/float/packed DXGI formats not already decoded
in this subset; non-tightly-packed DDS pitch variants; BasisU transcoding;
HDR/EXR; GL/Vulkan MSAA resolves. The function does **not** make all DDS
formats supported, and does not auto-rewrite 2D model shader bindings.

**Verification:** GitHub file/tree/commit/ref update succeeded.
CMake/C++ build: **NOT RUN** (full repository checkout unavailable in
this environment). `dds_layers`, other CTests and GPU validation:
**NOT RUN**. CI last checked: queued (not passing); recheck current SHA.
No build or performance claim.

**Next:** Compile `aiverse_dds_layers_test` and run
`ctest --test-dir build/texture-rhi --output-on-failure -R
'^(dds_layers|ktx2_layers|texture_asset|texture_contract|render_target_set)$'`.
Fix any static/API errors. Add direct `uploadDdsImage` name/format
documentation or alias in the shared `TextureAsset.h`, then test DDS
cubemap and 3D resource creation on Metal/Vulkan/GL. Continue the
separate code-commit + handoff-commit protocol after every stage.

## Stage 6G — shared DDS/KTX2 resource uploader API (2026-11-18)

**Code commit:** `9adeddd31691f98d950ab2f60d1cf155bfed8fd5`.
**Branch:** `feature/texture-rhi-unification` (draft PR #21).
**Changed:** `engine/render/TextureAsset.h`.

**Implementation:** Rename the dimension-aware implementation function
to `uploadContainerTexture(gfx::Device&, CompressedImage&, options, error)`
(the actual signature accepts a const CompressedImage reference), since
both the new `parseDdsTexture` and `parseKtx2` emit the same mip-major
`CompressedImage` representation. Retain an inline
`uploadKtx2Image` wrapper for existing callers and introduce
`uploadDdsImage` for native project consumption, both delegating to
the same implementation. Diagnostic strings now refer to container
data rather than KTX2 exclusively. Memory ownership, CPU fallback,
full mip/layer/face uploads, sRGB options and GPU cleanup semantics
are unchanged. Legacy model `sampler2D` import path is unchanged.

**API/save compatibility:** Old public `uploadKtx2Image` name remains;
new opt-in DDS and generic aliases added. No save/network/ECS changes
or on-disk format changes. No new dependency added.

**Verification:** GitHub source update/commit confirmed. C++ compile,
DDS/KTX2 CTests, GPU sampling and CI completion: **NOT RUN**.
No completed successful GitHub Actions outcome observed.

**Next:** Add a CPU mock-device test exercising
`parseDdsTexture` -> `uploadDdsImage` with a genuine multi-layer
DDS fixture, then run the texture-related CTests on a real native
clone and fix failures before extending backends. Continue separate
source and handoff commits after every bounded stage.

## Stage 6H — DDS DX10 array parser-to-RHI upload integration fixture (2026-11-18)

**Code commit:** `8994da135bd8fcbd91e5fbbcb9de24048c4864ee`.
**Branch:** `feature/texture-rhi-unification` (draft PR #21).
**Changed:** `tests/render/texture_asset.cpp`.

**Test source:** Extend existing mock-Device `texture_asset` CTest
with a synthetic DDS DX10 RGBA8 2D-array fixture (two layers,
two mips stored in DDS surface-major ordering). The test invokes
`parseDdsTexture`, verifies it reorganizes mips into the
shared mip-major `CompressedImage`, calls the new
`uploadDdsImage` public alias, checks the requested
`Texture2DArray` descriptor, layer count, explicit mip count,
and per-layer second-mip subresource uploads. It verifies
the returned GPU handle and cleanup through the mock device.

**Compatibility:** No runtime behavior or native public API
changes in this test-only stage. Model sampler2D path remains
unchanged; the generic uploader stays opt-in.

**Verification:** GitHub source commit confirmed.
Build, CTest `texture_asset`, Vulkan/Metal/GLES GPU test:
**NOT RUN**; no CI passing result claimed.

**Next:** Actually compile and run `texture_asset`,
`dds_layers`, `ktx2_layers`, `texture_contract`,
`render_target_set` on a native clone; fix all failures.
Then validate DDS cube array / 3D on real backends,
complete GL/Vulkan MSAA/resolve, and extend remaining
compressed and HDR pixel formats. After each bounded stage
commit handoff separately with exact SHA and test status.

## Stage 5A — native BC4/BC5/EAC_R11 and BC5 CPU normal reconstruction (2026-11-18)

**Code commit:** `b47def0ba8c23dc284907ce44bc25e74f4d9b2c9`.
**Working branch:** `feature/texture-rhi-unification` (draft PR #21).
**Changed:** `engine/gfx/Gfx.h`, Metal, GL and Vulkan RHI
texture implementations, `engine/render/TextureCodec.h/.cpp`,
`engine/render/ImageDds.cpp`, `engine/render/TextureAsset.h`,
`engine/render/Model.cpp` (nine files).

**Source work:** Append `PixelFormat::BC4`, `PixelFormat::BC5`,
`PixelFormat::EAC_R11` without renumbering existing public enum
entries; add compressed-format validation, native Metal pixel formats
(BC4/BC5 on BC-capable hardware, EAC_R11 on Apple GPUs), GL RGTC1/2
and EAC_R11 compressed enums with capability checks, and Vulkan
BC4/BC5/EAC_R11 `VkFormat` mappings, compressed-image flags and
correct 8/16-byte block validation. Extend `BlockFormat` by appending
BC5 and KTX2 vkFormat 141 mapping; new CPU BC5 block decoder
reconstructs tangent-space positive normal Z to match existing DDS
BC5 CPU fallback. DDS container parser now preserves BC5 blocks
instead of eagerly converting all data to RGBA8. The generic asset
GPU upload helper offers native BC4/BC5/EAC_R11 where supported.

**Important BC5 parity guard:** Native BC5 GPU sampling exposes RG
only; it does not reconstruct positive normal Z. The new
`TextureUploadOptions.shaderReconstructsBc5NormalZ` defaults to
**false**, preserving the existing CPU-decoded RGBA normal-map
behavior; enable it only in consumers whose shaders correctly
reconstruct Z. Existing model GPU compressed path adds BC4/EAC_R11
support but keeps BC5 on the decoded fallback.

**Compatibility:** All existing `PixelFormat` and `BlockFormat`
numeric values are preserved by appending entries. Existing 2D
materials, saves, network, ECS authority and asset schemas unchanged.
No new file dependencies.

**NOT implemented:** sRGB compressed BC native format variants
(other than existing manual shader decode), BC5 signed SNORM,
BC6H, RGBA integer/float variants, HDR/EXR, BasisLZ/UASTC
transcoding or GL/Vulkan MSAA resolves.

**Verification:** GitHub source commit/ref update confirmed.
Native Metal/GL/Vulkan compile, CTest and GPU validation:
**NOT RUN** (repository checkout unavailable through this runtime).
No passing CI result asserted.

**Next:** Add unit tests for BC5 CPU decoded RGBA normal,
KTX2 VK_FORMAT_BC5_UNORM and DDS DX10 BC5 preserved blocks.
Use mock device to prove native BC5 GPU upload is gated by
`shaderReconstructsBc5NormalZ` and that the default path
still reconstructs the Z component. Validate native compiler and
GPU format support. Commit test stage and separate handoff.

## Stage 5B — BC5 CPU/native parity regression fixtures (2026-11-18)

**Code commit:** `b0a1ce1225e1a716d33efb76a02ba395e4544514`.
**Branch:** `feature/texture-rhi-unification`, draft PR #21.
**Changed:** `tests/render/texture_asset.cpp`,
`tests/render/dds_layers.cpp`, `tests/render/ktx2_layers.cpp`.

**Test source added:** Synthetic BC5 RGTC2 blocks with constant
R=255/G=128 are checked through `decodeBlocksSlice` with the
expected reconstructed tangent-space Z≈128. The mock GPU
asset test verifies that default BC5 uploads receive RGBA8
CPU-decoded normals even if the GPU advertises native BC5,
while `shaderReconstructsBc5NormalZ=true` explicitly allows
native `PixelFormat::BC5` compressed uploads. DDS DX10 format
83 blocks remain stored as `BlockFormat::BC5` and decode
to the same channels. KTX2 vkFormat 141 is mapped to BC5
and decoded in CPU fallback. The tests reuse existing
`texture_asset`, `dds_layers` and `ktx2_layers` targets.

**Compatibility:** Test-only stage, no gameplay, native API,
save/network or serialized asset changes. Confirms intended
default BC5 normal-map semantics.

**Verification:** GitHub code commit/ref update succeeded.
C++ compile, CTests, Metal/GL/Vulkan native GPU validation:
**NOT RUN** (full repository clone not available here).
No green CI outcome asserted.

**Next:** Compile and execute all three CPU tests, then run
actual BC4/BC5/EAC R11 sampling on each backend with GPU
capability checks. Audit DDS/KTX2 sRGB handling and BC1_RGB
opaque semantics. Continue per-stage handoff after fixes.

## Stage 6I — update DDS and block-compressed format reference (2026-11-18)

**Documentation commit:** `2e612ecf43acc24dee89e2576651729f1f1391ac`.
**Branch:** `feature/texture-rhi-unification` (PR #21 Draft).
**Changed:** `docs/texture-rhi.md`.

**Documentation:** Explain the now-shared layered KTX2/DDS
`CompressedImage` representation, DDS mip/face ordering
conversion, `parseDdsTexture` and `uploadDdsImage` native
usage with `TextureUploadOptions`. Record BC4/BC5/EAC_R11
source-level capability mapping on Metal/GL/Vulkan, explicit
BC5 normal-Z reconstruction requirement, all remaining
native/format/transcoding limitations, and unrun GPU tests.
The older `decodeDds` 2D model path remains separate.

**Verification:** GitHub documentation commit completed;
C++ build, compiler tests, and native GPU validation:
**NOT RUN**. No green CI outcome asserted.

**Next:** Run new and existing texture-related native CTests
on the current branch and fix errors, then continue GL/Vulkan
MSAA, remaining typed formats and asset pipeline integration.
Update this handoff after every code/documentation stage.

## Stage 5C — shared compressed format metadata and fail-closed GPU allocations (2026-11-18)

**Code commit:** `944b08351b67768aa939bda1c7b9089d6345bd3f`.
**Branch:** `feature/texture-rhi-unification` (draft PR #21).
**Paths:** `engine/gfx/Gfx.h`, `engine/gfx/metal/GfxMetal.mm`,
`engine/gfx/gl/GfxGL.cpp`, `engine/gfx/vulkan/VkResources.cpp`,
`tests/render/texture_contract.cpp`.

**Reason:** A platform backend must never reinterpret compressed
BC4/BC5/BC7/EAC data as an RGBA8 upload when a native enum or GPU
format is absent. Previously Metal could report BC compression on
some Apple devices but its iOS compile-time pixel format mapping
did not expose the BC enum, causing allocation to follow the
uncompressed RGBA8 branch.

**Implementation:** Central `isCompressedTextureFormat`,
`compressedTextureBlockBytes`, `compressedTextureBlockSize`
constexpr helpers define compressed capabilities and 8/16-byte
4x4/6x6 storage requirements. Existing compressed format enum
values remain unchanged. Metal `supports(BC*)` now requires an
actual mapped Metal pixel format, and `createTexture` rejects
unmappable compressed requests before any RGBA8 fallback.
GL 2D and dimensional allocations validate format capability
before issuing compressed GPU calls. Vulkan and Metal reuse
the shared block-size helpers for upload validation.
CPU `texture_contract` test now asserts the new BC4/BC5/EAC
classification and block byte counts.

**Compatibility:** Any previously silently accepted unsupported
compressed format is now rejected explicitly. Existing supported
compressed/uncompressed formats preserve their path. No
save/network/ECS changes or new on-disk formats.

**Verification:** Source committed and branch moved through GitHub.
Native compiler, CTest and GPU checks: **NOT RUN**.
CI last observed queued/cancelled, not green.

**Next:** Test on iOS (particularly GPUs that claim BC support),
macOS, Android GLES3, Windows GL33 and Vulkan with actual
device format queries. Run CTest `texture_contract` and
BC5 DDS/KTX2 tests; fix any compiler/SDK enum errors before
expanding typed pixel formats. Continue separate implementation
and handoff commits after every stage.

## Stage 5D — compressed mip/face payload validation before GPU calls (2026-11-18)

**Code commit:** `d94b2b416763903697fc4f45f0c7314051abe900`.
**Branch:** `feature/texture-rhi-unification` (draft PR #21).
**Paths:** `engine/gfx/Gfx.h`, `tests/render/texture_contract.cpp`.

**Source implementation:** `validateTextureDesc` now rejects a
block-compressed texture unless `levelCount >= 1`, `levelData` and
`levelSize` are present, and every mip has a non-null payload
with enough bytes for all blocks, array layers/cube faces and volume
slices. Arithmetic uses overflow-checked products before comparing
byte sizes. Compressed initial data cannot be mixed with ordinary
`TextureDesc.data`, auto-generated mips, storage images, render targets
or multisample attachments. This common contract runs before GPU
allocation and prevents legacy GL/Metal compressed branches from
reading missing level arrays or uploading truncated face payloads.

**CPU test source:** Extended `texture_contract` with BC5
missing/truncated/valid block cases, compressed mip generation
rejection and six-face compressed cube byte sizing.

**API and compatibility:** No public signatures or enum values
changed. Correct compressed source buffers remain accepted;
invalid/undersized payload requests now fail with
`TextureDescError::InvalidUsage` instead of invoking undefined GPU
behavior. No save/network/ECS change.

**Verification:** Code successfully committed through GitHub.
Native compilation, CTest and GPU checks **NOT RUN** due to unavailable
repository checkout and queued CI; do not report passing tests.

**Next:** Compile `aiverse_texture_contract_test` and actual
client/editor on supported platforms; check that existing compressed
atlas/model callers supply full `levelCount/levelData/levelSize`.
Run Vulkan/Metal/GL compressed texture regression tests, fix any
remaining GPU-specific upload bugs, and continue the other feature
stages only with separate source+handoff commits.

## Stage 5E — single/dual-channel data texture formats (2026-11-18)

**Implementation commit:** `b1da7f8059cc651ba7c240ba5cfea71ce572dd3d`.
**Branch:** `feature/texture-rhi-unification`, draft PR #21.
**Paths:** `engine/gfx/Gfx.h`, Metal/GL/Vulkan RHI texture
implementations, Windows `engine/gfx/gl/GLApi.h` and
`tests/render/texture_contract.cpp`.

**Source implementation:** Append `PixelFormat::RG8`,
`R16F`, `RG16F`, `R32F`, `RG32F` without renumbering
existing public format values. Add shared
`uncompressedTexturePixelBytes` and
`isExtendedDataTextureFormat` helpers. Metal maps native
RG8/R16F/RG16F/R32F/RG32F pixel formats and uses the
accurate CPU row/slice byte stride for uploads/subresources.
GL33/GLES3 maps matching sized internal formats, RED/RG
external formats and byte/half/float transfer types for 2D,
3D and array resources; Windows loader enum coverage
extended. GLES3 float32 formats use nearest filtering by
default on platforms without float-linear support. Vulkan
maps matching VkFormats and staging byte strides. Source
contract test checks all new and existing byte sizes.

**Scope / compatibility:** New formats are for sampled data
textures, LUTs and normal/height/physics data only at this
stage. `renderTarget=true` for these formats intentionally
fails closed until graphics pipelines and render pass
format-matching support them. Existing render target,
save/network/ECS/legacy 2D behavior unchanged.

**Risks requiring next correction:** Metal default sampler
still treats new R32F/RG32F as linearly filterable, while
the conservative GL path forces nearest. Align this policy
and explicitly reject unsupported automatic mip generation
for new typed formats instead of silently allocating a
single level (manual `mipLevels`/subresource updates remain
available). This is the next bounded source stage.

**Verification:** GitHub code commit confirmed. Native
C++ compile, GPU sampling, CTest and CI success:
**NOT RUN**; no successful workflow result asserted.

**Next:** Fix float32 filtering/automatic-mip portability,
add corresponding tests, then validate on Mac Metal,
Windows GL33, mobile GLES3 and Vulkan. Avoid describing
typed render targets as supported before per-format
renderpass tests.

## Stage 5F — conservative typed mip policy and Metal float32 filtering (2026-11-18)

**Implementation commit:** `e17f109aeec3e1333d8b73f8f6779b9d5cfeae08`.
**Branch:** `feature/texture-rhi-unification` (draft PR #21).
**Paths:** `engine/gfx/Gfx.h`, `engine/gfx/metal/GfxMetal.mm`,
`tests/render/texture_contract.cpp`.

**Fix:** `validateTextureDesc` now rejects automatic
`mipmaps=true` for `RG8/R16F/RG16F/R32F/RG32F` until
all three backends have verified mip generation for those
formats. Explicit `mipLevels` with subresource uploads remain
supported, preventing silent fallback to a single mip on
one backend. Metal default sampler now requests nearest
filtering for R32F/RG32F just like its older RGBA32F data
path and the conservative GLES3 float32 policy. Added CPU
contract tests for automatic-mip rejection and explicit
mip-level acceptance.

**Compatibility:** Existing RGBA8/R8/depth/HDR shader paths
and `Texture2D` defaults are unchanged. New typed
formats remain sampleable-only until color-target pipeline
support is intentionally added. No save/network/ECS changes.

**Validation:** GitHub source commit confirmed. Native
Metal C++/Objective-C++ compilation, all CTests and GPU
validation **NOT RUN**. No passing CI claim.

**Next:** Run the typed format contract test and native
2D/3D/array upload/sampling on Metal, GL33, GLES3 and
Vulkan; verify row/slice pitch with actual GPU readback.
Then extend typed color render targets and remaining
integer/depth formats only after building and testing.

## Stage 5G — canonical typed/compressed texture documentation and test commands (2026-11-18)

**Documentation commit:** `9bb6fd6fd5d1ecd1cb09f02241c0efe7831c9a9e`.
**Branch:** `feature/texture-rhi-unification` (Draft PR #21).
**Changed:** `docs/texture-rhi.md`.

**Documentation changes:** Update actual source-stage status to
include the later DDS and typed-format work, describe native
BC4/BC5/EAC R11 with safe compressed mip/face validation,
RG8/R16F/RG16F/R32F/RG32F sampled-only scope, manual
mip upload, and float32 nearest filtering. Clarify remaining
integer/packed/HDR targets, Basis transcoding, MSAA, streaming
and WebGL/Android/desktop GPU gaps. Add exact CMake/CTest
commands for the five texture-focused CPU tests and Vulkan
backend integration tests, including real GPU-vs-skipped
distinction.

**Verification:** GitHub documentation commit completed.
No source C++ files changed in this stage.
Native builds, tests and GPU execution **NOT RUN**;
CI completion not confirmed. Do not infer production readiness.

**Next:** On a native checkout, run listed targeted
CMake/CTest commands and write actual outcomes here.
Fix any compiler/API/test failures before merging.
Then implement remaining GL/Vulkan MSAA and typed/HDR
formats, each with separate code/handoff commits.

## Stage 5H — correct GLES3 float32 mip filter selection (2026-11-18)

**Code commit:** `11aa645dded11c5912988f5af86803a09b3f718f`.
**Branch:** `feature/texture-rhi-unification`, draft PR #21.
**Changed:** `engine/gfx/gl/GfxGL.cpp`.

**Bug fix:** In 2D texture allocation, explicit `mipLevels>1`
previously selected `GL_LINEAR_MIPMAP_LINEAR` solely from the
requested `Filter::Linear`, even when GLES3's float32-format
policy had downgraded min/mag to nearest because linear float
filtering is not portable. Minification now derives mip filter
from the *effective* `f` (GL_NEAREST or GL_LINEAR), so
R32F/RG32F/RGBA32F remain nearest sampled with authored mips.
The dimensional texture allocation path already used this
effective-filter policy.

**Compatibility:** Only affects float32 formats with explicit
mips and a linear requested filter; legacy RGBA8/R8 mip
filtering remains the same. No public API, ECS, save,
network or on-disk asset changes.

**Verification:** GitHub code commit confirmed; native GL
compiler/driver tests and CTest **NOT RUN**.
No passing CI asserted.

**Next:** Validate actual GL33/GLES3 sampler completeness
for explicit float32 mip chains on Android, Windows and
WebGL2, then continue other typed formats and production
GPU validation.

## Stage 6J — compressed allocation failure safely falls back to RGBA8 (2026-11-18)

**Code commit:** `1216ee213a6e12bd91007a0587ddaac21d290c3e`.
**Branch:** `feature/texture-rhi-unification`; still Draft PR #21.
**Changed paths:** `engine/render/TextureAsset.h`,
`tests/render/texture_asset.cpp`.

**Defect found by source review:** `uploadContainerTexture` attempts
native compressed allocation first when the format is advertised.
If `Device::createTexture` rejects that allocation, CPU decode to
RGBA8 was attempted using the same `TextureDesc`, but stale
`levelCount` / `levelData` / `levelSize` still pointed to the BC
payload and `mipLevels` had been cleared to zero. Vulkan treats
explicit level data as an upload for uncompressed formats too: the
second allocation could therefore read compressed bytes as RGBA,
fail bounds validation or produce undefined sampling. GL/Metal also
would not reserve the explicitly authored fallback mip chain.

**Implemented:** After successful CPU decoding and before the second
allocation, reset `levelCount = 0`, `levelData = nullptr`,
`levelSize = nullptr`, restore `mipLevels` from
`CompressedImage::levels.size()`, then set `data` to decoded RGBA
level zero. Existing per-layer/per-z explicit `updateTextureRegion`
for the other mips remains unchanged. The native compressed success
path, old sampler2D material import, save/network/ECS code and
backends are otherwise untouched.

**Added test source:** `texture_asset` mock advertises BC5 support
but refuses a compressed `createTexture` call. The test checks the
BC5 native attempt, RGBA8 retry with no stale compressed pointers,
two allocated mip levels, base-level CPU image bytes and explicit
mip-1 upload. The existing BC5 opt-in/safe decoding paths remain
covered.

**Verification performed:** Reviewed the actual Metal, GL and Vulkan
`createTexture` sources and their mip precedence; both code/test
blobs were committed to GitHub. **C++ compilation: NOT RUN. CTest:
NOT RUN. GPU validation: NOT RUN.** This execution environment cannot
resolve `github.com` for a native repository clone. The existence
of test code does not mean it has passed.

**How to verify on a native checkout:**

```sh
git switch feature/texture-rhi-unification
git pull --ff-only origin feature/texture-rhi-unification
cmake -S . -B build/texture-rhi -DAIVERSE_BUILD_TESTS=ON
cmake --build build/texture-rhi --target aiverse_texture_asset_test
ctest --test-dir build/texture-rhi --output-on-failure -R '^texture_asset$'
```

Also run `texture_contract`, `dds_layers`, `ktx2_layers`,
`render_target_set` and actual Metal/GL/Vulkan allocation and
sampling coverage; investigate any native compiler or driver failures.
Keep PR #21 Draft until verified.

**Next bounded step:** Add a source-reviewed regression for
an *array/cubemap* compressed allocation rejection (not only BC5 2D),
and check integer overflow/budget caps before `TextureUploadDesc`
pixel-size conversions; then prioritize real CTest/GPU builds and
GL/Vulkan MSAA resolve support. Append the next agent handoff after
its code commit; never overwrite earlier stage entries.

## Stage 6K — cube-array CPU fallback parity and malformed metadata gates (2026-11-18)

**Code commit:** `bcc9564e38cc1e59b29dccdfd491626da9115774`.
**Branch:** `feature/texture-rhi-unification` (Draft PR #21).
**Paths:** `engine/render/TextureAsset.h`, `tests/render/texture_asset.cpp`.

**Implementation:** Before `arrayLayers * faces` in
`uploadContainerTexture`, reject a caller-created image with
`arrayLayers > INT_MAX / faces`, preventing signed integer overflow
in the RHI dimension conversion and downstream `levelImageCount`.
Reject a `oneDimensional` container with a non-unit height/depth,
arrays or cube faces rather than silently choosing a conflicting 2D/3D
dimension. This does not change valid DDS/KTX2 imports.

**Regression test source:** Expanded the test mock to inspect ALL
base-mip RGBA8 bytes for array/cubemap/volume allocations and retain
explicit mip upload layer IDs and first texels. Added a 12-face BC5
cube-array test with native compressed allocation rejected: the retry
must preserve CubeArray/12 layers/2 mips, upload 12 distinct decoded
base faces and independently upload all 12 layer mip-1 faces. Added
invalid array-layer overflow and malformed one-dimensional container
rejection tests. This builds upon Stage 6J's descriptor reset fix.

**Validation:** Committed source and tests to GitHub. **Native build:
NOT RUN. CTest: NOT RUN. GPU test: NOT RUN.** Execution environment
still cannot clone from `github.com` for full project compilation.
No claim that the new tests pass.

**Verification instructions:**

```sh
git switch feature/texture-rhi-unification
git pull --ff-only origin feature/texture-rhi-unification
cmake -S . -B build/texture-rhi -DAIVERSE_BUILD_TESTS=ON
cmake --build build/texture-rhi --target aiverse_texture_asset_test
ctest --test-dir build/texture-rhi --output-on-failure -R '^texture_asset$'
```

Also run the grouped texture suite documented in `docs/texture-rhi.md`.
Verify actual GPU BC5/cube-array sampling and Metal/GL/Vulkan
handling of uncompressed mip chains. No ECS/gameplay changes.
**Next:** Move to native compilation/CI and actual GPU tests first,
then GL/Vulkan MSAA/resolve, mixed-format MRT and typed formats in
separate bounded stages. Continue append-only per-stage handoffs,
with exact source SHAs and actual test outcomes.

## Stage 6L — preserve declared Texture3D with a single depth slice (2026-11-18)

**Code commit:** `3adb75295497168b0d271b263ca27a46184e22fa`.
**Branch:** `feature/texture-rhi-unification`, Draft PR #21.
**Files:** `engine/render/TextureCodec.h`,
`engine/render/TextureCodec.cpp`, `engine/render/ImageDds.cpp`,
`engine/render/TextureAsset.h`,
`tests/render/ktx2_layers.cpp`, `tests/render/dds_layers.cpp`,
`tests/render/texture_asset.cpp`.

**Root cause:** Both DDS DX10 `resourceDimension=TEXTURE3D` and
KTX2 `pixelDepth>0` declare a volume texture even when its depth
happens to be **one**. Previously `CompressedImage` stored only
numeric `depth`, and `uploadContainerTexture` inferred Texture3D
only from `depth>1`: authored 3D textures with one z slice were
silently turned into Texture2D.

**Implementation and API:** New additive
`CompressedImage::threeDimensional` boolean (defaults false for
existing manually built 2D images). KTX2 marks it when `pixelDepth`
is nonzero; DDS DX10/legacy parser marks it when the header declares
a volume. Both parsers preserve a true 3D image at depth=1.
The generic uploader chooses Texture3D when either this flag is set
or `depth>1` (for compatible legacy callers). Invalid combinations
(1D/cube/array with declared 3D) fail closed. Native BC paths remain
restricted to 2D/arrays/cubemaps, even when a 3D image has depth=1:
3D compressed textures continue through CPU fallback.

**Test additions (source only):** Synthetic single-slice volume in
`ktx2_layers` and `dds_layers`; invalid 3D/cube KTX2 combination;
`texture_asset` mock checks that a one-slice volume allocates a
Texture3D rather than Texture2D. Existing mock lifetime count
adjusted for the added successful allocation.

**Verification performed:** GitHub code commit succeeded. Local
C++ build **NOT RUN**, `ktx2_layers` / `dds_layers` /
`texture_asset` CTests **NOT RUN**, native GPU sampling **NOT RUN**.
The execution container still cannot resolve GitHub for a native
checkout; no green CI status verified. This is source-level
implementation, not platform certification.

**Next agent should run:**

```sh
git switch feature/texture-rhi-unification
git pull --ff-only origin feature/texture-rhi-unification
cmake -S . -B build/texture-rhi -DAIVERSE_BUILD_TESTS=ON
cmake --build build/texture-rhi --target \
  aiverse_ktx2_layers_test aiverse_dds_layers_test aiverse_texture_asset_test
ctest --test-dir build/texture-rhi --output-on-failure \
  -R '^(ktx2_layers|dds_layers|texture_asset)$'
```

Check actual Vulkan, Metal and GL 3D sampling with depth=1. Run the
remaining target set from `docs/texture-rhi.md` and report genuine
results. Continue with GL/Vulkan MSAA and resolve, mixed MRT and typed
formats only after validating the current source stages. Any
additional implementation must have its own append-only handoff.

## Stage 4R — GL33/GLES3 mixed-format MRT color attachments (2026-11-18)

**Code commit:** `fbbe97fbb030ee03bccbd81f13788b6c61c23903`.
**Branch/PR:** `feature/texture-rhi-unification`, Draft PR #21.
**Changed file:** `engine/gfx/gl/GfxGL.cpp`.

**Change:** Removed the unnecessary requirement that the 1–4 GL MRT
color targets all use the same pixel format. The existing permitted
RGBA8 / RGBA8_sRGB / RGBA16F attachment formats now each retain their
own format in `explicitPassFormats_`. `setPipeline` compares each slot
against `PipelineDesc.colorFormat` or
`PipelineDesc.additionalColorFormats[i-1]`, rejecting mismatches
before draw. The historical automatic HDR shader variant still applies
only to all-RGBA16F passes; mixed HDR/UNORM passes require an explicitly
compatible shader/pipeline. Desktop framebuffer sRGB conversion is
enabled when any color attachment is sRGB; the framebuffer stores
linear/encoded values per actual attachment format.

**Unchanged:** GL attachment count, extent, mip/layer checks, FBO
completeness, independent clear operations, legacy `beginPass`,
same-format MRT and pipeline defaults. GL/Vulkan MSAA/resolve,
per-attachment blend overrides and typed color targets are still
outside this stage.

**Verification:** Source edit and GitHub commit confirmed.
**C++ build NOT RUN; GL33/GLES3/WebGL2 GPU run NOT RUN; tests NOT RUN**,
per requested defer-testing workflow. FBO completeness and shader
linkage must be checked on real drivers. The backend still rejects
unsupported render passes instead of silently changing output count.

**Next:** Add GL MSAA renderbuffer attachment allocation + per-color
resolve with strict sample-count/format validation, and Vulkan MSAA
render pass support; then revisit mixed-format MRT across Vulkan/Metal
and independent blend policy. Keep every subsequent stage's source
commit and append-only handoff commit separate.

## Stage 4S — GL33/GLES3 multisample renderbuffers and per-color resolve (2026-11-18)

**Code commit:** `769f7d9d83ced30b3daf0bcccae642223e3a97e7`.
**Branch/PR:** `feature/texture-rhi-unification`, Draft PR #21.
**Files:** `engine/gfx/gl/GLApi.h`, `engine/gfx/gl/GfxGL.cpp`.

**Implementation:** The GL33/GLES3/WebGL2 portable MSAA path now
creates multisampled `Texture2D` render attachments as native
`GL_RENDERBUFFER` objects. The existing `TextureHandle` stores this
presentation-only renderbuffer and carries its sample count; sampled
binding, updating pixels, and constructing texture views of such
renderbuffers fail explicitly instead of treating renderbuffer IDs as
GL texture IDs. Destruction uses `glDeleteRenderbuffers`.
`GLApi.h` adds Windows GL33 function-loader entries for
renderbuffer creation/deletion/storage/framebuffer attachment.

`beginRenderTargets` validates homogeneous sample counts across
all color and depth attachments; resolve destinations must be distinct
single-sample 2D texture targets with matching mip extent and pixel
format. `createPipeline` checks the sample count against device
`GL_MAX_SAMPLES`; `setPipeline` enforces the active pass sample count.
The FBO attaches renderbuffers or existing mip/layer texture views
as appropriate; attachment formats remain per slot (Stage 4R).

At `endPass`, for every color slot with a resolve destination,
a temporary single-sample draw FBO receives a
`glBlitFramebuffer(..., GL_COLOR_BUFFER_BIT, GL_NEAREST)` from the
selected multisample source color attachment. This uses ES3/GL33
renderbuffers and blit rather than unsupported ES3 multisample
sampled texture objects. The legacy `copyColor` path refuses raw
renderbuffer handles; `RenderTargetSet::sampledColor(i)` yields the
allocated single-sample resolve texture. Transient MSAA outputs
without an explicit resolve remain un-sampleable.

**Compatibility and limits:** RHI `TextureDesc` validation already
requires 2D, renderTarget=true, no initial data/storage/mips for MSAA.
Only RGBA8/RGBA8_sRGB/RGBA16F color and Depth targets are wired;
the GPU may still reject a requested format/sample count. GL per-slot
resolve supports selected 2D mip destinations, but not layered
multisampled textures or depth resolve. State and FBO cleanup are
implemented in source; actual behavior on GL33, GLES3, WebGL2 and
Android remains unverified.

**Verification:** Connector wrote code commit and updated branch.
**No build executed. No CTest executed. No GPU rendering executed.**
No passing CI results asserted.

**Next:** Implement Vulkan MSAA attachment allocation/pipeline and
per-color renderpass resolves, plus native GPU verification on Vulkan
and GLES3/WebGL2. On macOS/Windows/Android tests, verify no GL errors,
sample-count mismatch rejection, multi-target clear/resolve, sRGB
conversion, depth attachments, and lifetime cleanup. Revisit mixed
attachment blend policies and sample-count flexibility separately.
Always append a new handoff after each code stage.

## Stage 4T — Vulkan mixed-format MRT renderpass and pipeline compatibility (2026-11-18)

**Code commit:** `2060e448c75550b789a3706072f55f99a1f2b42d`.
**Branch/PR:** `feature/texture-rhi-unification`, Draft PR #21.
**Files:** `engine/gfx/vulkan/VkDevice.h`, `engine/gfx/vulkan/VkDraw.cpp`.

**Implementation:** Vulkan MRT pass validation no longer rejects
different formats in separate color output slots (within the already
supported RGBA8/RGBA8_sRGB/RGBA16F set). MRT renderpass creation and
cache keys now hold the full fixed-size array of per-attachment
`VkFormat` rather than assuming all colors share attachment zero's
format. `VkAttachmentDescription::format` is populated per slot.
The per-frame `passFormats_` records the active pass formats; Vulkan
graphics pipeline variant cache keys additionally compare every
attachment format to avoid an incompatible pipeline being reused for
the same first output format. `setPipeline` checks
`PipelineDesc.colorFormat` and `additionalColorFormats` against the
corresponding actual pass slots, preserving legacy single-output
permissiveness. Automatic HDR specialization applies only when *all*
active color outputs are RGBA16F; heterogeneous passes require an
explicit matching pipeline. No save, ECS, material-cache or
replication changes.

**Limitations:** Only matching attachment dimensions and single
samples are supported by this Vulkan source stage. Per-target
blend policies are still global. Texture create/resolve and pipeline
support for MSAA have not been implemented in Vulkan yet. The loader
and renderpass API remain backward compatible with single-format MRT.

**Verification:** GitHub source update and branch commit completed.
**C++ compilation NOT RUN, Vulkan validation NOT RUN, CTest NOT RUN**,
as requested for deferred testing. Mixed format renderpass
compatibility and cache behavior must be checked with validation
layers and a real Vulkan driver before approval.

**Next stage:** Vulkan multisample image creation + renderpass
resolve attachments and pipeline raster sample matching. Add
GPU fixtures for 2–4 mixed-format MRT output and verify proper
pipeline cache separation. Follow with storage images and
streaming/readback features in separate bounded stages, each
documented immediately afterward.

## Stage 4U — reusable RenderTargetSet per-attachment format policy (2026-11-18)

**Source commits:** `69ee1e1ac6a25863d3b6680e639a06d1e8c10300`
(native owner) and `2f605fe3d231d571a79644b4ca889a7857c45b66`
(regression test source).
**Branch:** `feature/texture-rhi-unification`, Draft PR #21.
**Files:** `engine/gfx/RenderTargetSet.h`,
`tests/render/render_target_set.cpp`.

**API:** `RenderTargetSet::Config` gains
`bool perAttachmentFormats = false` and
`std::array<PixelFormat, kMaxRenderTargetColors> colorFormats`.
The previous `colorFormat` still configures **every** color target by
default. When per-attachment formats are enabled, a separate format
is selected for each slot at allocation and verified before touching
the GPU. The conservative backend-portable set is RGBA8,
RGBA8_sRGB and RGBA16F; unsupported compressed/typed formats fail
closed. Each MSAA resolve target gets its corresponding color
format. The new `colorFormat(index)` accessor reports the expected
pipeline output slot format. Depth-only sets now validate their
depth descriptor independently, without treating color slot zero
as an allocated attachment.

**Test source:** Mock device now advertises the declared color
format capabilities. New `mixedFormats()` tests a three-color
RGBA8/RGBA16F/RGBA8_sRGB target set with depth, per-slot owner
metadata, pass entry, correct lifetime cleanup, fail-before-allocation
on BC7 MRT and depth-only creation. Prior allocation rollback and
MSAA tests remain in place.

**Verification:** GitHub code/test commits confirmed.
**Build NOT RUN; CTest NOT RUN; Metal/GL/Vulkan GPU test NOT RUN.**
Backend runtime support and mixed-format shader output compatibility
must still be checked on real devices.

**Native caller contract:** Configure compatible
`PipelineDesc.colorAttachmentCount`, `colorFormat`, and
`additionalColorFormats` matching `targets.colorFormat(i)`.
Use explicit MRT fragment output locations 0..N-1 and
`targets.sampledColor(i)` when MSAA resolves are requested.
Continue recording architecture/API/files/actual tests and next work
after each bounded step. Next: Vulkan MSAA/resolve; streaming/upload
lifecycle and import format coverage are still unfinished.

## Stage 4V — Vulkan multisample image allocation and capability gates (2026-11-18)

**Code commit:** `d8a539959793e59d45bddcb2a9ab1a5da3e97395`.
**Branch/PR:** `feature/texture-rhi-unification`, Draft PR #21.
**Files:** `engine/gfx/vulkan/VkDevice.h`,
`engine/gfx/vulkan/VkResources.cpp`.

**Source implementation:** `VTexture::samples` stores the physical
sample count; `createImage` uses that count in
`VkImageCreateInfo::samples`. `createTexture` can now allocate
render-only multisampled Texture2D color (RGBA8/RGBA8_sRGB/RGBA16F)
and depth attachments, checking both physical-device framebuffer
sample masks and `vkGetPhysicalDeviceImageFormatProperties`.
Multisample images use attachment-only VkImageUsage rather than the
usual sampled/upload path, and skip sampler creation, initial staging,
and shader-read layout initialization. They are intended to be cleared
and resolved through a later Vulkan render-pass stage, not bound
directly as sampled textures. Pixel updates and texture views reject
multisampled parents. `destroyTexture` continues to retire images
and memory using the existing deferred GPU-safe release mechanism.

**Critical integration gap:** This is an allocation stage ONLY.
`VkDraw.cpp` still rejects explicit MSAA resolves and does not yet
build sample-count-compatible renderpasses or pipelines. The new
resource is not usable as a complete render target until Stage 4W.
Do not claim MSAA Vulkan is ready, even for a single attachment.

**Verification:** Source commit to GitHub succeeded. C++ compile
**NOT RUN**, CTest **NOT RUN**, Vulkan device/validation **NOT RUN**.
Native verification must check imageProps queries, allocation failure,
memory release and compatibility with RenderTargetSet on Vulkan.

**Next:** Build Vulkan `VkSubpassDescription::pResolveAttachments`
with an explicit per-slot resolve mask, sample-matched color/depth
attachments, pipeline multisample state and cache keys; all scoped
resolve image views/framebuffers must be deferred until GPU completion.
Then real validation layers and sampling tests. Record next code SHA
and real verification status in a new append-only handoff entry.

## Stage 4W — Vulkan MSAA renderpasses, resolves and pipeline matching (2026-11-18)

**Code commit:** `fdf75870cbdfb43d50ede67cacbcad1a4bbcdd99`.
**Branch/PR:** `feature/texture-rhi-unification`, Draft PR #21.
**Files:** `engine/gfx/vulkan/VkDevice.h`,
`engine/gfx/vulkan/VkDraw.cpp`.

**Source implementation:** Complete the Stage 4V allocation half at
render-pass level. Vulkan `renderPassForMrt` now takes the physical
sample count and a per-color resolve mask, included in its cache
key. The renderpass declares N multisample color attachments, then
one `VK_SAMPLE_COUNT_1_BIT` attachment for every requested resolve
slot, then optional same-sample depth. Subpass
`pResolveAttachments` maps color outputs to their correct resolve
slot, with `VK_ATTACHMENT_UNUSED` where no resolve exists.
The framebuffer builds the matching temporary 2D image views in
physical attachment order; all color, resolve and depth views
are deferred until after GPU execution completes.

`beginRenderTargets` checks sample counts across color/depth,
the resolved image's format, extent, mip, dimensionality, and
single-sample usage, rejects aliasing resolve destinations, and
requires clear=true for MSAA render attachments while there is no
persistent multisample layout tracker. A depth-only MSAA pass is
supported with a color count of zero. Native renderpasses select
attachment layouts and renderpass dependencies; the resolved
single-sample images return to SHADER_READ_ONLY_OPTIMAL.

`PipelineDesc.sampleCount` may now request supported
1/2/4/8/16x counts. Pipeline cache and `VkPipelineMultisampleStateCreateInfo`
track actual sample count and resolve topology. `setPipeline`
rejects mismatched sample counts. Legacy beginPass deliberately
skips multisampled resources with an error; callers use
beginRenderTargets. Direct sampling of an MSAA-only texture binds
a dummy and reports an error; sample the resolve destination.

**Restrictions:** Source-level only; Vulkan GPU tests/validation
not yet run. There is no depth resolve, no layered multisampling,
no non-cleared persistent multisample pass load, and no storage
image support in this stage. Mixed-format color MRT remains
supported as in Stage 4T. All existing Texture2D 1x pass paths
and native ECS game code remain unchanged in intended behavior.

**Actual verification:** GitHub code/tree/commit/ref update
**SUCCEEDED**; C++ native build **NOT RUN**; CTest **NOT RUN**;
Vulkan validation-layer GPU test **NOT RUN**. Do not merge Draft PR
on the basis of this source-only change.

**Next:** Add Vulkan device fixture with 2x/4x two-color clear,
two independent resolves, depth attachment, mixed HDR/UNORM and
shader sampling of resolved images. Run compiler + validation layers
and fix any errors. Then complete remaining import formats, async
readback/streaming and native editor inspection in separate stages,
each followed by its own handoff.

## Stage 4X — Vulkan 4x MSAA two-color resolve integration fixture (2026-11-18)

**Code commit:** `a5009a998417f3e042044f7d8f484f8a07f65879`.
**Branch:** `feature/texture-rhi-unification` (Draft PR #21).
**Files:** `tests/render/vulkan_tests.cpp`.

**Source:** New Vulkan GPU fixture allocates two 4x multisample RGBA8
render targets, one 4x depth attachment and two separate 1x resolve
textures. It first renders independent red and green clears and samples
both resolved textures into opposite screen halves for pixel assertions.
It then tests a 4x `PipelineDesc.colorAttachmentCount=2` fragment shader
writing blue/yellow into the two MRT slots, verifies the resolve outputs
again and destroys every resource. The fixture prints an explicit skip if
4x image creation is unsupported on the selected GPU; that skip is NOT a
passing MSAA validation. It exercises the already-implemented 4V/4W
Vulkan source stages without duplicating the other agent's code.

**Compatibility:** Test-only source stage. No new public RHI API, ECS, save,
network or runtime rendering changes.

**Actual validation:** GitHub code commit succeeded. Native compiler,
`aiverse_vulkan_tests`, Vulkan validation layers and GPU image
assertions: **NOT RUN** (the execution container has no git checkout
and cannot resolve github.com for clone). GitHub Actions latest results
were queued, not passed. Do not claim Vulkan MSAA is validated.

**Next:** Build `aiverse_vulkan_tests` and execute
`ctest --test-dir build/texture-rhi --output-on-failure -R '^vulkan_backend$'`
on real Vulkan SDK/Lavapipe, inspect validation errors and screen captures.
Then complete GL/GLES MSAA and cross-platform regression tests.
Commit the next code stage and a separate handoff as always.

## Stage 5I — BC6H signed/unsigned HDR container and RHI contracts (2026-11-18)

**Code commit:** `bdbc6f618a6419801757169de65bdbec69b8b2a3`.
**Branch/PR:** `feature/texture-rhi-unification`, Draft PR #21.
**Files:** `engine/gfx/Gfx.h`, `engine/render/TextureCodec.h`,
`engine/render/TextureCodec.cpp`, `engine/render/TextureAsset.h`.

**Implemented source contract:** Added
`gfx::PixelFormat::BC6H_UFloat` and `BC6H_SFloat` as 4×4
16-byte compressed GPU formats and matching `BlockFormat` values.
KTX2 VkFormats 143 (UFLOAT) and 144 (SFLOAT) now preserve native
BC6H compressed levels, faces and layers in `CompressedImage`.
`uploadContainerTexture` maps both to the corresponding GPU format.
BC6H represents HDR float radiance, so there is intentionally
**no unsafe RGBA8 CPU decoder/fallback**: when the backend rejects
native support, import fails explicitly instead of truncating HDR.
The common compressed mip/layer payload validator applies.

**Integration note:** This is the format/codec stage. Native
Metal/GL/Vulkan mappings and DDS DX10 95/96 are the separate next
stage, so do not claim native HDR BC6H sampling yet. Existing
2D material import, ECS, saves and unrelated formats are unchanged.

**Verification:** GitHub code commit confirmed. **Build NOT RUN,
CTest NOT RUN, GPU validation NOT RUN**, per the deferred testing
request. Test source and GPU fixture work is still pending.

**Next:** Add BC6H_UFloat/SFloat to native Metal pixel formats
(macOS only), Vulkan VkFormat, GL BPTC compressed enums and
capability gates, DDS DX10 layered parser, and synthetic import
tests. Record this as Stage 5J with its own handoff immediately.

## Stage 5J — native BC6H HDR sampling and DDS DX10 import metadata (2026-11-18)

**Code commit:** `db07550957cb09e710eefdfd7492fa6687224842`.
**Branch/PR:** `feature/texture-rhi-unification`, Draft PR #21.
**Files:** `engine/render/ImageDds.cpp`,
`engine/gfx/gl/GfxGL.cpp`, `engine/gfx/metal/GfxMetal.mm`,
`engine/gfx/vulkan/VkResources.cpp`.

**Implementation:** Finish the Stage 5I format contract. DX10
DDS layered parser recognizes 95/96 (`BC6H_UF16` /
`BC6H_SF16`) and preserves native mip-major compressed blocks
across array/cube faces as `BlockFormat::BC6H_UFloat/SFloat`.
The classic DDS-to-`Model::Image` 2D RGBA8 adapter deliberately
remains unchanged: it has no decoder for HDR BC6H.

GL33/GLES3 backend maps to BPTC unsigned/signed float 4×4
compressed enums, gated on the BPTC extension. Vulkan maps to
`VK_FORMAT_BC6H_UFLOAT_BLOCK` and
`VK_FORMAT_BC6H_SFLOAT_BLOCK`, with existing format-capability
query. Metal maps macOS BC6H formats (Ufloat/RGBFloat), using
the existing BC support probe and failing closed on iOS.
All backends retain the complete compressed mip payload contract.
The content requires a GPU with native BC6H support; no CPU HDR
fallback is silently substituted.

**Verification:** GitHub source commit succeeded. **No native
build, CTest, or GPU texture sampling run.** Test source for synthetic
BC6H DDS/KTX2 and actual macOS/Windows/Vulkan device sampling is
pending, along with other HDR/EXR import features.

**Next:** Add synthetic unsigned/signed BC6H import, byte-bound,
and GPU capability fallback coverage, and a native shader capture
with HDR radiance values above 1.0. Then implement HDR/EXR
uncompressed float import and runtime texture streaming. Continue
append-only per-stage handoffs.

## Stage 7A — bounded texture streaming queue API (2026-11-18)

**Implementation commit:** `0aea8701e634c89031327576a46fc67b52ab2d47`.
**Branch:** `feature/texture-rhi-unification`, PR #21 Draft.
**Changed file:** `engine/gfx/TextureStreaming.h` (new, header-only).

**Implementation:** Public genre-independent `gfx::TextureStreamQueue`
owns its created sampled, uncompressed GPU textures, creates monotonic
logical `TextureStreamId` tokens independent of backend `TextureHandle.id`,
and accepts `TextureUploadDesc` with caller data copied into queue-owned CPU
staging before the call returns. It validates the texture dimension, format,
mip/layer/3D bounds, row/slice pitches, payload byte size and safe arithmetic,
and enforces configurable total GPU memory, CPU staged memory, per-frame
transfer bytes, and per-frame upload counts. `drain()` runs FIFO on the GPU
device thread **outside** render passes, copying queued subresources through
`Device::updateTextureRegion`; if a GPU write fails it marks its logical
resource failed and cancels remaining queued writes. `cancel(ticket)`,
`destroy(id)`, `shutdown()`, `texture(id)` and `stats()` give ownership,
cancelation, diagnostics, and cleanup. Logical IDs are never recycled even if
the GPU backend reuses texture IDs, preventing pending data from landing on
a replacement texture. No extra dependencies; this is presentation-only and
usable in native game/project C++.

**Contract/limits:** This is **deferred frame-budgeted streaming**, not
background I/O or asynchronous GPU readback. Only uncompressed, sampleable,
non-MSAA textures and explicit manual mip allocations; no compressed partial
updates, storage write textures or virtual page tables. The `gfx::Device`
must outlive the queue, and its `updateTextureRegion` must consume/copy
the provided CPU bytes synchronously before returning. Queue calls must be
made on the rendering thread; concurrent submissions are not supported.
Destroy via this queue, not directly through the backend, to retain logical
generation ownership.

**Compatibility:** Standalone opt-in header; no existing `gfx::Device`
signature, save/network/ECS or renderer path changes.

**Verification:** GitHub source code/tree/ref update succeeded.
No native build, CTest or GPU execution took place because this environment
does not have a local AIVERSE clone and cannot resolve github.com.
CI jobs for earlier branch HEADs were queued, not passing. **NOT RUN**.

**Next:** Add `aiverse_texture_streaming_test` using a mock gfx::Device and
assert CPU buffer ownership, strict per-frame byte/update budgets, cancellation,
allocation rollback, row/slice validation, failed update quarantine and stale
resource-ID rejection. Execute compiler/CTest on a native checkout and write
actual outcomes. Afterwards consider optional readback and streaming residence
policy, keeping code and handoff as separate commits.

## Stage 7B — CPU staging and streaming scheduler regression tests (2026-11-18)

**Code commit:** `3ee65b1b522b8b40250889eb98cfc3436eaf2c3f`.
**Branch:** `feature/texture-rhi-unification`, Draft PR #21.
**Paths:** `tests/render/texture_streaming.cpp`, `CMakeLists.txt`.

**Test source:** Register GPU-independent `texture_streaming` CTest.
A mock gfx::Device verifies bounded texture GPU allocation, staging
byte caps, per-frame transfer/update limits, deep-copy of caller-owned
CPU source before it changes, sequential FIFO drains, ticket
cancellation, logical IDs not reused, stale-token rejection, cleanup
on shutdown, 3D volume upload with padded rows/slices, out-of-bounds
and truncated upload rejection, GPU update failure quarantine and
cancellation of subsequent jobs for the failed texture. The mock
uses the public Device interface so it also tests native C++ include/
link viability once compiled.

**Runtime/code changes:** Test-only, apart from registration in
CMake. Existing render paths, serialization, ECS/gameplay, and
resources unchanged. Staged uploads remain render-thread
synchronous GPU transfers, not actual asynchronous readbacks.

**Verification:** GitHub source commit/ref update succeeded.
Native C++ compilation and CTest execution: **NOT RUN** (no checkout;
container network DNS cannot resolve github.com). CI not confirmed
passing. Do not report fixture green until actually executed.

**Next:** Run
`cmake --build build/texture-rhi --target aiverse_texture_streaming_test`
and
`ctest --test-dir build/texture-rhi --output-on-failure -R '^texture_streaming$'`.
Correct compiler/behavior failures. Then add streaming texture
residency/LOD policy and a separate skill for native game agents;
continue with GL MSAA/resolve and editor resource views in
independent stage/handoff commits.

## Stage 5K — portable Radiance RGBE HDR import to RGBA16F (2026-11-18)

**Source commit:** `e2fb7b723ef6e214ff2709b16858e3c9a34f074f`.
**Branch/PR:** `feature/texture-rhi-unification`, Draft PR #21.
**New file:** `engine/render/HdrTexture.h`.

**Source implementation:** Header-only native C++ Radiance RGBE
loader and RHI upload utility. `parseRadianceHdr(std::string_view,
RadianceImage&, std::string&)` reads the Radiance / RGBE header with
`FORMAT=32-bit_rle_rgbe`, supports flat scanlines and modern
per-channel RLE scanlines, and preserves +/- horizontal and vertical
orientation into a canonical image with linear RGBA16F texels.
`RadianceImage` contains width, height and packed IEEE-754
binary16 RGBA texels; alpha is 1.0. File extent, scanline length,
packet lengths and a 512 MiB output budget are checked before
allocation. `uploadRadianceHdr(Device&, image, error)` calls
`Device::createTexture` using `gfx::PixelFormat::RGBA16F`,
checking format support; caller owns the returned TextureHandle.
Color values above 1.0 remain linear HDR; no tone mapping is
applied. This is presentation-only, and does not change ECS, saves
or existing model-material paths.

**Explicit gaps:** OpenEXR, legacy Radiance repeat-marker scanlines,
arbitrary XY-major orientations, and applying exposure/gamma metadata
are not supported. Do not claim complete HDR/EXR format coverage;
this stage specifically imports Radiance RGBE. The import's RLE,
rounding and GPU upload behavior still require verification.

**Verification:** GitHub source commit created successfully.
C++ build **NOT RUN**, CTest **NOT RUN**, GPU render **NOT RUN**,
per user's request to finish feature source before testing.

**Next:** Add source-level test fixture and convenience decode+upload
function, fix any code-review findings, then introduce optional
OpenEXR scanline parsing and resource streaming/readback phases with
separate source/handoff commits.

## Stage 5L — Radiance HDR one-call upload and half boundary correction (2026-11-18)

**Source commit:** `d0802baf79dd9e5177d8e9231dcab83abb8a4b9e`.
**Branch/PR:** `feature/texture-rhi-unification`, Draft PR #21.
**File:** `engine/render/HdrTexture.h`.

**Change:** Fix the IEEE binary16 subnormal-to-normal rounding
transition: values that round to 1024 units of 2^-24 produce the
smallest normal half (`0x0400`), rather than being incorrectly
clamped at `0x03FF`. Add
`loadRadianceHdr(gfx::Device&, std::string_view, std::string&)`
as a convenience native C++ API combining bounded
`parseRadianceHdr` and `uploadRadianceHdr`. The CPU scratch
lifetime is scoped to the call; GPU ownership remains with the
caller. Existing two-step API remains intact.

**Verification:** Source commit on GitHub verified.
**C++ build NOT RUN; CTest NOT RUN; GPU test NOT RUN** per the
user's defer-testing decision.

**Next:** Add a standalone source fixture and CMake test registration
for flat RGBE, RLE, orientation, HDR values, malformed input,
binary16 boundary and device upload/failure; record exact new
source commit and deferred test status in the next handoff. After
that tackle EXR import and texture readback/streaming coverage.

## Stage 5M — standalone Radiance HDR CPU/GPU-contract test source (2026-11-18)

**Source commit:** `0382dcd42e25c26523d4b737d44d9349e1ee361d`.
**Branch/PR:** `feature/texture-rhi-unification`, Draft PR #21.
**Files:** New `tests/render/hdr_texture.cpp` and `CMakeLists.txt`.

**Implementation:** Registered the GPU-independent CTest target
`aiverse_hdr_texture_test` / `hdr_texture` under
`AIVERSE_BUILD_TESTS AND NOT EMSCRIPTEN`. The new synthetic
fixture source constructs flat RGBE with radiance >1.0, a modern
per-channel RLE scanline, flipped horizontal orientation, truncated
payloads, invalid run-length packets, oversized image metadata,
and a binary16 subnormal-to-normal boundary. A mock `gfx::Device`
captures RGBA16F upload bytes and verifies caller GPU ownership,
unsupported-format early refusal and failed allocation reporting.
The tests are self-contained (no external .hdr asset).

**Execution:** Source and CMake registry commit confirmed on GitHub.
**No CMake configuration, compilation, CTest or real GPU tests ran**,
per user's plan to finish feature source before verification.
Do not claim the fixture passes.

**How to run after implementation is complete:**

```sh
cmake -S . -B build/texture-rhi -DAIVERSE_BUILD_TESTS=ON
cmake --build build/texture-rhi --target aiverse_hdr_texture_test
ctest --test-dir build/texture-rhi --output-on-failure -R '^hdr_texture$'
```

**Next:** Add optional OpenEXR uncompressed scanline support
(half/float RGB[A] channels), and integrate texture load into
project-native APIs; keep unsupported EXR compression explicit.
Work on background streaming and portable readback as separate
source stages with handoff after each.

## Stage 7C — post-drain streaming completion notifications (2026-11-18)

**Code commit:** `a84382bbae16b89cd8f5c99afb3c59f09b092be3`.
**Branch:** `feature/texture-rhi-unification`, draft PR #21.
**Files:** `engine/gfx/TextureStreaming.h`,
`tests/render/texture_streaming.cpp`.

**Implementation/API:** `gfx::TextureStreamQueue::drain` accepts an optional
`CompletionFn` receiving `(TextureUploadTicket,TextureStreamId,bool success)`
for each GPU update attempted in that frame. Results are accumulated during
the bounded FIFO transfer loop and callbacks are called **after** all queue
iteration is finished. This avoids iterator invalidation and supports safe
resource destruction or scheduling new uploads from completion callbacks.
Callbacks execute on the GPU device/render thread, not asynchronously or
on background workers. Existing no-arg `drain()` callers are source
compatible. Cancelled jobs do not produce a GPU completion callback;
`cancel` and `destroy` remain explicit.

**Tests added:** Mock-device streaming fixture now validates two successful
per-ticket completions in submission order, matching stable logical
resource IDs, and a failed GPU submission callback observing its resource
quarantined before notification.

**Compatibility:** Header-only optional API; no new backend virtuals, ECS,
save/network or on-disk formats. The GPU ownership and byte budgets from
stages 7A–7B remain unchanged.

**Verification:** GitHub code/tree/branch update confirmed. Native C++
build, CTest, graphics device validation: **NOT RUN**. Latest GitHub
Actions results have been queued, not confirmed passing. This is an
unverified source change, not a production-ready streaming system.

**Next:** Implement a CPU-only per-mip/per-array-layer residency planner
that requests complete coarse-to-fine mips and maps completion success
to safe sampler LOD clamps. Add deterministic tests, then connect it to
streaming queues. Run the actual native tests and GPU sampling.

## Stage 5N — bounded scanline OpenEXR HALF/FLOAT import (2026-11-18)

**Source commit:** `29bb2e6887305b3ac15adc4e7ad57a9c710e7de0`.
**Branch/PR:** `feature/texture-rhi-unification`, Draft PR #21.
**New file:** `engine/render/ExrTexture.h`.

**Implementation:** Header-only native C++ uncompressed OpenEXR
reader `parseOpenExr(std::string_view, RadianceImage&, std::string&)`
and one-call `loadOpenExr(Device&, bytes, error)`, reusing the
Stage 5K RGBA16F GPU uploader. Supports OpenEXR version 2,
single-part, untiled, **uncompressed** scanlines with arbitrary
channel order and full-resolution planar R/G/B and optional A
HALF (binary16) or FLOAT (binary32) data; unrelated channels are
skipped according to declared byte stride. Honors dataWindow
origin (including negative offsets), chunk scanline offsets and
out-of-order physical scanlines. Missing alpha becomes 1.0;
negative linear radiance is retained with its sign. Disallows
duplicate RGB channels, nonfinite pixel values, integer RGB
channels, subsampled channels, corrupt offsets/row sizes, huge
attributes and 512 MiB+ decoded allocations. Source is
presentation-only; neither ECS nor legacy material import changes.

**Explicit limitations:** This does NOT implement general OpenEXR:
ZIP/ZIPS/PIZ/RLE/DWAA/DWAB/B44/B44A or PXR24 compression,
deep/tiled/multipart images, multi-resolution/mipmap level
files, arbitrary channel naming (requires bare R/G/B), color
management/chromaticities or half-float texture array imports.
These inputs fail closed with errors instead of being silently
converted. No third-party OpenEXR codec dependency introduced.

**Verification:** GitHub source commit confirmed. **C++ compilation
NOT RUN, CTest NOT RUN, GPU test NOT RUN** (requested deferred tests).
Needs synthetic scanline fixtures, malformed payload tests and
actual macOS/GL/Vulkan rendering before production approval.

**Next:** Add EXR fixture source and CMake target without executing
tests; then document EXR/HDR public APIs and plan compressed EXR
integration using a vetted decoder, plus texture readback and
resident streaming. Each stage must get an append-only handoff.

## Stage 7D — generation-safe mip residency planner (2026-11-18)

**Code commit:** `3fba022b93aeec0e865f2941dbcb96cf41a3ed35`.
**Branch:** `feature/texture-rhi-unification`, Draft PR #21.
**File:** new `engine/gfx/TextureMipResidency.h`.

**Source implementation/API:** `gfx::TextureMipResidency` is a CPU-only
presentational mip residency scheduler for `TextureDesc` resources.
`initialize` validates the original descriptor and builds a 0/1/2
absent/queued/resident state per mip and per layer/face (volume textures
use one complete volume per mip). `claimNext` prioritizes coarsest
mip and then layer order. `complete(TextureMipRequest,bool)`
marks upload success or makes a failed slice retryable; `invalidate`
clears residency. Requests contain a generation token so a delayed GPU
completion from a destroyed/reinitialized resource cannot corrupt the
replacement mip state. `completeMip`, `finestResidentMip`,
`sampleable`, `fullyResident`, `pendingCount` and
`residentSliceCount` provide explicit observability.
`residentSampler` returns a validated `SamplerDesc` clamped to the
contiguous coarsest-to-finest mip range, and returns false until all
faces/layers of the coarsest level are loaded. Avoids sampling
undefined uninitialized GPU levels. It does not own GPU textures;
connect it to `TextureStreamQueue` results from Stage 7C.

**Safety/limits:** Only CPU metadata; no virtual page tables or GPU
feedback, no new RHI virtual methods, no independent I/O worker.
The renderer must not bind a texture for sampling if `sampleable` is
false. Stream queue and residency planner must be used from the
render/device thread. Existing ECS, saves/network and model materials
are unchanged.

**Validation:** GitHub source commit and branch update succeeded.
Native compiler/CTest/GPU tests **NOT RUN** because no repository
checkout is available in this environment. GitHub Actions for earlier
commits remained queued/cancelled; no green claim.

**Next:** Add a mock CPU test covering array face-complete readiness,
coarse-to-fine claim ordering, failures/retries, stale completion after
reset, 3D volume grouping and exact LOD clamp. Then document native
agent usage and optional integration with TextureStreamQueue's
completion callback. Keep code and handoff as separate commits.

## Stage 7E — mip residency and sampler LOD contract tests (2026-11-18)

**Code commit:** `e531f0bb38e2a4d61ea6398d018bb18d4d02aee2`.
**Branch:** `feature/texture-rhi-unification` (PR #21 Draft).
**Paths:** `tests/render/texture_mip_residency.cpp`, `CMakeLists.txt`.

**Test source:** New GPU-independent `texture_mip_residency` CTest target.
The fixture tests a six-face cubemap with four mips: claiming all
coarsest faces first, refusing sampling while one face is still
pending, clamping a sampler to the sole completely resident mip,
expanding sampler range only when all faces of the next mip complete,
making failed uploads retryable, refusing duplicate GPU completion,
and invalidating a coarse face to disable sampling again. Also
tests 3D volume grouping as one request per complete mip,
generation-safe rejection of late completion after reset,
invalid texture extents, single-mip fully-resident behavior
and sampler descriptor validation.

**Public API change:** None beyond the CPU-only
`TextureMipResidency` from Stage 7D. No ECS/save/network/render-path
changes. The test does not imply virtual texture GPU pages or
asynchronous device readback are implemented.

**Actual verification:** GitHub source/tree/commit/ref update
succeeded. Native C++ compilation and CTest **NOT RUN** due to
unavailable full checkout in this execution environment;
no Vulkan/Metal/GL GPU tests run and CI not confirmed green.

**Next:** Build `aiverse_texture_mip_residency_test` and run
`ctest --test-dir build/texture-rhi --output-on-failure
-R '^(texture_streaming|texture_mip_residency)$'`.
Then wire a native sample using `TextureStreamQueue.drain(callback)`
to `TextureMipResidency.complete` and update an independent
sampler only after complete mip/face residency. Test real GPU
LOD bounds, including stale resource destruction and reset.
Continue source/handoff commits separately.

## Stage 7F — native texture RHI skill and AGENTS.md registration (2026-11-18)

**Code/documentation commit:** `b122e213c894c63846a95fe895312ba89f2a9705`.
**Branch:** `feature/texture-rhi-unification`, Draft PR #21.
**Files:** `AGENTS.md`, new
`skills/visuals/aiverse-texture-rhi/SKILL.md`.

**Agent-facing deliverable:** Register one concise reusable native
C++ texture recipe in AGENTS.md so future FPS, RTS and other projects
choose existing `gfx::Device` resources, `gfx::RenderTargetSet`,
`gfx::TextureStreamQueue`, `gfx::TextureMipResidency` and
`render::parseKtx2`/`parseDdsTexture`/`uploadContainerTexture`
instead of rewriting texture backends or per-genre systems.
The skill includes API mapping, a one-mip layered stream
example, old model sampler2D compatibility, BC5 normal-Z
reconstruction and GPU compressed-format gates, resource
ownership, mip-ready LOD clamps and exact CMake/CTest
verification commands. It states clearly that streaming is
render-thread deferred submissions, not GPU async readback,
and that this PR's platform GPU verification is still pending.

**Compatibility:** Documentation-only changes; no source C++
implementation, ECS/save/network data, assets or runtime
behavior changed. Agent skill is explicitly scoped to native
project consumers and warns against using GPU resources in
deterministic ECS simulation.

**Actual validation:** GitHub code/tree/commit/ref update succeeded.
No C++ compile, GPU validation or CTest executed.
GitHub Actions have not been confirmed green; do not imply
production-readiness from the skill examples.

**Next:** Run the full CPU texture test matrix and GPU
format/readback tests on native targets. Continue remaining
virtual-texture page cache/feedback, native async readback,
BasisLZ/UASTC/OpenEXR integration and editor resource
inspection as separate bounded stages with handoffs.

## Stage 5O — synthetic OpenEXR CPU regression fixture and registration (2026-11-18)

**Source commits:** `41e18941d46769b2b7c51a76284e3176a4871a37` (fixture plus CMake registration), `0aed9de646aecb0e627846031c6e99e518054869` (explicit offset-table fixture fix).
**Branch:** `feature/texture-rhi-unification`; PR #21 remains Draft.
**Files:** new `tests/render/exr_texture.cpp`; `CMakeLists.txt`.

**Code:** Registered a standalone CPU-only `aiverse_exr_texture_test` / CTest `exr_texture`, constructed from synthetic EXR version-2 uncompressed scanlines, channels in non-RGB file order and mixed HALF/FLOAT samples. Includes two scanlines stored in reverse physical order, negative dataWindow origin, positive HDR values, negative floating HDR, default and explicit alpha, bad magic/version, compressed EXR refusal, truncated scanline, duplicate offset/physical row rejection. The test explicitly retains its generated offset table position, so reverse-ordered chunks cannot confuse test setup.

**API/architecture:** Exercise existing `engine/render/ExrTexture.h::parseOpenExr`; no ECS/game state, rendering pipeline, format expansion or native project dependency changed. No GPU fixture in this stage. These are authored test *sources*; execution is separate.

**Actual verification:** Source and fix commits written to GitHub. **CMake NOT RUN; C++ compilation NOT RUN; CTest NOT RUN; Metal/GL/Vulkan NOT RUN.** Do not treat the presence of a registered test as a successful test run.

**Run later:**
```sh
cmake -S . -B build/texture-rhi -DAIVERSE_BUILD_TESTS=ON
cmake --build build/texture-rhi --target aiverse_exr_texture_test
ctest --test-dir build/texture-rhi -R '^exr_texture$' --output-on-failure
```

**Next stage:** Connect `TextureStreamQueue` and `TextureMipResidency` into a safe genre-agnostic native streaming facade; test callback mapping and stale-token rejection, then add backend readback and editor inspection. Continue append-only per-stage handoffs with exact commit IDs and accurate verification status.

## Stage 7F — native staging + mip-residency coordination (2026-11-18)

**Code commit:** `948df1c88ecad6aa5294173f21db32cd9824d963`.
**Working branch:** `feature/texture-rhi-unification`, Draft PR #21.
**File:** new `engine/gfx/TextureResidentStreamer.h`.

**Implementation:** Header-only genre-independent `gfx::TextureResidentStreamer`
facade coordinates the prior `TextureStreamQueue` and
`TextureMipResidency` without introducing a new backend or genre policy.
`create(TextureDesc)` allocates empty authored-mip sampled storage and
creates residency metadata; `stageNext(TextureStreamId, FillFn)` claims
the next coarse-to-fine mip/face/volume, asks the caller for complete
tightly packed CPU bytes, checks integer/pitch/frame budgets, and
deep-copies through the existing queue. Failed producers or staging
rejections immediately unclaim the mip so it can be retried.

`drain(CompletionFn)` applies GPU completion outcomes to the correct
ticket+resource+generation before notifying callers; on a failed update
the underlying queue quarantines the resource and cancels later
updates, while the coordinator invalidates remaining ticket mappings
and refuses sampling. `cancel` makes a staged mip retryable;
`destroy` cancels all pending jobs before the backend handle is
released, and never recycles its logical resource identity.
`sampledTexture` and `residentSampler` fail closed until *every*
face/layer in the coarsest mip is complete; `finestResidentMip`
and `fullyResident` expose progress without exposing undefined
GPU mip data. `TextureStreamStats` exposes aggregate memory and
upload budgets.

**Native API:** Include `engine/gfx/TextureResidentStreamer.h`,
construct with `gfx::Device&`, create with explicit `mipLevels`,
stage packed data for the chosen mip/layer with `FillFn`, call
`drain` on the graphics thread, then use `sampledTexture` and
the `SamplerDesc` from `residentSampler` with the RHI's independent
sampler binding interface. GPU uploads occur during `drain`; no
separate file I/O worker, sparse page allocator, virtual texture
feedback, or automatic GPU readback is claimed.

**Files changed:** only the new header. Prior streaming and residency
headers are reused unchanged to avoid colliding with another agent.

**Tests actually performed:** GitHub file creation completed.
**C++ compile NOT RUN, CTest NOT RUN, GPU backends NOT RUN.**
A deterministic mock-device coordinator CTest and cross-backend LOD
sampling are needed before considering this feature verified.

**Limitations:** Current queue preallocates the full mip chain, so
streaming defers upload bandwidth and residency, **not** physical
VRAM allocations for missing mips. Eviction destroys whole
resources. Production sparse/virtual texture physical-page
budgeting and worker-driven I/O remain separate work.

**Next step:** Add a CPU-only coordinator test for mixed 2D arrays,
coarse-to-fine sampler clamp, failed producer retry, cancellation,
stale resource destruction, GPU upload failure, and exact per-frame
byte limits. Register it in CMake and append a new handoff with
actual test status. Later integrate native texture inspection/editor.

## Stage 7G — virtual texture CPU page cache with LRU and parent fallback (2026-11-18)

**Code commit:** `2d570cc233578fef48a9a3391b83826a657ba717`.
**Branch:** `feature/texture-rhi-unification`, Draft PR #21.
**File:** new `engine/gfx/VirtualTexturePages.h`.

**Implementation:** Source-only, genre-neutral `gfx::VirtualTexturePageCache`
maintains a bounded number of physical page slots (for a caller-owned
`Texture2DArray` atlas), maps virtual `{mip,x,y,layer}` keys onto
generational physical slice assignments, prioritizes unused/free slots
and deterministically evicts least-recently-used **resident/unpinned**
pages. Pending uploads are never evicted until explicitly canceled by
the caller; stale GPU completions are rejected by global slot generation
tokens even after cache clear/reinit. Optional pinned fallback pages,
`touch`, `pin`, `evict`, `complete`, `abandon` and
`resolveWithCoarseFallback` allow the renderer to choose an available
parent page in the SAME virtual layer; absent parents return invalid
and require an explicit fallback material. `atlasDescriptor` creates
a `Texture2DArray` descriptor with one physical slice per page; actual
GPU allocation, upload, page-table shader and VT feedback are the
host project's responsibility.

**Known edge to fix immediately:** Calling `reserve` again for an
already-pending key currently reports `needsUpload=true`; the next
source stage must make it false for duplicate in-flight requests to
avoid redundant writes to the same physical slot.

**Limits:** CPU mapping/index only, no GPU virtual texture feedback,
no shader indirection/page-table upload and no demand paging from disk.
It is a reusable *foundation*, not complete sparse virtual texturing.
The caller must cancel a pending `TextureStreamQueue` ticket before
`abandon` allows reuse of its physical slice. Rendered page textures
and physical atlas storage remain non-authoritative presentation data.

**Verification:** GitHub source commit successful; C++ compiler,
CTest, real GL/Metal/Vulkan GPU validation **NOT RUN** (full repo
checkout unavailable in this session). No passing CI asserted.

**Next:** Fix duplicate pending `needsUpload` semantics, then add
CPU tests for eviction, pinned roots, generation rejection, pending
upload non-eviction, correct coarser same-layer lookup and page
allocation bounds; each stage requires its own code/handoff commits.

## Stage 7H — virtual-texture LRU and pending-write safety tests (2026-11-18)

**Code commit:** `2b0c207234e75b3213db26e7b1503aba96f1f596`.
**Branch:** `feature/texture-rhi-unification`, Draft PR #21.
**Files:** `engine/gfx/VirtualTexturePages.h`,
new `tests/render/virtual_texture_pages.cpp`, `CMakeLists.txt`.

**Fix:** Repeated `reserve` on an already-pending virtual page
now returns `needsUpload=false` (and not-ready), preventing the
caller from queuing a second GPU write to the same physical
array slice. New allocations still return `needsUpload=true`.
Resident lookups return `ready=true`.

**Test source:** Register GPU-independent `virtual_texture_pages`
CTest. Synthetic tests cover valid physical `Texture2DArray`
atlas descriptors, pinned coarse root residency, in-flight
non-eviction, no duplicate pending upload, root fallback only
within the same virtual array layer, LRU replacement of ready
unpinned detail pages, stale generation rejection after eviction/
abandon/reinit, failure releasing physical slots, touch ordering,
mip geometry bounds and explicit invalid requests.
No GPU or shader-based VT test exists at this stage.

**Compatibility:** Opt-in CPU-only virtual page cache; unchanged
`gfx::Device`, gameplay/ECS/save and existing rendering.
This is a physical slice management foundation, **NOT**
a full virtual texture shader page-table/feedback pipeline.

**Verification:** GitHub source commit/branch update succeeded.
C++ compiler and CTest `virtual_texture_pages`: **NOT RUN**;
real Metal/GL/Vulkan GPU validation: **NOT RUN**.
Latest CI status has not been confirmed passing.

**Next:** Run
`cmake --build build/texture-rhi --target aiverse_virtual_texture_pages_test`
and `ctest --test-dir build/texture-rhi --output-on-failure
-R '^virtual_texture_pages$'`.
Then integrate a texture array atlas + page-table lookup in a
native sample, stage uploads through TextureStreamQueue and
release a slot only after cancellation or GPU completion.
Continue separate code and handoff commits per stage.

## Stage 7G — mip residency streaming coordinator mock tests (2026-11-18)

**Code commit:** `cc5ff6852ae7c72a7df8b9752be95df359d5cc08`.
**Branch:** `feature/texture-rhi-unification`, PR #21 still Draft.
**Files:** `tests/render/texture_resident_streamer.cpp`, `CMakeLists.txt`.

**Test source and API coverage:** New GPU-independent CTest target
`aiverse_texture_resident_streamer_test` / `texture_resident_streamer`
exercises the Stage 7F facade against a strict mock
`gfx::Device`. It covers 2D array coarse-to-fine mip/face
sequencing, unavailable sampling while a coarse face is
missing, callback delivery after GPU completion, per-frame
byte/upload limits, LOD sampler clamps on complete mip
ranges, entire 3D volume mip uploads, packed source ownership
with no source-pointer lifetime dependency, invalid producer
bytes, rejected/throw-free producer retry, ticket cancellation
and replay, destroy/recreate with non-recycled logical
resources, full GPU shutdown, and quarantine after failed
GPU updates. Assertions inspect the mock's submitted
mip/layer coordinates and pixel values.

**Implementation:** Test code only; no changes to existing render
paths or ECS/network state. The production facade remains
`engine/gfx/TextureResidentStreamer.h`.

**Tests actually performed:** GitHub test and CMake commit completed.
**CMake configure NOT RUN, C++ compile NOT RUN, CTest NOT RUN,
Metal/GL/Vulkan GPU sampling NOT RUN.** The test was prepared
for a later native checkout as requested, not reported passing.

**Native verification commands:**
```sh
cmake -S . -B build/texture-rhi -DAIVERSE_BUILD_TESTS=ON
cmake --build build/texture-rhi --target aiverse_texture_resident_streamer_test
ctest --test-dir build/texture-rhi --output-on-failure -R '^texture_resident_streamer$'
```

**Next:** Document new native API and adapt AI skill entry points,
then implement bounded image readback interfaces/backends and
editor diagnostics; realistic sparse/virtual physical-page
streaming is still a separate unimplemented subsystem.
Keep source/handoff commits separate and update tests status.

## Stage 8A — native texture API documentation and AI skill routing (2026-11-18)

**Code/docs commit:** `3627d46f70694e26ec43ad9437bd13c76f09ac3c`.
**Branch/PR:** `feature/texture-rhi-unification`, Draft PR #21.
**Paths:** `skills/visuals/aiverse-texture-rhi/SKILL.md`,
`docs/texture-rhi.md`, `AGENTS.md`.

**What changed:** The existing texture RHI agent skill now covers the
new `gfx::TextureResidentStreamer` facade as the preferred
native-project entry point for staged mip uploads, rather than asking
agents to reinvent manual ticket→mip completion tracking.
A compact standalone native C++ example demonstrates
`create` → `stageNext` → `drain` → checked
`sampledTexture`/LOD-clamped `residentSampler` → `destroy`.
The skill and `AGENTS.md` explicitly point native developers to
the Radiance RGBE and restricted uncompressed OpenEXR importers,
and warn against treating the GPU cache as ECS-authoritative.
`docs/texture-rhi.md` now distinguishes source-level mixed-format
MRT/MSAA from GPU certification, documents streaming ownership,
CPU GPU-byte budgets versus actual virtual texturing, BC6H mappings,
HDR/EXR limitations, and updates the grouped texture CTest commands.

**What did not change:** No engine ABI, actual sampler/renderer
bindings, asset format, save game, ECS or backend driver code was
changed by this stage. Native game integration is an API recipe,
not evidence of running a game or verifying an importer.

**Verification:** GitHub docs/tree commit succeeded. **CMake,
C++ build, CTest, GPU, README translation check and skill
catalog check NOT RUN**. No passing CI status asserted.

**Next exact step:** Implement readback contracts and native
backend extensions (with CPU+GPU fixtures), or integrate
resident streaming/inspection with the editor. After the
code commit, append a separate handoff with exact executed
verification status. Do not merge PR #21 while native build
and platform GPU tests are unverified.

## Stage 7H — explicit async texture readback RHI contract (2026-11-18)

**Code commit:** `5dc72d0b4288e0081454b3ff87543b2335115f6e`.
**Branch/PR:** `feature/texture-rhi-unification`, PR #21 Draft.
**File:** `engine/gfx/Gfx.h`.

**API:** New `TextureReadbackDesc { mipLevel, arrayLayer }`,
`TextureReadbackResult { width, height, PixelFormat format,
std::vector<uint8_t> pixels, success() }`, and optional
`Device::readTextureAsync(TextureHandle, TextureReadbackDesc,
TextureReadFn)`. The base implementation refuses unsupported
operation (`false`), rather than silently taking a screenshot or
modifying the request. Accepted requests must deliver tightly
packed top-left rows of the *actual* texture format at a later
`beginFrame` on the owner thread; GPU driver/completion threads
must never invoke user callbacks. A failed queued operation
returns an empty failure result. Staging lifetime is independent
from subsequent destruction of the parent texture.

**Compatibility:** Existing RHI implementers keep their old
behavior through the new default virtual method; no
existing texture upload/scene/ECS behavior is modified. This stage
only adds the public contract; it does **not** implement readback
on Metal, Vulkan, GL33 or GLES3.

**Actual verification:** GitHub commit completed. C++ compilation
**NOT RUN**, CTest **NOT RUN**, native GPU **NOT RUN**.
No green CI status claimed.

**Next step:** Implement Metal blit-to-shared-buffer readback for
RGBA8/RGBA16F (including BGRA render-target swizzle), with
callback dispatch at later `beginFrame`; then implement a
GL/GLES3 and Vulkan backend where portable. Add lifecycle
and invalid-subresource tests in distinct code/handoff stages.

## Stage 7I — virtual texture page-table CPU encoding and bounded GPU upload (2026-11-18)

**Code commit:** `750313df432b011d4df9dc375b8d1e6bc78839f6`.
**Branch:** `feature/texture-rhi-unification` (Draft PR #21).
**Files:** new `engine/gfx/VirtualTexturePageTable.h`,
`engine/gfx/VirtualTexturePages.h` (metadata getters).

**Implementation:** `VirtualTexturePageTable` converts the CPU page
cache into an RGBA8 mip-zero page-table texture, one 2DArray layer per
logical virtual texture layer. RGBA8 entry encodes physical atlas
slice (16-bit split into R/G), resident fallback mip (B), and valid
mask (A=255). For a missing detail tile, `rebuild` searches
resident parents only within the same virtual layer; no valid
parent emits an invalid table entry. Changes mark only affected
CPU rows dirty. `textureDescriptor` and `createGpuTexture`
allocate a suitable nearest-filtered Texture2DArray with all table
bytes. `flush(Device&,TextureHandle,maxBytes)` submits contiguous
dirty row groups under the requested byte budget via
`updateTextureRegion`, preserves rows on GPU failure for retry
and exposes read-only `entry`/dirty stats for editor diagnostics.
No GPU table is owned by this helper; caller destroys its handle.

**Scope:** CPU page table and render-thread uploads only.
There is not yet a GLSL/MSL virtual page-table sampler,
GPU feedback/readback, sparse hardware texture binding or
disk worker. VirtualPageCache physical atlas must be created
and uploaded independently (e.g. TextureStreamQueue). This
stage does not modify deterministic ECS, saves or netcode.

**Static review follow-up:** `flush` currently limits chunks
by byte budget but must also cap chunk size to
`INT_MAX / (width * 4)` because `TextureUploadDesc.slicePitchBytes`
is an `int`. Add this before native compilation and test it.
The platform runtime must ensure no sampling of page-table
rows until their GPU uploads finish.

**Verification:** GitHub source/tree/branch updates succeeded.
C++ build, CTest and Metal/GL/Vulkan page sampling:
**NOT RUN** (container lacks GitHub repo checkout and network clone).
No passing CI assertion.

**Next:** Fix int slicePitch limit, add GPU-independent
`virtual_texture_page_table` tests using a mock `gfx::Device`,
exercise mapped RGBA bytes, unchanged dirty rows, bounded
successive uploads, GPU failure retry and wrong-layer fallback.
Then create GLSL/MSL sampling helper and validate actual GPU
2DArray texture lookup.

## Stage 7I — Metal asynchronous texture mip/layer GPU readback (2026-11-18)

**Code commit:** `128ecb508a5f06240f6c1b59dfc140035af15a8c`.
**Branch:** `feature/texture-rhi-unification` (Draft PR #21).
**File:** `engine/gfx/metal/GfxMetal.mm`.

**Implementation/API:** Implements the previously optional
`Device::readTextureAsync(TextureHandle,TextureReadbackDesc,TextureReadFn)`
on Metal for sampled/rendered, single-sample 2D, 2D-array and cube
(subresource mip/face selected). Supports source RGBA8, sRGB RGBA8
and RGBA16F. Refuses depth, MSAA, 1D/3D and unsupported formats.
A dedicated `MTLBlitCommandEncoder` records a nonblocking
texture-to-shared-`MTLBuffer` transfer with 256-byte-aligned GPU
row pitch. A retained completion record copies rows to packed
CPU bytes after GPU completion and swizzles Metal BGRA render-target
storage to the portable RGBA result. The MTL completion handler
signals `atomic ready` but does not call user code. The next
`beginFrame` on the device owner thread invokes ready callbacks;
failure produces an empty `TextureReadbackResult`.
Returned source data outlives the parent texture handle because
the command buffer captures its staging target. Requests are
bounded to at most 32 in flight and 256 MiB staging each.
Other backend implementations remain fail-closed.

**Compatibility:** No scene rendering, capture, texture allocation,
ECS, network/save or material-layout changes; app code must check
the boolean return and `TextureReadbackResult::success()`.
Device must outlive callbacks. The API does not promise
automatic CPU orientation correction for custom shader UV
conventions, only packed top-left texture rows.

**Actual verification:** Connector committed source successfully.
**macOS/iOS compiler NOT RUN; Metal GPU capture NOT RUN;
CTest NOT RUN; UI integration NOT RUN.**
No passing status asserted. Real Metal validation is required
for aligned row copies, cubemap face indexing, BGRA swizzle,
HDR byte count, after-texture-destroy lifetime and
callback delivery order.

**Next:** Implement portable GL/GLES3 readback (with clear
sync/deferred semantics) and Vulkan staging/fence readback,
then add runtime GPU tests and editor inspection hooks.
Maintain separate code + handoff commits per stage.

## Stage 7J — virtual page-table RHI dirty-row tests and integer pitch bound (2026-11-18)

**Code commit:** `b444c4955b8accf789dcb9be573fdc5a23abda0e`.
**Branch:** `feature/texture-rhi-unification`, Draft PR #21.
**Files:** `engine/gfx/VirtualTexturePageTable.h`,
new `tests/render/virtual_texture_page_table.cpp`, `CMakeLists.txt`.

**Fix:** `VirtualTexturePageTable::flush` now clamps the upload
batch's row count to `INT_MAX / rowPitchBytes` in addition to
the caller's byte budget. This prevents signed overflow of
`TextureUploadDesc.slicePitchBytes` for large page-table uploads
before calling the GPU backend. Rows remain dirty on a failed
GPU subresource update for a later retry.

**Test source:** New `virtual_texture_page_table` CTest uses a mock
gfx::Device and physical page cache. Validates nearest-filtered
RGBA8 Texture2DArray creation with initially invalid entries,
parent-mip physical page lookup encoding, separate virtual layers,
full dirty rows only after cache changes, bounded per-frame row
uploads, rejection of budgets smaller than one row, preservation of
dirty rows on GPU failure and retry, single-row dirtiness after
one detail tile is resident, fallback invalidation after eviction,
and mismatched virtual dimensions/invalid inspection. No actual
GPU shader lookup or feedback test exists yet.

**Compatibility:** Only new opt-in CPU helpers/tests; no changes
to existing render passes, ECS, saves, networking or asset files.
The GPU page table is owned by its caller and must be destroyed
when the hosting renderer shuts down.

**Validation:** GitHub code commit/update succeeded. C++ compile,
CTest, Metal/GL/Vulkan GPU validation **NOT RUN** (no local checkout).
Do not claim page-table GPU fidelity without a native shader fixture.

**Next:** Provide portable GLSL/MSL virtual texture sampling helpers,
including physical atlas layer/mip decode and tile border addressing.
Then run `aiverse_virtual_texture_page_table_test`, add a real
shader capture that looks up page table rows, and complete
native readback/feedback when supported. Continue separate
code/handoff commits.

## Stage 7J — GL33/GLES3 nonblocking texture readback via PBO+fence (2026-11-18)

**Code commit:** `0b975df615d7de9042d7e3bb3d5e8ff98caaf16c`.
**Branch/PR:** `feature/texture-rhi-unification`, Draft PR #21.
**Files:** `engine/gfx/gl/GLApi.h`, `engine/gfx/gl/GfxGL.cpp`.

**Implementation:** Added a GL33/GLES3 override for
`readTextureAsync` supporting single-sample RGBA8/sRGB_RGBA8
2D, 2D-array and cubemap faces at a selected mip. It builds a
temporary framebuffer, attaches the exact selected mip/array
layer/cube face, queues nonblocking `glReadPixels` into a
`GL_PIXEL_PACK_BUFFER`, then inserts a GPU `GLsync` fence.
`beginFrame` polls `glClientWaitSync(timeout=0)`, maps only
signaled PBOs, copies/flips bottom-up framebuffer data into packed
top-left RGBA8 rows, deletes completed fences/PBOs, and invokes
app callbacks afterward on the GL context owner thread.
Device destructor releases in-flight resources. Up to 32
readbacks, 256 MiB each, are allowed. Unsupported formats,
MSAA, depth, nested passes and invalid subresources return
false, without invoking callbacks. The WebGL2 build returns
false explicitly because portable native glMapBufferRange
readback cannot be promised there. GL loader adds the needed
PBO mapping/fence functions and Windows GL33 constants.

**Compatibility:** Existing `captureNextFrame` path is unaffected;
no ECS/simulation/serialization changes. Asynchronous completion
is not a blocking `glFinish`; callbacks need future frames
and an active context. On an accepted GPU failure the callback
receives an empty `TextureReadbackResult`.

**Actual verification:** Source committed to GitHub.
**C++ compilation NOT RUN, CTest NOT RUN, GL33/GLES3/Android GPU
test NOT RUN, WebGL2 build NOT RUN**. The pending tests must
check GL entrypoint availability, framebuffer completeness,
row orientation, destroyed source lifetime, poll behavior,
signal/failure cleanup and no callback on rejected input.

**Next:** Implement a Vulkan image-to-host-staging readback with
frame-fence ownership, then add portable GPU fixtures and editor
image preview/readback plumbing. Continue source commit +
append-only handoff on every bounded stage.

## Stage 7K — reusable GLSL and Metal virtual texture lookup shader helpers (2026-11-18)

**Code commit:** `1f65dee131573feebdd4f6569676a9ce47f06510`.
**Branch:** `feature/texture-rhi-unification` (Draft PR #21).
**New file:** `engine/gfx/VirtualTextureSampling.h`.

**Implementation:** Native C++ header provides GLSL ES3/GL330 and
Metal Shading Language source snippets via `kVirtualTextureGLSL`
and `kVirtualTextureMSL`. Both decode the same RGBA8 page-table
ABI produced by Stage 7I: R/G little-endian 16-bit physical atlas
slice, B coarse fallback mip and A valid flag. Functions look up
the nearest sampled virtual mip-zero tile in a Texture2DArray
page table, resolve the physical Texture2DArray slice, compute
the local UV within an ancestor page and clamp to texel centers
with caller-supplied gutter/border pixels. Missing or out-of-range
entries return a caller-controlled fallback color rather than
sampling an undefined GPU slice. The snippets contain no shader
version directive so the engine backend can prepend its own.

**Capabilities/limits:** These are **shader source snippets only**,
not new graphics pipeline builders or validated shaders.
The game/material compiler must concatenate/bind the snippets
and bind an RGBA8 nearest page table plus physical array atlas,
each on suitable texture slots. The physical atlas requires
appropriately padded gutters to prevent edge seams. CPU
`VirtualTexturePageTable::flush` must complete before sampling
updated table rows. GPU feedback, readback, actual page fault I/O,
hardware sparse residency and VT mip-tail handling are still
outstanding.

**Compatibility:** New optional source header; no existing shader
materials or render passes changed. Does not affect native ECS,
saves/network or project asset schemas.

**Verification:** A first GitHub multi-call tree/commit attempt failed
with a transient GraphQL API error and left no verified source file.
Recovered by checking the branch and successfully creating the new
file with GitHub's single-file contents API. Final code commit above
is verified. GLSL/MSL compilation and real GPU capture: **NOT RUN**.
No passing CI result claimed.

**Next:** Build a native Vulkan/GL and Metal sampling test that
constructs a physical 2DArray atlas, uploads a CPU page-table
entry, runs the shader helper and checks fallback/mapped pixel
colors. Compile C++ CPU tests and record actual outcomes.
Continue stages for GPU feedback/readback and budgeted page uploads.

## Stage 7K — Vulkan async texture readback with frame-fence staging (2026-11-18)

**Code commit:** `5d8d6da68932f8d1f22e59c0b1c1f5f43ef7d353`.
**Branch:** `feature/texture-rhi-unification` (Draft PR #21).
**Files:** `engine/gfx/vulkan/VkDevice.h`,
`engine/gfx/vulkan/VkResources.cpp`,
`engine/gfx/vulkan/GfxVulkan.cpp`.

**Implementation/API:** Implements `Device::readTextureAsync` for
single-sample, color RGBA8, RGBA8_sRGB and RGBA16F 2D/2DArray/Cube/
CubeArray mip/face subresources. An accepted request allocates a
host-visible+coherent `VK_BUFFER_USAGE_TRANSFER_DST_BIT` staging
buffer, transitions the selected mip's array-layer range to
`TRANSFER_SRC_OPTIMAL`, issues `vkCmdCopyImageToBuffer`
with one requested layer and a tight row pitch, then restores
the image to `SHADER_READ_ONLY_OPTIMAL`. The staging buffer
and `TextureReadFn` remain owned by a pending request
associated with the current `serial_`.

`prepareSlot` already waits a Vulkan fence and updates
`completed_`. At a later `beginFrame`, `deliverTextureReads`
copies only fence-completed buffers to packed CPU result vectors,
releases staging buffers/allocations, and invokes user callbacks
on the owner thread **after** collection. GPU device shutdown
frees all remaining staging buffers after `vkDeviceWaitIdle`.
A destroyed source texture remains alive until the frame serial
completes via the existing deferred texture destruction policy.
Unsupported depth/MSAA/3D/compressed textures and invalid mip/layer
requests return false without callbacks.

**Compatibility:** Existing screenshot/backbuffer capture path
stays unchanged. This source stage does not add sparse/virtual page
allocation or GPU texture query UI. Vulkan packed row orientation
follows the image subresource texel order; no display-specific
Y-flip or tone mapping is applied to raw texture data.

**Actual verification:** Source commit to GitHub succeeded.
**Vulkan C++ build NOT RUN, CTest NOT RUN, validation layers and
GPU readback NOT RUN**; no driver correctness claims.
Check source-texture destruction, frame-slot recycling, sRGB/HDR
byte sizes, depth rejection, failed staging allocation, and
callback delivery on the render owner thread under a real Vulkan
device or Lavapipe.

**Next:** Add tests using Metal, GL33, GLES3 and Vulkan devices
for the new RHI readback contract. Integrate editor texture
preview/diagnostics and optional virtual texturing page cache
only after native compile and format validation.

## Stage 8A — GPU texture inspection contract and logical footprint estimates (2026-11-18)

**Code commit:** `065678454bd968c7e8a54043122bc36bc0f5c39c`.
**Branch:** `feature/texture-rhi-unification`, Draft PR #21.
**Paths:** `engine/gfx/Gfx.h`, `tests/render/texture_contract.cpp`.

**API/source implementation:** New `TextureResourceInfo` describes a live
user-created texture handle, dimension, pixel format, extent, array layers,
mip levels, sample count, render-target/storage usage, and approximate size
in bytes. `estimateTextureFootprint(TextureDesc)` estimates GPU resource
footprint from logical block sizes, mip extents, layers/volume slices and
samples, with arithmetic overflow checks; it is NOT a driver memory query.
The optional `Device::textureResources() const` API safely returns an
empty list until a backend overrides it. A CPU-only `texture_contract`
fixture checks RGBA8 arrays with mips, compressed BC7 cube, multisample
depth, typed half-float and a huge overflow request.

**Compatibility:** All existing Device implementations compile in principle
because inspection is optional with a default virtual implementation; no
change to existing shader, frame, ECS, save/network or on-disk data. Backend
enumeration and the actual editor widget are NOT implemented in this stage.

**Actual verification:** GitHub source/tree/commit/ref update succeeded.
C++ compilation, CTest, editor and GPU execution: **NOT RUN** (this
execution environment has no native checked-out repository).
GitHub Actions completion not confirmed. The size figures are estimates,
not measured allocations or driver-reported memory.

**Next:** Implement actual Metal/GL/Vulkan `textureResources()`
over their live texture pools without exposing swapchain/dummy internal
images. Include correctly scaled 3D mip depth, cube-array faces and MSAA
samples, and record the code commit followed by a separate handoff. Then
add an ImGui texture-resource view in the editor and native regression tests.

## Stage 8B — backend live GPU texture enumeration (2026-11-18)

**Code commit:** `be7f180d627655ac3d30059ff03ac619e55bb5a8`.
**Branch:** `feature/texture-rhi-unification`, Draft PR #21.
**Paths:** `engine/gfx/Gfx.h`, `engine/gfx/gl/GfxGL.cpp`,
`engine/gfx/metal/GfxMetal.mm`, `engine/gfx/vulkan/VkDevice.h`,
`engine/gfx/vulkan/VkResources.cpp`.

**Source implementation:** Metal/GL/Vulkan `Device::textureResources()`
overrides now enumerate only user-created, currently alive resources
from the corresponding backend texture pool (excluding swapchain,
internal dummy images, samplers/views). The returned typed entries
cover handle, texture dimension, pixel format, width/height/depth,
array and cube face counts, mip levels, sample count, attachment/storage
usage and a conservative estimated byte footprint via Stage 8A.
Pool gains a read-only `each`/`eachWithId` overload; existing mutable
iteration behavior is unchanged. `Gfx.h` now explicitly includes
`<algorithm>` for its new `std::max` footprint calculation.
No GPU synchronous readback or driver allocation query is used.

**Compatibility:** Inspection is opt-in, read-only and requires the
graphics owner thread. No existing rendering behavior changes,
save/network/ECS changes or new external dependencies.

**Limits:** Estimated bytes are *not* physical VRAM residency,
texture memory allocator footprint or shared texture accounting.
Editor GUI is not wired yet. As with existing pool-based handles,
only query and act on returned entries on the device owner thread;
do not treat a stale handle ID as permanent identity after deletion.

**Actual verification:** GitHub code/tree/commit/ref update succeeded.
C++/Objective-C++ compile, CTest, editor UI and live GPU resource
inspection: **NOT RUN**; CI passing result not confirmed.
Potential Metal SDK type availability and Vulkan linkage must be
checked by building native targets.

**Next:** Integrate a read-only ImGui editor texture inspector
with optional request to `gfx::Device::textureResources()`,
sorting/filtering by dimension/format and estimated bytes. Build
and run the editor as well as `texture_contract` CTest and
cross-backend resource allocation regression tests. Commit
implementation and handoff separately.

## Stage 7L — virtual texture physical page upload/publish coordinator (2026-11-18)

**Code commit:** `4064825167f76c9d8225c5da2b207c8671e5e083`.
**Branch/PR:** `feature/texture-rhi-unification`, Draft PR #21.
**File:** new `engine/gfx/VirtualTextureStreamer.h`.

**Implementation/API:** New native, genre-independent
`gfx::VirtualTextureStreamer` composes the existing
`VirtualTexturePageCache`, `VirtualTexturePageTable` and
`TextureStreamQueue`. `initialize` allocates a fixed-budget
Texture2DArray physical atlas and RGBA8 Texture2DArray page table.
`stage(VirtualPageKey,packedBytes,pin)` reserves a page or reuses
the deterministic LRU physical slot, **invalidates stale table
entries on the GPU before overwriting evicted physical slices**,
and deep-copies a complete physical page through the staging
queue. `drain` publishes a virtual page in the GPU table only
after successful GPU upload completion, rebuilds nearest parent-mip
fallback mappings, then dispatches app callbacks after synchronizing
the table. `cancel`, `evict`, `pin`, `touch`,
`physicalAtlas`, `pageTableTexture`, `resolve`,
`encodedPageTable`, `pageStats` and `streamStats` expose
lifetime, diagnostics and resource access. If an invalidating
page-table upload fails, the entire facade becomes non-sampleable
rather than risking an old virtual coordinate reading overwritten
physical texels. Source pixels must include the intended gutter.

**Architecture:** Fixed-count 2DArray atlas bounds physical GPU
memory while the logical virtual texture may contain many pages.
The 16-bit physical slot encoding and mip-fallback page-table
layout are from prior Stage 7H–7K virtual texture work and the
existing GLSL/MSL shader helpers. Graphics device owns backend
work; this facade owns its physical texture handles and must be
destroyed before `gfx::Device`. No genre-specific ECS, save
format or network changes. No runtime disk worker, automatic
feedback pass, disk paging, BC page transcoding or sparse API
allocation yet.

**Tests actually performed:** New header committed to GitHub.
**C++ compile NOT RUN; CTest NOT RUN; native GPU shader/render
NOT RUN.** This source-only stage is not production certified.

**Next:** Add a mock-device fixture verifying budgeted page
publishing, LRU+pinning, parent fallbacks, cancellation, staged
source ownership and upload failure quarantine. Register it in
CTest, then integrate virtual texture preview/telemetry into
editor and real GPU scenes. Continue separate code/handoff commits.

## Stage 8C — dockable GPU texture-resource inspector in editor (2026-11-18)

**Implementation commit:** `2408ed1accde2363b69141198531c9a3cc8f585b`.
**Branch:** `feature/texture-rhi-unification`, Draft PR #21.
**New file:** `apps/editor/windows/TextureResourcesWindow.cpp`.

**Source implementation:** Register `TextureResourcesWindow` as a normal
dockable AIVERSE editor window under menu group `look` (Графика),
closed by default. It accesses the editor's existing `device()`
and calls the new Metal/GL/Vulkan `textureResources()` snapshot to
display live user-created texture handles, dimensionality, pixel
format, texture extent/depth/layer count, mip levels, sample count,
render-target/MSAA counts and total estimated GPU resource bytes.
Includes ID/format/dimension text filter, render-target-only filter,
and sorted-by-estimated-bytes or stable-handle ordering. Uses the
existing editor WindowRegistry macro; the CMake editor target
automatically includes `apps/editor/windows/*.cpp`, so no direct
CMake edit is necessary. It does not read back image pixels or
change texture resource state.

**Correctness/compatibility:** The inspector is read-only, optional
and does not alter FPS/RTS components or ECS authority, network
or save data. Estimates exclude driver allocation overhead,
swapchain and internal textures; a row's handle is ephemeral.
No cached persistent GPU state is kept by the inspector.

**Verification:** GitHub source commit confirmed. macOS/Windows editor
build, interactive ImGui window, GL/Vulkan/Metal live texture enumeration,
CTest: **NOT RUN** (no local native checkout). GitHub Actions completion
not confirmed. The window must be tested under the editor's actual
ImGui version before describing it as functional.

**Next:** Build `aiverse_editor` for macOS/Windows and inspect
GPU texture allocations while loading multiple 2D/array resources.
Check new window menu registration, memory estimates, filter and
lifecycle, then run the texture-focused CTests. If the build is clean,
continue native async readback/GPU validation and production importer
integrations. Record exact results in a new handoff.

## Stage 7M — virtual texture streaming coordinator mock tests (2026-11-18)

**Code commit:** `88ba89f5dc89369688ef7ed170e529894f26f97a`.
**Branch:** `feature/texture-rhi-unification` (Draft PR #21).
**Files:** `tests/render/virtual_texture_streamer.cpp` and
`CMakeLists.txt`.

**Test source:** Registered `aiverse_virtual_texture_streamer_test` /
CTest `virtual_texture_streamer` under the existing
`AIVERSE_BUILD_TESTS AND NOT EMSCRIPTEN` gate. Synthetic mock GPU
checks physical atlas + table allocation, coarsest parent fallback
publication only after GPU upload completion, sample invalidation
before an evicted physical slot can be overwritten, deterministic
LRU eviction with a pinned intermediate page, fine-over-coarse
page-table replacement, explicit eviction and safety of pinned
pages, cancellation and re-staging, refusal of truncated physical
page bytes, GPU upload failure quarantine, callbacks and complete
ownership cleanup. The fixture uses no external image, GPU or
virtual texture asset.

**Implementation:** Test-only source and CMake registration.
Production runtime code remains the Stage 7L
`engine/gfx/VirtualTextureStreamer.h`, and the prior page
cache/table/GLSL/MSL helpers are reused unchanged.

**Actual verification:** GitHub test commit succeeded.
**C++ compilation NOT RUN, CTest NOT RUN, GPU test NOT RUN.**
The fixture's own correctness is still subject to compilation
and execution; no fabricated PASS result.

**Verification later:**
```sh
cmake -S . -B build/texture-rhi -DAIVERSE_BUILD_TESTS=ON
cmake --build build/texture-rhi --target aiverse_virtual_texture_streamer_test
ctest --test-dir build/texture-rhi --output-on-failure -R '^virtual_texture_streamer$'
```

**Next:** Document the unified virtual texture physical atlas
API in the native AI skill. Add editor resource telemetry showing
the public page cache stats and page-table entries. GPU texture
readback tests, complete OpenEXR/Basis transcoding, rendering
integration and platform builds are still outstanding.

## Stage 8D — read-only selection and basic 2D GPU thumbnail preview (2026-11-18)

**Code commit:** `b4af34c7f4d7c437d036c9b4ffd4202df7b7353e`.
**Branch:** `feature/texture-rhi-unification`, Draft PR #21.
**Path:** `apps/editor/windows/TextureResourcesWindow.cpp`.

**Implementation:** Editor GPU texture resource window now supports
click-to-select a live resource handle and a read-only ImGui preview
for ordinary single-sampled, uncompressed, non-depth Texture2D
resources. It reuses the same ImGui texture-handle binding and
render-target vertical orientation convention as the existing
`Viewport::image`, without allocating/destroying GPU textures
or modifying their contents. Selection is invalidated when its
resource is no longer present in the live inventory. Array,
cubemap, multisample and depth resources show an explicit
"readback required" message instead of incorrectly binding
them as sampler2D.

**Compatibility:** No changes to `gfx::Device`, ECS/gameplay,
saves/network or material shaders. The optional GPU editor
inspector remains closed by default.

**Limitations:** The preview is a native ImGui texture binding,
not a GPU readback or per-mip/layer color inspection. Filtered
float/typed 2D formats can still differ by sampler support;
actual GPU validation and backend-specific ImGui bindings
remain outstanding.

**Verification:** GitHub commit confirmed. Native C++/ImGui
compilation, editor launch and per-backend rendering:
**NOT RUN**. Latest CI not confirmed green.

**Next:** Build the editor, validate the texture window under
GL33, Metal and Vulkan. Add readback-based slice/face preview
for arrays and depth only if the backend supports the required
TextureReadbackDesc, and never block the render thread.
Continue code and handoff commits separately.

## Stage 8B — virtual pages/readback agent recipes and correct CTest command (2026-11-18)

**Documentation/skill commits:** `4eec698d1dfb1c26132714f0fa4a6143d7a41919`
(AI texture skill, including a corrected CMake multi-target command),
`8e24b098b8da377b722f83efed58c05f5efe0eb6` (canonical
`docs/texture-rhi.md` source-status and CTest matrix), and
`21ec39c179e468c5e9726655310cc5fc1586df87`
(`AGENTS.md` routing to `gfx::VirtualTextureStreamer` and
`Device::readTextureAsync`).
**Branch:** `feature/texture-rhi-unification` (PR #21 Draft).
**Files:** `skills/visuals/aiverse-texture-rhi/SKILL.md`,
`docs/texture-rhi.md`, `AGENTS.md`.

**Changes:** Native games and other agents are directed to reuse
the fixed-capacity physical virtual-texture atlas, page-table and
LRU coordinator, and the separate GPU readback contract rather
than generating per-genre texture APIs. Documents explain
driver/backend support, failed-resource quarantine, page table
publication ordering, color formats and limitations (no
hardware sparse textures, GPU feedback, full EXR/Basis transcoding,
or threaded disk workers). Added `virtual_texture_streamer`,
`virtual_texture_pages`, `virtual_texture_page_table`
and the previously added EXR/HDR/streaming fixture targets to
the CMake/CTest instructions. Fixed a broken line continuation
in the prior texture skill test command.

**Compatibility:** Documentation only; no production C++
source, ABI, ECS, saves or network code changed in this
documentation stage. C++ source stages 7H–7M remain unverified.

**Verification:** Documentation files updated in GitHub.
**CMake build NOT RUN, CTest NOT RUN, GPU tests NOT RUN,
skill-catalog audit NOT RUN, full run-tests.sh NOT RUN.**
Do not claim any passing checks or merge PR #21.

**Next:** Finish the editor texture resource inspector presentation,
OpenEXR/BasisU production transcoding policy, shader storage
image support and mobile/web compatibility. After the user-requested
implementation pass, run `./run-tests.sh --quick`, then full
`./run-tests.sh` and real Metal/GL/Vulkan GPU captures; fix failures
and record exact tested HEAD and outcomes.

## Stage 8E — refresh architecture docs and repair duplicated test commands (2026-11-18)

**Documentation commit:** `1d1f790a7e7a85f0db607fcaaf3da816ab8cc35b`.
**Branch:** `feature/texture-rhi-unification`, Draft PR #21.
**Path:** `docs/texture-rhi.md`.

**Documentation work:** Replace stale statements suggesting virtual
page tables/physical atlas streaming are wholly absent: the CPU-backed
`VirtualTextureStreamer`, fixed 2DArray physical atlas, GPU-uploadable
page table, LRU and GLSL/MSL snippets now exist in source. Clarify that
true hardware sparse textures, GPU-feedback-driven requests, disk paging
and native shader validation are NOT implemented or verified. Document
the new `Device::textureResources()` read-only snapshot and optional
editor **Графика → GPU Текстуры** table/2D thumbnail with byte estimates
distinct from driver-reported VRAM. Fix an earlier duplicated/broken
Markdown fenced validation block by replacing it with a single coherent
CMake/CTest command including the texture and virtual texture CPU tests
and Vulkan native GPU validation instructions.

**Compatibility:** Documentation-only stage, no C++ runtime changes.
No FPS/RTS ECS, networking, saves or existing renderer API modifications.

**Actual validation:** GitHub document update committed.
Native C++ build, CTest and live GPU/editor validation: **NOT RUN**.
No passing CI claimed.

**Next:** Perform native builds and texture/VT CTests; correct actual
failures. Add real hardware shader capture for page-table/physical
atlas sampling and backend readback. Ensure CI completes before merging.

## Stage 8F — Vulkan live resource enumeration regression fixture (2026-11-18)

**Code commit:** `36667df7470167ab07285e2e6b207e79f4754419`.
**Branch:** `feature/texture-rhi-unification`, Draft PR #21.
**Path:** `tests/render/vulkan_tests.cpp`.

**Source:** In the existing `vulkan_backend` integration fixture,
create an 8×8 RGBA8 `Texture2DArray` with two physical layers and
four explicitly allocated mips. Assert that `Device::textureResources()`
reports the live user texture with the correct type/format/extent/
layers/mips/sample count and an estimated footprint of 680 bytes;
check renderTarget/storage flags remain false. Destroy that handle
and assert the resource no longer appears in the returned snapshot.
The fixture is conditional on successful native texture allocation
and records CHECK failure rather than silently claiming success.
This validates inventory lifecycle, not actual GPU memory telemetry.

**Compatibility:** Test-only; no new runtime/editor/backend API
or save/network/ECS source changes.

**Validation:** GitHub source commit confirmed.
Vulkan SDK compile, CTest and real GPU/lavapipe execution:
**NOT RUN** in this environment. The full feature branch
still needs native compiler validation and cannot be considered
production ready.

**Next:** Run `aiverse_vulkan_tests`/`vulkan_backend` on a
working Vulkan device, and test analogous Metal/GL texture
enumeration and editor GUI. Check actual format/memory numbers,
including compressed textures, cube arrays, 3D and MSAA. Continue
separate source/handoff commits.

## Stage 8F — editor readback preview for selected mip/array/cubemap face (2026-11-18)

**Code commit:** `09c2ef46a680bb5a3d31e8ac6fcc783f4745c7b4`.
**Branch:** `feature/texture-rhi-unification` (Draft PR #21).
**Changed file:** `apps/editor/windows/TextureResourcesWindow.cpp`.

**Implementation/API:** The opt-in, read-only `Графика → GPU Текстуры`
inspector now exposes `Read mip` and `Read face / layer`
controls plus an explicit `Read GPU mip / face` action.
The editor calls the existing optional
`gfx::Device::readTextureAsync(TextureHandle,TextureReadbackDesc,
TextureReadFn)`, without treating Texture2DArray/Cube and
multisampled/depth resources as sampler2D. A later owner-thread
callback produces a bounded CPU preview sampled down to at
most 32×32 colored squares using ImGui draw-list rectangles:
no additional GPU preview allocations or texture ownership
are introduced. RGBA8/sRGB use original channels; RGBA16F
diagnostics are approximately tonemapped for display with
explicit warning. The full readback result vector is discarded
after creating its small preview, avoiding persistent large
CPU texture copies.

**Lifetime/safety:** Async callbacks hold a `weak_ptr` to
a private preview state, not the editor window's `this`.
Every new request increments a revision; stale callbacks and
destroyed UI windows cannot publish into invalid state.
If the selected resource disappears, the live GPU table
clears its selection. Unsupported GPU formats and device
operations display a readable fail-closed message; the UI
never implies that unsupported readback succeeded.
The existing direct 2D ImGui texture preview remains
unchanged and closed by default.

**Compatibility:** No authoritative ECS/gameplay/save/network
changes. The inspector reuses `Device::textureResources` and
optional readback; it does not modify native game texture handles.

**Verification:** GitHub code commit succeeded. **Editor C++
build NOT RUN, CTest NOT RUN, real Metal/GL/Vulkan readback/UI
validation NOT RUN, mobile/WebGL not checked.** Preview pixel
orientation, driver format support, shader binding and lifetime
still need real-device checks.

**Next:** Run editor build and all texture CPU fixtures; verify
readback previews for Metal GL33 GLES3 and Vulkan on actual
resources with selected mip/layer/cubemap face. Complete
remaining BasisLZ/UASTC transcode, broad OpenEXR codec coverage
and compute storage-image support as separate bounded stages.
Record every tested SHA and actual outcomes; leave PR Draft
until validated.

## Stage 8G — bound editor async texture previews and invalidate stale callbacks (2026-11-18)

**Code commit:** `7329ee8e853f5fe5b58c303584ff4427f7e30973`.
**Branch:** `feature/texture-rhi-unification`, Draft PR #21.
**Path:** `apps/editor/windows/TextureResourcesWindow.cpp`.

**Source fix:** The shared editor texture inspection window already gained
an independent per-mip/layer async CPU readback preview from another
agent; avoid reimplementing it. This stage sets a strict 16 MiB maximum
for the selected GPU subresource readback before calling the Metal/GL/
Vulkan backend, so a 32×32 downsampled inspector thumbnail does not
cause a giant full-image PBO/blit allocation. Reject unsupported
dimensions, depth and MSAA and defer to backend feature checks for
accepted requests. Suppress repeated readback submissions while a
previous request is still pending. When the selected live texture
changes or disappears, reset the requested mip/layer and clear the
cached preview, incrementing its revision generation. Asynchronous
callbacks carrying old revisions cannot repaint a new resource's
preview (the existing weak_ptr/lifetime guard remains intact).

**Compatibility:** Editor diagnostics only. No GPU texture allocation
paths, scene rendering, ECS, saves/network or asset file formats change.
No new backend methods were added.

**Verification:** GitHub source commit confirmed. Native editor
compilation, ImGui preview and real GPU readback latency/memory tests:
**NOT RUN**; CI green status not verified. The 16 MiB cap is a
source-level budget, not a measured peak memory guarantee.

**Next:** Run the editor on Metal/GL/Vulkan with textures of different
mips/layers and test selection changes during pending readbacks,
resource destruction before completion, unsupported WebGL2 paths
and accepted failed transfers. Then validate all texture CTests,
render passes and platform GPUs before merging.

## Stage 6M — opt-in KTX2 BasisLZ/UASTC transcoder bridge (2026-11-18)

**Code commit:** `81498d54244fed7eaebf267d27aac6f5bcb21444`.
**Branch/PR:** `feature/texture-rhi-unification`, Draft PR #21.
**New file:** `engine/render/Ktx2Transcode.h`.

**Implementation/API:** Added native C++ callback contract
`Ktx2Transcoder`, `parseKtx2WithTranscoder`, and
`uploadKtx2WithTranscoder`. For regular KTX2 containers, the
existing built-in `parseKtx2` is reused unchanged. For KTX2
containers whose VkFormat is zero or whose supercompression
is BasisLZ, callers must provide a decoder/transcoder callback
that yields a canonical `CompressedImage` with all mip/array/
cube/volume slices, not just the first face. The new bridge
checks dimensions, layer/cube consistency, allowed mip counts,
payload byte extents and a per-mip 512 MiB budget before passing
data to the shared GPU uploader. A missing/failed callback or
invalid result fails closed with a descriptive error. GPU
output format selection remains the callback's responsibility,
including block compression and the target device capability.

**Critical limitation:** This stage does **not** include or
implement the actual Basis Universal codec. No Basis library
exists under the repository's `third_party` directory.
Full ETC1S/BasisLZ and UASTC transcoding requires integrating
a correctly licensed and tested external codec provider into
the application or build; the public callback is the seam for
that dependency. No false claim of successful Basis transcoding.
Existing KTX2/DDS importer and ECS/save/network paths remain
unchanged.

**Actual verification:** GitHub code commit completed.
**Native C++ compilation NOT RUN; CTest NOT RUN; real Basis
codec, GPU texture upload, CI pass NOT RUN/NOT CONFIRMED.**

**Next:** Add synthetic Basis-marked KTX2 source with a mock
callback and a malformed-output fixture; register a CPU CTest
and record actual results after tests run. Later vendor/test a
real Basis codec (or use the app's compatible external provider)
and verify transcode formats and full mip/layer/cube fidelity.
Add new handoffs after each stage.

## Stage 8H — focused native Texture RHI build/test log runner (2026-11-18)

**Code commit:** `c89f80fbdba75282352dcc10e8242b92b398d7d8`.
**Branch:** `feature/texture-rhi-unification`, Draft PR #21.
**New path:** `tools/verify-texture-rhi.sh`.

**Source/tool:** Standalone Bash script configuring a dedicated
`build/texture-rhi` CMake tree with `AIVERSE_BUILD_TESTS=ON`,
building the 13 texture-related CPU test targets, running focused
CTest and writing `configure.log`, `build.log`, `ctest.log`
and `summary.txt` into `build/texture-rhi/verify-logs/`.
Optional `--editor` builds `aiverse_editor`; optional `--vulkan`
also builds and invokes `vulkan_backend`. Parallelism configurable
with `--jobs N`, build dir with `--build-dir PATH`, other
`-DCMAKE_*=value` flags forwarded. All target build failures
are collected, not dropped; script exits nonzero on any failed
build or test. A Vulkan skip due to no driver is explicitly
flagged in the summary rather than considered GPU validation.
Run as `bash tools/verify-texture-rhi.sh --editor --vulkan`
on a machine with a real checkout and graphics SDK.

**Compatibility:** New developer-only test script. No engine/game
source, ECS/save/network, asset or editor runtime changes.
GitHub contents API created the file; invoking it with `bash`
does not depend on executable file mode.

**Actual validation:** GitHub source commit confirmed.
Script invocation, CMake configure, compilation, CTest, GPU and
editor testing: **NOT RUN** in this environment. Do not interpret
script presence as passing verification. Existing GH Actions
jobs on older branch commits remain queued without a runner
assigned (ubuntu-latest, windows-latest, macos-latest);
not passing on current HEAD.

**Next:** On macOS/Windows/Linux with native repo checkout,
run `bash tools/verify-texture-rhi.sh --editor` and add
`--vulkan` on Vulkan-enabled machines; send/share the generated
summary and failing logs. Fix *actual* compile/test failures
in the source branch, append separate code/handoff commits,
and only then consider PR #21 production ready.

## Stage 7N — asynchronous disk-backed virtual texture page source (2026-11-18)

**Code commit:** `97b06fcd4b7888c789155858d83e8466cb547d3c`.
**Branch / PR:** `feature/texture-rhi-unification`, Draft #21.
**File:** new `engine/gfx/VirtualTextureDiskQueue.h`.

**Implemented source:** Native C++20 `gfx::VirtualTextureDiskQueue` starts
one owned `std::thread` dedicated to reading immutable, tightly packed
RGBA8 virtual page files. Config declares the trusted asset root, mip-zero
tile dimensions, virtual mip/layer counts, physical page size, maximum
outstanding requests and maximum resident CPU result bytes. Files use a
deterministic integer-only layout
`m<MIP>/l<LAYER>/x<X>_y<Y>.rgba`; requested coordinates are validated
against virtual mip extents and cannot inject arbitrary path segments.
`request(VirtualPageKey)` deduplicates in-flight and ready requests;
`cancel(ticket)` drops queued/completed work and invalidates active
blocking-read results; `takeReady(out,maxBytes,maxCount)` transfers
ownership of bounded result bytes to the render thread. The worker
verifies exact on-disk byte size, reads into CPU-owned buffers, blocks
when the result-memory budget is exhausted and notifies the caller
through polling (no GPU/device calls from the worker). `stats()`,
`lastError()`, `pageBytes()`, `shutdown()` expose lifecycle and
memory accounting. Tickets are monotonic and never recycled on restart.
A missing/truncated file is an explicit failed result, never a silent
partial GPU page. Caller passes successful page bytes to existing
`VirtualTextureStreamer::stage`/`drain` on the GPU owner thread.

**Scope / missing:** This is a real filesystem I/O worker, *not*
a GPU feedback pass, codec/transcoder, native sparse API, or
automatic scene camera demand predictor. The root is a trusted
game asset directory (symlink access is not sandboxed). A single
cancellation of a deduplicated key cancels the shared ticket for
all callers. Thread destruction waits for an active filesystem read;
the worker must be destroyed before its host game shuts down.

**Compatibility:** Opt-in header only; no change to Device ABI,
texture formats, runtime render passes, deterministic ECS,
save/network data or existing project assets. Use as a
separate disk source for the physical page atlas, never as
ECS-authoritative state.

**Actual verification:** File created and code commit confirmed
through GitHub connector. C++ compile, standalone test, GPU,
editor and CI: **NOT RUN** yet. The local container cannot
resolve github.com to clone the AIVERSE checkout. No green
CI status was observed.

**Next:** Write a focused mock disk fixture and CTest target
using a temporary asset root, real binary files, duplicate
request/cancellation, invalid coordinates, exact byte sizes,
ready-byte backpressure, ticket lifetime, restart and missing
file failures. Link with `Threads::Threads`. Run a standalone
local compile where possible and the full native test later.
Then add an integration helper that pumps disk results into
`VirtualTextureStreamer::stage` on the render thread.
Separate source and handoff commits per stage.

## Stage 7O — real disk-page I/O / cancellation / backpressure CPU fixture (2026-11-18)

**Code commit:** `90c654c626d5887f3ebfec1a1c790c8827af975f`.
**Branch:** `feature/texture-rhi-unification` (Draft PR #21).
**Paths:** new `tests/render/virtual_texture_disk_queue.cpp`,
`CMakeLists.txt`.

**Test source:** Registered `aiverse_virtual_texture_disk_queue_test`
and CTest `virtual_texture_disk_queue`, linked with
`Threads::Threads`. Unlike the GPU mock fixtures, this test uses
real temporary filesystem folders and packed `.rgba` page files
with known byte values. It covers two-page FIFO I/O, deduplicated
page request IDs, invalid mip/tile/layer rejection, 64-byte
result-memory backpressure and render-thread polling, rejection
of poll budgets smaller than one page, missing/truncated file
failures without partial pixels, max-outstanding requests,
queued/inflight cancellation, restart of the disk worker with
monotonic ticket IDs, stale ticket rejection and invalid config
refusal. All page paths are created using the exact public
`relativePath` ABI, not manually guessed strings.

**Compatibility:** Test/CMake stage only. No change to GPU
Device, editor, gameplay/ECS state, save/network or file format.
It covers the `VirtualTextureDiskQueue` added in Stage 7N.

**Actual verification:** GitHub source/tree/commit/ref update
succeeded. Native C++ compilation and execution of this CTest:
**NOT RUN** (local container cannot resolve github.com for clone).
It is not a passing test merely because registered in CMake.

**Next:** Compile and execute
`cmake --build build/texture-rhi --target aiverse_virtual_texture_disk_queue_test`
and `ctest --test-dir build/texture-rhi --output-on-failure
-R '^virtual_texture_disk_queue$'` on a native repo checkout.
Add a GPU-thread integration adapter that moves ready pages into
`VirtualTextureStreamer::stage`, handles retry/backpressure,
and discards results for destroyed virtual atlases. Write a
separate code+handoff commit, then update the verification script,
native skill and architecture docs. No background GPU work should
be claimed without real driver validation.

## Stage 7P — disk-to-GPU virtual texture streaming coordinator (2026-11-18)

**Implementation commits:** `d7346d63c96b19b2316609a860d22f653ccb7f59`
(new facade) and `332d9c325d86bb95e9272ebd7a11b8df052c10c3`
(exact page-residency lookup).
**Branch/PR:** `feature/texture-rhi-unification`, Draft #21.
**Files:** new `engine/gfx/VirtualTextureFileStreamer.h`,
`engine/gfx/VirtualTextureStreamer.h`.

**Implemented source:** Native `gfx::VirtualTextureFileStreamer`
composes a real disk I/O worker (`VirtualTextureDiskQueue`) with the
physical GPU page atlas, GPU upload queue, LRU and page-table
publication (`VirtualTextureStreamer`). `initialize` validates
virtual dimensions, layers, page size and RGBA8 format across the
two components and owns their lifetime. `requestPage(key,pin)`
deduplicates in-flight keys, avoids reloading a page already
*exactly* resident and optionally pins important coarse fallback
pages. The added `VirtualTextureStreamer::resolveExact` is
distinct from its existing coarser-fallback `resolve`: callers
still request missing detail pages even when a coarser ancestor
is available. Render-thread `pump(maxNewGpuPages,callback)`
polls bounded disk results, buffers completed page bytes, stages
them into the existing GPU queue (which copies source bytes),
calls the atlas' GPU drain, and delivers events only after
page-table publication or an explicit failed transfer. GPU
staging backpressure retains the CPU bytes for retry. Missing/
truncated disk results produce failure notifications without
GPU data. Disk/GPU pending and buffered pages are cancelable
via `cancel(ticket)` before submission. On a quarantined GPU
atlas, the facade drops outstanding work, masks all success
events and requires explicit reinitialization. Read-only
atlas/table handles, page lookup, stats and error surfaces
are provided. All GPU calls remain on the device thread;
disk worker never calls Device.

**Compatibility:** Opt-in header and one additional method on the
new virtual texture facade, no existing gfx::Device ABI,
ECS/saves/netcode, legacy texture asset or import behavior
changes. Device must outlive the coordinator. The component
implements filesystem-backed page demand **from explicit host
requests**; it is NOT yet automatic GPU-feedback demand paging.
A request already handed to an active GPU submission cannot be
canceled behind the driver's back.

**Verification:** Both commits exist on GitHub; native C++ build,
CTest and real GPU execution are **NOT RUN**. Existing GitHub CI
jobs observed queued/cancelled. The integration path is source-level
and is NOT production-certified.

**Next:** Add a combined real filesystem + mock GPU fixture verifying
coarse pinned page becomes visible only after GPU upload, finer
page replacement, callbacks, byte budgets, missing page failure,
repeated request, cancellation, staging backpressure, and a
quarantined atlas. Register a CTest and then compile on the native
checkout. Add actual virtual-page GPU feedback/prefetch policy
and disk asset packer only in separate documented stages.

## Stage 7Q — integrated real file-to-GPU virtual texture fixture (2026-11-18)

**Code commit:** `b21a5ba93df731174b8f287bf91e6d58511bb9cd`.
**Branch:** `feature/texture-rhi-unification`, Draft PR #21.
**Paths:** new `tests/render/virtual_texture_file_streamer.cpp`,
`CMakeLists.txt`.

**Test source:** Register
`aiverse_virtual_texture_file_streamer_test` /
CTest `virtual_texture_file_streamer` and link `Threads::Threads`.
The integration fixture uses actual immutable page files in a
temporary directory plus a deterministic mock `gfx::Device`.
Exercises the new `VirtualTextureFileStreamer`: constructing a
physical `Texture2DArray` atlas and GPU-backed page table,
deduplication and exact-residency distinction, a coarse fallback
pinned until its disk read and GPU upload complete, independent
fine page upload that overrides the ancestor, deterministic LRU
eviction of an unpinned detail page, restoration of coarse
fallback in the shader table, GPU texture/resource cleanup,
cancel of disk requests, missing file errors without
quarantining healthy GPU resources, and GPU update failure
quarantining the atlas so no partially updated texture can
be sampled. Invalid disk/atlas dimensions fail before allocation.
All tests are source-level and need CMake compilation.

**Compatibility:** Test/CMake only. No new gameplay, ECS,
save/netcode or public rendering API. Existing `VirtualTextureFileStreamer`
production facade from Stage 7P is unchanged.

**Actual verification:** GitHub source commit/ref update succeeded.
Native compiler, CTest and real GPU backend execution:
**NOT RUN**; the container has no clone and cannot resolve
github.com, and prior GitHub runners remain queued.
The test is not a passing verification until it executes.

**Next:** Compile the integrated fixture:
`cmake --build build/texture-rhi --target aiverse_virtual_texture_file_streamer_test`,
`ctest --test-dir build/texture-rhi --output-on-failure
-R '^(virtual_texture_disk_queue|virtual_texture_file_streamer)$'`.
Fix compiler/behavior failures. Update `tools/verify-texture-rhi.sh`,
the native texture RHI agent skill and `docs/texture-rhi.md`
to include disk worker/facade and new tests, each with a
separate code/docs and handoff commit.
After that implement an optional camera/CPU visibility-based
prefetch planner and/or real GPU feedback; do not imply
automatic sparse GPU feedback or complete BasisU transcoding.

## Stage 7R — disk-backed VT skill, architecture and verification script integration (2026-11-18)

**Implementation/documentation commit:** `c4a54cb627be31f1326530fc4c555a1bc44eebd7`.
**Branch / PR:** `feature/texture-rhi-unification`, Draft #21.
**Files:** `tools/verify-texture-rhi.sh`, `docs/texture-rhi.md`,
`skills/visuals/aiverse-texture-rhi/SKILL.md`.

**Changes:** Register the disk-thread CTest `virtual_texture_disk_queue`
and the real-file-to-mock-GPU integration CTest
`virtual_texture_file_streamer` in the focused verification script's
target list and both regular/Vulkan CTest filters. The architecture
document now distinguishes the new *actual* filesystem worker and
explicit page-request-to-atlas pipeline from unimplemented automatic
GPU feedback and hardware sparse GPU resources. The native AI skill
references both public headers and includes a concrete integration
example with trusted `.rgba` page layout, matching disk/atlas configs,
pinned coarse fallback request and render-thread `pump` callback.
Gameplay agents must reuse these independent texture libraries rather
than regenerate an RTS/FPS-specific loader.

**Compatibility:** Developer tooling, docs and skill only. No game
simulation, save/network state or gfx::Device API changed in this
stage. Existing callers need not enable the disk worker.

**Actual verification:** GitHub code/tree/commit/branch update
succeeded. Native compiler, Bash script invocation, CTest
and Metal/GL/Vulkan GPU tests: **NOT RUN**. The container still
cannot resolve github.com for a full checkout; GitHub Actions
were queued/cancelled, without passing results.

**Next:** Run `bash tools/verify-texture-rhi.sh --editor` on an actual
checkout and add `--vulkan` where Vulkan SDK/device exists.
Inspect `build/texture-rhi/verify-logs/{configure,build,ctest,summary}`
and fix real issues. For the two new targets use
`ctest -R '^(virtual_texture_disk_queue|virtual_texture_file_streamer)$'`.
Then implement a camera/CPU visibility prefetch policy and
optional atlas feedback buffer after measured demand; real
GPU page-table shader sampling remains required before merge.
Each stage still needs separate code + handoff commits.

## Stage 7S — dependency-free virtual texture disk asset packer (2026-11-18)

**Code commit:** `fdd23a2e25b51c6231f86308777452fc263f638c`.
**Branch:** `feature/texture-rhi-unification`, Draft PR #21.
**Paths:** `tools/virtual_texture_pack.py`,
`tests/render/virtual_texture_pack.py`.

**Implemented source:** Standard-library-only Python 3 CLI/utility
`pack_pages(inputRGBA8Layers,width,height,pageSize,gutter,destination)`
generates all `m<MIP>/l<LAYER>/x<X>_y<Y>.rgba` disk tile files
expected by the C++ `VirtualTextureDiskQueue`. Supports 1..65536
raw RGBA8 array layers (same source dimensions), a configurable
physical page size with edge-clamped gutters, deterministic mip
generation with 2x2 box filtering and edge clamping, mip count
until the virtual tile grid reduces to 1×1, exact fixed-size
physical pages, and a stable JSON manifest describing schema,
source sizes, virtual tile counts, layers, mip levels, gutter,
physical page dimensions and per-input SHA256 hashes.
The packer checks input dimensions, source file exact size,
pixel budget, tile/gutter validity and output collisions.
All writes happen under a temporary directory in the
destination's parent; only after successfully processing
every layer and mip is the staging directory renamed to
the requested output path. Failures clean temporary content
and do not destroy preexisting output.

**New tests:** A pure-Python `unittest` fixture with
multi-layer 4×4 RGBA gradients, complete mip page count,
edge-gutter sampling, averaged mip texels, deterministic
odd-dimensional output, SHA/manifest metadata, file truncation
rollback and invalid output/pixel/gutter limits.

**Known source-level test issue:** The initial `test_multi_layer_mips_and_gutters`
test locates its expected green-channel pixel at a row-one
offset while checking a row-two source. Correct the index
from `(4+2)*4` to `(2*4+2)*4` in a separate fix commit
before calling that test passing. This is a test assertion
offset mistake, not a production packer algorithm finding.

**Compatibility:** Opt-in packaging tool. No change to
native gfx::Device, simulation/ECS authority, saves/network,
existing project asset formats or render pipeline.
The asset input is **raw RGBA8**, not PNG/EXR/KTX2; convert
source images upstream. Packed byte pages include gutters
and fit the disk I/O worker/physical GPU atlas contract.

**Verification:** GitHub code/tree/commit/ref update succeeded.
Python unittest and CMake/CTest/native GPU execution:
**NOT RUN** yet. Do not claim a passing packer test before
fixing the known assertion and executing it.

**Next:** Fix the gutter-index assertion; register the Python
test in the focused native verifier and CTest where Python3
is available. Execute a real Python unittest in this container
if possible, record its exact outcome; add a matching
manifest sanity check in the disk-backed facade and
configure input conversion to PNG via an optional image
provider later. Each bounded code stage requires a distinct
handoff commit.

## Stage 7T — correct Python packer gutter assertion and register CTest (2026-11-18)

**Code commit:** `e3618dd816b063ccf4af9ce74ef2180824ce444e`.
**Branch / PR:** `feature/texture-rhi-unification`, Draft #21.
**Paths:** `tests/render/virtual_texture_pack.py`,
`CMakeLists.txt`, `tools/verify-texture-rhi.sh`.

**Fix:** Correct the synthetic 4×4 gutter assertion's source
row coordinate from a row-one offset to the intended row-two
offset, `(2*4+2)*4`, matching the actual edge-clamped tile
sampling. Register `virtual_texture_pack` under the existing
`AIVERSE_BUILD_TESTS && Python3_Interpreter_FOUND` CTest gate,
with a direct Python test runner and no third-party packages.
Add this test name to both normal and optional-Vulkan filters
in the canonical `tools/verify-texture-rhi.sh` runner.
Other pure Python tests include odd sizes/multi-layer metadata,
mip generation and transactional failure rollback.

**Compatibility:** Testing and verification tooling only.
No native engine, GPU/scene, ECS, save/network or asset schema change.

**Verification:** GitHub source commit succeeded. Real Python
test process and CTest build/test: **NOT RUN at commit time**.
The test's source code is present and registered but not
assumed passing.

**Next:** Execute the Python unittest from a real checkout,
and test the same source logic in an isolated local sandbox
if the code can be supplied there. Record actual commands,
exit codes and failure diagnostics. Then add a safe game
asset manifest loader/validator to match the disk queue
configuration, and validate the whole file→GPU flow.
Separate implementation and handoff commits for each stage.

## Stage 7U — executed Python packer tests against exact committed blobs (2026-11-18)

**Code:** stages 7S/7T, source commits
`fdd23a2e25b51c6231f86308777452fc263f638c` and
`e3618dd816b063ccf4af9ce74ef2180824ce444e`.
**Branch/PR:** `feature/texture-rhi-unification`, Draft #21.
**Verification-only stage:** no engine/source files changed here.

**Actual executed test environment:** Isolated local sandbox on
Python **3.13.5**, using copies of the two committed files.
The copies were verified byte-for-byte via Git blob hashes:
`tools/virtual_texture_pack.py` =
`82e1a749b926fa4ae410710c78bac14dc2777d88`;
`tests/render/virtual_texture_pack.py` =
`b681fd09ce216606aeee6d2fd43bf4fa00505da1`,
matching the exact GitHub file SHAs on this branch.

**Command actually run:**
`python3 /mnt/data/_vt_check/tests/render/virtual_texture_pack.py -v`
**Result: PASS, 3 tests in 0.006 seconds**, process exit 0:
`test_invalid_sources_are_transactional`,
`test_multi_layer_mips_and_gutters`,
`test_odd_dimensions_and_determinism` all `ok`.

**CLI smoke test actually run:** Invoked the exact committed
packer as a subprocess with an 8×8 raw RGBA8 layer, physical
page size 6, gutter 1 and a temporary output root.
**Result: exit 0**, manifest reported 2×2 mip-zero tiles
and 2 virtual mip levels; generated 5 page files, each
exactly 144 bytes, plus `manifest.json` (verified present).

**Scope of PASS:** Only these Python packer and asset-layout
checks were actually executed. No full AIVERSE repository
checkout was present. CMake/CTest, C++ disk worker/facade
compilation, Metal/GL/Vulkan GPU tests, editor and CI:
**NOT RUN / NOT CONFIRMED PASSING**. The Python results are
not evidence that the native graphics pipeline works.

**Next:** Build and run native
`virtual_texture_disk_queue` and
`virtual_texture_file_streamer` CTests on a repo checkout.
Validate the resulting `.rgba` files with the real C++
filesystem loader and atlas/page-table shader on a GPU;
then implement camera visibility prefetch or GPU feedback,
with separate source/handoff commits.

## Stage 6N — harden KTX2 external transcoder boundary (2026-11-18)

Code SHA: 6f0569f638f186fb8606a9d7065101e80f4dcac3.
Branch: feature/texture-rhi-unification, Draft PR #21.
Changed file: engine/render/Ktx2Transcode.h.

Implementation: Stage 6M's external callback boundary now validates the KTX2
source header/index before calling Basis Universal: magic, total input budget,
dimensions, declared 1D/3D/cube/array topology, mip count, compression scheme
and level index ranges. Callback output must preserve source extents,
array layers, cube faces and mip count. Unknown BlockFormat enum values,
non-exact mip sizes and aggregate decoded payload exceeding 512 MiB fail closed.
Exceptions inside a plugin callback clear output and report failure.
Non-Basis KTX2 still goes through the standard parseKtx2 parser.

API: Existing Ktx2Transcoder, validateTranscodedImage,
parseKtx2WithTranscoder and uploadKtx2WithTranscoder signatures remain unchanged.
The actual BasisLZ/UASTC codec is not bundled; an external codec is required.
This stage does NOT provide a real native Basis transcoder.

Compatibility: No RHI backend, ECS, save/network, other importer or material
shader behavior changed.

Actual verification: GitHub source commit succeeded. Native C++ compiler
NOT RUN, CTest NOT RUN, GPU NOT RUN. Full native checkout unavailable.
No passing result is claimed.

Next: Synthetic CTest fixture covering valid layered Basis transcode,
truncated index, mismatched topology/levels, unknown format, failure,
callback exception and exact mip payloads. Commit source and handoff separately.

## Stage 6O — KTX2 transcoder CPU contract fixture (2026-11-18)

Code commit: 89c146f5b079f9a952d3001786d1f6f904671ceb.
Branch: feature/texture-rhi-unification; Draft PR #21.
Files: tests/render/ktx2_transcode.cpp, CMakeLists.txt.

Source: register aiverse_ktx2_transcode_test / ktx2_transcode with
the existing aiverse_models and aiverse_zstd_dec link targets.
Synthetic KTX2 tests model two-mip twelve-face Basis data and
verify complete mip-major output through an injected callback.
Negative fixtures cover missing codec, callback errors/exceptions,
payload offset pointing inside the header, truncated file index,
unsupported supercompression, illegal faces, wrong array extent,
truncated/oversized mip output and unknown BlockFormat enums.
An ordinary non-Basis RGBA8 KTX2 bypasses the plugin and uses
the standard parser. No decoder implementation is bundled.

Verification performed: GitHub source + CMake commit and fast-forward
ref update succeeded. C++ build NOT RUN; CTest NOT RUN; GPU NOT RUN.
No verified PASS claim is made for the unexecuted fixture.

Next: Ensure the focused texture verification script includes
ktx2_transcode, then implement only a genuine licensed Basis codec
when its dependency is available. Before approval, run a native
build/CTest and a real GPU upload covering normal/sRGB, arrays,
cubemaps and mip chains. Follow the per-stage separate handoff policy.

## Stage 6P — Basis transcoder verifier and native-agent documentation (2026-11-18)

Source/docs commit: 76fce9b3e74ee17e774fd11b59b212f7ae50d783.
Branch: feature/texture-rhi-unification, PR #21 Draft.
Paths: tools/verify-texture-rhi.sh, skills/visuals/aiverse-texture-rhi/SKILL.md, docs/texture-rhi.md.

Changes: Add aiverse_ktx2_transcode_test to the focused build target list
and ktx2_transcode to both normal and --vulkan CTest expressions.
Update the AI texture skill with the injected Ktx2Transcoder API and
requirements for complete mip/face arrays, codec licensing and GPU support.
Document the bridge's exact topology/budget validation separately from
the still-unimplemented concrete Basis Universal codec. No ECS or native
graphics backend files are changed in this documentation stage.

Actual validation: GitHub commit and ref update succeeded. Native C++ build,
CTest, verifier script, GPU tests and skill audit were NOT RUN.
Next: Implement a real licensable/available Basis codec instead of a stub,
or continue independent storage textures, disk streamer integration and
native verification. Do not merge PR #21 before build/driver gates.

## Stage 4Y — explicit compute-writable texture API and Metal binding (2026-11-18)

Code commit: 2c799f4e8f55a03be10aeceba5d8612604865223.
Branch: feature/texture-rhi-unification, PR #21 Draft.
Files: engine/gfx/Gfx.h, engine/gfx/metal/GfxMetal.mm,
tests/render/texture_contract.cpp.

API: Device::supportsStorageTexture(PixelFormat, TextureDimension) defaults
to false and Device::bindComputeStorageTexture(slot, TextureHandle)
defaults to false. Existing setComputeTexture continues to support
read-only sampled compute inputs. Writable binding requires an active
compute encoder, a storage=true texture, a supported format/dimension
and a valid slot; unsupported requests fail visibly.

Metal source now advertises a conservative writable subset:
RGBA8 or RGBA16F Texture2D, Texture2DArray and Texture3D.
createTexture checks the advertised capability before allocation;
MTTexture retains the storage creation flag and the writable
binding checks MTLTextureUsageShaderWrite. No hidden downgrade to
sample-only storage is performed. GL33/GLES3 and Vulkan retain the
safe default unsupported contract because compute/shader image
binding is not yet implemented in those backends.

Source test additions in texture_contract cover structurally valid
2D, 2DArray and 3D writable descriptors and reject compressed
storage formats. These are CPU descriptor tests, not a Metal GPU test.

Actual verification: GitHub multi-file commit/ref update succeeded.
C++ compile NOT RUN, CTest NOT RUN, Metal shader/compute test NOT RUN,
GL/Vulkan unsupported-device test NOT RUN. No GPU capability
has been claimed certified.

Next: Add an actual Metal compute shader test that writes RGBA8
and RGBA16F 2D/array/volume images and confirms readback.
Build native editor, then add Vulkan compute descriptor/pipeline
only if the backend supports the necessary synchronization/bindings.
Do not present the capability as cross-backend storage support.

## Stage 5P — native compressed sRGB format selection (2026-11-18)

Code commit: 71259009accf0d2e6943bda692c59a97a5c4093e.
Branch: feature/texture-rhi-unification, Draft PR #21.
Paths: engine/gfx/Gfx.h, engine/gfx/gl/GfxGL.cpp,
engine/gfx/metal/GfxMetal.mm, engine/gfx/vulkan/VkResources.cpp,
engine/render/TextureAsset.h, tests/render/texture_contract.cpp,
tests/render/texture_asset.cpp, apps/editor/windows/TextureResourcesWindow.cpp.

Implementation: added native PixelFormat variants BC1_sRGB, BC3_sRGB,
BC7_sRGB, ETC2_RGB8_sRGB, ETC2_RGBA8_sRGB, ASTC_4x4_sRGB,
ASTC_6x6_sRGB. Shared compressed-format block byte/dimension
calculations include the variants. OpenGL maps to standard
compressed sRGB GL enums; GLES/WebGL sRGB S3TC capability is
gated separately from base S3TC. Metal maps to sRGB variants
on Apple-supported families and macOS BC; Vulkan uses matching
VK_FORMAT_*_SRGB_BLOCK and native per-format device probing.
TextureAsset now selects hardware-sRGB compressed upload for
appropriate KTX2/DDS color-space metadata rather than requiring
opt-in manual shader gamma conversion or CPU RGBA8 fallback.
Unsupported hardware formats still use CPU fallback where a
decoder exists; ASTC and BC6H remain fail-closed without a
compatible native GPU or external decoder. BC1_RGB special
opaque-index semantics continue to avoid the BC1_RGBA shortcut.
Editor texture resource names include all appended formats.

Test sources added: texture_contract checks sRGB block sizes
and ASTC block dimensions; texture_asset mock verifies native
BC7_sRGB and BC1_sRGB GPU descriptors without manual shader opt-in.

Compatibility: Enum values are appended to preserve earlier
numeric identifiers. No ECS/gameplay, save/network schema,
material shader or non-texture backend API changes.

Verification: GitHub code commit/ref update succeeded.
C++ compile NOT RUN; CTest NOT RUN; GL/WebGL/GLES, Metal
and Vulkan sampling/readback NOT RUN. The enum names and
backend support require real platform compiler/GPU checks.

Next: Add synthetic KTX2 sRGB format upload tests for
ETC2/ASTC and reject unsupported mobile sRGB compression
without silently selecting UNORM; then run native build
and GPU tests before merge. Maintain per-stage handoff.

## Stage 5Q — compressed sRGB KTX2 and CPU fallback regression source (2026-11-18)

Code SHA: ca71c7459ef0940acd38a538e32bc4a083fd2199.
Branch: feature/texture-rhi-unification (Draft PR #21).
Files: tests/render/ktx2_layers.cpp and tests/render/texture_asset.cpp.

New synthetic KTX2 source fixtures cover all seven native sRGB
block variants: BC1, BC3, BC7, ETC2 RGB, ETC2 RGBA, ASTC 4x4 and
ASTC 6x6. The parser must preserve true srgb metadata, source
BlockFormat and correct single-mip block payload byte length.
Mock RHI uploads exercise native ETC2 RGB sRGB and ASTC 4x4 sRGB
without a manual shader gamma opt-in. BC1_RGB sRGB (VkFormat132)
is deliberately decoded to uncompressed RGBA8_sRGB rather than
reinterpreted as BC1_RGBA_sRGB and losing opaque index-3 semantics.

Testing status: GitHub code/test source commit succeeded.
Native C++ build NOT RUN; ktx2_layers and texture_asset CTests
NOT RUN; Metal/Vulkan/GL/GLES/WebGL GPU sampling NOT RUN.
No PASS result claimed.

Next: Update native texture skill/docs for sRGB selection and
test the staged source using real CMake/CTest. Verify shader
sampling/color conversion on actual Metal/Vulkan/GL and mobile
devices. Keep PR #21 Draft until backend and regression gates pass.

## Stage 5R — sRGB compression format documentation (2026-11-18)

Code/docs SHA: ef7b57d478377bedecf1c893b4e36965a160af81.
Branch: feature/texture-rhi-unification (Draft PR #21).
Files: docs/texture-rhi.md, skills/visuals/aiverse-texture-rhi/SKILL.md.

Documentation now describes hardware-compressed sRGB BC1/BC3/BC7,
ETC2 and ASTC variants with backend capability checks, native
color decoding, CPU RGBA8_sRGB fallbacks, WebGL2 sRGB-S3TC
extension requirement and BC1_RGB opaque index-3 semantics.
Legacy trustShaderSrgbDecode remains an explicit shader opt-in
rather than a requirement for actual sRGB-compressed GPU formats.
The native agent skill instructs game coders to reuse these
native RHI paths rather than re-encode or double-convert color.

Actual verification: GitHub docs commit and ref update succeeded.
C++ compile, CTest, skill catalog audit and GPU shader tests NOT RUN.
No new engine code or save/network data changed in this stage.

Next: Native compilation and CTest on current HEAD; GPU color
accuracy captures for Metal/GL/GLES/Vulkan/WebGL2. Record exact
outcomes and correct failures before approving Draft PR #21.

## Stage 5S — explicit pair include for KTX2 sRGB fixture (2026-11-18)

Code commit: 1ddf652f9241e88d573a7ecb87bf7c0511f54a96.
Branch: feature/texture-rhi-unification (Draft PR #21).
Path: tests/render/ktx2_layers.cpp.

Source review correction: explicitly include <utility> for the
std::pair<uint32_t,BlockFormat> vector fixture introduced
in Stage 5Q, rather than relying on transitive declarations
from <vector>. No behavior/runtime implementation changed.

GitHub source commit succeeded. Native compiler NOT RUN,
CTest NOT RUN, actual GPU shader tests NOT RUN.
Next: Build the grouped texture suite on a native checkout
and fix genuine compiler/test errors; preserve stage handoffs.

## Stage 7V — pure UV visibility prefetch planner (2026-11-18)

Code commit: bd1e38fdd4d732d98820527a15293923996cde3d.
Branch: feature/texture-rhi-unification; PR #21 Draft.
New file: engine/gfx/VirtualTexturePrefetch.h.

Public API: gfx::VirtualTexturePrefetchPlanner with initialize(config),
plan(regions,exactResident), stats(), reset(); descriptors
VirtualTextureVisibleRegion, VirtualTexturePrefetchConfig,
VirtualTexturePrefetchRequest and VirtualTexturePrefetchStats.
The planner takes only normalized UV rectangle visibility, mip
target, virtual layer and priority: no FPS, RTS, camera entity,
foliage/terrain or GPU API dependencies. It validates finite input,
clips rectangle bounds, sorts higher-priority views first,
constructs deduplicated coarse-to-fine mip page requests, pins
coarsest pages if selected, optionally skips EXACT resident pages
and caps regions/candidates/new requests per frame. Stable
ordering is mip descending, priority descending, layer/y/x.
Unspecified application visibility and desired mip selection
remain caller-driven, not guesses about camera or textures.

Compatibility: CPU-only opt-in header, no existing renderer
usage/ABI, ECS save/network or source asset-format changes.
Known limitation: candidate budget caps page enumeration; when
world UV coverage exceeds it, later fine mips are deferred.
A camera/visibility adapter and deterministic fixture are next.

Verification: GitHub code commit completed. C++ compilation,
CTest, full native build and Metal/GL/Vulkan GPU tests NOT RUN.
Next: add a mock CPU CTest for invalid rectangles, overlapping
views, coarse-first order, clipping, exact-resident exclusion and
candidate/request limits, then integrate with the existing
VirtualTextureFileStreamer requestPage API. Keep per-stage
implementation/handoff separate.

## Stage 7W — UV visibility prefetch CPU fixture and verification target (2026-11-18)

Source commits: 568a75fe6f2f08cb6c82b66f73d2be18e9a498a5
(fixture + test registration), 445f393060671c79760c367f5b3ad17b53f5e814
(correct overlapping visible-region candidate count).
Branch: feature/texture-rhi-unification, PR #21 Draft.
Files: tests/render/virtual_texture_prefetch.cpp, CMakeLists.txt,
tools/verify-texture-rhi.sh.

Implementation: Register aiverse_virtual_texture_prefetch_test as
CTest virtual_texture_prefetch and add it to the focused verifier.
CPU-only tests exercise coarsest-mip priority, multiple virtual
layers, exact-resident omission rather than parent-fallback omission,
UV clamping, overlapping rectangle deduplication, invalid/nonfinite
coverage, stable ordering, pinned fallbacks, maxRegions,
maxCandidates and maxRequestsPerFrame. Test fixture count was
corrected after source review: 8 distinct pages for overlapping
regions in the tested mip geometry, not 12.

Compatibility: No native renderer, graphics backend, game ECS or
save/network changes. Test code only; planner remains the Stage 7V
engine/gfx/VirtualTexturePrefetch.h source.

Actual verification: GitHub source and correction commits succeeded.
CMake compile NOT RUN, CTest NOT RUN, full native build NOT RUN,
GPU sampling NOT RUN; no passing status is claimed.

Next: Provide optional, genre-independent requestPage integration
with VirtualTextureFileStreamer. It must not interpret parent
fallback as exact residency. Then document native game integration,
with a source commit and a separate append-only handoff.

## Stage 7X — connect UV visibility plans to real disk-backed VT uploads (2026-11-18)

Source commit: e21efd6f3a1a8545fae76699f46e0dd8e9101d6f.
Branch: feature/texture-rhi-unification, Draft PR #21.
Files: engine/gfx/VirtualTextureFileStreamer.h and new
engine/gfx/VirtualTexturePrefetchAdapter.h.

API: prefetchVisibleVirtualPages(VirtualTextureFileStreamer&,
VirtualTexturePrefetchPlanner&, visibleRegions) returns bounded
VirtualTexturePrefetchDispatch containing planner stats, accepted
new disk-page tickets and refused count. It skips both EXACT
resident pages and pending disk/GPU pages; parent-fallback
sampling does not suppress detail loading. It upgrades pins
on already resident/pending coarsest fallback pages.
VirtualTextureFileStreamer now exposes exactResidentPage,
pendingPage and pinPage; duplicate requestPage(key,pin=true)
also promotes an existing pending request to pinned.
Work is sorted coarse-to-fine by the Stage 7V planner; if
queue capacity rejects a page, it stops before submitting
finer pages and allows a later frame to replan. GPU Device
work remains solely inside streamer.pump(); I/O remains on
the existing worker. No genre-specific state is introduced.

Compatibility: new opt-in C++ header and additive inline
facade methods only. No RHI backend virtuals, serialization,
game ECS, save/network or disk asset format changed.

Actual verification: GitHub source/tree commit succeeded.
C++ build NOT RUN, CTest NOT RUN, real disk/GPU integration
NOT RUN. Adapter still needs a deterministic test fixture
covering exact-vs-parent residency, dedup, pin upgrade,
backpressure and cross-frame planning.

Next: add prefetch adapter fixture and native AI skill usage.
Commit source and handoff separately, then execute real native
build/CTest when implementation is complete.

## Stage 7Y — integrated visibility-prefetch-to-GPU test source (2026-11-18)

Test source commit: 641c7977ac0b68dd9be1b20ad5c6080d2c75bab3.
Branch: feature/texture-rhi-unification, PR #21 Draft.
File: tests/render/virtual_texture_file_streamer.cpp.

The existing native filesystem+mock Device CTest now additionally
includes VirtualTexturePrefetchAdapter.h and exercises automatic
UV visibility to disk page queue to owned GPU atlas/table uploads.
First-frame request budget submits coarse and intermediate mip pages;
the following frame skips those pending pages and requests the
fine mip; a third frame does not issue duplicate reads. After
worker/GPU completion, exact residency of three visible pages
is verified and the coarsest mip is pinned. The fixture also
constructs a separately pending unpinned coarse page, requests
the visible region and checks promotion to a pinned fallback
before GPU residency is published. All pages are synthetic
fixed-size RGBA8 assets under a temporary test directory.

No additional CMake target is required: the existing
aiverse_virtual_texture_file_streamer_test / virtual_texture_file_streamer
target now builds this integration test.

Compatibility: test-only update; no native RHI, ECS, save/network
or asset-layout modification. No automatic camera-to-UV projection;
that is still the caller's responsibility.

Verification: GitHub test-source commit succeeded. Native C++
build NOT RUN; CTest NOT RUN; actual disk/GPU hardware tests
NOT RUN. Do not report this fixture as passing.

Next: Document prefetchVisibleVirtualPages in the native texture
skill, along with the UV visibility adapter ownership and
coarse fallback pinning guarantees, then hand off. Later
compile/run CTest and fix actual failures.

## Stage 7Z — native visibility prefetch agent guidance and test routing (2026-11-18)

Documentation code commit: f9e7e8640529fb45b461b6e33ec7b0dc6b5c694f.
Branch: feature/texture-rhi-unification, PR #21 remains Draft.
Paths: skills/visuals/aiverse-texture-rhi/SKILL.md,
docs/texture-rhi.md and AGENTS.md.

Implementation: Native texture skill has a genre-independent
example projecting a visible UV rectangle into
VirtualTexturePrefetchPlanner and prefetchVisibleVirtualPages,
then pumping the existing disk-backed GPU streamer. It documents
exact residency vs coarse fallback, pin promotion, bounded
CPU region/candidate and frame-request budgets, request tickets
and caller ownership of camera/terrain/world visibility projection.
The canonical architecture doc adds an explicit prefetch section,
references both new public headers and includes
aiverse_virtual_texture_prefetch_test and virtual_texture_prefetch
in native CMake/CTest commands. AGENTS.md instructs all game agents
to reuse the generic prefetch APIs, not to build per-genre streamers.

Compatibility: Documentation and AI skill only, no C++ engine
runtime, asset formats, ECS authority, saves or networking changed.

Actual verification: GitHub doc/tree/ref update succeeded.
C++ compile, CTest, skill audit and real driver validation NOT RUN.
No green CI or GPU verification claimed.

Next: Build the feature branch and execute the expanded focused
texture tests. Fix any compile/test failures, especially
disk->GPU prefetch callback ordering and backend source integration.
Add GPU feedback only as a separate bounded pipeline with
safe synchronization and a separate handoff.

## Stage 7AA — bounded RGBA8 GPU feedback codec for virtual textures (2026-11-18)

Code commit: 44dab9d947f51b62ec46827515135a6734f95785.
Branch: feature/texture-rhi-unification, PR #21 Draft.
New file: engine/gfx/VirtualTextureFeedback.h.

Implementation: Defines a portable *opt-in* 32-bit virtual page ID
stored as four RGBA8 UNORM bytes: x/y use 11 bits each, mip uses
five bits, virtual layer four bits, and the MSB marks valid pixels.
Zero is an invalid/cleared pixel. CPU
encodeVirtualTextureFeedback/decodeVirtualTextureFeedbackId
roundtrip this layout. Bounded VirtualTextureFeedbackDecoder
accepts only exact packed linear RGBA8 GPU readback data,
validates virtual tile bounds and format, skips invalid pixels,
deduplicates pages and ranks them by observed pixel frequency.
The decoder converts page coordinates to normalized
VirtualTextureVisibleRegion rectangles for the existing
UV prefetch planner, retaining source mip/layer and priority.
Shader snippets kVirtualTextureFeedbackGLSL and
kVirtualTextureFeedbackMSL encode the same integer bit layout.

Source limits: 2048x2048 mip-zero tile grid, <=31 mip levels,
<=16 virtual layers, 16MiB max readback pixels (config limit),
65536 max unique page records, bounded output regions.
This is a feedback codec + shader helper, NOT a registered
render pass, GPU feedback camera or automatic readback loop.
App must render the helpers to unblended non-sRGB RGBA8
and call Device::readTextureAsync, then feed decoded regions
to prefetchVisibleVirtualPages. Its callback still executes on
the graphics-device owner thread in a later frame.

Compatibility: CPU/GLSL/MSL header only; no change to
gfx::Device ABI, RHI backend, ECS, save/network or asset format.

Verification: GitHub source commit succeeded. C++ compilation,
CTest, GLSL/MSL shader compilation, GPU write/readback accuracy
and native platform tests NOT RUN.

Next: Add CPU codec roundtrip, malformed readback, multi-layer
and frequency priority regression tests; register in CMake and
focused verifier, then add usage instructions to the agent skill.
Run native GPU feedback render/readback before release.

## Stage 7AB — virtual texture feedback CPU regression test and verifier (2026-11-18)

Source commits: cd96dede3aa5cac94d9743dfdb07fcecbd3951af
(fixture, CMake registration, focused test verifier),
5861334b1f6c0a08b4bbef9582dfb45fddf1b5a9
(invalid sentinel assertion uses VirtualPageKey.mip rather
than nonexistent VirtualPageKey::valid()).
Branch: feature/texture-rhi-unification, Draft PR #21.
Files: tests/render/virtual_texture_feedback.cpp,
CMakeLists.txt and tools/verify-texture-rhi.sh.

New GPU-independent CTest aiverse_virtual_texture_feedback_test /
virtual_texture_feedback covers packed 32-bit feedback ID
roundtrips, invalid zero sentinel and out-of-range IDs,
frequency-based deduplication, bounded RGBA8 framebuffer
readback decoding, output UV rectangle priority and mip/layer
preservation, parent-mip-first integration with the generic
VirtualTexturePrefetchPlanner, refusal of sRGB or truncated
readback results, finite unique-page and pixel count limits,
and rejecting unsupported virtual-layer metadata.
Verification tool adds this target and filter in both
ordinary and optional Vulkan runs.

Compatibility: test registration and fixture only; runtime
codec remains Stage 7AA. No ECS, save/network or backend API
changes in this stage.

Actual verification: GitHub code+fix commit succeeded.
CMake configure NOT RUN, C++ compile NOT RUN, CTest NOT RUN,
real feedback shader GPU render/readback NOT RUN.
No claims of passing native tests.

Next: Document render-to-linear-RGBA8 feedback usage in
the native texture agent skill, and connect the decoder to
the regular device readTextureAsync call in an opt-in
owner-thread helper with safe callback ownership. Do not
merge without actual Metal/GL/Vulkan shader and CTest checks.

## Stage 7AC — async GPU feedback-to-disk prefetch runtime facade (2026-11-18)

Code commit: 8bef3d8085bbb327818f3123d1dc847fea34c0e2.
Branch: feature/texture-rhi-unification, PR #21 Draft.
New file: engine/gfx/VirtualTextureFeedbackRuntime.h.

Implementation: Header-only VirtualTextureFeedbackRuntime composes
the Stage 7AA RGBA8 page feedback decoder, Stage 7V CPU UV
prefetch planner and Stage 7X disk-backed request adapter.
initialize(Device&,feedbackConfig,prefetchConfig) validates
consistent virtual tile geometry and bounded decoder budgets.
capture(TextureHandle,TextureReadbackDesc) only requests the
existing optional asynchronous RHI readback, outside encoders.
The callback owns a weak_ptr to shared decoder state, never
captures the helper's this pointer, and can safely be dropped
after shutdown. A second capture is refused while the prior
request is pending or its decoded result awaits consumption.
submit(VirtualTextureFileStreamer&) sends decoded visible tile
requests to the normal disk staging queue on its owner thread;
the caller independently invokes streamer.pump() to issue GPU
uploads and publish page-table mappings.

Explicit limits: app must render a linear non-sRGB RGBA8 feedback
texture with shader IDs and no blending/tone mapping. No
implicit camera pass, scene shader insertion or GPU compute
feedback pipeline is created. RHI backends still require
actual asynchronous readback support. There is no blocking
GPU wait or cross-thread Device call in this facade.

Compatibility: Pure optional native header, no change to Device
virtual API, ECS authority, save schema, network, gameplay
or existing backend render paths.

Actual verification: GitHub file commit succeeded.
C++ compiler NOT RUN; CTest NOT RUN; real asynchronous
Metal/GL/Vulkan feedback rendering/readback NOT RUN.

Next: Add a CPU/mock RHI test that simulates deferred completion,
device refusal, second-pending capture, shutdown with a stale
callback, and visibility submission to the file streamer.
Update native texture skill/AGENTS.md, and keep PR #21 Draft
until full native verification.

## Stage 7AD — deferred feedback callback and disk prefetch integration fixture (2026-11-18)

Source test commit: 1b64a42d7bb279ce9a8f900db9ffb67649118395.
Branch: feature/texture-rhi-unification, PR #21 Draft.
File: tests/render/virtual_texture_file_streamer.cpp.

Expanded the existing virtual_texture_file_streamer CTest with
VirtualTextureFeedbackRuntime in a mock-RHI environment.
MockGpu::readTextureAsync stores a real deferred callback;
a synthetic RGBA8 target emits the same visible detail page
twice. Runtime capture refuses duplicates before completion,
then decodes the next-frame callback and submits only the
coarsest and intermediate physical disk pages within its
per-frame request limit. Consuming a decoded result twice
does not create duplicate disk requests. A second pending
callback is invoked after runtime.shutdown and must find
only an expired weak state, preventing use-after-free.
Device rejection is recoverable; a returned sRGB feedback
target fails closed; mismatched virtual layer configs are
rejected before an async request is sent. GPU resources and
the disk worker are released by the existing streamer.

Compatibility: No RHI backend or production header changes in
this test stage. The runtime facade remains Stage 7AC;
this fixture uses the existing CMake/CTest target.

Actual verification: GitHub test commit succeeded.
C++ compile NOT RUN, CTest NOT RUN, Metal/GL/Vulkan feedback
shader render and asynchronous readback NOT RUN.
No passing native execution claimed.

Next: Register the runtime usage/feedback format restrictions
in the native texture skill and canonical docs. On a native
checkout compile and execute the prefetch/feedback/file-streamer
tests, then validate the GLSL/MSL encoding in a real graphics
feedback pass and compare exact readback IDs.

## Stage 7AE — native feedback shader/readback usage and agent routing (2026-11-18)

Docs/skill commit: 052f5171aa5429c6acbaf9b6d1d64abebab767a6.
Branch: feature/texture-rhi-unification; Draft PR #21.
Paths: skills/visuals/aiverse-texture-rhi/SKILL.md,
docs/texture-rhi.md and AGENTS.md.

Documentation now points all native game agents to
VirtualTextureFeedbackRuntime and VirtualTextureFeedbackDecoder
for opt-in visibility feedback. The skill explains the 32-bit
page encoding, non-sRGB RGBA8 feedback target requirements,
the required unblended/untone-mapped shader pass,
Device::readTextureAsync then later submit(fileStreamer)
and normal fileStreamer.pump() on the owner thread.
Runtime shutdown uses weak callbacks and does not block the
GPU; no explicit camera/render pass/material-shader integration
is claimed. AGENTS.md keeps this presentation-only and
genre independent. Both docs and skill include
aiverse_virtual_texture_feedback_test and CTest
virtual_texture_feedback in focused native verification.

Compatibility: Documentation and agent guidance only.
No runtime C++, ECS, save/network, shader-graph or disk format
changed at this documentation stage.

Actual verification: GitHub doc commit succeeded.
Native compiler NOT RUN, CTest NOT RUN, shader compilation
and GPU readback tests NOT RUN. PR #21 remains Draft.

Next: Source-review and compile the newly introduced prefetch
and feedback CPU fixture targets on a complete native checkout,
fix errors, then validate an actual RGBA8 feedback pass on
Metal/GL33/GLES3/Vulkan and confirm integer-ID fidelity.
WebGL2 still needs a separate portable path.

## Stage 7AF — prevent resident coarse levels from starving fine mip prefetch (2026-11-18)

Code commit: c5de29e27ad178b10364ec323dde969bc495712c.
Branch: feature/texture-rhi-unification, Draft PR #21.
Changed file: engine/gfx/VirtualTexturePrefetch.h.

Bug: prior planner counted all coarse candidate keys against
maxCandidates before checking exact residency/pending status.
A large fully-resident ancestor range could fill the cap and
prevent the desired finer mip tiles from being inspected,
causing repeated no-progress prefetch frames.

Fix: the exact-resident/pending callback is now applied once
per unique key while enumerating tiles. Resident or pending
keys are tracked separately and do not consume the nonresident
candidate budget. Added maxExaminedPages (default 16384)
to cap total visited unique tiles/callbacks even if all
coarse keys are resident. Stats now includes examinedPages;
alreadyResident counts unique visited skipped tiles.
Existing priority, pin promotion, sorted emission and
maxRequestsPerFrame semantics remain unchanged.

Compatibility: additive config/stats field and internal CPU
planner behavior only. No RHI Device ABI, save/network, ECS,
GPU render targets, shader or asset format changes.

Actual verification: GitHub code update succeeded.
C++ build NOT RUN, CTest NOT RUN, GPU NOT RUN.
Next: Extend virtual_texture_prefetch CTest to verify a
maxCandidates=1 plan can still request the only missing
fine tile beneath two already resident coarse ancestors,
and verify maxExaminedPages caps fully-resident full-screen
coverage. Append separate test-source handoff.

## Stage 7AG — regression for absent fine tile beneath resident coarse mips (2026-11-18)

Code/test commit: 7bc72f9cfc685ab8aecde0e0722ab16c1b0404c7.
Branch: feature/texture-rhi-unification, PR #21 Draft.
File: tests/render/virtual_texture_prefetch.cpp.

New GPU-independent test residentCoarseDoesNotStarveFine:
a 4x4 tile grid with 3 mips and one nonresident-candidate
slot has its two coarse ancestors reported as already resident.
The planner must skip these ancestors during enumeration
and issue the one missing mip0 page (3,3), with exactly
three unique lookups, two skipped resident pages and no
candidate-cap blockage. Tightening maxExaminedPages to two
must stop before the fine page and set candidateLimitReached
without performing a third callback. A zero lookup budget
is rejected during planner initialization.

The existing CMake/CTest target virtual_texture_prefetch
is unchanged and now includes this regression source.
No runtime, RHI backend, ECS or disk format changes.

Actual verification: GitHub test-source commit succeeded.
Native C++ compilation NOT RUN, CTest NOT RUN, GPU NOT RUN.
Test asserts are source-level only, not passing evidence.

Next: Update native texture documentation with the separate
maxExaminedPages CPU safety bound. Then compile/run the
focused verifier on a full source checkout and correct
actual failures. Keep PR #21 Draft pending driver checks.

## Stage 7AH — document separate prefetch CPU lookup and candidate budgets (2026-11-18)

Documentation commit: 73981a02610e49b5edcad0eaa9d5e2f4baf9b84e.
Branch: feature/texture-rhi-unification, PR #21 Draft.
Files: skills/visuals/aiverse-texture-rhi/SKILL.md,
docs/texture-rhi.md.

Changes: The native visible-UV usage example configures
maxExaminedPages alongside maxCandidates. Architecture
documentation explicitly distinguishes a bounded number
of all unique page/residency lookups (including already
resident or queued mip ancestors) from the budget for
nonresident pages that still need uploads. This captures
the Stage 7AF no-starvation scheduling correction and
prevents agents from treating the two caps as equivalent.

Compatibility: Documentation-only; no production code, ECS,
serialization, disk layout or network change.

Actual verification: GitHub doc commit succeeded.
Native C++ compile, CTest, skill-catalog audit and GPU tests
NOT RUN. PR remains Draft.

Next: Native build/test all texture RHI sources and relevant
editor + real Metal/GL/Vulkan feedback shader readbacks.
Record exact tested HEAD and fix compiler/behavior errors.
Do not claim the source as production-ready until validation.

## Stage 9A — merge current main into Texture RHI branch (2026-11-18)

Merge commit: `2dc38b95d7525786c98b966113267047666a0290`.
Merge parents: Texture RHI prior HEAD `986310c60cadd9e1a6a74897a813c70c99408f46`
and main `9ee6ff6fbf390bd45363bc92c838f492dfe92eee`.
Working branch: `feature/texture-rhi-unification`, Draft PR #21.

Action: fetched GitHub's exact main and branch heads, compared the
common ancestor `dd711f5e2a7f66ed25bd7330875e6e87b7109e55`
to both branches and verified main introduces 48 changed paths,
only AGENTS.md and CMakeLists.txt overlapping Texture RHI changes.
Built a true, two-parent Git merge commit on GitHub with the latest
main blob SHAs/modes for the other 46 paths, without force-push.
AGENTS.md retains the Texture RHI agent contract and adds
main's InputFlow continuation policy, ownership table row and
Enhanced Input handoff section. CMakeLists.txt keeps all Texture
RHI CTest additions and adds main's AIVERSE::InputFlow links
for Android client, desktop client and editor.

Source changes: most main InputFlow/client/editor changes are
integrated without modifying them; no Texture RHI feature files
were overwritten. Main and feature commit ancestry are now
both represented as parents of the merge.

Actual verification: GitHub merge tree/commit/ref write succeeded
with expected head lease and without a force push. Full local
Git merge command NOT RUN (local container cannot resolve github.com).
Native CMake build NOT RUN; CTest NOT RUN; GPU tests NOT RUN;
input/texture joint feature execution NOT RUN. This stage must
not be mistaken for a successful build or resolved runtime conflict.

Next: verify compare main...feature reports zero commits behind,
then proceed with texture feature source work only after the merge.
Run full native compilation/tests and check InputFlow/Texture RHI
integration before marking the Draft PR ready for merge.

## Stage 7AI — owned linear RGBA8 virtual texture feedback pass (2026-11-18)

Source commit: 156c82a3db86bae26a645e9ca3634fbb5201ea5b.
Branch: feature/texture-rhi-unification, Draft PR #21.
New public header: engine/gfx/VirtualTextureFeedbackPass.h.
Main integration merge parent: 2dc38b95d7525786c98b966113267047666a0290.

API: VirtualTextureFeedbackPass::initialize(Device&,width,height,
VirtualTextureFeedbackConfig,VirtualTexturePrefetchConfig) allocates
a single-sample single-mip Texture2D RGBA8 UNORM offscreen color
target, and validates pixel budget/descriptor/backend capabilities.
begin() clears the buffer to zero (invalid feedback ID), opens
the standard RenderTargetSet render pass and configures a matching
viewport/scissor. The caller supplies a fragment shader writing
the Stage 7AA 32-bit packed virtual-page IDs, one opaque color
output and no sRGB/MSAA/blending/postprocess. endAndCapture()
closes the pass and requests the existing nonblocking
Device::readTextureAsync callback. submitVisiblePages(fileStreamer)
forwards decoded UV visibility through the pre-existing
feedback runtime and disk-backed prefetch adapter in a later
graphics frame. This helper never starts a graphics frame,
does disk I/O, or writes ECS/network/save state.

Lifetime: fully owns the temporary feedback render target;
shutdown invalidates runtime's weak callback state and frees
target via RenderTargetSet. It refuses to start a new capture
when the previous async request is pending or awaits consumption.
Device must outlive this owner. Unsupported RHI async readback
fails explicitly instead of implying a successful feedback pass.

Compatibility: additive, opt-in header-only C++ API. Existing
graphics render paths, shader graph and gameplay are unchanged.
Open issues: no renderer/camera invokes this helper automatically;
shader compilation/feedback render fidelity remain unverified.

Actual verification: GitHub file commit succeeded. Native C++
compile NOT RUN, CTest NOT RUN, real Metal/GL/Vulkan GPU NOT RUN.
Next: add deterministic mock-device tests for allocation rollback,
clear state, begin/end/capture ordering, async completion,
unsupported backend and weak callback destruction. Register
the test in CMake and focused Texture RHI verifier before
integrating in a live renderer.

## Stage 7AJ — standalone feedback render pass/mock-device test (2026-11-18)

Code/test commit: dce980b82c3eb08a1e401261bcfdad60c289e253.
Branch: feature/texture-rhi-unification, Draft PR #21.
Files: tests/render/virtual_texture_feedback_pass.cpp,
CMakeLists.txt, tools/verify-texture-rhi.sh.

Test source: aiverse_virtual_texture_feedback_pass_test /
CTest virtual_texture_feedback_pass uses a strict mock
gfx::Device. It checks a bounded, single-sample, single-color
linear RGBA8 texture is allocated with no extra attachments,
color pass clears all channels to zero, viewport/scissor
match the target, a nested begin is rejected, and a capture
can only start after endPass. A deferred callback completes
the packed RGBA8 page decoder and prevents another begin
until the result is consumed. Device rejection, invalid pixel
budget, incompatible feedback/prefetch configs and texture
allocation failure must fail without leaks or active passes.
A late callback after feedback pass shutdown holds only
the runtime's weak state and is ignored safely.

Registered the target in normal CMake and the separate
focused Texture RHI verifier, including optional --vulkan
filter. No actual native graphics driver is involved in
these mock tests; they do not validate shader pipeline
color formats, integer precision or GPU readback fidelity.

Compatibility: Test source and registration only; runtime
code remains Stage 7AI; no gameplay/ECS or save changes.

Actual verification: GitHub test/tree/ref commit succeeded.
CMake configure NOT RUN; C++ compile NOT RUN; CTest NOT RUN;
Metal, GL/GLES and Vulkan feedback rendering NOT RUN.
No passing tests claimed.

Next: Document the public VirtualTextureFeedbackPass API
and native renderer usage, then perform a real native build,
focused CTest and backend shader render/readback checks.
Do not merge PR #21 while those checks remain pending.

## Stage 7AK — native render feedback pass skill and architecture handoff (2026-11-18)

Docs/skill source commit: ff078bfc20e64514f78ea72b0bbfa90e9964e8c4.
Branch: feature/texture-rhi-unification, Draft PR #21.
Paths: skills/visuals/aiverse-texture-rhi/SKILL.md,
docs/texture-rhi.md, AGENTS.md.

Changes: Public API recipe for VirtualTextureFeedbackPass
owns a single RGBA8 UNORM offscreen color attachment,
clears invalid feedback pixels, then delegates rendering
to caller-owned opaque feedback shaders (one color output),
requests asynchronous texture readback at endAndCapture,
and uses submitVisiblePages plus fileStreamer.pump on
later frames. The native agent skill and AGENTS contract
direct game developers to use the shared, genre-independent
helper instead of creating one-off camera/terrain/RTS managers.
Document no depth/MSAA/sRGB/blending/dithering/postprocess
and the readback limitations; no actual camera draw, shader
compilation or RenderGraph registration is claimed.
Add aiverse_virtual_texture_feedback_pass_test and CTest
virtual_texture_feedback_pass to native verification instructions.

Compatibility: Documentation only. No graphics backend source,
save/network, ECS simulation or game genre code changed.

Actual verification: GitHub docs/tree/ref commit succeeded.
C++ build NOT RUN, CTest NOT RUN, GPU shader/render NOT RUN,
skill catalog audit NOT RUN. No CI status declared passed.

Next: Run source builds and CPU CTests with the merged main
InputFlow targets, then validate feedback drawing and packed ID
readback on Metal/GL33/GLES3/Vulkan. Repair actual failures
before marking PR ready and keep this append-only handoff up to date.

## Stage 8A — native libktx Basis/UASTC adapter (2026-11-18)

Source commit: 0533b36ee19729f38172ebb1357208aa673030b1.
Branch feature/texture-rhi-unification; Draft PR #21.
New header: engine/render/Ktx2Libktx.h.
Provides makeLibktxBasisTranscoder(Device&,options) and
uploadKtx2WithLibktx, using actual Khronos libktx decode API
when compiled with AIVERSE_TEXTURE_WITH_LIBKTX and linked.
Codec is NOT vendored; disabled mode fails closed with clear error.
Selects BC7 -> ASTC 4x4 -> ETC2 RGBA -> BC3 based on
Device::supports for correct sRGB/linear native formats,
or uncompressed RGBA32 (RGBA8 texels) fallback.
Uses libktx mip/layer/face offset functions to pack full
mip-major array/cube/volume output and bounded 512MiB memory.
Original KTX topology and decoded payload sizes are also
verified by existing parseKtx2WithTranscoder code.
Explicit current limitation: UASTC HDR targets requiring
RGBA16F/BC6H are not supported by this LDR output adapter.
No CMake option/linkage added yet in this stage; next stage
must add explicit opt-in dependency wiring and tests.
Compatibility: opt-in header only, no existing RHI/ABI, save
or ECS format changed.
Verification: GitHub source commit succeeded; libktx headers
not available locally, C++ compile NOT RUN, libktx runtime
test NOT RUN, GPU upload validation NOT RUN.
Next: CMake optional libktx target, smoke/negative fixtures,
agent skill and production native build with libktx installed.

## Stage 8B — opt-in system libktx CMake integration (2026-11-18)

Code commit: 4f54f932f933f36423f6def2d8ee7f2b62ac9d8d.
Files: CMakeLists.txt, tests/render/ktx2_transcode.cpp.
New CMake option AIVERSE_TEXTURE_WITH_LIBKTX=ON explicitly
requires installed ktx.h and libktx, wires both into
aiverse_runtime as public interface dependency and existing
aiverse_ktx2_transcode_test. OFF is default and has no
additional package dependency. Test source includes
Ktx2Libktx.h and checks default options; if enabled, checks
compiler sees the real ktxTexture2 API. Existing bridge
synthetic fixture is preserved.
Compatibility: opt-in CMake and test-only; OFF behavior unchanged.
Verification: GitHub tree/commit succeeded; CMake configure
NOT RUN (GitHub repository clone unavailable from this container),
C++ compile NOT RUN, real libktx runtime decode NOT RUN.
Next: Add real ETC1S/UASTC test assets (licensed), compile with
AIVERSE_TEXTURE_WITH_LIBKTX=ON and validate format selection
on BC7/ASTC/ETC2 and RGBA8 devices. Document dependency.

## Stage 8C — Vulkan compute shader SPIR-V compiler (2026-11-18)

Code commit 0bd7e79e0e62efd47c28710cfe225665869ce582.
Files: engine/gfx/vulkan/VkShaderCompiler.h and .cpp.
Adds compileComputeProgram(glslBody,spv,error), using
glslang::TShader(EShLangCompute) and TProgram link, with
Vulkan 1.1 SPIR-V 1.3 target and explicit GLSL450 input.
Automatically adds default workgroup local_size_x=64
when the source does not declare it. Source capped to 1MiB;
empty/invalid compilation fails closed with diagnostics.
Compatible binding convention documented in header:
storage images 0..3, sampled textures 4..11, storage
buffers 12..15, optional uniform buffer 16.
This stage only adds compiler source and API declaration;
native Vulkan pipeline/device support is the NEXT stage.
Verification: GitHub source/tree/ref commit succeeded.
glslang compiler NOT RUN, Vulkan graphics build NOT RUN.
No native or driver tests claimed passed.
Next: Add Vulkan compute resource/pipeline/descriptor/dispatch
backend, appropriate VkApi loader commands and storage-image
capability and memory barriers; document fallbacks.
## Stage 8D — Vulkan compute and storage allocation scaffolding (2026-11-18)

Code commit: 45056f91b0df88315afb79f19c4abf688ede038f.
Paths: VkApi.h, VkDevice.h, VkResources.cpp, GfxVulkan.cpp.
Added Vulkan compute entry points (vkCreateComputePipelines,
vkCmdDispatch), VulkanDevice public compute overrides and
VCompute resource pool, queue-family compute capability
selection, and cleanup for owned compute resources.
Storage texture allocation now conditionally enables
VK_IMAGE_USAGE_STORAGE_BIT, advertises storage in GPU
resource inventory, and checks RGBA8/RGBA16F 2D/2DArray/3D
against VkFormat STORAGE_IMAGE and SAMPLED_IMAGE features.
Reject unsupported storage usages rather than silent fallback.
Status: INCOMPLETE until next code stage implements all new
VulkanDevice compute method bodies in VkCompute.cpp and
adds that file to aiverse_gfx_vulkan source target. Do not
compile this intermediate HEAD; missing method definitions
would cause a linker failure. No change to GL/Metal behavior.
Verification: GitHub commit/ref update only; no native build
or GPU test RUN. Next: complete shader/pipeline/descriptors
dispatch, read/write layout barriers, ownership cleanup;
then compile and fix native tests.

## Stage 8E — Vulkan compute pipeline + synchronized storage images (2026-11-18)

Code commit: e47e11eeb8213204f0305bcbbc569c75bc87bc84.
Files: new engine/gfx/vulkan/VkCompute.cpp and CMakeLists.txt.
Adds real VulkanDevice::createCompute/destroyCompute/beginCompute/
setComputeBuffer/setComputeTexture/bindComputeStorageTexture/
setComputeUniforms/dispatch/endCompute definitions, completing
Stage 8D backend virtual methods. CMake compiles VkCompute.cpp.
Compiles GLSL compute via Stage 8C, creates VkComputePipeline
with descriptors set0 binding0..3 STORAGE_IMAGE, 4..11
COMBINED_IMAGE_SAMPLER, 12..15 STORAGE_BUFFER, 16 UBO.
Descriptor pools are bounded, one dispatch per pool and
deferred until the completed GPU frame serial. A 4KB max
uniform staging buffer is GPU-owned until completion.
dispatch uses 64 X threads per group, validates GPU group
counts, checks read/write alias, transitions storage images
SHADER_READ_ONLY->GENERAL and back with conservative
all-commands memory barriers. GPU queue must expose compute
and storage formats RGBA8/RGBA16F, 2D/array/3D.
Limitations: this is a constrained Vulkan shader resource
binding ABI, not automatic translation of legacy Metal
ComputeDesc.msl. GLSL source must use the documented
Vulkan binding layout; 4 writable image slots, 8 sampled
slots and 4 storage buffers. No transparent emulation in GL.
Compatibility: additive Vulkan backend; other RHI unchanged.
Verification: GitHub commit/ref succeeded. Native Vulkan SDK
compiler/glslang compile NOT RUN; real GPU descriptor validation
NOT RUN. Explicit PASS cannot be claimed.
Next: add Vulkan compute tests with GLSL shader writing
rgba8/rgba16f, readback exact pixel values and verify barriers.
Review compile + device limits before releasing.

## Stage 8F — optional external OpenEXR compressed HDR reader (2026-11-18)

Code commit: d2a17d7ebb41e08250d9b5ac6eb46cd03629f1e5.
New header: engine/render/ExrOpenExr.h.
Adds parseOpenExrWithLibrary and loadOpenExrWithLibrary.
With AIVERSE_TEXTURE_WITH_OPENEXR, uses real OpenEXR 3.x
RgbaInputFile on an immutable bounded memory IStream.
Reads original dataWindow and RGBA half pixels, supports
ZIP/ZIPS/PIZ and other formats OpenEXR library handles,
including first-level tiled sources where supported.
Converts to existing RadianceImage linear RGBA16F and
rejects invalid window sizes or nonfinite channel values.
Without dependency macro uses existing narrow uncompressed
parser, not a fake compressed decoder. No external codecs vendored.
Known: this is currently header-only; NEXT stage must add
optional OpenEXR 3 CMake dependency and a real compressed
EXR regression fixture. Multipart/deep EXR remain unsupported.
Compatibility: opt-in native reader; no renderer ABI or save
format change. Verification: GitHub source commit only,
native OpenEXR headers/compile/test NOT RUN.
Next: wire CMake, add synthetic OpenEXR-generated ZIP/PIZ
test, verify fixture and real OpenEXR native decoding.

## Stage 8G — OpenEXR 3 optional CMake and ZIP/ZIPS/PIZ fixtures (2026-11-18)

Code commit: 3ea20363b15909316fce92f7742c7e07418c8e43.
Files: CMakeLists.txt, engine/render/ExrOpenExr.h,
tests/render/exr_texture.cpp.
Adds AIVERSE_TEXTURE_WITH_OPENEXR=ON opt-in to find
OpenEXR 3 imported OpenEXR::OpenEXR, link runtime and
EXR fixture with public macro. OFF default stays dependency-free
and keeps the original narrow uncompressed parser.
Native test, only when enabled, uses installed library to
WRITE three small RGB[A] EXR files (ZIP/ZIPS/PIZ) and
decode them using parseOpenExrWithLibrary; asserts
linear HALF RGBA including negative blue and alpha.
Explicit includes stdexcept/cstdlib added to the external
header. No external library vendored into repo.
Limitations: tests are source fixtures, not executed.
Multipart/deep EXR still unsupported; uncompressed narrow
import remains the default without system library.
Compatibility: opt-in CMake only; no default build ABI changes.
Verification: GitHub code/tree/ref commit succeeded,
native CMake configure NOT RUN, OpenEXR test NOT RUN,
real image GPU sampling NOT RUN.
Next: run CMake/CTest with system OpenEXR 3 on developer machine,
check PNG/EXR color handling in final renderer and audit
library dependency packaging for app distribution.

## Stage 8H — 3D texture mip-slice GPU readback and inspector (2026-11-18)

Code commit: f694c31dad3f67dca7cb48a0841154c297616142.
Files: engine/gfx/{gl/GfxGL.cpp,metal/GfxMetal.mm,
vulkan/VkResources.cpp}, apps/editor/windows/TextureResourcesWindow.cpp.
Metal readTextureAsync now accepts Texture3D with each
mip depth slice selected via request.arrayLayer; 3D blits
use sourceSlice=0 and sourceOrigin.z. GLES3/GL33 attaches
the requested 3D slice with glFramebufferTextureLayer
to an intermediate PBO/fence readback framebuffer.
Vulkan VkBufferImageCopy now supports Texture3D with
imageSubresource baseArrayLayer 0 and imageOffset.z for
the requested mip-slice. Existing 2D/arrays/cubes unchanged.
Inspector adds per-mip depth slice slider and enables
bounded 16MiB async readback previews for 3D textures.
Textures with compressed/depth/MSAA/unsupported formats
still reject. 3D readback is presentation-only.
Compatibility: additive readback capability, no ECS,
asset format, shaders, serialization, net changes.
Verification: GitHub code/commit succeeded. Metal/GL/Vulkan
native build NOT RUN, readback-on-device NOT RUN, editor
preview UI NOT RUN, no real GPU confidence claimed.
Next: add readback per-backend fixtures and GPU integration
tests for z-slices/mip, verify row orientation and upload
format across platforms. Existing WebGL2 still refuses native
asynchronous readback.

## Stage 8I — focused Texture RHI GitHub Actions (2026-11-18)

CI source commit: 528263eb70544fceedf632599d8f1354bb0a53d7.
New file: .github/workflows/texture-rhi.yml.
Adds opt-in workflow_dispatch and path-filtered pull_request
workflow with concurrency cancellation; Linux CPU matrix
OpenEXR OFF/ON and separate Vulkan compile + lavapipe
test job, uploaded configure/build/CTest logs.
Runs the existing tools/verify-texture-rhi.sh with
the bounded native test list, -DAIVERSE_BUILD_CLIENT=OFF,
editor/server/tools/headless OFF, Vulkan ON in Vulkan job.
Vulkan success is explicitly NOT a real hardware PASS
unless CTest logs show actual driver execution.
Compatibility: CI-only YAML, no source/gameplay changes.
Actual verification: GitHub YAML commit succeeded; workflows
NOT RUN or confirmed successful yet; existing repository
CI mostly queued/cancelled at recent heads.
Next: inspect actions for this exact HEAD, download logs
when a job completes, fix observed native compile/test failures,
then evaluate real Metal/GLES3 GPU tests and reviewer gates.

## Stage 8J — bounded cross-frame deferred GPU feedback visibility (2026-11-18)

Code commit: a88ddd4bb5e74dade0cc90ebcfb3b50dd94e8fa9.
File: engine/gfx/VirtualTextureFeedbackRuntime.h.
After a successful asynchronous page-ID feedback capture,
submit(fileStreamer) now retains the corresponding generic UV
visibility regions across several graphics frames (default
maxDeferredFrames=8, allowed 1..1024). Subsequent calls issue
missing fine-detail pages once earlier coarse pages become
resident or pending. Fresh feedback replaces old visibility
rather than accumulating stale camera requests.
The coordinator releases regions once no candidate or
request-budget deferral remains, on retry-limit exhaustion,
or shutdown. No new GPU capture is required for every
deferred detail page. Device owner thread and existing
streamer/prefetch bounds are unchanged.
Compatibility: optional fourth initialize arg with a default;
existing callers compile without changes. No ECS, serialization,
network, shaders or RHI Device virtual API changes.
Verification: GitHub source commit succeeded. Native C++ build
NOT RUN; virtual_texture_file_streamer CTest NOT RUN; GPU NOT RUN.
KNOWN FOLLOW-UP: existing runtime integration test still assumes
submit() consumes all visibility in a single call. The next
stage MUST update that fixture to expect second-frame fine
mip submission and third-frame no-op, and consider retry cap.
Do not claim passing tests at this intermediate HEAD.

## Stage 8K — deferred feedback multi-frame native fixture correction (2026-11-18)

Code/test commit: b30eff59566988ba4596c401e8979b1e204ac980.
File: tests/render/virtual_texture_file_streamer.cpp.
Adjust the mock GPU/disk end-to-end fixture for Stage 8J.
First runtime.submit still queues coarse and middle mip
within maxRequestsPerFrame=2. Second submit on next frame
now queues exactly one fine mip without another GPU readback.
A third call must not issue duplicate requests. This tests
the intended retained bounded visibility and distinguishes
consuming the feedback result from completing the page work.
Compatibility: test-only; production source Stage 8J unchanged.
Verification: GitHub test commit succeeded, native compile
NOT RUN, CTest NOT RUN, real GPU feedback NOT RUN.
Next: test maxDeferredFrames edge and freshness of a replaced
view, then document the retry semantics. Native CI and GPU
tests remain mandatory before release.

## Stage 8L — Vulkan compute GLSL and real GPU storage-image fixture (2026-11-18)

Code/test commit: e38b4109f2899a64d3649afdeaa1500556065554.
Path: tests/render/vulkan_tests.cpp.
Extended the Vulkan test suite with real compileComputeProgram
valid/invalid GLSL checks (runs even when device is skipped).
When an offscreen Vulkan device and RGBA8 storage capability
are present, the GPU test creates a 4x4 storage texture,
compiles a compute pipeline using binding0 rgba8 writeonly
image2D, dispatches four rows of 64 X lanes with bounds
checks, then records asynchronous texture readback in the
same frame after the pipeline has completed. Subsequent
frames must receive a callback and exactly 64 RGBA8 bytes
matching (255,64,128,255) to one byte tolerance.
Explicitly rejects BC7 storage image capability.
The test always destroys its compute program and image.
Missing device/format prints a SKIPPED marker, not GPU PASS.
No engine runtime or public API changes in this stage.
Verification: GitHub source commit succeeded. glslang compilation,
CTest, Vulkan driver storage readback NOT RUN. No PASS claims.
Next: run `ctest -R vulkan_backend` with Lavapipe and a real
driver, inspect SKIPPED lines, fix any shader/pipeline errors.

## Stage 8M — native agent integration for optional codecs, Vulkan compute and 3D previews (2026-11-18)

Documentation source commit: 688bf554854770301c6d7be7d9ea07a348b72357.
Files: docs/texture-rhi.md, skills/visuals/aiverse-texture-rhi/SKILL.md, AGENTS.md.
Document system libktx and OpenEXR 3 opt-in build flags;
point agents at Ktx2Libktx.h and ExrOpenExr.h rather than
reimplementing decoders. Real LDR ETC1S/BasisLZ/UASTC
transcoder supports capability-selected BC7/ASTC/ETC2/BC3
or RGBA8 fallback. UASTC HDR remains separate/unimplemented.
OpenEXR external package decodes ZIP/ZIPS/PIZ and more
first-part RGBA scanline sources, not deep/multipart.
Describe Vulkan GLSL450 compute shader binding ABI:
set0 storage images 0..3, sampled images 4..11, storage
buffers 12..15, UBO 16, guarded RGBA8/16F storage.
Document limited GL/GLES/WebGL2 compute support, native
Texture3D mip-depth slice inspector readback, and
bounded cross-frame retry when GPU feedback exceeds quota.
Register new path-filtered Texture RHI CI workflow and
the difference between queued/skipped tests and GPU PASS.
No engine/runtime source or serialization changed at
this documentation-only stage.
Verification: GitHub docs commit succeeded. Skill catalog
audit NOT RUN; native build/CTest NOT RUN; Vulkan GPU/Metal/
GLES and OpenEXR/libktx runtime not run.
Next: review code for local/native compile failures,
consume CI logs once results are actually available;
fix issues with separate code and handoff commits.

## Stage 8N — optional UASTC_HDR -> RGBA16F full mip import (2026-11-18)

Source commit: 65f21b4a3dd7a7b48fb457e67207cd61db799e61.
File: engine/render/Ktx2Libktx.h.
Adds uploadKtx2UastcHdrWithLibktx(Device&,encoded,error),
separate from LDR CompressedImage / RGBA8 transcoding.
Enabled only with system Khronos libktx. Validates KTX2
header/mip index via existing inspect, verifies Basis HDR
and linear transfer metadata, then calls real libktx
TranscodeBasis(KTX_TTF_RGBA_HALF). It checks source
dimensions/layers/cube faces/mips after transcode.
Allocates RHI RGBA16F texture (2D/array/cube/3D as supported),
validates every libktx image offset and total 512MiB budget,
uploads every mip and face/depth slice using existing
Device::updateTextureRegion; destroys partially uploaded
GPU texture on any failure. No 8-bit downgrade or HDR
tone mapping. Without libktx macro it fails explicitly.
Unlike texture-pack writers it does not modify ECS/save data.
Known limitation: does not create native BC6H/ASTC HDR
compressed output; uses RGBA_HALF for compatibility.
Verification: GitHub code commit succeeded, libktx native
header/build NOT RUN, UASTC_HDR real file test NOT RUN,
RGBA16F Vulkan/Metal/GL GPU reads NOT RUN.
Next: dedicated HDR fixture with real UASTC_HDR encoded data
and GPU mock tests for mips, topology and unsupported codec;
update docs/AI skills for HDR API, and run libktx CI.

## Stage 8O — Vulkan compute async callback fixture lifetime fix (2026-11-18)

Test source commit: 27dada2769b9480377e431217b89c7e9b9604d14.
File: tests/render/vulkan_tests.cpp.
Earlier Stage 8L GPU readback test captured on-stack bool
and texture pixels by reference inside an asynchronous RHI
callback. When a GPU callback failed to arrive within the
bounded frame loop, test locals could leave scope before
the backend eventually delivered the callback (dangling
references, potential use-after-scope). Fixed by capturing
a shared_ptr<ReadbackState> by value; result remains owned
until the backend consumes or releases the callback.
No changes to GPU source or expected pixel assertions.
Verification: GitHub test commit succeeded; C++ compile
NOT RUN; Vulkan CTest/readback NOT RUN; no PASS claims.
Next: run Vulkan backend test with SDK and compute-capable
driver, inspect precise texture bytes and fix any failures.

## Stage 8P — UASTC_HDR decoder ABI and libktx dependency fixture (2026-11-18)

Test code commit: 4ea9117d44e981970295dc61a543ab9a7bbe2f55.
File: tests/render/ktx2_transcode.cpp.
Adds an actual typed function pointer instantiation for
uploadKtx2UastcHdrWithLibktx(Device&,bytes,error), ensuring
the newly added HDR entry point remains compilable from
a native consumer even when the codec macro is disabled.
When AIVERSE_TEXTURE_WITH_LIBKTX=ON the fixture checks
that the installed libktx development package exposes
ktxTexture2_TranscodeBasis, ktxTexture2_GetImageOffset and
ktxTexture2_IsHDR, in addition to the existing type check.
This test does NOT include production encoded UASTC_HDR
payloads; true half-texture content/GPU uploads still
need runtime test assets and a native library build.
Compatibility: source test-only; no renderer or save changes.
Verification: GitHub source commit succeeded; native
C++/libktx compile, CTest, HDR GPU output NOT RUN.
Next: check exact libc/SDK ABI on Linux/macOS/Windows
and add real UASTC_HDR texture fixture when a licensed
sample becomes available.

## Stage 8Q — UASTC_HDR agent guidance and limits (2026-11-18)

Documentation commit: e984b1b60773fb17d3aeb05e808accd700515814.
Files: docs/texture-rhi.md, skills/visuals/aiverse-texture-rhi/SKILL.md, AGENTS.md.
Record uploadKtx2UastcHdrWithLibktx as optional system
libktx KTX_TTF_RGBA_HALF path, with all mips, cube/array/
volume slices represented by linear RGBA16F RHI resources.
Warn that native BC6H/ASTC_HDR compressed targets remain
unsupported and that production HDR fixtures/real GPU tests
are not executed. Preserve default dependency-free import.
No runtime/ECS/network changes. Source docs GitHub commit
succeeded, native skill audit NOT RUN, GPU tests NOT RUN.
Next: run CI/test on native SDKs and verify half-pixel fidelity
and full mip topology. Keep PR #21 Draft until verified.


## Stage 8R — bounded feedback retry lifecycle fixture (2026-11-18)

Test source commit: 060cdb49cbf094cd943ab7e07f819a9412c11e05.
Changed file: tests/render/virtual_texture_file_streamer.cpp.
Extended asynchronousGpuFeedbackRuntime with a one-frame deferred budget
scenario, verifying that already pending pages remain deduplicated and
that subsequent submit calls do not issue new requests after expiration.
Also verifies initialize rejects maxDeferredFrames=0 and 1025, with
diagnostic errors. Existing normal 8-frame test still checks deferred
fine-mip request on the next submit and safe callback shutdown.

Scope: native mock-GPU CTest fixture only. No runtime, ECS, material,
serialization, or backend behavior changes.
Verification: GitHub commit succeeded. Native C++ build and CTest
NOT RUN because no local repository checkout and github.com DNS fails
in the available container. No GPU pass claimed.
Next: execute aiverse_virtual_texture_file_streamer_test and
ctest -R virtual_texture_file_streamer on a native checkout; fix
any compiler or assertion failures. Then add replacement-camera
visibility fixture and integrate GPU feedback capture into a real
renderer pass, validating Metal/GL/Vulkan separately.


## Stage 8S — retry fixture readback ownership correction (2026-11-18)

Test fix commit: 7f34beec5d9a05470eea1c6fa79ed5e6d6e9484b.
File: tests/render/virtual_texture_file_streamer.cpp.
The Stage 8R fixture reused a TextureReadbackResult after its pixel
vector had been moved into the stale-callback test. Replaced that reuse
with a newly initialized RGBA8 two-pixel feedback result encoding the
same fine-page ID, so the one-frame retry scenario is meaningful and
independent of moved-from vector state.

Verification: GitHub source commit succeeded. Native C++ compile,
CTest and GPU execution NOT RUN. This is not a passing-test claim.
Next: run the focused native test and then test replacement visibility
with a new camera region and distinct missing page keys; integrate
the feedback pass into an actual renderer only after validating its
render-thread and frame-order lifecycle.


## Stage 8T — camera visibility replacement regression (2026-11-18)

Test source commit: 1ce98a3823c28768a634294321a9d6691bb0565.
File: tests/render/virtual_texture_file_streamer.cpp.
Added cameraFeedbackReplacesDeferredVisibility with two distinct
one-pixel RGBA8 page IDs, emitted through MockGpu async completion.
The first camera view uses the two-request frame budget on coarse and
middle pages, leaving its fine page deferred. A second camera view is
captured and decoded before another submit; its dispatch must request
new visibility work without queuing the stale first camera's fine page.
The fixture also verifies owner shutdown releases GPU resources.
This tests the existing replacement behavior rather than adding
automatic camera capture or an engine render-pass hook.

Verification: GitHub commit succeeded; C++ compile, CTest and native
GPU integration NOT RUN. No visual or performance claim.
Next: execute focused native tests, add real renderer feedback draw
integration (opt-in, no genre dependencies), verify Metal/GL/Vulkan
render-to-readback byte fidelity and page-table updates, then assess
remaining backend capability and HDR import gaps.


## Stage 8U — opt-in feedback renderer callback integration (2026-11-18)

Source commit: 5dd9b58bf46ab249ea8a70f25f283b24d223d3a6.
Test commit: 2fdbdcb0989f52d690fad1456f8c1f9e9e9a63cc.
Files: engine/gfx/VirtualTextureFeedbackPass.h,
tests/render/virtual_texture_feedback_pass.cpp.
Added template renderAndCapture(DrawFeedback&&) to the existing
VirtualTextureFeedbackPass. The callback receives Device& inside
the bounded, cleared linear RGBA8 render target. On callback false,
the pass is ended without scheduling partial readback; on success,
endAndCapture submits asynchronous readback. Existing pending and
unconsumed feedback guards remain in effect. The mock-device fixture
tests callback order, success, false return, no partial capture and
rejection while prior visibility is awaiting consumption.

Usage: pass.renderAndCapture([&](gfx::Device& device) -> bool {
    // Bind a single-output RGBA8 opaque feedback pipeline and draw
    // packed page IDs from the active material/terrain visibility.
    return true; // only when all feedback draws were submitted
});
Later, call pass.submitVisiblePages(streamer) and streamer.pump().
No automatic camera/material/Terra integration is claimed.
Callback must not throw; the existing Device and pass contracts are
render-thread only. No ECS or genre-specific dependency introduced.

Verification: GitHub source/test commits succeeded. Native C++ build,
CTest, backend shader compilation and GPU readback NOT RUN.
Next: compile and run virtual_texture_feedback_pass and
virtual_texture_file_streamer; add real material/shader feedback
submission and per-backend GPU readback fidelity tests.


## Stage 8V — exception-safe feedback pass callback (2026-11-18)

Runtime source commit: c2e53674f9856576194f6fab1a13377c4ec53593.
Test source commit: c9692e050ccf98bde275dac2a17c84b5edc879e7.
Files: engine/gfx/VirtualTextureFeedbackPass.h and
tests/render/virtual_texture_feedback_pass.cpp.
renderAndCapture now wraps the caller-provided draw callback in
try/catch(...), ends the active RenderTargetSet pass on exception,
records a diagnostic and rethrows. This avoids leaving the Device
inside a render pass if the callback throws; false return still ends
without readback, successful return still schedules async capture.
The mock-device regression fixture exercises false, throwing and
successful callbacks in sequence, verifying end counts, no stale
readback and safe recovery.

Verification: GitHub commits succeeded. Native C++ compilation,
CTest and GPU execution NOT RUN; no passing tests claimed.
Compatibility: additive exception cleanup in the opt-in callback API;
no ECS, genre, serialization or native backend modifications.
Next: compile with the project's exception settings and run
virtual_texture_feedback_pass; connect a concrete shader/material
feedback draw and verify GPU pixel fidelity across Metal/GL/Vulkan.


## Stage 8W — material UV-to-page feedback shader helpers (2026-11-19)

Source commit: 17eff4e4d73a2e11776e07560da78ed27a949126.
Test source commit: 465a5c3c230d82481948dac6a9977d438fc96388.
Files: engine/gfx/VirtualTextureFeedback.h and
tests/render/virtual_texture_feedback.cpp.
Added CPU virtualTextureFeedbackPageAtUV(u,v,mip,layer,tilesX,tilesY)
and matching GLSL/MSL material helper snippets. These calculate
ceil-divided page counts at explicit mip, clamp UV=1 to the last
page, reject invalid UV/topology and emit the existing 32-bit
packed RGBA8 feedback ID. Material snippets depend on the existing
aiverseEncodeFeedback / aiverse_encode_feedback encoder functions:
include/concatenate encoder source BEFORE material helper source.
Shader consumers must supply an explicit mip and virtual layer,
use an unblended linear RGBA8 single-output target and no sRGB.
CPU fixture covers origin, UV upper edge, mip, non-power-of-two
tile counts, invalid arguments and packed ID validity.

This is a reusable shader building block, NOT a wired material
pipeline or a demonstrated GPU shader compile.
Verification: GitHub commits succeeded. C++ compilation, CTest,
GLSL/MSL shader validation and GPU execution NOT RUN.
Next: compile virtual_texture_feedback CTest, compile GLSL/MSL
helper snippets with backend shader toolchains, and wire a real
material/terrain feedback draw through VirtualTextureFeedbackPass
with correct explicit mip selection and camera-visible geometry.


## Stage 8X — reject nonfinite material feedback UV in GPU helpers (2026-11-19)

Runtime source commit: 9e93e6b9ae159754773e801acd29b2ca1c197947.
Test source commit: aecd1fa6e0621ac388d89d2f5de6994fa6f4602b.
Files: engine/gfx/VirtualTextureFeedback.h and
tests/render/virtual_texture_feedback.cpp.
GLSL material feedback helper now checks isnan/isinf on UV before
integer tile conversion; MSL checks all(isfinite(uv)). The existing
CPU equivalent already rejects nonfinite doubles; new fixture covers
NaN, positive and negative infinity. This prevents undefined or
implementation-specific float-to-int page selection for bad material
coordinates. All valid UV and packed-ID behavior remains unchanged.

Verification: GitHub commits succeeded. Native C++ CTest and
GLSL/MSL compilation/GPU execution NOT RUN. This is not a verified
backend fix until shader toolchains compile both snippets.
Next: run texture feedback CPU CTest, compile GLSL 330/ES 300 and
MSL material helpers with their encoder definitions, then integrate
a concrete renderer draw and compare GPU RGBA8 IDs to CPU results.


## Stage 8Y — embedded GLSL feedback material compilation fixture (2026-11-19)

Source commit: e77767b1e64aef3292dc2dbceae7c9c4aa05e916.
New file: tests/render/check_virtual_texture_feedback_shaders.py.
The standalone Python 3 verifier extracts the actual embedded
kVirtualTextureFeedbackGLSL encoder and
kVirtualTextureFeedbackMaterialGLSL source from
engine/gfx/VirtualTextureFeedback.h. It composes full fragment shaders
for GLSL 330 core and GLES 300 with uniform inputs and one RGBA8
output, and invokes glslangValidator -S frag. Any missing shader
symbol, compiler absence or compilation failure exits nonzero.
Run: python3 tests/render/check_virtual_texture_feedback_shaders.py.
This is an opt-in validation script, not yet registered in CTest;
it does not compile the MSL helper or run actual GPU readback.

Verification: GitHub file creation succeeded. The compiler is not
installed in the available execution environment; no GLSL PASS
or native CTest is claimed.
Next: run this verifier with glslangValidator and correct shader
syntax/precision issues; integrate with CMake/CTest on an environment
with a compiler; compile MSL on macOS and compare rendered feedback
RGBA8 bytes to the CPU page-ID encoder on real devices.


## Stage 8Z — conditional GLSL shader CTest registration (2026-11-19)

Build source commit: 1b44a6976e39889f1cf973c10bd97f1f97b6609e.
Changed file: CMakeLists.txt.
Within the existing texture test block, CMake now detects
Python3 Interpreter and glslangValidator and registers
virtual_texture_feedback_shaders only when both exist.
The test invokes the embedded-source GLSL 330/GLES 300 verifier
introduced in Stage 8Y. This avoids breaking toolchains without
glslangValidator, but missing registration is NOT a PASS:
CI must install the compiler and assert the test actually runs.

Verification: GitHub source commit succeeded. CMake configure,
CTest, GLSL compilation and GPU execution NOT RUN.
Next: run cmake configure with glslangValidator installed, check
ctest -N -R virtual_texture_feedback_shaders includes exactly
one test, execute it, then compile MSL with xcrun metal on macOS.


## Stage 9A — Metal feedback shader compile validation (2026-11-19)

Source commit: dec5d88d3380800232176f956a6dcb12e090e9ab.
CMake commit: 539d07fa08f58f47144b4273d5880b01d4720ff1.
Files: tests/render/check_virtual_texture_feedback_msl.py,
CMakeLists.txt.
New macOS Python verifier extracts the actual embedded
kVirtualTextureFeedbackMSL and kVirtualTextureFeedbackMaterialMSL
strings from VirtualTextureFeedback.h, composes a minimal Metal
fragment entry point, and invokes xcrun -sdk macosx metal -c
to emit AIR. Compiler errors and missing AIR fail the script.
CMake conditionally registers virtual_texture_feedback_msl when
APPLE, Python3 and xcrun are available. A missing test does not
count as a pass. No production renderer source changed.

Verification: GitHub source commits succeeded. macOS xcrun,
CMake configure, CTest, GPU readback and real shader execution
NOT RUN. MSL shader syntax is not yet certified.
Next: run ctest -N -R virtual_texture_feedback_msl and the
test itself on a macOS Metal SDK; fix any compiler diagnostics.
Then add a real Metal/GL/Vulkan offscreen feedback render fixture
and compare readback bytes with CPU encodeVirtualTextureFeedback.


## Stage 9B — focused verifier includes optional shader compilation (2026-11-19)

Source commit: e0dc6cdc978ff91044f96cdcfa84509b1199968f.
Changed file: tools/verify-texture-rhi.sh.
Expanded focused CTest regex to include
virtual_texture_feedback_shaders and virtual_texture_feedback_msl,
including the --vulkan path. The verification script records
registered shader tests with ctest -N and explicitly reports
glslangValidator/xcrun toolchain availability in summary.txt.
Absence of optional compiler is reported as NOT VERIFIED,
not misrepresented as a shader PASS. Existing native build and
CTest gates remain unchanged.

Verification: GitHub commit succeeded. Bash execution, CMake
configuration, CTest and GPU validation NOT RUN in this environment.
Next: execute bash tools/verify-texture-rhi.sh --editor on macOS
with Xcode Metal tools, and on Linux with glslangValidator;
inspect verify-logs/summary.txt and ctest.log. Fix actual
shader compiler errors before enabling renderer integration.


## Stage 9C — frame-level virtual texture streaming coordinator (2026-11-19)

Source commit: ecfd2bd13891d630741aa1aa5b989dbc109ed8d8.
Changed file: engine/gfx/VirtualTextureFeedbackPass.h.
Added VirtualTextureFeedbackPass::FrameResult and updateStreaming(
VirtualTextureFileStreamer&, size_t maxNewGpuPages=8,
const VirtualTextureFileStreamer::CompletionFn&={}).
The method is called on the RHI owner thread after beginFrame
delivers async feedback callbacks. It first submits decoded and
deferred visibility via the existing bounded prefetch planner,
then pumps the independently owned file streamer to stage physical
page uploads and publish page-table updates. FrameResult exposes
the dispatch, staged page count and whether streaming remains ready.
It does nothing if the pass is uninitialized, active, or the file
streamer is unavailable. The existing explicit submitVisiblePages
and streamer.pump APIs remain available.

Compatibility: additive native C++ convenience API; no ECS, genre,
renderer shader, asset format or backend ABI changes. No automatic
scene/material feedback drawing is implied.
Verification: GitHub source commit succeeded. Native compile,
CTest, GPU render/readback and editor integration NOT RUN, per
user request to prioritize functions before tests.
Next: wire feedback rendering into the generic material submission
path with correct camera-visible geometry and explicit mip selection;
provide a safe opt-in lifecycle in renderer/editor, then implement
remaining cross-backend RenderTexture/storage parity before native
test and validation phase.


## Stage 9D — resizable feedback target and configurable deferred budget (2026-11-19)

Source commit: 94c056b8ce1642d262f20e71899b98b3670e507f.
File: engine/gfx/VirtualTextureFeedbackPass.h.
Added optional maxDeferredFrames=8 to initialize, forwarding the
existing 1..1024 bounded retry budget to VirtualTextureFeedbackRuntime.
Added resize(width,height), retaining feedback/prefetch configs and
deferred-frame budget, rebuilding the linear RGBA8 render target and
decoder for viewport changes. Resize requires an initialized idle
pass; same-size resize is a no-op. Reallocation invalidates old
weak readback callback state; the Device still owns any outstanding
GPU staging. Failure leaves the object uninitialized rather than
publishing a partial replacement. shutdown clears retained config.
Existing initialize call sites remain source compatible.

Verification: GitHub source commit succeeded. Per user instruction,
native compile, CTest, GPU readback and visual tests NOT RUN.
Limitations: resize is a destructive reallocation, not an atomic
preserve-on-failure swap; renderer must recreate/retry on failure.
Next: integrate viewport resize and feedback drawing in a generic
renderer/material path, then close remaining backend RenderTexture
and storage image feature gaps. Defer tests until functional phase
is finished, but do not mark source as validated.


## Stage 9E — derivative-based feedback mip policy (2026-11-19)

Source commit: 5d95d281c06debd34fd609aa25f9df28e729b523.
File: engine/gfx/VirtualTextureFeedback.h.
Added optional kVirtualTextureFeedbackAutoMipGLSL and
kVirtualTextureFeedbackAutoMipMSL fragment-shader snippets.
aiverseFeedbackMipFromDerivatives / aiverse_feedback_mip_from_derivatives
accept UV, logical virtual texture texel extent, mipLevels and mipBias.
They use max squared UV derivative footprint, clamp negative LOD to
mip 0 and bound the result to mipLevels-1. Nonfinite UV/extent/bias
or derivative footprint and invalid mip counts return -1. The caller
passes the resulting mip into existing material feedback helper.
These are generic shader utilities: no FPS/RTS/Terra dependency and
no automatic pipeline/material binding has been added.

Important: call derivative function in uniform fragment flow, before
conditional discard/early returns; the texel extent is the logical
virtual texture size (not feedback render-target pixels). The
material helper and encoder must still be concatenated in the
correct order. Real GLSL/MSL compiler behavior remains unverified.
Verification: GitHub source commit succeeded; no CMake/CTest,
shader compiler, GPU or visual tests run, per user's functional-first
instruction.
Next: integrate automatic mip policy with generic renderer feedback
submission and material virtual-texture binding; then close backend
feature gaps before the dedicated validation phase.


## Stage 9F — complete feedback fragment shader source builders (2026-11-19)

Source commits: ba3ef59334d3bd0c76795446a8205277591a183b,
0558da0da78f45bb37d7d25587ab9d884a45b6f7.
File: engine/gfx/VirtualTextureFeedback.h.
Added virtualTextureFeedbackFragmentGLSL(bool automaticMip=true)
and virtualTextureFeedbackFragmentMSL(bool automaticMip=true).
They compose the existing packed page-ID encoder, UV-to-page
material helper, optional derivative-based mip selector and a
complete fragment entry point. GLSL declares vVirtualUV and
uniforms uVirtualBaseTiles/uVirtualLayer/uVirtualMip/
uVirtualMipLevels/uVirtualTexels/uVirtualMipBias. Metal declares
AiverseFeedbackParams in fragment buffer(0) and stage input UV
at user(locn0), with entry point aiverseVirtualFeedbackFragment.
The follow-up commit corrected source newline escaping.

Usage: supply a compatible vertex shader, shader version/header
as required by the selected RHI, set viewport/depth testing to
match visible geometry, disable blending, use a linear RGBA8
single-sample attachment and pass correct material/texture
geometry parameters. MSL parameter struct layout must match
Metal's ABI. Source builder is not yet integrated into the
main renderer or material system. Do not assume this is a
verified production shader until compiler and GPU validation.

Verification: GitHub source commits succeeded. Native compile,
shader compilation, CTest and GPU tests NOT RUN, per user
instruction to complete functionality first.
Next: connect source builders to generic material pipeline
creation and scene-visible draw submission; finish backend
storage texture and render target feature parity.


## Stage 9G — reusable feedback pipeline descriptor (2026-11-19)

Source commit: 4ee20aa106ed38a3341598683035c6e13b742c41.
Changed file: engine/gfx/VirtualTextureFeedbackPass.h.
Added virtualTextureFeedbackPipelineDesc(ShaderHandle,
bool depthAttachment=false), an additive native RHI helper
returning PipelineDesc for one RGBA8 UNORM attachment, one sample,
opaque blend, color writes enabled, and no depth-only mode.
The caller supplies shader, vertex layout and cull policy.
Depth testing/writes are disabled by default because the
current VirtualTextureFeedbackPass creates no depth attachment.
The optional depthAttachment flag is only valid if the caller
has actually configured a matching depth attachment.

Limitations: this is a pipeline description factory, not a
created pipeline, material registry, or renderer integration.
Without a depth attachment overlapping geometry feedback may
report occluded pages. Do not claim visibility-correct GPU
feedback until depth support is implemented and integrated.
Verification: GitHub source commit succeeded; native compilation,
CTest, shader compilation and GPU tests NOT RUN, per user request.
Next: implement optional depth attachment in feedback pass,
then integrate actual generic renderer draw submission and
per-material shader parameter binding; finish backend parity.


## Stage 9H — optional depth attachment for visibility-correct feedback (2026-11-19)

Source commit: a06dfa5e4771270b2d0fffc924feb7d8e8e69f39.
Changed file: engine/gfx/VirtualTextureFeedbackPass.h.
Extended initialize(...,size_t maxDeferredFrames=8,
bool depthAttachment=false) with optional depth support.
Depth-enabled passes require PixelFormat::Depth capability,
allocate depth via RenderTargetSet, and clear depth to 1.0
at every begin(). resize() retains the depth mode, shutdown()
clears it. hasDepthAttachment() and depthTexture() expose
read-only state/handle for renderers. Existing call sites remain
compatible with depth disabled by default. Pair with
virtualTextureFeedbackPipelineDesc(shader,true) and depth-tested
geometry submissions for camera-visible feedback.

Limitations: geometry and camera transforms still come from
the renderer. Depth correctness requires compatible projection,
culling, winding and draw order. Depth attachment is not an
automatic copy of the main scene depth buffer.
Verification: GitHub source commit succeeded. Per user's
functions-first instruction, CMake, CTest, shader compilation,
GPU rendering and readback NOT RUN.
Next: connect the depth-enabled pass to generic renderer
submission and material parameter binding, then close remaining
RenderTexture/storage image backend parity.


## Stage 9I — generic material/mesh feedback draw queue (2026-11-19)

Source commit: 792b6f14ad89c40e26ddc95738142333434e28da.
Changed file: engine/gfx/VirtualTextureFeedbackPass.h.
Added VirtualTextureFeedbackDraw (pipeline, vertex/index buffer,
counts, offsets, instance count and per-draw prepare(Device&)
callback) plus VirtualTextureFeedbackPass::renderDrawsAndCapture().
The helper validates draw handles/counts before opening the pass,
then uses renderAndCapture to bind pipeline and buffers, invoke
the caller's material/uniform preparation callback and issue
draw/drawIndexed. Any failed prepare callback ends the pass
without capture; thrown exceptions use existing exception-safe
renderAndCapture cleanup. This is genre-independent and can
accept terrain, static mesh or procedural geometry submissions.

Important: renderer still owns camera culling, material bindings,
vertex layouts, pipeline creation, depth compatibility and
visibility ordering. This is a native submission adapter, not
automatic interception of the engine's scene render queue.
Non-indexed instancing is intentionally rejected because Device
draw() exposes no instance count. Invalid draw commands are
rejected before rendering. Device draw methods return void,
so post-submit GPU failure cannot be detected here.
Verification: GitHub source commit succeeded. Native compile,
CTest, shader compilation, GPU readback and visual tests NOT RUN
per user request to finish functions first.
Next: integrate visible render queue construction into the
engine renderer and expose per-material feedback params; close
backend render-target/storage image feature gaps.


## Stage 9J — generic renderer visibility queue adapter (2026-11-19)

Source commit: 9a51b6ad86b82245acde20dab1da2c982302db03.
Changed file: engine/gfx/VirtualTextureFeedbackPass.h.
Added VirtualTextureFeedbackDrawSource abstract interface with
collectFeedbackDraws(vector<VirtualTextureFeedbackDraw>&), and
VirtualTextureFeedbackPass::renderVisibleAndCapture(source).
Collection happens before entering the GPU pass, after checking
that the pass is ready/idle and no feedback capture is pending
or awaiting consumption. A failed collection returns false
without beginning a pass; successful collection forwards the
draw queue to renderDrawsAndCapture. An empty visible scene
still produces a valid zero-cleared feedback capture.
The renderer is responsible for translating its already-culled
mesh/material queue into compatible feedback draw commands,
including pipeline, geometry, uniforms and depth policy.
No dependency on terrain, editor or genre-specific systems.

Scope caveat: this is the common renderer adapter API only;
no existing renderer has yet been wired to call it automatically.
Verification: GitHub source commit succeeded. Native compile,
CTest, shader compiler and GPU readback NOT RUN, as requested.
Next: identify and integrate the engine's actual shared
renderer visibility queue with this adapter, then implement
per-material virtual texture parameter binding and remaining
backend RenderTexture/storage parity.


## Stage 9K — Renderer3D opaque mesh queue bridge (2026-11-19)

Source commit: 0f1ed98c646f1068f4de83a21c1d34bcef7afc4d.
Changed file: engine/render/Renderer3D.h.
Added an additive Renderer3D::collectMeshFeedbackDraws(out,pipeline,
FeedbackMeshBinder) method. It walks the existing items_ immediate
mesh queue and emits VirtualTextureFeedbackDraw commands for
opaque valid indexed meshes, copying each model transform into
a per-draw prepare callback. The binder is responsible for
setting virtual texture UV mapping and material uniforms; it
receives the mesh and model matrix. No GPU work occurs during
collection. The caller can submit the resulting vector via
VirtualTextureFeedbackPass::renderDrawsAndCapture() while no
other pass is active.

Important limitations: this bridge currently excludes ModelItem
model/animation/LOD batches and customOpaque_ terrain renderers.
The built-in MeshVertex has no UV attribute; binders must use a
compatible feedback vertex shader and a real UV source (e.g.
procedural/world-space mapping) rather than assuming UV0 exists.
The bridge does not auto-run in Renderer3D::render(), since
that function owns scene/shadow passes and leaves the main pass
active for later drawing. Scene culling is not independently
recomputed by this bridge; items_ may contain offscreen meshes,
so the GPU rasterization clips them but CPU collection includes
them. Feedback is not yet complete for all scene content.

Verification: GitHub source commit succeeded. No native compile,
CTest, shader validation or GPU tests run, per user's instruction.
Next: add ModelItem and custom terrain feedback registration
with correct material UVs, integrate feedback scheduling before
the main pass, then finish backend parity and validate.


## Stage 9L — Renderer3D custom terrain/foliage feedback submission (2026-11-19)

Source commits: 0e2ee2f2145d47ab646d4505b82c58be108c6d2b,
e77768f53107b71bf6e04cdc52998246405776dc.
Changed files: engine/render/Renderer3D.h,
engine/render/Renderer3D.cpp.
Added Renderer3D::drawCustomFeedback(FeedbackCustomDraw)
to register per-scene terrain/foliage/procedural feedback
draw callbacks. Added renderVirtualTextureFeedback(pass,
meshPipeline,meshBinder), which collects opaque immediate mesh
feedback draws, then submits them and the registered custom
draws through a single renderAndCapture invocation. Custom
callbacks execute inside the feedback render pass and must
bind their own RGBA8 feedback pipeline, geometry, uniforms,
and matching depth state. beginScene clears customFeedback_
alongside other per-frame draw queues.

This is opt-in and NOT automatically called by Renderer3D::render:
the main pass remains active after render() so the host must
schedule feedback before it. ModelItem/animated models are not
covered. Terra is not yet wired to register callbacks. No
backend feature parity claims are made.
Verification: GitHub commits succeeded. Native build, CTest,
shader compilation and GPU tests NOT RUN, per user request.
Next: connect Terra's renderer to drawCustomFeedback, add
ModelItem feedback handling, and schedule feedback at the
appropriate point before Renderer3D main scene pass.


## Stage 9M — opt-in automatic feedback scheduling in Renderer3D (2026-11-19)

Source commits: 5c7da4b40798e1ab69fe82bfb72bd935c1f56519,
a4f759f4fb56e873faa3a0d8fb5c675a2e1ca0e5.
Files: engine/render/Renderer3D.h, engine/render/Renderer3D.cpp.
Added Renderer3D::setVirtualTextureFeedback(pass*,meshPipeline,
FeedbackMeshBinder) and clearVirtualTextureFeedback(). The
renderer stores a non-owning feedback pass pointer and optional
mesh material binder. When configured and feedback pass is
ready, with no pending/ready async readback, Renderer3D::render()
submits the opaque mesh and registered custom feedback callbacks
after shadow passes and before opening the main scene pass.
This preserves the existing contract where the main scene pass
stays open for subsequent UI rendering. Frame-level streamer
pump remains external; the application must invoke
VirtualTextureFeedbackPass::updateStreaming after readback
completion, or the pass will eventually stop capturing new data.

Lifecycle: call clearVirtualTextureFeedback() before destroying
the feedback pass or device. This is opt-in, not default.
ModelItem and automatic Terra producer registration remain
outstanding. The renderer's generic mesh binder must supply
compatible UV mapping, transforms and material parameters.
Verification: GitHub source commits succeeded; native build,
CTest, GLSL/MSL compiler and GPU execution NOT RUN by request.
Next: implement ModelItem feedback bridge, integrate Terra
feedback callbacks and finish backend parity before testing.
