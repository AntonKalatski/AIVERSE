// Stage 185: CPU-only 52-channel semantic/IK runtime contract for Studio.
// This is a real MotionWeave FacialControlRig/FacialPreviewSession test,
// not a replacement for native Metal/Vulkan/GLES rendering acceptance.
#include "aiverselib/animation/FacialPreviewSession.h"
#include "engine/render/Animation.h"
#include "engine/render/Model.h"
#include "tests/AlwaysCheck.h"
#include <cmath>
#include <limits>
#include <string>

using namespace aiverse;
using namespace aiverse::render;
using namespace aiverse::lib::animation;

static Model makeFace() {
    Model model;
    model.nodes.resize(2);
    model.order={0,1};
    model.nodes[0].name="Face";
    model.nodes[0].mesh=0;
    model.nodes[0].children={1};
    model.nodes[1].name="Eye.L";
    model.nodes[1].parent=0;
    model.nodes[1].t={0,1,0};
    model.meshes.resize(1);
    model.meshes[0].primitives.resize(1);
    model.meshes[0].primitives[0].morphTargets=52;
    for(int i=0;i<52;++i)
        model.meshes[0].morphTargetNames.push_back("expression"+std::to_string(i));
    return model;
}
static Json authored() {
    Json doc=Json::object(),morphs=Json::array(),rig=Json::array();
    doc.set("version",Json::number(int64_t(1)));
    for(int i=0;i<52;++i) {
        Json item=Json::object();
        item.set("parameter",Json::string("expression"+std::to_string(i)));
        item.set("node",Json::string("Face"));
        item.set("target",Json::string("expression"+std::to_string(i)));
        morphs.push(std::move(item));
    }
    Json eye=Json::object(),axis=Json::array();
    axis.push(Json::number(int64_t(0)));
    axis.push(Json::number(int64_t(0)));
    axis.push(Json::number(int64_t(1)));
    eye.set("bone",Json::string("Eye.L"));
    eye.set("op",Json::string("aim"));
    eye.set("target",Json::string("lookAt"));
    eye.set("weightParameter",Json::string("gazeWeight"));
    eye.set("axis",std::move(axis));
    rig.push(std::move(eye));
    doc.set("morphs",std::move(morphs));
    doc.set("rig",std::move(rig));
    return doc;
}
int main() {
    static_assert(kMaxMorphTargets==64,
                  "Studio native Renderer3D must share 64-channel MotionWeave ABI");
    auto model=makeFace();
    auto document=authored();
    FacialPreviewSession session;
    FacialPreviewSession::Scalars scalars;
    FacialPreviewSession::Vectors vectors{{"lookAt",{2.f,1.f,3.f}}};
    for(int i=0;i<52;++i)
        scalars.emplace("expression"+std::to_string(i),i%2?0.f:1.f);
    scalars.emplace("gazeWeight",1.f);
    std::string error;
    AIVERSE_TEST_CHECK(session.configure(model,document,scalars,vectors,error));
    AIVERSE_TEST_CHECK(session.ready());
    Animator animator;animator.setModel(&model); // exactly one parent pose owner
    Pose pose;animator.evaluate(pose);
    AIVERSE_TEST_CHECK(session.apply(model,pose,.016f,error));
    AIVERSE_TEST_CHECK(pose.size()==2);
    for(int i=0;i<52;++i)
        AIVERSE_TEST_CHECK(std::abs(pose[0].w[i]-(i%2?0.f:1.f))<.001f);
    AIVERSE_TEST_CHECK(pose[1].r.w<.999f); // existing eye Aim IK
    AIVERSE_TEST_CHECK(!session.scalar("absent",.5f));
    AIVERSE_TEST_CHECK(!session.scalar("expression0",2.f));
    AIVERSE_TEST_CHECK(session.scalar("expression0",.8f));
    animator.evaluate(pose);
    AIVERSE_TEST_CHECK(session.apply(model,pose,.016f,error));
    AIVERSE_TEST_CHECK(pose[0].w[0]>.79f && pose[0].w[0]<.99f);

    // Invalid edits must preserve last valid compiled program and state.
    Json invalid=authored();
    invalid.member("morphs").at(0).set(
        "target",Json::string("nonexistent"));
    AIVERSE_TEST_CHECK(!session.configure(model,invalid,scalars,vectors,error));
    AIVERSE_TEST_CHECK(session.ready());
    animator.evaluate(pose);
    AIVERSE_TEST_CHECK(session.apply(model,pose,.016f,error));

    // In-place importer generation mutation invalidates compiled semantic rig.
    model.meshes[0].morphTargetNames[0]="renamed";
    animator.evaluate(pose);
    AIVERSE_TEST_CHECK(!session.apply(model,pose,.016f,error));
    AIVERSE_TEST_CHECK(!error.empty());
}
