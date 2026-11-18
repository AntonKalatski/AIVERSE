# PHYSIS / NEREID / TerraWeave — shared ownership and integration contract

Status: **design / coordination contract only** (2026-11-18). This document does **not** claim that cross-system adapters, compilation or tests already exist.

## Parallel branches and owners

| Capability | Code owner / working branch | Authoritative state and public entry | Must not implement |
|---|---|---|---|
| PHYSIS (nature/climate) | **PHYSIS agent**, `feature/physis-nature-simulation`, draft PR #25 | `engine/physis/`; `aiverse::physis::install`, `environmentAt`, `forEachPresentationCell`; ECS atmosphere, precipitation, surface snow/wetness/ice, fire/quake events | Water-body/query/buoyancy/river/ocean geometry, shoreline geometry, terrain mesh, VFX/render backend |
| NEREID (water) | **Water agent**, `feature/nereid-water-core`, draft PR #23; check current PR/branch head before integrating | `libraries/include/aiverselib/water/Nereid*.h`; `aiverse::lib::nereid::WaterBodyComponent`, `WaterContactComponent`, `WaterSample`, `sampleWater`, `registerNativeWaterPipeline` | Reimplement climate/PHYSIS, terrain/foliage generation, genre-specific water forks |
| TerraWeave (land/biome/foliage) | **Terra agent**, `feature/terraweave` | `engine/world/TerraWeave*.h`; `aiverse::terraweave::generateChunk`, `heightAtCm`, `scatter`, flora/arbor adapters | Implement a new climate simulation or water solver; maintain a duplicate mutable water/atmosphere world state |
| Composition / host adapter | **Integration owner (assigned when merging)**, separate integration branch or PR | Optional adapters consuming PHYSIS and NEREID and presenting to Terra; host `Simulation` system ordering, persistence, replay/rollback, cross-module tests | Add bidirectional includes to foundational PHYSIS/NEREID/Terra core or silently migrate legacy RTS |
| VFX/render | **Render/FX agent** (separate) | FX/render presentation consuming immutable snapshots | Advance climate/water/terrain authority or persist GPU handles in ECS |

These are **subsystem roles, not claims about which human is assigned**. When agents create new PRs, record exact owner, branch, head SHA and API compatibility in the handoff. No agent should commit into another active agent's working branch without coordination.

## One source of truth for each property

- **TerraWeave:** static/generator terrain height, biome baseline, soil/exclusions, vegetation descriptors. PHYSIS may read terrain height/biome via an explicit adapter; does not write generated terrain.
- **PHYSIS:** dynamic atmosphere and weather (rain/snow normalized intensities, wind, humidity, temperature); surface wetness/snow/ice and fire/quake components. Generic `EnvironmentalResponse` is the read-only contract.
- **NEREID:** water body identity/shape, surface/bottom elevation, water-contact sampling, current/flow and buoyancy outputs. NEREID is the future canonical water gameplay authority when enabled by the host.
- **Important current overlap:** PHYSIS `HazardCell::waterDepth` / `advanceDisasters` include an **experimental shallow-flood cellular toy model**, not a substitute for NEREID bodies. Do **not** treat that depth as another canonical lake/river/ocean level when NEREID is active. Until a tested opt-in ownership switch / transfer adapter exists, host integration must avoid concurrent PHYSIS flooding for the same region. Fire/quake can remain PHYSIS-owned; decoupling those from the flood pass is a follow-up task.
- Legacy `modules/rts/view/RtsWater.*`, `waterNetwork`, `WaterOps` and baked map cells remain untouched until migration is proven with old tests; NEREID's existing raster import is the opt-in path, not a forced replacement.

## Geometry, units and coordinates (mandatory at boundaries)

| System | Horizontal | Vertical | Values |
|---|---|---|---|
| TerraWeave | signed `int64_t xCm, zCm`, world centimetres | `heightAtCm` / `heightsCm`, signed cm | integer authoritative generation |
| PHYSIS | X/Z grid `Cell{x,z}` with `Settings::cellSizeMeters` + `originX/originZ` | `Cell::elevation`, `Fixed` metres | Q47.16 `Fixed`, signed metre coordinates for `cellAtPosition` |
| NEREID | `Vec3.x/z`, `Fixed` metres | `Vec3.y`, `surfaceY`, `bottomY`, `Fixed` metres | World X/Z horizontal; Y up; `WaterSample` |

- Convert `TerraWeave cm -> Fixed metres` **explicitly**, using checked integer arithmetic; do not cast signed centimetres to metres or assume both generators share the same seed, origin or cell scale.
- Use floor division (including negative coordinates) to select PHYSIS cells; use exact NEREID point/footprint semantics. A PHYSIS cell is not a NEREID water body; many PHYSIS cells may overlap one water body and vice versa.
- Choose a documented terrain sampling convention (cell centre, corners, and NEREID waterline) to avoid shoreline gaps and intermittent flooded/grass terrain.
- For very large worlds, avoid precision loss/overflow in world-to-Fixed conversion; absolute extent and origin-rebasing policies require explicit validation.

