// GameCore on world-space maps: the actor systems (engine/actor/ActorModule.h) run the match.
// One host for every project; grid systems are not set up on these maps (docs/no-genre-split.md).
#include "GameCore.h"
#include "../actor/ActorModule.h"

namespace aiverse::game {
void GameCore::setupActor(Simulation& sim, const Json& rules, const Json& map) {
    actor_ = std::make_shared<actor::ActorHost>();
    actor_->setup(sim, rules, map);
}
void GameCore::actorCommand(Simulation& sim, const Command& cmd) { actor_->applyCommand(sim, cmd); }
void GameCore::actorHash(Simulation& sim, Hasher& h) { actor_->hashState(sim, h); }
bool GameCore::actorSerialize(Simulation& sim, Archive& a) { return actor_->serializeState(sim, a); }
int GameCore::actorResult(Simulation& sim) { return actor_->result(sim); }
bool GameCore::actorReady(Simulation& sim) { return actor_->readyToStep(sim); }
bool GameCore::readyToStep(Simulation& sim) { return actor_ ? actorReady(sim) : true; }
}

namespace aiverse::actor {
ActorHost* tryHost(Simulation& sim) {
    auto* core = dynamic_cast<game::GameCore*>(&sim.module());
    return core ? core->actor() : nullptr;
}
ActorHost& host(Simulation& sim) {
    if (auto* a = tryHost(sim)) return *a;
    throw std::logic_error("actor::host: the simulation does not run a world-space map");
}
const ActorHost& host(const Simulation& sim) { return host(const_cast<Simulation&>(sim)); }
}
