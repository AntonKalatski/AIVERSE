# Lighting stage 43: Luma area lights and particle lights (2026-11-22)

Branch `feature/aiv20261122_luma_area` (from `feature/aiv20261122_luma_motion`).

## What changed
- `LumaLightKind::Sphere` / `Tube` (`engine/render/LumaWeaveLights.h`): new fields `sourceRadius`,
  `halfLength`; validation, bounds (tube covers its segment), `lumaSameLight`.
- GPU packet (`LumaWeaveGpuPacket.h`): tube half length in `directionCone.w`, source radius in
  `settings.w` (was reserved). ABI size unchanged (80 bytes).
- Shaders, MSL and Vulkan GLSL: `lumaLightPoint` (tube closest axis point), `lumaSourceRadius`;
  `directUnshadowed` clamps the falloff distance at the source radius; PBR widens the GGX lobe by the
  angular size with energy rescale; shadow rays aim at the representative point.
- CPU reference (`LumaWeaveDirectReference.h`) matches.
- Authoring: `PointLight::sourceRadius` / `halfLength`; `LumaHost` maps them; Studio
  `studioAuthoredLights` reads `type: sphere|tube`, `sourceRadius`, `length`, `yaw`; MCP light schema.
- Particles: `FxPointLight::uid`; `engine/fx/FxLumaBridge.h` `appendLumaFxLights`;
  `LumaHost::setDynamicLights` republishes static + dynamic each update;
  `RenderLightingRuntime::setDynamicLights` pass-through.

## Tests
- `lumaweave_direct_reference`: sphere/tube values, bounds, packing, fx keys.
- `LumaScene.h` cases 10 (sphere, receiver inside the emitter) and 11 (tube whose centre is out of
  range); CTest regex now `Luma GPU 11`.
- Verification runs: see the commit / queue log (Mac Metal `gpu:` run, phone Vulkan job).

## Not done
- No host shades particles with Luma yet: Studio has no particles; the game client does not use Luma.
  The API is in place for the first host that has both.
- Rect area lights / LTC: not implemented.
