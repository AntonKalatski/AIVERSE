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

## Source tree and compatibility

- Canonical core: `engine/render/Prisma.{h,cpp}`, `PrismaData.{h,cpp}`, `PrismaShaders.h`, `PrismaVolumes.h`.
- Temporal/screen-space: `PrismaScreenLighting.{h,cpp}`, `PrismaScreenLightingShaders.h`.
- Legacy `PostFx*.h`, `ScreenLighting.h` and old ops header are compatibility-only; legacy .cpp shims have no definitions. New engine changes MUST go in PRISMA files.
- The volume stack remains GPU-free. The main PRISMA chain includes **source-level, experimental** Auto Exposure, DoF, camera/object motion blur, Stages 42–43 native temporal resolve/jitter, and Stage 44 fixed-scale temporal upscaling. RenderGraph execution is partially opt-in; automatic dynamic resolution and production-grade TSR/FSR are **not** implemented. Native builds and GPU parity tests remain unrun for these stages; never label them verified or production-ready.

## Mandatory validation and handoff

Run `./run-tests.sh --quick` plus targeted HDR policy/GPU pixel tests once a native checkout is available; on macOS build `aiverse_hdr_policy`, `aiverse_hdr_scene`, `aiverse_hdr_gl`, editor/client and check the Metal path. Report actual commands and PASS/FAIL/NOT RUN, never infer success from source files or queued CI.

After **each** bounded code stage, commit `docs/handoffs/prisma-stage-XX.md` on the same branch: exact SHA, architecture and API, all changed paths, actual tests/results, limitations, and the next actionable step. Keep PRISMA handoffs separate from `docs/agent-handoffs/universal-niagara-vfx-2026-11-18.md` maintained by SpectraFX.
