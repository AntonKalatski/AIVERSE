// Stage 184 — Studio facial-preview asset binding, source-only contract.
// This branch has an 8-channel Renderer3D; do NOT claim GPU facial preview
// for 52/64 channels until the MotionWeave 64-channel runtime is integrated.
#pragma once
#include "apps/studio-native/StudioAssetCatalog.h"
#include "apps/studio-native/StudioContentDirectory.h"
#include "apps/studio-native/StudioFacialRigDocument.h"
#include "engine/render/Model.h"
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace aiverse::studio {

class StudioFacialPreviewBinding final {
public:
    void reset() {
        guid_.clear();
        path_.clear();
        draft_=Json();
        values_.clear();
        vectors_.clear();
        ++revision_;
    }
    bool bound() const {return !guid_.empty();}
    uint64_t revision() const {return revision_;}

    // Use ONLY native catalog GUIDs, not browser-provided file paths.
    // File must still be inside project/content with no symlink segments.
    static std::string checkedModelPath(
            const StudioAssetCatalog& catalog,const std::string& guid) {
        namespace fs=std::filesystem;
        if(!studioValidGuid(guid))
            throw std::invalid_argument("Invalid facial model GUID");
        const auto entry=catalog.byGuid(guid);
        if(!entry || entry->directory || entry->importer!="model")
            throw std::runtime_error("Facial model GUID missing or is not a model");
        const fs::path relative(entry->path);
        if(relative.empty() || relative.is_absolute())
            throw std::runtime_error("Invalid relative model asset path");
        for(const auto& part:relative)
            if(part.empty() || part=="." || part=="..")
                throw std::runtime_error("Unsafe model asset path");
        const auto ext=relative.extension().string();
        if(ext!=".glb" && ext!=".gltf" && ext!=".fbx")
            throw std::runtime_error("Facial preview requires glTF/GLB/FBX");
        fs::path check=studioContentRoot(catalog.project());
        for(const auto& part:relative) {
            check/=part;
            std::error_code ec;
            const auto status=fs::symlink_status(check,ec);
            if(ec || fs::is_symlink(status))
                throw std::runtime_error("Symlink or unavailable facial model path");
        }
        if(!fs::is_regular_file(fs::symlink_status(check)))
            throw std::runtime_error("Facial model is not a regular asset");
        return entry->path; // content-relative only; never expose full path
    }

    Json bind(const StudioAssetCatalog& catalog,const std::string& guid,
              const Json& draft) {
        std::string diagnostic;
        if(!validStudioFacialRigDraft(draft,&diagnostic))
            throw std::invalid_argument("Invalid facial rig: "+diagnostic);
        const std::string path=checkedModelPath(catalog,guid);
        std::map<std::string,float> defaults;
        std::map<std::string,std::array<float,3>> vectors;
        for(const auto& morph:draft["morphs"].items()) {
            const std::string name=morph.getStr("parameter");
            defaults.try_emplace(name,0.f);
        }
        for(const auto& op:draft["rig"].items()) {
            const auto name=op.getStr("weightParameter");
            if(!name.empty())defaults.try_emplace(name,1.f);
            for(const char* key:{"target","pole","translationParameter",
                                 "rotationParameter","scaleParameter"}) {
                const auto param=op.getStr(key);
                if(!param.empty())vectors.try_emplace(param,
                    std::array<float,3>{0.f,0.f,0.f});
            }
        }
        for(const auto& item:vectors)
            if(defaults.contains(item.first))
                throw std::invalid_argument("Facial input declared as both scalar and vector: "+item.first);
        // Publish the COMPLETE new binding only after native GUID and
        // authoring schema checks. This is NOT a Model/Graph/GPU compile.
        guid_=guid;
        path_=path;
        draft_=draft;
        values_=std::move(defaults);
        vectors_=std::move(vectors);
        ++revision_;
        return describe();
    }

    Json controls(uint64_t expectedRevision,const Json& values,
                  const Json& vectors) {
        if(!bound() || revision_!=expectedRevision)
            throw std::runtime_error("Stale facial preview selection; bind model again");
        if(!values.isObject() || values.size()>256 ||
           (!vectors.isNull() && !vectors.isObject()) ||
           vectors.size()>256)
            throw std::invalid_argument("Facial inputs must be bounded typed objects");
        auto candidate=values_;
        auto vectorCandidate=vectors_;
        for(const auto& [name,value]:values.members()) {
            const auto it=candidate.find(name);
            if(it==candidate.end() || !value.isNumber())
                throw std::invalid_argument("Unknown/non-numeric facial input: "+name);
            const double number=value.toDouble();
            if(!std::isfinite(number) || number<0. || number>1.)
                throw std::invalid_argument("Facial preview input must be 0..1");
            it->second=float(number);
        }
        for(const auto& [name,entry]:vectors.members()) {
            auto it=vectorCandidate.find(name);
            if(it==vectorCandidate.end() || !entry.isArray() ||
               entry.size()!=3)
                throw std::invalid_argument("Unknown/non-vector facial IK input: "+name);
            std::array<float,3> next{};
            for(size_t axis=0;axis<3;++axis) {
                const auto& coord=entry[axis];
                if(!coord.isNumber() || !std::isfinite(coord.toDouble()) ||
                   std::abs(coord.toDouble())>1000000.)
                    throw std::invalid_argument("Nonfinite/out-of-range facial IK vector");
                next[axis]=float(coord.toDouble());
            }
            it->second=next;
        }
        values_=std::move(candidate);
        vectors_=std::move(vectorCandidate);
        return describe();
    }
    Json describe() const {
        Json result=Json::object();
        result.set("bound",Json::fromBool(bound()));
        result.set("guid",Json::string(guid_));
        result.set("path",Json::string(path_));
        result.set("revision",Json::number(int64_t(revision_)));
        Json values=Json::object();
        for(const auto& [name,value]:values_)
            values.set(name,Json::number(double(value),6));
        result.set("values",std::move(values));
        Json vectors=Json::object();
        for(const auto& [name,value]:vectors_) {
            Json coords=Json::array();
            for(float component:value)
                coords.push(Json::number(double(component),6));
            vectors.set(name,std::move(coords));
        }
        result.set("vectors",std::move(vectors));
        result.set("runtimeReady",Json::fromBool(false));
        result.set("runtimeVerified",Json::fromBool(false));
        result.set("rendererChannelCapacity",
                   Json::number(int64_t(render::kMaxMorphTargets)));
        result.set("reason",Json::string(
            "Current Studio renderer has "+std::to_string(render::kMaxMorphTargets)+
            " morph slots; full MotionWeave 64-channel Model/Renderer3D and FacialPreviewSession must be integrated before GPU facial preview"));
        return result;
    }
private:
    std::string guid_,path_;
    Json draft_;
    std::map<std::string,float> values_;
    std::map<std::string,std::array<float,3>> vectors_;
    uint64_t revision_=0;
};

} // namespace aiverse::studio
