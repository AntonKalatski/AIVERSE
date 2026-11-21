#include "engine/world/NereidFlowMap.h"
#include "tests/TestCheck.h"
#include <iostream>
using namespace aiverse;
using namespace aiverse::world;
using namespace aiverse::lib::nereid;
static bool near(float a,float b,float error=.0001f){return std::abs(a-b)<error;}
int main(){
    WaterBodyComponent body;body.halfExtents={20_fx,20_fx};body.flow={2_fx,0_fx,-1_fx};
    NereidFlowMapSource source{body};source.rapidStrength=.8f;source.rapidThreshold=.2f;
    auto constant=buildNereidFlowMap(source);
    AIVERSE_TEST_CHECK(!constant.empty());
    for(const auto& cell:constant.texels){
        AIVERSE_TEST_CHECK(cell.x==2&&cell.y==-1&&cell.z>0&&cell.w==1);
    }
    AIVERSE_TEST_CHECK(constant.sample(-100,100).x==2);
    AIVERSE_TEST_CHECK(constant.sample(NAN,0).w==0);
    auto rect=constant.rect(1000,-300);
    AIVERSE_TEST_CHECK(near(rect.x,float(constant.minX-1000))&&rect.z>0);

    WaterGeometryComponent river;river.kind=WaterGeometryKind::RiverSpline;
    river.points={{0_fx,0_fx,6_fx},{0_fx,20_fx,6_fx},{12_fx,40_fx,6_fx}};
    WaterRiverGradeComponent grade;grade.risePerMetre={0_fx,Fixed::fromRatio(2,100)};
    grade.automaticCurrent=true;grade.responseSeconds=5_fx;
    source.geometry=&river;source.grade=&grade;
    const auto originalFlow=body.flow;
    auto bend=buildNereidFlowMap(source);
    const auto straight=bend.sample(1,11),turned=bend.sample(7,31);
    AIVERSE_TEST_CHECK(near(straight.x,0)&&straight.y<-.8f);
    AIVERSE_TEST_CHECK(turned.x<-.3f&&turned.y<-.5f);
    AIVERSE_TEST_CHECK(near(turned.x/turned.y,.6f,.01f));
    AIVERSE_TEST_CHECK(body.flow==originalFlow); // no renderer write into ECS configuration
    grade.automaticCurrent=false;
    auto manual=buildNereidFlowMap(source);
    AIVERSE_TEST_CHECK(manual.sample(1,11).x==2&&manual.sample(7,31).x==2);

    WaterCurrentFieldComponent currents;currents.nodes.resize(3);
    for(auto& node:currents.nodes)node.downstream=1_fx;
    WaterVortexFieldComponent vortices;vortices.vortices.push_back({{0_fx,10_fx},8_fx,2_fx,0_fx});
    source.currents=&currents;source.vortices=&vortices;
    auto combined=buildNereidFlowMap(source);
    const auto p=nereidSurfaceCurrentAt(source,{1_fx,0_fx,11_fx});
    // (1,11) is a texel centre on the world lattice: compare canonical sampling directly.
    const auto sample=combined.sample(1,11);
    AIVERSE_TEST_CHECK(near(sample.x,float(p.x.toDouble()))&&near(sample.y,float(p.z.toDouble())));
    const auto old=sample;
    vortices.vortices[0].circulation=-2_fx;
    auto changed=buildNereidFlowMap(source).sample(1,11);
    AIVERSE_TEST_CHECK(!near(changed.x,old.x));
    AIVERSE_TEST_CHECK(combined.sample(1,11).x==old.x); // immutable CPU snapshot

    source.geometry=nullptr;source.grade=nullptr;source.currents=nullptr;
    body.footprint=WaterFootprint::Infinite;
    AIVERSE_TEST_CHECK(buildNereidFlowMap(source).empty());
    NereidFlowMapOptions first;first.infinitePatch=NereidFlowBounds{-32,-32,32,32};
    auto second=first;second.infinitePatch=NereidFlowBounds{0,-32,64,32};
    const auto left=buildNereidFlowMap(source,first),right=buildNereidFlowMap(source,second);
    AIVERSE_TEST_CHECK(left.cellMetres==right.cellMetres);
    for(int z=0;z<20;++z){
        const auto a=left.sample(5.3,z+.2),b=right.sample(5.3,z+.2);
        AIVERSE_TEST_CHECK(a.x==b.x&&a.y==b.y);
    }
    body.footprint=WaterFootprint::Box;body.halfExtents={2000_fx,1500_fx};source.vortices=nullptr;
    const auto large=buildNereidFlowMap(source);
    AIVERSE_TEST_CHECK(large.width<=256&&large.height<=256&&large.texels.size()<=16384);
    AIVERSE_TEST_CHECK(large.cellMetres>2&&large.sample(0,0).x==2);
    auto invalid=first;invalid.cellMetres=0;bool rejected=false;
    try{(void)buildNereidFlowMap(source,invalid);}catch(const std::invalid_argument&){rejected=true;}
    AIVERSE_TEST_CHECK(rejected);
    invalid=first;invalid.cellMetres=NAN;rejected=false;
    try{(void)buildNereidFlowMap(source,invalid);}catch(const std::invalid_argument&){rejected=true;}
    AIVERSE_TEST_CHECK(rejected);
    body.enabled=false;AIVERSE_TEST_CHECK(buildNereidFlowMap(source).empty());
    std::cout<<"NEREID camera-independent flow map PASS\n";
}
