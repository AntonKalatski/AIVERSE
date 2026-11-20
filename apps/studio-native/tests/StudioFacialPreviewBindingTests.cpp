#include "apps/studio-native/StudioFacialPreviewBinding.h"
#include "tests/AlwaysCheck.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unistd.h>
using namespace aiverse;
using namespace aiverse::studio;
namespace fs=std::filesystem;
int main() {
    const fs::path project=fs::temp_directory_path()/
        ("aiverse-facial-preview-bind-"+std::to_string(::getpid()));
    AIVERSE_TEST_CHECK(fs::create_directory(project));
    try {
        const fs::path content=project/"content";
        AIVERSE_TEST_CHECK(fs::create_directory(content));
        {
            std::ofstream file(content/"face.glb",std::ios::binary);
            file<<"glTF"; // identity test only; NOT a valid imported GPU model
        }
        {
            std::ofstream file(content/"wrong.png",std::ios::binary);
            file<<"png";
        }
        StudioAssetCatalog assets(project);
        assets.rebuild();
        const auto model=assets.byPath("face.glb");
        const auto image=assets.byPath("wrong.png");
        AIVERSE_TEST_CHECK(model.has_value() && image.has_value());
        StudioFacialPreviewBinding preview;
        const Json rig=Json::parse(R"({"version":1,"morphs":[
            {"parameter":"jawOpen","node":"Face","target":"jawOpen"},
            {"parameter":"eyeBlinkLeft","node":"Face","target":"eyeBlinkLeft"}
            ],"rig":[]})");
        const Json info=preview.bind(assets,model->guid,rig);
        AIVERSE_TEST_CHECK(info["bound"].boolean());
        AIVERSE_TEST_CHECK(info["guid"].str()==model->guid);
        AIVERSE_TEST_CHECK(info["path"].str()=="face.glb");
        AIVERSE_TEST_CHECK(!info["runtimeReady"].boolean() &&
                           !info["runtimeVerified"].boolean());
        AIVERSE_TEST_CHECK(info["rendererChannelCapacity"].integer()==8);
        const auto generation=uint64_t(info["revision"].integer());
        AIVERSE_TEST_CHECK(preview.controls(generation,Json::parse(
            R"({"jawOpen":0.8})"))["values"]["jawOpen"].toDouble()>0.79);
        bool rejected=false;
        try {preview.controls(generation,Json::parse(
            R"({"jawOpen":2})"));}catch(const std::exception&){rejected=true;}
        AIVERSE_TEST_CHECK(rejected);
        AIVERSE_TEST_CHECK(preview.describe()["values"]["jawOpen"].toDouble()>0.79);
        rejected=false;
        try {preview.controls(generation,Json::parse(
            R"({"unknown":0.2})"));}catch(const std::exception&){rejected=true;}
        AIVERSE_TEST_CHECK(rejected);
        rejected=false;
        try {preview.bind(assets,image->guid,rig);}catch(const std::exception&){rejected=true;}
        AIVERSE_TEST_CHECK(rejected && preview.describe()["guid"].str()==model->guid);
        rejected=false;
        try {preview.controls(generation+1,Json::parse(
            R"({"jawOpen":0})"));}catch(const std::exception&){rejected=true;}
        AIVERSE_TEST_CHECK(rejected);
        preview.reset();
        AIVERSE_TEST_CHECK(!preview.describe()["bound"].boolean());
        fs::remove_all(project);
    }catch(...) {
        std::error_code ec;
        fs::remove_all(project,ec);
        throw;
    }
}
