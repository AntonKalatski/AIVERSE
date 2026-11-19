---
name: aiverse-prisma
description: PRISMA — universal AIVERSE post-processing, HDR, tone mapping, bloom, temporal/screen-space effects and camera volumes; migration and agent handoff.
---

# PRISMA — AIVERSE universal image post-processing

**Read first:** `AGENTS.md` (mandatory PRISMA vs SpectraFX ownership matrix), `docs/prisma.md`, `docs/postfx.md`, `docs/hdr.md`, the latest `docs/handoffs/prisma-stage-XX.md`.

## Choose the right owner

- PRISMA owns **image-space processing**: HDR scene resolve, tonemapping, color grading, LUTs, bloom, DoF, motion blur, auto exposure, TAA/FXAA/upscaling, screen-space GI/AO/SSR, camera volumes and full-screen `postfx` Shader Graph domain. `PrismaScreenLighting` is part of PRISMA.
- SpectraFX owns **simulation/presentation of 3D VFX**: emitter graphs, sprite/mesh/ribbon particles, smoke/fire/explosions/trails, ECS FX events. It adds linear HDR pixels to the scene BEFORE PRISMA processes the result.
- Shared Renderer3D/RHI, generic Shader Graph compiler and ECS transport adapters are engine infrastructure, not a second postprocess implementation. **Do not duplicate features or couple them to genres.**

## Native C++ integration

```cpp
#include "engine/render/Prisma.h"
#include "engine/render/PrismaScreenLighting.h"

aiverse::render::Prisma prisma;
prisma.init(device);
prisma.configure(gameManifest["postfx"], mapDocument["postfx"]);
prisma.setQuality(aiverse::render::PrismaQuality::High);

// Optional per-camera blend; currently host-controlled (not auto-wired to ECS):
aiverse::render::PrismaVolumeStack cameraVolumes;
prisma.applyCameraVolumes(cameraVolumes, camera.eye());

// Existing Renderer3D flow: scene, particle emissions, resolve, then UI.
renderer.render(pass, &prisma);
drawSceneVfx();
renderer.resolvePrisma();
// Draw HUD/UI after the image resolve.
```

Use `aiverse::render::PrismaSettings`, `PrismaEffect`, `PrismaType`, `parsePrisma`, `prismaCatalog`, `prismaCostReport` for GPU-free data workflows. Link `AIVERSE::prisma_data` (alias of `aiverse_postfx_data`) for pure data/quality logic; renderer integration uses `aiverse_runtime` as before.

## Serialized camera Post Process Volumes (Stage 08)

Use `engine/render/PrismaVolumeData.h` and `parsePrismaVolumes(game["prismaVolumes"], map["prismaVolumes"], &issues)`. GameClient now applies the merged stack from `game.json` and map data to its current camera every frame. The editor applies it per viewport without rewriting project assets. Volume shapes: global/sphere/axis-aligned box, position, radius/halfExtents, priorities, weight, blendDistance, camera layerMask and `profile` with existing `postfx` effect schema. A matching `id` in the map replaces the game definition; map `replace:true` clears game volumes. Only explicitly authored `hdr`/`enabled` override the base camera's settings. **Per-effect parameters are also selectively blended:** un-authored contrast/saturation/quality/textures remain unchanged when the volume only sets e.g. exposure; see `PrismaVolumeEffectMask` (Stage 09). Native C++ volumes without a mask preserve full-profile semantics. Read `docs/prisma.md` for JSON and known blending restrictions. Tests in `tests/render/prisma_volume.cpp` (CTest `prisma_volume`) must be run before shipping.

## Editor volume authoring (Stage 10)

Open **PRISMA — Постобработка**, choose **Вся игра** or **Эта карта**, then use **PRISMA Volumes** to add a global, sphere or AABB box volume (optionally at Scene camera eye). The panel edits volume transforms, blend/priority/quality masks and nested postfx effects. Checkboxes next to effect parameters control **explicit override vs inherited** values; disabling an override removes the JSON key and preserves global camera style. The shared `makePrismaVolumeJson` factory validates new authored volumes. A native editor/Metal validation is mandatory; currently this UI is committed **but not compiled/run**. See Stage 10 handoff.

## Editor and automation

