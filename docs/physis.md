# AIVERSE PHYSIS

PHYSIS is the **genre-independent** nature simulation for AIVERSE. Weather, local climate zones, wet ground, snowfall, fire, shallow flooding and quake intensity are authoritative ECS state. It is **not** coupled to FPS/RTS or any renderer.

## Modules

| Header | Responsibilities |
|---|---|
| `engine/physis/Physis.h` | Fixed-point authoritative cell grid, Atmosphere/Surface/State, seeded tick, queries |
| `engine/physis/PhysisClimate.h` | ClimateZone, WeatherFront, seasonal forcing, regional tick |
| `engine/physis/PhysisEnvironment.h` | SurfaceMaterial, snow/wetness/ice, generic response and friction/runoff hints |
| `engine/physis/PhysisDisasters.h` | Optional fuel/fire, shallow flooding, QuakeEvent, deterministic old/new buffers |
| `engine/physis/PhysisWind.h` | 3D wind zones, deterministic gusts, shelter attenuation and `windAt` |
| `engine/physis/PhysisGravity.h` | World gravity baseline, additive/priority gravity fields, `gravityAt` |
| `engine/physis/PhysisStorm.h` | Opt-in hurricane/tornado vortex, storm tracks, climate rain/pressure forcing |
| `engine/physis/PhysisForces.h` | Quadratic wind drag, force receiver/output ECS and pure force sampling |
| `engine/physis/PhysisPresentation.h` | Unified read-only `NatureFieldSample` for native Terrain/FX/Physics hosts |
| `engine/physics/PhysisBox3D.h` | **Optional Box3D** physics::Scene adapter; does not modify PHYSIS core |
| `engine/physis/PhysisDiagnostics.h` | Read-only bounded debug raster, scalar layers and 16-bit normalization |
| `engine/physis/PhysisDebugPalette.h` | Renderer-independent CPU diagnostic colours and invalid-cell masking |
| `apps/editor/windows/PhysisWindow.cpp` | Optional dockable editor heatmap with separate demo World and read-only live ECS source |
| `engine/physis/PhysisSpatialIndex.h` | Transient COW snapshot, 2D bins, ordered candidate selection for wind/gravity/storm fields |
| `engine/physis/PhysisSceneGeometry.h` | Pure Fixed-metre tile quads for Scene projection (presentation only) |
| `engine/physis/PhysisProjectConfig.h` | Optional JSON `physis` map block → `InstallOptions` / `installFromMap` for any native GameModule |
| `engine/physis/PhysisFieldAssets.h` | Validated, stable-ID wind/storm/gravity asset parser and deterministic ECS spawning |
| `apps/editor/PhysisFieldEditor.h` | Universal editor field list, typed inspector, Scene markers and one-Undo drag gizmo |
| `apps/editor/PhysisAuthoring.h` | Validated RTS/FPS map authoring, one `MapDoc::beginEdit` per Apply, Undo/Redo |
| `apps/editor/PhysisSceneOverlay.h` | Editor-only camera projection of read-only PHYSIS fields and zone rings |
| `engine/physis/PhysisIntegration.h` | Generic Simulation::addSystem, persistent ECS schemas, read-only presentation feed |

## Quick integration for any C++ native game

```cpp
#include "engine/physis/PhysisIntegration.h"
void MyGame::setup(aiverse::Simulation& sim, const aiverse::Json&, const aiverse::Json&) {
    aiverse::physis::InstallOptions options;
    options.climate.seed = sim.config().seed;
    options.climate.width = 32;
    options.climate.height = 32;
    options.enableDisasters = true;
    options.enableStorms = true;        // optional hurricane/tornado update
    options.enableForces = true;        // optional ECS ForceReceiver/ForceOutput pass
    options.enableWindZones = true;     // persist WindZone/WindShelter when used
    options.enableGravityFields = true; // persist GravityField/GravitySettings when used
    // When NEREID owns water: options.floodMode = physis::FloodMode::ExternalAuthority;
    aiverse::physis::install(sim, options);
}

// After committed Simulation::step() (e.g. from renderer/editor):
aiverse::physis::forEachPresentationCell(sim.world(), [](const auto& cell) {
    // cell.environment.wind / wetness / rain / snow / ice
    // cell.hasHazards ? cell.hazards.fire / waterDepth / quake : none
});
```

