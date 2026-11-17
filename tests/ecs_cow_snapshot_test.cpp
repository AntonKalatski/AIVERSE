// c++ -std=c++20 -O2 -pthread -I. tests/ecs_cow_snapshot_test.cpp -o /tmp/ecs-cow-test
#include "engine/core/ECS.h"
#include <cassert>
#include <cstdint>
#include <iostream>
using namespace aiverse;
struct Position {int64_t x=0;};
struct Health {int64_t hp=100;};
struct Resource {int64_t gold=1;};
static void check(World& w,Entity e,int64_t x,int64_t hp,int64_t gold) {
    assert(w.alive(e));
    assert(w.get<Position>(e).x==x);
    assert(w.get<Health>(e).hp==hp);
    assert(w.resource<Resource>()->gold==gold);
}
int main() {
    World w;
    w.enableCowSnapshots(true);
    Entity a=w.create(),b=w.create();
    w.add<Position>(a,Position{10});w.add<Position>(b,Position{20});
    w.add<Health>(a,Health{100});w.add<Health>(b,Health{200});
    w.setResource<Resource>(Resource{7});
    World s1=w;
    assert(w.sharedPoolCount()==2);
    assert(w.sharedResourceCount()==1);
    w.get<Position>(a).x=30;
    assert(w.sharedPoolCount()==1);
    assert(s1.sharedPoolCount()==1);
    w.get<Health>(b).hp=300;
    w.resource<Resource>()->gold=8;
    check(s1,a,10,100,7);
    check(w,a,30,100,8);
    World s2=w;
    w.each<Position>([](Entity,Position& p){p.x+=1;});
    w.destroy(a);
    Entity recycled=w.create();
    assert(recycled.index==a.index && recycled.gen==a.gen+1);
    w.add<Position>(recycled,Position{400});
    w.add<Health>(recycled,Health{500});
    assert(s1.alive(a)&&s2.alive(a));
    check(s2,a,30,100,8);
    assert(!w.alive(a));
    assert(s1.get<Position>(b).x==20);
    assert(s2.get<Health>(b).hp==300);
    w.copyFrom(s1);
    check(w,a,10,100,7);
    w.get<Position>(a).x=42;
    check(s1,a,10,100,7);
    // Re-enable legacy deep-copy mode after detaching shared contents.
    w.enableCowSnapshots(false);
    World legacy=w;
    w.get<Position>(a).x=77;
    assert(legacy.get<Position>(a).x==42);
    assert(!w.cowSnapshotsEnabled());
    std::cout<<"PASS COW world snapshots, resource detachment, generations, rollback, legacy fallback\n";
}
