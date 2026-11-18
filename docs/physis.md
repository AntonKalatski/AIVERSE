# AIVERSE PHYSIS

PHYSIS is the **genre-independent** nature simulation for AIVERSE. Weather, local climate zones, wet ground, snowfall, fire, shallow flooding and quake intensity are authoritative ECS state. It is **not** coupled to FPS/RTS or any renderer.

## Modules

| Header | Responsibilities |
|---|---|
| `engine/physis/Physis.h` | Fixed-point authoritative cell grid, Atmosphere/Surface/State, seeded tick, queries |
| `engine/physis/PhysisClimate.h` | ClimateZone, WeatherFront, seasonal forcing, regional tick |
| `engine/physis/PhysisEnvironment.h` | SurfaceMaterial, snow/wetness/ice, generic response and friction/runoff hints |
| `engine/physis/PhysisDisasters.h` | Optional fuel/fire, shallow flooding, QuakeEvent, deterministic old/new buffers |
| `engine/physis/PhysisWind.h` | 3D wind zones, deterministic gusts, shelter attenuation and `windAt` |
| `engine/physis/PhysisGravity.h` | World gravity baseline, additive/priority gravity fields, `gravityAt` |
| `engine/physis/PhysisStorm.h` | Opt-in hurricane/tornado vortex, storm tracks, climate rain/pressure forcing |
| `engine/physis/PhysisForces.h` | Quadratic wind drag, force receiver/output ECS and pure force sampling |
| `engine/physics/PhysisBox3D.h` | **Optional Box3D** physics::Scene adapter; does not modify PHYSIS core |
| `engine/physis/PhysisIntegration.h` | Generic Simulation::addSystem, persistent ECS schemas, read-only presentation feed |

## Quick integration for any C++ native game

```cpp
#include "engine/physis/PhysisIntegration.h"
void MyGame::setup(aiverse::Simulation& sim, const aiverse::Json&, const aiverse::Json&) {
    aiverse::physis::InstallOptions options;
    options.climate.seed = sim.config().seed;
    options.climate.width = 32;
    options.climate.height = 32;
    options.enableDisasters = true;
    options.enableStorms = true;  // optional hurricane/tornado update
    options.enableForces = true;  // optional ECS ForceReceiver/ForceOutput pass
    // When NEREID owns water: options.floodMode = physis::FloodMode::ExternalAuthority;
    aiverse::physis::install(sim, options);
}

// After committed Simulation::step() (e.g. from renderer/editor):
aiverse::physis::forEachPresentationCell(sim.world(), [](const auto& cell) {
    // cell.environment.wind / wetness / rain / snow / ice
    // cell.hasHazards ? cell.hazards.fire / waterDepth / quake : none
});
```

All values used by gameplay use `Fixed`; never feed presentation floats back into PHYSIS. PHYSIS world coordinates are a static X/Z grid; `Cell::elevation` is project-assigned. For custom host scheduling choose exactly one `advance` variant per tick. To register PHYSIS into generic Simulation and persistent ECS schema, install during `GameModule::setup`.

## Current implementation boundary

- The implemented weather evolution is a deterministic simplified local model, not a physically accurate global circulation model; seasons are triangle waves, fronts have Manhattan falloff.
- Fire, flood and quakes are base ECS mechanics; hurricane/tornado are simplified deterministic vortex/forcing fields, **not** complete atmospheric CFD, volcano, avalanche, fluid, terrain deformation or destruction solvers.
- Responses like traction multiplier and runoff potential are coefficients/hints. An **optional, explicit** `engine/physics/PhysisBox3D.h` adapter now applies PHYSIS wind/ gravity corrections to `physics::Scene` dynamic bodies, but no automatic Scene/WorldScene hookup or production Terra, NEREID, Foliage, VFX, Audio or Editor integration is wired.
- Grid updates are currently serial and eager; do not advertise parallel job execution, streaming, distributed weather or efficient millions-of-cells throughput.
- No compile/CTest/CI verification has been completed in this connector session; **code present does not mean tests passed**.

## Tests

Use the project-wide unified runner (required by AGENTS.md):

```sh
./run-tests.sh --filter '^physis_'
```

This performs full Debug configure/build before filtered CTest. For investigating a standalone target, see the individual handoff for exact CMake commands.

## AI agent continuity