Existing external identifiers are stable by design: `postfx` JSON key, `postfx_catalog`, `postfx_get`, `postfx_add`, `postfx_set`, `postfx_preset`, `postfx_estimate`, `view_postfx` MCP operations and `postfx` saved panel id. The implementation now lives in `apps/editor/core/PrismaOps.{h,cpp}` and `apps/editor/windows/PrismaWindow.cpp` (title PRISMA). Do not rename persisted schemas or duplicate editor registration.

Common workflow: inspect `postfx_catalog` → choose preset or add effects → check device-tier budget from `postfx_estimate` → compare `view_postfx` tiers and `view_screenshot`. The `postfx` Shader Graph domain is post-processing, not the SpectraFX particle graph.

## GPU-free RenderGraph dependency planning (Stage 07)

Include `engine/render/PrismaRenderGraph.h` from native projects/tooling to declare typed resources and passes, compile a stable topological order, detect cycles, duplicate writers, read/write feedback and invalid resources. The returned `PrismaGraphPlan` describes first/last use and aliasable transient slots; it is **NOT an executable RHI render graph**. External input/output/temporal history are managed by the caller. See `docs/prisma.md` and `tests/render/prisma_graph.cpp`. Do not replace the existing `Prisma::resolve()` sequence until actual backend resource allocation/execution is integrated and GPU-verified.

## Opt-in full-resolution temporal resolve (Stage 42)

The canonical engine path is `Prisma::resolve()`, **not** a second
postprocessing implementation inside `PrismaScreenLighting`. The
`temporalResolve` effect is in the normal `postfx.effects` catalog and
shares quality profiles, Camera Volumes and data-only parsers. It is
**inactive by default**, even when configured, until the native host sets
`AIVERSE_PRISMA_TEMPORAL=1` and uses HDR + High quality.

Example authored game/map effect:

```json
{"postfx":{"effects":[{"type":"temporalResolve","historyWeight":0.86,
"depthThreshold":0.0075,"motionScale":0.04,"reactiveStrength":0.95,
"minQuality":"high"}]}}
```

It uses camera + optional object velocity (current-minus-previous UV, signed
RGBA16F), previous per-camera HDR color/depth ping-pong, 3×3 current-color
neighborhood clipping, four independently depth-validated nearest history
samples and optional current-frame reactive particle coverage. One velocity
pass is shared with opt-in Motion Blur. TAA runs before the blur, Bloom and
final HDR tonemap. HDR FXAA is skipped when temporal actually resolved to
prevent two spatial/temporal AA filters on the same frame. Calling
`prisma.resetMotionHistory(viewId)` after a cut invalidates **both** motion
and temporal history. `retireView(viewId)` fence-retires camera-owned
history; owner GPU completion/lifetime rules are unchanged.

Stage 42 alone is native-resolution stabilization; Stage 43 adds optional
jittered subpixel reconstruction, still **not** dynamic-resolution temporal
upscaling or a motion-reconstruction quality certification. It must be validated on Metal/GL/GLES/Vulkan with
moving actors, silhouettes, sky, particle transparency, camera teleports,
dual viewports and pass count. No native compile, CTest or GPU screenshot
was run in the GitHub-only stage. See
`docs/handoffs/prisma-stage-42.md`.

## Native-resolution subpixel sampling and confidence (Stage 43)

Stage 43 extends `temporalResolve` with two optional catalog parameters:
`jitterPixels` (0..0.5, default 0.5) and `historyConfidence` (0..1,
default 0.65). Jitter is **separately gated** by
`AIVERSE_PRISMA_JITTER=1` *and* `AIVERSE_PRISMA_TEMPORAL=1` plus an
active High/HDR temporal effect. Jitter never runs for games that have not
explicitly enabled both environment flags.

Before `Renderer3D::beginScene`, call the presentation-only camera API:

```cpp
prisma.setViewId(cameraId);              // stable identity, not resolution
prisma.applyCameraVolumes(volumes, camera.eye());
auto renderCamera = prisma.jitteredCamera(camera, framebufferW, framebufferH);
renderer.beginScene(renderCamera, framebufferW, framebufferH);
// Keep the ORIGINAL camera for picking, camera navigation and HUD layout.
```

`jitteredCamera` uses a per-camera 8-phase Halton (2,3) sequence in
`RTSCamera::jitter` (NDC). The temporal shader samples the current HDR
scene at `stablePixel + currentJitter` and reads prior HDR history at the
reprojected pixel **minus previousJitter**; the persistent history target
always uses the stable output grid. The previous jitter is removed from
motion magnitude when computing history confidence.