## Data flow to Terra (no core dependency inversion)

1. TerraWeave generates/loads terrain chunks and immutable baseline biome/height. Its existing `scatter` remains pure and deterministic.
2. **Host-owned adapter**, not TerraWeave core, samples Terra height/biome and optionally configures PHYSIS `Cell::elevation`, `ClimateZone` and `SurfaceMaterial` before/at deterministic tick barriers.
3. PHYSIS advances exactly **once** per authoritative tick and publishes weather, wetness, snow, ice and hazard read-only snapshots (`environmentAt` / `forEachPresentationCell`).
4. NEREID maintains/query samples of its own ECS water bodies via `aiverse::lib::nereid::sampleWater` or its registered pipeline. **Water agent owns any precipitation-to-water-volume conversion** and water dynamics. Current PHYSIS `rain` is normalized intensity, NOT water depth in metres or flow in cubic metres; do not directly add it to `surfaceY`. Conversion needs calibrated units/timestep and a versioned deterministic policy.
5. Terra's **optional** render/scene adapter uses both: water body/shore masks, shoreline/waterline from NEREID; wetness/snow/ice and wind from PHYSIS; terrain collision/slope/mesh and foliage from TerraWeave. Composition must be read-only to authoritative water/weather; dirty-chunk invalidation occurs only on meaningful data/version changes.
6. Physics, AI/nav, audio and VFX consume the same canonical snapshots; they may return explicit gameplay events at next tick boundaries, not mutate unrelated authoritative systems during their internal update.
7. Legacy RTS views remain opt-in and visually unchanged until dedicated conversion and regression tests pass.

### Expected optional adapter interface (PROPOSAL, NOT IMPLEMENTED)

```cpp
// e.g. engine/world/adapters/TerraEnvironmentAdapter.h (future integration PR)
struct TerraEnvironmentCellView {
    // Converted Terra height/biome + read-only PHYSIS EnvironmentalResponse
    // + optional NEREID WaterSample, with explicit valid/absent flags.
};

// Host owns composition: sample PHYSIS and NEREID independently at a
// documented world-space X/Z position, then produce view/terrain masks.
// Never use a single mutable shared `waterDepth` field for both solvers.
```

A useful first consumer is a CPU-only terrain debug overlay: snow/wetness colouring and NEREID waterline, without modifying authoritative terrain or water. Its contract test should exercise two unrelated native hosts, negative coordinates, multiple PHYSIS cells per NEREID body, shoreline masks, save/load, rollback and module ordering.

## Scheduling and networking

- Initial setup: spawn/load Terra static input; install/register PHYSIS and NEREID persistence in `GameModule::setup` before the registry is sealed; inject baseline material/elevation before the first PHYSIS advance.
- Tick: canonical game input/physics update as appropriate; update PHYSIS once; optional calibrated rain/snowmelt input to NEREID; NEREID water pipeline once; sample contacts and compute optional buoyancy; host physics integrates exactly once. Exact system registration order is project-owned and must be documented/tested.
- Presentation: read only after committed simulation tick; no GPU/OS time/randomness in authoritative calculations.
- Keep rollback/serialization and network hashes on ECS authority only. Do not replicate a separate Terra/PHYSIS copy of NEREID water bodies, or recompute important gameplay water state from GPU waves.

## Acceptance before merging the parallel branches

- Rebase/merge with explicit conflict review of `AGENTS.md`, CMake, test runners, per-stage handoffs; preserve other agents' changes.
- Build and run `./run-tests.sh --filter '^(physis_|ecs_nereid_|terraweave)'`; run the relevant legacy `water_*`, terrain, and native ECS regression tests. **As of this contract: no cross-module build/CTest result has been produced.**
- Verify independent vanilla PHYSIS, NEREID, TerraWeave builds still work. No forced dependency from PHYSIS core to NEREID/Terra, or vice versa; opt-in adapter is its own target.
- Test deterministic output across snapshot restore, save/load and replay; negative coords and centimetres/metres conversion; elevation/waterline coherence; no double flooding.
- Update **root `AGENTS.md`**, each subsystem handoff, this contract and any new public API docs after every stage. New implementation stages require separate code and handoff commits with exact SHA and real test status.

## Working links

- PHYSIS: PR #25 (`feature/physis-nature-simulation`), `docs/physis.md`, `engine/physis/AGENTS.md`
- NEREID: PR #23 (`feature/nereid-water-core`), `docs/nereid/ARCHITECTURE.md`, `docs/nereid/HANDOFF.md`
- TerraWeave: `feature/terraweave`, `docs/terrain/TERRAWEAVE.md`, `docs/terrain/AGENT_HANDOFF.md`
