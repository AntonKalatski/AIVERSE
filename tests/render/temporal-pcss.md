# Temporal PCSS sampling (High HDR)

This change extends the existing PCSS improvement in PR #6. It **does not** introduce a separate shadow-history texture: the existing `ScreenLighting` HDR temporal color history performs reprojection and bounded accumulation.

## How it works

- High/Medium PCF disks have 24/12 taps respectively (low stays at four). Both GLSL and Metal use the same expanded sampling disk.
- A fractional `quality.x` bit (0.5) enables a per-frame golden-angle rotation of the Poisson disk. The integer tap count is unchanged, so there is no U1 buffer-layout change.
- `PostProcess::temporalShadowResolve()` defaults to false. `ScreenLighting` returns true only after its HDR/TAA target has been allocated and `running_` is true. The forward renderer enables sample rotation only in Medium/High and when the active postprocess reports temporal history support.
- Existing `ScreenLightingShaders.h` history validation now tests previous-camera distance for the exact receiver on depth edges, reducing trails from foreground silhouettes and maintaining the weight of nearby soft shadows on continuous surfaces.

## Quality / limitations

- The temporal sample rotation is only effective when `ScreenLighting::setEnabled(true)`, HDR post quality High, and an active post chain are present. Without TAA, samples remain screen-space deterministic, so there is no additional flicker.
- Temporal **color** accumulation may also smooth material/specular variation; this is not a separate shadow-only denoiser. Further improvements would require dedicated screen-space shadow-mask and receiver-normal history textures.
- Sample budget increases PCF shading cost; benchmark Metal GPU frame time, especially on wide penumbrae and leaf silhouettes.
- A per-frame time seed derives from `camPos.w` / `graphTime_` quantized to 60 Hz. It is only a sample decorrelation seed, not motion reprojection.

## Validation checklist

1. Run `python3 tests/render/check_model_lod_shaders.py` to check GLSL desktop/GLES model variants.
2. Build `aiverse_hdr_scene` and (on macOS) `aiverse_hdr_gl` with `AIVERSE_BUILD_HDR_TESTS=ON`.
3. Capture `AIVERSE_CONTACT_SHADOW_TEST=1` with `AIVERSE_SHADOW_FIXED=1` and without it, following `tests/render/contact-shadows.md`.
4. With high-quality postprocessing and ScreenLighting on, pan the camera across the building and foliage shadow edges; look for remaining stipple and ghosting at depth discontinuities.
5. Repeat with ScreenLighting off: there must be no new shadow flicker. Profile performance on Metal.

**Static integration checks** were completed for the matching GLSL/MSL shader blocks, macro quoting, postprocess gating, and temporal depth-validation wiring. Actual C++/shader compilation and frame capture require a local rendering backend and have not been run here.
