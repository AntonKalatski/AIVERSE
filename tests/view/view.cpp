#include "engine/view/View.h"
#include <cassert>
#include <cmath>
using namespace aiverse;
using namespace aiverse::render;
namespace aiverse::platform {
bool readContentFile(const std::string&,std::string&){return false;}
}
int main() {
    Model m;
    m.nodes.resize(1);m.order={0};m.nodes[0].name="root";
    auto graph=AnimationGraph::compile(m,Json::parse(R"({
        "parameters":{"intensity":0.0},
        "nodes":[{"id":"base","kind":"bind"}],
        "output":"base"})"));
    View view(0);
    view::Object animated;
    animated.id=101;animated.generation=7;animated.draw.model=&m;
    animated.graph=graph;
    animated.parameters.set("intensity",.5f);
    view::Object unanimated;
    unanimated.id=202;unanimated.draw.model=&m;
    std::vector<view::Object> objects{animated,unanimated};
    auto result=view.update(objects,.016f);
    assert(result.source.created==1);
    assert(result.drawable==2);
    assert(result.animated==1);
    auto id=view.presentationEntity(101);
    assert(view.presentation().alive(id));
    assert(view.presentation().get<lib::animation::AnimationPoseComponent>(id).valid);
    assert(view.presentation().get<lib::animation::AnimationInputsComponent>(id)
        .overrides.values.at("intensity")==.5f);
    animated.generation++;
    objects[0]=animated;
    result=view.update(objects,.016f);
    assert(result.source.restarted==1);
    objects.erase(objects.begin());
    result=view.update(objects,.016f);
    assert(result.source.removed==1);
    assert(!view.presentation().alive(id));
    view.clear();
    assert(view.presentation().countExisting<lib::animation::AnimationPlayerComponent>()==0);
}
