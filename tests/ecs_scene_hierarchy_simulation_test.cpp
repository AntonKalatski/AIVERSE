// Compile with engine/sim/Simulation.cpp, Fixed.cpp, FixedMath.cpp,
// engine/data/Json.cpp and JsonCompose.cpp.
#include "engine/sim/SceneHierarchySystem.h"
#include "engine/render/SceneHierarchyRender.h"
#include <cassert>
#include <iostream>
#include <memory>
using namespace aiverse;
static LocalTransform at(int x){
    LocalTransform value;
    value.position.x=Fixed::fromInt(x);
    return value;
}
class HierarchyModule : public GameModule {
public:
    std::shared_ptr<SceneHierarchy> hierarchy=std::make_shared<SceneHierarchy>();
    Entity root=kNullEntity,child=kNullEntity;
    std::string name() const override {return "hierarchy-example";}
    void setup(Simulation& sim,const Json&,const Json&) override {
        auto& w=sim.world();
        root=w.create();child=w.create();
        hierarchy->attach(w,root,at(10));
        hierarchy->attach(w,child,at(2));
        hierarchy->setParent(w,child,root);
        // Called after movement systems. This also registers an automatic
        // restore hook for SimSnapshot and successfully loaded binary states.
        installSceneHierarchy(sim,hierarchy);
    }
    void applyCommand(Simulation&,const Command&) override {}
    void hashState(Simulation& sim,Hasher& h) override {
        auto* pos=sim.world().tryGetConst<WorldTransform>(child);
        assert(pos);h.fx(pos->position.x);
    }
    int result(Simulation&) override {return 0;}
};
int main(){
    auto module=std::make_unique<HierarchyModule>();
    auto* game=module.get();
    Simulation sim({1,20,0,true},std::move(module));
    sim.setup(Json::object(),Json::object());
    assert(game->hierarchy->lastUpdated()==2);
    auto check=[&](int x) {
        const auto* world=sim.world().tryGetConst<WorldTransform>(game->child);
        assert(world && world->position.x==Fixed::fromInt(x));
        auto matrix=render::hierarchyMatrix(*world);
        assert(matrix.at(0,3)==float(x));
    };
    check(12);
    SimSnapshot before;
    sim.saveState(before);
    game->hierarchy->setLocal(sim.world(),game->root,at(20));
    sim.step();
    check(22);
    sim.loadState(before); // automatic hierarchy rebuild and recomputation
    check(12);
    assert(game->hierarchy->lastUpdated()==2);
    sim.step();
    check(12);
    assert(game->hierarchy->lastUpdated()==0);
    std::cout<<"PASS Simulation parent transforms, presentation matrix and snapshot restoration\n";
}
