# AIVERSE PHYSIS

PHYSIS is the **genre-independent** nature simulation for AIVERSE. Weather, local climate zones, wet ground, snowfall, fire, shallow flooding and quake intensity are authoritative ECS state. It is **not** coupled to FPS/RTS or any renderer.

## Modules

| Header | Responsibilities |
|---|---|
| `engine/physis/Physis.h` | Fixed-point authoritative cell grid, Atmosphere/Surface/State, seeded tick, queries |
| `engine/physis/PhysisClimate.h` | ClimateZone, WeatherFront, seasonal forcing, regional tick |
| `engine/physis/PhysisEnvironment.h` | SurfaceMaterial, snow/wetness/ice, generic response and friction/runoff hints |
| `engine/physis/PhysisDisasters.h` | Optional fuel/fire, shallow flooding, QuakeEvent, deterministic old/new buffers |
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
- Fire, flood and quakes are base ECS mechanics, **not** a complete volcano, hurricane, avalanche, wildfire, fluid, geomorphology or destruction solver.
- Responses like traction multiplier and runoff potential are coefficients/hints for other systems. No production Terra, Water, Foliage, Physics, VFX, Audio or Editor consumer is wired by this branch.
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

Each next stage gets code commit(s) followed by a separate handoff commit with SHA, paths, real tests, gaps, and the next exact task. Agent-specific constraints: `engine/physis/AGENTS.md`.
