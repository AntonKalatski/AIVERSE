#include "HumanMaterials.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::vivid {
namespace {
Json vector4(float a,float b,float c,float d=1.0f) {
    auto x=Json::array();
    for(float v:{a,b,c,d})x.push(Json::number(double(v),6));
    return x;
}
struct Nodes {
    Json graph=Json::object(), list=Json::array();
    int serial=0;
    std::string add(const char* op,
                    std::initializer_list<std::string> inputs={},
                    Json value={}) {
        const std::string name="h"+std::to_string(serial++);
        auto item=Json::object();
        item.set("id",Json::string(name));
        item.set("op",Json::string(op));
        if(inputs.size()) {
            auto source=Json::array();
            for(const auto& input:inputs)source.push(Json::string(input));
            item.set("inputs",std::move(source));
        }
        if(!value.isNull())item.set("value",std::move(value));
        list.push(std::move(item));
        return name;
    }
    std::string constant(float scalar) {
        return add("constant",{},Json::number(double(scalar),6));
    }
    std::string color(const std::array<float,3>& rgb) {
        return add("constant",{},vector4(rgb[0],rgb[1],rgb[2]));
    }
    std::string scalarOp(const char* op,std::string input) {
        return add(op,{std::move(input)});
    }
    std::string binary(const char* op,std::string a,std::string b) {
        return add(op,{std::move(a),std::move(b)});
    }
    std::string ternary(const char* op,std::string a,std::string b,std::string c) {
        return add(op,{std::move(a),std::move(b),std::move(c)});
    }
    std::shared_ptr<const render::graph::Program> finish(
        const std::string& output, const std::string& roughness,
        bool high=false) {
        graph.set("version",Json::number(std::int64_t(1)));
        graph.set("domain",Json::string("material"));
        graph.set("nodes",std::move(list));
        if(high)graph.set("minQuality",Json::string("high"));
        auto outs=Json::object();
        outs.set("color",Json::string(output));
        outs.set("roughness",Json::string(roughness));
        graph.set("outputs",std::move(outs));
        return std::make_shared<const render::graph::Program>(
            render::graph::compile(graph,"material"));
    }
};
render::Material basic(const char* name,std::array<float,3> color,float rough) {
    render::Material m;
    m.name=name;
    m.baseColor={color[0],color[1],color[2],1.0f};
    m.roughness=rough;
    m.metallic=0;
    m.vertexColors=false;
    return m;
}
std::shared_ptr<const render::graph::Program> skinGraph(const HumanSkin& d) {
    Nodes g;
    const auto input=g.add("input");
    const auto uv=g.add("uv");
    const auto normal=g.add("normal");
    const auto view=g.add("viewDirection");
    std::array<float,3> pigment;
    for(int i=0;i<3;++i)
        pigment[std::size_t(i)]=std::clamp(d.melaninColor[std::size_t(i)]*
                    (1.04f-d.melanin*0.22f),0.02f,1.0f);
    const auto base=g.binary("multiply",input,g.color(pigment));
    const auto poreFreq=g.constant(42.0f+85.0f*d.poreScale);
    const auto poreUv=g.binary("multiply",uv,poreFreq);
    const auto noise=g.scalarOp("gradientNoise",poreUv);
    const auto poreOffset=g.binary("subtract",noise,g.constant(0.5f));
    const auto pore=g.binary("multiply",poreOffset,g.constant(0.06f*d.poreScale));
    const auto wrinkleUv=g.binary("multiply",uv,
                                  g.constant(20.0f+60.0f*d.wrinkleStrength));
    const auto wrinkleNoise=g.scalarOp("gradientNoise",wrinkleUv);
    const auto wrinkle=g.binary("multiply",
                     g.binary("subtract",wrinkleNoise,g.constant(0.5f)),
                     g.constant(0.085f*d.wrinkleStrength));
    const auto skinNoise=g.binary("multiply",base,
             g.binary("add",g.constant(1.0f),g.binary("add",pore,wrinkle)));
    const auto fresnel=g.ternary("fresnel",normal,view,g.constant(2.4f));
    const auto blood=g.color({0.17f*d.hemoglobin,
                             0.035f*d.hemoglobin,0.023f*d.hemoglobin});
    const auto scatter=g.binary("multiply",fresnel,
                    g.binary("multiply",blood,g.constant(d.scatteringMm/6.0f)));
    const auto skinColor=g.scalarOp("clamp",g.binary("add",skinNoise,scatter));
    const auto finalRoughness=g.constant(std::clamp(
            d.roughness*(1.0f-0.55f*d.oiliness-0.22f*d.wetness),0.04f,1.0f));
    return g.finish(skinColor,finalRoughness);
}
std::shared_ptr<const render::graph::Program> irisGraph(const HumanEyes& d) {
    Nodes g;
    const auto uv=g.add("uv");
    const auto origin=g.add("constant",{},vector4(0.5f,0.5f,0,0));
    const auto radius=g.scalarOp("length",g.binary("subtract",uv,origin));
    const auto pupilRadius=std::max(0.02f,d.irisRadius*d.pupil*0.5f);
    const auto irisRadius=std::max(0.10f,d.irisRadius*0.49f);
    const auto irisMask=g.scalarOp("oneMinus",
        g.ternary("smoothstep",g.constant(irisRadius-0.012f),
                  g.constant(irisRadius+0.012f),radius));
    const auto pupilMask=g.scalarOp("oneMinus",
        g.ternary("smoothstep",g.constant(pupilRadius-0.01f),
                  g.constant(pupilRadius+0.01f),radius));
    const auto pupil=g.color({0.008f,0.009f,0.012f});
    const auto sclera=g.color({0.92f,0.89f-d.scleraRedness*0.12f,
                              0.86f-d.scleraRedness*0.18f});
    const auto fibreNoise=g.scalarOp("gradientNoise",
                       g.binary("multiply",uv,g.constant(85.0f)));
    const auto irisColor=g.binary("multiply",g.color(d.irisColor),
                       g.binary("add",g.constant(0.82f),
                            g.binary("multiply",fibreNoise,g.constant(0.28f))));
    const auto iris=g.ternary("mix",sclera,irisColor,irisMask);
    const auto eye=g.ternary("mix",iris,pupil,pupilMask);
    const auto limbalOuter=g.scalarOp("oneMinus",
        g.ternary("smoothstep",g.constant(irisRadius-0.055f),
                  g.constant(irisRadius-0.015f),radius));
    const auto limbal=g.binary("subtract",irisMask,limbalOuter);
    const auto darkening=g.binary("multiply",limbal,
                          g.constant(0.25f*d.limbus));
    const auto result=g.scalarOp("clamp",
                   g.binary("multiply",eye,
                       g.binary("subtract",g.constant(1.0f),darkening)));
    return g.finish(result,g.constant(0.19f),true);
}
std::shared_ptr<const render::graph::Program> hairGraph(const HumanGroom& d) {
    Nodes g;
    const auto uv=g.add("uv");
    const auto strands=g.scalarOp("gradientNoise",
        g.binary("multiply",uv,g.add("constant",{},
                                   vector4(65.0f,6.0f,0,0))));
    const auto variation=g.binary("add",g.constant(0.62f),
        g.binary("multiply",strands,g.constant(0.45f)));
    const auto facing=g.ternary("fresnel",g.add("normal"),
                        g.add("viewDirection"),g.constant(4.0f));
    const auto glint=g.binary("multiply",facing,g.constant(0.2f));
    const auto highlight=g.binary("add",variation,glint);
    const auto melaninDarkening=g.constant(1.0f-0.38f*d.melanin);
    const auto color=g.scalarOp("clamp",g.binary("multiply",
             g.binary("multiply",g.color(d.color),melaninDarkening),highlight));
    return g.finish(color,g.constant(std::clamp(d.roughness,0.05f,1.0f)));
}
} // namespace

