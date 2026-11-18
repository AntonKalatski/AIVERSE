# AIVERSE NEREID — architecture and integration contract

NEREID is the universal, genre-independent water framework of AIVERSE. It is
**not** a fork of RTS water nor a renderer-only material. Existing legacy
water remains active until explicit, tested migration.

## Existing water (do not remove)

- `modules/rts/view/RtsWater.{h,cpp}`: spline networks, shores, geometry, water cells.
- `modules/rts/view/RtsShoreView.{h,cpp}`: shores, props and foam rendering.
- `apps/editor/core/WaterOps.{h,cpp}`: editor bake and water operations.
- `apps/editor/windows/WaterWindow.cpp`: water editing UI.
- `tests/tests_water.inc`: existing water/shore/map behavior.
- `docs/water.md`: existing scene/map representation.

The previous `waterNetwork` is baked into map tiles and the RTS simulation
consumes these tiles, **not** the source water body splines. Do not change
baked tile semantics, game saves, terrain generation or shore placement by
introducing NEREID.

## Stage 1 — authoritative gameplay data + sampling

Public C++ headers:
- `<aiverselib/water/NereidWater.h>`
- `<aiverselib/water/NereidWaterSystems.h>`
- Package: `AIVERSE::lib_nereid_water` from `libraries/catalog.json`.

Namespace: `aiverse::lib::nereid`.

ECS component contract:
- `WaterBodyComponent`: authoritative definition, fixed-point coordinates
  (X/Z horizontal, Y up), kind (Ocean/Lake/River/Pool/Puddle), footprint
  (Infinite/Box/Disc), surface/bottom Y, flow, overlap priority, enabled.
- `WaterProbeComponent`: authoritative query point provided by host input,
  physics, transform sync or game-specific policy.
- `WaterContactComponent`: ECS-owned sampled result and tick; not kept as
  independent FPS/RTS vector state.
- `WaterSample`: selected generation-safe body Entity, kind, surface/bottom,
  clamped immersion depth, flow, priority and found/submerged bits.

Only point-in-footprint sampling is implemented at this stage. A water
`kind` does not automatically supply a spline, hydrodynamics or waves.

## Selection / gameplay ordering

1. Host input/movement/physics writes `Transform` or `WaterProbeComponent`.
2. Optional `addWaterProbeFromTransformSystem(sim,name)` copies the shared
   `Transform::pos` into probes; it is not needed for custom probe producers.
3. `addWaterSamplingSystem(sim,name)` scans water bodies and updates
   `WaterContactComponent`. Higher priority wins; same priority takes higher
   surface, then lowest packed Entity ID. Query does not depend on pool order.
4. Game-specific swim, buoyancy, currents and damage stages consume contact.
5. Renderer may consume presentation snapshots and add GPU-only detail.

This is O(body count * probe count); spatial acceleration is planned.
No bodies or probes are structurally changed during the sampling pass.

## Determinism, persistence and native code

- Authoritative values use `aiverse::Fixed` / integer, never floats or GPU
  wave samples. No RNG or genre branches in NEREID core.
- Register `registerNereidWaterPersistence(sim)` during the host module's
  `setup`, before stepping/sealing, to add versioned native ECS schemas:
  `nereid.water_body`, `nereid.water_probe`, `nereid.water_contact`.
- World-native snapshots, hashes and serialization are host-integration
  concerns: do not add a second mutable mirror. The host must also persist
  any independent Transform/movement state affecting future probes.
- Add water to an arbitrary native game's public target by linking
  `AIVERSE::lib_nereid_water`. Do not link FPS/RTS for the library.
- `tests/ecs_nereid_water_test.cpp` exercises two unrelated hosts, rollback,
  COW, native save/load and checksum for NEREID components. These are
  authored tests, **not yet executed** in the current implementation.

## Roadmap and constraints

Stage 2: shared water-body geometry adapters (legacy `waterNetwork`,
spline river, closed lake polygon), explicit world position/terrain height
contract, accelerated spatial queries; test old water editor/bake parity.
Stage 3: render backend with low/medium/high profiles, depth/shore foam,
SSR/planar reflection fallback, LOD and underwater view.
Stage 4: composable water mechanics (buoyancy, swimming, drag, currents, wakes,
impulses, waterfalls), deterministic CPU game state + GPU visual layering.
Stage 5: editor UX, auto-shores, weather coupling, serialization upgrades,
native and existing FPS/RTS adoption, platform performance/CI.

