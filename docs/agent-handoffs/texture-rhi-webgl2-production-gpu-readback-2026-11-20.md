# AIVERSE Texture RHI — production WebGL2 atlas + indexed table pixel oracle
Date: 2026-11-20

## Branch and implementation commits
- Branch: `feature/texture-rhi-unification`; main untouched.
- `engine/gfx/gl/GfxGL.cpp`: `87f9510a9b5c166de98219cbe48b24f23d968a12`.
- `tests/render/webgl2_vt_integration.cpp`: `b106fee9bc62dfc5a545dbf198c2d0566cd9a798`.
- `tests/render/webgl2_context_recovery_browser.mjs`: `d17172ab30d917ee9d4e127a7beb18b9db0dc61c`.
- CMake option `AIVERSE_WEBGL_CONTEXT_ACCEPTANCE` was registered earlier and remains OFF by default.

## Architecture and API contract
For Emscripten builds **only when** `AIVERSE_WEBGL_CONTEXT_ACCEPTANCE=ON`, the production GLES3 Device now supports bounded Texture2DArray RGBA8 / sRGB opt-in GPU readback using FBO attachment `glFramebufferTextureLayer` and synchronous `glReadPixels` into temporary host memory. This WebGL2-only path limits bytes to 256 KiB and 16 outstanding callbacks. It saves/restores READ_FRAMEBUFFER_BINDING, PIXEL_PACK_BUFFER_BINDING, PACK_ALIGNMENT and READ_BUFFER; unbinds pack PBO to ensure `glReadPixels` writes host memory. It flips the bottom-up readback to the RHI top-down pixel contract. Crucially, the **pixel read is synchronous**, but the API callback is deferred until the next beginFrame; do NOT describe the actual GPU transfer as asynchronous. Production WebGL builds with acceptance flag OFF retain the previous unsupported readTextureAsync behavior and incur no test-path overhead.

The independent C++ acceptance app exercises actual `VirtualTextureStreamer`: pinned coarse root (slice 0), fine detail (slice 1), replacement detail at another virtual tile via LRU, indexed table republish. On final residency, it queues three readbacks directly from production RHI TextureHandles:
1. Root atlas slice 0: 4x4 RGBA8 color (28,75,98,255).
2. Recycled detail slice 1: 4x4 RGBA8 color (11,205,47,255).
3. Indexed page-table array slice 0: 4x4 RGBA8; all tiles map to slice 0 / mip 2 except (3,3), which maps to slice 1 / mip 0.
Callbacks compare exact pixels, and the VT generation only completes after all 3 succeeded. The Playwright `--vt` mode now requires stage=4 and gpu_reads=3 in the initial generation and after each of two `WEBGL_lose_context` recovery cycles.

This covers production upload bytes and page-table propagation; it does **not** prove real material shader bilinear/trilinear sampling parity or in-flight GPU draw overlap.

## Actual verification
GitHub source and handoff updates succeeded. No Emscripten compilation, Playwright execution, browser WebGL2 GPU readback, CMake, CTest, or native Metal/Vulkan/GLES tests were run this turn. Source-level changes alone DO NOT establish a passing backend. Expected callback and compiled shader/GL API portability remain unverified.

## Reproduction
```sh
emcmake cmake -S . -B build/web-vt-gpu \
  -DAIVERSE_BUILD_CLIENT=OFF -DAIVERSE_BUILD_TESTS=OFF \
  -DAIVERSE_WEBGL_CONTEXT_ACCEPTANCE=ON
cmake --build build/web-vt-gpu --target aiverse_webgl_vt_acceptance --parallel 4
# Serve the build directory on HTTP, verify actual artifact name/URL.
node tests/render/webgl2_context_recovery_browser.mjs \
  http://localhost:8000/webgl-vt-acceptance.html --vt
```
Requires installed Node/Playwright Chromium, live browser WebGL2 context and WEBGL_lose_context extension. Test must fail on missing prerequisites, missing exports or wrong GPU pixels.

## Next work
1. Run actual Emscripten compile and browser acceptance; fix compiler/linkage/Playwright and JS WebGL state restoration errors based on output.
2. The fixture currently compares page-table texel bytes and physical atlas uniform pages, but has no full material/shader sampler oracle. Add a fragment render-and-readback reference for page edges and mip transitions once the backend compiles.
3. Check WebGL context restoration while in-flight stream batches and deferred callbacks exist, plus repeated context loss and GL resource cleanup.
4. Complete Linux EGL/GLES3, Vulkan and Metal physical tests, streaming pressure and per-frame performance.
5. Execute Mac Studio full texture acceptance `--full-engine` after all source work and preserve SHA/result. Leave PR #21 Draft and GPU-authored ICB OFF by default.

Each following step needs separate AGENT handoff: changed paths, architecture/API, actual tests+results, branch/commit, limitations, concrete next action.
