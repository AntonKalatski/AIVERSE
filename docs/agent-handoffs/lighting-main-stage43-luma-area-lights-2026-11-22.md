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
- Mac Studio `ctest -L Lighting` at `73f443b8a` (luma_area + luma_motion + main): 68/70; the two
  failures are `prisma_smaa_gpu` / `prisma_smaa_gl` (PRISMA Stage 97 threshold, sharpness 12.9 vs
  limit 13.2; not this branch). All 12 Luma cases pass on Metal.
- Phone (Samsung Galaxy A05s, Adreno 610, Vulkan): Luma cases 0–11 pass, including 9 (object velocity),
  10 (sphere) and 11 (tube).

## Metal object velocity was broken (found by Luma case 9)
- `Renderer3DMotionShaders.h` kMSL took `.r` of a `depth2d::sample` result (a float): the rigid
  velocity overlay never compiled on Metal, so the whole rigid velocity pass was skipped and PRISMA /
  Luma got no object velocity on Metal. Fixed (`96229ba30`).
- kBakedMSL used `vertex` (a Metal keyword) as a variable name: baked-animation velocity never compiled
  on Metal. Fixed (`15c3c4c0a`).
- `GfxMetal` MSL compile errors now name the shader and quote the failing source line (the line number
  counts the colour prefix).
- `LumaWeavePostProcess`: a reused confirmed result reports the object velocity of its own dispatch.

## Not done
- No host shades particles with Luma yet: Studio has no particles; the game client does not use Luma.
  The API is in place for the first host that has both.
- Rect area lights / LTC: not implemented.
