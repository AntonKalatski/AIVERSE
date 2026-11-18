---
name: aiverse-nereid-water
description: Use NEREID deterministic ECS water bodies and contact sampling in native projects of any AIVERSE game genre.
---

# NEREID: universal water system

Read `AGENTS.md`, `docs/nereid/ARCHITECTURE.md`,
`docs/nereid/HANDOFF.md`, and `docs/water.md` before editing water.

## Use the shared package

Link `AIVERSE::lib_nereid_water`, include
`<aiverselib/water/NereidWaterSystems.h>`, and use
`aiverse::lib::nereid`. Register `registerNereidWaterPersistence(sim)`
from native `GameModule::setup`. Configure ECS WaterBodyComponent Entities;
attach WaterProbeComponent+WaterContactComponent to any unit, actor, ship or
object. Supply WaterProbe position after movement, optionally by
`addWaterProbeFromTransformSystem`, then `addWaterSamplingSystem`.

The sampling policy is common across genres; do NOT implement separate FPS
or RTS water sampling. `sampleWater(world,position)` selects priority,
surface, then stable generation-safe Entity. `depth` is clamped 0..surface-
bottom and `submerged` is true only within the fluid volume.

## Do not conflate different sources of truth

Existing RTS maps use `waterNetwork` in editor/presentation, and baked
water tiles in game simulation. Do NOT replace the old tile-based mechanics,
change beach/cliff bake or reinterpret world coordinates without explicit
migration and parity tests. NEREID starts opt-in.

The NEREID values are integer/Fixed; GPU displacement and refraction must
never affect authoritative damage, swimming or network state. Add future
shapes, water physics, VFX and editor integrations as separate systems or
renderer adapters, not genre forks.

## Tests, handoff, status

`ecs_nereid_water_test` tests two independent native GameModules, native
ECS persistence, checksum replay and COW. Until it passes on a real build,
the system is **experimental/unverified**.

After each implementation commit, update `docs/nereid/HANDOFF.md` with exact
SHA, changes, verification status, unfinished work and next action. Also
update this skill and `docs/nereid/ARCHITECTURE.md` when API changes.

## Stage 2 — buoyancy and drag

For buoyant objects also link `AIVERSE::lib_nereid_buoyancy`, include
`<aiverselib/water/NereidBuoyancySystems.h>`, and register
`registerNereidBuoyancyPersistence(sim)` at setup.

Attach WaterBuoyancyComponent, WaterVelocityInputComponent and
WaterForceComponent to the querying Entity. Host physics supplies the
per-tick velocity. Run `addWaterBuoyancySystem` after the water sample
and before host integration, then consume WaterForceComponent.acceleration
with the host's *existing* gravity/collision solver. Never duplicate the
velocity owner or integrate gravity twice. Missing/stale contact returns
zero, and outputs are deterministic Fixed values.

Test target: `ecs_nereid_buoyancy_test` (unverified).


## Stage 3 — polygon lakes and spline rivers (experimental)

Use `<aiverselib/water/NereidGeometry.h>` or the
`NereidWaterSystems.h` umbrella and register
`registerNereidGeometryPersistence(sim)` when enabling shape overrides.

Add a `WaterGeometryComponent` to the same Entity as a
`WaterBodyComponent`. Use `WaterGeometryKind::Polygon` for an X/Z
closed simple ring (no closing duplicate necessary) or
`WaterGeometryKind::RiverSpline` for a centerline with a half-width
per `WaterGeometryPoint`. Attach 3..256 polygon points or 2..256
spline points; all spline half-widths must be positive. Surface, bottom
and flow are still defined by the regular water body. Only the footprint
is overridden. `sampleWater` already reads it.

Never feed float renderer tessellation or shader waves back into gameplay.
Do not re-encode legacy `waterNetwork` until migration tests prove
unchanged water baking and shoreline behavior. New test target
`ecs_nereid_geometry_test` is authored but NOT RUN; also run previous
NEREID tests before declaring this stage verified.


## Stage 4 — optional indexed water sampler

Include `<aiverselib/water/NereidQueryIndex.h>`. For large water-body
counts, replace `addWaterSamplingSystem` with
`addIndexedWaterSamplingSystem(sim,"water.sample")`. **Do not register
both.** It generates an in-tick temporary AABB index, scans infinite oceans
separately and uses the same deterministic selection and contact schema.

`WaterQueryIndex(world).sample(position)` is suitable for read-only
batch queries only: constructing the index holds a reference to the World.
Never retain it across World mutations, reloaded saves or rollback.
Always rebuild when source bodies or spline geometry have changed.
For few bodies and probes, the linear sampler may be cheaper.

Stage 4 authored test: `ecs_nereid_query_index_test`, not yet executed.
Run all four `ecs_nereid_.*_test` cases plus existing RTS water tests,
and record actual build/CTest results in `docs/nereid/HANDOFF.md`.

## Stage 5 — legacy waterNetwork / tile-map importer (experimental)

For **existing** RTS/editor water, read
`modules/rts/view/NereidLegacyWaterAdapter.h` and
`docs/nereid/ARCHITECTURE.md`.

