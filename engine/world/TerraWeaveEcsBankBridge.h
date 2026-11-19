// TerraWeave <-> NEREID ECS: one authoritative bank entity -> one Earthworks
// grid vertex. NEREID computes erosion ONCE in its existing ECS system.
// This writer consumes the output after that system's read/write barrier.
// Genre-neutral: no RTS/FPS module, client, graphics or second water solver.
#pragma once
#include "engine/world/TerraWeaveNereidSoilBridge.h"
#include "engine/world/TerraWeaveRootEvents.h"
#include "engine/world/TerraWeaveGrassCover.h"
#include <aiverselib/water/NereidShorelineSystems.h>
#include "engine/sim/Simulation.h"
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::terraweave {

// Explicit saved grid ownership: the bank entity must carry this mapping
// alongside NEREID's WaterBankSoil and WaterBankErosionOutput components.
// A game decides how to create shoreline entities from its actual rivers,
// lakes and coastlines; the renderer never guesses wetness from terrain Y.
struct BankEarthVertexComponent {
    EarthVertex vertex{};
};
inline void io(Archive& a,BankEarthVertexComponent& c){
    a(c.vertex.x,c.vertex.z);
}
inline void registerTerraWeaveBankVertexPersistence(Simulation& sim){
    sim.registerPersistentComponent<BankEarthVertexComponent>(
        "terraweave.bank_vertex",1);
}

struct EcsBankTerrainCommit {
    EarthworkChange earth{};
    PlantSupportUpdate vegetation{};
    std::vector<GrassCoverChange> grassCoverage{};
    uint32_t scannedBanks=0;
    uint32_t editedVertices=0;
    uint32_t completedTick=0;
};

// This is the SINGLE soil-commit stage after native
// addNereidBankErosionSystem. Never recompute erodeShore here.
// Stage all ECS output and dependent terrain/vegetation changes on copies
// so invalid source data cannot partially modify Earthworks/roots/grass.
inline EcsBankTerrainCommit commitNereidBankTerrain(
        const World& ecs,uint32_t completedTick,EarthworksLayer& earth,
        RootSupportRegistry* roots=nullptr,
        GrassCoverRegistry* grass=nullptr){
    EcsBankTerrainCommit output;
    output.completedTick=completedTick;
    std::map<EarthVertex,EarthVertexEdit> edits;
    ecs.eachExistingConst<lib::nereid::WaterBankErosionOutputComponent>(
        [&](Entity entity,
            const lib::nereid::WaterBankErosionOutputComponent& erosion){
            if(erosion.sampledTick!=completedTick)return;
            const auto* binding=ecs.tryGetConst<BankEarthVertexComponent>(entity);
            const auto* bank=ecs.tryGetConst<lib::nereid::WaterBankSoilComponent>(entity);
            if(!binding || !bank)
                throw std::invalid_argument(
                    "TerraWeave NEREID erosion output missing bank vertex/soil");
            if(++output.scannedBanks>16384u)
                throw std::length_error("TerraWeave ECS bank tile budget");
            if(!bank->enabled || !erosion.changed)return;
            if(erosion.removedCm && erosion.depositedCm)
                throw std::invalid_argument(
                    "TerraWeave NEREID contradictory sediment transaction");
            const int64_t diff=int64_t(erosion.afterGroundCm)-
                               erosion.beforeGroundCm;
            if(!diff || diff!=int64_t(erosion.depositedCm)-
                              erosion.removedCm ||
               bank->soil.groundCm!=erosion.afterGroundCm ||
               earth.heightAt(binding->vertex)!=erosion.beforeGroundCm)
                throw std::invalid_argument(
                    "TerraWeave NEREID stale/inconsistent soil output");
            EarthVertexEdit edit{binding->vertex,
                                erosion.beforeGroundCm,
                                erosion.afterGroundCm};
            if(!edits.emplace(binding->vertex,edit).second)
                throw std::invalid_argument(
                    "TerraWeave multiple bank entities claim one earth vertex");
        });
    if(edits.empty())return output;

    // Use the same recorded source revision and rectangular transaction as
    // standalone excavation; 16k ECS bank cells fit Earthworks' 65k budget.
    std::vector<EarthVertexEdit> batch;
    batch.reserve(edits.size());
    for(const auto& [key,edit]:edits){(void)key;batch.push_back(edit);}

    EarthworksLayer nextEarth=earth;
    output.earth=nextEarth.applyEdits(batch,earth.revision());
    RootSupportRegistry nextRoots;
    GrassCoverRegistry nextGrass;
    if(roots)nextRoots=*roots;
    if(grass)nextGrass=*grass;
    if(grass)output.grassCoverage=
        nextGrass.applyEarthwork(output.earth,nextEarth.spacingCm());
    if(roots){
        const SoilHeightSampler source=[&nextEarth](int64_t x,int64_t z)
                ->std::optional<int32_t> {
            const int64_t edge=kWorldLimitCm-nextEarth.spacingCm();
            if(x<=-edge||z<=-edge||x>=edge||z>=edge)
                return std::nullopt;
            return nextEarth.sampleHeightCm(x,z);
        };
        output.vegetation=nextRoots.applyGroundChange(
            source,output.earth,completedTick);
    }

    // Prevalidated state swap: no copy allocation or partial update once
    // the authoritative terrain, root and grass transitions are ready.
    earth.exchangeSnapshotState(nextEarth);
    if(roots)roots->exchangeSnapshotState(nextRoots);
    if(grass)grass->exchangeSnapshotState(nextGrass);
    output.editedVertices=uint32_t(batch.size());
    return output;
}

// Install AFTER registerNativeWaterPipeline(sim,... enableBankErosion=true).
// The host owns all references for the entire simulation lifetime. The
// optional callback can publish EarthworkChange into a native graphics/collision
// TerrainMutationCoordinator at the normal tick-to-render barrier; never
// render or touch gfx::Device inside this ECS system.
template<class OnCommitted>
inline void addTerraWeaveEcsBankCommitSystem(
        Simulation& sim,std::string name,EarthworksLayer& earth,
        RootSupportRegistry* roots,GrassCoverRegistry* grass,
        OnCommitted&& onCommitted){
    sim.addSystem(std::move(name),
        [&earth,roots,grass,callback=std::forward<OnCommitted>(onCommitted)]
        (Simulation& current)mutable{
            auto result=commitNereidBankTerrain(
                current.world(),current.tick(),earth,roots,grass);
            if(!result.earth.vertices.empty())callback(result);
        });
}

} // namespace aiverse::terraweave