The temporal confidence rejection combines per-tap previous-depth matching,
3×3 current-color luminance variance and neighborhood clipping, current
depth discontinuities, object/camera motion and the existing reactive mask.
Invalid/sharp camera cuts seed fresh history; changes to projection type,
FOV or clip planes explicitly reset motion and color history. Scene/Game/
GameClient render copies of camera; input/picking/overlays remain unjittered.
`PrismaStats` and the editor PRISMA panel show last-frame resolve/jitter.

Stage 43 is **native-resolution subpixel temporal AA**, not TSR/FSR/DLSS,
dynamic resolution, jittered shadows or proven reconstruction quality.
Do not compose with the older optional `PrismaScreenLighting` TAA.
No CMake/native compile, CTest or Metal/GL/Vulkan screenshots were run;
verify motion, transparency, silhouettes and subpixel stability before
production. See `docs/handoffs/prisma-stage-43.md`.

## Experimental temporal upscaling (Stage 44)

The **canonical** Prisma owner now has a host-controlled split between the
internal 3D scene resolution and the final output target. It is strictly
opt-in: author `temporalResolve` with `renderScale` in **[0.5,1]** (default
1), choose HDR/High, then set all three environment flags
`AIVERSE_PRISMA_TEMPORAL=1`, `AIVERSE_PRISMA_JITTER=1` and
`AIVERSE_PRISMA_UPSCALE=1`. A 1.0 renderScale or missing flag retains the
original 1:1 render size. The scale is quantized to 1/16 steps to avoid
continuous transient-texture churn across camera volumes.

```cpp
prisma.setViewId(stableViewId);
prisma.applyCameraVolumes(volumes, originalCamera.eye());
auto [renderW, renderH] = prisma.renderSizeForOutput(outputW, outputH);
auto renderCamera = prisma.jitteredCamera(originalCamera, renderW, renderH);
renderer.beginScene(renderCamera, renderW, renderH);
// Draw 3D and particle/reactive data in renderW x renderH.
renderer.render(fullOutputPass, &prisma); // still a full-size output target
drawVfxInto3DScene();
renderer.resolvePrisma();
prisma.clearCameraVolumes();
// Draw HUD and overlays at outputW x outputH with originalCamera.
```

The existing GameClient and editor Scene/Game implementations follow this
contract; renderer projection/jitter, scene depth, motion vectors, camera
blur and other HDR passes operate at the internal size. Before the final
tonemap/composite, the temporal shader writes an **output-size RGBA16F**
per-view history; it uses normalized signed motion vectors independent of
the two resolutions, current RGB/depth at native scene UVs, separately
depth-validated history taps at output pixel spacing and mild compressed-HDR
reconstruction controlled by `reconstructionSharpness` (0..0.5, default
0.15). The final image and UI retain full output resolution. If the
temporal pass fails, the full-output composite can sample the low-res
current frame with ordinary bilinear filtering, but temporary jitter
instability is possible until the next frame.

This is a first experimental **fixed-scale temporal upscaling** path,
NOT a shipping FSR/TSR/DLSS substitute and NOT automatic dynamic
resolution. It is uncompiled and GPU-unverified: evaluate motion
reprojection, thin details, foliage, silhouettes, bloom and transparency,
over/under-allocation, scaling ratios and Metal/GL/Vulkan shader validity.
See `docs/handoffs/prisma-stage-44.md`. Do not enable by default or infer
improved frame rate from source code.

## GPU-fence-gated adaptive scaling and stable fallback (Stage 45)

`engine/render/PrismaAdaptiveResolution.h` adds a CPU-only
`PrismaAdaptiveResolution` hysteresis controller that adjusts render scale
only from **new, completed GPU-frame timings** plus a strictly advancing
`Device::gpuCompletedFence()`. The native `Device::gpuFrameMs()` supplies
an asynchronously completed *whole GPU frame* measurement where supported
(e.g. Metal); unsupported GL/Vulkan/mobile backends may return 0 and must
retain the authored scale. CPU frame times/FPS are NEVER substituted.

