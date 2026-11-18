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
