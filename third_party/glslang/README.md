# glslang (vendored)

- Upstream: https://github.com/KhronosGroup/glslang
- Version: 16.6.0, commit e1b562a8bed273a02f30b59b66a5d499793cede5
- Used by: the Vulkan RHI backend (`engine/gfx/vulkan/VkShaderCompiler.cpp`) to compile the engine's GLSL to SPIR-V at run time.
- Kept: `glslang/{GenericCodeGen,MachineIndependent,Include,Public,ResourceLimits,OSDependent}`, `SPIRV/`.
- Removed: HLSL front end, C interfaces, the grammar source `glslang.y` (the generated `glslang_tab.cpp` is kept),
  tests, standalone tool, external dependencies, upstream build files.
- `generated/glslang/build_info.h` was produced by upstream `build_info.py` for this version.
- Build: `CMakeLists.txt` here (target `aiverse_glslang`), no optimizer (`ENABLE_OPT` off), no HLSL.

Licenses (see `LICENSE.txt`): BSD-3-Clause for most of glslang, BSD-2-Clause and MIT for some Khronos
files, Apache-2.0 for `SPIRV/hex_float.h` and `SPIRV/bitutils.h`, the Bison special exception for the
generated parser `glslang_tab.cpp`, and the NVIDIA preprocessor licence for `MachineIndependent/preprocessor`.
Allowed for this repository by the owner (AGENTS.md "Licenses/assets").
