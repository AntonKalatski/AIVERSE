# AIVERSE Texture RHI — production WebGL2 VT loss/restore integration

Date: 2026-11-20

## Source branch and commits
Branch: `feature/texture-rhi-unification`. Do not merge to main until final hardware acceptance.
- `tests/render/webgl2_vt_integration.cpp` — production VT harness: `1d7eb6c0b0b4758c400e1bfe8dbed5fd53d2c9cf`; follow-up standard-library include: `464163e14eb9876395b424ae40306fa62735900b`.
- `CMakeLists.txt` — standalone web acceptance executable target: `ec2f1a50a7a4a8ab0a7d75a49cdf011c0f0a77b6`.
- `tests/render/webgl2_context_recovery_browser.mjs` — `--vt` integration checks: `af4790099bd8ddb69550941bd184a26acb813d7f`.
- WebGL2 context-loss platform instrumentation from prior stage under `AIVERSE_WEBGL_CONTEXT_ACCEPTANCE`; this option remains OFF by default.

## Architecture, functions and actual assertions
This acceptance does NOT rely only on JS-created textures. A standalone Emscripten application defines `aiverseMain()` using `platform::run()` and the existing `AppCallbacks`. Each `init(Device&)` allocates a real `gfx::VirtualTextureStreamer` with an RGBA8 Texture2DArray atlas (2 slices, 2 physical mip levels) and an indexed page table. It stages a pinned coarse root page, drains the production upload queue in successive `frame()` callbacks, waits for exact residency and page-table fallback, stages a fine page, and replaces it with another detail to trigger LRU eviction. It checks old-detail fallback, replacement mapping, pinned-root survival and staging-byte retirement. `shutdown()` destroys the previous streamer before platform reinitialization. Four opt-in exported WASM counters report generation, stage, completion count, shutdown count, and error count to browser automation.

`tests/render/webgl2_context_recovery_browser.mjs` optionally takes `--vt` to assert production VirtualTextureStreamer completes root/detail/replacement state progression first and again after each of two browser `WEBGL_lose_context` cycles; no hidden success on missing exports or skipped context-loss extension. Browser pixel oracles already validate separate WebGL2 layer/mip framebuffer readbacks; production VT GPU atlas bytes are NOT independently read back yet. The C++ fixture verifies actual Device uploads and indexed page-table CPU state, not post-render GPU sampling parity.

## Build/reproduction
Requires Emscripten, working WebGL2 browser, Node/Playwright. Example:
```sh
emcmake cmake -S . -B build/web-vt-gpu \
  -DAIVERSE_BUILD_CLIENT=OFF \
  -DAIVERSE_BUILD_TESTS=OFF \
  -DAIVERSE_WEBGL_CONTEXT_ACCEPTANCE=ON
cmake --build build/web-vt-gpu --target aiverse_webgl_vt_acceptance --parallel 4
# Serve build/web-vt-gpu over HTTP; verify the actual output filename exists.
npm install --no-save playwright
npx playwright install chromium
node tests/render/webgl2_context_recovery_browser.mjs \
  http://localhost:8000/webgl-vt-acceptance.html --vt
```
The web target creates `webgl-vt-acceptance.html` with the production web shell. Browser acceptance requires actual GPU support; SwiftShader may supply software-accelerated WebGL2 and does not qualify as mobile hardware coverage.

## Actual verification performed
Read the production streamer, platform, web CMake and previous browser harness via GitHub connector; committed new source through GitHub and updated source includes. **No Emscripten compilation, JavaScript/Playwright execution, WebGL2 GPU readback, CMake, native Metal, Vulkan or GLES runtime test ran in this stage.** The branch remains unverified.

## Unresolved issues / next steps
1. Compile Emscripten target; inspect linker/runtime symbols and browser console; fix compile or logic errors based on actual logs. Ensure CMake's target and shell work with `AIVERSE_BUILD_CLIENT=OFF`.
2. Extend the acceptance to attach/read back the actual production VT atlas and indexed page-table GPU textures using real rendering or an opt-in RHI readback path for WebGL; browser JS probe alone is not evidence of that parity.
3. Validate application shutdown/init when the browser loses context (glDelete on lost context may be invalid; release CPU handles safely). Check repeated loss and loss during partial mip upload, not only after completed VT state.
4. Re-run Linux GLES3 `gles_texture_array_gpu`, Vulkan source compiler/real readbacks, native Metal seven GPU tests, 2048-cycle streaming stress.
5. After feature closure run Mac Studio `bash tools/texture-rhi-final-verify.sh --jobs 4 --full-engine`; require fresh acceptance manifest and record SHA. Keep Draft PR #21, experimental GPU ICB default OFF.

For every subsequent stage record changed files, architecture/API, actual tests and outcomes, limitations, current SHA/branch, concrete next action.
