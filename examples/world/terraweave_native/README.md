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
    sim.world(),cfg,dig,sim.tick());
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
