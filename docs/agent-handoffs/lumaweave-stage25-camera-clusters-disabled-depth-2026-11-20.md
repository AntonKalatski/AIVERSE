# AIVERSE LumaWeave — Stage 25: camera-grid source parity and disabled-view allocation gate

Date: 2026-11-20. Repository: `chromealex/AIVERSE`. Branch: `feature/lumaweave-massive-lighting`. This is NOT merged to main.

## Source commits and changes

1. `3fc1ddf94579df82ef0e415dc102b71e17f0d663` — `tests/render/lumaweave_gpu_packet.cpp`: extend the existing GPU packet test with a camera-following grid movement from x=0 to x=100, keeping the registry's authored light revision unchanged. The test explicitly expects the new cluster build generation, new GPU grid origin, and no stale light indices when the new view contains no lights. This checks the Stage23 source-generation contract without requiring a Metal device.
2. `d8698d8f0b5a6eb202c34d8b9a83cdaa024748a2` — `engine/render/LumaWeavePostProcess.h`: `scenePass` now checks active, ready, HDR scene, source/geometry publication, valid targets and bounds *before* attempting to allocate an auxiliary depth attachment for additive HDR composition. Disabled Metal Luma, unsupported GLES/mobile fallback, incomplete sources and zero-size views no longer cause an unnecessary HDR-depth allocation. Existing valid enabled-view composition logic and depth-sampler/attachment isolation are retained.

## Compatibility, invariants and boundaries

- No GPU shader strings, storage ABI, public C++ signatures, ECS/save/network state, material/normal buffers, Luma quality defaults, legacy direct LightSystem or dynamic GI behavior changed.
- Direct and indirect radiance remain mutually exclusive owners. Luma does NOT transparently override mobile raster fallback or silently enable itself.
- The Stage23 camera-following grid generation change is exercised by the test even if `LumaWeaveRegistry::revision()` is unchanged.
- Deferred destruction and GPU fence ownership of resized material/depth attachments remain a separate unresolved RHI audit. This patch only avoids unnecessary disabled-view allocations; it does NOT claim a complete GPU lifetime fix.

## Actual verification

- **PERFORMED:** connected-GitHub source inspection, two successful acknowledged source commits, inspection of the camera coordinator path and existing `AIVERSE_TEST_CHECK` test convention.
- **NOT RUN:** local/native Clang/GCC compilation, `lumaweave_gpu_packet` CTest, Metal MSL shader compile, real GPU camera-pan screenshot, Vulkan/GLES build or parity, HDR texture lifetime stress and full AIVERSE acceptance. Previous 33/33 owner test result predates this commit and cannot validate it.

## Next action

1. When the feature freeze is complete, run the single consolidated `tools/lumaweave_acceptance.sh --run` on native macOS, with an exact tested SHA; also run and record the new `lumaweave_gpu_packet` regression.
2. Audit per-view material/compose/surface textures for in-flight GPU frame retirement at resolution changes or device loss (not just compute completion); avoid destroying attachments referenced by older draws.
3. Reconcile Luma and Hybrid GI with current main/renderer/TerraWeave, preserving per-view source identity, one direct-light owner, and indirect GI exclusions.
4. Verify camera movement, source changes, PBR specular/material coverage, Metal/Vulkan/GLES route behavior and true GPU timings. Keep production main merge blocked until real cross-platform evidence.