All values used by gameplay use `Fixed`; never feed presentation floats back into PHYSIS. PHYSIS world coordinates are a static X/Z grid; `Cell::elevation` is project-assigned. For custom host scheduling choose exactly one `advance` variant per tick. To register PHYSIS into generic Simulation and persistent ECS schema, install during `GameModule::setup`.

## Current implementation boundary

- The implemented weather evolution is a deterministic simplified local model, not a physically accurate global circulation model; seasons are triangle waves, fronts have Manhattan falloff.
- Fire, flood and quakes are base ECS mechanics; hurricane/tornado are simplified deterministic vortex/forcing fields, **not** complete atmospheric CFD, volcano, avalanche, fluid, terrain deformation or destruction solvers.
- Responses like traction multiplier and runoff potential are coefficients/hints. An **optional, explicit** `engine/physics/PhysisBox3D.h` adapter now applies PHYSIS wind/ gravity corrections to `physics::Scene` dynamic bodies, but no automatic Scene/WorldScene hookup or production Terra, NEREID, Foliage, VFX, Audio or Editor integration is wired.
- Grid updates are currently serial and eager; do not advertise parallel job execution, streaming, distributed weather or efficient millions-of-cells throughput.
- No compile/CTest/CI verification has been completed in this connector session; **code present does not mean tests passed**.

## Tests

Use the project-wide unified runner (required by AGENTS.md):

```sh
./run-tests.sh --filter '^physis_'
```

This performs full Debug configure/build before filtered CTest. For investigating a standalone target, see the individual handoff for exact CMake commands.

## AI agent continuity

- Stage 1: [Core](agent-handoffs/physis-core-2026-11-18.md)
- Stage 2: [Climate](agent-handoffs/physis-climate-2026-11-18.md)
- Stage 3: [Environment](agent-handoffs/physis-environment-2026-11-18.md)
- Stage 4: [Disasters](agent-handoffs/physis-disasters-2026-11-18.md)
- Stage 5: [Simulation/persistence](agent-handoffs/physis-integration-2026-11-18.md)
- Stage 6: [PHYSIS / NEREID / TerraWeave ownership and reuse](agent-handoffs/physis-ownership-terra-water-2026-11-18.md)
- Stage 7: [External water authority mode](agent-handoffs/physis-flood-authority-2026-11-18.md)
- Stage 8: [Wind, gust and shelter](agent-handoffs/physis-wind-2026-11-18.md)
- Stage 9: [Gravity fields](agent-handoffs/physis-gravity-2026-11-18.md)
- Stage 10: [Hurricane and tornado events](agent-handoffs/physis-storm-2026-11-18.md)
- Stage 11: [Forces and Box3D adapter](agent-handoffs/physis-forces-box3d-2026-11-18.md)
- Stage 12: [Numerical bounds and native CI](agent-handoffs/physis-numerical-ci-2026-11-18.md)
- Stage 13: [Unified environment sampling and save compatibility](agent-handoffs/physis-sampling-persistence-2026-11-18.md)
- Stage 14: [Read-only diagnostic rasters](agent-handoffs/physis-diagnostics-2026-11-18.md)
- Stage 13: [Unified nature field sampling and save-schema compatibility](agent-handoffs/physis-sampling-persistence-2026-11-18.md)


- Stage 15: [Fast diagnostic ECS reads](agent-handoffs/physis-diagnostic-fastpath-2026-11-18.md)
- Stage 16: [Snapshot-backed spatial index](agent-handoffs/physis-spatial-index-2026-11-18.md)
- Stage 17: [PHYSIS editor debug view](agent-handoffs/physis-editor-debug-view-2026-11-18.md)
- Main merge: [merged InputFlow/main verification](agent-handoffs/physis-main-merge-2026-11-18.md)
- Stage 18: [Scene overlay + Inspector authoring/Undo](agent-handoffs/physis-editor-scene-inspector-2026-11-18.md)
- Stage 19: [Field assets, Gizmos and single-Undo editing](agent-handoffs/physis-field-assets-gizmos-2026-11-18.md)Each next stage gets code commit(s) followed by a separate handoff commit with SHA, paths, real tests, gaps, and the next exact task. Agent-specific constraints: `engine/physis/AGENTS.md`.

