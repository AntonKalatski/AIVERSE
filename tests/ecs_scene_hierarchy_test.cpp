// Universal hierarchy regression (no graphics, no float simulation).
#include "engine/sim/SceneHierarchy.h"
#include <cassert>
#include <iostream>
using namespace aiverse;
static LocalTransform local(int x,int y=0,int z=0){
    LocalTransform t;t.position={Fixed::fromInt(x),Fixed::fromInt(y),Fixed::fromInt(z)};
    return t;
}
static int x(const World& w,Entity e){
    auto* t=w.tryGetConst<WorldTransform>(e);
    assert(t);return int(t->position.x.toInt());
}
static void nested() {
    World w;SceneHierarchy graph;
    Entity a=w.create(),b=w.create(),c=w.create(),root2=w.create();
    assert(graph.attach(w,a,local(10)));
    assert(graph.attach(w,b,local(2)));
    assert(graph.attach(w,c,local(3)));
    assert(graph.attach(w,root2,local(100)));
    assert(graph.setParent(w,b,a));
    assert(graph.setParent(w,c,b));
    assert(graph.update(w)==4);
    assert(x(w,a)==10 && x(w,b)==12 && x(w,c)==15);
    assert(graph.update(w)==0);
    // Deeply nested child dirty while ancestors remain clean.
    assert(graph.setLocal(w,c,local(5)));
    assert(graph.update(w)==1);
    assert(x(w,c)==17);
    assert(graph.setLocal(w,a,local(20)));
    assert(graph.update(w)==3);
    assert(x(w,b)==22&&x(w,c)==27);
    assert(!graph.setParent(w,a,c)); // cycle
    assert(!graph.setParent(w,a,a)); // self
    Entity stale{999999,0};
    assert(!graph.setParent(w,c,stale));
    assert(graph.setParent(w,b,root2));
    assert(graph.update(w)==2);
    assert(x(w,b)==102 && x(w,c)==107);
    // Detach parent from graph before destroying its Entity.
    assert(graph.detach(w,b));
    w.destroy(b);
    assert(!graph.contains(b));
    assert(graph.update(w)==1);
    assert(x(w,c)==5);
    Entity reused=w.create();
    assert(reused.index==b.index&&reused.gen==b.gen+1);
    assert(graph.attach(w,reused,local(7)));
    assert(graph.update(w)==1);
    assert(!graph.setParent(w,reused,b)); // old generation forbidden
}
static void rotateScale() {
    World w;SceneHierarchy g;
    Entity a=w.create(),b=w.create();
    LocalTransform root=local(10);
    root.scale={2_fx,2_fx,2_fx};
    root.rotation={Fixed{},1_fx,Fixed{},Fixed{}}; // 180deg Y
    assert(g.attach(w,a,root));
    assert(g.attach(w,b,local(1)));
    assert(g.setParent(w,b,a));
    assert(g.update(w)==2);
    assert(x(w,b)==8);
}
static void rollback() {
    World w;SceneHierarchy graph;
    Entity a=w.create(),b=w.create();
    graph.attach(w,a,local(1));graph.attach(w,b,local(2));
    graph.setParent(w,b,a);graph.update(w);
    World snapshot=w;
    graph.setLocal(w,a,local(100));graph.update(w);
    assert(x(w,b)==102);
    w.copyFrom(snapshot);
    graph.rebuild(w);
    assert(graph.update(w)==2);
    assert(x(w,b)==3);
    assert(graph.update(w)==0);
    // Bad cycle injected by external ECS producer is repaired on rebuild.
    w.add<Parent>(a,Parent{b});
    graph.rebuild(w);
    assert(graph.update(w)==2);
    assert(!w.has<Parent>(b));
}
static void deep() {
    World w;SceneHierarchy graph;
    Entity first=kNullEntity,previous=kNullEntity;
    constexpr int n=3000;
    for(int i=0;i<n;++i) {
        Entity e=w.create();
        assert(graph.attach(w,e,local(1)));
        if(previous.valid())assert(graph.setParent(w,e,previous));
        if(!first.valid())first=e;
        previous=e;
    }
    assert(graph.update(w)==n);
    assert(x(w,previous)==n);
    assert(graph.update(w)==0);
    graph.setLocal(w,first,local(2));
    assert(graph.update(w)==n);
    assert(x(w,previous)==n+1);
}
int main(){
    nested();rotateScale();rollback();deep();
    std::cout<<"PASS hierarchy parent links, deep updates, dirty branches, cycles, rollback and generations\n";
}
