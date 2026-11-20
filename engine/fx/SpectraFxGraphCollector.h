// SpectraFX: opt-in read-only Simulation event collector -> canonical graph
// playback. Use this after the gameplay module registered its systems.
// A single producer must own each event stream: do not install alongside
// another visual collector consuming identical cues.
#pragma once

#include <functional>
#include <utility>
#include "../sim/Simulation.h"
#include "SpectraFxEventMap.h"
#include "SpectraFxGraphPlayback.h"

namespace aiverse::fx {

// Register after gameplay systems so events are already emitted. The
// ".collect" suffix also lets Simulation::present() drive network mirrors.
// The map, playback runtime and translator must outlive Simulation itself.
template<class Translator>
void installSpectraFxGraphCollector(Simulation& simulation,
                                    SpectraFxGraphPlayback& playback,
                                    Translator translator) {
    simulation.addSystem("spectrafx.graph.collect",
        [&playback, translator=std::move(translator)](Simulation& sim) mutable {
            for(const auto& event:sim.events()) {
                SpectraFxCue cue;
                if(translator(event,cue))
                    playback.dispatch(cue);
            }
        });
}

// Convenience for real one-shot SimEvent types with authored mapping.
// Long-running effects must instead supply a generation-safe key translator;
// the zero-key route deliberately cannot Move/Stop/Kill an attached effect.
inline void installSpectraFxOneShotGraphCollector(
    Simulation& simulation, SpectraFxGraphPlayback& playback,
    const SpectraFxEventMap& eventMap) {
    installSpectraFxGraphCollector(simulation,playback,
        [&eventMap](const SimEvent& event,SpectraFxCue& cue) {
            return eventMap.translateOneShot(event,cue);
        });
}

// Presentation owner frame order:
//   1. Simulation::step() or ::present() invokes the registered collector.
//   2. ParticleSystem::setView(camera, viewProj, dimensions)
//   3. ParticleSystem::update(realDelta)
//   4. playback.prune()
//   5. scene.render() then ParticleSystem::render(fog)
// On rollback, game reset or scene unload call playback.reset() and ensure
// stale event streams are not replayed as newly confirmed presentation events.
} // namespace aiverse::fx