The `temporalResolve` effect now provides optional `adaptiveResolution`
(default false), `targetGpuMs` (default 16.6; 4..50 ms) and
`minRenderScale` (default 0.5). It also requires
`AIVERSE_PRISMA_ADAPTIVE=1` along with the Stage 44 trio of
`AIVERSE_PRISMA_TEMPORAL=1`, `AIVERSE_PRISMA_JITTER=1`,
`AIVERSE_PRISMA_UPSCALE=1`. The authored `renderScale` is the upper
bound. Over target by >15% for 12 new completed GPU observations steps
scale down 1/16; <80% of target for 36 samples steps it up; 48 new
completed GPU samples of cooldown prevent oscillation. Each view has
its own state, but timings represent the entire GPU frame, NOT an
isolated viewport.

Before returning a reduced render size, `renderSizeForOutput()` now
**preflights GPU resources** on the graphics thread: valid temporal and
velocity pipelines, fallback composite, full-output HDR history pair and
matching scratch depth, low-res HDR scene/depth/velocity/scratch. Failed
preflight returns the original full dimensions, suppresses jitter for the
view and rate-limits retries (60 calls). Resource ownership stays in
the existing Prisma cache/history and is GPU-fence retired on resize.

If the temporal pass unexpectedly fails *after* jittered geometry has
already been encoded, the existing final composite now samples at
`s + currentJitter` to cancel the projection offset. This is a
stable-space **spatial fallback**, not a substitute for history or an
automatic GPU retry. The shader's `graphFrame.zw` is zero in normal
paths and is used only for fallback sampling. Input/overlay/UI cameras
are unchanged. The editor shows the adaptive-state, current internal
scale, whole-GPU-frame timing and whether jitter compensation was used.

**Do not confuse:** `gpuCompletedFence` is a validity gate for new
asynchronous timings, never permission to release arbitrary resources;
normal Prisma GPU lifetime rules remain unchanged. Profiling and
compilation have NOT RUN in this stage, and neither adaptive frame-rate
improvement nor image-quality superiority has been measured. See
`docs/handoffs/prisma-stage-45.md`.

## Scoped viewport GPU diagnostics and Prisma-memory guard (Stage 46)

The RHI provides three **optional**, nonblocking `gfx::Device` hooks:
`beginGpuScope(tag)`, `endGpuScope()`, and
`takeGpuScopeTimings()`. The bounded scope covers an actual region of GPU
commands between the two calls, not CPU duration. It uses a **separate
result queue** from the existing `timeGpuFrame/takeGpuTimings` benchmark
interface and must not overlap an active full-frame `GL_TIME_ELAPSED`
query. Desktop OpenGL 3.3 on supported macOS/Windows builds uses real GPU
elapsed queries; GL ES/WebGL/Linux fallback, Metal and Vulkan report
unsupported rather than inventing per-viewport timing. Metal's existing
`gpuFrameMs()` remains a whole-command-buffer value, never a viewport
value.

The editor can opt into Scene/Game GPU-region timings with
`AIVERSE_PRISMA_VIEWPORT_GPU_TIMING=1`. It scopes the GPU submission
from `Renderer3D::render` through `resolvePrisma` and the final viewport
pass, then nonblockingly polls only finished scope queries in the
pre-render maintenance window. `Prisma::acceptViewGpuTiming(viewId,ms)`
accepts those **completed/valid** samples from a trusted host;
`viewGpuTimings()` supplies read-only per-view diagnostics. The
per-view adaptive controller prefers a *new scoped measurement* when
available, otherwise falls back to the previously documented confirmed
whole-frame GPU timer; it never substitutes CPU time/FPS. A scope may
include shadows, reflections and FX, but does not measure asset uploads
before `Renderer3D::render`. Render passes with no scope support simply
remain unmeasured.

The existing opt-in `temporalResolve.adaptiveResolution` catalog adds
`maxPrismaMemoryMiB` (32..4096 MiB, default 256). The controller
compares it with the **lower-bound** sum of
`Prisma::memoryStats().liveKnownBytes` plus
`Prisma::retirementStats().knownPendingBytes`, where shared size-cache
resources are counted only once and pending resources are not
double-counted. A known-memory overage can drive a downward 1/16 scale
step after 12 distinct confirmed GPU completions (even when no valid
GPU-ms duration is available). It blocks upward quality promotion while
over budget and still uses the 48-sample cooldown. It never force-releases
resources: actual destruction still requires the existing GPU-fenced
retirement path. This is a **Prisma-only known-memory guard**, NOT total
VRAM usage, device residency, physical driver allocation or a real
VRAM-budget query; opaque/unknown textures are excluded.

