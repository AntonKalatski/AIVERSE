// TerraWeave automatic residency planner + tree trunk colliders (CPU only).
#include "engine/world/TerraWeaveStreaming.h"
#include "engine/world/TerraWeaveTreeColliders.h"
#ifdef AIVERSE_TERRAWEAVE_TEST_WITH_PHYSICS
#include "engine/world/TerraWeaveTreeCollidersBox3D.h"
#endif
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <set>
#include <stdexcept>
#include <vector>

using namespace aiverse;
using namespace aiverse::terraweave;

namespace {
template<class F> bool throws(F f){try{f();}catch(const std::exception&){return true;}return false;}

void plannerBasics(){
    TerraStreamingSettings s;
    s.tileSizeCm=12800; s.collisionRadiusCm=19200; s.hysteresisCm=3200; s.maxPinnedTiles=64;
    TerraWeaveStreamingPlanner p(s);
    const TerraStreamObserver cam{6400,6400,0,0};
    auto plan=p.update(std::span(&cam,1));
    assert(!plan.pinnedCells.empty());
    assert(plan.pinnedCells.front()==(world::Cell{0,0,0})); // tile under the observer first
    assert(plan.pinnedChanged && plan.entered==plan.pinnedCells.size() && plan.left==0);
    // Every tile within the radius is present; disc of 192 m around a 128 m tile centre.
    for(int z=-1;z<=1;++z)for(int x=-1;x<=1;++x)
        assert(std::find(plan.pinnedCells.begin(),plan.pinnedCells.end(),world::Cell{x,0,z})!=plan.pinnedCells.end());
    // Same input -> identical plan, no change flags.
    auto again=p.update(std::span(&cam,1));
    assert(again.pinnedCells==plan.pinnedCells);
    assert(!again.pinnedChanged && !again.grassMoved);
    assert(plan.grassValid && plan.grassArea.minX<6400 && plan.grassArea.maxX>6400);
    // Negative coordinates: floor division.
    assert(p.tileAt(-1,-1)==(world::Cell{-1,0,-1}));
    assert(p.tileAt(12799,0)==(world::Cell{0,0,0}) && p.tileAt(12800,0)==(world::Cell{1,0,0}));
}

void plannerHysteresis(){
    TerraStreamingSettings s;
    s.tileSizeCm=10000; s.collisionRadiusCm=10000; s.hysteresisCm=4000; s.maxPinnedTiles=128;
    TerraWeaveStreamingPlanner p(s);
    TerraStreamObserver o{5000,5000,0,0};
    const auto first=p.update(std::span(&o,1)).pinnedCells;
    const bool hadEast=std::find(first.begin(),first.end(),world::Cell{1,0,0})!=first.end();
    assert(hadEast);
    // Move 3 tiles west in small steps: tile {1,0} leaves only after radius+hysteresis.
    std::set<int> leftAt;
    for(int step=1;step<=60;++step){
        o.xCm=5000-step*500;
        const auto plan=p.update(std::span(&o,1));
        const bool east=std::find(plan.pinnedCells.begin(),plan.pinnedCells.end(),world::Cell{1,0,0})!=plan.pinnedCells.end();
        if(!east){leftAt.insert(step);break;}
    }
    assert(!leftAt.empty());
    // Tile {1,0} starts at x=10000. It must stay while distance <= 14000 cm,
    // i.e. observer x >= -4000 -> step <= 18; leave at step 19 (x=-4500).
    assert(*leftAt.begin()==19);
    // Moving back: it re-enters only when inside the plain radius (x>=0, step 10).
    int enteredAt=-1;
    for(int step=18;step>=0;--step){
        o.xCm=5000-step*500;
        const auto plan=p.update(std::span(&o,1));
        if(std::find(plan.pinnedCells.begin(),plan.pinnedCells.end(),world::Cell{1,0,0})!=plan.pinnedCells.end()){enteredAt=step;break;}
    }
    assert(enteredAt==10);
}

void plannerMultiObserverAndBudget(){
    TerraStreamingSettings s;
    s.tileSizeCm=10000; s.collisionRadiusCm=5000; s.hysteresisCm=0; s.maxPinnedTiles=12;
    TerraWeaveStreamingPlanner a(s), b(s);
    std::vector<TerraStreamObserver> obs{{5000,5000,0,0},{205000,5000,0,1},{5000,-195000,3000,2}};
    std::vector<TerraStreamObserver> rev(obs.rbegin(),obs.rend());
    const auto pa=a.update(obs), pb=b.update(rev);
    assert(pa.pinnedCells==pb.pinnedCells);           // observer order independent
    assert(pa.grassArea.minX==pb.grassArea.minX && pa.grassArea.minZ==pb.grassArea.minZ);
    assert(pa.primaryXCm==5000 && pa.primaryZCm==5000); // lowest priority value = primary
    assert(std::find(pa.pinnedCells.begin(),pa.pinnedCells.end(),world::Cell{20,0,0})!=pa.pinnedCells.end());
    assert(std::find(pa.pinnedCells.begin(),pa.pinnedCells.end(),world::Cell{0,0,-20})!=pa.pinnedCells.end());
    // Priority ordering: all priority-0 tiles come before priority-1 tiles.
    assert(pa.pinnedCells.front()==(world::Cell{0,0,0}));
    // Budget overflow keeps the most important tiles and reports the rest.
    s.maxPinnedTiles=3; s.collisionRadiusCm=15000;
    TerraWeaveStreamingPlanner c(s);
    const auto pc=c.update(obs);
    assert(pc.pinnedCells.size()==3 && pc.overflowTiles>0);
    assert(pc.pinnedCells.front()==(world::Cell{0,0,0}));
    // Validation.
    assert(throws([]{TerraStreamingSettings bad; bad.maxPinnedTiles=513; TerraWeaveStreamingPlanner x(bad);}));
    assert(throws([]{TerraStreamingSettings bad; bad.grassDeadZoneCm=bad.grassRadiusCm+1; TerraWeaveStreamingPlanner x(bad);}));
    TerraWeaveStreamingPlanner v;
    TerraStreamObserver far{kWorldLimitCm,0,0,0};
    assert(throws([&]{v.update(std::span(&far,1));}));
    TerraStreamObserver huge{0,0,100000000,0};
    assert(throws([&]{v.update(std::span(&huge,1));}));
}

void plannerGrassDeadZone(){
    TerraStreamingSettings s; s.grassRadiusCm=6400; s.grassDeadZoneCm=1600; s.grassSnapCm=800;
    TerraWeaveStreamingPlanner p(s);
    TerraStreamObserver o{0,0,0,0};
    const auto a=p.update(std::span(&o,1));
    o.xCm=1500;                                   // inside dead zone: rectangle stays
    const auto b=p.update(std::span(&o,1));
    assert(!b.grassMoved && b.grassArea.minX==a.grassArea.minX);
    o.xCm=1700;                                   // outside: re-centred on the snap grid
    const auto c=p.update(std::span(&o,1));
    assert(c.grassMoved && c.grassArea.minX==1600-6400 && c.grassArea.maxX==1600+6400);
}

void trunkColliders(){
    ForestSceneSettings settings;
    settings.chunkSizeCm=2400; settings.radiusChunks=1; settings.maxResidentChunks=9;
    settings.scatter.spacingCm=450; settings.scatter.densityPermille=1000;
    settings.terrain.baseHeightCm=0; settings.terrain.reliefCm=0;
    TerraWeaveForestScene forest(settings);
    TreeProfile profile;
    profile.speciesId=77; profile.primaryBranches=5;
    FoliageSpecies placement; placement.id=77;
    placement.biomeMask=kLandBiomes|biomeBit(Biome::Ocean); placement.maxSlopePermille=100000;
    forest.addSpecies(profile,placement,7);
    forest.setHeightSource([](int64_t,int64_t)->int32_t{return 250;});
    forest.streamAt({1200,1200});
    const TreeTrunkShape shape=treeTrunkShape(profile,7);
    assert(shape.radiusCm==profile.trunkRadiusCm);
    assert(shape.heightCm>=profile.minHeightCm && shape.heightCm<=profile.maxHeightCm);
    assert(treeTrunkShape(profile,7)==shape);     // deterministic per prototype seed
    TreeTrunkShapes shapes{{77,shape}};
    const RectCm area{0,0,2400,2400};
    TreeTrunkColliderSettings cs;
    const auto trunks=collectTreeTrunkColliders(forest,area,shapes,cs);
    assert(!trunks.empty());
    for(size_t i=1;i<trunks.size();++i)assert(trunks[i-1].tree<trunks[i].tree);
    const auto trees=forest.treesInRect(area);
    for(const auto& t:trunks){
        assert(area.contains(t.xCm,t.zCm));
        assert(t.baseYCm==250-cs.sinkCm);
        const auto tree=std::find_if(trees.begin(),trees.end(),[&](const FoliageInstance& f){return f.key==t.tree.key;});
        assert(tree!=trees.end());
        assert(t.radiusCm==std::max<int64_t>(cs.minRadiusCm,int64_t(shape.radiusCm)*tree->scalePermille/1000));
    }
    // Unknown species -> no colliders; a thinner setting shrinks the radius.
    assert(collectTreeTrunkColliders(forest,area,{},cs).empty());
    TreeTrunkColliderSettings thin=cs; thin.radiusPermille=500;
    const auto thinTrunks=collectTreeTrunkColliders(forest,area,shapes,thin);
    assert(thinTrunks.size()==trunks.size() && thinTrunks[0].radiusCm<trunks[0].radiusCm);

    // SpatialWorld: a sweep towards a trunk stops at its surface; IDs stable.
    world::SpatialWorld spatial(4);
    TreeTrunkCollisionPublisher pub;
    const world::Cell tile{0,0,0};
    assert(pub.publish(spatial,tile,trunks,1000,4096)==trunks.size());
    assert(pub.colliderCount()==trunks.size());
    const auto& t0=trunks.front();
    const Fixed y=Fixed::fromRatio(int64_t(t0.baseYCm)+cs.sinkCm+100,100);  // 1 m above ground
    const Vec3 start{Fixed::fromRatio(t0.xCm-300,100),y,Fixed::fromRatio(t0.zCm,100)};
    const Vec3 delta{Fixed::fromInt(3),{},{}};
    const auto hit=spatial.cast(start,delta);
    assert(hit.hit);
    const auto owner=pub.slotOf(hit.id);
    assert(owner && owner->first==tile);
    // The hit collider is a trunk whose box the ray enters before 3 m.
    assert(hit.fraction<Fixed::fromInt(1));
    // Republish (e.g. after a harvest removed the first tree): IDs re-pack, old ones gone.
    std::vector<TreeTrunkCollider> fewer(trunks.begin()+1,trunks.end());
    pub.publish(spatial,tile,fewer,1000,4096);
    assert(!spatial.colliders().contains(1000+uint32_t(trunks.size()-1)));
    assert(spatial.colliders().contains(1000));
    // Overlapping ranges of another tile and foreign IDs are rejected untouched.
    assert(throws([&]{pub.publish(spatial,{1,0,0},trunks,1500,4096);}));
    world::Collider actor{20000,{{Fixed::fromInt(500),{},{}},{Fixed::fromInt(501),Fixed::fromInt(1),Fixed::fromInt(1)}}};
    spatial.add(actor);
    assert(throws([&]{pub.publish(spatial,{1,0,0},trunks,20000,4096);}));
    assert(spatial.colliders().contains(20000));
    std::vector<TreeTrunkCollider> unsorted(trunks.rbegin(),trunks.rend());
    if(unsorted.size()>1)assert(throws([&]{pub.publish(spatial,{2,0,0},unsorted,30000,4096);}));
    assert(throws([&]{pub.publish(spatial,{2,0,0},trunks,30000,uint32_t(trunks.size()-1));}));
    // retainOnly drops tiles that left the streaming plan.
    pub.publish(spatial,{3,0,0},std::span(trunks.data(),1),40000,16);
    const world::Cell keep[]{tile};
    pub.retainOnly(spatial,keep);
    assert(!spatial.colliders().contains(40000) && spatial.colliders().contains(1000));
    pub.shutdown(spatial);
    assert(pub.colliderCount()==0 && spatial.colliders().size()==1);

    // Removing a tree from the presentation forest removes its collider too.
    const auto removed=trunks.front().tree;
    forest.removeTree(removed);
    const auto after=collectTreeTrunkColliders(forest,area,shapes,cs);
    assert(after.size()+1==trunks.size());
    assert(std::none_of(after.begin(),after.end(),[&](const auto& t){return t.tree==removed;}));

#ifdef AIVERSE_TERRAWEAVE_TEST_WITH_PHYSICS
    const auto body=makeTreeTrunkBox3DBody(trunks.front(),7);
    assert(body.shape==physics::ShapeKind::Capsule && body.motion==physics::Motion::Static);
    assert(body.radius>0 && body.halfHeight>0 && body.userData==7);
    physics::Scene scene;
    TreeTrunkBox3DPublisher box;
    assert(box.publish(scene,tile,trunks));
    assert(box.bodyCount()==trunks.size());
    scene.step();
    const auto ray=scene.raycast({float(start.x.toDouble()),float(start.y.toDouble()),float(start.z.toDouble())},{3,0,0});
    assert(ray.body!=physics::kNoBody);
    box.unload(scene,tile);
    assert(box.bodyCount()==0);
    std::puts("terraweave_streaming: Box3D trunk capsules ok");
#endif
    std::printf("terraweave_streaming: %zu trunks, first r=%dcm h=%dcm\n",trunks.size(),trunks[0].radiusCm,trunks[0].heightCm);
}
}

int main(){
    plannerBasics();
    plannerHysteresis();
    plannerMultiObserverAndBudget();
    plannerGrassDeadZone();
    trunkColliders();
    std::puts("terraweave_streaming: ok");
    return 0;
}
