# AIVERSE Texture RHI — WebGL2 seam fixture QA and diagnostic gate
Date: 2026-11-20

## Branch / files / commits
Branch: `feature/texture-rhi-unification` (main unchanged).
Changed file: `tests/render/WebGL2VtSeamOracle.h`.
- `cdc872d31d7bd4a3f5aa512ca3ad3da5acc13c50`: independent oracle sensitivity gate and diagnostic output including address mode, UV, pixel, RGBA channel, expected, actual and delta.
- `8d365aaf19cadb475f5cc051e913342f07493b1b`: calibrates the sensitivity gate to >=3 for the current authored gradient, greater than the actual allowed GPU/CPU tolerance of 2 RGBA8 levels.

## Intent and architecture
The existing opt-in WebGL2 fixture samples canonical `gfx::kVirtualTextureGLSL` with Repeat/MirrorRepeat and 1-pixel physical gutters. The CPU oracle uses independent 12x6 logical source pixels and bilinear interpolation, not the GPU atlas coordinate implementation. A new sensitivity assertion rejects a degenerate source if the internal tile boundary midpoint becomes indistinguishable (within the fixture's allowed <=2 levels) from the legacy incorrect centre-of-interior sample.

When GPU pixels differ by >2, the fixture now logs the exact mode, sample UV, output pixel coordinate, channel, independent expected byte, observed GPU byte and delta instead of only a generic FAIL. This is diagnostic test improvement; no production shader or runtime texture API was changed.

## Verification actually done
GitHub source reads/commits succeeded. The independent numeric oracle was evaluated locally in Python (no renderer/GPU) for u={0.48,0.50,0.52,1.02} and v=0.5:
- Repeat RGB samples: (80,94,166), (83,95,165), (85,95,163), (59,84,186).
- MirrorRepeat RGB samples: (80,94,166), (83,95,165), (85,95,163), (132,117,121).
- Midpoint RGB differs from first interior right-tile texel by maximum **3** 8-bit levels, so threshold >=3 is the correct sensitivity gate (>=4 would incorrectly fail this fixture).

These are real CPU numeric calculations, **not** GLSL compilations, Vulkan/Metal/GLES tests or GPU pixels. The container has `clang++` but no `emcc` or `glslangValidator`; its direct GitHub git/HTTPS access is blocked by DNS. No Emscripten, browser Playwright, native hardware GPU build or complete CTest suite ran in this stage.

## Unresolved / next agent action
1. Run the opt-in Emscripten target `aiverse_webgl_vt_acceptance` with `AIVERSE_WEBGL_CONTEXT_ACCEPTANCE=ON` on a machine with Emscripten and execute `tests/render/webgl2_context_recovery_browser.mjs <actual-page-url> --vt` in a working Playwright Chromium browser. Investigate compiler errors and compare actual GPU pixels with the numeric values above.
2. In particular inspect shaders, RHI framebuffer readback/callback ordering and WebGL context restoration. Do not weaken the <=2 byte tolerance in response to genuine differences.
3. Build/test native Linux EGL/GLES3 texture array, Vulkan upload, Metal texture GPU, and 2048-cycle portable VT stress. Then run Mac Studio final one-pass `bash tools/texture-rhi-final-verify.sh --jobs 4 --full-engine`; record actual SHAs, output and PASS/failures.
4. Maintain Draft PR #21; keep experimental Metal ICB disabled by default until hardware gates pass. After any further changes create another handoff with implementation, architecture/API, touched paths, actual tests/outcomes, branch/commit, limitations and next step.
