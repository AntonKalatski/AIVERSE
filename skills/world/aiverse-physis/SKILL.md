---
name: aiverse-physis
description: Reuse AIVERSE PHYSIS for deterministic weather, wind, gusts, storms, gravity and environmental forces. Shows genre-neutral ECS integration, optional Box3D Scene forces, ownership split with Water/NEREID and TerraWeave, tests and mandatory handoffs.
---

# AIVERSE PHYSIS — shared nature and force systems

**Never reimplement PHYSIS inside an FPS/RTS/RPG module.** PHYSIS core must remain genre-independent, renderer-independent and headless. Read `docs/physis.md`, `engine/physis/AGENTS.md`, and `docs/physis-nereid-terraweave-integration.md` before editing.

## Ownership

- **PHYSIS:** atmospheric/weather climate, wind/gust/shelter, storms (hurricanes/tornadoes), snow/wetness/ice, fire/quakes, configurable gravity and environmental force **policy**.
- **NEREID water agent (PR #23):** canonical lakes, rivers, oceans, flow/current, water surfaces and buoyancy. Set `InstallOptions::floodMode = FloodMode::ExternalAuthority` when NEREID owns water. PHYSIS rain is normalized intensity, **not** metres of water.
- **TerraWeave agent (PR #24):** land geometry, heightfield, biome, foliage, vegetation and ground presentation. Terra consumes immutable PHYSIS/NEREID snapshots; PHYSIS must not generate terrain or run a second water solver.
- **Box3D Physics:** movement/collision/forces and physics snapshots. Use the existing `AIVERSE::physics` target and `physics::Scene`; only the optional bridge under `engine/physics/PhysisBox3D.h` may include Box3D types.
- **Renderer/VFX:** consumes presentation snapshots only; never controls authoritative climate.

## C++ API map

| Feature | Header | API |
|---|---|---|
| Core grid and ticking | `engine/physis/Physis.h` | `initialize`, `advance`, `cellAtPosition`, `sample` |
| Climate and weather fronts | `PhysisClimate.h` | `ClimateZone`, `WeatherFront` |
| Surface wetness / snow | `PhysisEnvironment.h` | `environmentAt`, `EnvironmentalResponse`, `advanceEnvironment` |
| Fire/quake/shallow-flood prototype | `PhysisDisasters.h` | `DisasterConfig`, `FloodMode`, `ignite`, `hazardAt` |
| Spatial 3D wind and gusts | `PhysisWind.h` | `WindZone`, `WindShelter`, `windAt` |
| Uniform/point gravity fields | `PhysisGravity.h` | `GravitySettings`, `GravityField`, `gravityAt` |
| Hurricane/tornado vortex fields | `PhysisStorm.h` | `StormEvent`, `StormKind`, `windWithStormsAt`, `stormsAt` |
| Shared drag/gravity force policy | `PhysisForces.h` | `ForceReceiver`, `ForceOutput`, `evaluateForces`, `advanceForces` |
| Combined read-only 3D climate + hazard + force fields | `PhysisPresentation.h` | `NatureFieldSample`, `sampleNatureField` |
| Generic native host | `PhysisIntegration.h` | `InstallOptions`, `install`, `forEachPresentationCell` |
| Optional Box3D consumption | `engine/physics/PhysisBox3D.h` | `applyPhysisToBody`, `PhysisBodyMaterial` |

Install PHYSIS once from `GameModule::setup` before the persistence registry seals. `InstallOptions.enableStorms`, `enableForces`, `enableWindZones`, and `enableGravityFields` default false, so existing games retain their behaviour. PHYSIS updates precisely once per authoritative `Simulation::step` (never once per render frame); the optional force pass **only publishes** ECS outputs and does not move bodies.

## Persistence invariants

Existing Stage 1–5 native save schemas must not change by default. `enableWindZones` registers WindZone/WindShelter, `enableGravityFields` registers GravityField/GravitySettings, `enableStorms` registers StormEvent, and `enableForces` registers ForceReceiver/ForceOutput. Set the flags consistently before saving/loading/replaying; the registry rejects mismatched schema manifests. The new `sampleNatureField` is a read-only adapter used by Terra, VFX, editor and physics consumers, not a second world state.

## Box3D usage

```cpp
#include "engine/physis/PhysisIntegration.h"
#include "engine/physics/PhysisBox3D.h"

// In generic native GameModule::setup:
aiverse::physis::InstallOptions p;
p.enableStorms = true;
p.enableDisasters = true;
p.floodMode = aiverse::physis::FloodMode::ExternalAuthority; // when NEREID owns water
aiverse::physis::install(sim, p);

// Physics owner thread, after authoritative PHYSIS update:
for (auto body : optedInDynamicBodies)
    aiverse::physics::applyPhysisToBody(scene, sim.world(), body);
scene.step();
```

Link `AIVERSE::physics` and enable `AIVERSE_ENABLE_PHYSICS=ON` only in opt-in native hosts. The bridge samples the actual Box3D body mass/velocity/position and applies quadratic wind drag plus **mass × (PHYSIS gravity − Box3D built-in gravity) × gravityScale** with `Scene::applyForce` before `Scene::step`; otherwise gravity would be counted twice. Do not apply `ForceOutput::totalForce` to the *same* Box3D body. `physics::Character` retains its separate scalar-gravity controller and is **not** modified by this adapter. Box3D float solver does **not** become lockstep-deterministic because PHYSIS uses fixed point.

## Current status and tests

Implemented fields are simple authored wind/vortex/gravity policies; they are **not** CFD weather, N-body gravitational physics, full destruction, water storm surge, GPU storm effects or foliage wind deformation. No automatic Terra/NEREID coupling is claimed. Native compilation and tests are **NOT RUN** for connector-authored stages, so keep PR #25 draft until validated.

```sh
./run-tests.sh --filter '^physis_'
cmake -S . -B build/physis-physics -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_ENABLE_PHYSICS=ON
cmake --build build/physis-physics --target aiverse_physis_box3d_test
ctest --test-dir build/physis-physics -R '^physis_box3d$' --output-on-failure
```

For Box3D integration also run `physics_runtime`, `physics_scene`; for shared water/terrain run NEREID/Terra and legacy regressions. Do not claim PASS unless logs demonstrate it.

## Mandatory agent handoff

**Every implementation stage** must commit its code/tests, then a **separate documentation commit** to `docs/agent-handoffs/physis-<stage>-YYYY-MM-DD.md` with branch, exact implementation SHA, paths, public API, actual test PASS/FAIL/NOT RUN results, known limitations and next explicit task. Update root `AGENTS.md`, `engine/physis/AGENTS.md` and `docs/physis.md` when changing ownership/contract. Do not commit into another agent's active Water or Terra branch; no force pushes.