## Cross-system ownership (PHYSIS / NEREID / TerraWeave)

PHYSIS owns **climate/weather, natural event fields and environmental force/gravity policies**, but never Box3D rigid-body integration itself. NEREID (Water agent,
draft PR #23) owns water-body geometry, flow, contacts and buoyancy; TerraWeave
(Terra agent, draft PR #24) owns terrain, biomes, vegetation and land rendering.
Use the opt-in host integration plan and precise metre/centimetre conversion in
[`physis-nereid-terraweave-integration.md`](physis-nereid-terraweave-integration.md).
The existing PHYSIS shallow-flood prototype is **not** an authoritative
replacement for NEREID water. When a host uses NEREID as the water authority,
set `InstallOptions::floodMode = FloodMode::ExternalAuthority`: PHYSIS continues
to update fire and quake but clears/skips its local shallow-flood depth. This
opt-in switch is authored and covered by tests; cross-module NEREID/Terra
integration and real build/CTest verification remain pending.

This is a **design contract only**. Terra/NEREID integration, rendering and
cross-system tests remain NOT IMPLEMENTED / NOT RUN on this branch.

## Wind, gravity, storms and optional Box3D usage

```cpp
#include "engine/physis/PhysisIntegration.h"
#include "engine/physics/PhysisBox3D.h" // link AIVERSE::physics, if enabled

// During GameModule::setup:
aiverse::physis::InstallOptions options;
options.enableStorms = true;
options.enableForces = true; // only if generic ECS force outputs are required
aiverse::physis::install(sim, options);

aiverse::physis::StormEvent tornado;
tornado.kind = aiverse::physis::StormKind::Tornado;
tornado.center = {10_fx, 0_fx, 10_fx};
tornado.eyeRadiusMeters = 20_fx;
tornado.outerRadiusMeters = 150_fx;
tornado.heightMeters = 500_fx;
sim.world().add<aiverse::physis::StormEvent>(sim.world().create(), tornado);

// Physics owner: after the authoritative PHYSIS tick, just before Box3D Scene::step:
for (aiverse::physics::BodyHandle body : optedInBodies)
    aiverse::physics::applyPhysisToBody(box3dScene, sim.world(), body);
box3dScene.step();
```

PHYSIS authoritative types are `Fixed` ECS data. The external Box3D bridge
converts from/to Box3D floats **only at this boundary**. Box3D already applies
scene gravity; the adapter applies **delta gravity** and wind drag via
`Scene::applyForce`, not a second gravity force. Do **not** also apply
`ForceResult::totalForce` to the same Box3D body. Opt-in kinematic character
controllers need a separate locomotion integration; neither hardcoded scalar
`CharacterParams::gravity` nor native Body motion is overridden here.

Physics is opt-in. Run `./run-tests.sh --filter '^physis_'` and, with
`-DAIVERSE_ENABLE_PHYSICS=ON`, run CTest `physis_box3d` and the existing
`physics_runtime` / `physics_scene`. These tests are **authored but NOT RUN**.
There are no physically accurate Navier–Stokes hurricanes, tornado debris
destruction, N-body orbital solvers, Terra foliage wind deform or water surge
in the current implementation.

## Read-only combined sampling (for Terra, VFX, Physics and tools)

```cpp
#include "engine/physis/PhysisPresentation.h"

aiverse::physis::NatureFieldSample nature;
if (aiverse::physis::sampleNatureField(sim.world(), queryPosition, nature)) {
    const auto wind = nature.windVelocity; // 3D Fixed metres per second
    const auto gravity = nature.gravityAcceleration; // m/s squared
    if (nature.hasAtmosphere) {
        const auto rain = nature.environment.rain; // normalized, not water volume
        const auto snow = nature.environment.snowpack;
    }
    if (nature.hasHazards) {
        const auto fire = nature.hazards.fire;
    }
}
```

`sampleNatureField` is immutable and returns true when PHYSIS is initialized,
including outside the climate grid; `hasAtmosphere`/`hasHazards` indicate
which grid data is present. It does not depend on TerraWeave/NEREID/Box3D
headers. Terra should consume this as a read-only environment overlay and
sample NEREID's canonical waterline separately. **No production Terra/NEREID
adapter or render scene is linked yet.**

**Save compatibility:** `Simulation::PersistentEcsRegistry` stores a strict
list of schema IDs. Therefore PHYSIS **does not register** the new optional
wind/gravity/storm/force component types by default. Set
`InstallOptions.enableWindZones` when persisting `WindZone`/`WindShelter`,
`enableGravityFields` for `GravityField`/`GravitySettings`,
`enableStorms` for `StormEvent`, and `enableForces` for
`ForceReceiver`/`ForceOutput`. Existing Stage 1–5 save registry remains
unchanged when these switches are off. **Switching flags on an existing saved
game changes the manifest and is not an automatic save migration.** Save and
load must use the same feature flags. Tests are authored, not run.

## Diagnostic raster for editor/debug consumers

```cpp
#include "engine/physis/PhysisDiagnostics.h"
aiverse::physis::DiagnosticRequest query;
query.originX = -16; query.originZ = -16;
query.width = 32; query.height = 32;
query.stride = 1;
query.layer = aiverse::physis::DiagnosticLayer::WindSpeedMetersPerSecond;
aiverse::physis::DiagnosticRaster raster;
if (aiverse::physis::sampleDiagnosticRaster(sim.world(), query, raster)) {
    // raster.rawValues = Fixed::raw data; raster.valid = inside PHYSIS grid
    // diagnosticNormalized(raster, i) = 0..65535 for view/shader coloring
}
```

The diagnostic API is **CPU-only and read-only**: it does not render an
overlay in Scene, change physics, mutate the authoritative World, or sample
NEREID water. It is capped at 65,536 cells per query; missing climate/hazard
cells are marked invalid, not replaced with misleading zeros. Only PHYSIS
standalone shallow flooding is displayed by `LocalFloodMeters` (NEREID remains
canonical water authority). A future editor adapter may upload/use the returned
raster as a heatmap, with no PHYSIS dependency on the renderer. Tests are
authored but not yet confirmed by native CTest/CI.

## Indexed reads for large diagnostic or editor sampling sessions

The default `windAt`, `gravityAt`, `stormsAt`, `sampleNatureField`,
and `sampleDiagnosticRaster` remain available and do not require an index.
For repeated queries against **one committed ECS snapshot**, use the opt-in
spatial accelerator:

```cpp
#include "engine/physis/PhysisSpatialIndex.h"
#include "engine/physis/PhysisDiagnostics.h"
aiverse::physis::FieldSpatialIndex index(sim.world(),
    {.tileSizeMeters = 256, .maxTilesPerVolume = 1024});

// Stable values pinned to the index's original COW world snapshot:
aiverse::physis::NatureFieldSample nature;
if (index.sample(queryPosition, nature)) {
    const auto wind = nature.windVelocity;
    const auto gravity = nature.gravityAcceleration;
}

// Optional accelerated debug raster for vector field layers:
aiverse::physis::DiagnosticRequest request;
request.layer = aiverse::physis::DiagnosticLayer::WindSpeedMetersPerSecond;
aiverse::physis::DiagnosticRaster image;
aiverse::physis::sampleDiagnosticRasterIndexed(index, request, image);
```

The transient index groups bounded wind zones, shelters, gravity fields and
storm volumes into **X/Z world-metre tiles**; final 3D range/timing checks
remain in the canonical PHYSIS policy. Global gravity and very large volumes
use a bounded fallback list; candidate traversal preserves ECS insertion/pool
order for Fixed-point accumulation and gravity-priority tie-breaks.

Build once after an authoritative committed tick, use for many queries and
**rebuild after an ECS change, rollback or climate/storm movement**. It is
not persistent state, not a spatial index for climate WeatherFront/QuakeEvent
simulation, and not a world streaming/job scheduler. The index holds a
copy-on-write World snapshot; keeping many historical indexes resident may
retain COW pages and increase memory. No native benchmark or compiler success
has been observed yet; tests are source-only until CTest runs.

## Stage 17 — PHYSIS editor debug window

The editor now discovers `apps/editor/windows/PhysisWindow.cpp` automatically
through its existing `AIVERSE_EDITOR_WINDOW` registry and CMake glob.
Open **Мир → PHYSIS · Природа**. The panel is **closed by default**.

- When the active `Editor::sim()` has PHYSIS initialized, it displays a
  read-only raster of live PHYSIS ECS climate, surface, wind, gravity, fire,
  earthquake or standalone local flood state. The panel **does not call
  `Simulation::step` on the game's World**, does not edit game.json, map
  documents or Box3D, and does not inject genre-specific scene entities.
- When the current game does not initialize PHYSIS, the user can explicitly
  create a separate **PHYSIS demo World** in the panel, with wind, a tornado,
  local gravity, a climate gradient and a fire. Demo step controls advance
  only that independent World; they do not mutate the current project.
- Layers, palette, sample stride, window size (8..64 PHYSIS cells) and X/Z
  centre can be adjusted. Clicking a cell displays the fixed-point value,
  climate cell coordinates, 3D wind and gravity. The panel outlines
  active wind zones (W), storm radii (T/H) and gravity volumes (G).
- Scalar weather/hazard layers read ECS directly; wind/gravity layers use
  the transient `FieldSpatialIndex`. Refresh occurs on source change,
  new tick or explicit **Обновить данные**. Edits/rollback that retain the
  exact tick require manual refresh until the editor gains revision tokens.

`PhysisDebugPalette.h` exposes `DebugColor`, `DebugPalette`,
`debugGradient` and `diagnosticColor` for native CPU tools. This is
presentation-only; colour values are never fed back into simulation.

**Not yet implemented:** world-space overlay projected into the Scene camera,
a live PHYSIS agent Inspector for editing component data, general project
`game.json` PHYSIS auto-install, GPU textures and particle/VFX integration.
The window is an actual editor GUI implementation, but **native editor
compilation, UI screenshot and manual clicking have NOT been verified**.
Use `./run-tests.sh --filter '^physis_'` and build/test the editor on macOS
before claiming it works end-to-end.


## Stage 18 — real Scene viewport overlay and Inspector map authoring

After the main merge `810feba70f01cdc35003ac80ea8486f39dc16049`,
the editor Scene adds an opt-in transparent heatmap drawn **in the actual 3D
view**, projected through the existing `RTSCamera::worldToScreen`, plus
world-space rings for WindZone, StormEvent and GravityField. The toolbar
controls live under **Мир → PHYSIS · Природа**; toggle **Показать 3D-оверлей
в «Сцене»**. This is an ImGui presentation overlay (not shaded 3D geometry or
Box3D collision) and uses the same read-only `DiagnosticRaster` and bounded
`FieldSpatialIndex`. It only reads a live simulation that already has PHYSIS;
it does not install or tick the active world.

The shared Inspector under **Свойства карты** now includes a PHYSIS section:
**Создать PHYSIS**, **Настроить PHYSIS**, **Удалить PHYSIS**. The edit dialog
has structured controls for grid origin/dimensions, seed, temperature,
humidity, wind/gravity/storm/disaster/force opt-in and NEREID external-water
authority, plus an extended JSON tab. **Применить** validates the complete
draft and calls `MapDoc::beginEdit` only once; existing Ctrl+Z/Ctrl+Shift+Z
Undo/Redo is shared with other map edits. Invalid JSON/settings are rejected
without changing the map. Both regular and FPS map formats are supported via
`apps/editor/PhysisAuthoring.h`; no genre-specific ECS logic is added.

Authored data is stored under the map-level `"physis"` JSON key.
It is a **declaration, not automatic live ECS writeback**. Any native project
can opt in from its generic `GameModule::setup`:

```cpp
#include "engine/physis/PhysisProjectConfig.h"

void MyGame::setup(aiverse::Simulation& sim,
                   const aiverse::Json& rules,
                   const aiverse::Json& map) {
    aiverse::physis::installFromMap(sim, map);
}
```

`installFromMap` returns false for absent/disabled configuration; otherwise
it strictly reads grid/climate parameters and flags, registers persistence and
one fixed-tick PHYSIS system, and returns true. It does not install itself
into RTS, FPS or other genre modules automatically. As of **Stage 19**, validated individual `windZones`, `storms` and
`gravityFields` arrays **are** spawned deterministically when explicitly
installed via `installFromMap`. Each asset requires a stable unique `id`;
the native importer sorts IDs before creating ECS entities, including across
field types. Unknown extra keys are preserved by the editor but are not
interpreted by the importer. Runtime IDs are authoritative ECS entities;
the authored JSON ID is not itself a persisted component.

**Validation:** stage 18 source tests `physis_scene_geometry`,
`physis_project_config`, and `physis_authoring` were written and registered
in CTest. The last one needs `aiverse_editor_core` and runs in the editor /
unified build; the PHYSIS-focused headless CI compiles the other two.
Studio validation was updated to build/run all three and compile the editor.
As of this commit, **C++ build, CTest, CI PASS, editor interaction and
screenshot are NOT RUN / UNVERIFIED**. See the separate Stage 18 handoff.

## Stage 19 — individually authored WindZone, StormEvent and GravityField

Open **Мир → PHYSIS · Природа**. The section **PHYSIS — объекты и Gizmo**
can create, list, select and remove wind zones, tornadoes/hurricanes and
local gravitational fields even while the game is not running. Existing
MapProps Inspector shows the selected PHYSIS field's position, radius and
type-dependent properties. The advanced field JSON editor preserves unknown
extra properties but rejects invalid typed values.

Turn on **Редактировать маркеры мышью** and click a W/S/G marker in Scene:
drag in the X/Z plane; hold **Shift** to change Y. The first effective
movement calls `MapDoc::beginEdit` and subsequent movements call
`MapDoc::touched`; **one whole drag equals one Undo/Redo operation**.
When the marker editor is off, normal genre-independent Scene mouse selection
works as before. A PHYSIS marker is a map document asset, **not** a fake live
ECS selection or a Box3D body. An active game `World` is not mutated.

Map example:

```json
{
  "physis": {
    "enabled": true,
    "grid": {"originX": -4, "originZ": -4, "width": 8,
             "height": 8, "cellSizeMeters": 100, "seed": 8},
    "climate": {"baseTemperature": 15, "baseHumidity": 0.5},
    "enableStorms": true,
    "enableWindZones": true,
    "enableGravityFields": true,
    "floodMode": "ExternalAuthority",
    "windZones": [
      {"id": "wind-1", "center": [0,0,0],
       "radiusMeters": 120, "velocity": [15,0,0], "gustAmplitude": 3}
    ],
    "storms": [
      {"id": "storm-1", "kind": "Tornado", "center": [50,0,25],
       "eyeRadiusMeters": 20, "outerRadiusMeters": 150,
       "heightMeters": 300, "tangentialWind": 45, "updraft": 25}
    ],
    "gravityFields": [
      {"id": "gravity-1", "mode": "Uniform", "center": [0,0,0],
       "radiusMeters": 200, "acceleration": [0,-2,0],
       "overrideBaseline": true}
    ]
  }
}
```

The generic native `installFromMap(sim, map)` validates all assets **before**
changing PHYSIS World state and spawns components in lexicographic stable-ID
order; corresponding `enableWindZones`, `enableStorms`,
`enableGravityFields` flags are required for nonempty arrays so persistence
schemas match. Stable IDs are authoring keys and **are not added to the
persistent ECS component schema**; deterministic spawn order and unchanged
map/save manifest are required for consistent replay. Edits to authored
map JSON do not hot-reload live PHYSIS or rebind existing ECS entities.

The core library contains no editor/genre/Box3D/NEREID/TerraWeave dependency.
Water remains NEREID-owned; setting `ExternalAuthority` disables PHYSIS's
separate shallow flood. Box3D dynamics are still an optional independent
consumer of PHYSIS forces.

**Source tests added:** `physis_field_assets` and (editor_core only)
`physis_field_authoring`, plus expanded `physis_project_config` persistence
fixture. Registered in focused Linux/macOS PHYSIS and Studio CI.
**Native compilation, CTest and graphical drag QA NOT RUN / NOT VERIFIED.**

## Stage 20 — Scene field radius gizmos and metre-grid snapping

The shared PHYSIS field panel now offers **Move** and **Radius** tools. When the
radius tool is active, select a W/S/G map field and drag its world-projected
radius handle (on the +X perimeter). The Scene draws the selected field's
outer X/Z perimeter. Storm radius editing changes `outerRadiusMeters`, never
the eye radius; the canonical native validator rejects invalid eye/outer pairs.
The gizmo uses the editor camera's ground-plane ray at the field's current Y;
it does not modify 3D height or perform full 3D volumetric rotation.

**Snap to grid** is opt-in and has a configurable step of 0.25..1000 metres.
Move snaps X/Y/Z to the world origin grid; radius snapping is applied before
validated bounds. Negative coordinates use symmetric rounding. Height dragging
keeps an unsnapped accumulator to avoid losing sub-grid mouse motion. The snap
settings are editor-session preferences, not additional map/ECS schema fields.
Each successful move/resize drag remains **one MapDoc Undo/Redo transaction**.
No live PHYSIS World, Box3D body, NEREID water, or game genre state is edited.

The existing `tests/physis_field_authoring_test.cpp` has new source fixtures
for radius Undo/Redo, invalid radii, no-op edits and negative-coordinate snap.
**Native build, CTest and graphical editor validation remain NOT RUN.** See
`docs/agent-handoffs/physis-editor-gizmo-snapping-resize-2026-11-18.md`.


## Stage 21 — multi-select, group transforms, duplication and deletion

The shared PHYSIS field panel and Scene marker overlay support multiple
selected authored fields at once:
- Ctrl/Shift-click W/S/G markers or rows toggles selection membership.
  A plain row click switches to one field. Select all / Clear selection are
  also available.
- In Move mode, drag an already selected marker to translate the entire set
  by the SAME X/Z or Shift+Y delta. A clicked member becomes the active pivot
  without dropping other members. Relative offsets are preserved across
  WindZone, StormEvent and GravityField. Optional snap affects the pivot only,
  not every field independently. Completed axes remain when Shift mode changes.
- Duplicate selected copies complete JSON field objects, including custom
  properties, and allocates deterministic collision-free ASCII IDs
  (for example wind-1-copy, wind-1-copy-2). Default copy offset is +5 m
  in both X and Z, or the selected snap step when enabled. The new copies
  become selected. Delete selected is atomic across all field types.
- Group duplicate, group deletion and entire group drag each contribute ONE
  MapDoc Undo action. Invalid group selections or out-of-range positions are
  rejected without partial updates. The Radius gizmo edits only the currently
  active field; individual property editing needs a one-field selection.

Authoring-only C++ functions in apps/editor/PhysisAuthoring.h:
  moveFields(MapDoc&, vector<FieldPosition>, bool firstEdit, string* error)
  duplicateFields(MapDoc&, vector<string> ids, dx, dy, dz, vector<string>& newIds, string* error)
  removeFields(MapDoc&, vector<string> ids, string* error)

Drag updates derive absolute targets from initial positions, and the entire
authoring config is validated before committing through the same native
parseFieldAssets parser used by installFromMap. There is no gameplay World,
Box3D, NEREID or TerraWeave mutation and no ECS save-schema change.

Source fixtures for group Undo/Redo, atomic rejection, new ID generation,
custom-key preservation and regular/FPS map roundtrips are in
tests/physis_field_authoring_test.cpp. Native build, CTest and GUI QA
NOT RUN. See docs/agent-handoffs/physis-editor-group-authoring-2026-11-18.md.


### Group rotation and uniform geometric scale (Stage 21 extension)

With any PHYSIS fields selected, choose **Поворот вокруг Y, град** and
**Масштаб геометрии**, then **Применить к выбранным**. The active member's
center is the pivot. Both operations are performed in one atomic MapDoc
transaction by physis_authoring::transformFields. Rotation changes the
world-space X/Z arrangement around the common Y axis; uniform scale
changes the offsets in XYZ and the sources' spatial extents (wind/gravity
radius; storm eye radius, outer radius and height). Wind velocity/direction,
storm forces, gravity acceleration, enabled flags and IDs are NOT changed.
This is intentional: field values are independent from geometry.

Whole-config native validation rejects an invalid transformed position,
radius, height or source combination, leaving the entire original
selection unchanged. Orientation/heading rotation, per-axis nonuniform
scaling, volumetric 3D rotation gizmos and live ECS writeback are not
implemented. Rotation uses editor double math and writes 3-decimal metre
coordinates; authoritative runtime still reads quantized Fixed map inputs.
New rotation/scale/Undo fixtures are authored but native CTest is NOT RUN.
