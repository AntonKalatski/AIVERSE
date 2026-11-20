# AIVERSE Texture RHI — GLES3/WebGL2 upload-state isolation
Date: 2026-11-19

## Branch / commits
Branch: `feature/texture-rhi-unification` (main unchanged).
Implementation: `1c794a199980e51e79e644b278fd22a05baff7a3`.
Changed `engine/gfx/gl/GfxGL.cpp` (`Device::updateTextureRegion` GL implementation).

## Problem and architecture
The region-upload path previously saved/restored `GL_UNPACK_ALIGNMENT` but left the caller's texture binding mutated, and accepted an arbitrary previously bound `GL_PIXEL_UNPACK_BUFFER`. When a pixel unpack buffer is bound, the pointer passed to `glTexSubImage2D/3D` is interpreted as a buffer offset, not a host pointer. This is a correctness and potential invalid-memory-access issue for the CPU-copy streaming queue sharing a GL context with other uploads.

## Implemented
- Resolve the relevant binding enum for GL_TEXTURE_2D, GL_TEXTURE_3D, GL_TEXTURE_CUBE_MAP or GL_TEXTURE_2D_ARRAY (literal GL enum values remain portable to desktop GL loader).
- Save current binding on the active texture unit, current `GL_PIXEL_UNPACK_BUFFER_BINDING`, and `GL_UNPACK_ALIGNMENT`. On GL state-query error, fail closed before an upload.
- Unbind PBO before CPU pointer uploads, bind target texture, and set unpack alignment = 1 for tightly packed per-row copies.
- Restore unpack alignment, texture binding and unpack PBO after upload; report error if either upload or restoration fails and clear stale own error on success.
- Preserve the existing integer-overflow checks and row/slice pitch bounds. Existing public texture and streaming APIs unchanged.

## Actual verification
GitHub change committed; **no GL/GLES3/WebGL2 compile, real GPU render/readback, CTest, Vulkan or Metal test has run during this stage**. Do not claim runtime PASS.

## Remaining risks
- GL state from earlier operations may already contain errors. The new path fails closed but `glGetError` consumes one preexisting error for diagnosis.
- Current source assumes WebGL2/GLES3 PBO state, which is required by GL ES 3.0 / WebGL2, but must be validated in browser and mobile builds.
- No context-loss/recovery handling added: GL name recreation and upload retry require a separate lifecycle design. A GPU test should force PBO/nondefault bindings and verify both texture bytes and complete state restoration.
- Native GPU readback parity, multiframe atlas retirement and long-duration stress still need execution. Metal/Vulkan and shader source changes are pending their final consolidated build.

## Next stage
1. Add an actual GL ES 3 / WebGL2 context test that initializes a 2D array with two mips, binds nondefault unpack PBO and texture state, uploads padded RGBA rows via production RHI, asserts state restored, and independently reads each array slice/mip through FBO.
2. Implement explicit context-loss handling and resource recreation at platform/RHI boundary only after confirming current lifecycle callbacks and texture ownership.
3. On Mac Studio when features complete: `bash tools/texture-rhi-final-verify.sh --jobs 4 --full-engine`. Separately run Android/WebGL2/Vulkan GPU checks and keep PR #21 Draft until complete evidence.

Handoff rule: after every stage document files, APIs, actual tests, limitations, SHA/branch and next action.
