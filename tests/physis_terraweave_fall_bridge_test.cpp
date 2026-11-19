// Stage 87 optional REAL PHYSIS + TerraWeave + Box3D composition test.
// CMake registers this ONLY after TerraWeave headers are present and physics
// is enabled. It is not run on the PHYSIS-only branch.
#include "engine/world/PhysisTerraWeaveFallBridge.h"
#include <cmath>
#include <iostream>
#include <string>
#include <optional>
#include <stdexcept>

using namespace aiverse;
namespace pn = aiverse::physis;
namespace tw = aiverse::terraweave;
namespace host = aiverse::world;

static void require(bool b,const char* msg) {
    if(!b)throw std::runtime_error(msg);
}

int main() {
    World world;
    pn::Settings cfg;
    cfg.width=cfg.height=1;
    pn::initialize(world,cfg);
    world.setResource<host::NatureFallHostState>(host::NatureFallHostState{});
    tw::TerraWeaveAuthorityResource authority;
    tw::AnchoredPlant tree;
    tree.anchor.plantKey=551;
    tree.anchor.speciesId=14;
    tree.anchor.xCm=-500;
    tree.anchor.zCm=1200;
    tree.anchor.plantedHeightCm=200;
    tree.anchor.roots=tw::defaultRootProfile(tw::PlantHabit::Tree);
    authority.rooted.push_back(tree);
    world.setResource<tw::TerraWeaveAuthorityResource>(authority);

    const Entity proxy=world.create();
    require(host::bindNatureFallPlant(world,proxy,{14,551}),
            "failed authoritative Terra root binding");
    pn::NatureFractureOutboxComponent out;
    out.climateTick=17;
    out.eventKey=pn::natureFractureEventKey(proxy,17);
    out.delivery=pn::NatureFractureDelivery::Pending;
    world.add<pn::NatureFractureOutboxComponent>(proxy,out);
    const World preCommit=world;

    auto* saved=world.resource<tw::TerraWeaveAuthorityResource>();
    saved->earth.revision=47;
    const auto report=host::dispatchPhysisTerraWeaveFalls(world);
    require(report.committed==1 && report.pendingAtEnd==0,
            "PHYSIS event was not staged and acknowledged");
    const auto* ledger=world.resourceConst<host::NatureFallHostState>();
    require(ledger && ledger->records.size()==1 &&
            ledger->records[0].plant.plantKey==551 &&
            ledger->records[0].soilRevision==47,
            "Terra root identity or soil revision lost");
    const auto* fallenRoot =
        world.resourceConst<tw::TerraWeaveAuthorityResource>();
    require(fallenRoot->rooted[0].fallen &&
            fallenRoot->stateRevision==1,
            "wind fracture did not atomically mark canonical rooted plant fallen");

    physics::Scene scene;
    tw::FallenPlantPhysics bodies(scene);
    host::NatureFallBox3DReconciler reconciler(scene);
    auto spec=[](std::uint32_t species)
            ->std::optional<tw::FallenPlantBodySpec> {
        if(species!=14)return std::nullopt;
        return tw::FallenPlantBodySpec{};
    };
    const auto first=reconciler.reconcile(world,bodies,spec);
    require(first.spawned==1 && bodies.count()==1 && scene.bodyCount()==1,
            "Box3D falling tree was not created");
    const auto again=reconciler.reconcile(world,bodies,spec);
    require(again.reused==1 && bodies.count()==1 && scene.bodyCount()==1,
            "duplicate PHYSIS event created second Box3D body");
    scene.step();
    require(bodies.body({14,551})!=physics::kNoBody,
            "fallen tree body missing after physical step");
    // The host can persist float Box3D motion separately from lockstep ECS.
    const auto midFall=reconciler.captureFrame(bodies);
    require(midFall.poses.size()==1,"mid-fall body was not captured");
    std::string poseBytes;
    Archive poseWriter(&poseBytes);
    auto serialized=midFall;
    host::io(poseWriter,serialized);
    require(poseWriter.ok(),"physics checkpoint writer failed");
    host::NatureFallPhysicsFrame loadedFrame;
    Archive poseReader(poseBytes);
    host::io(poseReader,loadedFrame);
    require(poseReader.ok() && loadedFrame.poses.size()==1,
            "mid-fall pose checkpoint did not deserialize");
    scene.step();

    world=preCommit; // authoritative rollback before event was staged
    require(!world.resourceConst<tw::TerraWeaveAuthorityResource>()
                 ->rooted[0].fallen,
            "rollback retained the PHYSIS-caused Terra root collapse");
    const auto gone=reconciler.reconcile(world,bodies,spec);
    require(gone.removed==1 && bodies.count()==0 && scene.bodyCount()==0,
            "rollback retained non-authoritative Box3D body");

    // A replay with the exact same event ID must create ONE new derived body.
    world.resource<tw::TerraWeaveAuthorityResource>()->earth.revision=47;
    const auto replay=host::dispatchPhysisTerraWeaveFalls(world);
    require(replay.committed==1,"replayed PHYSIS fall did not stage");
    const auto resurrected=reconciler.reconcile(world,bodies,spec);
    require(resurrected.spawned==1 && scene.bodyCount()==1,
            "rollback replay failed to reconstruct one Box3D body");
    require(reconciler.restoreFrame(loadedFrame,bodies),
            "host mid-fall Box3D pose/velocity restore failed");
    const auto restoredPos=scene.position(bodies.body({14,551}));
    require(std::fabs(restoredPos.y-midFall.poses[0].position.y)<0.001f,
            "Box3D restored pose diverged from captured host checkpoint");

    // Repair in ONE ECS authority transaction: reset PHYSIS stress,
    // rearm event tombstone, retire journal record and restore rooted tree.
    pn::NatureStressOutputComponent stress;
    stress.fractured=true;
    stress.accumulatedDamage=1_fx;
    world.add<pn::NatureStressOutputComponent>(proxy,stress);
    world.resource<pn::State>()->tick=18;
    auto replanted=world.resourceConst<tw::TerraWeaveAuthorityResource>()
                    ->rooted[0];
    replanted.fallen=false;
    require(host::repairPhysisTerraWeavePlant(
                world,proxy,out.eventKey,17,replanted),
            "transactional repair/replant failed");
    require(!host::repairPhysisTerraWeavePlant(
                world,proxy,out.eventKey,17,replanted),
            "duplicate repair unexpectedly accepted");
    const auto* afterRepair=world.resourceConst<host::NatureFallHostState>();
    require(afterRepair->records.size()==1 &&
            !afterRepair->records[0].active &&
            !world.resourceConst<tw::TerraWeaveAuthorityResource>()
                ->rooted[0].fallen,
            "repair did not preserve ledger tombstone and rooted standing state");
    require(reconciler.reconcile(world,bodies,spec).removed==1 &&
            bodies.count()==0 && scene.bodyCount()==0,
            "repair did not remove the host-owned falling body");

    // A later wind fracture must get a NEW event and recreate one body,
    // not consume the acknowledged first-cycle tombstone.
    world.resource<pn::State>()->tick=19;
    auto& newStress=world.get<pn::NatureStressOutputComponent>(proxy);
    newStress.valid=true;
    newStress.fractured=true;
    newStress.newlyFractured=true;
    newStress.sampledClimateTick=19;
    newStress.accumulatedDamage=1_fx;
    pn::advanceNatureFractureOutbox(world);
    const auto second=pn::pendingNatureFractures(world);
    require(second.size()==1 && second[0].eventKey!=out.eventKey,
            "replanted tree reused the first fracture identity");
    require(host::dispatchPhysisTerraWeaveFalls(world).committed==1,
            "replanted tree's second fracture did not commit");
    require(world.resourceConst<host::NatureFallHostState>()
                ->records.size()==2,
            "second fracture did not preserve prior tombstone");
    require(reconciler.reconcile(world,bodies,spec).spawned==1 &&
            scene.bodyCount()==1,
            "refractured tree did not create exactly one body");

    // Shrubs use the same rooted authority/Box3D path; cosmetic grass is
    // intentionally excluded from individual root and rigid-body records.
    tw::AnchoredPlant shrub;
    shrub.anchor.plantKey=552;
    shrub.anchor.speciesId=15;
    shrub.anchor.roots=tw::defaultRootProfile(tw::PlantHabit::Shrub);
    world.resource<tw::TerraWeaveAuthorityResource>()
        ->rooted.push_back(shrub);
    const Entity shrubProxy=world.create();
    require(host::bindNatureFallPlant(world,shrubProxy,{15,552}),
            "shrub binding failed");
    pn::NatureFractureOutboxComponent shrubOut;
    shrubOut.climateTick=19;
    shrubOut.eventKey=pn::natureFractureEventKey(shrubProxy,19);
    shrubOut.delivery=pn::NatureFractureDelivery::Pending;
    world.add<pn::NatureFractureOutboxComponent>(shrubProxy,shrubOut);
    require(host::dispatchPhysisTerraWeaveFalls(world).committed==1,
            "shrub PHYSIS fracture failed");
    auto shrubSpec=[](std::uint32_t species)
            ->std::optional<tw::FallenPlantBodySpec> {
        if(species==14)return tw::FallenPlantBodySpec{};
        if(species!=15)return std::nullopt;
        tw::FallenPlantBodySpec cfg;
        cfg.heightCm=140;
        cfg.radiusCm=12;
        return cfg;
    };
    require(reconciler.reconcile(world,bodies,shrubSpec).spawned==1 &&
            scene.bodyCount()==2,
            "shrub did not create one additional Box3D body");
    tw::AnchoredPlant grass;
    grass.anchor.plantKey=553;
    grass.anchor.speciesId=16;
    grass.anchor.roots=tw::defaultRootProfile(tw::PlantHabit::Grass);
    require(!host::makeNatureFallRecord(second[0],grass,47),
            "cosmetic grass incorrectly acquired a rooted rigid-body event");

    std::cout<<"PHYSIS/TerraWeave/Box3D fall, repair and rollback PASS\n";
}
