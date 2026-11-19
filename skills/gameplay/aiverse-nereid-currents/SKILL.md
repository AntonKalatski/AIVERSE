---
name: aiverse-nereid-currents
description: Per-river downstream, lateral and shear fixed-point currents, reusable in any native AIVERSE project.
---

# NEREID — Per-river downstream, lateral and shear fixed-point currents

Read `AGENTS.md`, `docs/nereid/ARCHITECTURE.md`, `docs/nereid/HANDOFF.md` and the common `skills/gameplay/aiverse-nereid-water/SKILL.md` first.

## Package and native API

Use `target_link_libraries(my_native_game PRIVATE AIVERSE::lib_nereid_currents)` in CMake, or `aiverse_native_use_libraries(aiverse_native_GAME nereid-currents)` when using the native module helper. The shared water dependency is `AIVERSE::lib_nereid_water`. Include `<aiverselib/water/NereidCurrentField.h>`; namespace `aiverse::lib::nereid`.

Attach WaterCurrentFieldComponent to a water body with valid enabled RiverSpline WaterGeometryComponent. Field must have one WaterCurrentNode per spline point; waterCurrentAt produces fixed-point current shared by indexed and reference NEREID sampling. Register registerNereidCurrentPersistence. Never derive gameplay current from the shader.

## Engineering contract

All simulation inputs and outputs are deterministic Fixed/ECS state. These systems are genre-independent: do not add FPS, RTS, PHYSIS, Box3D or visual renderer dependencies. Do not change the authoritative `WaterBodyComponent.flow` to implement presentation effects. Use `docs/nereid/HANDOFF.md` for current limitations, implemented APIs, exact commits, known tests and next agent steps.

Source: `libraries/include/aiverselib/water/NereidCurrentField.h`.
Status: experimental; native performance, platform tests and renderer parity must be verified before production use.
