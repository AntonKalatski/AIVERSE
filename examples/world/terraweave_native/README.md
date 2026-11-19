# Native TerraWeave + NEREID composition example

This opt-in example shows a *real, genre-neutral C++ integration seam* for a native AIVERSE `GameModule`. It does **not** change default RTS/FPS modules or create a second water solver. NEREID remains canonical for water velocity, bank contact and sediment; TerraWeave owns soil heightfield, root strength, plant ages/lighting and grass coverage.

Source: [Integration.h](Integration.h).

## GameModule setup phase

```cpp
#include "examples/world/terraweave_native/Integration.h"
using namespace aiverse;

terraweave::TerraWeaveEcsAuthorityConfig cfg;
cfg.generator.seed=1234;
cfg.soilGridCm=100;
cfg.bedrockCm=-1000000;
// Must identify generator version, seed, content hashes and height assets.
cfg.immutableRecipeId="terraweave:v1:seed1234:your-content-digest";

// Call once in GameModule::setup, before the persistence registry seals.
examples::terraweave_native::registerSystems(sim,cfg,
    [](const terraweave::EcsBankTerrainCommit& tick) {
        // Only enqueue event data; do NOT step Box3D or render inside ECS.
        // In particular, tick.vegetation.fell contains server fall triggers.
    });

// The game/authoring system creates a lake/river/ocean body, e.g.
lib::nereid::WaterBodyComponent lake;
lake.kind=lib::nereid::WaterKind::Lake;
lake.footprint=lib::nereid::WaterFootprint::Box;
lake.center={0_fx,0_fx,0_fx};
lake.halfExtents={20_fx,20_fx};
lake.surfaceY=1_fx;
lake.bottomY=-3_fx;
examples::terraweave_native::addWaterBody(sim.world(),lake);

// Attach one ECS bank-cell on a real shoreline; choose the X/Z probe on
// the wet side, and an Earthworks grid vertex on the soil side.
lib::nereid::WaterBankSoilComponent bank;
bank.soil.soil=lib::nereid::ShoreSoil::Sand;
bank.wetContactPermille=1000;
const terraweave::EarthVertex bankVertex{21,0};
const int32_t groundCm=terraweave::heightAtCm(
    cfg.generator,2100,0);
examples::terraweave_native::addShoreCell(
    sim.world(),bankVertex,groundCm,{19_fx,0_fx,0_fx},bank);
```

The bank probe's water contact must be valid for the same completed ECS tick. Real games should author/rasterize many bank-cell entities at bounded sample density, and may configure current fields, vortices, tides and PHYSIS waves from existing NEREID systems. **Never** create one bank ECS entity per grass blade.

### Player digging and ECS rollback

```cpp
terraweave::EarthworkBrush dig;
dig.centerXcm=1000; dig.centerZcm=0;
dig.radiusCm=150; dig.depthCm=50;
auto changed=terraweave::excavateAuthoritativeEcs(
    sim.world(),cfg,dig,sim.tick(),
    /*enableSlopeFailures=*/true);
// changed.slope.slides contains deterministic moved-soil events.
// changed.rooted.fell can become Box3D fallen-tree events at the host
// physics barrier; changed.grass is aggregated patch damage.
// Save/replay/rollback is canonical through TerraWeaveAuthorityResource.
// Do not also run a separate standalone NEREID Earthworks bridge on the
// same soil vertices; choose ECS outputs OR the standalone bridge.
```

Register `TerraWeaveAuthorityResource` via the helper once; `Simulation` copies it with World snapshots and stores it in `PersistentEcsRegistry` using schema version 2. When adding game-specific plant age/light or grass mutations to this resource, increment `stateRevision`; the provided dig/ECS bank helpers do so automatically.

### Render/collision/native owner phase

Create, once, an `EarthworksLayer` with immutable generator settings and a `TerrainMutationCoordinator`, `RootSupportRegistry`, `GrassCoverRegistry` plus `NativeTerraWeaveWorldBinding`. Their reference lifetimes must exceed all callbacks. After each completed simulation tick:

```cpp
std::string navError;
auto outcome=examples::terraweave_native::publishFrame(
    sim, binding, terrain, worldScene, device, spatialCollision,
    navmesh, navSource, simulationPinnedCells, grassAreaCm,
    [](world::Cell tile) -> uint32_t { /* stable reserved IDs per tile */
        return reservedTerrainCollisionId(tile);
    }, &navError, /* afterRollback = */ false);

if (!outcome.groundReady || !outcome.navigationReady) {
    // Hold simulation movement into affected terrain until collision and
    // navigation are consistent. Keep previously resident ground until
    // the new GPU/collider material has been published.
}
// WorldScene::update(...) + draw(...) occur in the regular app frame.
// After worldScene.draw(...), terrain.draw(renderer,originCm).
```

