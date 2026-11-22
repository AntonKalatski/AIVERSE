// NEREID large-world ocean LOD acceptance (CPU only, no GPU device).
// Covers: legacy single-patch coverage around the camera, nested LOD rings
// (crack/T-junction-free stitching, bounded triangles, area, winding,
// snapping/residency keys, precision far from the origin, tiles) and the
// manifest switch, using the fixture tests/fixtures/nereid/ocean-large-world.json.
#include "engine/render/NereidSurfaceResident.h"
#include "engine/world/NereidWaterManifest.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
using namespace aiverse;
namespace n=aiverse::lib::nereid;
static void check(bool value,const std::string& why){if(!value)throw std::runtime_error(why);}

struct Bounds{double minX=1e300,maxX=-1e300,minZ=1e300,maxZ=-1e300;};
static Bounds bounds(const n::WaterSurfaceMesh& m,double ox,double oz) {
    Bounds b;
    for(const auto& v:m.vertices) {
        const double x=double(v.x)+ox,z=double(v.z)+oz;
        b.minX=std::min(b.minX,x);b.maxX=std::max(b.maxX,x);
        b.minZ=std::min(b.minZ,z);b.maxZ=std::max(b.maxZ,z);
    }
    return b;
}
// Every edge is shared by exactly two triangles except the outer square.
// A T-junction or crack leaves an interior edge used once.
static void checkWatertight(const n::WaterSurfaceMesh& m,const char* what) {
    std::map<std::pair<uint32_t,uint32_t>,int> edges;
    double area=0;
    for(size_t i=0;i<m.indices.size();i+=3) {
        const uint32_t t[3]={m.indices[i],m.indices[i+1],m.indices[i+2]};
        const auto& a=m.vertices[t[0]];const auto& b=m.vertices[t[1]];const auto& c=m.vertices[t[2]];
        const double up=((b.z-a.z)*(c.x-a.x)-(b.x-a.x)*(c.z-a.z))*0.5;
        check(up>0,std::string(what)+": triangle not facing +Y or degenerate");
        area+=up;
        for(int e=0;e<3;++e) {
            uint32_t p=t[e],q=t[(e+1)%3];
            if(p>q)std::swap(p,q);
            ++edges[{p,q}];
        }
    }
    const auto box=bounds(m,0,0);
    for(const auto& [edge,count]:edges) {
        check(count==1||count==2,std::string(what)+": edge used more than twice");
        if(count==2)continue;
        const auto& a=m.vertices[edge.first];const auto& b=m.vertices[edge.second];
        const bool outer=(a.x==b.x&&(a.x==float(box.minX)||a.x==float(box.maxX)))||
                         (a.z==b.z&&(a.z==float(box.minZ)||a.z==float(box.maxZ)));
        check(outer,std::string(what)+": open interior edge (crack or T-junction)");
    }
    const double expected=(box.maxX-box.minX)*(box.maxZ-box.minZ);
    check(std::abs(area-expected)<=expected*1e-6,std::string(what)+": area does not match the covered square");
}
static std::string readFile(const char* path) {
    std::ifstream in(path);
    check(bool(in),std::string("cannot read fixture ")+path);
    std::stringstream s;s<<in.rdbuf();return s.str();
}

