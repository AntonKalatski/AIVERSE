#include "engine/character/vivid/HumanCreatorSession.h"
#include "engine/character/vivid/MorphAuthoring.h"
#include "engine/character/vivid/ViewportSculptPick.h"
#include "engine/character/vivid/DeformedSculptPick.h"
#include "engine/character/vivid/DeformedSculptWorkspace.h"
#include <limits>
#include "engine/character/vivid/MorphLodTransfer.h"
#include "engine/character/vivid/MorphLodSurface.h"
#include "engine/character/vivid/MorphLodMapping.h"
#include "engine/character/vivid/MorphLodGpuAuthoring.h"
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
    // 7AL: the accelerated picker must be equivalent to the original
    // triangle-order reference for neutral and animated morph poses.
    // On a dense planar grid a narrow ray should visit far fewer triangles
    // than the total model; repeated clicks retain CPU buffer capacities.
    {
        using namespace aiverse::render;
        constexpr int side=41;
        Model model;
        model.vertices.resize(side*side);
        for(int y=0;y<side;++y)for(int x=0;x<side;++x)
            model.vertices[std::size_t(y*side+x)].pos={
                float(x)/float(side-1),float(y)/float(side-1),0};
        for(int y=0;y<side-1;++y)for(int x=0;x<side-1;++x) {
            const auto a=std::uint32_t(y*side+x),b=a+1,c=a+side;
            model.indices.insert(model.indices.end(),{
                a,b,c,b,c+1,c
            });
        }
        MeshDef mesh;
        Primitive primitive;
        primitive.firstIndex=0;
        primitive.indexCount=std::uint32_t(model.indices.size());
        mesh.primitives.push_back(primitive);
        model.meshes.push_back(std::move(mesh));
        Node owner;
        owner.mesh=0;
        model.nodes.push_back(owner);
        const std::array<mat4,1> pose{{mat4{}}};
        MorphTargetInput target;
        target.name="lift";
        MorphDelta delta;
        delta.vertex=std::uint32_t((side/2)*side+side/2);
        delta.position={0,0,0.1f};
        target.deltas.push_back(delta);
        const auto morphs=SparseMorphLibrary::compile(
            model.vertices.size(),{target});
        const std::array<ShapeRoute,1> routes{{
            {Channel::JawOpen,"lift",1.f}
        }};
        const auto binding=FaceShapeBinding::compile(morphs,routes);
        DeformedSculptWorkspace workspace;
        FaceFrame face;
        for(float expression:{0.f,1.f}) {
            face.values[std::size_t(Channel::JawOpen)]=expression;
            for(float x:{0.12f,0.26f,0.51f,0.82f}) {
                const float3 origin{x,0.51f,2.f},direction{0,0,-1};
                const auto brute=pickDeformedSculptVertex(
                    model,0,pose,morphs,binding,face,origin,direction,
                    model.vertices.size());
                const auto fast=workspace.pick(
                    model,0,pose,morphs,binding,face,origin,direction,
                    model.vertices.size());
                req(brute.hit && fast.hit && brute.vertex==fast.vertex &&
                    std::fabs(brute.rayDistance-fast.rayDistance)<1.e-5f &&
                    std::fabs(brute.worldPosition.x-fast.worldPosition.x)<1.e-5f &&
                    std::fabs(brute.worldPosition.y-fast.worldPosition.y)<1.e-5f &&
                    std::fabs(brute.worldPosition.z-fast.worldPosition.z)<1.e-5f,
                    "refitted BVH sculpt hit differs from full-triangle reference");
            }
        }
        req(workspace.stats().triangles==model.indices.size()/3 &&
            workspace.stats().testedTriangles<
                workspace.stats().triangles/4,
            "BVH did not cull triangle candidates for a narrow ray");
        const auto warmCapacity=workspace.vertexCapacity();
        face.values[std::size_t(Channel::JawOpen)]=0.5f;
        req(workspace.pick(model,0,pose,morphs,binding,face,
            {0.51f,0.51f,2},{0,0,-1},model.vertices.size()).hit &&
            workspace.vertexCapacity()==warmCapacity,
            "sculpt workspace unexpectedly grew after warm-up");
        auto badPose=pose;
        badPose[0].at(0,3)=std::numeric_limits<float>::quiet_NaN();
        req(!workspace.pick(model,0,badPose,morphs,binding,face,
            {0.51f,0.51f,2},{0,0,-1},model.vertices.size()).hit,
            "refitted picking accepted a nonfinite node pose");
        req(workspace.pick(model,0,pose,morphs,binding,face,
            {0.51f,0.51f,2},{0,0,-1},model.vertices.size()).hit,
            "valid subsequent pick did not recover after rejected pose");
        // Both scratch layers must leave their previous published outputs
        // untouched when the next pose/weights are rejected.
        std::vector<float3> bindPositions,bindScratch;
        const std::array<float,1> goodWeights{{1.f}};
        const std::array<float,1> badWeights{{
            std::numeric_limits<float>::quiet_NaN()
        }};
        req(prepareSculptMorphPositions(model,morphs,goodWeights,
                                        bindPositions,&bindScratch),
            "initial morph scratch reconstruction failed");
        const auto originalZ=bindPositions[std::size_t(delta.vertex)].z;
        req(!prepareSculptMorphPositions(model,morphs,badWeights,
                                         bindPositions,&bindScratch) &&
            bindPositions[std::size_t(delta.vertex)].z==originalZ,
            "failed morph reconstruction overwrote last-good bind positions");
        SkinnedWeightSurface skinSurface;
        SkinnedWeightScratch skinScratch;
        req(prepareSkinnedWeightSurface(model,0,pose,skinSurface,
                                         bindPositions,&skinScratch),
            "initial skinned scratch reconstruction failed");
        const auto originalWorld=skinSurface.world[std::size_t(delta.vertex)].z;
        req(!prepareSkinnedWeightSurface(model,0,badPose,skinSurface,
                                          bindPositions,&skinScratch) &&
            skinSurface.ready &&
            skinSurface.world[std::size_t(delta.vertex)].z==originalWorld,
            "failed skin reconstruction overwrote last-good world positions");
        workspace.reset();
        req(workspace.stats().triangles==0 &&
            workspace.pick(model,0,pose,morphs,binding,face,
                {0.51f,0.51f,2},{0,0,-1},model.vertices.size()).hit,
            "BVH did not rebuild after explicit candidate reset");
    }
    // 7AJ: published GPU morph is sampled in bind space BEFORE skinning.
    // Picking must return the original vertex ID and final world position,
    // including the case where the morphed triangle differs from bind pose.
    {
        using namespace aiverse::render;
        Model model;
        model.vertices.resize(3);
        model.vertices[0].pos={0,0,0};
        model.vertices[1].pos={1,0,0};
        model.vertices[2].pos={0,1,0};
        for(auto& v:model.vertices) {
            v.joints[0]=0;
            v.weights[0]=255;
        }
        model.indices={0,1,2};
        MeshDef mesh;
        Primitive prim;
        prim.firstIndex=0;
        prim.indexCount=3;
        prim.jointPalette={0};
        mesh.primitives.push_back(prim);
        model.meshes.push_back(std::move(mesh));
        model.nodes.resize(2);
        model.nodes[0].mesh=0;
        model.nodes[0].skin=0;
        model.skins.resize(1);
        model.skins[0].joints={1};
        model.skins[0].inverseBind={mat4{}};
        const std::array<mat4,2> pose{{mat4{},mat4::translate({0,0,2})}};
        MorphTargetInput shape;
        shape.name="lift";
        MorphDelta delta;
        delta.vertex=0;
        delta.position={0.4f,0,0};
        shape.deltas.push_back(delta);
        const auto library=SparseMorphLibrary::compile(3,{shape});
        const std::array<ShapeRoute,1> routes{{
            {Channel::JawOpen,"lift",1.0f}
        }};
        const auto binding=FaceShapeBinding::compile(library,routes);
        FaceFrame face;
        face.values[std::size_t(Channel::JawOpen)]=1.0f;
        const auto hit=pickDeformedSculptVertex(model,0,pose,
            library,binding,face,{0.42f,0.05f,4.0f},{0,0,-1},3);
        req(hit.hit && hit.vertex==0 &&
            std::fabs(hit.worldPosition.x-0.4f)<1.e-5f &&
            std::fabs(hit.worldPosition.z-2.0f)<1.e-5f,
            "morphed/skinned sculpt pick missed actual GPU-visible vertex");
        face.values[std::size_t(Channel::JawOpen)]=0.0f;
        const auto neutral=pickDeformedSculptVertex(model,0,pose,
            library,binding,face,{0.05f,0.05f,4.0f},{0,0,-1},3);
        req(neutral.hit && neutral.vertex==0 &&
            std::fabs(neutral.worldPosition.x)<1.e-6f &&
            std::fabs(neutral.worldPosition.z-2.0f)<1.e-5f,
            "neutral morph/rig picking differs from expected skinned geometry");
        req(!pickDeformedSculptVertex(model,0,pose,
            library,binding,face,{2,2,4},{0,0,-1},3).hit,
            "deformed sculpt accepted ray missing all triangles");
        req(!pickDeformedSculptVertex(model,0,pose,
            library,binding,face,{0.05f,0.05f,4},{0,0,-1},2).hit,
            "deformed sculpt authored generated/non-source triangle");
        auto badPose=pose;
        badPose[1].at(0,3)=std::numeric_limits<float>::quiet_NaN();
        req(!pickDeformedSculptVertex(model,0,badPose,
            library,binding,face,{0.05f,0.05f,4},{0,0,-1},3).hit,
            "invalid MotionWeave pose did not fail closed");
        auto badModel=model;
        badModel.meshes[0].primitives[0].jointPalette.clear();
        req(!pickDeformedSculptVertex(badModel,0,pose,
            library,binding,face,{0.05f,0.05f,4},{0,0,-1},3).hit,
            "invalid skin palette accepted for deformed sculpt pick");
        auto shortLibrary=SparseMorphLibrary::compile(2,{});
        std::vector<float3> scratch{{9,9,9}};
        req(!prepareSculptMorphPositions(model,shortLibrary,{},scratch) &&
            scratch.size()==1 && scratch[0].x==9,
            "invalid morph source changed last-good picking scratch");
    }
    // 7AK: after palette seam splitting the *visible* triangle can use
    // cloned global indices, but sculpting must still edit the author's
    // original source vertex. Generated LOD IDs stay unselectable.
    {
        using namespace aiverse::render;
        Model model;
        model.vertices.resize(5);
        model.vertices[0].pos={0,0,0};
        model.vertices[1].pos={1,0,0};
        model.vertices[2].pos={0,1,0};
        model.vertices[3]=model.vertices[0]; // seam clone of vertex zero
        model.vertices[4]=model.vertices[0]; // generated LOD, not authorable
        for(auto& v:model.vertices){v.joints[0]=0;v.weights[0]=255;}
        model.indices={3,1,2};
        MeshDef mesh;
        Primitive primitive;
        primitive.firstIndex=0;
        primitive.indexCount=3;
        primitive.jointPalette={0};
        mesh.primitives.push_back(primitive);
        model.meshes.push_back(std::move(mesh));
        model.nodes.resize(2);
        model.nodes[0].mesh=0;
        model.nodes[0].skin=0;
        model.skins.resize(1);
        model.skins[0].joints={1};
        model.skins[0].inverseBind={mat4{}};
        const std::array<mat4,2> pose{{mat4{},mat4::translate({0,0,2})}};
        MorphTargetInput shape;
        shape.name="lift";
        for(std::uint32_t vertex:{0u,3u}) {
            MorphDelta delta;
            delta.vertex=vertex;
            delta.position={0.4f,0,0};
            shape.deltas.push_back(delta);
        }
        const auto library=SparseMorphLibrary::compile(5,{shape});
        const std::array<ShapeRoute,1> routes{{
            {Channel::JawOpen,"lift",1.0f}
        }};
        const auto binding=FaceShapeBinding::compile(library,routes);
        FaceFrame face;
        face.values[std::size_t(Channel::JawOpen)]=1.0f;
        std::vector<std::uint32_t> authorIds;
        const std::array<SkinSeamSplit::Clone,1> clones{{
            {0,3,0,0}
        }};
        req(buildSculptSourceVertexMap(5,3,clones,authorIds) &&
            authorIds.size()==5 && authorIds[0]==0 &&
            authorIds[3]==0 &&
            authorIds[4]==std::numeric_limits<std::uint32_t>::max(),
            "seam mapping must preserve source IDs and exclude generated LODs");
        const auto hit=pickDeformedSculptVertex(
            model,0,pose,library,binding,face,
            {0.42f,0.05f,4.f},{0,0,-1},3,authorIds);
        req(hit.hit && hit.vertex==0 &&
            std::fabs(hit.worldPosition.x-0.4f)<1.e-5f &&
            std::fabs(hit.worldPosition.z-2.0f)<1.e-5f,
            "seam clone did not resolve to original source vertex");
        model.indices[0]=4; // same triangle geometry, only LOD index changes
        req(!pickDeformedSculptVertex(
            model,0,pose,library,binding,face,
            {0.05f,0.05f,4.f},{0,0,-1},3,authorIds).hit,
            "generated LOD triangle leaked into original sculpt vertex selection");
        const std::array<SkinSeamSplit::Clone,2> sourceAndLodClones{{
            {0,3,0,0},{4,5,0,1}
        }};
        std::vector<std::uint32_t> expandedIds;
        req(buildSculptSourceVertexMap(6,3,sourceAndLodClones,expandedIds) &&
            expandedIds.size()==6 && expandedIds[3]==0 &&
            expandedIds[5]==std::numeric_limits<std::uint32_t>::max(),
            "LOD seam clones must remain excluded without blocking publication");
        const auto lastGood=authorIds;
        const std::array<SkinSeamSplit::Clone,2> duplicated{{
            {0,3,0,0},{1,3,0,0}
        }};
        req(!buildSculptSourceVertexMap(5,3,duplicated,authorIds) &&
            authorIds==lastGood,
            "duplicate clone mapping must fail without overwriting last-good map");
        const std::array<SkinSeamSplit::Clone,1> invalid{{
            {8,3,0,0}
        }};
        req(!buildSculptSourceVertexMap(5,3,invalid,authorIds) &&
            authorIds==lastGood,
            "invalid clone source ID must preserve last-good map");
        const std::array<std::uint32_t,1> shortMapping{{0}};
        model.indices[0]=3;
        req(!pickDeformedSculptVertex(
            model,0,pose,library,binding,face,
            {0.42f,0.05f,4.f},{0,0,-1},3,shortMapping).hit,
            "truncated published vertex mapping must reject picking");
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
        const auto accelerated=MorphLodSurface::projectAccelerated(
            sourcePositions,faces,lodPositions,0.05f);
        req(accelerated.size()==correspondence.size(),
            "BVH LOD bake returned different number of mappings");
        for(std::size_t i=0;i<accelerated.size();++i) {
            req(accelerated[i].triangle==correspondence[i].triangle &&
                std::fabs(accelerated[i].bary.x-correspondence[i].bary.x)<1.e-5f &&
                std::fabs(accelerated[i].bary.y-correspondence[i].bary.y)<1.e-5f &&
                std::fabs(accelerated[i].bary.z-correspondence[i].bary.z)<1.e-5f,
                "BVH facial LOD projection differs from reference");
        }
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
        MorphLodMapping mappingSidecar;
        mappingSidecar.sourceVertices=3;
        mappingSidecar.sourceTriangles=1;
        mappingSidecar.targetVertexIds={4,7};
        mappingSidecar.sourceFingerprint=MorphLodMapping::fingerprint(sourcePositions,faces);
        mappingSidecar.samples=correspondence;
        const auto restored=MorphLodMapping::fromJson(mappingSidecar.toJson());
        req(restored.samples.size()==2 &&
            restored.targetVertexIds==std::vector<std::uint32_t>({4,7}) &&
            std::fabs(restored.samples[0].bary.x-0.5f)<1.e-5f,
            "LOD barycentric sidecar lost mapping data");
        const auto restoredMorphs=restored.transfer(sourceMorph,sourcePositions,faces);
        req(restoredMorphs.targets()==1 && restoredMorphs.deltas()==2,
            "LOD sidecar did not reapply stored surface projection");
        auto alteredSource=sourcePositions;
        alteredSource[0].x=0.2f;
        bool fingerprintRejected=false;
        try {(void)restored.transfer(sourceMorph,alteredSource,faces);}
        catch(const std::invalid_argument&){fingerprintRejected=true;}
        req(fingerprintRejected,
            "LOD sidecar accepted changed same-size source geometry");
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
    {
        // A single actor CSR must contain both original facial mesh vertices
        // and independently authored LOD vertices with the same target weight.
        MorphTargetInput original;
        original.name="jawOpen";
        MorphDelta a;
        a.vertex=0;
        a.position={0,0.2f,0};
        original.deltas.push_back(a);
        const auto full=SparseMorphLibrary::compile(5,{original});
        MorphTargetInput lodTarget;
        lodTarget.name="jawOpen";
        MorphDelta b;
        b.vertex=1;
        b.position={0,0.4f,0};
        lodTarget.deltas.push_back(b);
        const auto compact=SparseMorphLibrary::compile(2,{lodTarget});
        const std::array<std::uint32_t,2> ids{{3,4}};
        const auto merged=MorphLodGpuAuthoring::combine(full,compact,ids);
        req(merged.vertices()==5 && merged.targets()==1 &&
            merged.deltas()==2 && merged.gpuDeltas()[0].vertex==0 &&
            merged.gpuDeltas()[1].vertex==4,
            "full-model GPU actor CSR omitted source or authored LOD delta");
        MorphTargetInput additional;
        additional.name="jawOpen";
        MorphDelta third;
        third.vertex=0;
        third.position={0,0.6f,0};
        additional.deltas.push_back(third);
        const auto secondCompact=SparseMorphLibrary::compile(1,{additional});
        const std::array<std::uint32_t,1> secondIds{{2}};
        const std::array<MorphLodGpuAuthoring::Layer,2> layers{{
            {&compact,ids},{&secondCompact,secondIds}}};
        const auto combined=MorphLodGpuAuthoring::combineMany(full,layers);
        req(combined.vertices()==5 && combined.targets()==1 &&
            combined.deltas()==3 &&
            combined.gpuDeltas()[0].vertex==0 &&
            combined.gpuDeltas()[1].vertex==2 &&
            combined.gpuDeltas()[2].vertex==4,
            "multi-LOD facial CSR missing shared semantic deltas");
        const std::array<std::uint32_t,1> conflictingIds{{4}};
        const std::array<MorphLodGpuAuthoring::Layer,2> conflictLayers{{
            {&compact,ids},{&secondCompact,conflictingIds}}};
        bool multiRejected=false;
        try {(void)MorphLodGpuAuthoring::combineMany(full,conflictLayers);}
        catch(const std::invalid_argument&){multiRejected=true;}
        req(multiRejected,"overlapping multi-LOD target vertices accepted");
        const std::array<std::uint32_t,2> overlap{{0,4}};
        bool rejected=false;
        try {(void)MorphLodGpuAuthoring::combine(full,compact,overlap);}
        catch(const std::invalid_argument&){rejected=true;}
        req(!rejected,
            "LOD delta on unused target index must not conflict with source");
        MorphDelta c;
        c.vertex=0;
        c.position={0,0.1f,0};
        lodTarget.deltas.push_back(c);
        const auto overlapCompact=SparseMorphLibrary::compile(2,{lodTarget});
        rejected=false;
        try {(void)MorphLodGpuAuthoring::combine(full,overlapCompact,overlap);}
        catch(const std::invalid_argument&){rejected=true;}
        req(rejected,"GPU actor LOD CSR accepted overlapping authored deltas");
    }
    std::filesystem::remove(file);
}
}
int main(){try{test();std::cout<<"VIVID creator transactions PASS\n";}
catch(const std::exception& e){std::cerr<<"VIVID creator FAIL: "<<e.what()<<"\n";return 1;}return 0;}