On `Simulation` rollback or restoring a saved game, invoke `publishFrame` with `afterRollback=true` even when the ECS state revision equals the previous one. This invalidates cached terrain and grass meshes and restores the fallen-tree visibility set without interfering with separate harvested-tree removals. The host must keep native Box3D and other physics ownership synchronized on its own owner thread.

**Known limits:** Native code must reserve disjoint collider IDs and avoid duplicate old static terrain colliders. `WorldScene` only hides the original static ground draw where the dynamic replacement is GPU resident. `nav::Mesh` must be initially built, and its source must use the same EarthworksLayer. A resource's large sparse state may cost memory when COW snapshots are detached. Box3D falls need a separate authoritative server transform stream, and the heightfield cannot create underground caves or overhangs.

**QA status:** Source committed but this example and Stage 53–55 features have NOT been compiled, executed, tested or benchmarked; tests are scheduled after functional completion. Do not merge Draft PR until native build and gameplay/graphics tests confirm behavior.


## Stage 57–59: automatically discover shore and riverbed sediment carriers

The shoreline/riverbed rasterizer reads the *same* ECS water bodies, river splines, polygon footprints, confluence/cascade connections and TerraWeave terrain used by the native game. It does not simulate water separately. On map load or AFTER water geometry/soil topology changes, invoke this during a safe structural ECS barrier **before** NEREID sampling:

```cpp
terraweave::ShorelineRasterSettings scan;
scan.area={-6400,-6400,6400,6400}; // 128x128 1m lattice at most
scan.includeWetBedCells=true;      // also carry sediment through the interior riverbed
scan.minWetBedDepthCm=5;
scan.maxWaterDepthCm=3000;         // avoid unbounded deep-ocean interior cells
scan.splashHeightCm=100;
auto soil=terraweave::ShorelineSoilAt{
    [&](terraweave::EarthVertex p,int32_t elevation)->lib::nereid::ShoreSoil {
        (void)elevation;
        const auto c=terraweave::climateAt(cfg.generator,biomeSettings,
            p.x*cfg.soilGridCm,p.z*cfg.soilGridCm);
        return terraweave::biomeShoreSubstrate(c.biome);
    }
};
auto refreshed=examples::terraweave_native::refreshDetectedShoreline(
    sim.world(),cfg,scan,soil);
// refreshed.created / updated / disabled / manualSkipped
```

Prefer an **authored soil-material map** over the fallback biome→substrate mapping: rocks should block erosion, sand/silt can migrate, clay/gravel require stronger currents. An optional root-binding callback returns 0..1000 from saved root/vegetation support state. The raster detects wet/dry transition banks using actual NEREID coverage (polygon, river spline, ocean or lake) and bounded wave splash. With `includeWetBedCells=true`, fully submerged interior cells also participate, allowing sediment to cross broad rivers rather than only the perimeter. These remain one bounded bank/bed ECS entity per *terrain grid vertex*, never one per grass blade.

`Integration.h::registerSystems` installs stages in canonical order:

1. NEREID water sampling; local erosion/deposition from `WaterBankSoilComponent`.
2. `nereid.sediment_transport`: simultaneous mass-conserving micro-centimetre transport down the Fixed current, with saved carry. Source and target must be in the same body OR explicitly linked by a valid NEREID confluence/cascade inlet→outlet.
3. `terraweave.bank_commit`: applies NEREID's **already calculated** local ground edits once to canonical ECS Earthworks, then root support and grass coverage.

A former bank that becomes dry is disabled but keeps its suspended sediment in saved ECS state; moving water cannot silently discard that material. The terrain owner must rerasterize when river course/water geometry changes (NOT every frame), and all generated cells must remain under the configured 16,384 sample budget. Deposition on a low-flow wet-bed cell raises its height. Once it emerges above the surface, subsequent topology refresh can turn it into a new sandbar/shoreline. This is a bounded sediment policy, not a solved full 3D Navier–Stokes sediment model.

**Stage57–59 source is committed, but this example and the new kernels have NOT been compiled or run in a native game or CTest yet.** The native host still owns the terrain mesh/collision/NavMesh publication, visual terrain LOD, physics and replication barriers.


## Stage 60–61 — slumping banks and excavation-driven collapse

The opt-in example `registerSystems` enables unstable-slope resolution by default. A material's effective angle of repose comes from `SlopeSoil` (NEREID bank soil class and wetness, with optional caller-provided nonbank soil maps). Water saturation weakens sand/silt/clay; root binding strengthens plant-covered ground. After NEREID bank erosion and downstream suspended sediment transfer, `TerraWeaveEcsAuthority` stages a slope failure preview, merges downhill soil movements with the original bank erosion outputs and publishes ONE `Earthworks` soil revision. Existing NEREID bank ECS groundCm values at landslide vertices are synchronized to the final height, so the next water tick sees the same terrain. Sediment suspension/carry is preserved, never recomputed by the terrain system.

