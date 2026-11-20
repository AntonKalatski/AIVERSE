# Texture RHI — alpha-fringe test fixture correction
Date: 2026-11-20

## Branch / changes / commit
- Branch: `feature/texture-rhi-unification`; main not modified.
- Updated `tests/render/virtual_texture_mip_pipeline.cpp` in commit `721258403d09a026250e5db8892a4524b7a2cee7`.
- No production code changed.

## Reported failure
Mac Studio native portable one-shot `ctest` reported 21/22 PASS; `virtual_texture_mip_pipeline` failed: `transparent red does not cause black/red alpha-fringe` at test #114. Native script then exited with 8, see `build/texture-final-logs/failure.txt`. Subsequent Metal GPU tests did not run; no full acceptance manifest PASS for that SHA.

## Root cause
The same 4x4 RGBA vector is reused from an initial red checkerboard mip scenario. The alpha-fringe scenario changed pixels at flat indices 0..3 (top row), not the full 2x2 spatial footprint used by mip1 texel (0,0): source (0,0),(1,0),(0,1),(1,1). Two opaque red pixels in the second row remained from the earlier checkerboard, polluting the premultiplied RGB average. That is a test fixture error, not an error in `buildVirtualTexturePhysicalMips`.

## Correction and API
Explicitly zero/reset the complete 4x4 buffer, initialize the true upper-left 2x2 region to opaque green (RGBA 0,255,0,255), then replace (0,0) with fully transparent red (255,0,0,0). The existing check for mip1 first texel R=0, G=255 and A in [190,192] is now unambiguous. Public API unchanged. No shader, GPU or genre-specific dependencies.

## Verification actually performed
- GitHub write committed successfully at SHA above.
- Independently evaluated the exact CPU premultiplied average of the new 2x2 fixture via Python in the local sandbox: result `(0,255,0,191)`, PASS for the existing numerical assertions.
- **Did not run the C++ executable, CTest, full engine build, Metal, Vulkan, GLES3 or WebGL2 browser GPU tests after this commit.** The Mac Studio 21/22 result is from the *prior* SHA, not this fix.

## Next agent step
1. On Mac Studio in the texture feature worktree fetch this branch and verify exact HEAD SHA; rebuild the focused native test, then run `ctest --test-dir build/texture-final-safe --output-on-failure --no-tests=error -R '^virtual_texture_mip_pipeline$'` (adjust build path if needed).
2. Only once focused test PASS is verified, rerun `bash tools/texture-rhi-final-verify.sh --jobs 4 --full-engine` at a freshly fetched matching SHA. Capture logs, native Metal tests, full CTest, acceptance.txt and branch commit.
3. If a new failure appears, diagnose its actual log rather than treating this CPU calculation as CTest PASS.
4. Keep PR #21 Draft until all required gates have real PASS; experimental GPU-authored ICB remains default OFF. Create another handoff after the next work stage including actual test results.