Scene/Game scoped timings and memory pressure appear in Stats/Prisma
editor windows, and `PrismaStats` shows the GPU timing source
(scoped/whole), last valid ms, known bytes and pressure. On an unsupported
backend the UI says unavailable, not 0 ms.

These APIs are source-level and **NOT native/GPU tested**. Before shipping,
compile GL33+GLES and Metal/Vulkan, run GPU timing query/camera lifetime
smoke, verify no overlap with other profilers and benchmark consumers,
exercise memory thresholds and test GL context loss/query saturation.
`docs/handoffs/prisma-stage-46.md` is the next-agent source of truth.

## Metal hardware viewport timers and Renderer3D memory accounting (Stage 47)

Stage 47 implements an experimental **real Metal GPU timestamp** backend
for the existing `gfx::Device::beginGpuScope/endGpuScope/takeGpuScopeTimings`
API. It uses `MTLCounterSampleBuffer` from the Metal device's
`MTLCommonCounterSetTimestamp` only if that GPU supports
`MTLCounterSamplingPointAtBlitBoundary` or
`MTLCounterSamplingPointAtStageBoundary`. Scope endpoints are encoded
into tiny blit encoders in the *existing* command buffer, never split into
separate command buffers or taken from the whole-frame
`GPUStartTime/GPUEndTime`. The completed command buffer's two timestamp
samples are resolved in a nonblocking poll; the GPU clock delta is
converted to elapsed time by **two correlated CPU/GPU clock pairs**
(`MTLDevice::sampleTimestamps:gpuTimestamp:`). Impossible/invalid
samples are rejected, never passed off as a valid per-viewport reading.
Scopes are bounded to 64 pending records. GPU fences and existing Metal
frame completion remain unchanged. Devices without supported timestamp
counters return unsupported, so the prior whole-GPU-frame fallback is
still separate and correctly labeled.

The editor's existing opt-in
`AIVERSE_PRISMA_VIEWPORT_GPU_TIMING=1` now covers supported Metal
devices as well as desktop GL33. The editor already scopes Scene/Game
GPU commands and polls completed scope results separately from
whole-frame benchmark timings. **No Vulkan or GLES/WebGL scoped
timing support is claimed.**

`Renderer3D::residentMemoryStats()` is a read-only CPU metadata estimate
for **directly owned** GPU resources: sun shadow map, selection mask,
scene-capture textures, trace placeholders, renderer-owned model
placeholders, per-camera object-motion/reactive buffers and owned
line/selection/indirect buffers. Distinct handles are counted once;
`sceneColor_/sceneDepth_` cache aliases are not counted a second time.
Pending retired object-motion textures appear in a separate byte field,
not in live residency. This does not include external Model/Material
assets, `LightSystem` shadow/reflection resources, persistent scene/
geometry owners, unenumerated GPU driver/shader allocations, or PRISMA
targets. Buffer physical versions/mips can exceed the reported bytes.

The host calls `prisma.setExternalRendererGpuMemory(liveKnownBytes,
pendingKnownBytes)` before `prisma.renderSizeForOutput()`. Editor
Scene/Game and GameClient now supply the direct Renderer3D memory
estimate. Prisma's adaptive threshold `maxPrismaMemoryMiB` keeps its
existing serialized key but compares the **known Prisma +
directly-owned Renderer3D live and pending lower bound** with the
author's threshold. The two owners' resources do not overlap. This
is not a VRAM-capacity query, not a true hard cap, and not justification
to destroy GPU-fenced objects early.

**Stage 47 correctness fix:** a per-view scoped query sequence and the
whole-frame GPU fence are separate token domains. The hysteresis policy
resets its sample counter (NOT render scale) when switching sources.
A scoped sample is stale after more than eight subsequent completed GPU
fence advances without a replacement, allowing a truthful whole-frame
fallback. No duplicate GPU completion is counted as a fresh sample.

Feature guards, game data, ECS simulation and editor ownership remain
as in Stages 41–46. **Native Obj-C++/Metal compilation, counter query
runtime, GPU timing calibration and VRAM profiling have NOT RUN.**
Apple hardware-counter support and GPU calibration must be verified on
real devices before shipping; see `docs/handoffs/prisma-stage-47.md`.

