#include "HumanMaterials.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
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
    // Broad hemoglobin tint at grazing angles plus shallow diffuse warmth.
    // This is an artist-controlled surface approximation, not screen-space SSS.
    const auto scatter=g.binary("multiply",fresnel,
                    g.binary("multiply",blood,g.constant(d.scatteringMm/6.0f)));
    const auto shallow=g.binary("multiply",blood,
        g.constant(std::clamp(d.scatteringMm/6.0f,0.0f,1.0f)*0.12f));
    const auto skinColor=g.scalarOp("clamp",g.binary("add",skinNoise,
                                    g.binary("add",scatter,shallow)));
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
    // Layered iris: fine radial fibers plus a lower-frequency crypt band.
    // The radial term is centered on the actual pupil UV rather than
    // absolute UV, preventing the texture from drifting with iris scaling.
    const auto radialUv=g.binary("subtract",uv,origin);
    const auto radialNoise=g.scalarOp("gradientNoise",
        g.binary("multiply",radialUv,
                 g.add("constant",{},vector4(108.0f,108.0f,0,0))));
    const auto cryptNoise=g.scalarOp("gradientNoise",
        g.binary("multiply",radialUv,g.constant(24.0f)));
    const auto fiberVariation=g.binary("add",g.constant(0.72f),
        g.binary("add",g.binary("multiply",fibreNoise,g.constant(0.20f)),
            g.binary("add",g.binary("multiply",radialNoise,g.constant(0.14f)),
                          g.binary("multiply",cryptNoise,g.constant(0.10f)))));
    const auto irisColor=g.binary("multiply",g.color(d.irisColor),fiberVariation);
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
    // A second longitudinal low-frequency modulation softens the repeated
    // glints on cards while retaining the dedicated renderer Hair BRDF.
    const auto longitudinal=g.scalarOp("gradientNoise",
        g.binary("multiply",uv,
                 g.add("constant",{},vector4(3.0f,17.0f,0,0))));
    const auto glint=g.binary("multiply",facing,
        g.binary("add",g.constant(0.10f),
            g.binary("multiply",longitudinal,g.constant(0.16f))));
    const auto highlight=g.binary("add",variation,glint);
    const auto melaninDarkening=g.constant(1.0f-0.38f*d.melanin);
    const auto color=g.scalarOp("clamp",g.binary("multiply",
             g.binary("multiply",g.color(d.color),melaninDarkening),highlight));
    return g.finish(color,g.constant(std::clamp(d.roughness,0.05f,1.0f)));
}
// Mobile/medium-specific graphs share the full ShaderGraph compiler, while
// reducing nodes rather than hiding costly effects behind a runtime branch.
std::shared_ptr<const render::graph::Program> compactSkinGraph(
        const HumanSkin& d,HumanMaterialQuality quality) {
    Nodes g;
    std::array<float,3> pigment{};
    for(int i=0;i<3;++i)
        pigment[std::size_t(i)]=std::clamp(d.melaninColor[std::size_t(i)]*
                    (1.04f-d.melanin*0.22f),0.02f,1.0f);
    const auto base=g.binary("multiply",g.add("input"),g.color(pigment));
    const auto rough=g.constant(std::clamp(
        d.roughness*(1.0f-0.55f*d.oiliness-0.22f*d.wetness),0.04f,1.0f));
    if(quality==HumanMaterialQuality::Low)return g.finish(base,rough);
    const auto fresnel=g.ternary("fresnel",g.add("normal"),
                                g.add("viewDirection"),g.constant(2.4f));
    const auto blood=g.color({0.17f*d.hemoglobin,
                             0.035f*d.hemoglobin,0.023f*d.hemoglobin});
    const auto scatter=g.binary("multiply",fresnel,
        g.binary("multiply",blood,g.constant(d.scatteringMm/8.0f)));
    return g.finish(g.scalarOp("clamp",g.binary("add",base,scatter)),rough);
}
std::shared_ptr<const render::graph::Program> compactIrisGraph(
        const HumanEyes& d,HumanMaterialQuality quality) {
    Nodes g;
    const auto uv=g.add("uv");
    const auto center=g.add("constant",{},vector4(0.5f,0.5f,0,0));
    const auto radius=g.scalarOp("length",g.binary("subtract",uv,center));
    const auto irisRadius=std::max(0.10f,d.irisRadius*0.49f);
    const auto pupilRadius=std::max(0.02f,d.irisRadius*d.pupil*0.5f);
    const auto irisMask=g.scalarOp("oneMinus",g.ternary("smoothstep",
        g.constant(irisRadius-0.012f),g.constant(irisRadius+0.012f),radius));
    const auto pupilMask=g.scalarOp("oneMinus",g.ternary("smoothstep",
        g.constant(pupilRadius-0.010f),g.constant(pupilRadius+0.010f),radius));
    const auto sclera=g.color({0.92f,0.89f-d.scleraRedness*0.12f,
                              0.86f-d.scleraRedness*0.18f});
    auto iris=g.color(d.irisColor);
    if(quality==HumanMaterialQuality::Medium) {
        const auto grain=g.scalarOp("gradientNoise",
            g.binary("multiply",uv,g.constant(43.0f)));
        iris=g.binary("multiply",iris,
            g.binary("add",g.constant(0.86f),
                         g.binary("multiply",grain,g.constant(0.20f))));
    }
    const auto colored=g.ternary("mix",sclera,iris,irisMask);
    const auto eye=g.ternary("mix",colored,
                               g.color({0.008f,0.009f,0.012f}),pupilMask);
    return g.finish(eye,g.constant(0.19f));
}
std::shared_ptr<const render::graph::Program> compactHairGraph(
        const HumanGroom& d,HumanMaterialQuality quality) {
    Nodes g;
    const auto darkness=g.constant(1.0f-0.38f*d.melanin);
    auto color=g.binary("multiply",g.color(d.color),darkness);
    if(quality==HumanMaterialQuality::Medium) {
        const auto grain=g.scalarOp("gradientNoise",
            g.binary("multiply",g.add("uv"),
                g.add("constant",{},vector4(65.0f,6.0f,0,0))));
        color=g.binary("multiply",color,g.binary("add",
            g.constant(0.75f),g.binary("multiply",grain,g.constant(0.30f))));
    }
    return g.finish(color,g.constant(std::clamp(d.roughness,0.05f,1.0f)));
}
void enforceGraphBudget(const render::Material& material,
                        HumanMaterialQuality quality) {
    if(!material.shaderGraph)return;
    const auto budget=humanMaterialBudget(quality);
    if(material.shaderGraph->nodes>budget.maxNodes ||
       material.shaderGraph->samples>budget.maxSamples)
        throw std::runtime_error("VIVID material graph exceeded quality budget");
}
} // namespace

