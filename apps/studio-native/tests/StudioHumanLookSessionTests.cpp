#include "apps/studio-native/StudioSession.h"
#include "tests/AlwaysCheck.h"
#include <filesystem>
#include <fstream>
#include <unistd.h>
#include "tools/modeling/Modeling.h"
#include <thread>
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
  // Real build through native dispatch: async completion, self-contained GLB,
  // no overwrite, no partial output and unknown source GUID rejection.
  auto model=modeling::build(modeling::robotDocument());model.model->clips.clear();
  const auto glb=modeling::exportGlb(*model.model);
  {std::ofstream f(project/"content/person.glb",std::ios::binary);f.write(glb.data(),std::streamsize(glb.size()));}
  catalog.rebuild();auto person=catalog.byPath("person.glb");AIVERSE_TEST_CHECK(person.has_value());
  auto build=Json::object();build.set("guid",Json::string(person->guid));build.set("look",vivid::HumanAppearance{}.toJson());build.set("name",Json::string("assembled"));build.set("textureSize",Json::number(int64_t(256)));build.set("wardrobe",Json::parse(R"({"version":1,"hiddenMeshes":[],"items":[]})"));
  auto preset=Json::parse(R"({"version":1,"hiddenMeshes":[],"items":[{"id":"watch","asset":"person.glb","slot":"wrist_left","enabled":true,"bone":"","hideMeshes":[],"masks":[]}]})");
  {std::ofstream f(project/"content/wardrobe.json");f<<preset.dump();}
  const auto presetResult=call("studio.humanWardrobeFromAsset",build);
  AIVERSE_TEST_CHECK(!presetResult.has("error")&&presetResult["result"]["items"][0].getStr("asset")==person->guid);
  AIVERSE_TEST_CHECK(presetResult["result"]["items"][0]["scale"].size()==3&&presetResult["result"]["items"][0].getStr("finish")=="source");
  AIVERSE_TEST_CHECK(call("studio.humanWardrobeInfo",build)["result"]["meshes"].size()>0);
  AIVERSE_TEST_CHECK(call("studio.humanCharacterBuild",build)["result"].getBool("running"));
  auto poll=[&](){Json status;for(int n=0;n<500;++n){status=call("studio.humanCharacterBuildStatus")["result"];if(!status.getBool("running"))return status;std::this_thread::sleep_for(std::chrono::milliseconds(10));}return status;};
  auto result=poll();AIVERSE_TEST_CHECK(!result.getBool("running")&&!result.has("error")&&result.has("guid"));
  AIVERSE_TEST_CHECK(fs::file_size(project/"content/Characters/assembled/model.glb")>100);
  AIVERSE_TEST_CHECK(fs::is_regular_file(project/"content/Characters/assembled/source.json"));
  AIVERSE_TEST_CHECK(call("studio.humanCharacterBuild",build)["result"].getBool("running"));
  AIVERSE_TEST_CHECK(poll().has("error"));
  AIVERSE_TEST_CHECK(!fs::exists(project/"content/Characters/.assembled.pending"));
  const auto initialAnimation=call("studio.humanPreviewAnimation")["result"];AIVERSE_TEST_CHECK(initialAnimation.getBool("playing")&&initialAnimation.getBool("faceIdle")&&initialAnimation.getStr("pose")=="relaxed");
  auto animParams=Json::object();auto anim=initialAnimation;anim.set("pose",Json::string("t_pose"));animParams.set("settings",anim);AIVERSE_TEST_CHECK(call("studio.humanPreviewAnimation",animParams)["result"].getStr("pose")=="t_pose");
  anim.set("speed",Json::number(100.));animParams.set("settings",anim);AIVERSE_TEST_CHECK(call("studio.humanPreviewAnimation",animParams).has("error"));AIVERSE_TEST_CHECK(call("studio.humanPreviewAnimation")["result"].getStr("pose")=="t_pose");
  {render::Model fixture;fixture.nodes.resize(4);fixture.meshes.resize(1);fixture.nodes[0].name="root";fixture.nodes[1].name="upperarm_l_23";fixture.nodes[1].parent=0;fixture.nodes[2].name="lowerarm_l_24";fixture.nodes[2].parent=1;fixture.nodes[2].t={1,-1,0};fixture.nodes[3].name="face";fixture.nodes[3].parent=0;fixture.nodes[3].mesh=0;fixture.meshes[0].morphTargetNames={"eyeBlinkLeft","eyeBlinkRight","mouthSmileLeft"};fixture.finalize();
   auto clip=vivid::humanPreviewClip(fixture,2,true);render::Pose pose;render::bindPose(fixture,pose);render::sampleClip(clip,1.7f,pose);AIVERSE_TEST_CHECK(pose[3].w[0]>.99f&&pose[3].w[1]>.99f);std::vector<render::mat4> world;render::worldMatrices(fixture,pose,world);const auto hand=world[2].transformPoint({});AIVERSE_TEST_CHECK(hand.x>1.4f&&std::abs(hand.y)<.001f);
   render::bindPose(fixture,pose);render::sampleClip(clip,0,pose);AIVERSE_TEST_CHECK(pose[3].w[0]==0);render::Pose end;render::bindPose(fixture,end);render::sampleClip(clip,6,end);AIVERSE_TEST_CHECK(std::abs(end[3].w[2]-pose[3].w[2])<1e-5f);
   auto off=vivid::humanPreviewClip(fixture,0,false);render::bindPose(fixture,pose);render::sampleClip(off,1.7f,pose);AIVERSE_TEST_CHECK(pose[3].w[0]==0);render::worldMatrices(fixture,pose,world);const auto relaxed=world[2].transformPoint({});AIVERSE_TEST_CHECK(relaxed.y< -1.3f&&relaxed.x<.3f);AIVERSE_TEST_CHECK(fixture.clips.empty()&&fixture.nodes[2].t.x==1);
  }
  const auto lightInfo=call("studio.humanStudioLighting")["result"];AIVERSE_TEST_CHECK(lightInfo["presets"].size()==8);
  auto lightParams=Json::object();auto lights=Json::parse(R"({"preset":"gel","rotation":90,"gain":0.75,"backdrop":true})");lightParams.set("settings",lights);
  AIVERSE_TEST_CHECK(call("studio.humanStudioLighting",lightParams)["result"]["settings"].getStr("preset")=="gel");
  lights.set("preset",Json::string("unknown"));lightParams.set("settings",lights);AIVERSE_TEST_CHECK(call("studio.humanStudioLighting",lightParams).has("error"));
  AIVERSE_TEST_CHECK(call("studio.humanStudioLighting")["result"]["settings"].getStr("preset")=="gel");
  const auto backdrop=vivid::humanStudioCyclorama();AIVERSE_TEST_CHECK(backdrop.vertices().size()==134&&backdrop.indices().size()==396);
  for(size_t i=0;i<backdrop.indices().size();i+=3){const auto& a=backdrop.vertices()[backdrop.indices()[i]];const auto& b=backdrop.vertices()[backdrop.indices()[i+1]];const auto& c=backdrop.vertices()[backdrop.indices()[i+2]];AIVERSE_TEST_CHECK(render::dot(render::cross(b.pos-a.pos,c.pos-a.pos),a.nrm)>0);}
  auto face=Json::parse(R"({"version":1,"meshes":[],"values":{"width":0.1},"landmarks":[]})");face.member("meshes").push(Json::string(model.model->meshes[0].name));
  build.set("face",face);build.set("name",Json::string("face_baked"));
  AIVERSE_TEST_CHECK(call("studio.humanCharacterBuild",build)["result"].getBool("running"));AIVERSE_TEST_CHECK(!poll().has("error"));
  auto scan=Json::object();scan.set("guid",Json::string(person->guid));scan.set("scanGuid",Json::string(person->guid));scan.set("face",face);scan.set("distance",Json::number(.1));scan.set("strength",Json::number(.5));
  AIVERSE_TEST_CHECK(call("studio.humanFaceFitScan",scan)["result"].getBool("running"));auto fitted=poll();AIVERSE_TEST_CHECK(!fitted.has("error")&&fitted["face"]["landmarks"].size()>=4);
  auto parts=Json::object();parts.set("guid",Json::string(person->guid));parts.set("headGuid",Json::string(person->guid));parts.set("bone",Json::string(""));parts.set("hiddenMeshes",Json::array());parts.set("name",Json::string("parts_imported"));
  AIVERSE_TEST_CHECK(call("studio.humanHeadBodyImport",parts)["result"].getBool("running"));AIVERSE_TEST_CHECK(!poll().has("error"));
  build.set("guid",Json::string("../../outside"));AIVERSE_TEST_CHECK(call("studio.humanCharacterBuild",build).has("error"));
  fs::remove_all(root);
 } catch(...) {fs::remove_all(root);throw;}
}
