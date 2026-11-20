# AIVERSE Texture RHI — canonical WebGL2 nonuniform bilinear seam oracle
Date: 2026-11-20

## Branch and commits
Branch: `feature/texture-rhi-unification`; main untouched.
- `tests/render/WebGL2VtSeamOracle.h`: `1d94ffd5434521090687bba48ba82bd632b9e15b`.
- `tests/render/webgl2_vt_integration.cpp`: `7e34272478f3bddffb22e0cb2d865405daf06878`, final marker `694df247f8d3c38b7922bb5353646037b420b551`.
- `tests/render/webgl2_context_recovery_browser.mjs`: `e54db97b968b88439849d461343c9e965783549c`.

## Implementation and API
The opt-in standalone Emscripten acceptance app now owns an independent `aiverse::test::WebGL2VtSeamOracle` during each GPU Device generation. It creates two authored 8x8 RGBA8 Texture2DArray atlases, one with Repeat boundary gutters and another with MirrorRepeat gutters, a shared 2x1x1 indexed page-table mapping two virtual page columns to physical slices 0 and 1, and a 4x4 RGBA8 render target.

Each atlas physical texel is authored from immutable logical 12x6 RGB gradient source coordinates with a one-pixel border. The shader is constructed by embedding the **actual** production `gfx::kVirtualTextureGLSL`, invoking `aiverseSampleVirtual` with gutterPixels=1 and address mode Repeat or MirrorRepeat. A full-screen triangle samples u={0.48,0.50,0.52,1.02}, v=0.5. The independent CPU reference performs direct original-source bilinear interpolation and wraps/mirrors logical integer texel coordinates, **not** shader page lookup or physical UV conversion. Results from the production RHI framebuffer readback have a strict <=2 RGBA8-channel tolerance across all 16 pixels per mode.

The two GPU draws and readbacks are performed only after earlier production `VirtualTextureStreamer` atlas/table and trilinear acceptance passes. The app increments its completed generation only when the seam probe callbacks have completed without failure and exposes `_aiverse_web_vt_seam_verified()`. Playwright `--vt` now requires this result on the initial Device and both `WEBGL_lose_context` recovery cycles, as well as all prior source-color, LRU and shader/trilinear assertions.

This adds no genre-specific API or production shader implementation. It is a test-only reusable helper and a change to existing opt-in acceptance.

## Checks actually completed
GitHub source create/update actions succeeded, and relevant C++/GLSL production signatures were inspected through the GitHub connector. **No Emscripten/C++ compiler, WebGL2 browser, shader compiler, native Metal/Vulkan/GLES hardware or CTest was run this turn.** Therefore tests are authored but NOT passed; exact shader pipeline compilation and GPU readback/edge oracle correctness remain to verify. Prior successful Mac Metal runs were for older source SHAs.

## Next actions
1. Compile opt-in web target: `emcmake cmake -S . -B build/web-vt-gpu -DAIVERSE_BUILD_CLIENT=OFF -DAIVERSE_BUILD_TESTS=OFF -DAIVERSE_WEBGL_CONTEXT_ACCEPTANCE=ON` then `cmake --build build/web-vt-gpu --target aiverse_webgl_vt_acceptance --parallel 4`. Fix actual errors; do not invent a successful build.
2. Serve `webgl-vt-acceptance.html` from its actual build directory and run `node tests/render/webgl2_context_recovery_browser.mjs http://localhost:8000/webgl-vt-acceptance.html --vt` with Playwright Chromium installed. Require zero failures and all three device generations completing source GPU and seam oracles.
3. If observed GLSL/GL differences exceed 2 RGBA levels, record exact mode/u/expected/actual and identify addressing, filtering, page-table or framebuffer state bugs. Do not merely weaken tolerance.
4. Include more nonuniform page/mip transitions, anisotropic taps, true cross-frame resource overlap, real browser device loss and mobile WebGL support, plus performance budgets.
5. Run Linux GLES3 native texture GPU test; Metal/Vulkan real GPU tests; full engine Mac Studio one-shot `bash tools/texture-rhi-final-verify.sh --jobs 4 --full-engine` after feature work. Keep PR #21 Draft and GPU-authored ICB default OFF until evidence supports merging.

## Handoff rule
After each stage preserve branch/commit IDs, changed file list, exact code and APIs, tests actually executed/results, unverified risks and next explicit step for the next AI agent.
