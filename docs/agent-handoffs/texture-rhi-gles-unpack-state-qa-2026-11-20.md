# AIVERSE Texture RHI — GLES3/WebGL2 unpack-state isolation QA
Date: 2026-11-20

## Branch / implementation / exact commits
Branch `feature/texture-rhi-unification`, main untouched.
- `engine/gfx/gl/GfxGL.cpp`: `660f333001717d8801ae6410f0ed533511f72948` (dimensional texture creation isolation), `a5b00a838e81ebb30ded84009ab19ff822c0dacd` (valid local enum constants), `02acb42e7c24b353a562508d64772250be77c637` (region upload full pixel-store state).
- `tests/render/gles_texture_array_gpu.cpp`: `2b95bd32c49a9ba043ae5b37f91c479a61baedaf` (native GPU regression exercising dirty unpack context).

## Root cause and API
The shared GLES3/GL33/WebGL2 RHI uses `glTexImage3D` for Texture2DArray/3D allocation and per-row `glTexSubImage2D/3D` in `updateTextureRegion`. Both previously inherited the caller's `GL_PIXEL_UNPACK_BUFFER` binding and/or unpack pixel-store layout parameters; PBO binding turns a host pointer into an offset, and `UNPACK_SKIP_PIXELS/ROWS/IMAGES` can corrupt source addressing even for single-row calls.

Allocation now uses a local RAII guard (covers early error returns), preserving/restoring bound texture, bound unpack PBO, alignment, ROW_LENGTH, IMAGE_HEIGHT and SKIP_PIXELS/ROWS/IMAGES, while forcing canonical unpack settings (PBO=0, alignment=1, other layout/skip=0) for the actual upload. Region update adds analogous explicit pixel-store save/reset/restore to existing PBO+texture-binding isolation. Public gfx::Device API unchanged, genre-neutral.

The opt-in Linux EGL/GLES3 GPU test deliberately leaves a 4-byte unpack PBO bound and all five nondefault layout states before creating a 4x4x2, 2-mip Texture2DArray. It requires all prior state to survive texture allocation, and confirms the same state after each of four padded-row mip/layer region uploads. It also reads back actual GPU pixel bytes through production RHI.

## Actual verification
GitHub edits saved; relevant gfx API and production source inspected. **No Emscripten build, native GLES3 compile or GPU readback, Metal/Vulkan run, CMake, CTest, Playwright, or complete source build was run in this stage.** A prior CPU-seam oracle is not GPU PASS. Expect compile/runtime correction once real logs are available.

## Known risks
- Other legacy GL upload paths may also mutate unpack/PBO state: `createTexture(Texture2D)`, older `updateTexture`, compressed uploads, and indirect tools should receive similar hygiene after auditing, without silently changing performance behavior.
- Resource creation queries use GLES3 standard unpack enums; verify Linux GLES3, Android GLES3, Windows GL33 and browser WebGL2 compile paths.
- Context loss while resource destruction is in progress still requires browser hardware tests.
- All WebGL2 VT shader sampling/nonuniform seam oracles are source-added but unverified.

## Concrete next steps
1. On an EGL/GLES3-capable Linux host: `cmake -S . -B build/texture-gles-gpu -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_TEXTURE_GLES_GPU_TESTS=ON`; build target `aiverse_gles_texture_array_gpu_test`; `ctest --test-dir build/texture-gles-gpu --output-on-failure -R '^gles_texture_array_gpu$'`.
2. On Emscripten + browser: build `aiverse_webgl_vt_acceptance` with `AIVERSE_WEBGL_CONTEXT_ACCEPTANCE=ON` and run Playwright `--vt`. Fix shader/API compilation using actual error logs, and preserve test outputs.
3. Close remaining legacy GL upload state paths and GPU loss during in-flight callbacks; run Vulkan/Metal real GPU and the 2048-cycle portable VT stress.
4. Once feature development is closed run `bash tools/texture-rhi-final-verify.sh --jobs 4 --full-engine` on Mac Studio. Keep Draft PR #21; do not claim hardware PASS before proof.

After each change record API, paths, branch/current commit, actual tests/results, remaining risks and next step in its own handoff.
