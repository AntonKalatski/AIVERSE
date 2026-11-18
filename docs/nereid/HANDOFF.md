# NEREID — living AI-agent handoff

**Repository**: `chromealex/AIVERSE`
**NEREID branch**: `feature/nereid-water-core`
**Created from**: `feature/universal-entity-inspector-fps` (working project
branch, may advance concurrently).
**Last implementation commit**: `5dc63de21772da931c68bed02954866de186d754`
**Native build/CTest status**: NOT RUN / UNVERIFIED for stages 1 and 2.

## Stage 1 — deterministic water queries (implementation commit 8099a884)

New/changed files in implementation commit:
- `libraries/include/aiverselib/water/NereidWater.h`: ECS types,
  pure footprint policy, field-wise Archive IO.
- `libraries/include/aiverselib/water/NereidWaterSystems.h`: pure
  `sampleWater`, transform adapter, ECS sampling registration, persistent IDs.
- `tests/ecs_nereid_water_test.cpp`: shooter/strategy simulation hosts,
  ocean/lake overlap, immersion, save/load, COW, rollback.
- `libraries/catalog.json`: header package `nereid-water`.
- `CMakeLists.txt`: target and CTest `ecs_nereid_water_test`.

**No existing FPS, RTS, render, editor, physics or map files were modified.**

## How to consume

```cpp
#include <aiverselib/water/NereidWaterSystems.h>
namespace nw = aiverse::lib::nereid;

// Inside native GameModule::setup(Simulation& sim,...)
nw::registerNereidWaterPersistence(sim);
// Attach WaterBodyComponent to ECS water source Entities.
// Attach WaterProbeComponent + WaterContactComponent to query Entities.
// Attach shared Transform OR write WaterProbeComponent::position yourself.
nw::addWaterProbeFromTransformSystem(sim, "nereid.probe"); // optional
nw::addWaterSamplingSystem(sim, "nereid.sample");
```

Use CMake `AIVERSE::lib_nereid_water`, and read
`docs/nereid/ARCHITECTURE.md` plus
`skills/gameplay/aiverse-nereid-water/SKILL.md`.

## Mandatory verification (not yet performed)

```sh
cmake -S . -B build/ecs-test -DAIVERSE_BUILD_TESTS=ON
cmake --build build/ecs-test --target aiverse_ecs_nereid_water_test -j 4
ctest --test-dir build/ecs-test --output-on-failure -R '^ecs_nereid_water_test$'
```

Also run legacy `water_*` fixtures and the relevant FPS/RTS ECS tests
before connecting NEREID to existing games. Compile/link a standalone native
game module with `AIVERSE::lib_nereid_water`.

## Known limits

- Currently supports infinite, axis-aligned box and disk water footprints;
  no river spline or irregular polygon.
- Static surface and flow only. No waves, buoyancy, hydraulic solver, VFX or
  under-water rendering has been implemented.
- Overlap is O(water-body count * probe count).
- Native water components are v1; Transform/motion components must be saved
  by their own owners. The library does not change old `waterNetwork` or
  the `~` tile bake.
- Existing `WaterKind` enum values represent semantic kinds, not feature
  completeness.
- If build/test failure occurs, fix it first and document exact command/result.

## Next agent action

1. Read `AGENTS.md`, `docs/ecs-mechanic-composition.md`,
   `docs/water.md` and `docs/nereid/ARCHITECTURE.md`.
2. Configure/build/run new NEREID test and fix any compile/runtime failure.
3. Implement next opt-in capability without altering the existing game's
   water source-of-truth or float presentation pipeline; favor shared
   deterministic ECS policy over per-genre implementations.
4. After EVERY implementation stage, append: exact commit SHA, modified
   files, API, test logs/result, limitations, next action.
5. Keep the implementation and documentation in separate commits, document
   the implementation immediately before proceeding to the next stage.

## Stage 2 — composable buoyancy and flow drag (implementation commit `5dc63de21772da931c68bed02954866de186d754`)

Files changed: `libraries/include/aiverselib/water/NereidBuoyancySystems.h`,
`tests/ecs_nereid_buoyancy_test.cpp`, `CMakeLists.txt`,
`libraries/catalog.json`.

Public package `AIVERSE::lib_nereid_buoyancy` depends on
`AIVERSE::lib_nereid_water`. Public API in `aiverse::lib::nereid`:
- `WaterBuoyancyComponent`: acceleration, linear drag and immersion height
  (data-only, fixed-point).
