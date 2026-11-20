# AIVERSE Texture RHI — legacy GL/GLES/WebGL2 texture upload closure
Date: 2026-11-20

## Branch, files, exact commits
Branch `feature/texture-rhi-unification`; main not merged.
- `engine/gfx/gl/GfxGL.cpp` — `7b8324a303bd1b91e7f72da6f3b5f614b23b53bb`
- `tests/render/gles_texture_array_gpu.cpp` — `7e5491538a8ecf39d7741035a6721131cdde74a0`

## Problem and implementation
Previous stages isolated the dimensional Texture2DArray and generic updateTextureRegion paths. The remaining legacy `createTexture(Texture2D)` path, including compressed Texture2D mip uploads, still used caller-owned GL_PIXEL_UNPACK_BUFFER and GL_UNPACK_* context state, and could leave the previous binding modified. Host pointers can become PBO offsets, while UNPACK_ROW_LENGTH / SKIP_* can shift input source rows.

The Texture2D path now wraps allocation/upload in a scoped unpack-state guard covering normal and early-return paths. It saves texture 2D binding, unpack PBO binding, UNPACK_ALIGNMENT, ROW_LENGTH, IMAGE_HEIGHT, SKIP_PIXELS, SKIP_ROWS and SKIP_IMAGES; disables PBO and resets pixel-store layout during upload; restores the previous state. Compressed Texture2D creation additionally rejects absent level arrays, empty mip payloads and sizes above GLsizei max before dereferencing / passing to GL, and checks GL upload errors instead of registering a handle on failure.

The legacy `Device::updateTexture(h,x,y,w,h,pixels,rowPitch)` now constructs `TextureUploadDesc` and delegates to the existing validated `updateTextureRegion()`, retaining the public void API but using strict bounds/pitch validation and pixel-store/PBO isolation. On rejected input, lastError provides a diagnostic instead of leaving shared GL state modified. No RTS/FPS coupling; normal GL API unchanged.

## Regression fixture
The opt-in native Linux EGL/GLES3 `gles_texture_array_gpu` test now also creates an ordinary 4x4 RGBA8 Texture2D while a 4-byte unpack PBO and dirty unpack row/skip state are bound, checks that creation preserves all state, calls the legacy `updateTexture()` with padded 20-byte rows, checks state and empty lastError, and independently reads GPU pixels through production `readTextureAsync`. Existing array/mip readback checks remain.

## Actual verification
Code retrieved and edited in GitHub, successful commits returned. **No CMake, clang/GCC compilation, Emscripten build, native GLES3 GPU test, Playwright, Metal, Vulkan or CTest were executed in this stage.** The prior statuses were code-added, not hardware PASS.

## Limitations and next steps
- Need verify all GL code paths compiled under Windows GL loader, Linux GLES3, Android GLES3 and Emscripten WebGL2; untested C++ compile/driver assumptions may require fixes.
- Investigate whether `glGetError` attribution, compressed upload content sizes, preservation of framebuffer binding and unusual 2D typed formats require further hardening.
- Run native Linux EGL GLES3 acceptance: `cmake -S . -B build/texture-gles-gpu -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_TEXTURE_GLES_GPU_TESTS=ON`, build `aiverse_gles_texture_array_gpu_test`, then `ctest --test-dir build/texture-gles-gpu --output-on-failure -R '^gles_texture_array_gpu$'`.
- Run WebGL2 standalone acceptance via Emscripten target `aiverse_webgl_vt_acceptance` and Playwright `--vt`. Verify shader seams and browser context loss.
- Run Metal/Vulkan and full texture suite once feature work done: `bash tools/texture-rhi-final-verify.sh --jobs 4 --full-engine` on Mac Studio. Record tested SHA, complete logs, true pass/fail counts.
- Keep PR #21 Draft, experimental Metal ICB default OFF. After each subsequent stage update handoff covering architecture/API, modified files, actual tests/results, limitations, branch/commit and next action.