A player dig may use `excavateAuthoritativeEcs(ecs,cfg,brush,tick,true,slopeSettings)` to trigger the same local slide without any water. The returned `TerraWeaveAuthoritativeEcsChange` contains `slope.slides`, `rooted.fell` and `grass`. Route each fall event to the existing optional Box3D `FallenPlantPhysics` on the native server owner thread; do not step Box3D within ECS. The same final soil revision is consumed by GPU/SpatialWorld/NavMesh and near/far vegetation via `NativeTerraWeaveWorldBinding`.

`SlopeFailureSettings` bounds max slides per tick and soil moved per slide, default max 4096 disjoint source/destination pairs and 40cm displacement each. Genuine caves, overhangs, granular avalanche dynamics, debris bodies and secondary effects on water flow are not implemented by this bounded heightfield policy. New Stage60–61 sources have NOT been compiled or tested yet; PR stays Draft. Read `docs/terrain/stage-60-wet-slope-failure-handoff.md` and `docs/terrain/stage-61-nereid-slopes-ecs-handoff.md`.


## Stage 62 — fallen logs, landslide debris and NEREID flow feedback

`Integration.h::registerSystems` enables optional persistence for `nereid.flow_obstruction` and `terraweave.auto_flow_obstruction` without requiring Box3D to be linked by ordinary native water users. Only the native server that actually uses Box3D includes [PhysicsIntegration.h](PhysicsIntegration.h).

A completed authoritative soil tick may produce `EcsBankTerrainCommit::slope.slides` and `EcsBankTerrainCommit::vegetation.fell` (or `TerraWeaveAuthoritativeEcsChange::slope/rooted.fell` for player digging). At the Box3D owner-thread barrier, consume these ONCE:

```cpp
#include "examples/world/terraweave_native/PhysicsIntegration.h"

// PhysicsScene is your existing server-owned physics::Scene.
// Allocate these one time; they keep stable event IDs and bounded handles:
terraweave::FallenPlantPhysics fallenTrees(physicsScene);
terraweave::SlopeDebrisPhysics slopeDebris(physicsScene);

// Once per completed soil tick, outside ECS systems:
auto effects = examples::terraweave_native::applySlideAndTreePhysics(
    sim.world(), cfg, commit.slope, commit.vegetation.fell,
    commit.completedTick, slopeDebris, fallenTrees, forestScene);
// effects: dynamic debris bodies and actual fallen tree bodies created.
// The source soil mass has ALREADY moved in Earthworks.
// Debris bodies are collision/visual proxies, not a second ground-mass store.

// On the authoritative Box3D step:
physicsScene.step();
// At the next ECS structural barrier, BEFORE NEREID samples water:
auto obstructionChanges =
    examples::terraweave_native::publishObstructionsAfterPhysics(
        sim.world(), worldScene, physicsScene,
        &fallenTrees, &slopeDebris,
        [&](Entity waterBody)->std::optional<uint64_t> {
            // Map canonical ECS body to NereidSceneSurface.id
            return sceneIdForWaterEntity(waterBody);
        });
```

NEREID's existing `sampleWater` and `WaterQueryIndex::sample` now both apply the exact same **Fixed-point** local obstruction policy AFTER river currents, vortices, confluences and cascades: debris and tree trunks slow water, with bounded lateral deflection based on which side of the obstruction a probe lies. This affects bank erosion, sediment advection and water interactions **on the following authoritative tick**. A mirrored immutable copy of those obstruction records is sent into `WorldScene`; water whitewater shader packets and ripple drift use the same deflection. Manual NEREID flow-obstruction entities are not overwritten by the generated tree/debris publisher.

**Important:** The real water solver/geometry is still owned exclusively by NEREID. This is a local obstruction current approximation: it does NOT integrate hydrodynamic pressure, raise upstream water levels, flood new terrain or reroute the water body mesh. `WorldScene` needs an explicit ECS body → stable scene-water ID mapping; never assume ECS index equals the scene ID. Physics poses are float/server-only and must be replicated; clients only use authoritative water/terrain and rendered transforms. Transient Box3D debris has a bounded lifetime and count and does NOT create a new terrain mass source. Do not generate a physics body for every grain of sand or grass blade.

On shutdown, call `slopeDebris.shutdown()` and `fallenTrees.shutdown()` **before** destroying their `physics::Scene`. Refresh generated water obstacles to an empty snapshot if physics sources disappear. After ECS rollback/load, the native host must restore/recreate authoritative physical bodies before recapturing water obstruction records.

**Verification:** Stage62 source is committed, but no Clang/GCC, CMake, CTest, native ECS replay, Box3D scene, Metal/GL/Vulkan screenshot or performance test was run. Keep PR #24 Draft; the next agent should validate the first native compile before integration sign-off.
