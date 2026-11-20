# Texture RHI + SpectraFX — Metal storage compatibility

Date: 2026-11-20
Branch: integration/texture-rhi-main-20261120
Starting integration commit: c95f2fdd0
Current stage: second merge with updated main, not yet committed

## Root cause
Texture RHI restricted Metal writable textures to RGBA8/RGBA16F.
SpectraFX requires RGBA32F Texture2D GPU storage for particle positions.

## Changes
- engine/gfx/metal/GfxMetal.mm:
  permit RGBA32F writable Texture2D.
- tests/spectrafx_metal_rhi.mm:
  assert RGBA32F writable texture capability before GPU initialization.

## Existing GPU API
MTLPixelFormatRGBA32Float and MTLTextureUsageShaderWrite already exist.
No change to shader mathematics or fallback policy.

## Actual tests
Before fix: full CTest 207/209 PASS.
Failures: spectrafx_metal_rhi and spectrafx_metal_scene.
After fix: NOT YET RUN.

## Limitations
RGBA32F writable array/3D support remains unverified.
Metal GPU execution must be rerun on the merged implementation.

## Next
Rebuild both failing tests, verify real GPU output, run full CTest
and seven Texture RHI Metal tests. Update this handoff with results
and final merge SHA before pushing integration branch.
