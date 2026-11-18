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
    assert(graph.lastVisited()==0); // no root scan on a clean frame
    // Deeply nested child dirty while ancestors remain clean.
    assert(graph.setLocal(w,c,local(5)));
    assert(graph.update(w)==1);
    assert(graph.lastVisited()==3); // only root -> parent -> leaf visited
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
static void nonuniformScaleShear() {
    World w;SceneHierarchy g;
    Entity parent=w.create(),child=w.create();
    LocalTransform p;
    p.scale={2_fx,1_fx,1_fx};
    LocalTransform l;
    // 45 degrees around Z: combining this rotation with parent X-scale
    // produces shear that cannot be represented by quaternion + scale alone.
    l.rotation.z=Fixed::fromRatio(382683,1000000);
    l.rotation.w=Fixed::fromRatio(923880,1000000);
    assert(g.attach(w,parent,p));
    assert(g.attach(w,child,l));
    assert(g.setParent(w,child,parent));
    assert(g.update(w)==2);
    const auto* world=w.tryGetConst<WorldTransform>(child);
    assert(world);
    auto result=hierarchy_detail::transformPoint(world->affine,Vec3{1_fx,Fixed{},Fixed{}});
    assert(fxAbs(result.x-Fixed::fromRatio(1414214,1000000))<Fixed::fromRatio(1,100));
    assert(fxAbs(result.y-Fixed::fromRatio(707107,1000000))<Fixed::fromRatio(1,100));
    // The transformed basis vectors are non-orthogonal: real affine shear.
    const auto dot=world->affine.at(0,0)*world->affine.at(0,1)
                  +world->affine.at(1,0)*world->affine.at(1,1);
    assert(fxAbs(dot)>Fixed::fromRatio(1,2));
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
static void cowSnapshotHierarchy() {
    World w;
    SceneHierarchy graph;
    Entity parent=w.create(),child=w.create();
    assert(graph.attach(w,parent,local(10)));
    assert(graph.attach(w,child,local(3)));
    assert(graph.setParent(w,child,parent));
    graph.update(w);
    w.enableCowSnapshots(true);
    World snapshot=w;
    assert(graph.setLocal(w,parent,local(70)));
    assert(graph.update(w)==2);
    assert(x(w,child)==73);
    assert(static_cast<const World&>(snapshot).tryGetConst<WorldTransform>(child)
           ->position.x==13_fx);
    w.copyFrom(snapshot);
    graph.rebuild(w);
    assert(graph.update(w)==2);
    assert(x(w,child)==13);
    assert(graph.update(w)==0);
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
    assert(graph.lastVisited()==0);
    graph.setLocal(w,previous,local(4));
    assert(graph.update(w)==1);
    assert(graph.lastVisited()==n); // ancestor path, one written leaf
    assert(x(w,previous)==n+3);
    graph.setLocal(w,first,local(2));
    assert(graph.update(w)==n);
    assert(x(w,previous)==n+4);
}
int main(){
    nested();rotateScale();nonuniformScaleShear();rollback();cowSnapshotHierarchy();deep();
    std::cout<<"PASS hierarchy parent links, deep updates, dirty branches, cycles, rollback and generations\n";
}
