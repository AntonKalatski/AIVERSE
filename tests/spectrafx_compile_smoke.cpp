// Compile-only SpectraFX API smoke: every header can be included from a
// normal native game module without relying on editor- or RTS-only includes.
// Built as an OBJECT target; it deliberately does not require an RHI device.
#include "engine/fx/SpectraFX.h"
#include "engine/fx/SpectraFxCueBridge.h"
#include "engine/fx/SpectraFxCueInbox.h"
#include "engine/fx/SpectraFxCommandQueue.h"
#include "engine/fx/SpectraFxSimulationBridge.h"
#include "engine/fx/SpectraFxEmitterEvents.h"
#include "engine/fx/SpectraFxEmitterController.h"
#include "engine/fx/SpectraFxEmitterRuntime.h"
#include "engine/fx/SpectraFxEmitterAsset.h"
#include "engine/fx/SpectraFxGraph.h"
#include "engine/fx/SpectraFxGraphAsset.h"
#include "engine/fx/SpectraFxGraphRegistry.h"
#include "engine/fx/SpectraFxGraphPlayback.h"
#include "engine/fx/SpectraFxGpuSpawnScheduler.h"
#include "engine/fx/SpectraFxGpuLod.h"
#include "engine/fx/SpectraFxGpuBudgetGovernor.h"
#include "engine/fx/SpectraFxGpuParticleSlots.h"
#include "engine/fx/SpectraFxGpuCompaction.h"
#include "engine/fx/SpectraFxGpuAlphaSort.h"
#include "engine/fx/SpectraFxGpuBillboardRenderer.h"
#include "engine/fx/SpectraFxGpuImpactFeedback.h"
#include "engine/fx/SpectraFxHybridEmitter.h"
#include "engine/fx/SpectraFxGpuEmitterAsset.h"
#include "engine/fx/SpectraFxGpuEmitterLibrary.h"
#include "engine/fx/SpectraFxHybridPlayback.h"
#include "engine/fx/SpectraFxGpuRoutes.h"
#include "engine/fx/SpectraFxSceneHost.h"

// The names below must be valid public API types in a standalone TU.
namespace aiverse::fx {
void spectraFxCompileSmoke() {
    SpectraFxCueInbox inbox;
    SpectraFxGpuBudgetGovernor governor;
    SpectraFxGpuLodSettings lod;
    SpectraFxHybridEmitter::Config emitter;
    SpectraFxGpuRouteAsset::Routes routes;
    (void)inbox;
    (void)governor;
    (void)lod;
    (void)emitter;
    (void)routes;
}
} // namespace aiverse::fx
