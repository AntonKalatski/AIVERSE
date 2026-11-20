# SpectraFX Stage 99 — independent per-camera GPU/CPU preparation (2026-11-19)

**Branch:** `feature/spectrafx-generic-cue-queue`
**Code SHA:** `8f9777c17df9c4b22d2104927ef949a925ede6b3`
**Previous Stage 98 handoff:** `docs/agent-handoffs/spectrafx-stage98-metal-rhi-2026-11-19.md`
**Last validated native acceptance:** Stage 97 source `4e363db3a6fc0d1631a574165febb7d26e08c3b8`. **Stage 98 and Stage 99 have NOT been compiled or tested.**

## Problem and behavior

Existing `SpectraFxSceneHost::prepareGpu(dt,camera)` and `prepareCpu(dt,camera,...)` each advance simulation time. Calling them for each render target/camera causes extra particle ageing, duplicate spawning and potentially multiple impact callbacks. GPU sorted-alpha output also depends on camera forward, so sharing camera A's sort texture for camera B is not visually correct.

**Implemented separation:**

- `engine/fx/SpectraFxHybridEmitter.h`: `prepareRenderView(eye,forward)` updates only the *presentation* LOD and, if authored GPU alpha sorting is enabled and 2+ visible particles exist, runs per-camera sorted-index preparation outside render/compute encoders. It does not step particle ages, spawn particles or modify authoritative state. Uses existing sorted-alpha strict CPU fallback/suspension behavior on GPU errors.
- `engine/fx/SpectraFxHybridPlayback.h`: `prepareRenderView(eye,forward)` applies the per-camera path to active and one-shot emitters, accumulating failure diagnostics. Homogeneous shared arenas remain additive-only; their GPU storage/compaction is reusable for both views, with per-view billboard matrices at rendering time.
- `engine/fx/SpectraFxSceneHost.h`: `prepareRenderView(const SpectraFxView&)` and an optional camera-template compatibility adapter. This updates CPU `ParticleSystem::setView` (camera position/culling/billboard basis only), then prepares GPU alpha sorting; does not call CPU/GPU `update`, `playGpuImpactEffects` or drain incoming cues.
- `tests/spectrafx_compile_smoke.cpp`: compile-only invocations for both direct view packet and unrelated cinematic camera template.

**Multi-camera owner frame order (every camera may have a different RT):**

```cpp
device.beginFrame(framebufferW, framebufferH);
host.prepareGpu(dt, viewA);                 // exactly once per frame
host.prepareCpu(dt, viewA);                 // exactly once per frame
// Camera A: outside scene pass
host.prepareRenderView(viewA);              // per-camera; no dt
rendererA.beginScene(cameraA, widthA, heightA);
rendererA.render(passA);                    // leaves compatible pass open
host.render(rendererA.lighting, viewA);
device.endPass();

// Camera B: the first render pass has fully ended
host.prepareRenderView(viewB);              // resort GPU alpha for B, no dt
rendererB.beginScene(cameraB, widthB, heightB);
rendererB.render(passB);
host.render(rendererB.lighting, viewB);
device.endPass();

device.endFrame();
```

`renderer.beginScene` currently accepts an old `RTSCamera` in Renderer3D; this is **Renderer3D's separate camera modernization work**, not a SpectraFX dependency. SpectraFX itself takes only `SpectraFxView`.

## Constraints / limitations

- `prepareRenderView` must be invoked on the render/RHI owner thread, **after** compute update and **outside** any render/compute encoder. Camera A and B passes must be sequential with preparation between passes (the alpha-sort scratch texture is reused, so do not prepare all views before rendering them).
- `prepareGpu` and `prepareCpu` intentionally retain historical behavior: they advance once *per call*. New per-view API does not automatically detect or deduplicate repeated simulation calls. The host owns correct frame scheduling.
- Shared arenas are additive-only and do not require sorted alpha, but their spawn-scale LOD is evaluated once using the primary update camera. This is a visibility/quality policy choice to revisit if per-view culling or VR stereo is added.
- `SpectraFxView` still uses a float-origin projection packet. No new deterministic ECS authority, genre-specific game code, serialization or persistent layout changed.
- The above is code authored through GitHub and has **not yet been native compiled/tested or visually captured**. GPU sort errors, renderpass sequencing, and multiple viewports still require real Metal acceptance.

## Actual checks, source and next action

- Fetched current source of `HybridEmitter`, `HybridPlayback`, `SceneHost` and header-smoke fixture; applied uniquely checked substitutions, committed and fast-forward-pushed the code.
- **NOT RUN:** CMake, Clang, Metal, CTest, RHI multi-view fixture, Editor GPU Preview, lifecycle stress, visuals.
- User's Stage 97 M3 Ultra 8/8 native acceptance remains valid for the earlier SHA only.
- **Next agent:** run Stage 98's `spectrafx_metal_rhi` CTest and the updated `aiverse_spectrafx_header_smoke` target, fix compiler/RHI errors and repeat complete acceptance in one pass. Add a GPU sorted-alpha two-view fixture and real Renderer3D/Editor side-by-side offscreen capture. Stress `HybridPlayback::resetPresentation`, async GPU callbacks and 3-frame retire with actual telemetry before claiming production readiness.

Commands from clean isolated worktree:

```bash
cd ~/Projects/AIVERSE-spectrafx-validation
git status --short
git fetch origin
git switch --detach origin/feature/spectrafx-generic-cue-queue
bash tools/spectrafx/acceptance.sh build/spectrafx-validation
```

Keep `main` and independent PHYSIS/NEREID/TerraWeave/PRISMA branches untouched until explicitly reviewed for integration.
