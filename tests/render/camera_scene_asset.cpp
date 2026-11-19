// AIVERSE camera authoring asset JSON round-trip (native C++/ECS).
#include "engine/sim/CameraSceneAsset.h"
#include "engine/render/EcsCameraRender.h"
#include <stdexcept>
using namespace aiverse;
static void check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
int main(){
 World source;SceneHierarchy scene;
 auto a=source.create();
 LocalTransform local;local.position={1_fx,2_fx,3_fx};
 check(scene.attach(source,a,local),"attach");
 source.add<CameraComponent>(a,CameraComponent{});
 source.add<CameraNameComponent>(a,CameraNameComponent{5,"Overview"});
 source.add<CameraOutputComponent>(a,CameraOutputComponent{CameraOutputKind::Texture,42,256,256});
 auto json=cameraSceneToJson(source);
 check(json["cameras"].size()==1,"serialize");
 World dest;SceneHierarchy target;std::vector<Entity> created;
 std::string error;
 check(loadCameraScene(dest,target,Json::parse(json.dump()),&created,&error),"parse");
 check(created.size()==1&&dest.get<CameraNameComponent>(created[0]).name=="Overview","name");
 render::EcsCameraView view;
 check(render::resolveEcsCamera(dest,created[0],view),"presentation");
 check(view.output.targetAssetKey==42 && int(view.camera.eye().x)==1,"render values");
 auto invalid=json;
 invalid["cameras"].itemsMut()[0].set("id",Json::number(int64_t(0)));
 World broken;SceneHierarchy hierarchy;
 check(!loadCameraScene(broken,hierarchy,invalid,nullptr,&error) &&
       broken.countExisting<CameraComponent>()==0,"reject before mutation");
}