## Cross-owner GPU memory budget (Stage 48)

Stage 48 expands the Stage 47 **source-level** lower-bound memory path
while preserving the established no-double-counting ownership rules.
`engine/render/GpuOwnedMemory.h` defines a read-only
`GpuOwnedMemoryCounter` and `GpuOwnedMemoryStats`. It samples RHI
`textureInfo/bufferInfo` only; it never reads back, waits on,
destroys or transfers GPU objects. It deduplicates exact texture and
buffer handle IDs per owner. Known formats are counted using minimum
bytes/pixel and unrecognized compressed/missing RHI metadata as
**unknown**, never guessed. Per-backend buffer versions and mips can
make actual physical VRAM larger than the known lower bound.

All of these GPU owners now provide a **read-only allocation estimate**:
- `LightSystem::ownedGpuMemoryStats()`: light map and depth, static
  light atlas, spot shadow, planar reflection color/depth, black
  placeholder and light map/halo/beam/decal buffers
- `PersistentGpuSceneUpload::ownedGpuMemoryStats()`: every in-flight
  per-frame GPU scene upload slot, not just the current one
- `GpuSceneCulling::ownedGpuMemoryStats(device)`: each allocated
  per-frame visibility/compute buffer
- `GpuSceneDrawCommands::ownedGpuMemoryStats(device)`: each
  input/output/compaction/visible-count buffer across all in-flight
  GPU submission slots

`Renderer3D::residentMemoryStats()` now includes these **mutually
exclusive owners** alongside its previous shadow/capture/selection/
motion buffers. Per-owner known byte subtotals allow the editor to
explain the total. Borrowed geometry/model/material buffers are
explicitly excluded. The global `GlobalGeometryPool` lives outside
Renderer3D; its `geometryPoolStats(device).allocatedBytes` counts
each shared RHI geometry page once, independent of how many Models
reference it. Geometry pages are NOT also enumerated as independent
Renderer3D buffers.

The host sets both sources **before** requesting render resolution:

```cpp
const auto owned = renderer.residentMemoryStats();
prisma.setExternalRendererGpuMemory(
    owned.liveTextureKnownBytes + owned.liveBufferKnownBytes,
    owned.pendingMotionKnownBytes);
prisma.setExternalGeometryPoolGpuMemory(
    geometryPoolStats(device).allocatedBytes);
auto [renderW, renderH] = prisma.renderSizeForOutput(outputW, outputH);
```

Both Editor Scene/Game and native GameClient use this flow. The existing
`temporalResolve.maxPrismaMemoryMiB` name remains for JSON/editor/MCP
backward compatibility but the Stage 48 threshold now applies to the
known resident/pending lower bound of **Prisma + Renderer3D subowners
+ GlobalGeometryPool pages** (saturating arithmetic). It is NOT a
device-wide VRAM budget or a promise to stay under the threshold.
Other geometry owners, dedicated external model buffers/material
textures, other apps using the same GPU, driver heap alignment and
mips remain unobservable. Do not double count those when new owner
telemetry is subsequently added.

**Work sequencing:** finish remaining functional owners, integration,
fallback and capabilities **before** beginning the separate build/
GPU validation/repair phase, per user instruction. After each feature
stage, commit a dedicated handoff. Stage 48 source was structurally
inspected, not natively compiled or GPU-tested. See
`docs/handoffs/prisma-stage-48.md` for the complete owner boundary
and subsequent feature scope.

## Dedicated model assets and per-viewport admission (Stage 49)

The current renderer/memory report is a **strictly owned, lower-bound GPU
inventory**. Stage 49 extends the Stage 48 owner tree to model assets
without counting shared geometry pages or borrowed material images twice:

- `appendModelOwnedGpuMemory(Model, GpuOwnedMemoryCounter&)` counts
  dedicated Model VBO/IBO **only when `geometryPoolPage` is empty**,
  nonborrowed `Image.tex`, and owned morph/baked-animation textures.
  Its ownership follows the real `releaseModel()` contract.
- `ModelTextureCache::appendOwnedGpuMemory(counter)` enumerates cached
  model/material PBR textures once from the cache owner. Models borrowing
  this cache set `Image.borrowedTexture=true`; do not count them again.
