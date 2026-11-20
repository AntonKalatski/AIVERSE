# AIVERSE texture stage — nonfinite UV/LOD guards
Date: 2026-11-19

## Status
- Branch: `feature/texture-rhi-unification`
- Implementation commit: `91aad4536a64e7211d81a4f647f072f4d86e0abe`
- Changed: `engine/gfx/VirtualTextureSampling.h`
- Existing GL/GLES/Vulkan shader compilation fixture remains `tests/render/check_virtual_texture_sampling_shaders.py`
- No engine builds or tests have been run after this change.

## Changes
Both canonical GLSL and Metal Shader Language helpers now define finite-range checks for one/two-dimensional float values. They compare absolute magnitudes against 1e8. NaN fails the comparison and Infinity also fails. The conservative magnitude guard ensures 2D derivatives scaled by a virtual texture size remain below the range where squaring them in `length()` could overflow float32.

- `aiverseVirtualMipLod` / `aiverse_virtual_mip_lod` return LOD=0 when derivatives, dimensions or bias are invalid.
- `aiverseSampleVirtualTrilinear` / `aiverse_sample_virtual_trilinear` return caller fallback color for invalid UV, derivatives, gutter or explicit mip LOD before float-to-int conversion.
- `aiverseSampleVirtualAnisotropic` / `aiverse_sample_virtual_anisotropic` return caller fallback color when UV, derivatives, virtual texture size, gutter or bias are invalid. Previous tap count float-before-int clamp is preserved.
- Default/normal parameters and the 1–8 tap API are unchanged. No genre dependencies were added.

## Caveats
The 1e8 finite-range cutoff is deliberate defensive policy, not a physical limit of texture formats or an optimal tuning result. Inputs with magnitude beyond this threshold fall back even when finite. Shader comparison under optimization and behavior on specific GLES devices still need hardware tests. This stage does not fix all possible edge cases of the simpler legacy VT sampling overloads or guarantee GPU pixel parity.

## Validation and next stage
Committed source only: no CMake, glslangValidator, xcrun metal, Vulkan/GLES device, or CTest execution in this turn. Run the existing canonical sampler compilation gates for GL33/GLES3, Vulkan SPIR-V and MSL together in the final agreed pass. Add an independent CPU numerical oracle and actual GPU tests for NaN/Inf, near-limit finite derivatives, zero gradients, coarse-mip transitions and anisotropic tap count. Continue on actual backend descriptor binding/readback and streaming/LRU soak. At feature completion on Mac Studio run `bash tools/texture-rhi-final-verify.sh --jobs 4 --full-engine`, preserving full logs and the new acceptance manifest.

Keep GPU-authored ICB disabled by default and draft PR #21 unmerged until acceptance. After each additional stage create another agent handoff documenting implementation, files, commits, actually run tests, limitations and next step.
