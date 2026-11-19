# NEREID Stage 41 — deterministic linked river confluence (ECS core)

**Date:** 2026-11-19.
**Feature branch:** `feature/nereid-water-core`, Draft PR #23.
**Starting revision:** `c7437f7ccadf4bf95824c32b4384abc406e663b6`, with main already an ancestor of feature branch.
**Implementation and documentation:** one atomic Git commit including this file (read commit history for exact SHA).

## Architecture

New header `libraries/include/aiverselib/water/NereidConfluences.h` defines optional `WaterConfluenceComponent` on a distinct junction Entity: up to four explicit generation-safe inlet water Entity IDs (minimum two), an outlet Entity, Fixed-point X/Z mouth position, influence radius, blend, final speedLimit and enable flag. All configurations are verified by `validWaterConfluence`. Persistence is an opt-in, separate v1 ECS schema `nereid.confluence`, registered via `registerNereidConfluencePersistence(sim)`. Existing water/cascade archives unchanged.

New `NereidConfluenceFlow.h` provides `waterConfluenceMouthContribution` and `blendWaterConfluenceContributions`, pure Fixed-point kernels, plus `applyWaterConfluenceFlow(world,probe,sample)`. Each junction is eligible only for a submerged water contact selected on its outlet; the authored mouth must lie on both outlet and all active input footprints. Missing/dead/incompatible inputs suppress the entire junction (no partial mixing). Input IDs are sorted before summation. Upstream current uses existing unified `waterCurrentWithVortices`, with bounded input velocities. At most 32 lowest generation-safe junction IDs are considered, with deterministic sorted reduction; the mixture is smoothly weighted by mouth proximity. The result is a local `WaterSample.flow` override, never a mutation of `WaterBodyComponent.flow` or physical mass transfer.

**Query integration:** both `NereidWaterSystems.h` (reference) and `NereidQueryIndex.h` (indexed) invoke the same policy after body selection, before existing `applyWaterCascadeFlow`. This order retains cascade-mouth precedence and keeps buoyancy/drag consumers of `WaterContact` shared across genres.

**Native opt-in:** `NereidNativeWater.h` has `NativeWaterPipelineConfig::enableConfluences=false` (default). Enable before setup and attach junction component after creating linked body Entities. Standalone manual registration is also supported.

Example C++:
```cpp
namespace nw = aiverse::lib::nereid;
nw::NativeWaterPipelineConfig cfg;
cfg.enableConfluences = true;
nw::registerNativeWaterPipeline(sim, "water", cfg);
// At setup / canonical ECS structural barrier:
nw::WaterConfluenceComponent join;
join.inletCount=2;
join.inlets[0]=northFork;
join.inlets[1]=eastFork;
join.outlet=lowerRiver;
join.mouth={40_fx,5_fx,20_fx};
join.radius=8_fx;
join.blend=Fixed::fromRatio(3,4);
const Entity junction=sim.world().create();
sim.world().add<nw::WaterConfluenceComponent>(junction,join);
```

## Changed files

- NEW `libraries/include/aiverselib/water/NereidConfluences.h`.
- NEW `libraries/include/aiverselib/water/NereidConfluenceFlow.h`.
- `libraries/include/aiverselib/water/NereidWaterSystems.h`
- `libraries/include/aiverselib/water/NereidQueryIndex.h`
- `libraries/include/aiverselib/water/NereidNativeWater.h`
- `docs/nereid/HANDOFF.md`, `docs/nereid/ARCHITECTURE.md`, `skills/gameplay/aiverse-nereid-water/SKILL.md`, this handoff.

## Known limitations and verification

No scene/manifest read-only confluence presentation adapter yet. Render ripples and rapid whitewater keep existing base/cascade fields until Stage 42 supplies a snapshot. This is a localized analytical velocity blend, not mass conservation, pressure or terrain erosion. Junction authoring must overlap all input footprints and outlet. Only 32 stable contributing junctions, four upstreams each. Current implementation is an O(junctions × inlets) post-query scan. Geometry, current and entity lifetime remain canonical ECS state.

**Actually performed:** GitHub source review, guarded patches, atomic remote commit. **Not performed:** local C++ compiler, CTest, shader compiler, gameplay/scene execution, rollback/hash verification, FPS/RTS/PHYSIS/Box3D regressions. The code is unverified pending feature completion; keep PR Draft.

## Specific next step

Stage 42: build a read-only scene confluence snapshot from ECS generation-safe IDs and a matching `WorldScene` current bridge for ripple and rapid water presentation, sharing `waterConfluenceMouthContribution` and the bounded sorted reducer. Then optimize water material residency (do not reupload VB/IB on shader-only edits). Add build/CTest and sampling parity tests in the deferred validation phase.