- `WaterVelocityInputComponent`: per-tick canonical-motion velocity input;
  not a second source of truth for the host's physical velocity.
- `WaterForceComponent`: computed acceleration, immersed fraction, sampledTick.
- `computeWaterForces(sample,params,velocity)`: pure shared CPU policy.
- `addWaterBuoyancySystem(sim,name)`: read same-tick sampled contact and
  velocity, publish force; zeroes invalid or stale input.
- `registerNereidBuoyancyPersistence(sim)`: native ECS schema v1
  (`nereid.buoyancy`, `nereid.velocity_input`, `nereid.force`).

Force result includes upward water acceleration and drag relative to the
water's flow; **gravity, collisions and velocity integration remain with the
host physics/motion owner**. This avoids a second mutable velocity or
genre-specific swim implementation. Use only one solver.

Suggested sequence in a host:
```cpp
nw::registerNereidWaterPersistence(sim);
nw::registerNereidBuoyancyPersistence(sim);
// After movement and host per-tick input:
nw::addWaterProbeFromTransformSystem(sim, "water.probe"); // optional
nw::addWaterSamplingSystem(sim, "water.sample");
nw::addWaterBuoyancySystem(sim, "water.buoyancy");
// Then host physics integrates WaterForceComponent::acceleration.
```

**Verification:** NOT RUN / UNVERIFIED. Git access from the available
terminal returned `Could not resolve host: github.com`; connected GitHub
writes were successful, but do not imply a C++ build. New test
`ecs_nereid_buoyancy_test` checks drag relative to river/ocean flow,
submersion, dry reset, two unrelated native hosts, v1 save/load, COW/replay.
Compile it on a checkout:
```sh
cmake --build build/ecs-test --target aiverse_ecs_nereid_buoyancy_test -j 4
ctest --test-dir build/ecs-test --output-on-failure -R '^ecs_nereid_(water|buoyancy)_test$'
```

**Limits**: this is a contact/force-output *policy*, not a dynamic fluid solver,
mesh, boat hull evaluator, wave field, ocean FFT, physics engine or gameplay
swimming controller. Host is responsible for keeping WaterVelocityInput
current from its canonical motion state each tick. Validate platform compile,
native physics coupling, save/rollback and existing RTS/FPS parity before
shipping.

**Next agent action**: First configure/build both NEREID tests and fix any
issues. Then create separate spline/polygon geometry adapter and renderer
stages. Do not move RTS waterNetwork ownership without existing
`water_*` bake parity and non-regression tests. After each code stage commit,
append another status section to this handoff in a documentation commit.


## Stage 3 — optional polygon and river-spline geometry

**Implementation commit**: `e00eb62c92ae1bdb5f932cf1d60f7fa4ce0a5e9c` on `feature/nereid-water-core`.

Files added: `libraries/include/aiverselib/water/NereidGeometry.h`,
`tests/ecs_nereid_geometry_test.cpp`.
Files changed: `libraries/include/aiverselib/water/NereidWaterSystems.h`,
`CMakeLists.txt`. Documentation update follows as a separate commit.

### API / architecture

- `WaterGeometryComponent` is an **optional ECS component on the same Entity**
  as `WaterBodyComponent`. It has `kind`, `points`, `enabled`.
- `WaterGeometryKind::Polygon` is a simple closed X/Z ring (implicit closing
  edge, boundary inclusive, no holes). Each `WaterGeometryPoint` stores
  `x`, `z`, `halfWidth`; polygon ignores `halfWidth`.
- `WaterGeometryKind::RiverSpline` represents a centerline polyline. Each
  endpoint's `halfWidth` is interpolated along segments; river width is
  tested against the nearest point on each segment.
- `coversWaterBody(body,optionalGeometry,point)` is a pure deterministic
  Fixed-point footprint policy; geometry overrides Box/Disc/Infinite when
  enabled. Disabled/missing geometry uses v1 footprint. Enabled invalid
  geometry yields no coverage (no accidental infinite fallback).
- `sampleWater(world,point)` automatically consults optional geometry and
  **retains** existing priority, surface, depth, body ID and flow selection.
- `registerNereidGeometryPersistence(sim)` registers
  `nereid.geometry` v1 separately from existing `nereid.water_body` v1,
  so older data schemas and callers stay unchanged.
- Bounded archive IO rejects more than 256 geometry nodes before allocation.
  The interface remains `AIVERSE::lib_nereid_water`; no FPS/RTS dependency.

