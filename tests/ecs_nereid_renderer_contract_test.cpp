// NEREID stage 7C: CPU-only renderer bridge contract, no GPU device needed.
#include "engine/render/NereidWaterRenderer.h"
#include "engine/render/NereidSurfaceResident.h"
#include "engine/world/NereidWaterScene.h"
#include "engine/world/NereidWaterManifest.h"
#include "engine/world/NereidEcsPresentation.h"
#include "engine/render/Shaders.h"
#include "engine/render/NereidWaterReflectionPolicy.h"
#include <iostream>
#include <stdexcept>
#include <string>
using namespace aiverse;
static void check(bool value,const char* message) {
    if(!value)throw std::runtime_error(message);
}
int main() {
    lib::nereid::WaterSurfaceVertex shore;
    shore.shore=1.f;
    lib::nereid::WaterSurfaceVertex deep;
    const auto a=render::nereidWaterVertexColor(shore);
    const auto b=render::nereidWaterVertexColor(deep);
    check(a!=b,"shore foam must affect vertex tint");
    const lib::nereid::WaterSurfaceStyle style{};
    check(render::validNereidWaterStyle(style),"valid default water style");
    auto bad=style;bad.opacity=1.f;
    check(!render::validNereidWaterStyle(bad),
          "opaque water breaks translucent reflection path");
    check((render::kDrawWaterSurface & render::kDrawUnlit)==0 &&
          (render::kDrawWaterSurface & render::kDrawNoCastShadow)==0,
          "NEREID draw flag conflicts with existing flags");
    check(std::string(render::shaders::kLitFS).find("u_params.x < -0.5")
          !=std::string::npos,"GLSL water flag missing");
    check(std::string(render::shaders::kLitMSL).find("u.params.x < -0.5")
          !=std::string::npos,"Metal water flag missing");
    check(render::sameWaterReflectionPlane(12.f,12.01f),
          "same-level water must share primary planar reflection");
    check(!render::sameWaterReflectionPlane(12.f,9.f),
          "water at a distinct elevation must not reuse a wrong mirror");
    check(!render::sameWaterReflectionPlane(12.f,12.03f),
          "water beyond plane epsilon must not use primary reflection");
    check(std::string(render::shaders::kLitFS).find(
          "u_params.x < -1.5 ? 0.0 : u_params.y")!=std::string::npos,
          "GLSL secondary-plane reflection suppression missing");
    check(std::string(render::shaders::kLitMSL).find(
          "u.params.x < -1.5 ? 0.0 : u.params.y")!=std::string::npos,
          "Metal secondary-plane reflection suppression missing");
    // Stable patch reuses a resident ocean mesh until the camera moves into
    // another grid cell; small camera shifts are not GPU upload triggers.
    lib::nereid::WaterSurfaceMeshOptions opts;
    opts.oceanPatchRadius=128.f;
    opts.cameraX=10.f;opts.cameraZ=-10.f;
    auto first=render::makeNereidSurfaceKey(7,true,opts);
    opts.cameraX=120.f;opts.cameraZ=-120.f;
    auto shifted=render::makeNereidSurfaceKey(7,true,opts);
    check(render::sameNereidSurfaceKey(first,shifted),
          "camera motion within an ocean patch must not trigger upload");
    opts.cameraX=129.f;
    auto moved=render::makeNereidSurfaceKey(7,true,opts);
    check(!render::sameNereidSurfaceKey(first,moved),
          "crossing an ocean cell must trigger rebuild");
    auto revised=render::makeNereidSurfaceKey(8,true,opts);
    check(!render::sameNereidSurfaceKey(moved,revised),
          "geometry revision must invalidate resident surface");
    opts.cameraX=10.f;
    auto boxA=render::makeNereidSurfaceKey(7,false,opts);
    opts.cameraX=10000.f;
    auto boxB=render::makeNereidSurfaceKey(7,false,opts);
    check(render::sameNereidSurfaceKey(boxA,boxB),
          "finite water body must ignore camera motion");
    auto recolored=style;recolored.foamStrength=0.2f;
    check(!render::sameNereidSurfaceKey(
          boxB,render::makeNereidSurfaceKey(7,false,opts,recolored)),
          "foam tint change must trigger upload");
    check(render::nereidPatchAnchor(-1.f,128.f)==-128.f,
          "negative camera coordinates must floor to correct patch");
    check(!std::isfinite(render::nereidPatchAnchor(0.f,0.f)),
          "invalid ocean radius must be rejected");
    world::NereidWaterScene sceneWater;
    std::vector<world::NereidSceneSurface> waterSources(2);
    sceneWater.setSurfaces(waterSources);
    check(sceneWater.visibleSurfaceCount()==0,
          "unuploaded water must not be counted as resident");
    bool rejected=false;
    try {
        sceneWater.setSurfaces(std::vector<world::NereidSceneSurface>(
            world::NereidWaterScene::kMaxSurfaces+1));
    }catch(const std::invalid_argument&) {rejected=true;}
    check(rejected,"water scene must reject unbounded surface lists");
    check(sceneWater.visibleSurfaceCount()==0,
          "rejected configuration must leave scene state unchanged");
    // WorldScene manifest is opt-in; legacy manifests instantiate no water.
    const auto emptyManifest=world::parseNereidWaterManifest(Json::parse("{}"));
    check(emptyManifest.surfaces.empty() && emptyManifest.maxUploadsPerFrame==2,
          "legacy world manifest should not opt in to NEREID");
    const auto authored=world::parseNereidWaterManifest(Json::parse(R"json(
    {"nereidWater":{"maxUploadsPerFrame":3,"surfaces":[
      {"kind":"ocean","shape":"infinite","surfaceY":2,"bottomY":-100,
       "oceanPatchRadius":300,"lod":"far"},
      {"kind":"lake","shape":"polygon","surfaceY":4,"bottomY":-5,
       "points":[[0,0],[40,0],[40,40],[0,40]],"style":{"opacity":0.7}},
      {"kind":"river","shape":"river","surfaceY":3,"bottomY":0,
       "points":[[0,0,4],[100,0,8]],"lod":"medium"}
    ]}})json"));
    check(authored.maxUploadsPerFrame==3 && authored.surfaces.size()==3,
          "manifest must parse water bodies and upload quota");
    check(authored.surfaces[0].body.footprint==lib::nereid::WaterFootprint::Infinite &&
          authored.surfaces[0].mesh.oceanPatchRadius==300.f,
          "manifest ocean patch configuration missing");
    check(authored.surfaces[1].useGeometry &&
          authored.surfaces[1].geometry.points.size()==4,
          "manifest polygon points missing");
    check(authored.surfaces[2].geometry.kind==lib::nereid::WaterGeometryKind::RiverSpline &&
          authored.surfaces[2].geometry.points[1].halfWidth==8_fx,
          "manifest river node widths missing");
    auto rejectedManifest=[](const char* json){
        try {
            world::parseNereidWaterManifest(Json::parse(json));
        }catch(const JsonError&){return true;}
        return false;
    };
    check(rejectedManifest(R"({"nereidWater":{"maxUploadsPerFrame":0,"surfaces":[]}})"),
          "manifest must reject zero per-frame GPU budget");
    check(rejectedManifest(R"({"nereidWater":{"surfaces":[
      {"shape":"river","surfaceY":2,"bottomY":1,"points":[[0,0,-1],[2,3,1]]}
    ]}})"),"manifest must reject negative river width");
    check(rejectedManifest(R"({"nereidWater":{"surfaces":[
      {"shape":"box","surfaceY":2,"bottomY":1,"halfExtents":[1,-3]}
    ]}})"),"manifest must reject negative box footprint");
    check(rejectedManifest(R"({"nereidWater":{"surfaces":[
      {"shape":"disc","surfaceY":2,"bottomY":1,"radius":0}
    ]}})"),"manifest must reject empty circle");
    sceneWater.setMaxUploadsPerFrame(1);
    check(sceneWater.maxUploadsPerFrame()==1,
          "scene water scheduler must expose configured budget");
    bool rejectedQuota=false;
    try {sceneWater.setMaxUploadsPerFrame(17);}
    catch(const std::invalid_argument&){rejectedQuota=true;}
    check(rejectedQuota && sceneWater.maxUploadsPerFrame()==1,
          "out-of-range upload budget must preserve last valid value");
    // Stable identity retains only changed per-body revisions.
    world::NereidWaterScene identity;
    world::NereidSceneSurface lakeA,lakeB;
    lakeA.id=10;lakeA.body.center.x=4_fx;
    lakeB.id=20;lakeB.body.center.x=30_fx;
    identity.setSurfaces({lakeA,lakeB});
    const auto initialA=identity.surfaceRevision(10);
    const auto initialB=identity.surfaceRevision(20);
    check(initialA!=0 && initialB!=0,"surface revisions must initialize");
    identity.setSurfaces({lakeB,lakeA});
    check(identity.surfaceRevision(10)==initialA &&
          identity.surfaceRevision(20)==initialB,
          "reordering surfaces must not invalidate stable GPU owners");
    lakeA.body.center.x=8_fx;
    identity.setSurfaces({lakeA,lakeB});
    check(identity.surfaceRevision(10)!=initialA &&
          identity.surfaceRevision(20)==initialB,
          "a changed water body must not invalidate unrelated surfaces");
    bool duplicate=false;
    try {identity.setSurfaces({lakeA,lakeA});}
    catch(const std::invalid_argument&){duplicate=true;}
    check(duplicate && identity.surfaceCount()==2,
          "duplicate id must reject snapshot transactionally");
    identity.setMaxEstimatedUploadBytes(4096);
    check(identity.maxEstimatedUploadBytes()==4096,
          "estimated byte quota must be configurable");
    check(identity.estimatedUploadBytes(lakeA)>0,
          "estimated surface upload must account for geometry budget");
    identity.setVisibilityRange(20.f);
    Vec3 camera{0_fx,0_fx,0_fx};
    check(world::NereidWaterScene::visibleFrom(lakeA,&camera,20.f),
          "near water footprint should be visible");
    lakeB.body.center.x=1000_fx;
    check(!world::NereidWaterScene::visibleFrom(lakeB,&camera,20.f),
          "far water must be distance culled");
    lakeB.body.footprint=lib::nereid::WaterFootprint::Infinite;
    check(world::NereidWaterScene::visibleFrom(lakeB,&camera,20.f),
          "infinite ocean must remain visible");
    check(rejectedManifest(R"({"nereidWater":{"surfaces":[
      {"id":1,"surfaceY":2,"bottomY":1,"shape":"box","halfExtents":[2,2]},
      {"id":1,"surfaceY":3,"bottomY":1,"shape":"box","halfExtents":[2,2]}
    ]}})"),"manifest must reject duplicate explicit water ids");
    const auto budgets=world::parseNereidWaterManifest(Json::parse(R"({
      "nereidWater":{"maxEstimatedUploadBytes":8192,"visibilityRange":200,
        "surfaces":[{"id":17,"surfaceY":2,"bottomY":1,
                     "shape":"box","halfExtents":[10,20]}]}
    })"));
    check(budgets.surfaces[0].id==17 &&
          budgets.maxEstimatedUploadBytes==8192 &&
          budgets.visibilityRange==200.f,
          "manifest stable ID and culling/upload quota not applied");
    // ECS snapshots use generation-safe Entity IDs and copy geometry.
    World ecsWorld;
    const Entity bodyEntity=ecsWorld.create();
    lib::nereid::WaterBodyComponent ecsBody;
    ecsBody.center.x=12_fx;
    ecsWorld.add<lib::nereid::WaterBodyComponent>(bodyEntity,ecsBody);
    lib::nereid::WaterGeometryComponent ecsGeometry;
    ecsGeometry.points={
        {0_fx,0_fx,0_fx},{20_fx,0_fx,0_fx},{20_fx,10_fx,0_fx}};
    ecsWorld.add<lib::nereid::WaterGeometryComponent>(bodyEntity,ecsGeometry);
    const auto ecsSnapshot=world::snapshotNereidEcsWater(ecsWorld);
    check(ecsSnapshot.size()==1 &&
          ecsSnapshot[0].id==uint64_t(bodyEntity.packed())+1 &&
          ecsSnapshot[0].useGeometry &&
          ecsSnapshot[0].geometry.points.size()==3,
          "ECS snapshot must copy water and geometry with stable entity ID");
    // Stage 13: prepared CPU geometry has an exact RHI payload byte count.
    lib::nereid::WaterSurfaceMeshOptions preparedOpts;
    preparedOpts.lod=lib::nereid::WaterSurfaceLod::Far;
    const auto prepared=render::NereidSurfaceResident::prepare(
        lakeA.body,nullptr,123,preparedOpts);
    check(prepared.has_value(),"valid box must prepare its CPU surface");
    check(prepared->bufferBytes==
          prepared->cpu.vertices.size()*sizeof(render::MeshVertex)+
          prepared->cpu.indices.size()*sizeof(uint32_t),
          "prepared water upload budget must equal actual VB/IB payload");
    check(prepared->bufferBytes==render::nereidMeshBufferBytes(prepared->cpu),
          "exact byte accounting must match standalone policy");
    auto invalidStyle=style;invalidStyle.opacity=1.f;
    check(!render::NereidSurfaceResident::prepare(
        lakeA.body,nullptr,123,preparedOpts,invalidStyle).has_value(),
        "invalid water material must not stage GPU geometry");
    // Automatic LOD has no effect unless explicitly enabled.
    world::NereidSceneSurface lodSurface=lakeA;
    lodSurface.mesh.lod=lib::nereid::WaterSurfaceLod::Medium;
    lodSurface.autoLod=true;
    lodSurface.mediumDistance=30.f;
    lodSurface.farDistance=100.f;
    Vec3 nearCam{8_fx,0_fx,0_fx},midCam{60_fx,0_fx,0_fx},
         farCam{400_fx,0_fx,0_fx};
    check(world::NereidWaterScene::selectedLod(lodSurface,&nearCam)==
          lib::nereid::WaterSurfaceLod::Near,
          "near water should select near LOD");
    check(world::NereidWaterScene::selectedLod(lodSurface,&midCam)==
          lib::nereid::WaterSurfaceLod::Medium,
          "mid-distance water should select medium LOD");
    check(world::NereidWaterScene::selectedLod(lodSurface,&farCam)==
          lib::nereid::WaterSurfaceLod::Far,
          "far-distance water should select far LOD");
    Vec3 boundaryCam{31_fx,0_fx,0_fx};
    check(world::NereidWaterScene::selectedLod(
              lodSurface,&boundaryCam,lib::nereid::WaterSurfaceLod::Near)==
          lib::nereid::WaterSurfaceLod::Near,
          "LOD hysteresis must retain near detail at transition boundary");
    Vec3 crossCam{56_fx,0_fx,0_fx};
    check(world::NereidWaterScene::selectedLod(
              lodSurface,&crossCam,lib::nereid::WaterSurfaceLod::Near)==
          lib::nereid::WaterSurfaceLod::Medium,
          "LOD hysteresis must step down after positive threshold");
    Vec3 backCam{38_fx,0_fx,0_fx};
    check(world::NereidWaterScene::selectedLod(
              lodSurface,&backCam,lib::nereid::WaterSurfaceLod::Medium)==
          lib::nereid::WaterSurfaceLod::Medium,
          "LOD hysteresis must resist oscillation when moving back");
    lodSurface.autoLod=false;
    check(world::NereidWaterScene::selectedLod(lodSurface,&farCam)==
          lib::nereid::WaterSurfaceLod::Medium,
          "manual water LOD must remain unchanged");
    const auto lodConfig=world::parseNereidWaterManifest(Json::parse(R"({
      "nereidWater":{"surfaces":[{"id":22,"shape":"box",
        "surfaceY":2,"bottomY":-2,"halfExtents":[8,8],
        "autoLod":true,"mediumDistance":80,"farDistance":200}]}
    )"));
    check(lodConfig.surfaces.size()==1 && lodConfig.surfaces[0].autoLod &&
          lodConfig.surfaces[0].mediumDistance==80.f &&
          lodConfig.surfaces[0].farDistance==200.f,
          "JSON camera LOD thresholds must deserialize");
    check(rejectedManifest(R"({"nereidWater":{"surfaces":[{
      "surfaceY":2,"bottomY":1,"shape":"box","halfExtents":[1,1],
      "autoLod":true,"mediumDistance":500,"farDistance":100
    }]}})"),"reversed LOD thresholds must be rejected");
    check(rejectedManifest(R"({"nereidWater":{"surfaces":[{
      "surfaceY":2,"bottomY":1,"shape":"box","halfExtents":[1,1],
      "autoLod":true,"mediumDistance":30,"farDistance":40,
      "lodHysteresis":20
    }]}})"),"hysteresis larger than transition band must fail");
    // Stage 14: worker configuration and staging memory validation.
    const auto asyncCfg=world::parseNereidWaterManifest(Json::parse(R"({
        "nereidWater":{"asyncPrepare":true,"maxStagingBytes":4194304,
                       "surfaces":[]}
    })"));
    check(asyncCfg.asyncPrepare && asyncCfg.maxStagingBytes==4194304,
          "async water preparation manifest must parse");
    check(rejectedManifest(R"({"nereidWater":{"asyncPrepare":5,"surfaces":[]}})"),
          "async water preparation must be a boolean");
    check(rejectedManifest(R"({"nereidWater":{"maxStagingBytes":1,"surfaces":[]}})"),
          "too-small staging memory budget must be rejected");
    world::NereidWaterScene asyncWater;
    check(!asyncWater.asyncPreparation(),
          "worker mode must be opt-in for old scenes");
    asyncWater.setAsyncPreparation(true);
    asyncWater.setMaxWaterStagingBytes(1024*1024);
    check(asyncWater.asyncPreparation() &&
          asyncWater.maxWaterStagingBytes()==1024*1024,
          "async scene controls must retain explicit configuration");
    bool rejectedStaging=false;
    try{asyncWater.setMaxWaterStagingBytes(512);}
    catch(const std::invalid_argument&){rejectedStaging=true;}
    check(rejectedStaging && asyncWater.maxWaterStagingBytes()==1024*1024,
          "staging budget failure must preserve previous value");
    std::cout<<"PASS NEREID renderer bridge + multi-height planar fallback contract\n";
}
