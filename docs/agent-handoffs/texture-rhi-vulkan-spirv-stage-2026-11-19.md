# AIVERSE Texture RHI — Vulkan SPIR-V shader gate (2026-11-19)

## Stage scope
Extended the existing **canonical** VirtualTextureSampling.h shader compilation fixture with a separate Vulkan SPIR-V target. This is portable compiler-level validation only, not Vulkan runtime render acceptance.

## Implemented
- `tests/render/check_virtual_texture_sampling_shaders.py`: `--backend vulkan` compiles the production GLSL helper as GLSL 450 using `glslangValidator -V --target-env vulkan1.1 -S frag`.
- Uses Vulkan-compatible explicit descriptor set/binding declarations for atlas, indexed page table, and uniform parameter block. All legacy, gradient, virtual trilinear and anisotropic sampling functions stay embedded in the compiled shader.
- The fixture requires compiler success and an actual .spv output artifact; missing `glslangValidator` is a hard failure if invoked directly.
- `CMakeLists.txt`: adds `virtual_texture_sampling_vulkan_spirv` CTest only when both Python and glslangValidator are found. This is separate from `virtual_texture_sampling_glsl` (GL 3.3 / GLES 3.0) and `virtual_texture_sampling_msl` (Metal).
- The production sampling shader and texture RHI implementation are **unchanged** by this stage.

## Files and commits
- `tests/render/check_virtual_texture_sampling_shaders.py` — 6209ba4634d09675d711bf7dd8d6b73905de1751
- `CMakeLists.txt` — 724b4ce820d714f30ed086e00d5a1301a5c2d871
- This handoff is recorded on `feature/texture-rhi-unification`; do not merge into main before final acceptance.

## Validation status — do not confuse source edit with tested code
GitHub writes succeeded. **No shader compiler, CMake configure/build, Metal GPU test, Vulkan GPU runtime, or full engine regression was executed for these commits.** Last independently confirmed complete Mac acceptance is SHA `7494f32d`: 21 portable + 7 Metal GPU tests, `full_engine=0`. Subsequent physical mip and indexed virtual-texture work is still pending the agreed one-pass final testing.

## Next step for another agent
1. Finish remaining runtime features and backend integration before the final test pass.
2. On a machine with `glslangValidator`: `python3 tests/render/check_virtual_texture_sampling_shaders.py --backend vulkan`. Resolve real SPIR-V compilation errors, if any.
3. CMake and CTest: `ctest --test-dir <build-dir> --output-on-failure -R '^virtual_texture_sampling_(glsl|vulkan_spirv|msl)$'`.
4. On Mac Studio, only after feature completion run the established `bash tools/texture-rhi-final-verify.sh --jobs 4 --full-engine`. Preserve separate native Metal tests.
5. Linux Vulkan actual framebuffer/texture readback, GLES/mobile limits, physical mip visual transitions and long-run streaming/performance remain unproven until executed on hardware.

## Architecture and agent coordination
Generic `engine/gfx` texture functionality stays independent of game genres. Do not claim that compiling source to SPIR-V proves Vulkan descriptor binding compatibility, page-table upload visibility or render parity. Keep Draft PR #21 and experimental GPU ICB default OFF until measured acceptance.
