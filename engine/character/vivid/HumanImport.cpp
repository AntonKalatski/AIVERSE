#include "HumanImport.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace aiverse::vivid {
namespace {
void require(bool condition,const char* message){
    if(!condition)throw std::invalid_argument(message);
}
void relativePath(const std::string& path,bool required) {
    if(path.empty()) {
        if(required)throw std::invalid_argument("VIVID import: empty model path");
        return;
    }
    if(path.size()>256||path.front()=='/'||path.front()=='~'||
       path.find("..")!=std::string::npos ||
       path.find('\\')!=std::string::npos ||
       path.find(':')!=std::string::npos ||
       path.find('\n')!=std::string::npos ||
       path.find('\r')!=std::string::npos)
        throw std::invalid_argument("VIVID import: unsafe/non-relative asset path");
}
const char* surfaceName(HumanSurface s) {
    switch(s) {
    case HumanSurface::Skin:return "skin";
    case HumanSurface::Iris:return "iris";
    case HumanSurface::Sclera:return "sclera";
    case HumanSurface::Cornea:return "cornea";
    case HumanSurface::Hair:return "hair";
    case HumanSurface::Eyelash:return "eyelash";
    case HumanSurface::Teeth:return "teeth";
    }
    return "skin";
}
HumanSurface surfaceFromName(std::string_view name){
    for(auto s:{HumanSurface::Skin,HumanSurface::Iris,
                HumanSurface::Sclera,HumanSurface::Cornea,
                HumanSurface::Hair,HumanSurface::Eyelash,HumanSurface::Teeth})
        if(name==surfaceName(s))return s;
    throw std::invalid_argument("VIVID import: unknown human surface category");
}
std::uint32_t integer(const Json& j) {
    if(!j.isNumber()||j.numberLiteral().find_first_of(".eE")!=std::string::npos)
        throw std::invalid_argument("VIVID import: expected integer");
    const auto n=j.integer(-1);
    if(n<0 || n>std::numeric_limits<std::uint32_t>::max())
        throw std::invalid_argument("VIVID import: index outside u32");
    return std::uint32_t(n);
}
MorphVec3 readVec(const Json& j) {
    require(j.isArray()&&j.size()==3,"VIVID morphs: expected vec3");
    MorphVec3 out{};
    for(int i=0;i<3;++i) {
        require(j[std::size_t(i)].isNumber(),"VIVID morphs: expected numeric component");
        const double value=j[std::size_t(i)].toDouble(
                  std::numeric_limits<double>::quiet_NaN());
        require(std::isfinite(value)&&std::abs(value)<=10.0,
                "VIVID morphs: malformed/nonfinite delta");
        out[std::size_t(i)]=float(value);
    }
    return out;
}
void allowed(const Json& obj,std::initializer_list<const char*> keys){
    require(obj.isObject(),"VIVID import: expected object");
    for(const auto& [name,v]:obj.members()){
        (void)v;
        const auto it=std::find_if(keys.begin(),keys.end(),
                        [&](const char* key){return key==name;});
        require(it!=keys.end(),"VIVID import: unknown field");
    }
}
std::string file(const render::FileReader& read,
                 const std::string& path,std::size_t limit) {
    std::string source;
    if(!read(path,source))throw std::runtime_error("VIVID import: cannot read "+path);
    if(source.size()>limit)throw std::runtime_error("VIVID import: file size budget exceeded");
    return source;
}
} // namespace

void HumanImportManifest::validate() const {
    require(version==1,"VIVID import: manifest version unsupported");
    relativePath(modelPath,true);
    relativePath(dnaPath,false);
    relativePath(morphPath,false);
    require(routes.size()<=4096 && materials.size()<=512,
            "VIVID import: route/material budget exceeded");
    for(const auto& route:routes) {
        require(int(route.channel)>=0 && int(route.channel)<int(Channel::Count) &&
                !route.targetName.empty() && route.targetName.size()<=128 &&
                std::isfinite(route.gain)&&route.gain>=0&&route.gain<=4,
                "VIVID import: invalid shape mapping");
    }
    for(const auto& [name,surface]:materials) {
        require(!name.empty()&&name.size()<=128 &&
                int(surface)>=0&&int(surface)<=int(HumanSurface::Teeth),
                "VIVID import: invalid human material mapping");
    }
}
HumanImportManifest HumanImportManifest::fromJson(const Json& src) {
    allowed(src,{"version","model","dna","morphs","routes","materials"});
    HumanImportManifest m;
    require(src.has("version"),"VIVID import: missing manifest version");
    m.version=integer(src["version"]);
    require(src["model"].isString(),"VIVID import: model path string required");
    m.modelPath=src["model"].str();
    if(src.has("dna")) {
        require(src["dna"].isString(),"VIVID import: invalid DNA path");
        m.dnaPath=src["dna"].str();
    }
    if(src.has("morphs")) {
        require(src["morphs"].isString(),"VIVID import: invalid morph path");
        m.morphPath=src["morphs"].str();
    }
    if(src.has("routes")) {
        const auto& routes=src["routes"];
        require(routes.isArray()&&routes.size()<=4096,
                "VIVID import: malformed facial routes");
        for(const auto& r:routes.items()) {
            allowed(r,{"channel","target","gain"});
            require(r["channel"].isString()&&r["target"].isString(),
                    "VIVID import: facial route requires channel and target");
            Channel channel{};
            const auto name=r["channel"].str();
            require(channelFromName(name,channel),"VIVID import: unknown ARKit channel");
            float gain=1.0f;
            if(r.has("gain")) {
                require(r["gain"].isNumber(),"VIVID import: nonnumeric route gain");
                gain=float(r["gain"].toDouble(std::numeric_limits<double>::quiet_NaN()));
            }
            m.routes.push_back({channel,r["target"].str(),gain});
        }
    }
    if(src.has("materials")) {
        const auto& materials=src["materials"];
        require(materials.isObject()&&materials.size()<=512,
                "VIVID import: malformed material mapping");
        for(const auto& [name,value]:materials.members()) {
            require(value.isString(),"VIVID import: material type must be string");
            m.materials.emplace(name,surfaceFromName(value.str()));
        }
    }
    m.validate();
    return m;
}
Json HumanImportManifest::toJson() const {
    validate();
    auto j=Json::object();
    j.set("version",Json::number(std::int64_t(version)));
    j.set("model",Json::string(modelPath));
    if(!dnaPath.empty())j.set("dna",Json::string(dnaPath));
    if(!morphPath.empty())j.set("morphs",Json::string(morphPath));
    auto routesJson=Json::array();
    for(const auto& route:routes){
        auto r=Json::object();
        r.set("channel",Json::string(std::string(channelName(route.channel))));
        r.set("target",Json::string(route.targetName));
        r.set("gain",Json::number(double(route.gain),6));
        routesJson.push(std::move(r));
    }
    j.set("routes",std::move(routesJson));
    auto materialsJson=Json::object();
    for(const auto& [name,surface]:materials)
        materialsJson.set(name,Json::string(surfaceName(surface)));
    j.set("materials",std::move(materialsJson));
    return j;
}