Native usage:
```cpp
nw::registerNereidWaterPersistence(sim);
nw::registerNereidGeometryPersistence(sim); // opt-in for polygon/spline
// Attach WaterBodyComponent and WaterGeometryComponent to water entity.
// Existing addWaterSamplingSystem() reads both automatically.
```

### Verification and limitations

**Native C++ build, CTest, platform runtime: NOT RUN / UNVERIFIED.**
The current execution environment cannot resolve github.com for cloning.
The GitHub connector wrote the implementation but cannot compile C++.
The new test is **authored, not executed**. Its intended cases: polygon
interior/edge/exterior, width-varying spline, overlapping priority,
multiple independent native hosts, native save/load, COW and rollback.

```sh
cmake -S . -B build/ecs-test -DAIVERSE_BUILD_TESTS=ON
cmake --build build/ecs-test --target \
  aiverse_ecs_nereid_water_test \
  aiverse_ecs_nereid_buoyancy_test \
  aiverse_ecs_nereid_geometry_test -j 4
ctest --test-dir build/ecs-test --output-on-failure \
  -R '^ecs_nereid_(water|buoyancy|geometry)_test$'
```

**Limits:** simple polygons only (no holes/self-intersections), one
variable-width centerline per geometry component, up to 256 nodes. All
query math is deterministic Fixed, not fluid rendering or GPU waves.
Overlap remains O(water bodies × probes × geometry segments). This is a
gameplay footprint and cannot reconstruct auto-shores or editor bake.
Existing `waterNetwork` must not be replaced without parity tests.

**Next agent**: run the three NEREID tests and repair compile/runtime errors
before adding further production integration. Then design a cached,
rollback-safe spatial query accelerator and a read-only adapter from legacy
`waterNetwork`; keep the legacy source and bake unchanged. Update docs
after every implementation commit with actual test output, limits and SHA.


## Stage 4 — transient ECS water-query broadphase

**Implementation commit:** `9451e4a7ed682d3d9cecb87d205cc2a7865d617a`
on `feature/nereid-water-core`.

**Files added:** `libraries/include/aiverselib/water/NereidQueryIndex.h`,
`tests/ecs_nereid_query_index_test.cpp`.
**Files modified:** `libraries/include/aiverselib/water/NereidWaterSystems.h`,
`CMakeLists.txt`.

### Architecture / API

- `WaterQueryIndex(const World&)` creates a **transient read-phase**
  balanced AABB tree for finite enabled water bodies (Box/Disc, polygon,
  variable-width river spline); infinite oceans are kept in a separate list.
- `WaterQueryIndex::sample(Vec3)` uses conservative X/Z AABB checks followed
  by the unchanged exact Fixed-point `coversWaterBody` policy.
- `considerWaterCandidate` is now shared by both the old reference
  `sampleWater(world,point)` and indexed sampler; selection remains
  **priority > surface > generation-safe Entity ID**.
- `addIndexedWaterSamplingSystem(sim,name)` is an optional **replacement**
  for `addWaterSamplingSystem`. Register only one of the two.
  Both write the same `WaterContactComponent` and tick stamp.
- The index is **rebuilt every sampling tick** from ECS authoritative bodies.
  No index state is persisted in save/network/rollback. Do not retain a
  `WaterQueryIndex` after structural edits, geometry/position changes,
  `World` replacement or rollback; its bounds become stale.
- No new ECS schemas or catalog packages are required; link
  `AIVERSE::lib_nereid_water` and include
  `<aiverselib/water/NereidQueryIndex.h>`.

Native usage:
```cpp
nw::registerNereidWaterPersistence(sim);
nw::registerNereidGeometryPersistence(sim); // when geometry components exist
nw::addWaterProbeFromTransformSystem(sim,"water.probe");
nw::addIndexedWaterSamplingSystem(sim,"water.sample"); // NOT both samplers
```

This is a performance policy option for multiple water bodies/probes:
index construction is currently O(B log² B) due to repeated
median partitions (not amortized across ticks); query is typically
O(log B + nearby bodies), but worst-case O(B) with overlapping bodies.
It has not been benchmarked: **no performance gain is claimed yet**.

### Verification

**Code authored and pushed via GitHub connector. Native build/CTest NOT
RUN.** Previous CI runs were queued or cancelled due to newer pushes, and
no completed passing NEREID run was available while implementing stage 4.
Do not interpret an authored test as passing.

