# SpectraFX Stage 104 — Renderer3D multi-view, depth and reset Metal acceptance

**Date:** 2026-11-19  
**Branch:** `feature/spectrafx-generic-cue-queue`  
**Code commits:**
- `4e7539d298076a3944bf837e570e0cd420e26275` — real Renderer3D + SceneHost Metal test, CMake registration and strict fourth MetalGPU gate
- `4518f775451c32af36a8db8fdd21e95a2fbf433a` — optional offscreen PPM captures
**Last hardware-validated source:** `fceb8caef1bf26085063b55c224631a3c53a9407` (Stage 103, 9/9 acceptance stages and 3/3 prior MetalGPU tests passed on user's Apple M3 Ultra).

## Implemented

- New `tests/spectrafx_metal_scene.mm` uses the **production** `gfx::createMetalDevice`, `render::Renderer3D`, `fx::SpectraFxSceneHost`, normal `ParticleSystem` initialization, GPU emitter assets/cue routes, Metal texture readback and renderer scene passes.
- The fixture defines the regular `aiverseMain` entry point expected by `PlatformMac.mm`, but does **not** launch the AppKit editor or an FPS/RTS game. It creates a small standalone `CAMetalLayer` and four independent RGBA8 + depth offscreen render targets. No game content files are required: the opt-in project-owned `fx/gpu/routes.json` and `fx/gpu/scene_test.gpu.json` are provided through the generic file-reader interface.
- One GPU emitter (forced GPU / no CPU fallback) is spawned through the **actual cue inbox**, and `prepareGpu(dt,primaryView)` + `prepareCpu(dt,primaryView)` are called **exactly once per RHI frame**. Each camera's render is preceded by `prepareRenderView(view)` outside the encoder and followed by `Renderer3D::render(pass)` and `SpectraFxSceneHost::render` inside the opened main pass. The same particle state is drawn in two independent viewports without running simulation twice.
- Each camera receives baseline + FX target. The fixture reads pixels through `gfx::Device::readColorRGBA8` **after** GPU frame completion, compares actual color deltas, checks an opaque blue Renderer3D box occludes the particle center and asserts view B's particle centroid moves relative to view A.
- A live `fx.resetScene()` outside encoders invalidates the old cue epoch, detaches the emitter, keeps GPU slots charged for the in-flight retirement window and returns the next frame to baseline. Four future frames are processed to assert the GPU-retirement slot budget drops to zero. Native RHI `gpuHealthy` is checked after completion.
- Optional visual evidence: set `AIVERSE_SPECTRAFX_SCENE_CAPTURE=/tmp/spectrafx-scene` before running; the new test writes five top-down PPM images:
  `/tmp/spectrafx-scene-baseline-a.ppm`,
  `/tmp/spectrafx-scene-effect-a.ppm`,
  `/tmp/spectrafx-scene-baseline-b.ppm`,
  `/tmp/spectrafx-scene-effect-b.ppm`,
  `/tmp/spectrafx-scene-after-reset-a.ppm`.
  No output image files are created by default or during ordinary CI.

## Build/test wiring

- `CMakeLists.txt` adds macOS-only `aiverse_spectrafx_metal_scene_test`, compiling the standalone Objective-C++ test with ARC and reusing `$<TARGET_OBJECTS:aiverse_runtime>`, `aiverse_runtime`, `aiverse_fx` and `aiverse_render2d`. **No duplicate Metal backend or fake Renderer3D**. Registered CTest `spectrafx_metal_scene` with labels `SpectraFX;MetalGPU;RHI;Renderer3D` and skip code 77 when no Metal GPU exists.
- `tools/spectrafx/acceptance.sh` includes the target before full CTest.
- `tools/spectrafx/verify_metal_ctest.py` expects **4/4** actual MetalGPU tests (`gpu`, `billboard`, `rhi`, `scene`), and prints totals based on the expected set rather than a hard-coded 3.
- `tools/spectrafx/test_verify_metal_ctest.py` updates the standard-library parser fixture to test all four statuses. The older 3/3 passing report is **not** evidence of the fourth test running.
- The existing CI workflow's `tests/spectrafx*`, `CMakeLists.txt` and `tools/spectrafx/**` path filters already pick up all new files; no workflow ownership change was needed.

## Genre independence / compatibility

This adds **tests and diagnostic capture only**; no new FPS, RTS or Editor code enters `engine/fx`. `Renderer3D` still exposes a legacy concrete `RTSCamera` scene API (separately owned by the cameras workstream), but SpectraFX itself uses only `SpectraFxView`. The test intentionally adapts that renderer camera for integration without changing production camera code being worked on by another agent.

The fixture tests **Metal + Renderer3D**, two independent render targets, depth test, callback epoch boundary and retirement. It does **not** imply Vulkan/GLES compute is enabled or that the actual ImGui Editor GPU preview, PRISMA postprocessing composition, multi-view alpha-sorted correctness, production load/stress or memory-leak analysis is completed.

## Actual verification status

**Source review only for Stage 104:** fetched the actual branch implementation, rendered-frame sequence, RHI and existing native tests, applied exactly checked source edits, created and pushed the code commits. Confirmed CMake registration and the 4-name strict Metal parser structure from GitHub source. **No Clang/Objective-C++ native compilation, CTest, Metal M3 Ultra execution, rendered pixels, visual captures or screenshot comparisons were run on this new revision.**

The Stage 103 validated SHA remains distinct. The next agent must not claim Stage 104 PASS until receiving new hardware output tied to this exact stage's source.

## Next unified Mac Studio acceptance

On the user's **clean** detached worktree, preserving the previous test logs when useful:

```bash
cd ~/Projects/AIVERSE-spectrafx-validation
git status --short
git fetch origin
git switch --detach origin/feature/spectrafx-generic-cue-queue

AIVERSE_ACCEPTANCE_REQUIRE_METAL=1 \
AIVERSE_SPECTRAFX_SCENE_CAPTURE=/tmp/spectrafx-scene \
  bash tools/spectrafx/acceptance.sh build/spectrafx-validation
```

Expected macOS CTest MetalGPU set: **four tests**, all **real PASS**, `SKIP=0`, `MISSING=0`, `FAIL=0` at current source SHA. Review `build/spectrafx-validation/spectrafx-acceptance/summary.md` and full `ctest_metal.log`, especially the new `[spectrafx-metal-scene]` pixel/centroid/depth and reset diagnostics; inspect the five PPMs. Any compile/link/depth/pixel failure must be diagnosed and fixed at the source with its own handoff; do not loosen assertions solely to make tests green.

## Remaining next stages

Run real Editor Scene/Game viewport preview interaction (camera orbit, pause/step, changes in authored graph and shader assets), validate PRISMA integration/depth/tonemapping and frame pacing, then stress actual production HybridPlayback with thousands of keyed emitter lifecycles and GPU health/memory metrics. Do not merge the highly divergent feature branch to main until source conflicts, full native acceptance and presentation visuals are reviewed.
