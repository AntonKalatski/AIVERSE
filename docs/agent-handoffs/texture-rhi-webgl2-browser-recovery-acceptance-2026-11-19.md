# AIVERSE texture RHI — opt-in WebGL2 browser recovery acceptance

Date: 2026-11-19

## Branch / commits
Branch `feature/texture-rhi-unification` (no merge to main).
- `engine/platform/web/PlatformWeb.cpp` read-only acceptance counters: `87e76cdbf20383fc5a7b96eec67e01e8c6021a89`
- `tests/render/webgl2_context_recovery_browser.mjs` Playwright browser probe: `f0a92f7125aa7f9eae1822c53dd18b596f015431`, option handling correction `cf026b446c945fec268d19c80f7716b37b86f5da`
- `CMakeLists.txt` opt-in test compilation: `49d44d8be25d0062e710bccb85829f0cabe1b9ba`.

## Test API / architecture
`AIVERSE_WEBGL_CONTEXT_ACCEPTANCE` defaults OFF in web builds. When ON, `PlatformWeb.cpp` exports four Emscripten KEEPALIVE read-only functions:
- `_aiverse_web_test_generation()`: increments after initial `init(Device&)` and every completed recovery.
- `_aiverse_web_test_frames()`: increments after a complete platform-owned `beginFrame/frame/endFrame`.
- `_aiverse_web_test_losses()`: increments on browser context-loss callback.
- `_aiverse_web_test_suspended()`: true when platform has suspended frames.

The Playwright JS script loads an already-built/served AIVERSE WebGL2 page, requires observed initial frames, exercises the real browser `WEBGL_lose_context` extension twice, verifies lost flag and stable frame count, restores context, verifies device generation increments and frames resume, then uses WebGL2 `TEXTURE_2D_ARRAY` uploads plus `framebufferTextureLayer/readPixels` to validate four combinations of layer/mip GPU pixels. Every failure causes a nonzero exit, including missing extension, runtime counters, or actual browser GPU availability. No silent skips.

The array-pixel probe checks browser WebGL2 behavior, **not** AIVERSE `Device::updateTextureRegion` itself: the previously added Linux EGL/GLES3 test is the production backend upload/readback gate. The browser checks platform lifecycle via actual AIVERSE counters, not by guessing from a console line.

## Run instructions
Requires an Emscripten toolchain, web application build that uses `PlatformWeb.cpp`, Node and Playwright Chromium. Example configure flag:
```sh
emcmake cmake -S . -B build/web-texture-acceptance -DAIVERSE_BUILD_TESTS=OFF -DAIVERSE_WEBGL_CONTEXT_ACCEPTANCE=ON
cmake --build build/web-texture-acceptance --parallel 4
# Serve the produced WebGL app HTML/JS/WASM locally, then:
npm install --no-save playwright
npx playwright install chromium
node tests/render/webgl2_context_recovery_browser.mjs http://localhost:8000/<actual-built-page>.html
```
The actual build target/output page name depends on the host CMake configuration; do not assume the URL exists without inspecting build output. Chromium SwiftShader is permitted for functional WebGL2 acceptance but is not physical mobile/browser hardware coverage. Confirm the browser logs and process exit code.

## Actual verification
GitHub code writes succeeded and file snippets were inspected. **No Emscripten compilation, Node invocation, browser GPU test, physical GLES/WebGL device, CMake or CTest occurred here.** Treat script as added but unverified. A previous Mac acceptance at a much older SHA does not cover these changes.

## Risks and next agent tasks
1. Run web build with opt-in counters and Playwright; diagnose test failures, shader context loss/recreation crashes, Emscripten export visibility, and browser GL errors.
2. Verify `S.cb.shutdown()` genuinely releases old streaming queues, virtual page tables, shaders and texture handles after context loss and that `S.cb.init()` reconstructs them using host-side asset bytes. The current platform cannot resurrect arbitrary GPU-only data by itself.
3. Add an end-to-end WebGL2 native AIVERSE Device fixture that performs real `updateTextureRegion` through `gfx::Device`, then test forced context loss with VT resident pages, streamed page republish and independent readback. Do not claim the browser probe proves that integration.
4. Exercise background tabs, rapid repeated context losses, page errors, Android/iOS browsers, WebGL2-specific texture format limits.
5. Complete Linux GLES3 GPU, Metal/Vulkan GPU and 2048-cycle mock streaming acceptance. At feature completion run the established Mac Studio full-engine one-shot and preserve SHA/logs. Keep PR #21 Draft, Metal GPU ICB default OFF.

Every further step requires a separate handoff with branch, files, API, actual tests/results, limitations and next action.