```sh
cmake -S . -B build/ecs-test -DAIVERSE_BUILD_TESTS=ON
cmake --build build/ecs-test --target \
  aiverse_ecs_nereid_water_test aiverse_ecs_nereid_buoyancy_test \
  aiverse_ecs_nereid_geometry_test aiverse_ecs_nereid_query_index_test -j 4
ctest --test-dir build/ecs-test --output-on-failure -R '^ecs_nereid_.*_test$'
```

The authored `ecs_nereid_query_index_test` exercises ~1,800 probe
positions per host against reference sampling, mixed oceans/discs/splines/
polygons, priority tie policies, invalid geometry, two unrelated native
hosts, native save/load, COW rollback and next-tick index rebuild after
water geometry changes. This test has **not run**.

### Limits / next action

- Spatial bounds are conservative; the canonical exact per-body geometry
  test still decides water contact. The index captures `World` by
  reference; it is valid only within its construction read phase.
- Geometry polygon holes/self-crossings remain unsupported; no change to
  RTS `waterNetwork`, tile baking, editor or water shaders.
- For very few bodies and probes, rebuilding per tick may be slower than
  unindexed scans; decide empirically after profiling.
- **Next agent:** compile all four NEREID tests, fix failures, then profile
  at small and large body/probe counts and add optional opt-in threshold.
  After that, implement read-only legacy spline geometry import with
  parity tests before any native/editor migration. Maintain a handoff
  section after every implementation commit with exact SHA, files, test
  commands/results, current limitations and next task.

## Stage 5 — legacy raster-to-ECS import (code committed; tests not run)

**Branch:** `feature/nereid-water-core`.
**Initial implementation:** `4447c5ddeb86de1e715eafd8b12c4190d7b200d0`.
**Follow-up correction:** `b55062501c78c26c9f9b5317b0ef9f2241895753`
(baked-tile API, wet-to-dry test, input validation).
**Test expectation correction:** `630367954294b22454f898d93e9aa1473c7e25b6`.

Changed files:
- Added `libraries/include/aiverselib/water/NereidRasterImport.h`: pure
  generic row-run rectangularisation, water-body conversion.
- Added `modules/rts/view/NereidLegacyWaterAdapter.h`: optional read-only
  legacy map bridge, under the *legacy* module (core remains genre-free).
- Added `tests/ecs_nereid_legacy_raster_test.cpp`: cell-centre parity
  against the existing waterCells raster, baked-tile parity,
  indexed-vs-reference checks, two independent native hosts,
  persistence, COW and rollback.
- Updated `CMakeLists.txt`: `aiverse_ecs_nereid_legacy_raster_test`
  links the core engine and `RtsWater.cpp` directly (no editor/runtime
  or rendering target).
- Updated `libraries/catalog.json` description to reflect stages 1–5.

### API

`aiverselib/water/NereidRasterImport.h` (generic):
- `WaterRasterImportConfig`: Fixed `cellMeters`, `surfaceY`,
  `bottomY`, common `kind`, `priority`, `maxRectangles`.
- `rectangleWaterCells(wet,width,height,maxRectangles)`: deterministic
  run-length compression into disjoint rectangular spans, merges
  same-width spans across adjacent rows.
- `waterBodiesFromRaster(wet,width,height,config)`: returns
  `std::vector<WaterBodyComponent>` Box shapes in world-metres X/Z,
  with Y surface/depth from config; caller creates ECS entities.
  Each wet grid cell centre is covered, each dry centre is excluded.
  Input cap 4,194,304 cells (4 MiB mask); rectangle cap defaults 262,144.

`modules/rts/view/NereidLegacyWaterAdapter.h`:
- `game::water::importBakedWaterCellsToNereid(cells,w,h,cfg,'~')`
  reads **canonical simulation map tiles**, exact matching `~`.
  Prefer this path for games which edit baked maps after `waterNetwork`.
- `game::water::importWaterNetworkToNereid(network,w,h,legacyCm,cfg)`
  uses the **existing** `buildGeometry` and `waterCells` functions
  to reproduce the editor's network *source* wet mask before subsequent
  bridge/map editing. It validates matching legacy and Fixed cell metres.

Both are **explicit load-time converters**, not live systems. The returned
data can be attached to any native game's water-body ECS. They do NOT
touch `Network`, shore types, tiles, map saves, editor UI or old RTS
navigation. `WaterKind` is set by the importer config because baked
raster cells do **not** retain ocean/river identity; **never** infer the
legacy body type from the merged mask.

