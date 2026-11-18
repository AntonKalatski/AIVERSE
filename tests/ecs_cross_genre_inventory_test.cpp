// Transactional cross-genre inventory in ECS (RTS store + FPS backpack).
#include <aiverselib/genre/EcsInventorySystems.h>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
using namespace aiverse;
namespace lg=aiverse::lib::genre;
static void require(bool ok,const char* msg){if(!ok)throw std::runtime_error(msg);}
struct RtsStoreTag {};
struct FpsBagTag {};

template<class Tag>class Host final:public GameModule {
public:
    Entity stock=kNullEntity,target=kNullEntity;
    std::string name()const override{return "ecs-transactional-inventory";}
    void setup(Simulation& sim,const Json&,const Json&)override {
        auto& world=sim.world();
        stock=world.create();target=world.create();
        world.add<Tag>(stock);
        lg::InventoryComponent first;first.config={4,20};first.items={{1,6}};
        lg::InventoryComponent second;second.config={4,20};
        world.add<lg::InventoryComponent>(stock,first);
        world.add<lg::InventoryComponent>(target,second);
        lg::InventoryCatalog catalog;
        catalog.byType.emplace(1,lib::gameplay::ItemType{1,10,1,0});
        catalog.byType.emplace(2,lib::gameplay::ItemType{2,2,5,0});
        world.setResource<lg::InventoryCatalog>(std::move(catalog));
        world.setResource<lg::InventoryCommandQueue>();
        world.setResource<lg::InventoryResultQueue>();
        require(lg::enqueueInventoryAction(world,{0,lg::InventoryOperation::Transfer,
                   stock,target,{1,4}})==1,"transfer sequence wrong");
        require(lg::enqueueInventoryAction(world,{0,lg::InventoryOperation::Add,
                   target,kNullEntity,{2,3}})==2,"add sequence wrong");
        require(lg::enqueueInventoryAction(world,{0,lg::InventoryOperation::Add,
                   target,kNullEntity,{2,1}})==3,"failed-add sequence wrong");
        lg::addInventoryTransactionSystem(sim,"shared-inventory-transactions");
    }
    void applyCommand(Simulation&,const Command&)override{}
    void hashState(Simulation& sim,Hasher& hash)override {
        const auto& world=sim.world();
        for(Entity entity:{stock,target}) {
            const auto* items=world.tryGetConst<lg::InventoryComponent>(entity);
            require(items!=nullptr,"missing ECS inventory");
            hash.u32(uint32_t(items->items.size()));
            for(const auto& item:items->items) {
                hash.i64(item.type);hash.i64(item.count);
            }
        }
        const auto* commands=world.resourceConst<lg::InventoryCommandQueue>();
        const auto* results=world.resourceConst<lg::InventoryResultQueue>();
        require(commands&&results,"missing inventory resources");
        hash.u64(commands->nextId);
        hash.u32(uint32_t(commands->pending.size()));
        hash.u32(uint32_t(results->events.size()));
        for(const auto& item:results->events) {
            hash.u64(item.id);
            hash.u32(uint32_t(item.decision.reason));
        }
    }
    int result(Simulation&)override{return 0;}
};

template<class Tag>uint64_t exercise(bool cow) {
    auto mod=std::make_unique<Host<Tag>>();auto* host=mod.get();
    Simulation sim(SimConfig{67,20,0,true},std::move(mod));
    sim.setup(Json::object(),Json::object());
    if(cow)sim.world().enableCowSnapshots(true);
    SimSnapshot initial;sim.saveState(initial);
    auto run=[&] {
        sim.step();
        const auto& w=sim.world();
        const auto* from=w.tryGetConst<lg::InventoryComponent>(host->stock);
        const auto* to=w.tryGetConst<lg::InventoryComponent>(host->target);
        const auto* result=w.resourceConst<lg::InventoryResultQueue>();
        const auto* queue=w.resourceConst<lg::InventoryCommandQueue>();
        require(from&&from->items.size()==1&&from->items[0].count==2,
                "source inventory transfer mismatch");
        require(to&&to->items.size()==3&&to->items[0].count==4&&
                to->items[1].count==2&&to->items[2].count==1,
                "destination inventory not transactionally preserved");
        require(result&&result->events.size()==3,"missing ordered transaction results");
        require(result->events[0].decision.reason==lib::gameplay::Reason::Ok&&
                result->events[1].decision.reason==lib::gameplay::Reason::Ok&&
                result->events[2].decision.reason==lib::gameplay::Reason::Capacity,
                "failed transaction did not report capacity error");
        require(queue&&queue->nextId==4&&queue->pending.empty(),
                "command queue was not consumed exactly once");
        return sim.checksum();
    };
    const auto expected=run();
    sim.loadState(initial);
    require(run()==expected,"inventory rollback/replay checksum divergence");
    return expected;
}
int main() {
    const auto expected=exercise<RtsStoreTag>(false);
    require(exercise<FpsBagTag>(false)==expected,"FPS/RTS inventory mismatch");
    require(exercise<RtsStoreTag>(true)==expected,"RTS inventory COW mismatch");
    require(exercise<FpsBagTag>(true)==expected,"FPS inventory COW mismatch");
    std::cout<<"PASS ECS transactional cross-genre inventory and COW rollback\n";
}
