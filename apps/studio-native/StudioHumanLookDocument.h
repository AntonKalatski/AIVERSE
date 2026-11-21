#pragma once
#include "apps/studio-native/StudioFacialRigDocument.h"
#include "engine/character/vivid/HumanAppearance.h"
namespace aiverse::studio {
inline bool validStudioHumanLookName(const std::string& name){return validStudioFacialRigName(name);}
inline bool validStudioHumanLookDraft(const Json& doc,std::string* error){try{(void)vivid::HumanAppearance::fromJson(doc);return true;}catch(const std::exception& ex){if(error)*error=ex.what();return false;}}
inline std::filesystem::path studioHumanLookFolder(
        const std::string& projectRoot,bool create=false) {
    namespace fs=std::filesystem;
    if(projectRoot.empty())throw std::runtime_error("Open a project first");
    const fs::path root=fs::weakly_canonical(fs::path(projectRoot));
    if(!fs::is_directory(root))throw std::runtime_error("Invalid project root");
    const fs::path folder=root/"human-looks";
    std::error_code ec;
    const auto status=fs::symlink_status(folder,ec);
    if(!ec && fs::is_symlink(status))
        throw std::runtime_error("Human look folder cannot be a symlink");
    if(!fs::exists(folder)) {
        if(!create)return folder;
        if(!fs::create_directory(folder))
            throw std::runtime_error("Cannot create human-looks directory");
    }
    if(!fs::is_directory(folder) || fs::is_symlink(fs::symlink_status(folder)))
        throw std::runtime_error("Human look folder is not a real directory");
    return folder;
}

inline std::filesystem::path studioHumanLookFile(
        const std::string& projectRoot,const std::string& name,
        bool createFolder=false) {
    namespace fs=std::filesystem;
    if(!validStudioHumanLookName(name))
        throw std::invalid_argument("Invalid human look filename");
    const fs::path path=studioHumanLookFolder(projectRoot,createFolder)/
                        (name+".json");
    const auto state=fs::symlink_status(path);
    if(fs::is_symlink(state))
        throw std::runtime_error("Human look file cannot be a symlink");
    if(fs::exists(path) && !fs::is_regular_file(path))
        throw std::runtime_error("Human look entry must be a regular file");
    return path;
}

inline Json listStudioHumanLooks(const std::string& projectRoot) {
    namespace fs=std::filesystem;
    Json names=Json::array();
    const fs::path folder=studioHumanLookFolder(projectRoot);
    if(!fs::exists(folder)) {
        Json result=Json::object();
        result.set("looks",std::move(names));
        return result;
    }
    std::vector<std::string> sorted;
    for(const auto& entry:fs::directory_iterator(folder)) {
        if(!entry.is_regular_file() || entry.is_symlink() ||
           entry.path().extension()!=".json")continue;
        const auto name=entry.path().stem().string();
        if(validStudioHumanLookName(name))sorted.push_back(name);
        if(sorted.size()>128)throw std::runtime_error("Too many human look files");
    }
    std::sort(sorted.begin(),sorted.end());
    for(const auto& name:sorted)names.push(Json::string(name));
    Json result=Json::object();
    result.set("looks",std::move(names));
    return result;
}

inline Json readStudioHumanLook(const std::string& root,const std::string& name) {
    namespace fs=std::filesystem;
    const fs::path file=studioHumanLookFile(root,name);
    if(!fs::exists(file) || fs::file_size(file)>128u*1024u)
        throw std::runtime_error("Human look file unavailable or too large");
    std::ifstream input(file,std::ios::binary);
    if(!input)throw std::runtime_error("Cannot read human look file");
    std::string bytes(std::istreambuf_iterator<char>{input},
                      std::istreambuf_iterator<char>{});
    const Json doc=Json::parse(bytes);
    std::string error;
    if(!validStudioHumanLookDraft(doc,&error))
        throw std::runtime_error("Invalid saved human look: "+error);
    Json result=Json::object();
    result.set("name",Json::string(name));
    result.set("look",doc);
    return result;
}

inline Json saveStudioHumanLook(const std::string& root,
                                const std::string& name,const Json& doc) {
    namespace fs=std::filesystem;
    std::string error;
    if(!validStudioHumanLookDraft(doc,&error))
        throw std::invalid_argument(error);
    const fs::path file=studioHumanLookFile(root,name,true);
    // Descriptor-relative exclusive staging prevents path traversal,
    // following a substituted pending symlink, or truncating the live
    // preset before a complete payload has been written.
    const std::string filename=name+".json";
    const std::string pending=filename+".pending";
    const int dir=::open(file.parent_path().c_str(),
                         O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
    if(dir<0)throw std::runtime_error("Cannot open human look project folder");
    int fd=-1;
    try {
        fd=::openat(dir,pending.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
        if(fd<0)throw std::runtime_error("Cannot create exclusive human look staging file");
        const std::string bytes=doc.dump(2)+"\n";
        std::size_t offset=0;
        while(offset<bytes.size()) {
            const ssize_t n=::write(fd,bytes.data()+offset,bytes.size()-offset);
            if(n<=0)throw std::runtime_error("Failed writing human look staging file");
            offset+=std::size_t(n);
        }
        if(::fsync(fd)!=0)throw std::runtime_error("Human look staging fsync failed");
        if(::close(fd)!=0) {
            fd=-1;
            throw std::runtime_error("Failed closing human look staging file");
        }
        fd=-1;
        if(::renameat(dir,pending.c_str(),dir,filename.c_str())!=0)
            throw std::runtime_error("Cannot publish human look file");
        (void)::fsync(dir);
        (void)::close(dir);
    }catch(...) {
        if(fd>=0)(void)::close(fd);
        (void)::unlinkat(dir,pending.c_str(),0);
        (void)::close(dir);
        throw;
    }
    Json result=Json::object();
    result.set("name",Json::string(name));
    result.set("look",doc);
    return result;
}

} // namespace aiverse::studio
