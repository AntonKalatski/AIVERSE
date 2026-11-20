# SpectraFX Stage 107 — PRISMA HDR/Bloom integration acceptance

Date: 2026-11-20
Branch: `feature/spectrafx-generic-cue-queue`
Implementation commit: `9ee2ffeb5a0ff3a0d3088e291b098a95279994dc`
Previous Stage 106: `f12afc07483523b34923f1fc1378c654cf40c9fb`.
**Last native verified source:** `c4fc948535557f1cc1a00459d1a3cd48257ad6ca` (Apple M3 Ultra, 126/126 full CTest, 4/4 hardware MetalGPU). This Stage 107 code has **not yet been native built/tested**.

## Implementation and architecture

Updated `tests/spectrafx_metal_scene.mm` only. The existing production Renderer3D/SceneHost test was augmented with a new PRISMA postprocessing pass, not replaced with a mock. No game/genre core logic or PRISMA source changed.

After the existing camera RT, depth and rollback tests, the fixture:
- Initializes real `render::PostFx` on the same `gfx::Device`, configures `hdr:on` + High quality Bloom + neutral color grade, checks `active()` and `hdrRequested()`.
- Replays a fresh `SpectraFxCue` in the new scene epoch after reset, calls `SceneHost::prepareGpu(dt, view)` and `prepareCpu(dt, view)` once per frame, verifies real GPU backend selection.
- Renders baseline and FX into separate offscreen RGBA8/depth targets using `Renderer3D::render(pass, &prisma)`, writes optional particles **inside PRISMA's linear HDR scene pass**, invokes `Renderer3D::resolvePostFx()`, and closes its final output pass. The device follows real Metal pipeline variants and PRISMA-owned HDR scene textures, not a test-only renderer.
- Reads both completed output targets using diagnostic-only `gfx::Device::readColorRGBA8`. Asserts actual PRISMA passes, HDR scene selection, detectable FX contribution and foreground depth occlusion after the post chain.
- Allows optional PPM screenshots `...-prisma-baseline-a.ppm` and `...-prisma-effect-a.ppm` when `AIVERSE_SPECTRAFX_SCENE_CAPTURE=/tmp/spectrafx-scene`; no captures by default.
- Invokes live reset outside render encoders, processes four more GPU frames, verifies retiring slots empty, serializes completed GPU work via diagnostic readback, and releases PRISMA-owned GPU resources before the RHI.

## Boundaries and limitations

PRISMA owns Bloom, HDR scene target, tonemapping and PostFx state. SpectraFX only provides presentation particle drawing in Renderer3D's open scene pass. No dependencies on FPS, RTS, Editor or gameplay authority are introduced into `engine/fx`.

This tests HDR/Bloom on Metal with one FX camera and a foreground occluder, plus LDR/multi-camera baseline tests inherited from Stage 104. It does NOT verify live Editor ImGui composition, temporal screen-lighting, visual image comparison performed by a human, Vulkan/GLES GPU compute, sorted-alpha multi-camera pixel index, or high-volume stress.

## Actual verification

The source was inspected through GitHub, source anchors checked, and the updated test fixture committed/pushed. **No native Mac Studio build, Metal test, CTest, screenshots or performance measurements were executed for Stage 107**. The previously passing 4/4 hardware tests belong to Stage 105 code SHA, not this change. Standard acceptance script already builds and runs `spectrafx_metal_scene` and checks strict 4/4 Metal; no test list updates required.

## Next action

Implement real two-camera GPU alpha-sort index parity test and high-load HybridPlayback stress, then run one strict full native acceptance:

```bash
cd ~/Projects/AIVERSE-spectrafx-validation
git status --short
git fetch origin
git switch --detach origin/feature/spectrafx-generic-cue-queue
AIVERSE_ACCEPTANCE_REQUIRE_METAL=1 \
AIVERSE_SPECTRAFX_SCENE_CAPTURE=/tmp/spectrafx-scene \
  bash tools/spectrafx/acceptance.sh build/spectrafx-validation
```

Check new `[spectrafx-metal-scene] prisma_hdr` diagnostics, CMake compiler logs, strict Metal evidence, 126+ full CTest and PPMs. Correct failures at their source with separate stage handoff; do not relax depth/pixel assertions without analysis. Preserve independent branches, avoid blind `main` merge.