Do not import control points directly: old `waterNetwork` uses
Catmull-Rom and a 0.75-cell bake margin; straight-line conversion
changes river edges. Use
`game::water::importBakedWaterCellsToNereid(cells,w,h,config)`
for the simulation's canonical `~` raster, or
`game::water::importWaterNetworkToNereid(network,w,h,legacyCm,config)`
only when source-network (pre-postprocessing) parity is desired.
The bridge returns Box `WaterBodyComponent` values for a native
project to attach; it does not modify the old map, editor, or ECS
implicitly. `WaterRasterImportConfig` provides Fixed coordinates
and surface/depth/priority, and must use the map's cell-metre scale.

This is a **load-time import**, not a per-tick conversion. For
runtime-updated maps, perform a deliberate body replacement and
rebuild indexed sampling next tick. The resulting rectangular cover
matches the legacy mask at **cell centres**, not at all sub-cell
positions; baked `~` cells also lose per-body semantic WaterKind.

The generic raster conversion lives in
`aiverselib/water/NereidRasterImport.h` and does **not** depend
on RTS. Test: `ecs_nereid_legacy_raster_test` (authored, NOT RUN).
Record actual results and any fixes in `docs/nereid/HANDOFF.md`.

## Stage 6 — easiest opt-in native water setup

Include `<aiverselib/water/NereidNativeWater.h>`, link public
`AIVERSE::lib_nereid_water` and (when buoyancy is enabled)
`AIVERSE::lib_nereid_buoyancy`. From native GameModule::setup, call
`registerNativeWaterPipeline(sim,"water",cfg)` exactly once.
`NativeWaterPipelineConfig` toggles geometry persistence, buoyancy
output and optional Transform->WaterProbe synchronization.
`spawnNativeWaterBodies(sim.world(),bodies)` creates ECS-only water
body entities in deterministic source order. Host still owns velocity,
gravity, collision, structural changes and entity lifecycle.

`addAdaptiveWaterSamplingSystem(sim,name,AdaptiveWaterSamplingConfig{})`
is available if manually composing a pipeline. It uses only ECS
water-body/probe counts to select reference or indexed sampling, not
frame timings; tune thresholds via profile data. Do not register it
alongside other NEREID sampling systems. No extra persistence schema.

New test: `ecs_nereid_native_pipeline_test`. All six NEREID
fixtures are also included in `tests/run_ecs_experiment_suite.sh`,
which CI runs. Do not claim green until GitHub Actions completes.

## Stage 7A — sampling performance benchmark

Profile the water broadphase before tuning
`AdaptiveWaterSamplingConfig` thresholds. Build/run
`aiverse_ecs_nereid_sampling_bench` or use
`bash tests/run_ecs_experiment_suite.sh --bench`.
The test reports microseconds per tick for 16/64/256 bodies with
8/128/1024 probes, explicitly including index rebuild time.
It is a benchmark with gameplay parity checks, not a CI timing gate.
No measurements were run during stage 7A.

## Stage 7B — surface meshes for any genre

Include `<aiverselib/water/NereidWaterSurfaceMesh.h>` for a
read-only, backend-agnostic water mesh. Call
`buildWaterSurfaceMesh(body, optionalGeometry, options)` in
presentation/assets preparation, NOT authoritative Simulation
updates. Outputs vertices (X,Y,Z,UV,shore factor) and 32-bit indices.
Supported: box/disc/infinite patch, simple polygons (concave allowed)
and variable-width river strips. Near/Medium/Far modes control LOD;
ocean patch is camera-relative, so rebuild when its reference moves.
Zero/invalid polygons return empty. GPU material hookup is a separate
renderer stage; never present this as completed refractive water.

Test `ecs_nereid_surface_mesh_test` is authored but UNVERIFIED.

## Stage 7C — draw water surfaces with existing renderer

With a presentation-owned `gfx::Device` and
`render::Renderer3D`, include
`engine/render/NereidWaterRenderer.h`:

```cpp
auto cpu=nw::buildWaterSurfaceMesh(body,geometry,meshOptions);
auto gpu=aiverse::render::uploadNereidWaterSurface(device,cpu,style);
// Every scene frame, after Renderer3D::beginScene and before render:
aiverse::render::drawNereidWaterSurface(renderer,gpu,style);
// During scene shutdown or before replacing the uploaded mesh:
aiverse::render::releaseNereidWaterSurface(device,gpu);
```

`kDrawWaterSurface` is the opt-in water shader flag; at any
world Y it enables the existing open-water normal-wave shading
and the renderer's existing reflection quality tier. No
separate NEREID shader copies are necessary. A single planar
reflection plane is currently shared across the scene; SSR only
works in trace-enabled quality modes. This is not yet an
automatic RTS/FPS integration or realistic refraction/underwater
system.

Run `ecs_nereid_surface_mesh_test` and
`ecs_nereid_renderer_contract_test`, then actual GL/Metal
rendering, before marking this verified. Maintain GPU handle
lifetime and avoid changing ECS from renderer code.

## Stage 8 — water at several heights

The current Renderer3D planar mirror is single-plane. NEREID's
`kDrawWaterSurface` surfaces at a different Y now explicitly skip
the primary plane reflection instead of showing the wrong scene
image. The matching policy is
`render::sameWaterReflectionPlane` in
`engine/render/NereidWaterReflectionPolicy.h`.
Secondary surfaces retain wave shading and trace-tier SSR when
active. Do not claim separate multi-plane reflections or correct
refraction exist yet.

When working on render paths, test multiple water heights and both
OpenGL and Metal; the new `ecs_nereid_renderer_contract_test`
contains basic CPU/shader-source checks but cannot substitute GPU
compilation or screenshots.
