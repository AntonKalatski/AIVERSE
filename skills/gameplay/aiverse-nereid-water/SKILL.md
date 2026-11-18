---
name: aiverse-nereid-water
description: Use NEREID deterministic ECS water bodies and contact sampling in native projects of any AIVERSE game genre.
---

# NEREID: universal water system

Read `AGENTS.md`, `docs/nereid/ARCHITECTURE.md`,
`docs/nereid/HANDOFF.md`, and `docs/water.md` before editing water.

## Use the shared package

Link `AIVERSE::lib_nereid_water`, include
`<aiverselib/water/NereidWaterSystems.h>`, and use
`aiverse::lib::nereid`. Register `registerNereidWaterPersistence(sim)`
from native `GameModule::setup`. Configure ECS WaterBodyComponent Entities;
attach WaterProbeComponent+WaterContactComponent to any unit, actor, ship or
object. Supply WaterProbe position after movement, optionally by
`addWaterProbeFromTransformSystem`, then `addWaterSamplingSystem`.

The sampling policy is common across genres; do NOT implement separate FPS
or RTS water sampling. `sampleWater(world,position)` selects priority,
surface, then stable generation-safe Entity. `depth` is clamped 0..surface-
bottom and `submerged` is true only within the fluid volume.

## Do not conflate different sources of truth

Existing RTS maps use `waterNetwork` in editor/presentation, and baked
water tiles in game simulation. Do NOT replace the old tile-based mechanics,
change beach/cliff bake or reinterpret world coordinates without explicit
migration and parity tests. NEREID starts opt-in.

The NEREID values are integer/Fixed; GPU displacement and refraction must
never affect authoritative damage, swimming or network state. Add future
shapes, water physics, VFX and editor integrations as separate systems or
renderer adapters, not genre forks.

## Tests, handoff, status

`ecs_nereid_water_test` tests two independent native GameModules, native
ECS persistence, checksum replay and COW. Until it passes on a real build,
the system is **experimental/unverified**.

After each implementation commit, update `docs/nereid/HANDOFF.md` with exact
SHA, changes, verification status, unfinished work and next action. Also
update this skill and `docs/nereid/ARCHITECTURE.md` when API changes.

## Stage 2 — buoyancy and drag

For buoyant objects also link `AIVERSE::lib_nereid_buoyancy`, include
`<aiverselib/water/NereidBuoyancySystems.h>`, and register
`registerNereidBuoyancyPersistence(sim)` at setup.

Attach WaterBuoyancyComponent, WaterVelocityInputComponent and
WaterForceComponent to the querying Entity. Host physics supplies the
per-tick velocity. Run `addWaterBuoyancySystem` after the water sample
and before host integration, then consume WaterForceComponent.acceleration
with the host's *existing* gravity/collision solver. Never duplicate the
velocity owner or integrate gravity twice. Missing/stale contact returns
zero, and outputs are deterministic Fixed values.

Test target: `ecs_nereid_buoyancy_test` (unverified).