render::Material humanMaterial(HumanSurface surface,const HumanDNA& dna) {
    dna.validate();
    render::Material m;
    switch(surface) {
    case HumanSurface::Skin:
        m=basic("VIVID skin",dna.skin.melaninColor,dna.skin.roughness);
        m.shaderGraph=skinGraph(dna.skin);
        m.shading=render::ShadingModel::Skin;
        m.humanProfile={dna.skin.scatteringMm/6.0f,dna.skin.wetness};
        m.roughness=std::clamp(dna.skin.roughness-dna.skin.oiliness*0.23f,0.04f,1.0f);
        break;
    case HumanSurface::Iris:
        m=basic("VIVID layered iris",dna.eyes.irisColor,0.19f);
        m.shaderGraph=irisGraph(dna.eyes);
        m.shading=render::ShadingModel::Eye;
        m.humanProfile={dna.eyes.corneaIor,dna.eyes.tearFilm};
        break;
    case HumanSurface::Sclera:
        m=basic("VIVID sclera",{0.92f,0.89f,0.86f},0.24f);
        break;
    case HumanSurface::Cornea:
        m=basic("VIVID cornea",{1.0f,1.0f,1.0f},0.04f);
        m.alpha=render::AlphaMode::Blend;
        m.baseColor.w=std::clamp(0.07f+0.09f*dna.eyes.tearFilm,0.01f,0.22f);
        m.castShadow=false;
        m.doubleSided=true;
        m.shading=render::ShadingModel::Eye;
        m.humanProfile={dna.eyes.corneaIor,dna.eyes.tearFilm};
        break;
    case HumanSurface::Hair:
        m=basic("VIVID groom",dna.groom.color,dna.groom.roughness);
        m.shaderGraph=hairGraph(dna.groom);
        m.shading=render::ShadingModel::Hair;
        m.humanProfile={0.65f+dna.groom.clump*0.30f,dna.groom.roughness};
        m.doubleSided=true;
        break;
    case HumanSurface::Eyelash:
        m=basic("VIVID eyelash",dna.groom.color,0.52f);
        m.doubleSided=true;
        m.shading=render::ShadingModel::Hair;
        m.humanProfile={0.35f,0.55f};
        break;
    case HumanSurface::Teeth:
        m=basic("VIVID teeth",{0.88f,0.83f,0.72f},0.25f);
        break;
    }
    return m;
}
render::Material humanMaterialFromSource(HumanSurface surface,
                                        const HumanDNA& dna,
                                        const render::Material& source) {
    auto dst=humanMaterial(surface,dna);
    // The imported texture array remains in the owning render::Model; its
    // integer indices are still valid after remapping the material factors.
    dst.baseTex=source.baseTex;
    dst.mrTex=source.mrTex;
    dst.normalTex=source.normalTex;
    dst.occlusionTex=source.occlusionTex;
    dst.emissiveTex=source.emissiveTex;
    dst.teamTex=source.teamTex;
    dst.normalScale=source.normalScale;
    dst.occlusionStrength=source.occlusionStrength;
    dst.uvScale=source.uvScale;
    dst.uvOffset=source.uvOffset;
    dst.uvRotation=source.uvRotation;
    dst.emissive=source.emissive;
    dst.emissiveStrength=source.emissiveStrength;
    dst.name=source.name;
    if(surface==HumanSurface::Hair || surface==HumanSurface::Eyelash)
        dst.doubleSided=true;
    // Alpha surface semantics come from VIVID (transparent cornea, solid
    // flesh and sclera); never allow an imported cutout to turn the cornea
    // into an opaque depth-writing model.
    return dst;
}

HumanSurfaceMaterials humanMaterials(const HumanDNA& dna) {
    return {humanMaterial(HumanSurface::Skin,dna),
            humanMaterial(HumanSurface::Iris,dna),
            humanMaterial(HumanSurface::Sclera,dna),
            humanMaterial(HumanSurface::Cornea,dna),
            humanMaterial(HumanSurface::Hair,dna),
            humanMaterial(HumanSurface::Eyelash,dna),
            humanMaterial(HumanSurface::Teeth,dna)};
}
} // namespace aiverse::vivid
