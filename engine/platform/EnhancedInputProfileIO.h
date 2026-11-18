// Validated, crash-conscious user-profile persistence for Enhanced Input.
// Host/presentation only. Never call from deterministic ECS systems.
#pragma once
#include "EnhancedInputConfig.h"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

namespace aiverse::platform::enhanced_config {
namespace detail {
inline bool ioError(std::string* error,const char* prefix,const std::error_code& ec) {
    if(error)*error=std::string(prefix)+": "+ec.message();
    return false;
}
} // namespace detail

// A malformed or missing file never modifies the caller's current profile.
inline bool loadProfileFile(const std::filesystem::path& file,Profile& out,std::string* error=nullptr) {
    std::ifstream stream(file,std::ios::binary);
    if(!stream){if(error)*error="could not open input profile";return false;}
    const std::string text((std::istreambuf_iterator<char>(stream)),std::istreambuf_iterator<char>());
    if(stream.bad()){if(error)*error="could not read input profile";return false;}
    try {
        Profile parsed;
        if(!readProfile(Json::parse(text),parsed,error))return false;
        out=std::move(parsed);
        return true;
    } catch(const std::exception& e) {
        if(error)*error=e.what();
        return false;
    }
}

// The temporary file is fully written and validated before an existing profile is touched.
// POSIX rename replaces atomically. On Windows, rename-to-backup is used if replacement fails.
// On any write failure, the caller's in-memory binding must remain unchanged.
inline bool saveProfileFile(const std::filesystem::path& file,const Profile& profile,std::string* error=nullptr) {
    Profile valid;
    if(!readProfile(writeProfile(profile),valid,error))return false;
    std::error_code ec;
    if(!file.parent_path().empty()) {
        std::filesystem::create_directories(file.parent_path(),ec);
        if(ec)return detail::ioError(error,"profile directory",ec);
    }
    const auto temp=std::filesystem::path(file.string()+".tmp");
    const auto backup=std::filesystem::path(file.string()+".bak");
    {
        std::ofstream stream(temp,std::ios::binary|std::ios::trunc);
        if(!stream){if(error)*error="cannot open temporary profile";return false;}
        stream<<writeProfile(valid).dump(2)<<'\n';
        stream.flush();
        if(!stream){if(error)*error="cannot write temporary profile";return false;}
        stream.close();
        if(!stream){if(error)*error="cannot close temporary profile";return false;}
    }
    std::filesystem::rename(temp,file,ec);
    if(!ec)return true;
    // Windows does not rename over an existing destination. Do not delete the
    // existing profile first; move it to a backup and restore it if commit fails.
    ec.clear();
    const bool original=std::filesystem::exists(file,ec);
    if(ec)return detail::ioError(error,"profile existence",ec);
    if(!original){if(error)*error="cannot commit temporary profile";return false;}
    std::filesystem::remove(backup,ec);
    if(ec)return detail::ioError(error,"profile backup cleanup",ec);
    std::filesystem::rename(file,backup,ec);
    if(ec)return detail::ioError(error,"profile backup",ec);
    std::filesystem::rename(temp,file,ec);
    if(ec) {
        const auto writeError=ec;
        std::error_code restored;
        std::filesystem::rename(backup,file,restored);
        if(error)*error=std::string("profile commit: ")+writeError.message()+
                          (restored?" (also failed to restore backup: "+restored.message()+")":"");
        return false;
    }
    std::filesystem::remove(backup,ec); // profile committed; a stale backup is harmless
    return true;
}
} // namespace aiverse::platform::enhanced_config
