// COW page conversion and append must leave the pool valid after exceptions.
#include "engine/core/ECS.h"
#include <cassert>
#include <iostream>
#include <stdexcept>
using namespace aiverse;
struct Fragile {
    int value=0;
    static inline bool throwCopy=false,throwMove=false;
    explicit Fragile(int v=0):value(v){}
    Fragile(const Fragile& other):value(other.value) {
        if(throwCopy)throw std::runtime_error("copy");
    }
    Fragile(Fragile&& other):value(other.value) {
        if(throwMove)throw std::runtime_error("move");
    }
    Fragile& operator=(const Fragile&)=default;
    Fragile& operator=(Fragile&&)=default;
};
int main() {
    World world;
    Entity first=world.create();
    world.add<Fragile>(first,1);
    Fragile::throwCopy=true;
    bool caught=false;
    try {world.enableCowSnapshots(true);}
    catch(const std::runtime_error&){caught=true;}
    assert(caught && !world.cowSnapshotsEnabled());
    assert(world.get<Fragile>(first).value==1);
    Fragile::throwCopy=false;
    world.enableCowSnapshots(true);
    World snapshot=world;
    Entity second=world.create();
    Fragile::throwMove=true;
    caught=false;
    try {world.add<Fragile>(second,2);}
    catch(const std::runtime_error&){caught=true;}
    assert(caught && world.countExisting<Fragile>()==1 && !world.has<Fragile>(second));
    Fragile::throwMove=false;
    world.add<Fragile>(second,2);
    assert(world.countExisting<Fragile>()==2);
    assert(static_cast<const World&>(snapshot).tryGetConst<Fragile>(first)->value==1);
    while(world.countExisting<Fragile>()<256) {
        auto e=world.create();world.add<Fragile>(e,int(world.countExisting<Fragile>()));
    }
    Entity boundary=world.create();
    Fragile::throwMove=true;
    caught=false;
    try {world.add<Fragile>(boundary,256);}
    catch(const std::runtime_error&){caught=true;}
    assert(caught && world.countExisting<Fragile>()==256 && !world.has<Fragile>(boundary));
    Fragile::throwMove=false;
    world.add<Fragile>(boundary,256);
    assert(world.countExisting<Fragile>()==257);
    world.destroy(first);
    assert(world.countExisting<Fragile>()==256);
    assert(snapshot.alive(first));
    world.copyFrom(snapshot);
    assert(world.countExisting<Fragile>()==1 && world.get<Fragile>(first).value==1);
    std::cout<<"PASS COW page conversion, failed append recovery and shared snapshot isolation\n";
}