render::Material humanMaterial(HumanSurface surface,const HumanDNA& dna,
                               HumanMaterialQuality quality) {
    dna.validate();
    render::Material m;
    switch(surface) {
    case HumanSurface::Skin:
        m=basic("VIVID skin",dna.skin.melaninColor,dna.skin.roughness);
        m.shaderGraph=quality==HumanMaterialQuality::High
            ?skinGraph(dna.skin):compactSkinGraph(dna.skin,quality);
        m.shading=render::ShadingModel::Skin;
        m.humanProfile={dna.skin.scatteringMm/6.0f,dna.skin.wetness};
        m.roughness=std::clamp(dna.skin.roughness-dna.skin.oiliness*0.23f,0.04f,1.0f);
        break;
    case HumanSurface::Iris:
        m=basic("VIVID layered iris",dna.eyes.irisColor,0.19f);
        m.shaderGraph=quality==HumanMaterialQuality::High
            ?irisGraph(dna.eyes):compactIrisGraph(dna.eyes,quality);
        m.shading=render::ShadingModel::Eye;
        m.humanProfile={dna.eyes.corneaIor,dna.eyes.tearFilm};
        break;
    case HumanSurface::Sclera:
        // The sclera is not pure white. Small blood-vessel absorption is
        // driven by authored DNA and must affect the actual PBR material.
        m=basic("VIVID sclera",{
            0.92f,
            std::clamp(0.89f-0.12f*dna.eyes.scleraRedness,0.0f,1.0f),
            std::clamp(0.86f-0.18f*dna.eyes.scleraRedness,0.0f,1.0f)},0.24f);
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
        m.shaderGraph=quality==HumanMaterialQuality::High
            ?hairGraph(dna.groom):compactHairGraph(dna.groom,quality);
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
    if(quality==HumanMaterialQuality::Low &&
       (surface==HumanSurface::Skin || surface==HumanSurface::Iris ||
        surface==HumanSurface::Cornea || surface==HumanSurface::Hair ||
        surface==HumanSurface::Eyelash))
        m.shading=render::ShadingModel::Pbr;
    enforceGraphBudget(m,quality);
    return m;
}
render::Material humanMaterial(HumanSurface surface,const HumanDNA& dna) {
    return humanMaterial(surface,dna,HumanMaterialQuality::High);
}
render::Material humanMaterialFromSource(HumanSurface surface,
                                        const HumanDNA& dna,
                                        const render::Material& source,
                                        HumanMaterialQuality quality) {
    auto dst=humanMaterial(surface,dna,quality);
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
    if(surface==HumanSurface::Hair || surface==HumanSurface::Eyelash) {
        dst.doubleSided=true;
        dst.alpha=source.alpha;   // preserve artist alpha-masked hair cards
        dst.alphaCutoff=source.alphaCutoff;
        dst.transparent=source.transparent;
        dst.baseColor.w=source.baseColor.w;
    }
    // Cornea always retains the DNA tear-film transparent material; imported
    // alpha modes do not turn the corneal shell into an opaque depth writer.
    return dst;
}

render::Material humanMaterialFromSource(HumanSurface surface,
                                        const HumanDNA& dna,
                                        const render::Material& source) {
    return humanMaterialFromSource(surface,dna,source,
                                   HumanMaterialQuality::High);
}
HumanSurfaceMaterials humanMaterials(const HumanDNA& dna,
                                     HumanMaterialQuality quality) {
    return {humanMaterial(HumanSurface::Skin,dna,quality),
            humanMaterial(HumanSurface::Iris,dna,quality),
            humanMaterial(HumanSurface::Sclera,dna,quality),
            humanMaterial(HumanSurface::Cornea,dna,quality),
            humanMaterial(HumanSurface::Hair,dna,quality),
            humanMaterial(HumanSurface::Eyelash,dna,quality),
            humanMaterial(HumanSurface::Teeth,dna,quality)};
}
HumanSurfaceMaterials humanMaterials(const HumanDNA& dna) {
    return humanMaterials(dna,HumanMaterialQuality::High);
}
} // namespace aiverse::vivid
