# Texture RHI: GLES/WebGL upload safety and streaming soak
Date: 2026-11-19

## Branch and commits
- Branch: `feature/texture-rhi-unification` (main unchanged).
- `engine/gfx/gl/GfxGL.cpp`: `1919f7e29b99d93883b30c784174025cd4844ea3`, include correction `c88fbb4ae8a966e944c34ed0441b4d39d8bf55e6`.
- `tests/render/virtual_texture_mip_pipeline.cpp`: `c1703d71fd9598f36d2ccfe1cefb01c59505c8db`.

## Changes and architecture
- GL33/GLES3/WebGL2 backend `updateTextureRegion`: validates row-byte multiplication and rowPitch*height against signed integer overflow before allocating/updating the GL image. Preserves and restores the context's previous `GL_UNPACK_ALIGNMENT` after each update, and sets a descriptive `lastError` on GL upload error. The source uploader still uses per-row `glTexSubImage2D/3D` for padded input rows.
- Portable virtual texture test `repeatedCancellationAndLruReuse` now exercises 2048 cycles rather than 96. After every cycle it checks that staging bytes and queued tickets are zero, the pinned coarse root remains present, and streamer remains ready. The previous checks already cover invisible partial mip chains, cancellation, LRU eviction, and indexed table fallback.
- All changes are genre-independent in `engine/gfx` and its tests.

## Tests actually run
GitHub file updates succeeded. **No GL/GLES/WebGL runtime test, native Metal/Vulkan GPU test, C++ build, or CTest was executed for these changes.** Do not mistake expanded fixtures or successful commits for PASS.

## Limitations / risks
- GL's `glGetError` reports context error state; pre-existing errors may affect upload attribution. The GL texture binding, unpack row length and other context state are not completely normalized/restored in this stage; investigate with a dedicated integration test.
- Actual GLES/WebGL2 texture array support, browser-specific pixel store behavior, context loss and recovery, and shader visual parity remain unproven.
- The 2048-cycle test uses a mock GPU; it does not establish physical GPU memory fences, frame overlap, or final Metal blit correctness.
- Previous Vulkan and Metal upload changes still await native compilation. Keep PR #21 Draft and experimental GPU-authored ICB default OFF.

## Next agent steps
1. Run `aiverse_virtual_texture_mip_pipeline_test` and `ctest -R '^virtual_texture_mip_pipeline$'` in the final consolidated build. Fix new assertions/compilation if needed.
2. Add GLES/WebGL2 real-context GPU texture array readback + multiple-mip cases with `GL_UNPACK_ALIGNMENT` state pre/post and padded row source tests.
3. Exercise context-loss/restore and GL error propagation; check the renderer's resource ownership interactions.
4. Add runtime streaming soak that forces GPU frame overlap and validates atlas readback plus page-table propagation. Complete Vulkan and Metal real readback gates.
5. Run full Mac Studio `bash tools/texture-rhi-final-verify.sh --jobs 4 --full-engine` once feature development ends. Preserve exact SHA/log manifest; require a real PASS before merging.

Per-stage agent handoff must document API, files, branch/commit, actual tests/results, limitations and next action.
