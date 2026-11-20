#include "engine/world/TerraWeaveRiverbed.h"
#include "engine/world/TerraWeaveAlpine.h"
#include "engine/world/NereidManifestHost.h"
#include <cassert>
using namespace aiverse;
using namespace aiverse::terraweave;
int main(){
    TerrainSettings base;base.reliefCm=0;
    std::vector<AlpinePeak> peaks{{0,0,10000,16000,18000,400}};
    assert(alpineHeightCm(base,peaks,30000,0)==heightAtCm(base,30000,0));
    assert(alpineHeightCm(base,peaks,0,0)>10000);
    bool varied=false;
    for(int x=-9000;x<9000;x+=150){auto other=base;other.seed+=1;varied|=alpineHeightCm(base,peaks,x,2000)!=alpineHeightCm(other,peaks,x,2000);}
    assert(varied);
    bool badPeak=false;try{alpineHeightCm(base,{{0,0,0,1000,1000,0}},0,0);}catch(const std::invalid_argument&){badPeak=true;}assert(badPeak);

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
    assert(whole.heightAt({3,0})>0); // water perimeter buried in bank, no exposed cut edge
    assert(alpineHeightCm(base,peaks,0,0,3500,9000)==heightAtCm(base,0,0));
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
