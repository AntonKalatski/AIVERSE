#include "engine/character/vivid/HumanPreview.h"
#include "engine/render/GpuMaterialState.h"
#include "engine/render/ModelShaders.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace aiverse;
using namespace aiverse::vivid;
namespace {
void expect(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
bool finite(render::float3 v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
void test() {
    for(auto name:{"neutral","warm","cool","stylized"}) {
        auto dna=HumanDNA::preset(name);
        auto mat=humanMaterials(dna);
        expect(mat.skin.shaderGraph && mat.iris.shaderGraph &&
               mat.hair.shaderGraph,"PBR human shader graphs missing");
        expect(mat.skin.shading==render::ShadingModel::Skin &&
               mat.iris.shading==render::ShadingModel::Eye &&
               mat.cornea.shading==render::ShadingModel::Eye &&
               mat.hair.shading==render::ShadingModel::Hair,
               "human materials did not select dedicated PBR modes");
        expect(std::string(render::shaders::kModelFS).find("human skin")!=std::string::npos &&
               std::string(render::shaders::kModelMSL).find("human BRDF lobes")!=std::string::npos,
               "GLSL/Metal human BRDF source missing");
        render::Model modelA,modelB;
        const auto original=mat.skin;
        auto adjusted=original;
        adjusted.humanProfile.x*=0.2f;
        expect(!render::equivalentGpuMaterial(modelA,original,modelB,adjusted) &&
               render::gpuMaterialStateHash(modelA,original)!=
               render::gpuMaterialStateHash(modelB,adjusted),
               "GPU batching merged human materials with different subsurface profiles");
        for(const auto& material:{mat.skin,mat.iris,mat.hair}) {
            expect(material.shaderGraph->domain=="material" &&
                   !material.shaderGraph->glsl.empty() &&
                   !material.shaderGraph->msl.empty() &&
                   material.shaderGraph->outputs.count("roughness") &&
                   material.shaderGraph->outputs.count("color"),
                   "GLSL/Metal material surface graph invalid");
        }
        expect(mat.cornea.alpha==render::AlphaMode::Blend &&
               !mat.cornea.castShadow,"cornea incorrectly opaque / shadow casting");
        auto human=generateHumanPreview(dna,GroomQuality::Medium);
        expect(human.head&&human.hair&&human.rig.valid(),
               "reference human head or rig missing");
        expect(human.head->findNode("head")>=0 &&
               human.head->findNode("jaw")>=0 &&
               human.head->findNode("eye.L")>=0 &&
               human.head->findNode("eye.R")>=0,
               "facial joint hierarchy incomplete");
        expect(human.rig.hasFacialJoints(),"reference head has no jaw/eye joints");
        expect(human.facialShapes.targets()==5 &&
               human.facialBinding.routes()==5 &&
               human.facialShapes.vertices()==human.head->vertices.size(),
               "facial morph routes incomplete");
        expect(human.head->materials.size()>10 &&
               human.head->vertices.size()>2500,
               "procedural skin/eye/teeth surfaces missing");
        std::size_t irisVertices=0;
        for(const auto& mesh:human.head->meshes) {
            if(mesh.name!="iris.L"&&mesh.name!="iris.R")continue;
            for(const auto& p:mesh.primitives)
                for(std::uint32_t k=0;k<p.vertexCount;++k) {
                    const auto& uv=human.head->vertices[std::size_t(p.firstVertex+k)].uv;
                    expect(uv.x>=0&&uv.x<=1&&uv.y>=0&&uv.y<=1,
                           "iris radial shader UV outside bounds");
                    ++irisVertices;
                }
        }
        expect(irisVertices>=128,"radial iris surfaces missing");
        expect(human.groom.guides().size()>8 &&
               human.groom.vertexCount()==human.hair->vertices.size(),
               "hair geometry and guides mismatch");
        expect(human.hair->meshes[0].deformable,
               "dynamic groom incorrectly eligible for geometry pooling");
        expect(human.groom.step(0.016f,{0.5f,0.1f,0.2f}),
               "groom timestep failed");
        expect(!human.groom.step(-1.0f) &&
               !human.groom.step(1.5f),"invalid groom dt accepted");
        auto changed=human.hair->vertices;
        expect(human.groom.updateCardVertices(changed),
               "groom render stream did not update");
        for(const auto& v:changed)expect(finite(v.pos)&&finite(v.nrm),
                                         "groom generated nonfinite render vertex");
        const auto stats=human.groom.stats();
        expect(stats.triangles==human.hair->indices.size()/3,
               "groom card triangle stats mismatch");
    }
    auto high=generateHumanPreview(HumanDNA{},GroomQuality::High);
    auto low=generateHumanPreview(HumanDNA{},GroomQuality::Low);
    expect(low.groom.triangleCount()<high.groom.triangleCount(),
           "hair LOD did not reduce geometry");
    auto weights=std::vector<float>(high.facialShapes.targets(),0.0f);
    FaceController controller(0);
    expect(controller.set(Layer::Emotion,Channel::MouthSmileLeft,0.8f),
           "facial controller setup failed");
    controller.snap();
    expect(high.facialBinding.evaluate(controller.frame(),weights),
           "human expression mapping failed");
    expect(weights[3]>0.7f,"human smile not mapped to corrective shape");
}
}
int main(){try{test();std::cout<<"VIVID human appearance/groom PASS\n";}
catch(const std::exception& e){std::cerr<<"VIVID appearance FAIL: "<<e.what()<<"\n";return 1;}return 0;}
