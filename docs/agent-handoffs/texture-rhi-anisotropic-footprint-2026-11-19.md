# Texture RHI — anisotropic footprint integer overflow guard
Date: 2026-11-19

## State and scope
Branch: `feature/texture-rhi-unification` (not main).
Commit: `f8b06ee3d5b984321b463a0e5463fa17f0442d1a`.
File changed: `engine/gfx/VirtualTextureSampling.h`.
No genres/modules affected; the change is only in canonical GLSL/MSL texture sampling helpers.

## Change
Virtual-texture anisotropic sampling previously computed `int(ceil(major/minor))` (GLSL) or `uint(ceil(major/minor))` (MSL), then clamped to the allowed tap count. When a valid but extreme UV derivative produces an extremely high ratio, the integer conversion can overflow before the clamp. Both shaders now clamp the floating result to `[1,limit]`, where `limit` remains clamped to `[1,8]`, **before** converting to an integer. Sampling layout, virtual mip index, gradients, addressing modes and max tap limits are unchanged for ordinary footprints.

## Validation
Code change written to GitHub, no native GLSL/MSL compiler or GPU tests were executed in this turn. Do not infer runtime correctness from the successful write. The existing canonical VT shader fixture `tests/render/check_virtual_texture_sampling_shaders.py` references and compiles both anisotropic overloads; it now also has a Vulkan SPIR-V compilation mode from the preceding stage.

## Required subsequent checks
1. Run `python3 tests/render/check_virtual_texture_sampling_shaders.py --backend glsl`, `--backend vulkan` and (on macOS) `--backend metal`.
2. Add a numerical anisotropic edge-case stress oracle (huge but finite derivatives, narrow/large aspect ratios, clamp/repeat/mirror policies), then verify GPU image parity and physical mip/trilinear transitions on actual hardware.
3. Test Vulkan and GLES texture bindings and readback on supported devices; passing source compilation alone does not qualify.
4. Once all remaining features are complete, run `bash tools/texture-rhi-final-verify.sh --jobs 4 --full-engine` on the Mac Studio and inspect its new acceptance manifest.
5. Keep PR #21 Draft and experimental ICB OFF by default until passing evidence exists.

## Agent notes
This change is intentionally limited to float-to-integer safety; it does not claim to solve nonfinite NaN UV derivatives, filtering quality, mip LOD bias or streaming stress. Continue with those as separate stages, each with a distinct handoff and actual test results.