- Stage 1: [Core](agent-handoffs/physis-core-2026-11-18.md)
- Stage 2: [Climate](agent-handoffs/physis-climate-2026-11-18.md)
- Stage 3: [Environment](agent-handoffs/physis-environment-2026-11-18.md)
- Stage 4: [Disasters](agent-handoffs/physis-disasters-2026-11-18.md)
- Stage 5: [Simulation/persistence](agent-handoffs/physis-integration-2026-11-18.md)
- Stage 6: [PHYSIS / NEREID / TerraWeave ownership and reuse](agent-handoffs/physis-ownership-terra-water-2026-11-18.md)
- Stage 7: [External water authority mode](agent-handoffs/physis-flood-authority-2026-11-18.md)
- Stage 8: [Wind, gust and shelter](agent-handoffs/physis-wind-2026-11-18.md)
- Stage 9: [Gravity fields](agent-handoffs/physis-gravity-2026-11-18.md)
- Stage 10: [Hurricane and tornado events](agent-handoffs/physis-storm-2026-11-18.md)
- Stage 11: [Forces and Box3D adapter](agent-handoffs/physis-forces-box3d-2026-11-18.md)

Each next stage gets code commit(s) followed by a separate handoff commit with SHA, paths, real tests, gaps, and the next exact task. Agent-specific constraints: `engine/physis/AGENTS.md`.

## Cross-system ownership (PHYSIS / NEREID / TerraWeave)

PHYSIS owns **climate/weather, natural event fields and environmental force/gravity policies**, but never Box3D rigid-body integration itself. NEREID (Water agent,
draft PR #23) owns water-body geometry, flow, contacts and buoyancy; TerraWeave
(Terra agent, draft PR #24) owns terrain, biomes, vegetation and land rendering.
Use the opt-in host integration plan and precise metre/centimetre conversion in
[`physis-nereid-terraweave-integration.md`](physis-nereid-terraweave-integration.md).
The existing PHYSIS shallow-flood prototype is **not** an authoritative
replacement for NEREID water. When a host uses NEREID as the water authority,
set `InstallOptions::floodMode = FloodMode::ExternalAuthority`: PHYSIS continues
to update fire and quake but clears/skips its local shallow-flood depth. This
opt-in switch is authored and covered by tests; cross-module NEREID/Terra
integration and real build/CTest verification remain pending.

This is a **design contract only**. Terra/NEREID integration, rendering and
cross-system tests remain NOT IMPLEMENTED / NOT RUN on this branch.

## Wind, gravity, storms and optional Box3D usage

```cpp
#include "engine/physis/PhysisIntegration.h"
#include "engine/physics/PhysisBox3D.h" // link AIVERSE::physics, if enabled

// During GameModule::setup:
aiverse::physis::InstallOptions options;
options.enableStorms = true;
options.enableForces = true; // only if generic ECS force outputs are required
aiverse::physis::install(sim, options);

aiverse::physis::StormEvent tornado;
tornado.kind = aiverse::physis::StormKind::Tornado;
tornado.center = {10_fx, 0_fx, 10_fx};
tornado.eyeRadiusMeters = 20_fx;
tornado.outerRadiusMeters = 150_fx;
tornado.heightMeters = 500_fx;
sim.world().add<aiverse::physis::StormEvent>(sim.world().create(), tornado);

// Physics owner: after the authoritative PHYSIS tick, just before Box3D Scene::step:
for (aiverse::physics::BodyHandle body : optedInBodies)
    aiverse::physics::applyPhysisToBody(box3dScene, sim.world(), body);
box3dScene.step();
```

PHYSIS authoritative types are `Fixed` ECS data. The external Box3D bridge
converts from/to Box3D floats **only at this boundary**. Box3D already applies
scene gravity; the adapter applies **delta gravity** and wind drag via
`Scene::applyForce`, not a second gravity force. Do **not** also apply
`ForceResult::totalForce` to the same Box3D body. Opt-in kinematic character
controllers need a separate locomotion integration; neither hardcoded scalar
`CharacterParams::gravity` nor native Body motion is overridden here.

Physics is opt-in. Run `./run-tests.sh --filter '^physis_'` and, with
`-DAIVERSE_ENABLE_PHYSICS=ON`, run CTest `physis_box3d` and the existing
`physics_runtime` / `physics_scene`. These tests are **authored but NOT RUN**.
There are no physically accurate Navier–Stokes hurricanes, tornado debris
destruction, N-body orbital solvers, Terra foliage wind deform or water surge
in the current implementation.