Example:
```cpp
#include "modules/rts/view/NereidLegacyWaterAdapter.h"
namespace nw=aiverse::lib::nereid;
nw::registerNereidWaterPersistence(sim);
nw::WaterRasterImportConfig cfg;
cfg.cellMeters=4_fx;
cfg.surfaceY=8_fx;
cfg.bottomY=-4_fx;
for (const auto& body : aiverse::game::water::importBakedWaterCellsToNereid(
        legacyMapCells, width, height, cfg)) {
    const aiverse::Entity e=sim.world().create();
    sim.world().add<nw::WaterBodyComponent>(e,body);
}
nw::addIndexedWaterSamplingSystem(sim,"water.sample");
```

### Actual verification

- GitHub connector wrote code; **native C++ build/CTest NOT RUN**.
- The newest CI had queued/pending jobs, with older pushes cancelled;
  a cancelled workflow is **not** proof that tests pass.
- Authored parity tests check every cell centre in an 80x64 map,
  short exact-span fixtures, source-network compatibility, baked
  tile-map compatibility, two genres, persistence and rollback.
- The authored test was inspected and corrected before documentation:
  initial probe is wet, moved probe dry; expected rectangles for a
  two-column vertical merge is 2, not 3.

Run:
```sh
cmake -S . -B build/ecs-test -DAIVERSE_BUILD_TESTS=ON
cmake --build build/ecs-test --target \
  aiverse_ecs_nereid_water_test aiverse_ecs_nereid_buoyancy_test \
  aiverse_ecs_nereid_geometry_test aiverse_ecs_nereid_query_index_test \
  aiverse_ecs_nereid_legacy_raster_test -j 4
ctest --test-dir build/ecs-test --output-on-failure -R '^ecs_nereid_.*_test$'
```
Also run legacy `water_*` tests, FPS and RTS ECS tests.

### Limits and specific next step

- This is cell-based parity at **grid centres**, not sub-cell shoreline
  fidelity. For high-quality polygon/mesh rendering use legacy shore data
  or a separate presentation adapter.
- Large fragmented rasters can produce many rectangles and ECS entities.
  Measure memory, rebuild and broadphase cost before enabling by default.
- The raster is not a moving/hydraulic water solver. Regenerate bodies
  explicitly if a host changes its baked map during gameplay. Do not
  silently sync two canonical sources.
- Next agent: compile all five tests, fix any compiler or runtime
  failure, verify old `water_*` bake output unchanged, and benchmark
  rectangle counts/import time and indexed sampling. Then add a native
  opt-in migration path or independent render/shoreline adapter, with
  further per-stage handoff.
 
## Stage 6 — adaptive sampling, native setup, CI coverage

**Branch**: `feature/nereid-water-core` (base:
`feature/universal-entity-inspector-fps`). Implementation in successive
commits (all on the same branch):
- `4ecb353964c0eb01b0626375039731c291830f4d`: adaptive water sampler
- `83081b60e98e8ce484655b20f0db1900fa3e33db`: native-game setup helper
- `e58e814ebf8d56a21c5054270ccd3fc22aef9417`: independent-host test
- `505c0fcb0fc07fa613c2662f2fe314caac8e799e`: CTest target
- `bc867d1df4791e2e519f409c14c50af8bedf07e1`: CI invokes all six NEREID tests
- `2b48e15d68a1dd99fbb88517dce2ec58cde10390`: catalog note

### Files, API, architecture

- Added `libraries/include/aiverselib/water/NereidAdaptiveWater.h`.
  Public `AdaptiveWaterSamplingConfig{minWaterBodies=16,minProbes=8}`,
  pure `chooseIndexedWaterSampling(bodies,probes,config)`, and
  `addAdaptiveWaterSamplingSystem(sim,name,config)`. The decision uses only
  deterministic ECS counts, **never FPS, time or platform measurements**.
  It chooses reference scanning for small workloads and stage-4 BVH for
  larger ones, preserving the existing `WaterContactComponent` and
  priority selection. Install only ONE sampling system.
- Added `libraries/include/aiverselib/water/NereidNativeWater.h`.
  `NativeWaterPipelineConfig`: optional geometry persistence, optional
  buoyancy persistence+system, optional Transform-to-probe stage,
  adaptive thresholds. `registerNativeWaterPipeline(sim,prefix,config)`
  registers components and the stage order in any native GameModule.
  `spawnNativeWaterBodies(world, vector<WaterBodyComponent>)` creates
  water-only entities in canonical input order (call at setup or a
  structural-change barrier). The host keeps canonical physics and
  entity lifecycle ownership.
- Added `tests/ecs_nereid_native_pipeline_test.cpp`:
  two unrelated host tags, reference vs forced-indexed configs, same-tick
  samples, native persistence, checksum parity, rollback, COW.