SparseMorphLibrary parseHumanMorphs(const Json& json,std::size_t vertices) {
    allowed(json,{"version","vertices","targets"});
    require(json.has("version")&&integer(json["version"])==1,
            "VIVID morphs: unknown version");
    require(json.has("vertices")&&integer(json["vertices"])==vertices,
            "VIVID morphs: vertex IDs do not match model source");
    const auto& items=json["targets"];
    require(items.isArray()&&items.size()<=4096,
            "VIVID morphs: target count outside budget");
    std::vector<MorphTargetInput> targets;
    targets.reserve(items.size());
    for(const auto& item:items.items()) {
        allowed(item,{"name","deltas"});
        require(item["name"].isString() &&
                item["deltas"].isArray() && item["deltas"].size()<=16000000,
                "VIVID morphs: invalid shape/delta list");
        MorphTargetInput t;
        t.name=item["name"].str();
        t.deltas.reserve(item["deltas"].size());
        for(const auto& delta:item["deltas"].items()) {
            allowed(delta,{"vertex","position","normal","tangent"});
            require(delta.has("vertex"),"VIVID morphs: missing vertex");
            MorphDelta d{};
            d.vertex=integer(delta["vertex"]);
            if(delta.has("position"))d.position=readVec(delta["position"]);
            if(delta.has("normal"))d.normal=readVec(delta["normal"]);
            if(delta.has("tangent"))d.tangent=readVec(delta["tangent"]);
            t.deltas.push_back(d);
        }
        targets.push_back(std::move(t));
    }
    return SparseMorphLibrary::compile(vertices,std::move(targets));
}

HumanImportedAsset importHumanAsset(const HumanImportManifest& m,
                                   const render::FileReader& reader) {
    m.validate();
    HumanImportedAsset out;
    if(!m.dnaPath.empty())
        out.dna=HumanDNA::fromJson(Json::parse(file(reader,m.dnaPath,1024*1024)));
    else out.dna=HumanDNA{};
    // render::loadModel dispatches to the engine's glTF/FBX/OBJ readers.
    // Texture dependencies use the same caller-supplied FileReader.
    out.model=render::loadModel(m.modelPath,reader);
    if(!out.model || out.model->vertices.empty())
        throw std::runtime_error("VIVID import: model has no CPU vertices");
    const std::size_t importedVertices=out.model->vertices.size();
    Json morphJson;
    if(!m.morphPath.empty()) {
        morphJson=Json::parse(file(reader,m.morphPath,64ull*1024ull*1024ull));
        require(morphJson.isObject() && morphJson.has("vertices") &&
                integer(morphJson["vertices"])==importedVertices,
                "VIVID import: morphs do not match source model vertex IDs");
    }

    render::prepareModelUpload(*out.model);
    // Model preparation may append generated LOD/batched vertices but must
    // not shrink source vertex indices. If it does, fail rather than applying
    // facial corrections to the wrong vertices.
    if(out.model->vertices.size()<importedVertices)
        throw std::runtime_error("VIVID import: preparation invalidated source vertices");
    if(m.morphPath.empty())
        out.facialShapes=SparseMorphLibrary::compile(out.model->vertices.size(),{});
    else {
        // Source shapes target original vertex IDs. After preparation the
        // generated model-relative stream can be longer but keeps originals.
        morphJson.set("vertices",Json::number(std::int64_t(out.model->vertices.size())));
        out.facialShapes=parseHumanMorphs(morphJson,out.model->vertices.size());
    }
    out.facialBinding=FaceShapeBinding::compile(out.facialShapes,m.routes);
    out.rig=HumanRigBinding::compile(*out.model,out.dna.rig);
    for(const auto& [name,category]:m.materials) {
        auto match=out.model->findMaterial(name);
        if(match<0)
            throw std::runtime_error("VIVID import: unknown material "+name);
        out.model->materials[std::size_t(match)]=humanMaterial(category,out.dna);
    }
    return out;
}
} // namespace aiverse::vivid
