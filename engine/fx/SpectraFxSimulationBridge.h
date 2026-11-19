// SpectraFX optional Simulation adapter. Purely read-only presentation output.
//
// Usage (after the module has declared its gameplay systems):
//   auto bridge = std::make_shared<fx::SpectraFxCueBridge>();
//   bridge->bind(kExplosion, {"explosion"});
//   fx::installSpectraFxCollector(sim, *bridge, queue,
//       [](const SimEvent& ev, fx::SpectraFxCue& cue) {
//           if (ev.type != kExplosionEvent) return false;
//           cue.id = kExplosion;
//           cue.position = fx::spectraFxPosition(ev.pos);
//           return true;
//       });
// On the renderer thread, queue.flush(particles) BEFORE particles.update(dt).
// Call queue.reset(particles) on new match/rollback discontinuity.
// The collector stores bridge, queue and translator by reference/value;
// they MUST outlive the Simulation registration.
#pragma once

#include <utility>
#include "../sim/Simulation.h"
#include "SpectraFxCueBridge.h"

namespace aiverse::fx {

// Simulation's Fixed -> float conversion is presentation-only. No floats are
// returned to authoritative systems or included in state/checksum/save.
inline float3 spectraFxPosition(const aiverse::Vec3& pos) {
    return {static_cast<float>(pos.x.toDouble()),
            static_cast<float>(pos.y.toDouble()),
            static_cast<float>(pos.z.toDouble())};
}

// Register after gameplay systems: the SimEvent buffer exists for a tick and
// may be replaced by Simulation::present() for remote mirrored games.
// ".collect" suffix makes this collector eligible for Simulation::present().
// No mutation of sim.world(), RNG, game state or event buffer occurs here.
template<class Translator>
void installSpectraFxCollector(aiverse::Simulation& sim,
                               SpectraFxCueBridge& bridge,
                               SpectraFxCommandQueue& queue,
                               Translator translator) {
    sim.addSystem("spectrafx.collect",
        [&bridge, &queue, translator = std::move(translator)]
        (aiverse::Simulation& simulation) mutable {
            bridge.translate(simulation.events(), translator, queue);
        });
}

} // namespace aiverse::fx