- Updated `CMakeLists.txt`:
  `aiverse_ecs_nereid_native_pipeline_test`.
- Updated `tests/run_ecs_experiment_suite.sh`:
  all SIX NEREID tests are now compiled and executed in the ECS
  experimental GitHub Actions suite. Prior CI suite omitted them, so
  previous CI checks were not evidence about NEREID.
- Updated `libraries/catalog.json`; API remains public through the
  `AIVERSE::lib_nereid_water` and
  `AIVERSE::lib_nereid_buoyancy` targets.

### Minimal native consumption

```cpp
#include <aiverselib/water/NereidNativeWater.h>
namespace nw=aiverse::lib::nereid;
// Inside GameModule::setup -- register once before sealing persistence:
nw::NativeWaterPipelineConfig options;
options.enableGeometry=true;    // optional custom shore polygons
options.enableBuoyancy=true;    // optional force OUTPUT only
nw::registerNativeWaterPipeline(sim,"water",options);
// Attach WaterProbeComponent + WaterContactComponent to query entities.
// Supply Transform or write WaterProbeComponent::position yourself.
// For baked maps, import waterBodiesFromRaster() at LOAD TIME and:
auto ids=nw::spawnNativeWaterBodies(sim.world(),waterBodies);
```

### Verification / blockers

**C++ build, six-test CTest run, and shell CI execution: NOT RUN in this
execution environment.** Source and CI registrations were updated via the
GitHub connector; this does NOT establish successful compilation.
At start of stage 6, Actions jobs for stage 5 were queued, not green.
Check latest Actions for the current branch. Do NOT merge until actual CI
and FPS/RTS legacy regression results are available.

```sh
cmake -S . -B build/ecs-test -DAIVERSE_BUILD_TESTS=ON
cmake --build build/ecs-test --target aiverse_ecs_nereid_water_test \
 aiverse_ecs_nereid_buoyancy_test aiverse_ecs_nereid_geometry_test \
 aiverse_ecs_nereid_query_index_test aiverse_ecs_nereid_legacy_raster_test \
 aiverse_ecs_nereid_native_pipeline_test -j 4
ctest --test-dir build/ecs-test --output-on-failure -R '^ecs_nereid_.*_test$'
# Experimental CI-equivalent (runs other ECS tests too):
bash tests/run_ecs_experiment_suite.sh
```

### Limits and next step

Adaptive thresholds are heuristic, not measured; forced-index parity is
authored but unverified. Dynamic imports and reconfiguration must occur
through a deterministic structural barrier, not a renderer or parallel
sampling callback. The helper does not migrate existing RTS or FPS
runtime and adds no water render backend. Existing `waterNetwork` and
tile baking remain untouched.

**Next agent**: inspect the six NEREID results and fix compiler/runtime
failures first, then benchmark index rebuild and linear/indexed/adaptive
sampling for different body/probe counts. Verify native-project target
usage and old `water_*` baking parity; only then consider connecting
existing game examples or water presentation. After the next code stage
append exact commit/branch, files, tests actually run, limitations and
the following action to this document.

## Stage 7A — sampler performance diagnostic (authored, not executed)

**Implementation SHA:** `a8520bdcaefd0e54cf6f418b2c50b44c578c02db`, branch
`feature/nereid-water-core`, base `feature/universal-entity-inspector-fps`.

Files: added `tests/ecs_nereid_sampling_bench.cpp`, updated
`CMakeLists.txt`, `tests/run_ecs_experiment_suite.sh`.

Standalone diagnostic target:
```sh
cmake -S . -B build/ecs-test -DAIVERSE_BUILD_TESTS=ON
cmake --build build/ecs-test --target aiverse_ecs_nereid_sampling_bench -j 4
./build/ecs-test/aiverse_ecs_nereid_sampling_bench
# Or existing ECS tools + this benchmark:
bash tests/run_ecs_experiment_suite.sh --bench
```

This benchmark compares `sampleWater` (linear scan) against rebuilding
`WaterQueryIndex` once **per tick** and sampling the same input points.
Workload matrix: 16/64/256 bodies × 8/128/1024 probes, 12 repetitions;
reports average microseconds per tick, relative speedup and sample digest.
It warms and validates the selected body/depth/signatures before comparing.
No assertion on speedup (hardware, build flags and runner loads vary);
wrong gameplay parity causes a failure. It does not mutate simulation
state and wall-clock timing is **never** part of gameplay decisions.

