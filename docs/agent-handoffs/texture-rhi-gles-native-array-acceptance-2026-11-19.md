# AIVERSE Texture RHI — native EGL/GLES3 array/mip acceptance
Date: 2026-11-19

## Branch, files and exact commits
- Branch `feature/texture-rhi-unification`; main unchanged.
- `tests/render/gles_texture_array_gpu.cpp`: created in `1a37e7aba632f766b774408bb32b5aa08d5e4dc4`, include correction `8b6ed68201bce235e1cbc1189f54f4358891beeb`.
- `CMakeLists.txt`: `5a5f0dd8058eab95459cc4ba70a2452b9617c671`.
- This handoff was recorded separately.

## Architecture and coverage
A new opt-in CTest `gles_texture_array_gpu` / executable `aiverse_gles_texture_array_gpu_test` is enabled only on Linux when both `AIVERSE_BUILD_TESTS=ON` and `AIVERSE_TEXTURE_GLES_GPU_TESTS=ON`. The test creates an actual EGL ES3 pbuffer and production `createGLDevice(Backend::GLES3)`, with explicit failure (exit 2) if the context is unavailable. It creates an RGBA8 Texture2DArray (4x4 base, two layers, two mip levels). A sentinel texture, nonzero pixel unpack buffer and `GL_UNPACK_ALIGNMENT=8` are pre-bound. It uploads four independent RGBA colors through the actual production `Device::updateTextureRegion` with padded row pitches (20/24/12/16 bytes), then verifies GL texture binding, PBO binding, and unpack alignment are restored after each call. It uses the existing production GL `readTextureAsync` FBO/PBO/fence path to independently read back all four mip/layer combinations and compare each against the expected tightly packed color bytes. It checks GPU health and releases test resources.

This test exercises the prior GL/GLES PBO isolation fix (`1c794a19`) and mip/layer upload path, but it is NOT a WebGL2 browser GPU test: native `readTextureAsync` is intentionally unsupported under Emscripten. No context-loss simulation was added in this stage. No genre dependencies.

## Actual validation
GitHub writes completed and source files were reviewed. Container environment lacks EGL/GLES header development packages and cannot compile or run this native GPU test here. **No CMake, C++ compiler, CTest, GL/GLES GPU hardware, Metal or Vulkan checks have been performed for this stage.** The GitHub commit is not a passing test.

## Run on a Linux machine with EGL GLES3 drivers
```bash
cmake -S . -B build/texture-gles-gpu -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_TEXTURE_GLES_GPU_TESTS=ON
cmake --build build/texture-gles-gpu --target aiverse_gles_texture_array_gpu_test --parallel 4
ctest --test-dir build/texture-gles-gpu --verbose --no-tests=error -R '^gles_texture_array_gpu$'
```
With Mesa/Xvfb, verify the EGL ES3 renderer in logs; no silent skip is allowed. Use a real Android/WebGL2 setup separately for coverage beyond Linux GLES.

## Remaining work
1. Compile the new native test and fix any EGL/compiler/driver issues. Inspect actual per-mip/layer RGBA values, with a strict PASS marker after all assertions.
2. Add a real WebGL2 browser validation path and explicit context lost/restored resource recreation in the platform boundary. Current GL readTextureAsync is unsupported in Emscripten.
3. Validate zero/negative/overflow-prone pitches, GPU memory pressure and failure injection.
4. Finish Vulkan and Metal in-flight atlas retirement tests, GPU timing and streaming soak.
5. Run the established Mac Studio full-engine Texture RHI acceptance once features are complete, and preserve exact tested SHA. Keep PR #21 Draft until hardware evidence.

Per stage: separate handoff records changed files, architecture/API, actual tests/results, limitations, branch/commit, next step.
