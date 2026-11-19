#include "engine/character/vivid/HumanCreatorSession.h"
#include "engine/character/vivid/MorphAuthoring.h"
#include "engine/character/vivid/ViewportSculptPick.h"
#include "engine/character/vivid/MorphLodTransfer.h"
#include "engine/character/vivid/MorphLodSurface.h"
#include "engine/character/vivid/HumanImport.h"
#include <filesystem>
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace aiverse::vivid;
namespace {
void req(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
void test() {
    HumanCreatorSession session;
    req(session.dna().id=="vivid_default" && !session.dirty(),
        "creator default failed");
    const auto start=session.revision();
    req(session.edit([](HumanDNA& d){d.skin.wetness=0.7f;}),
        "valid DNA edit rejected");
    req(session.revision()==start+1 && session.dirty() &&
        session.dna().skin.wetness==0.7f,"DNA transaction not recorded");
    std::string error;
    req(!session.edit([](HumanDNA& d){d.groom.guideCount=999999;},&error) &&
        !error.empty() && session.dna().groom.guideCount==512,
        "invalid creator edit was not rolled back");
    req(session.undo() && session.dna().skin.wetness==0 &&
        session.canRedo(),"DNA undo failed");
    req(session.redo() && session.dna().skin.wetness==0.7f,
        "DNA redo failed");
    req(session.preset("stylized") &&
        session.dna().anatomy.eyeScale>1.1f,"creator preset not applied");
    req(session.undo() && session.dna().skin.wetness==0.7f,
        "preset undo failed");
    const auto file=(std::filesystem::temp_directory_path()/
        "aiverse-vivid-human-creator-test.json").string();
    req(session.saveFile(file,&error) && !session.dirty(),
        "creator save failed");
    HumanCreatorSession second;
    req(second.loadFile(file,&error) && !second.dirty() &&
        second.dna().toJson()==session.dna().toJson(),
        "creator JSON file load mismatch");
    req(!second.loadJson(aiverse::Json::parse(R"({"version":100})"),&error) &&
        second.dna().toJson()==session.dna().toJson(),
        "creator invalid DNA replaced last-good profile");
    {
        MorphTargetInput shape;
        shape.name="mouthSmileLeft";
        MorphDelta initial;
        initial.vertex=1;
        initial.position={0.1f,0,0};
        shape.deltas.push_back(initial);
        const auto csr=SparseMorphLibrary::compile(4,{shape});
        auto sculpt=MorphAuthoring::fromCompiled(csr);
        req(sculpt.targets()==1 && sculpt.vertices()==4,
            "sculpt target import failed");
        req(sculpt.setPosition(0,1,{0.25f,0.1f,0}) && sculpt.canUndo(),
            "sparse morph edit failed");
        req(sculpt.undo() && sculpt.redo(),
            "sparse morph undo/redo failed");
        const auto exported=sculpt.toJson(4);
        const auto compiled=parseHumanMorphs(exported,4);
        req(compiled.targets()==1 && compiled.deltas()==1,
            "sparse morph JSON roundtrip lost shape");
        std::size_t targetId=0;
        req(compiled.findTarget("mouthSmileLeft",targetId) && targetId==0,
            "sparse sculpt export lost target name");
        req(sculpt.addTarget("browUp") && sculpt.targets()==2,
            "named corrective creation failed");
        req(!sculpt.addTarget("browUp"),
            "duplicate corrective name accepted");
        req(sculpt.setPosition(1,2,{0,0.2f,0}) &&
            sculpt.removeTarget(0) && sculpt.targets()==1,
            "corrective deletion or remapping failed");
        req(sculpt.undo() && sculpt.targets()==2 &&
            sculpt.redo() && sculpt.targets()==1,
            "corrective target deletion undo/redo failed");
        const std::array<MorphVec3,4> positions{{{0,0,0},{0.1f,0,0},
                                                  {0.3f,0,0},{2,0,0}}};
        req(sculpt.brush(0,positions,0,0.35f,{0.05f,0,0}) &&
            sculpt.canUndo(),
            "falloff morph brush rejected");
        const auto brushed=sculpt.compile();
        req(brushed.deltas()>=2,"brush did not affect adjacent source vertices");
        req(!sculpt.brush(0,positions,0,-1.0f,{0.1f,0,0}) &&
            !sculpt.brush(0,positions,0,0.5f,{50.0f,0,0}),
            "invalid brush parameters accepted");
        req(sculpt.undo() && sculpt.redo(),
            "brush stroke history failed");
        req(!sculpt.setPosition(0,4,{1,0,0}) &&
            !sculpt.setPosition(0,1,{100,0,0}),
            "out-of-range sculpt accepted");
        bool rejected=false;
        try { (void)sculpt.toJson(1); }
        catch(const std::invalid_argument&) { rejected=true; }
        req(rejected,"sculpt exported generated vertex outside original topology");
    }
    {
        // Actual triangle-ray hit must choose the nearest source vertex,
        // never a projected point hidden behind unrelated model surfaces.
        aiverse::render::Model model;
        model.vertices.resize(3);
        model.vertices[0].pos={0,0,0};
        model.vertices[1].pos={1,0,0};
        model.vertices[2].pos={0,1,0};
        model.indices={0,1,2};
        aiverse::render::MeshDef mesh;
        aiverse::render::Primitive triangle;
        triangle.firstIndex=0;
        triangle.indexCount=3;
        mesh.primitives.push_back(triangle);
        model.meshes.push_back(std::move(mesh));
        aiverse::render::Node node;
        node.mesh=0;
        model.nodes.push_back(node);
        const std::array<aiverse::render::mat4,1> pose{};
        const auto picked=pickSculptVertex(model,0,pose,
            {0.05f,0.05f,2.0f},{0,0,-1},3);
        req(picked.hit&&picked.vertex==0,
            "viewport sculpt did not pick expected nearest triangle vertex");
        req(!pickSculptVertex(model,0,pose,
            {2,2,2},{0,0,-1},3).hit,
            "viewport sculpt accepted ray missing the triangle");
    }
    {
        auto target=MorphTargetInput{};
        target.name="symmetric";
        auto sculpt=MorphAuthoring::fromCompiled(
            SparseMorphLibrary::compile(2,{target}));
        const std::array<MorphVec3,2> source{{{-1,0,0},{1,0,0}}};
        req(sculpt.brush(0,source,0,0.2f,{0.1f,0,0},true),
            "bilateral sculpt stroke rejected");
        const auto compiledSculpt=sculpt.compile();
        const auto gpu=compiledSculpt.gpuDeltas();
        req(gpu.size()==2&&gpu[0].position[0]>0.09f &&
            gpu[1].position[0]<-0.09f,
            "sculpt symmetry did not mirror horizontal displacement");
        req(sculpt.undo()&&sculpt.compile().deltas()==0,
            "symmetric brush did not undo as one transaction");
    }
    {
        MorphTargetInput shape;
        shape.name="eyeBlinkLeft";
        MorphDelta delta;
        delta.vertex=2;delta.position={0,0.1f,0};
        shape.deltas.push_back(delta);
        auto source=SparseMorphLibrary::compile(4,{shape});
        const std::array<std::uint32_t,3> mapping{{0,2,3}};
        const std::array<aiverse::render::float3,4> sourcePositions{{
            {0,0,0},{1,0,0},{2,0,0},{3,0,0}}};
        const std::array<aiverse::render::float3,3> lodPositions{{
            {0,0,0},{2,0,0},{3,0,0}}};
        req(MorphLodTransfer::matchPositions(sourcePositions,lodPositions)==
            std::vector<std::uint32_t>(mapping.begin(),mapping.end()),
            "exact geometric LOD matching returned incorrect indices");
        auto lod=MorphLodTransfer::transfer(source,mapping);
        req(lod.vertices()==3 && lod.targets()==1 && lod.deltas()==1 &&
            lod.gpuDeltas()[0].vertex==1 &&
            std::fabs(lod.gpuDeltas()[0].position[1]-0.1f)<1.e-6f,
            "facial LOD source vertex mapping lost corrective delta");
        bool rejected=false;
        const std::array<std::uint32_t,1> bad{{100}};
        try {(void)MorphLodTransfer::transfer(source,bad);}
        catch(const std::invalid_argument&){rejected=true;}
        req(rejected,"facial LOD accepted invalid original vertex ID");
    }
    {
        // Independently decimated vertex inside a source face has no exact
        // source-vertex match, but receives barycentrically blended deltas.
        const std::array<aiverse::render::float3,3> sourcePositions{{
            {0,0,0},{2,0,0},{0,2,0}}};
        const std::array<std::uint32_t,3> faces{{0,1,2}};
        const std::array<aiverse::render::float3,2> lodPositions{{
            {0.5f,0.5f,0},{0.25f,0.25f,0.01f}}};
        const auto correspondence=MorphLodSurface::project(
            sourcePositions,faces,lodPositions,0.05f);
        req(correspondence.size()==2 &&
            correspondence[0].triangle==0 &&
            std::fabs(correspondence[0].bary.x-0.5f)<1.e-5f &&
            std::fabs(correspondence[0].bary.y-0.25f)<1.e-5f &&
            std::fabs(correspondence[0].bary.z-0.25f)<1.e-5f,
            "surface projected LOD barycentric coordinates incorrect");
        MorphTargetInput shape;
        shape.name="faceLift";
        MorphDelta a,b;
        a.vertex=0;a.position={0,1,0};
        b.vertex=1;b.position={0,2,0};
        shape.deltas={a,b};
        const auto sourceMorph=SparseMorphLibrary::compile(3,{shape});
        const auto projected=MorphLodSurface::transfer(
            sourceMorph,faces,correspondence);
        req(projected.vertices()==2 && projected.targets()==1 &&
            projected.deltas()==2 &&
            std::fabs(projected.gpuDeltas()[0].position[1]-1.0f)<1.e-5f,
            "decimated facial LOD did not interpolate source morph");
        bool rejected=false;
        const std::array<aiverse::render::float3,1> tooFar{{{0.5f,0.5f,1}}};
        try {(void)MorphLodSurface::project(sourcePositions,faces,tooFar,0.01f);}
        catch(const std::invalid_argument&){rejected=true;}
        req(rejected,"LOD projected a vertex outside authored surface tolerance");
    }
    std::filesystem::remove(file);
}
}
int main(){try{test();std::cout<<"VIVID creator transactions PASS\n";}
catch(const std::exception& e){std::cerr<<"VIVID creator FAIL: "<<e.what()<<"\n";return 1;}return 0;}