int main(int argc,char** argv) {
    n::WaterBodyComponent sea;
    sea.kind=n::WaterKind::Ocean;sea.footprint=n::WaterFootprint::Infinite;
    sea.surfaceY=0_fx;sea.bottomY=-200_fx;

    // 1. Legacy single patch: the camera keeps r/2 of water in every direction.
    {
        n::WaterSurfaceMeshOptions o;o.oceanPatchRadius=256;o.lod=n::WaterSurfaceLod::Far;
        for(double cam:{0.0,1.0,128.0,255.9,-0.1,-255.9,4000.0,4095.99}) {
            o.preciseCamera=true;o.cameraX64=cam;o.cameraZ64=-cam;
            const auto key=render::makeNereidSurfaceKey(1,true,o);
            o.originX=key.patchX;o.originZ=key.patchZ;
            const auto prepared=render::NereidSurfaceResident::prepare(sea,nullptr,1,o);
            check(bool(prepared),"legacy ocean patch prepare failed");
            const auto b=bounds(prepared->cpu,o.originX,o.originZ);
            const double margin=std::min({cam-b.minX,b.maxX-cam,-cam-b.minZ,b.maxZ+cam});
            check(margin>=128.0-1e-6,"legacy ocean patch edge closer than r/2 to the camera ("+std::to_string(margin)+" m)");
        }
    }

    // 2. Rings: topology, triangle budget, coverage.
    n::WaterSurfaceMeshOptions r;
    r.oceanRings=true;r.oceanPatchRadius=6000;r.lod=n::WaterSurfaceLod::Medium; // inner cell 4 m
    r.oceanInnerCells=16;r.preciseCamera=true;
    check(n::oceanRingLevels(r)==8 && n::oceanRingExtent(r)==8192.0,"ring levels for 6 km radius");
    r.cameraX64=10;r.cameraZ64=-7;
    {
        const auto key=render::makeNereidSurfaceKey(3,true,r);
        r.originX=key.patchX;r.originZ=key.patchZ;
        const auto p=render::NereidSurfaceResident::prepare(sea,nullptr,3,r);
        check(bool(p),"ring ocean prepare failed");
        checkWatertight(p->cpu,"rings k16 L8");
        // 2k x 2k inner cells, then 3k^2 cells per ring, 4k of them fanned.
        const size_t k=16,L=8;
        const size_t expected=2*(2*k)*(2*k)+(L-1)*(2*3*k*k+4*k);
        check(p->cpu.triangleCount()==expected,"ring triangle count "+std::to_string(p->cpu.triangleCount()));
        check(p->cpu.vertices.size()<20000,"ring vertex budget");
        const auto b=bounds(p->cpu,r.originX,r.originZ);
        check(b.minX<=10-8192+64+1e-6 && b.maxX>=10+8192-64-1e-6,"ring coverage around the camera");
        // Inner detail (4 m) must surround the camera: snapping keeps the
        // camera within step/2 = 16 m of the centre of a 64 m half square.
        check(std::abs(10-key.patchX)<=16 && std::abs(-7-key.patchZ)<=16,"ring centre snapping");
        // Legacy uniform alternative at the same extent and Near detail: 8x8
        // tiles x 4x4 sub-quads -> 2048 triangles of 128 m each; ring cells
        // near the camera are 4 m, i.e. 32x finer at a bounded cost.
        std::cout<<"rings: "<<p->cpu.triangleCount()<<" triangles, "<<p->cpu.vertices.size()
                 <<" vertices, half extent "<<n::oceanRingExtent(r)<<" m\n";
    }
    // Smaller and larger inner squares stay watertight; tiles partition indices.
    for(uint16_t k:{4,8,32}) for(uint8_t grid:{1,4}) {
        auto o=r;o.oceanInnerCells=k;o.rippleTileGrid=grid;o.oceanPatchRadius=2000;
        const auto key=render::makeNereidSurfaceKey(3,true,o);
        o.originX=key.patchX;o.originZ=key.patchZ;
        const auto p=render::NereidSurfaceResident::prepare(sea,nullptr,3,o);
        check(bool(p),"ring prepare k="+std::to_string(k));
        checkWatertight(p->cpu,"rings variant");
        if(grid==1)check(p->cpu.tiles.empty(),"untiled rings must be one draw");
        else {
            size_t covered=0;uint32_t next=0;
            for(const auto& t:p->cpu.tiles){check(t.firstIndex==next,"tile ranges must be contiguous");next+=t.indexCount;covered+=t.indexCount;}
            check(covered==p->cpu.indices.size(),"tiles must cover every index");
            check(p->cpu.tiles.size()==size_t(grid)*grid+size_t(n::oceanRingLevels(o)-1),"tile count: inner grid + one per ring");
        }
    }

    // 3. Residency: motion inside a snap cell keeps the GPU mesh; crossing rebuilds.
    {
        auto o=r;o.cameraX64=0;o.cameraZ64=0;
        const auto a=render::makeNereidSurfaceKey(3,true,o);
        o.cameraX64=15.9;o.cameraZ64=-15.9;
        const auto b=render::makeNereidSurfaceKey(3,true,o);
        check(render::sameNereidSurfaceKey(a,b),"motion inside a ring snap cell must not re-upload");
        o.cameraX64=16.1;
        const auto c=render::makeNereidSurfaceKey(3,true,o);
        check(!render::sameNereidSurfaceKey(a,c)&&c.patchX==32,"crossing a snap cell must rebuild");
        auto legacy=o;legacy.oceanRings=false;
        check(!render::sameNereidSurfaceKey(render::makeNereidSurfaceKey(3,true,legacy),c),"ring/patch switch must rebuild");
        // Same snapped centre => byte-identical CPU mesh (deterministic).
        auto p1=r;p1.cameraX64=1;p1.cameraZ64=1;auto p2=r;p2.cameraX64=-3;p2.cameraZ64=5;
        for(auto* o2:{&p1,&p2}){const auto k2=render::makeNereidSurfaceKey(3,true,*o2);o2->originX=k2.patchX;o2->originZ=k2.patchZ;}
        const auto m1=render::NereidSurfaceResident::prepare(sea,nullptr,3,p1);
        const auto m2=render::NereidSurfaceResident::prepare(sea,nullptr,3,p2);
        check(m1&&m2&&m1->cpu.vertices.size()==m2->cpu.vertices.size()&&m1->cpu.indices==m2->cpu.indices,"ring mesh not deterministic");
        for(size_t i=0;i<m1->cpu.vertices.size();++i)
            check(m1->cpu.vertices[i].x==m2->cpu.vertices[i].x&&m1->cpu.vertices[i].z==m2->cpu.vertices[i].z,"ring vertices differ");
    }

    // 4. Precision 100 000 km from the origin: local vertices stay exact.
    {
        auto o=r;o.cameraX64=1e8+5;o.cameraZ64=-1e8-5;o.oceanPatchRadius=1000;
        const auto key=render::makeNereidSurfaceKey(3,true,o);
        o.originX=key.patchX;o.originZ=key.patchZ;
        const auto p=render::NereidSurfaceResident::prepare(sea,nullptr,3,o);
        check(bool(p),"far-world rings");
        checkWatertight(p->cpu,"far-world rings");
        bool unit=false;
        for(const auto& v:p->cpu.vertices){check(std::abs(v.x)<=1024.f&&std::abs(v.z)<=1024.f,"far-world rings must be local");unit|=v.x==4.f&&v.z==-4.f;}
        check(unit,"far-world inner lattice lost precision");
    }

    // 5. Invalid ring settings fail closed.
    {
        auto o=r;o.oceanInnerCells=7;
        check(!render::NereidSurfaceResident::prepare(sea,nullptr,3,o),"odd inner cell count must be rejected");
        o=r;o.maxVertices=1000;
        check(!render::NereidSurfaceResident::prepare(sea,nullptr,3,o),"ring vertex budget must be enforced");
    }

    // 6. Manifest fixture (large-world sea).
    {
        const char* path=argc>1?argv[1]:"tests/fixtures/nereid/ocean-large-world.json";
        const auto root=Json::parse(readFile(path));
        const auto config=world::parseNereidWaterManifest(root);
        check(config.surfaces.size()==1,"fixture surfaces");
        const auto& s=config.surfaces[0];
        check(s.mesh.oceanRings&&s.mesh.oceanPatchRadius==6000.f&&s.mesh.oceanInnerCell==4.f&&
              s.mesh.oceanInnerCells==16&&s.body.footprint==n::WaterFootprint::Infinite,"fixture ring settings");
        bool threw=false;
        try{world::parseNereidWaterManifest(Json::parse(R"({"nereidWater":{"surfaces":[{"id":1,"kind":"ocean","shape":"infinite","surfaceY":0,"bottomY":-10,"oceanLod":"clipmap"}]}})"));}
        catch(const std::exception&){threw=true;}
        check(threw,"unknown oceanLod must be rejected");
    }
    std::cout<<"PASS NEREID large-world ocean LOD: legacy coverage, stitched rings, keys, precision, fixture\n";
    return 0;
}
