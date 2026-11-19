# AIVERSE GI — Stage 38: read-only Renderer3D light submission bridge (2026-11-20)

## Changes, exact commits and status

Branch: `feature/gi-lighting-portable-backends-20261119`, draft PR #47 to GI feature, not merged with main.

- `29e7a0b9e551dc0162e266ad228c0faf609d34be`: `engine/render/Lights.h`, public `LightSystem::submittedLights() const -> const std::vector<PointLight>&`.
- `2b8bb7f8a9e7c16469b45d00f5db3d9b06d4a079`: `engine/render/GiLightingController.h`, explicit `syncRendererLights(const Renderer3D&,uint32_t sourceNamespace,float3 focus,GiLightOverflowPolicy,Stats*)`.
- `b705162cb86a115ef4a37b960d9eb0f2381f843c`: `tests/render/gi_lighting_controller.cpp`, source test directly adding a point/spot to a vanilla Renderer3D LightSystem and synchronizing it into the indirect source controller.

**SOURCE ONLY, NOT COMPILED/TESTED.** No native CMake, CTest, CPU/GPU runtime, Vulkan SPIR-V, GLES model shader link, Metal GPU images or performance has run.

## Architecture/usage

The original `Renderer3D::lights` already owns a bounded per-scene submitted `std::vector<PointLight>` with up to 1024 direct sources. Stage 36's adapter accepted such a vector, but the host lacked an official read-only way to use the exact same source collection without creating a second application-side copy. `LightSystem::submittedLights()` now permits read-only inspection **after all scene-light `add()` calls and before the next `beginScene`/light reset**. It does not transfer ownership or grant mutation. The controller immediately converts/copies the valid source data into stable-key `GiMovingLight` descriptors via `GiLightBridge` and queues only opt-in `indirectMultiplier > 0` sources for independent indirect-only transport. No source/render pointers escape into Vulkan/Metal GPU jobs.

Typical order: `renderer.beginScene(...)`, submit direct `renderer.lights.add(pointOrSpot)`, publish immutable world geometry and world dirty revisions after ECS barriers, `giController.syncRendererLights(renderer, stableWorldNamespace, focus, policy)`, `giController.updateFrame(focus)`, `giController.bind(renderer)`, `renderer.render()`, then unbind before device/atlas destruction.

The existing direct shader, shadows, spot-shadow and baked GI behavior are unchanged. World owners must provide stable nonzero point/spot IDs and namespace, and must opt in indirect contribution. GPU admission still caps at 16 bounce sources vs CPU 64; `TopInfluence` is opt-in with explicit dropped-source statistics. The `PointLight` source vector remains valid only until the next `beginScene`; do not retain references across frames/threads.

## Known limits / next step

This adds an explicit adapter, not automatic subscription or a renderer-created controller. Sun/sky/area GI, Shader Graph, terrain/foliage custom shader and GPU clipmap cascade transfer remain separate. Metal/Vulkan compute and GLES3 LDR source code are unvalidated until actual native builds. Validate `aiverse_gi_lighting_controller_test` and `aiverse_gi_light_bridge_test`, then a real Scene frame with dynamically moving spotlight and editable terrain, comparing Metal/Vulkan/GLES. Preserve all concurrent renderer/texture/main branch changes and record tested SHA after the final acceptance pass.