Do not claim stage 3/4 features exist because corresponding `WaterKind`
values are present in the enum.

## Stage 2 — cross-genre buoyancy output

`<aiverselib/water/NereidBuoyancySystems.h>` exposes the optional
`AIVERSE::lib_nereid_buoyancy` package. It runs only when Entities have
WaterBuoyancyComponent, WaterVelocityInputComponent, WaterContactComponent
and WaterForceComponent.

`computeWaterForces` uses fixed-point immersion fraction =
clamp(contact.depth / immersionHeight, 0, 1). Output water acceleration =
up * buoyancyAcceleration * fraction -
(velocity - waterFlow) * linearDrag * fraction.
This excludes ordinary gravity and does not integrate movement or
alter contacts. A host's canonical physics owns all velocity and spatial
updates. Stage order must sample water BEFORE buoyancy and integrate AFTER.
Missing component or sample from another tick => zero output.

WaterVelocityInput is a per-tick view of host canonical physics state; if the
host retains velocity in an existing ECS component, copy it once immediately
before buoyancy rather than inventing a persistent second velocity owner.
Register `registerNereidBuoyancyPersistence` in setup when enabled.
`ecs_nereid_buoyancy_test` is authored and not yet run.


## Stage 3 — ECS geometry override (implemented, unverified)

`NereidGeometry.h` defines `WaterGeometryComponent` as an optional
per-body footprint override: a closed simple polygon (lakes, pools), or
a variable-half-width X/Z polyline (rivers, canals). Geometry has a
maximum of 256 nodes for bounded ECS save and replay. Polygon banks and
spline bank edges are included in queries. Self-intersecting polygons and
holes are not a supported contract.

The shared `sampleWater` queries the optional component on each water
body Entity. If the component is absent or disabled, original v1
Infinite/Box/Disc footprints continue to work. An invalid enabled geometry
covers nothing; it cannot silently expand to a larger fallback. Body height,
flow, priorities and stable ID tie-breaks are unchanged. Geometry is
independent from RTS/editor `waterNetwork`; no implicit migration.

To opt in: call `registerNereidGeometryPersistence(sim)` in native
setup before the registry seals, and attach `WaterGeometryComponent`
to `WaterBodyComponent` entities. This adds the separate
`nereid.geometry` v1 schema, leaving the water-body v1 wire format alone.
`AIVERSE::lib_nereid_water` is still the native link target.

This is a pure deterministic CPU footprint policy. It has no spatial index
or geometry mesh generation, waves, shoreline features, VFX or editor
adapter. Geometry component storage, snapshot and rollback are ECS owned.
Test: `ecs_nereid_geometry_test` (authored but NOT RUN).


## Stage 4 — transient water spatial broadphase (authored, not built)

`NereidQueryIndex.h` provides an optional acceleration adapter. It builds
a deterministic AABB hierarchy from `WaterBodyComponent` and optional
`WaterGeometryComponent` data and samples it with the **same**
`considerWaterCandidate` selection as the reference `sampleWater`.
Finite Box/Disc/Polygon/RiverSpline volumes are indexed by conservative
horizontal bounds. Infinite oceans stay outside the tree. Both check
`coversWaterBody` before yielding a sampled contact. Bodies with invalid
enabled geometry are excluded rather than falling back to box/ocean.

Register **either** `addIndexedWaterSamplingSystem` **or**
`addWaterSamplingSystem`; both consume a probe and overwrite
`WaterContactComponent` with the same tick. The index is constructed on
each simulation tick and discarded before the tick ends. It stores a
reference to the World and must not be reused after World writes,
structural changes, restoring a snapshot or loading a native save.
Nothing about the indexed data itself is authoritative or persistent.

No changes to NEREID persistence schemas, renderer, RTS/editor waterNetwork
or existing save streams. Two-host indexed/reference parity test:
`ecs_nereid_query_index_test` (author-written, NOT RUN yet).

## Stage 5 — read-only adoption of legacy baked-water raster

The legacy `waterNetwork` stores editor-side Catmull-Rom splines
and sea shorelines in **cells**, with variable river width in metres.
Its `waterCells` function adds a 0.75-cell edge tolerance, interpolates
sea frontiers and supports headland/shore features. Consequently,
interpreting its original control points directly as NEREID straight
spline segments **would not preserve existing wet cells**.

