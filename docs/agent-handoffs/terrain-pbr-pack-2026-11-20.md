# Bundled terrain PBR — 2026-11-20

Branch feature/aiv87_terrain_pbr; baseline f9aed9a705982e2c6b77b29869e309995ce8b163.
## Prerequisite: Metal compilation repair
Code b613c78e6feb6c9ff0d16dcbefaf850f49b71ac3: engine/gfx/metal/GfxMetal.mm restores missing closing brace in completedFrameSerial before gpuSubmissionFence. No GPU synchronization semantics changed. Existing main failed with missing layer_/device_ fields and nested function definitions.
Validation: clang++ -std=c++20 -fobjc-arc -fsyntax-only -I . -I libraries/include -I third_party/imgui -I third_party/lua engine/gfx/metal/GfxMetal.mm completed after fix (see next checkpoint for full build). Full editor build not yet complete. Initial overlapping build was interrupted and restarted; never count it as passing. CI NOT RUN.
Uncommitted next stage: terrain PBR pack, baker, installer, opt-in GGX shader and checkerboard for empty projects. Preserve these changes. Next: finish editor build then visual verification before pack commit.
