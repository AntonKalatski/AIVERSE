# NEREID — living AI-agent handoff

**Repository**: `chromealex/AIVERSE`
**NEREID branch**: `feature/nereid-water-core`
**Created from**: `feature/universal-entity-inspector-fps` (working project
branch, may advance concurrently).
**Last implementation commit**: `5dc63de21772da931c68bed02954866de186d754`
**Native build/CTest status**: NOT RUN / UNVERIFIED for stages 1 and 2.

## Stage 1 — deterministic water queries (implementation commit 8099a884)

New/changed files in implementation commit:
- `libraries/include/aiverselib/water/NereidWater.h`: ECS types,
  pure footprint policy, field-wise Archive IO.
- `libraries/include/aiverselib/water/NereidWaterSystems.h`: pure
  `sampleWater`, transform adapter, ECS sampling registration, persistent IDs.
- `tests/ecs_nereid_water_test.cpp`: shooter/strategy simulation hosts,
  ocean/lake overlap, immersion, save/load, COW, rollback.
- `libraries/catalog.json`: header package `nereid-water`.
- `CMakeLists.txt`: target and CTest `ecs_nereid_water_test`.

**No existing FPS, RTS, render, editor, physics or map files were modified.**

## How to consume

```cpp
#include <aiverselib/water/NereidWaterSystems.h>
namespace nw = aiverse::lib::nereid;

// Inside native GameModule::setup(Simulation& sim,...)
nw::registerNereidWaterPersistence(sim);
// Attach WaterBodyComponent to ECS water source Entities.
// Attach WaterProbeComponent + WaterContactComponent to query Entities.
// Attach shared Transform OR write WaterProbeComponent::position yourself.
nw::addWaterProbeFromTransformSystem(sim, "nereid.probe"); // optional
nw::addWaterSamplingSystem(sim, "nereid.sample");
```

Use CMake `AIVERSE::lib_nereid_water`, and read
`docs/nereid/ARCHITECTURE.md` plus
`skills/gameplay/aiverse-nereid-water/SKILL.md`.

## Mandatory verification (not yet performed)

```sh
cmake -S . -B build/ecs-test -DAIVERSE_BUILD_TESTS=ON
cmake --build build/ecs-test --target aiverse_ecs_nereid_water_test -j 4
ctest --test-dir build/ecs-test --output-on-failure -R '^ecs_nereid_water_test$'
```

Also run legacy `water_*` fixtures and the relevant FPS/RTS ECS tests
before connecting NEREID to existing games. Compile/link a standalone native
game module with `AIVERSE::lib_nereid_water`.

## Known limits

- Currently supports infinite, axis-aligned box and disk water footprints;
  no river spline or irregular polygon.
- Static surface and flow only. No waves, buoyancy, hydraulic solver, VFX or
  under-water rendering has been implemented.
- Overlap is O(water-body count * probe count).
- Native water components are v1; Transform/motion components must be saved
  by their own owners. The library does not change old `waterNetwork` or
  the `~` tile bake.
- Existing `WaterKind` enum values represent semantic kinds, not feature
  completeness.
- If build/test failure occurs, fix it first and document exact command/result.

## Next agent action

1. Read `AGENTS.md`, `docs/ecs-mechanic-composition.md`,
   `docs/water.md` and `docs/nereid/ARCHITECTURE.md`.
2. Configure/build/run new NEREID test and fix any compile/runtime failure.
3. Implement next opt-in capability without altering the existing game's
   water source-of-truth or float presentation pipeline; favor shared
   deterministic ECS policy over per-genre implementations.
4. After EVERY implementation stage, append: exact commit SHA, modified
   files, API, test logs/result, limitations, next action.
5. Keep the implementation and documentation in separate commits, document
   the implementation immediately before proceeding to the next stage.

## Stage 2 — composable buoyancy and flow drag (implementation commit `5dc63de21772da931c68bed02954866de186d754`)

Files changed: `libraries/include/aiverselib/water/NereidBuoyancySystems.h`,
`tests/ecs_nereid_buoyancy_test.cpp`, `CMakeLists.txt`,
`libraries/catalog.json`.

Public package `AIVERSE::lib_nereid_buoyancy` depends on
`AIVERSE::lib_nereid_water`. Public API in `aiverse::lib::nereid`:
- `WaterBuoyancyComponent`: acceleration, linear drag and immersion height
  (data-only, fixed-point).
- `WaterVelocityInputComponent`: per-tick canonical-motion velocity input;
  not a second source of truth for the host's physical velocity.
- `WaterForceComponent`: computed acceleration, immersed fraction, sampledTick.
- `computeWaterForces(sample,params,velocity)`: pure shared CPU policy.
- `addWaterBuoyancySystem(sim,name)`: read same-tick sampled contact and
  velocity, publish force; zeroes invalid or stale input.
- `registerNereidBuoyancyPersistence(sim)`: native ECS schema v1
  (`nereid.buoyancy`, `nereid.velocity_input`, `nereid.force`).

Force result includes upward water acceleration and drag relative to the
water's flow; **gravity, collisions and velocity integration remain with the
host physics/motion owner**. This avoids a second mutable velocity or
genre-specific swim implementation. Use only one solver.

Suggested sequence in a host:
```cpp
nw::registerNereidWaterPersistence(sim);
nw::registerNereidBuoyancyPersistence(sim);
// After movement and host per-tick input:
nw::addWaterProbeFromTransformSystem(sim, "water.probe"); // optional
nw::addWaterSamplingSystem(sim, "water.sample");
nw::addWaterBuoyancySystem(sim, "water.buoyancy");
// Then host physics integrates WaterForceComponent::acceleration.
```

**Verification:** NOT RUN / UNVERIFIED. Git access from the available
terminal returned `Could not resolve host: github.com`; connected GitHub
writes were successful, but do not imply a C++ build. New test
`ecs_nereid_buoyancy_test` checks drag relative to river/ocean flow,
submersion, dry reset, two unrelated native hosts, v1 save/load, COW/replay.
Compile it on a checkout:
```sh
cmake --build build/ecs-test --target aiverse_ecs_nereid_buoyancy_test -j 4
ctest --test-dir build/ecs-test --output-on-failure -R '^ecs_nereid_(water|buoyancy)_test$'
```

**Limits**: this is a contact/force-output *policy*, not a dynamic fluid solver,
mesh, boat hull evaluator, wave field, ocean FFT, physics engine or gameplay
swimming controller. Host is responsible for keeping WaterVelocityInput
current from its canonical motion state each tick. Validate platform compile,
native physics coupling, save/rollback and existing RTS/FPS parity before
shipping.

**Next agent action**: First configure/build both NEREID tests and fix any
issues. Then create separate spline/polygon geometry adapter and renderer
stages. Do not move RTS waterNetwork ownership without existing
`water_*` bake parity and non-regression tests. After each code stage commit,
append another status section to this handoff in a documentation commit.