Instead, `NereidRasterImport.h` implements a deterministic pure
converter from a generic binary raster to disjoint
`WaterBodyComponent` rectangles. Row-run compression also merges
identical spans in adjacent rows. This represents the baked water mask
exactly **at cell centres**, not an equivalent continuous shoreline.
Coordinates are X/Z world metres, `cellMeters` is explicitly Fixed,
and body heights are provided by the caller. There is no change to
existing NEREID schemas.

`modules/rts/view/NereidLegacyWaterAdapter.h` is the only
legacy-dependent header:
- `importWaterNetworkToNereid` uses the untouched legacy
  `buildGeometry` + `waterCells` rasterizer, and is appropriate for
  map source previews/baking.
- `importBakedWaterCellsToNereid` consumes the actual canonical game
  map cells (`~` by default), including post-bake changes; **prefer
  this for native gameplay migration**.

The adapter returns data without altering the input map or automatically
registering a game system. A native host opts in by creating entities,
registering `registerNereidWaterPersistence`, adding probes and
choosing one contact-sampling stage. No old RTS/editor save protocol,
shore/bank geometry or navigation is migrated automatically.
Baked cell masks lose per-body sea/river provenance, so callers specify
a common `WaterKind` and an explicit water surface/depth. To support
multiple semantic water levels, source distinct masks explicitly.

Fixture: `ecs_nereid_legacy_raster_test` (authored, NOT RUN)
compares all centres of an 80x64 legacy river/sea/strait map against
both NEREID sampling policies and verifies native save/COW/rollback.

## Stage 6 — native pipeline and adaptive sampling

The new `NereidAdaptiveWater.h` is an optional **deterministic**
selection policy. `addAdaptiveWaterSamplingSystem` evaluates ECS
water-body and contact-pool counts each tick; it uses reference scanning
below `minWaterBodies` or `minProbes`, or the transient stage-4 BVH
otherwise. This choice is not driven by elapsed time or presentation
frame rate, and both paths write identical `WaterContactComponent`
data under the same overlap/tick semantics. It introduces no persistence
schema. Only one contact-sampling system may be scheduled.

`NereidNativeWater.h` composes the existing reusable components and
systems from any native `GameModule::setup` without RTS/FPS/renderer
dependencies: `registerNativeWaterPipeline(sim,prefix,cfg)`.
Optional switches enable geometry persistence, buoyancy persistence and
force calculation, and transform-probe synchronization; the adaptive
sampler is the one contact stage. Physics integration remains the host's
responsibility. `spawnNativeWaterBodies(world,bodies)` creates water
body Entities deterministically; it deliberately does not invent an
independently writable water map. Attach probe/contact to any Entity
irrespective of genre.

CTest `ecs_nereid_native_pipeline_test` checks two unrelated hosts,
forced linear/indexed equivalent results, native save/load and COW
rollback. The ECS experimental shell runner now also compiles and
executes all six NEREID tests, including the legacy raster bridge.
**All results remain unverified until CI actually runs.**

## Stage 7A — performance characterization

`tests/ecs_nereid_sampling_bench.cpp` is an optional measurement
program (not a correctness-gated CTest). It uses `steady_clock` only in
the diagnostic process, never within simulation. It times a 3x3 matrix
of body and probe counts, including the **per-tick rebuild** of the
transient stage-4 AABB tree, and compares the resulting deterministic
sample signatures with the linear reference. Invoke CMake target
`aiverse_ecs_nereid_sampling_bench` or
`tests/run_ecs_experiment_suite.sh --bench`. Results are NOT YET
available; default adaptive thresholds remain heuristics.

## Stage 7B — reusable presentation geometry, not gameplay authority

`<aiverselib/water/NereidWaterSurfaceMesh.h>` provides independent
CPU mesh creation for NEREID water bodies using only generic ECS water
data: rectangles/discs, bounded infinite-ocean patches, simple concave
polygons and variable-width river ribbons. LOD controls tessellation
rather than entity/genre semantics. UVs and shoreline proximity
(`WaterSurfaceVertex.shore`) are outputs for rendering only.
`WaterSurfaceStyle` describes tint/foam/specular/alpha, without
imposing a backend. Invalid shapes are rejected rather than silently
rendered with an alternate footprint.

This is a read-only **presentation projection** of authoritative
`WaterBodyComponent`/`WaterGeometryComponent`, never persisted, hashed,
networked or read back into sim. Existing RTS/editor water surfaces,
shader effects, baked cells and shoreline models remain untouched.
New test `ecs_nereid_surface_mesh_test` was authored but NOT RUN.