**Actual verification:** code committed by GitHub connector; C++ bench
not built/run in this environment, so **no timing numbers are known**.
The latest GitHub workflow was queued when inspected, not verified.

**Limitations:** geometry in this fixture uses discs; profile polygon/spline
stress separately if adopting index by default. The measured ratio
includes build-per-tick cost, but not the contact-pool write and source
transform stages. Do not change adaptive defaults until real results
show a defensible break-even. Next: implement a universal presentation
mesh adapter; then obtain actual CI and benchmark output. Update this
handoff immediately after that stage with SHA/files/tests/limits.

## Stage 7B — backend-neutral water surface mesh

**Implementation commit:** `5957287763ca3879d58bb0507755aa5dbe7e26ff`,
branch `feature/nereid-water-core`. Files added:
`libraries/include/aiverselib/water/NereidWaterSurfaceMesh.h` and
`tests/ecs_nereid_surface_mesh_test.cpp`; files updated:
`CMakeLists.txt` and `tests/run_ecs_experiment_suite.sh`.

### Public API / ownership

`buildWaterSurfaceMesh(body,optionalGeometry,WaterSurfaceMeshOptions)`
returns an entirely **presentation-only** `WaterSurfaceMesh`, with
`WaterSurfaceVertex {x,y,z,u,v,shore}` and 32-bit triangle indices.
`WaterSurfaceLod` = Near / Medium / Far controls subdivisions and
disc polygonal resolution; `cameraX,Z` and `oceanPatchRadius` bound an
otherwise infinite ocean to a movable square patch. `foamWidth`
determines vertex proximity to shore segments; `uvScale` controls
texture-space units. `maxVertices` limits memory.

Shapes: box, disc, infinite camera patch, concave simple polygons with
ear clipping (+Y triangle winding for either polygon orientation), and
river ribbons with variable half-width and capped miter joins.
Self-intersecting and degenerate polygons return empty meshes, rather
than incorrect triangles. Source ECS water body and optional geometry
are read-only. Positions/Y come from fixed authoritative data, but
presentation coordinates/UVs/foam are floating-point and **must never
feed back into the deterministic simulation**.

`WaterSurfaceStyle` provides an opt-in material-input default
(color, foam tint/strength, alpha and specular) for the renderer
adapter to consume in a subsequent isolated stage. No new ECS schema,
shader binding, material persistence or scene draw path was added yet.
This code deliberately does **not** alter the existing RTS terrain water
or waterNetwork/bake, and does not introduce per-genre water mesh logic.

### Verification and next action

CMake executable / CTest `ecs_nereid_surface_mesh_test` tests box area,
+Y winding, near/far disc LOD, capped vertices, concave/reversed
polygons, invalid self-intersection, varying-width river, shore
factor and camera-relative ocean bounds. The ECS experimental shell
runner also builds/runs this fixture. **Actual C++ build and CTest: NOT
RUN / UNVERIFIED.** GitHub connector push succeeded; Actions status
needs separate confirmation.

```sh
cmake --build build/ecs-test --target aiverse_ecs_nereid_surface_mesh_test -j 4
ctest --test-dir build/ecs-test --output-on-failure -R '^ecs_nereid_surface_mesh_test$'
```

Limits: static surface sheet only; wave vertex displacement, depth
refraction, underwater treatment and native renderer integration are
not implemented. GPU vertex lifetime, camera-dependent patch rebuild,
CPU upload churn and platform-specific rendering need explicit
profiling and testing. **Next:** add a small RHI renderer bridge using
existing Renderer3D/MeshBuilder with a clean ownership/rebuild
contract. Keep shader effects as a separate change. First fix any
failed native CI tests before declaring this production-ready.

### Stage 7B immediate source review correction

**Fix commit:** `652ca2f3358f1384ee805cc2bdda72666910c449`.
In `NereidWaterSurfaceMesh.h`, the simple-polygon validator
incorrectly checked distance from vertex A to its **own A-B segment**
(which is always zero). That caused every polygon to be rejected
before ear clipping. Replaced it with the actual A-B segment-length
check, and explicitly included `<cstddef>` and `<iterator>`.
This was found by source review, **not by an executed C++ test**.
The authored concave/reversed polygon fixture must still be compiled
and run. All other stage-7B verification limitations remain.

## Stage 7C — real Renderer3D upload/draw bridge + explicit water shader flag

**Implementation commit:** `b96be812ee14cbb139ff09beae13e0fbc58a2bdb`
on `feature/nereid-water-core`. **Documentation commit:** this update.

