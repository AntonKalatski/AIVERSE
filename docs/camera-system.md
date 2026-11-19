# AIVERSE LENS — genre-independent multi-camera and capture architecture (design proposal)
Date: 2026-11-19
Branch: `feature/universal-multicamera-render-targets`

> **Status: DESIGN ONLY. Not yet implemented, built, or validated.** This proposal follows `docs/agent-handoffs/universal-multicamera-stage1-2026-11-19.md`.
> Confirm the owner wants each priority tier before expanding scope. Keep ownership in `engine` and never reference genre modules from common camera code.

## 1. Requirements
1. An arbitrary number of cameras simultaneously present in one scene, each addressable with a stable generation-checked `CameraHandle` and optional ECS entity.
2. Each camera can render to swapchain, external or managed render texture (color/depth, HDR), viewport sub-rectangle, or an offscreen capture; independent clear/load/store and layer masks.
3. A camera need not be active on screen to capture. A sampleable RT remains valid until the host explicitly releases it; GPU lifetime and inter-frame ownership must be respected.
4. Scheduled capture: every frame, fixed Hz, every N frames, dirty-driven, once, manual. Priorities and per-frame budgets must not starve essential output.
5. The same simulation snapshot can be viewed through many cameras. No genre-specific world, input or render logic is allowed in the camera infrastructure.
6. Camera stack: base/overlay, split-screen and picture-in-picture; explicit composition and clear semantics.
7. Independent temporal effects (TAA/SSR/SSAO, motion vectors, auto-exposure) and jitter/history per camera and output size. Camera cuts, teleports, dynamic resize and disabled captures invalidate histories.
8. No read/write feedback loops: a render pass may not sample the image being written. Capture dependencies form a DAG; reject cycles or use explicitly documented previous-frame history.
9. Avoid duplicating immutable geometry uploads per camera. Plan shadow, culling and LOD reuse without sharing view-dependent visibility incorrectly.

## 2. Existing implementation to preserve
- `engine/render/Math3D.h`: new `render::Camera` value type (formerly `RTSCamera`) with target/distance/orbit, perspective/orthographic, near/far and jitter.
- `Renderer3D::beginScene(camera, w, h) -> draw* -> render(gfx::PassDesc, PostProcess*) -> [resolvePostFx()] -> gfx::Device::endPass()`.
- `gfx::PassDesc` already supports separate `colorTarget` / `depthTarget` textures.
- `apps/editor/Editor.h::Viewport` already contains independent render-target pair for Scene/Game windows.
- `modules/rts/view/RtsPortrait.cpp` is an existing isolated offscreen Renderer3D pattern but contains RTS-specific material/model logic. Extract a reusable general portrait/model preview renderer first.
- `engine/render/PostFx`, `ScreenLighting`, `Lights`, existing Metal, GL, Vulkan RHI are reused rather than replaced.
- Shared `aiverselib/gameplay/Camera.h` is a camera controller helper, not a render/capture scheduler; avoid conflating the concepts.

## 3. Proposed engine modules (new API; not implemented)
- `engine/camera/CameraRegistry.h/.cpp` — stable `CameraHandle`, create/destroy/enable, state and explicit ownership; camera source may be ECS entity or direct C++.
- `engine/camera/CameraComponents.h` — serializable `CameraComponent`, `CameraTargetComponent` / `CameraRigComponent`, entity transform association. Cameras do not own simulation time.
- `engine/camera/CameraDirector.h/.cpp` — active view selection, priorities, virtual cameras, cuts and interpolated blends.
- `engine/camera/CameraRig.h/.cpp` — orthographic/perspective targets, follow, look-at, orbit, free-fly, spring arm, rail/dolly, constraints and deterministic damping.
- `engine/render/CameraRenderScheduler.h/.cpp` — capture requests, topological ordering of RT dependencies, policy/budget, debug stats, per-view save/restore.
- `engine/render/CameraView.h` — immutable `RenderView` snapshot (Camera, viewport, output, masks, clip overrides, quality, temporal key, clear).
- `engine/render/RenderTargetPool.h/.cpp` — matching color/depth, ownership, resize, explicit format/sample count and deferred destruction by GPU frame completion.
- `engine/render/PortraitRenderer.h/.cpp` — purely model/pose/material preview renderer without `RtsView`, `UnitType` or `modules/rts` includes. RTS stays a thin adapter and other hosts call directly.
- `apps/editor/windows/CameraWindow.cpp` — per-entity camera inspector, selection, live preview/RT visualization, capture flags and instrumentation.
- `engine/camera/CaptureAsset.h` (later) — named RT assets exposed to materials, UI and scripts without raw transient texture handles in save data.

