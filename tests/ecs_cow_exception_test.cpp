// COW page conversion and append must leave the pool valid after exceptions.
#include "engine/core/ECS.h"
#include <cassert>
#include <iostream>
#include <stdexcept>
using namespace aiverse;
struct Stable {int value=0;};
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
struct FragileResource {
    int value=0;
    static inline bool throwCopy=false;
    explicit FragileResource(int v=0):value(v){}
    FragileResource(const FragileResource& other):value(other.value) {
        if(throwCopy)throw std::runtime_error("resource copy");
    }
    FragileResource& operator=(const FragileResource&)=default;
};
int main() {
    World world;
    Entity first=world.create();
    world.add<Stable>(first,Stable{77});
    world.add<Fragile>(first,1);
    Fragile::throwCopy=true;
    bool caught=false;
    try {world.enableCowSnapshots(true);}
    catch(const std::runtime_error&){caught=true;}
    assert(caught && !world.cowSnapshotsEnabled());
    assert(world.get<Fragile>(first).value==1);
    assert(world.get<Stable>(first).value==77);
    assert(world.sharedCowPageCount()==0);
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
    // Disabling COW must be atomic even if a shared singleton resource
    // cannot be cloned. A failed mode switch must leave snapshots safe.
    {
        World resources;
        resources.setResource<FragileResource>(7);
        resources.enableCowSnapshots(true);
        World saved=resources;
        FragileResource::throwCopy=true;
        bool rejected=false;
        try {resources.enableCowSnapshots(false);}
        catch(const std::runtime_error&) {rejected=true;}
        assert(rejected && resources.cowSnapshotsEnabled());
        assert(static_cast<const World&>(saved).resourceConst<FragileResource>()->value==7);
        FragileResource::throwCopy=false;
        resources.enableCowSnapshots(false);
        assert(!resources.cowSnapshotsEnabled());
        resources.resource<FragileResource>()->value=19;
        assert(static_cast<const World&>(saved).resourceConst<FragileResource>()->value==7);
    }
    std::cout<<"PASS COW page conversion, failed append recovery and shared snapshot isolation\n";
}
