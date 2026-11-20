// Native-only model path sandbox for real Studio facial GPU preview.
// Browser sends project-relative keys; C++ derives actual filenames only
// beneath the already trusted Workspace project directory.
#pragma once
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <system_error>

namespace aiverse::studio {
inline std::filesystem::path studioFacialAssetFile(
    const std::string& root,const std::string& key,bool requireModel=false) {
    namespace fs=std::filesystem;
    if(root.empty()||key.empty()||key.size()>512)
        throw std::invalid_argument("Studio face preview requires a project asset key");
    const fs::path relative=fs::path(key);
    if(relative.is_absolute()||relative.has_root_path())
        throw std::invalid_argument("Studio face preview asset must be relative");
    if(requireModel) {
        for(const auto& part:relative)
            if(part==".."||part==".")
                throw std::invalid_argument("Studio facial model path contains traversal");
    }
    const fs::path canonicalRoot=fs::canonical(fs::path(root));
    const fs::path candidate=fs::canonical(canonicalRoot/relative);
    // Compare path elements instead of checking a string prefix:
    // project-evil is not a descendant of project.
    auto left=canonicalRoot.begin(),right=candidate.begin();
    for(;left!=canonicalRoot.end();++left,++right)
        if(right==candidate.end()||*left!=*right)
            throw std::invalid_argument("Studio face asset escapes project root");
    if(!fs::is_regular_file(candidate))
        throw std::invalid_argument("Studio facial asset must be a regular file");
    constexpr std::uintmax_t maxBytes=256u*1024u*1024u;
    if(fs::file_size(candidate)>maxBytes)
        throw std::invalid_argument("Studio facial asset exceeds import budget");
    if(requireModel) {
        const std::string extension=candidate.extension().string();
        if(extension!=".glb"&&extension!=".gltf"&&extension!=".fbx")
            throw std::invalid_argument("Studio facial preview needs GLB, glTF or FBX");
    }
    return candidate;
}
inline bool studioReadFacialAsset(
    const std::string& root,const std::string& key,std::string& bytes) {
    try {
        const auto file=studioFacialAssetFile(root,key,false);
        std::ifstream stream(file,std::ios::binary);
        if(!stream)return false;
        bytes.assign(std::istreambuf_iterator<char>{stream},
                     std::istreambuf_iterator<char>{});
        return bool(stream)||stream.eof();
    }catch(...) {return false;}
}
} // namespace aiverse::studio
