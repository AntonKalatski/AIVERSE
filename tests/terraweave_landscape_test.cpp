#include "engine/world/TerraWeaveRiverbed.h"
#include "engine/world/NereidManifestHost.h"
#include <cassert>
using namespace aiverse;
using namespace aiverse::terraweave;
int main(){
    lib::nereid::WaterBodyComponent body;body.surfaceY=0_fx;body.bottomY=-2_fx;
    lib::nereid::WaterGeometryComponent river;river.kind=lib::nereid::WaterGeometryKind::RiverSpline;
    river.points={{0_fx,-20_fx,3_fx},{0_fx,20_fx,3_fx}};
    EarthworksLayer whole(100,[](auto,auto){return 300;});
    EarthworksLayer split(100,[](auto,auto){return 300;});
    carveNereidRiverbed(whole,body,river,{-1000,-1000,1000,1000},500);
    carveNereidRiverbed(split,body,river,{-1000,-1000,0,1000},500);
    carveNereidRiverbed(split,body,river,{0,-1000,1000,1000},500);
    assert(whole.snapshot().heights==split.snapshot().heights);
    assert(whole.heightAt({0,0})==-200);
    assert(whole.heightAt({9,0})==300);
    assert(whole.heightAt({5,0})>whole.heightAt({3,0}));
    const auto before=whole.snapshot();bool rejected=false;
    try{carveNereidRiverbed(whole,body,river,{-1000,-1000,1000,1000},0);}catch(const std::invalid_argument&){rejected=true;}
    assert(rejected&&whole.snapshot().heights==before.heights);
    world::NereidManifestHost host;
    host.configure(Json::parse(R"({"nereidWater":{"surfaces":[{"id":1,"kind":"river","shape":"river","surfaceY":0,"bottomY":-2,"points":[[0,-20,3],[0,20,3]]}]}})"));
    assert(host.configuredSurfaces()==1);
    bool invalid=false;try{host.configure(Json::parse(R"({"nereidWater":{"surfaces":[{}]}})"));}catch(const std::exception&){invalid=true;}
    assert(invalid&&host.configuredSurfaces()==1);
    host.configure(Json::object());assert(host.configuredSurfaces()==0);
}
