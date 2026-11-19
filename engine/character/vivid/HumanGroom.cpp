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

float3 closestTriangle(float3 p,float3 a,float3 b,float3 c) {
    // Real-Time Collision Detection, closest point on triangle.
    const auto ab=b-a, ac=c-a, ap=p-a;
    const float d1=dot(ab,ap), d2=dot(ac,ap);
    if(d1<=0&&d2<=0)return a;
    const auto bp=p-b;
    const float d3=dot(ab,bp),d4=dot(ac,bp);
    if(d3>=0&&d4<=d3)return b;
    const float vc=d1*d4-d3*d2;
    if(vc<=0&&d1>=0&&d3<=0)return a+ab*(d1/(d1-d3));
    const auto cp=p-c;
    const float d5=dot(ab,cp),d6=dot(ac,cp);
    if(d6>=0&&d5<=d6)return c;
    const float vb=d5*d2-d1*d6;
    if(vb<=0&&d2>=0&&d6<=0)return a+ac*(d2/(d2-d6));
    const float va=d3*d6-d5*d4;
    if(va<=0&&(d4-d3)>=0&&(d5-d6)>=0)
        return b+(c-b)*((d4-d3)/((d4-d3)+(d5-d6)));
    const float denom=1.0f/(va+vb+vc);
    return a+ab*(vb*denom)+ac*(vc*denom);
}
bool rayHits(float3 origin,float3 direction,float3 a,float3 b,float3 c) {
    const auto e1=b-a,e2=c-a;
    const auto crossP=cross(direction,e2);
    const float det=dot(e1,crossP);
    if(std::fabs(det)<1e-7f)return false;
    const float inv=1.0f/det;
    const auto tvec=origin-a;
    const float u=dot(tvec,crossP)*inv;
    if(u<=1e-6f || u>=1.0f-1e-6f)return false;
    const auto qvec=cross(tvec,e1);
    const float v=dot(direction,qvec)*inv;
    if(v<=1e-6f || u+v>=1.0f-1e-6f)return false;
    return dot(e2,qvec)*inv>1e-5f;
}
}

bool HumanGroomRuntime::setCollisionMesh(
        std::span<const render::float3> vertices,
        std::span<const std::uint32_t> triangles) {
    if(vertices.empty()||vertices.size()>16384||
       triangles.empty()||triangles.size()%3||
       triangles.size()>32768*3)return false;
    for(const auto p:vertices)if(!good(p))return false;
    std::vector<CollisionTriangle> proposed;
    proposed.reserve(triangles.size()/3);
    for(std::size_t i=0;i<triangles.size();i+=3) {
        const auto a=triangles[i],b=triangles[i+1],c=triangles[i+2];
        if(a>=vertices.size()||b>=vertices.size()||c>=vertices.size()||
           a==b||a==c||b==c)return false;
        const auto ab=vertices[b]-vertices[a],ac=vertices[c]-vertices[a];
        if(length(cross(ab,ac))<1e-7f)return false;
        proposed.push_back({vertices[a],vertices[b],vertices[c]});
    }
    collisionTriangles_=std::move(proposed);
    return true;
}
void HumanGroomRuntime::clearCollisionMesh() noexcept {
    collisionTriangles_.clear();
}
render::float3 HumanGroomRuntime::resolveCollision(
        render::float3 p,const GroomGuide& guide) const {
    using namespace render;
    if(!collisionTriangles_.empty()) {
        // Interior-only constraint. The mesh must be closed; do not infer
        // inside/outside from arbitrary open triangle fragments.
        const float3 ray=normalize(float3{1.0f,0.173f,0.091f});
        std::size_t intersections=0;
        auto closest=p;
        float nearest=std::numeric_limits<float>::max();
        for(const auto& tri:collisionTriangles_) {
            if(rayHits(p,ray,tri.a,tri.b,tri.c))++intersections;
            const auto candidate=closestTriangle(p,tri.a,tri.b,tri.c);
            const auto delta=candidate-p;
            const float d2=dot(delta,delta);
            if(d2<nearest){nearest=d2;closest=candidate;}
        }
        if(intersections%2==1) {
            // Move from inside to the nearest surface, then slightly beyond.
            const auto direction=closest-p;
            if(length(direction)>1e-6f)
                return closest+normalize(direction)*0.001f;
            return closest+guide.normal*0.001f;
        }
        return p;
    }
    const float3 scaled{p.x/scalpRadii_.x,
                        p.y/scalpRadii_.y,
                        p.z/scalpRadii_.z};
    const float radius=length(scaled);
    if(radius>=1.0f)return p;
    return radius>1e-5f
        ? p*(1.001f/radius)
        : guide.root+guide.normal*0.001f;
}
void HumanGroomRuntime::generate(const HumanDNA& dna,GroomQuality quality) {
    dna.validate();
    const auto& g=dna.groom;
    std::size_t count=std::max<std::size_t>(
        4,std::size_t(std::lround(float(g.guideCount)*
                                 (0.12f+0.88f*g.density))));
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
    scalpRadii_={radiusX,radiusY,radiusZ};
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
            const float clump=g.clump*0.018f*t*t;
            const auto p=root+dir*(g.lengthMeters*t)+
                          float3{std::cos(curlAngle)*curl+
                                     std::sin(angle*0.27f)*clump,0,
                                 std::sin(curlAngle)*curl+
                                     std::cos(angle*0.27f)*clump};
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
                guide.position[j]=resolveCollision(guide.position[j],guide);
            }
        }
        // Final contact pass after segment constraints have converged.
        for(std::size_t j=1;j<guide.position.size();++j)
            guide.position[j]=resolveCollision(guide.position[j],guide);
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
    // The generated stream already has stable normal/tangent/UV/index
    // topology. Do not let auto LOD or material batching rewrite its
    // vertex IDs; the editor updates exactly this dynamic buffer per frame.
    m->uploadPrepared=true;
    return m;
}
} // namespace aiverse::vivid
