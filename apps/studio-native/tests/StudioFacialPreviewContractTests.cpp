// Integration Stage 182 — file sandbox contract for native GPU facial preview.
// Does not claim that a glTF/FBX file contains a compatible expression rig.
// Full renderer/ModelAssetProvider acceptance is a separate Mac GPU gate.
#include "apps/studio-native/StudioFacialPreviewPaths.h"
#include "tests/AlwaysCheck.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>

using namespace aiverse::studio;
namespace fs=std::filesystem;

int main() {
    const fs::path root=fs::temp_directory_path()/
        ("aiverse-face-preview-"+std::to_string(::getpid()));
    AIVERSE_TEST_CHECK(fs::create_directory(root));
    try {
        AIVERSE_TEST_CHECK(fs::create_directory(root/"models"));
        {
            std::ofstream file(root/"models"/"hero.glb",std::ios::binary);
            file<<"glTF fake test bytes"; // file path check only
        }
        AIVERSE_TEST_CHECK(
            studioFacialAssetFile(root.string(),"models/hero.glb",true)==
            fs::canonical(root/"models"/"hero.glb"));
        std::string bytes;
        AIVERSE_TEST_CHECK(studioReadFacialAsset(root.string(),
            "models/hero.glb",bytes) && bytes.starts_with("glTF"));
        auto rejects=[&](const std::string& key) {
            bool failed=false;
            try {(void)studioFacialAssetFile(root.string(),key,true);}
            catch(const std::exception&){failed=true;}
            AIVERSE_TEST_CHECK(failed);
        };
        rejects("../hero.glb");
        rejects("models/../../hero.glb");
        rejects("/etc/passwd");
        rejects("models/hero.exe");
        rejects("models/not-found.glb");
        const fs::path outside=fs::temp_directory_path()/
            ("aiverse-face-outside-"+std::to_string(::getpid())+".glb");
        {
            std::ofstream file(outside,std::ios::binary);
            file<<"outside";
        }
        std::error_code ec;
        fs::create_symlink(outside,root/"models"/"outside.glb",ec);
        if(!ec)rejects("models/outside.glb");
        fs::remove(outside,ec);
        fs::remove_all(root,ec);
    }catch(...) {
        std::error_code ec;
        fs::remove_all(root,ec);
        throw;
    }
    std::cout<<"Studio native facial GPU asset sandbox contract OK\n";
}
