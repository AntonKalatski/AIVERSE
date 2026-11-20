#include "apps/studio-native/StudioSession.h"
#include "tests/AlwaysCheck.h"
#include <filesystem>
#include <fstream>
#include <unistd.h>
using namespace aiverse;
namespace fs=std::filesystem;
int main() {
 const auto root=fs::temp_directory_path()/("studio-human-look-"+std::to_string(getpid()));
 const auto project=root/"templates"/"character";fs::create_directories(project/"content"/"Ada");
 try {
  {std::ofstream f(project/"game.json");f<<R"({"name":"Look test","module":"rts","rules":"rules.json"})";}
  {std::ofstream f(project/"rules.json");f<<R"({"resources":{},"types":{"person":{"name":"Person","health":{"max":1}}}})";}
  {std::ofstream f(project/"content/Ada/model.gltf");f<<"{}";}
  vivid::HumanAppearance look;look.materials["Face"]="skin";look.values["skin.roughness"]=.8f;
  {std::ofstream f(project/"content/Ada/appearance.json");f<<look.toJson().dump();}
  studio::StudioSession session(root.string(),project.string());
  auto call=[&](const char* method,Json params=Json::object()) {
   auto r=Json::object();r.set("id",Json::number(int64_t(1)));r.set("method",Json::string(method));r.set("params",std::move(params));return session.dispatch(r);
  };
  const auto info=call("studio.humanAppearanceInfo");
  AIVERSE_TEST_CHECK(!info.has("error")&&info["result"]["schema"].size()==72);
  studio::StudioAssetCatalog catalog(project);catalog.rebuild();const auto asset=catalog.byPath("Ada/model.gltf");
  AIVERSE_TEST_CHECK(asset.has_value());
  auto params=Json::object();params.set("guid",Json::string(asset->guid));
  const auto imported=call("studio.humanAppearanceFromAsset",params);
  AIVERSE_TEST_CHECK(imported["result"]["look"]==look.toJson());
  params.set("revision",info["result"]["preview"]["revision"]);params.set("look",look.toJson());
  const auto applied=call("studio.humanAppearanceApply",params);
  AIVERSE_TEST_CHECK(!applied.has("error")&&applied["result"]["preview"]["bound"].boolean());
  AIVERSE_TEST_CHECK(call("studio.humanAppearanceApply",params).has("error"));
  auto save=Json::object();save.set("name",Json::string("matte"));save.set("look",look.toJson());
  AIVERSE_TEST_CHECK(!call("studio.humanAppearanceSave",save).has("error"));
  AIVERSE_TEST_CHECK(call("studio.humanAppearanceLoad",save)["result"]["look"]==look.toJson());
  AIVERSE_TEST_CHECK(call("studio.humanAppearanceList")["result"]["looks"].size()==1);
  save.set("name",Json::string("../escape"));AIVERSE_TEST_CHECK(call("studio.humanAppearanceSave",save).has("error"));
  fs::remove(project/"content/Ada/appearance.json");
  fs::create_symlink(project/"human-looks/matte.json",project/"content/Ada/appearance.json");
  AIVERSE_TEST_CHECK(call("studio.humanAppearanceFromAsset",params).has("error"));
  fs::remove_all(root);
 } catch(...) {fs::remove_all(root);throw;}
}
