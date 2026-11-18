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


## Stage 3 — polygon lakes and spline rivers (experimental)

Use `<aiverselib/water/NereidGeometry.h>` or the
`NereidWaterSystems.h` umbrella and register
`registerNereidGeometryPersistence(sim)` when enabling shape overrides.

Add a `WaterGeometryComponent` to the same Entity as a
`WaterBodyComponent`. Use `WaterGeometryKind::Polygon` for an X/Z
closed simple ring (no closing duplicate necessary) or
`WaterGeometryKind::RiverSpline` for a centerline with a half-width
per `WaterGeometryPoint`. Attach 3..256 polygon points or 2..256
spline points; all spline half-widths must be positive. Surface, bottom
and flow are still defined by the regular water body. Only the footprint
is overridden. `sampleWater` already reads it.

Never feed float renderer tessellation or shader waves back into gameplay.
Do not re-encode legacy `waterNetwork` until migration tests prove
unchanged water baking and shoreline behavior. New test target
`ecs_nereid_geometry_test` is authored but NOT RUN; also run previous
NEREID tests before declaring this stage verified.


## Stage 4 — optional indexed water sampler

Include `<aiverselib/water/NereidQueryIndex.h>`. For large water-body
counts, replace `addWaterSamplingSystem` with
`addIndexedWaterSamplingSystem(sim,"water.sample")`. **Do not register
both.** It generates an in-tick temporary AABB index, scans infinite oceans
separately and uses the same deterministic selection and contact schema.

`WaterQueryIndex(world).sample(position)` is suitable for read-only
batch queries only: constructing the index holds a reference to the World.
Never retain it across World mutations, reloaded saves or rollback.
Always rebuild when source bodies or spline geometry have changed.
For few bodies and probes, the linear sampler may be cheaper.

Stage 4 authored test: `ecs_nereid_query_index_test`, not yet executed.
Run all four `ecs_nereid_.*_test` cases plus existing RTS water tests,
and record actual build/CTest results in `docs/nereid/HANDOFF.md`.
