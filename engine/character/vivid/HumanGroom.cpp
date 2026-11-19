#include "HumanGroom.h"
#include "HumanMaterials.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace aiverse::vivid {
namespace {
using namespace render;
constexpr float kGolden=2.39996322972865332f;
float clampFloat(float x,float a,float b){return std::clamp(x,a,b);}
float3 cardSide(float3 tangent) {
    auto axis=std::fabs(tangent.z)<0.92f?float3{0,0,1}:float3{1,0,0};
    return normalize(cross(tangent,axis));
}
bool good(float3 v){
    return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z) &&
           std::fabs(v.x)<100000.0f&&std::fabs(v.y)<100000.0f&&std::fabs(v.z)<100000.0f;
}
}
void HumanGroomRuntime::generate(const HumanDNA& dna,GroomQuality quality) {
    dna.validate();
    const auto& g=dna.groom;
    std::size_t count=g.guideCount;
    std::size_t segments=g.segments;
    if(quality==GroomQuality::Medium) {
        count=std::max<std::size_t>(4,count/3);
        segments=std::max<std::size_t>(2,segments/2);
    }else if(quality==GroomQuality::Low) {
        count=std::max<std::size_t>(4,count/8);
        segments=std::max<std::size_t>(2,segments/4);
    }
    segments_=segments;
    stiffness_=g.stiffness;
    damping_=g.damping;
    gravity_=g.gravity;
    windStrength_=g.wind;
    guides_.clear();
    guides_.reserve(count);
    const float headScale=dna.anatomy.headScale;
    const float radiusX=0.72f*dna.anatomy.headWidth*headScale;
    const float radiusY=0.90f*headScale;
    const float radiusZ=0.75f*headScale;
    for(std::size_t i=0;i<count;++i) {
        const float u=(float(i)+0.5f)/float(count);
        const float theta=std::acos(1.0f-0.75f*u);
        const float angle=float((i+std::size_t(dna.seed))*kGolden);
        const float st=std::sin(theta),ct=std::cos(theta);
        const float3 n=normalize(float3{
            st*std::cos(angle),ct,st*std::sin(angle)});
        const float3 root{n.x*radiusX,n.y*radiusY,n.z*radiusZ};
        GroomGuide guide;
        guide.root=root;
        guide.normal=n;
        guide.rest.resize(segments+1);
        guide.position.resize(segments+1);
        guide.previous.resize(segments+1);
        const auto dir=normalize(n*0.27f+float3{0,-0.85f,0});
        for(std::size_t j=0;j<=segments;++j) {
            const float t=float(j)/float(segments);
            const float curlAngle=angle+t*11.0f;
            const float curl=g.curl*0.06f*t*t;
            const auto p=root+dir*(g.lengthMeters*t)+
                          float3{std::cos(curlAngle)*curl,0,
                                 std::sin(curlAngle)*curl};
            guide.rest[j]=guide.position[j]=guide.previous[j]=p;
        }
        guides_.push_back(std::move(guide));
    }
    lastStepMs_=0;
}
bool HumanGroomRuntime::step(float dt,render::float3 wind) {
    using namespace render;
    if(!std::isfinite(dt)||dt<0||dt>0.05f||!good(wind))return false;
    if(dt==0||guides_.empty())return true;
    const auto start=std::chrono::steady_clock::now();
    const float dt2=dt*dt, friction=1.0f-damping_*0.65f;
    const float3 force=wind*windStrength_+float3{0,-9.81f*gravity_,0};
    for(auto& guide:guides_) {
        guide.position[0]=guide.previous[0]=guide.root;
        for(std::size_t j=1;j<guide.position.size();++j) {
            const auto p=guide.position[j];
            const auto velocity=(p-guide.previous[j])*friction;
            const auto shapePull=(guide.rest[j]-p)*(stiffness_*3.0f);
            guide.position[j]=p+velocity+(force+shapePull)*dt2;
            guide.previous[j]=p;
        }
        for(int iteration=0;iteration<5;++iteration) {
            for(std::size_t j=1;j<guide.position.size();++j) {
                const auto prev=guide.position[j-1];
                const auto delta=guide.position[j]-prev;
                const float len=length(delta);
                const float restLen=length(guide.rest[j]-guide.rest[j-1]);
                if(len>1e-6f) {
                    const auto displacement=delta*((len-restLen)/len);
                    if(j>1)guide.position[j-1]+=displacement*0.30f;
                    guide.position[j]=guide.position[j]-displacement*(j==1?1.0f:0.70f);
                }
                const float r=length(guide.position[j]);
                // Scalp protection is conservative and smooth, not a full
                // character-body/cloth collision system.
                const float scalp=0.72f;
                if(r<scalp && r>1e-5f)
                    guide.position[j]=guide.position[j]*(scalp/r);
            }
        }
        for(auto v:guide.position)if(!good(v))return false;
    }
    lastStepMs_=float(std::chrono::duration<double,std::milli>(
        std::chrono::steady_clock::now()-start).count());
    return true;
}
bool HumanGroomRuntime::updateCardVertices(
        std::span<render::ModelVertex> output) const {
    using namespace render;
    if(guides_.empty()||!segments_||output.size()!=vertexCount())return false;
    std::size_t k=0;
    for(const auto& guide:guides_) {
        for(std::size_t j=0;j<=segments_;++j) {
            const auto& pos=guide.position[j];
            const auto a=guide.position[j?j-1:0];
            const auto b=guide.position[std::min(j+1,segments_)];
            const auto tangent=normalize(b-a);
            const auto side=cardSide(tangent);
            const auto normal=normalize(cross(side,tangent));
            const float t=float(j)/float(segments_);
            const float width=0.0035f*(1.0f-0.72f*t);
            for(int edge=0;edge<2;++edge) {
                auto& v=output[k++];
                v.pos=pos+side*(edge?width:-width);
                v.nrm=normal;
                v.uv={float(edge),t};
                v.tangent={tangent.x,tangent.y,tangent.z,1.0f};
                v.color=0xffffffffu;
            }
        }
    }
    return true;
}
std::unique_ptr<render::Model> HumanGroomRuntime::makeCardModel(
        const HumanDNA& dna) const {
    using namespace render;
    dna.validate();
    if(guides_.empty()||segments_==0)return {};
    auto m=std::make_unique<Model>();
    m->path="vivid-groom-"+dna.id;
    m->vertices.resize(vertexCount());
    if(!updateCardVertices(m->vertices))
        throw std::logic_error("VIVID groom card generation failed");
    m->indices.reserve(triangleCount()*3);
    for(std::size_t i=0;i<guides_.size();++i) {
        const std::uint32_t base=std::uint32_t(i*(segments_+1)*2);
        for(std::size_t j=0;j<segments_;++j) {
            const std::uint32_t at=base+std::uint32_t(j*2);
            m->indices.insert(m->indices.end(),
                             {at,at+1,at+2,at+1,at+3,at+2});
        }
    }
    m->materials.push_back(humanMaterial(HumanSurface::Hair,dna));
    MeshDef hair;
    hair.name="vivid_groom_cards";
    hair.deformable=true; // never merge/put dynamic groom in static pool
    Primitive prim;
    prim.firstVertex=0;
    prim.vertexCount=std::uint32_t(m->vertices.size());
    prim.firstIndex=0;
    prim.indexCount=std::uint32_t(m->indices.size());
    hair.primitives.push_back(prim);
    m->meshes.push_back(hair);
    Node node;
    node.name="groom";
    node.mesh=0;
    node.runtimeMutable=true;
    m->nodes.push_back(node);
    m->finalize();
    // Model::finalize derives deformability only from skin/morph metadata.
    // A CPU-updated groom is dynamic even when it has no skin joint palette.
    // Restore this explicit dynamic policy AFTER finalize to prevent the
    // general rigid geometry pool from reusing our mutable vertex stream.
    m->meshes[0].deformable=true;
    return m;
}
} // namespace aiverse::vivid
