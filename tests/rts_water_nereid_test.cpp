// Legacy cell water -> NEREID conversion (engine/view/game/GameWaterNereid.h).
// CPU only: synthetic layouts in the exact form TerrainRenderer::legacyWaterLayout
// produces; the parsed result must match the legacy footprint and levels.
#include "engine/view/game/GameWaterNereid.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace aiverse;
static void check(bool v,const std::string& why){if(!v)throw std::runtime_error(why);}
static world::NereidManifest parse(const Json& j){return world::parseNereidWaterManifest(j);}

int main() {
    const render::float4 tint{0.22f,0.45f,0.62f,0.78f};
    auto flat=[](float h){return [h](float,float){return h;};};
    // 1. Sheet: one box over map + apron at the sheet height.
    {
        game::LegacyWaterLayout l;l.width=64;l.height=48;l.apron=12;l.sheet=true;l.any=true;l.sheetY=-0.12f;
        std::string why;
        auto m=game::legacyWaterToNereidManifest(l,tint,Json(),flat(0),&why);
        check(bool(m),"sheet conversion: "+why);
        auto p=parse(*m);
        check(p.surfaces.size()==1,"sheet: one surface");
        const auto& s=p.surfaces[0];
        check(s.body.footprint==lib::nereid::WaterFootprint::Box,"sheet: box");
        check(std::abs(s.body.center.x.toDouble()-32)<1e-3&&std::abs(s.body.center.z.toDouble()-24)<1e-3,"sheet: centre");
        check(std::abs(s.body.halfExtents.x.toDouble()-44)<1e-3&&std::abs(s.body.halfExtents.y.toDouble()-36)<1e-3,"sheet: map + apron");
        check(std::abs(s.body.surfaceY.toDouble()+0.12)<1e-3,"sheet: legacy height");
        check(std::abs(s.style.opacity-0.78f)<1e-3&&s.mesh.foamWidth==0.f,"sheet: opacity from legacy alpha, no edge foam");
        l.any=false;
        auto none=game::legacyWaterToNereidManifest(l,tint,Json(),flat(0),&why);
        check(none&&parse(*none).surfaces.empty(),"no water cells: no surface");
    }
    // 2. Bodies: a hidden bounding box is one surface; a lower dip inside the
    // box forces the exact rectangle cover instead.
    {
        game::LegacyWaterLayout l;l.width=10;l.height=8;l.apron=12;l.sheet=false;l.any=true;
        game::LegacyWaterLayout::Body b;b.level=1.4f;
        // L-shaped body: row 2 cols 2..6, rows 3..5 col 2..3
        for(int x=2;x<=6;++x)b.cells.push_back(2*10+x);
        for(int z=3;z<=5;++z)for(int x=2;x<=3;++x)b.cells.push_back(z*10+x);
        std::sort(b.cells.begin(),b.cells.end());b.water=b.cells;
        l.bodies.push_back(b);
        std::string why;
        auto hidden=game::legacyWaterToNereidManifest(l,tint,Json(),flat(2.f),&why);
        check(bool(hidden),"hidden box: "+why);
        auto p=parse(*hidden);
        check(p.surfaces.size()==1&&std::abs(p.surfaces[0].body.halfExtents.x.toDouble()-2.5)<1e-3&&
              std::abs(p.surfaces[0].body.halfExtents.y.toDouble()-2)<1e-3,"hidden body: one bounding box 5x4");
        check(std::abs(p.surfaces[0].body.surfaceY.toDouble()-1.4)<1e-3,"body keeps its legacy level");
        auto dip=[](float x,float z){return (x>=5&&x<=6&&z>=4&&z<=5)?0.5f:2.f;}; // a lower hollow at (5,4)
        auto exact=game::legacyWaterToNereidManifest(l,tint,Json(),dip,&why);
        check(bool(exact),"rect cover: "+why);
        auto q=parse(*exact);
        check(q.surfaces.size()==2,"exposed hollow: exact rectangle cover (row run + 2x3 block), got "+std::to_string(q.surfaces.size()));
        double area=0;for(const auto& s:q.surfaces)area+=4*s.body.halfExtents.x.toDouble()*s.body.halfExtents.y.toDouble();
        check(std::abs(area-double(b.cells.size()))<1e-3,"rectangle cover area equals cell count");
    }
    // 3. More surfaces than the scene holds: refused, caller keeps legacy.
    {
        game::LegacyWaterLayout l;l.width=200;l.height=4;l.sheet=false;l.any=true;
        for(int i=0;i<70;++i){game::LegacyWaterLayout::Body b;b.level=1;b.cells={i*2};b.water=b.cells;l.bodies.push_back(b);}
        std::string why;
        check(!game::legacyWaterToNereidManifest(l,tint,Json(),flat(2),&why)&&why.find("limit")!=std::string::npos,"over 64 surfaces must be refused");
    }
    // 4. Overrides: style and surface keys apply, geometry stays converter-owned.
    {
        game::LegacyWaterLayout l;l.width=8;l.height=8;l.apron=12;l.sheet=true;l.any=true;l.sheetY=-0.1f;
        auto o=Json::parse(R"({"lod":"near","surfaceY":99,"style":{"absorption":1.5,"turbidity":0.3}})");
        std::string why;
        auto m=game::legacyWaterToNereidManifest(l,tint,o,flat(0),&why);
        check(bool(m),"override: "+why);
        auto p=parse(*m);
        const auto& s=p.surfaces[0];
        check(s.mesh.lod==lib::nereid::WaterSurfaceLod::Near&&std::abs(s.style.absorption-1.5f)<1e-3f&&std::abs(s.style.turbidity-0.3f)<1e-3f,"overrides applied");
        check(std::abs(s.body.surfaceY.toDouble()+0.1)<1e-3,"geometry keys are not overridable");
        auto bad=Json::parse(R"({"style":{"opacity":1.5}})");
        check(!game::legacyWaterToNereidManifest(l,tint,bad,flat(0),&why),"invalid override must fail closed");
    }
    std::cout<<"PASS legacy water -> NEREID conversion\n";
    return 0;
}