### Output types
- `Screen` (swapchain); `Texture` (color/depth/HDR RT); `Viewport` (screen subrect); `Capture` (one-shot color/depth); `Cubemap` (six views, future).
- Do not introduce `RTS`, `FPS`, `Editor`, `Genre` as shared classes or enum cases.

### Scheduling and cycle examples
- `EveryFrame` = render each submitted frame; `Manual` = capture on request; `FixedHz` = rate-limited; `EveryNFrames` = update interval; `OnDirty` = host- or scene-invalidated.
- Capturing CCTV monitor camera C into texture T and displaying T on main camera A requires C before A; C must not render geometry sampling T unless using explicitly delayed history.
- For mirrors/portals, support recursion depth budget and explicit previous-frame fallback; no implicit infinite recursion.
- Track per-camera CPU submit ms, GPU timings when available, draw calls, triangles and RT footprint separately from aggregate frame stats.

## 4. Rendering sequence (conceptual)
1. Simulation completes one presentation snapshot; gather camera ECS state and controller/director outputs.
2. Resolve output sizes, policies and target ownership; schedule camera DAG by priorities/dependencies.
3. For each camera: immutable `RenderView`; scoped renderer policies; bind temporal history keyed by `CameraHandle` + view variant; `beginScene`; submit shared world data using camera-specific culling and LOD; render to selected `PassDesc`; resolve PostFX if needed; finish pass; publish produced texture.
4. Composite screen and overlays; finish GPU frame; retain targets and history until safe to recycle. Handle camera removal and resize without use-after-free.

## 5. Roadmap
- **P0**: generic `Camera` (stage 1 source migration, not compile validated), portrait extraction, camera registry, RenderTarget asset, camera-to-RT, independent screen camera, manual/every-frame capture, two-camera output example, tests.
- **P1**: policy scheduler, camera stack, director and rigs, ECS/serialization, editor camera window, per-view PostFX and temporal state, layer masks/culling and profiling, split-screen.
- **P2**: physical lens/DOF, cubemap and panoramas, HDR EXR/capture sequences, planar mirrors/portals, camera shakes and cinematic rails, stereo/OpenXR.
- Capture output should support environment-dependent clear/load/store, MSAA resolve, high-DPI size, orientation and backend Y-flip rules.

## 6. External libraries
- **None required for the P0 camera core**: use existing `Math3D`, `Renderer3D`, `gfx`, `PostFx`, `ImGui`, C++20.
- **OpenXR SDK (optional P2)** for XR runtimes; never make it mandatory for non-XR builds.
- **TinyEXR (optional P2)** for saving linear HDR image captures when no existing format writer suffices.
- **FFmpeg (optional external tooling)** for video export, not embedded in render graph nor required in runtime.
- Avoid GLM/BGFX replacement: AIVERSE already has math and platform RHI; mixing redundant matrix/texture ownership is a liability.

## 7. Tests / release criteria (planned, NOT RUN)
- C++/mock GPU: generation-safe handles, registry lifecycle, scheduling priorities, DAG cycle rejection, target resize, missing target, clears, invalid dimensions, manual capture and visibility masks.
- Real Metal, GL, Vulkan: render two distinct cameras into two RTs and screen in same frame, verify distinct pixels/view transforms and depth, texture orientation, alpha/compositing, resizing/recreation, no read-while-write hazards.
- Temporal: camera A/B alternating with distinct pose/time and post-processing history; cut/resize/reset; no history contamination.
- Stress: 16/64/256 offscreen cameras with 1/5/15/60 Hz budgets; VRAM bound and observed GPU completion; no implicit per-camera per-frame full-scene upload.
- Genre parity: RTS, FPS, voxel and editor scenes with no dependency from `engine` to `modules/*`.
- Mac Metal validation is required before advertising P0 as production-ready.

## 8. Handoff protocol
Every bounded implementation stage must have code commit(s), then a separate dated handoff commit on this branch, with current head, affected paths, actual executed tests and outcomes, compatibility and next action. Do not merge to `main` until build/CTest, GPU and editor acceptance gates are validated. Respect `AGENTS.md`, editor and Metal batching handoffs.