- `RtsView::ownedModelGpuMemoryStats(device)` aggregates those GPU-owned
  models from its unique model cache and cached material textures, with
  shared handle de-duplication across models.
- `TextureCache::ownedGpuMemoryStats()` independently enumerates
  sprite, generated and atlas-page textures, de-duplicating shared pages
  referenced by atlas sprites. It excludes retired/borrowed resources.

`Prisma::setExternalPresentationAssetsGpuMemory(knownBytes)` accepts
the sum of *nonoverlapping* presentation asset owners. Editor Scene/Game
add model resources and their independent FX sprite cache. GameClient
currently adds its RtsView model/material owner (other FX render owners
may publish additional disjoint inventories later). GeometryPool
still uses `setExternalGeometryPoolGpuMemory` and each page counts once
per device, NOT per model. No shader/driver/texture mip assumptions are
made; unknown RHI metadata remains unknown.

**Multiple cameras** can call
`Prisma::setViewQualityPolicy(viewId,{priority,maxTemporalHistoryMiB})`.
Priority 0..255 (0 yields first; 255 yields last) changes ONLY the
memory-pressure hysteresis in adaptive mode, never the ordinary
12-sample GPU-slowdown response. When the device is over the known
memory budget without slow GPU timing, low priority yields after 6
confirmed observations, default/medium after 12, high after 30; the
existing cooldown remains 48 completed observations. An independent
per-camera `maxTemporalHistoryMiB` limit (0 = no limit) considers
**two output-size RGBA16F histories**, i.e. `width*height*16` bytes.
If that amount is over the admission quota, `renderSizeForOutput`
declines the temporal upscale and returns full size, retires old
history through GPU fences, records admission rejection and
`jitteredCamera` explicitly stays unjittered. `resolve` MUST not
allocate that rejected history again. Original UI/picking remains
unshifted. `Prisma::retireView` clears both view policy and history
admission state.

Editor Scene uses a lower-priority policy (64; two-history quota
128 MiB); Editor Game and the native GameClient use 224 with
256 MiB history quota. Custom hosts can supply their own values via
the public genre-independent API. The existing
`maxPrismaMemoryMiB` is the *known* total owner budget, not a hard
VRAM cap or an allocator.

**No tests yet** per project sequencing: code and source readback
first, then standalone build/CTest/backend GPU validation only after
all functions are finished. Every implementation stage requires its
own `docs/handoffs/prisma-stage-XX.md`; Stage 49 details are in
`docs/handoffs/prisma-stage-49.md`. Prior stage ownership, Metal/GL
scoped timers, backend fallback and GPU-fenced resource lifetime are
not changed.

## Source tree and compatibility

- Canonical core: `engine/render/Prisma.{h,cpp}`, `PrismaData.{h,cpp}`, `PrismaShaders.h`, `PrismaVolumes.h`.
- Temporal/screen-space: `PrismaScreenLighting.{h,cpp}`, `PrismaScreenLightingShaders.h`.
- Legacy `PostFx*.h`, `ScreenLighting.h` and old ops header are compatibility-only; legacy .cpp shims have no definitions. New engine changes MUST go in PRISMA files.
- The volume stack remains GPU-free. The main PRISMA chain includes **source-level, experimental** Auto Exposure, DoF, camera/object motion blur, Stages 42–43 native temporal resolve/jitter, Stage 44 temporal upscaling and Stage 45 opt-in **whole-GPU-frame adaptive scale**. RenderGraph execution is partially opt-in; a production-grade dynamic resolution/TSR/FSR system is **not** implemented. Native builds and GPU parity tests remain unrun for these stages; never label them verified or production-ready.

## Mandatory validation and handoff

Run `./run-tests.sh --quick` plus targeted HDR policy/GPU pixel tests once a native checkout is available; on macOS build `aiverse_hdr_policy`, `aiverse_hdr_scene`, `aiverse_hdr_gl`, editor/client and check the Metal path. Report actual commands and PASS/FAIL/NOT RUN, never infer success from source files or queued CI.

After **each** bounded code stage, commit `docs/handoffs/prisma-stage-XX.md` on the same branch: exact SHA, architecture and API, all changed paths, actual tests/results, limitations, and the next actionable step. Keep PRISMA handoffs separate from `docs/agent-handoffs/universal-niagara-vfx-2026-11-18.md` maintained by SpectraFX.