Changed/added:
- New `engine/render/NereidWaterRenderer.h`.
- New `tests/ecs_nereid_renderer_contract_test.cpp`.
- Modified `engine/render/Renderer3D.h`, `Renderer3D.cpp`, `Shaders.h`.
- Registered CTest target in `CMakeLists.txt` and CI shell runner in
  `tests/run_ecs_experiment_suite.sh`.

### API, architecture and ownership

`render::uploadNereidWaterSurface(device,cpuMesh,style)` uploads
`WaterSurfaceMesh` from stage 7B into the *existing*
`Renderer3D::MeshBuilder` static VB/IB structure. It composes
per-vertex deep water tint with `WaterSurfaceVertex.shore` foam
in a normal vertex color; it does **not** invent a second RHI.

`render::drawNereidWaterSurface(renderer,mesh,style)` queues the
existing `Renderer3D::drawMesh` with alpha < 0.99, glossy specular,
`kDrawNoCastShadow`, and **new `kDrawWaterSurface=256` flag**.
`render::releaseNereidWaterSurface(device,mesh)` destroys both GPU
buffers and zeros the handle. **Caller owns** lifetime, static caching,
draw order, camera-relative infinite-patch refresh and device-loss
rebuild. No renderer automatically discovers NEREID ECS entities.

Renderer3D stores the opt-in flag via per-draw `u.params.x=-1`
while preserving `0` for lit and `1` for unlit. GLSL and Metal
now opt in to existing six-wave normal animation at *any* water
elevation when the flag is set. The unlit mix clamps this sentinel
before shading, so old lit/unlit/debug draw modes are preserved.
Trace-tier SSR can be applied to marked water even when Y is not near
zero. Pre-existing height-based terrain water behavior is unchanged.
No new textures or uniform block/layout/bindings are required.

`render::nereidWaterVertexColor` and
`render::validNereidWaterStyle` are pure CPU helpers; the latter
requires a translucent alpha and specular value 0.85–1.0 so the
existing reflection/render path recognizes water.

### Verification status and limitations

**Native C++ build, GLSL compile, Metal compile, real GPU screenshots,
and CTest are NOT RUN / UNVERIFIED**. New CPU contract test
`ecs_nereid_renderer_contract_test` checks foam vertex tint,
material validation, flag allocation and GLSL/MSL source sentinels.
This does NOT prove shader compilation or any GPU result. It is
also included in the ECS experiment shell runner. Check latest
GitHub Actions and fix failures before merging.

```sh
cmake -S . -B build/ecs-test -DAIVERSE_BUILD_TESTS=ON
cmake --build build/ecs-test --target   aiverse_ecs_nereid_surface_mesh_test   aiverse_ecs_nereid_renderer_contract_test -j 4
ctest --test-dir build/ecs-test --output-on-failure   -R '^ecs_nereid_(surface_mesh|renderer_contract)_test$'
# Also compile/launch the actual graphics editor/client on GL and Metal.
```

**Important limits:** the renderer bridge is opt-in, so FPS/RTS scenes
have not been migrated. Planar reflections are a single render
pass/plane chosen by existing `Renderer3DReflect.cpp`; multiple
water bodies at different Y levels are not yet reflected correctly.
SSR is available only when the existing trace tier has valid color
and depth inputs, and there are still no refractive depth lookups,
underwater/post effects, water collision waves or hydraulic solve.
The bridge tints foam by vertices (not per-pixel shoreline distance).
Explicit draw flag changes global lit shaders, so check regressions
on *both* GLSL and MSL before production.

**Next agent:** First obtain actual build/test results for all NEREID
targets (old and new) and old RTS water tests. Then validate the
renderer on actual GL and Metal, especially surface Y != 0 and
multi-plane reflection fallback. Add depth/refraction, per-pixel foam
or underwater view as another isolated renderer stage with a new
handoff after every implementation commit. Keep deterministic water
CPU authoritative and all GPU effects presentation-only.

### Stage 7C follow-up: coarse-LOD foam fix

**Implementation commit:** `a6fe7057b63485a8e9806e97ac4d979f23b826cf`.
After source review, the far surface geometry was discovered to contain
boundary-only vertices for some shapes. Per-vertex shoreline foam
therefore coloured the **entire far surface** as foam. The far LOD now
skips shoreline foam entirely (near/medium retain the distance-based
mask). The `ecs_nereid_surface_mesh_test` fixture asserts far box
vertex foam is zero. This is a visualization correctness fix; the
GPU/CTest pipeline is **still not verified**.
