// Authoring host for native Arbor/Grass APIs. No scene-specific morphology.
#include "engine/render/TerraWeaveModelAdapter.h"
#include "engine/render/TerraWeaveGrassRender.h"
#include "tools/terrain/ExportModel.h"
#include <filesystem>
#include <fstream>
#include <iostream>
namespace fs=std::filesystem;
using aiverse::Json;
using namespace aiverse::terraweave;
using namespace aiverse::terrain_export;
int main(int argc,char** argv){try{
    fs::path out,materials,groundFile;uint64_t seed=42;
    for(int i=1;i<argc;++i){const std::string k=argv[i];if(i+1==argc)throw std::runtime_error("missing value");const std::string v=argv[++i];if(k=="--out")out=v;else if(k=="--materials")materials=v;else if(k=="--ground")groundFile=v;else if(k=="--seed")seed=std::stoull(v);else throw std::runtime_error("unknown option: "+k);}
    if(out.empty() || materials.empty())throw std::runtime_error("usage: aiverse_terraweave_vegetation --out NEW_DIR --materials VEGETATION_TEXTURES [--ground HEIGHT_GRID_JSON --seed N]");
    if(fs::exists(out))throw std::runtime_error("output exists; refusing overwrite");
    for(auto name:{"spruce_bark","spruce_leaves","pine_bark","pine_leaves"})for(auto suffix:{"basecolor","normal","orm","ao"})if(!fs::is_regular_file(materials/(std::string(name)+"_"+suffix+".png")))throw std::runtime_error("missing PBR map");
    fs::create_directories(out/"textures");
    for(auto name:{"spruce_bark","spruce_leaves","pine_bark","pine_leaves"})for(auto suffix:{"basecolor","normal","orm","ao"}){std::string f=std::string(name)+"_"+suffix+".png";fs::copy_file(materials/f,out/"textures"/f);}
    auto catalog=Json::object(),trees=Json::array(),patches=Json::array();
    for(uint32_t i=0;i<12;++i){
        const bool pine=i%3==0;TreeProfile p;p.speciesId=pine?2:1;p.shape=CrownShape::Conifer;
        p.minHeightCm=pine?1100:850;p.maxHeightCm=pine?1700:1500;p.trunkRadiusCm=pine?28:24;
        p.crownStartPermille=pine?430:90;p.primaryBranches=pine?105:140;
        p.branchLengthPermille=pine?800:530;p.branchRisePermille=pine?180:40;p.leafRadiusCm=65;
        ArborMeshSettings mesh;mesh.radialSegments=8;mesh.doubleSidedLeaves=false;mesh.naturalConifer=true;mesh.sprayPairs=9;
        auto model=makeArborModel(p,detail::mix64(seed^i),mesh);
        model->materials[0].baseColor={.68f,.62f,.53f,1};model->materials[0].roughness=1;
        model->materials[1].baseColor={.90f,1.f,.85f,1};model->materials[1].roughness=1;model->materials[1].alpha=aiverse::render::AlphaMode::Mask;model->materials[1].alphaCutoff=.38f;
        const std::string name="tree-"+std::to_string(i),stem=pine?"pine":"spruce";save(out,name,*model,{stem+"_bark",stem+"_leaves"});
        auto e=Json::object(),tri=Json::array();e.set("file",Json::string(name+".gltf"));e.set("kind",Json::string(stem));
        for(const auto& m:model->meshes){uint32_t n=0;for(const auto& pr:m.primitives)n+=pr.indexCount/3;tri.push(Json::number(int64_t(n)));}e.set("triangles",tri);trees.push(e);
    }
    if(!groundFile.empty()){
        const auto doc=Json::parseFile(groundFile.string());const int64_t ox=doc.getInt("originXcm"),oz=doc.getInt("originZcm"),step=doc.getInt("stepCm");const int64_t nx=doc.getInt("columns"),nz=doc.getInt("rows");
        if(nx<2||nz<2||nx>2048||nz>2048||step<1||step>1000||doc["heightCm"].size()!=size_t(nx*nz)||doc["coverPermille"].size()!=size_t(nx*nz)||doc["patches"].size()>256)throw std::runtime_error("invalid bounded height/cover grid");
        if(ox< -kWorldLimitCm||ox>kWorldLimitCm||oz< -kWorldLimitCm||oz>kWorldLimitCm||ox+nx*step>kWorldLimitCm||oz+nz*step>kWorldLimitCm)throw std::runtime_error("grid extent");
        if(doc.has("waterHeightCm")&&doc["waterHeightCm"].size()!=size_t(nx*nz))throw std::runtime_error("water height grid size");
        for(const auto& v:doc["heightCm"].items())if(!v.isNumber()||v.integer()< -1000000||v.integer()>1000000)throw std::runtime_error("height range");
        for(const auto& v:doc["coverPermille"].items())if(!v.isNumber()||v.integer()<0||v.integer()>1000)throw std::runtime_error("cover range");
        auto sample=[&](const char* field,int64_t x,int64_t z){const double gx=double(x-ox)/step,gz=double(z-oz)/step;const auto ix=int64_t(std::floor(gx)),iz=int64_t(std::floor(gz));if(ix<0||iz<0||ix>=nx-1||iz>=nz-1)throw std::runtime_error("patch outside grid");const double tx=gx-ix,tz=gz-iz;const auto& a=doc[field];return int32_t(std::lround((a[size_t(iz*nx+ix)].integer()*(1-tx)+a[size_t(iz*nx+ix+1)].integer()*tx)*(1-tz)+(a[size_t((iz+1)*nx+ix)].integer()*(1-tx)+a[size_t((iz+1)*nx+ix+1)].integer()*tx)*tz));};
        uint64_t key=1;
        for(const auto& entry:doc["patches"].items()){
            GrassPatch p;p.key=key++;p.seed=seed;p.bounds={entry.getInt("minX"),entry.getInt("minZ"),entry.getInt("maxX"),entry.getInt("maxZ")};p.maximumBlades=uint32_t(std::clamp<int64_t>(entry.getInt("blades",2048),1,8192));
            GrassRenderQuality q;q.maxBladesPerPatch=p.maximumBlades;q.crossedBlades=false;q.bladeSegments=3;q.bladeHeightCm=38;q.bladeHalfWidthCm=2;q.bladesPerTuft=16;q.tuftRadiusCm=15;
            const auto ground=[&](int64_t x,int64_t z){return sample("heightCm",x,z);};
            const auto allows=[&](int64_t x,int64_t z){if(doc.has("waterHeightCm")&&sample("heightCm",x,z)-sample("waterHeightCm",x,z)<270)return false;return int64_t(detail::mix64(uint64_t(x)^detail::mix64(uint64_t(z)))%1000)<sample("coverPermille",x,z);};
            auto model=makeGrassPatchModel(p,ground,q,allows);
            if(!model)continue;
            for(uint32_t divisor:{3u,8u}){
                q.maxBladesPerPatch=std::max(1u,p.maximumBlades/divisor);auto lod=makeGrassPatchModel(p,ground,q,allows);if(!lod)continue;
                const uint32_t vo=uint32_t(model->vertices.size()),io=uint32_t(model->indices.size());
                model->vertices.insert(model->vertices.end(),lod->vertices.begin(),lod->vertices.end());for(auto index:lod->indices)model->indices.push_back(index+vo);
                auto mesh=lod->meshes[0];for(auto& pr:mesh.primitives){pr.firstVertex+=vo;pr.firstIndex+=io;}
                model->meshes[0].lods.push_back({int(model->meshes.size()),divisor==3?220.f:90.f});model->meshes.push_back(std::move(mesh));
            }
            model->finalize();
            model->materials[0].baseColor={.16f,.26f,.06f,1};const std::string name="grass-"+std::to_string(p.key);save(out,name,*model);
            auto e=Json::object();e.set("file",Json::string(name+".gltf"));e.set("xCm",Json::number(p.bounds.minX));e.set("zCm",Json::number(p.bounds.minZ));patches.push(e);
        }
    }
    catalog.set("version",Json::number(int64_t(1)));catalog.set("trees",trees);catalog.set("grass",patches);write(out/"catalog.json",catalog.dump());std::cout<<"Native Arbor prototypes: "<<trees.size()<<"; native grass patches: "<<patches.size()<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
