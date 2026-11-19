---
name: aiverse-physis
description: Reuse AIVERSE PHYSIS for deterministic weather, wind, gusts, storms, gravity and environmental forces. Shows genre-neutral ECS integration, optional Box3D Scene forces, ownership split with Water/NEREID and TerraWeave, tests and mandatory handoffs.
---

# AIVERSE PHYSIS — shared nature and force systems

**Never reimplement PHYSIS inside an FPS/RTS/RPG module.** PHYSIS core must remain genre-independent, renderer-independent and headless. Read `docs/physis.md`, `engine/physis/AGENTS.md`, and `docs/physis-nereid-terraweave-integration.md` before editing.

## Ownership

- **PHYSIS:** atmospheric/weather climate, wind/gust/shelter, storms (hurricanes/tornadoes), snow/wetness/ice, fire/quakes, configurable gravity and environmental force **policy**.
- **NEREID water agent (PR #23):** canonical lakes, rivers, oceans, flow/current, water surfaces and buoyancy. Set `InstallOptions::floodMode = FloodMode::ExternalAuthority` when NEREID owns water. PHYSIS rain is normalized intensity, **not** metres of water.
- **TerraWeave agent (PR #24):** land geometry, heightfield, biome, foliage, vegetation and ground presentation. Terra consumes immutable PHYSIS/NEREID snapshots; PHYSIS must not generate terrain or run a second water solver.
- **Box3D Physics:** movement/collision/forces and physics snapshots. Use the existing `AIVERSE::physics` target and `physics::Scene`; only the optional bridge under `engine/physics/PhysisBox3D.h` may include Box3D types.
- **Renderer/VFX:** consumes presentation snapshots only; never controls authoritative climate.

## C++ API map

| Feature | Header | API |
|---|---|---|
| Core grid and ticking | `engine/physis/Physis.h` | `initialize`, `advance`, `cellAtPosition`, `sample` |
| Climate and weather fronts | `PhysisClimate.h` | `ClimateZone`, `WeatherFront` |
| Surface wetness / snow | `PhysisEnvironment.h` | `environmentAt`, `EnvironmentalResponse`, `advanceEnvironment` |
| Fire/quake/shallow-flood prototype | `PhysisDisasters.h` | `DisasterConfig`, `FloodMode`, `ignite`, `hazardAt` |
| Spatial 3D wind and gusts | `PhysisWind.h` | `WindZone`, `WindShelter`, `windAt` |
| Uniform/point gravity fields | `PhysisGravity.h` | `GravitySettings`, `GravityField`, `gravityAt` |
| Hurricane/tornado vortex fields | `PhysisStorm.h` | `StormEvent`, `StormKind`, `windWithStormsAt`, `stormsAt` |
| Shared drag/gravity force policy | `PhysisForces.h` | `ForceReceiver`, `ForceOutput`, `evaluateForces`, `advanceForces` |
| Combined read-only 3D climate + hazard + force fields | `PhysisPresentation.h` | `NatureFieldSample`, `sampleNatureField` |
| Read-only raster for editor diagnostics | `PhysisDiagnostics.h` | `DiagnosticRequest`, `DiagnosticRaster`, `sampleDiagnosticRaster` |
| Snapshot spatial broadphase (optional) | `PhysisSpatialIndex.h` | `FieldSpatialIndex`, `sample`, `stats`, `candidateCount` |
| Indexed diagnostics (optional) | `PhysisDiagnostics.h` | `sampleDiagnosticRasterIndexed` |
| Debug palette (non-authoritative) | `PhysisDebugPalette.h` | `DebugColor`, `debugColor`, `diagnosticColor` |
| Dockable editor debug | `apps/editor/windows/PhysisWindow.cpp` | `AIVERSE_EDITOR_WINDOW(PhysisWindow)`, read-only live / private demo |
| Generic native host | `PhysisIntegration.h` | `InstallOptions`, `install`, `forEachPresentationCell` |
| Optional Box3D consumption | `engine/physics/PhysisBox3D.h` | `applyPhysisToBody`, `PhysisBodyMaterial` |

Install PHYSIS once from `GameModule::setup` before the persistence registry seals. `InstallOptions.enableStorms`, `enableForces`, `enableWindZones`, and `enableGravityFields` default false, so existing games retain their behaviour. PHYSIS updates precisely once per authoritative `Simulation::step` (never once per render frame); the optional force pass **only publishes** ECS outputs and does not move bodies.

## Persistence invariants

Existing Stage 1–5 native save schemas must not change by default. `enableWindZones` registers WindZone/WindShelter, `enableGravityFields` registers GravityField/GravitySettings, `enableStorms` registers StormEvent, and `enableForces` registers ForceReceiver/ForceOutput. Set the flags consistently before saving/loading/replaying; the registry rejects mismatched schema manifests. The new `sampleNatureField` is a read-only adapter used by Terra, VFX, editor and physics consumers, not a second world state.

## Box3D usage

```cpp
#include "engine/physis/PhysisIntegration.h"
#include "engine/physics/PhysisBox3D.h"

// In generic native GameModule::setup:
aiverse::physis::InstallOptions p;
p.enableStorms = true;
p.enableDisasters = true;
p.floodMode = aiverse::physis::FloodMode::ExternalAuthority; // when NEREID owns water
aiverse::physis::install(sim, p);

// Physics owner thread, after authoritative PHYSIS update:
for (auto body : optedInDynamicBodies)
    aiverse::physics::applyPhysisToBody(scene, sim.world(), body);
scene.step();
```

Link `AIVERSE::physics` and enable `AIVERSE_ENABLE_PHYSICS=ON` only in opt-in native hosts. The bridge samples the actual Box3D body mass/velocity/position and applies quadratic wind drag plus **mass × (PHYSIS gravity − Box3D built-in gravity) × gravityScale** with `Scene::applyForce` before `Scene::step`; otherwise gravity would be counted twice. Do not apply `ForceOutput::totalForce` to the *same* Box3D body. `physics::Character` retains its separate scalar-gravity controller and is **not** modified by this adapter. Box3D float solver does **not** become lockstep-deterministic because PHYSIS uses fixed point.

## Current status and tests

Implemented fields are simple authored wind/vortex/gravity policies; they are **not** CFD weather, N-body gravitational physics, full destruction, water storm surge, GPU storm effects or foliage wind deformation. No automatic Terra/NEREID coupling is claimed. Native compilation and tests are **NOT RUN** for connector-authored stages, so keep PR #25 draft until validated.

```sh
./run-tests.sh --filter '^physis_'
cmake -S . -B build/physis-physics -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_ENABLE_PHYSICS=ON
cmake --build build/physis-physics --target aiverse_physis_box3d_test
ctest --test-dir build/physis-physics -R '^physis_box3d$' --output-on-failure
```

For Box3D integration also run `physics_runtime`, `physics_scene`; for shared water/terrain run NEREID/Terra and legacy regressions. Do not claim PASS unless logs demonstrate it.

## Mandatory agent handoff

**Every implementation stage** must commit its code/tests, then a **separate documentation commit** to `docs/agent-handoffs/physis-<stage>-YYYY-MM-DD.md` with branch, exact implementation SHA, paths, public API, actual test PASS/FAIL/NOT RUN results, known limitations and next explicit task. Update root `AGENTS.md`, `engine/physis/AGENTS.md` and `docs/physis.md` when changing ownership/contract. Do not commit into another agent's active Water or Terra branch; no force pushes.

## Optional many-query acceleration (Stage 15–16)

For temperature/humidity/rain/snow/fire/quake diagnostic heatmaps, call
`sampleDiagnosticRaster`; scalar layers now read relevant ECS state directly
without traversing every wind/storm/gravity field.

For large numbers of **3D** wind/gravity/storm queries in one committed tick:
```cpp
#include "engine/physis/PhysisSpatialIndex.h"
#include "engine/physis/PhysisDiagnostics.h"
aiverse::physis::FieldSpatialIndex spatial(sim.world(),
    {.tileSizeMeters=256, .maxTilesPerVolume=1024});
aiverse::physis::NatureFieldSample data;
bool found = spatial.sample(queryPosition, data);
aiverse::physis::DiagnosticRaster raster;
aiverse::physis::DiagnosticRequest req;
req.layer = aiverse::physis::DiagnosticLayer::GravityMagnitude;
aiverse::physis::sampleDiagnosticRasterIndexed(spatial, req, raster);
```

`FieldSpatialIndex` copies the authoritative ECS World using the engine's
COW/snapshot contract and builds derived X/Z tile bins. Very large volumes and
global gravity use fallback lists; original fixed-point candidate order, storm
timing and gravity priority are retained. It is **not persisted** and must be
recreated after a PHYSIS tick, storm movement, authoritative mutation, rollback
or load if new state should be visible. Do not keep a separate mutable copy of
weather. No Box3D, NEREID or TerraWeave header is needed to use it.

This is a query-time broadphase, **not** a new streaming/grid simulation,
destruction engine or validated benchmark. Stage 16 test code checks reference
versus indexed Fixed values; actual CTest/CI PASS remains unverified until run.
See `docs/agent-handoffs/physis-spatial-index-2026-11-18.md`.

## Editor PHYSIS debug window (Stage 17)

The standard editor auto-discovers `apps/editor/windows/PhysisWindow.cpp`:
open **Мир → PHYSIS · Природа**. Hidden by default. It reads live
`Editor::sim()` only when `physis::State` exists. The window provides
a bounded 2D heatmap, 14 selectable layers, palettes, scalar values,
wind/gravity inspection, and outlines for WindZone/StormEvent/GravityField.
It **does not modify live authoritative ECS** or game project assets.

If PHYSIS is not installed in the current game, the editor offers
**Создать / сбросить демо**: a separate `aiverse::World` with a storm, wind,
gravity and fire. Only demo buttons advance this private World. Do not
mistake the sample for a deployed game's PHYSIS integration, and do not
reuse the demo world as an authoritative server simulation.

`PhysisDebugPalette.h` provides CPU colour mapping and invalid-cell masking
for other tools without adding render dependencies to PHYSIS. Reference CTest
`physis_editor_view` covers the palette and COW diagnostic data, but the
GUI must additionally be built and manually reviewed in the real editor.
**Editor build / CTest / screenshot / click-through NOT RUN yet.**
Handoff: `docs/agent-handoffs/physis-editor-debug-view-2026-11-18.md`.

## Stage 18 Scene viewport and authored map configuration

Before changing PHYSIS editor workflows read `docs/editor-unification-handoff.md`.
The standard Scene viewport now imports `apps/editor/PhysisSceneOverlay.h`
and draws opt-in projected PHYSIS heatmap tiles/rings, read-only against the
running generic Simulation. Open **Мир → PHYSIS · Природа**, enable the
3D Scene overlay (available only when the project installed PHYSIS).
The 2D demo World is independent, and must never be written to a game save.

The common Inspector's map properties page offers PHYSIS authoring
(`apps/editor/PhysisAuthoring.h`), with structured grid/climate/feature
settings plus advanced JSON. Apply validates first, snapshots MapDoc once,
and supports Undo/Redo for both regular and FPS map document formats.
It stores a map-level `"physis"` block, but does not mutate a live World.

Native projects opt in without genre dependency:

```cpp
#include "engine/physis/PhysisProjectConfig.h"
void MyGame::setup(aiverse::Simulation& sim, const Json& rules,
                   const Json& map) {
    aiverse::physis::installFromMap(sim, map);
}
```

The loader reads grid/climate/enable flags, rejects malformed input, respects
`FloodMode::ExternalAuthority` for NEREID-managed water and registers
optional component persistence. It does **not** instantiate authored field
arrays (future explicit entity spawn adapter), nor automatically add weather
to RTS/FPS modules.

Handoff: `docs/agent-handoffs/physis-editor-scene-inspector-2026-11-18.md`.
Actual `physis_authoring`, `physis_project_config`, `physis_scene_geometry`
CTest and interactive editor screenshot/launch: **NOT RUN**. Do not advertise
a verified UI or migrated runtime until CI/checkout evidence exists.

## Stage 19 — reusable authored wind/storm/gravity assets and Gizmos

Native project maps may author `physis.windZones`, `physis.storms`,
`physis.gravityFields`, each with a unique stable ASCII `id`, `center`
[X,Y,Z] in metres, appropriate radius and type-specific properties.
`engine/physis/PhysisFieldAssets.h` uses the SAME validation as
`apps/editor/PhysisAuthoring.h`, sorts all assets by ID and creates their
PHYSIS ECS components on explicit `installFromMap`. Nonempty collections
require `enableWindZones`, `enableStorms` or `enableGravityFields` true
to register the correct save schemas. The stable authored ID is NOT a new
persisted ECS identity and edits are not automatically hot-reloaded.

Editor: **Мир → PHYSIS · Природа** → **PHYSIS — объекты и Gizmo** can
create WindZone/Tornado/Hurricane/GravityField, select/delete/adjust typed
properties and advanced JSON. Enable `Редактировать маркеры мышью`, then
drag W/S/G markers in Scene, Shift-drag for height. The selected asset also
appears in the common MapProps Inspector. Every nontrivial drag gets one
MapDoc Undo snapshot, but never changes active game ECS directly.
See `docs/physis.md` and Stage 19
`docs/agent-handoffs/physis-field-assets-gizmos-2026-11-18.md`.

`physis_field_assets`, `physis_field_authoring` and the extended native
`physis_project_config` test are registered but have **NO actual CTest PASS**.
Do not state validated in-game rendering, GUI interaction or load/replay
until verified on an actual native checkout.


### Stage 21 — atomic group field authoring

Ctrl/Shift-click PHYSIS W/S/G map fields in Scene or the field list to
toggle multi-selection. Drag a group member in Move mode to translate the
whole group; Shift adjusts Y. Snap applies to the active pivot only.
Select all, clear selection, duplicate and delete work across field types.
Radius gizmo changes the active member alone.

Use physis_authoring::moveFields, duplicateFields and removeFields for
atomic map JSON batches. NEVER loop moveField per member; that would make
multiple Undo entries and allow partial invalid state. Renderer and ImGui
stay in apps/editor/PhysisFieldEditor.h, not engine/physis. No writes to
live Simulation ECS; the generic native installFromMap remains the only
explicit startup importer. Handoff:
docs/agent-handoffs/physis-editor-group-authoring-2026-11-18.md.
New test fixtures are NOT RUN in CTest.


Stage 21 also provides numeric group yaw and uniform scale controls in the
PHYSIS field panel. The active field's center is the pivot. Use
physis_authoring::transformFields for one atomic Undo transaction. It
rotates positions in X/Z and uniformly scales group spacing and spatial
radii/heights; never treat it as orientation of the vector wind/force
field itself. Native validation rejects the whole group if any radius,
height or location is invalid. C++ and Scene UI tests NOT RUN.


### Stage 22 — advanced PHYSIS Scene authoring

Enable mouse marker editing for W/S/G field assets and optionally enable
rectangle selection starting from empty Scene space. Ctrl/Shift adds
projected centre markers. Pivot modes: Active, Average, Bounds Centre,
World Origin or Custom XYZ. The world X/Y/Z handles are projected for
hit testing but position changes use RTSCamera::screenRay and exact
closestRayAxisParameter. Degenerate rays must not move fields.

Rotate Y and Uniform Scale each have a horizontal projected ring.
Use frozen JSON baseline and firstEdit/touched flags on transformFields
so one mouse drag is one MapDoc Undo and every frame is computed from the
same source, not accumulated. Yaw rotates positions only; scale changes
spatial volumes and positions, not the force/velocity vectors. The
authored FieldAssets ECS types have no quaternion orientation. No live ECS
writeback or water/terrain/Box3D authority change.

All C++ fixtures added are source-only, NOT RUN. Stage 22 handoff:
docs/agent-handoffs/physis-editor-marquee-3d-gizmos-2026-11-18.md.


### Stage 23 — stable-ID runtime reconciliation through Simulation commands

For projects that need live changes to authored WindZone, StormEvent
and GravityField assets, opt into installEditableFromMap in generic
GameModule::setup. Reserve a module command type, authorize system
commands on the server and dispatch them with applyFieldEditCommand
inside GameModule::applyCommand. A trusted host may call
submitEditableFieldsFromMap to parse/validate the full modified map
and queue its binary command through Simulation::submit/CommandSink.
Do NOT mutate the ECS World directly from an ImGui/Scene callback.

FieldRuntimeState is persisted and included in rollback snapshots.
Stable authored IDs map to Entity+kind+fingerprint; same-type updates
preserve Entity, type changes recreate, unchanged evolving storms are
not reset. Full desired field batch is sorted/validated and applied to
a staged World; wrong revision or malformed payload rejects atomically.
Existing static installFromMap remains default and save-compatible.
The opt-in adds physis.field_runtime v1 and all three component schemas
to the strict save manifest; no implicit migration.

Command wire caps: <=4096 int64 args including the length header, <=32760
payload bytes. Larger maps require an as-yet-unimplemented chunked atomic
protocol. The native host must stop client-side player=0 forgery;
there is no automatic editor registration, server ACL, GUI edit ACK or
live climate-grid/flood-policy migration in Stage 23.
See docs/agent-handoffs/physis-field-runtime-reconciliation-2026-11-18.md.
CTest added but NOT RUN.


### Stage 24 — PHYSIS editor runtime gateway

The trusted local project GameModule may implement
aiverse::editor::physis_live::FieldEditHost, return its own unique
fieldEditCommandType, explicitly allowLocalFieldEdits, install using
installEditableFromMap, then dispatch applyFieldEditCommand in
GameModule::applyCommand.

PHYSIS editor Live Editing is disabled when no opt-in host is available,
no FieldRuntimeState exists, or a network CommandSink is attached. The
editor queues validated MapDoc field updates, then observes the ECS
revision AND complete typed fingerprints for Applied. Treat Queued
as unresolved; never claim server ACK without a real remote receipt.

For authenticated server admission, use physis::admitAuthorizedFieldEdit
with a principal obtained from the authenticated CONNECTION plus the
server ACL. It rejects forged player=0 requests and constructs its own
trusted system command. No automatic remote editor transport has been
added. Do not write ECS directly from ImGui, duplicate NEREID or
TerraWeave, or bypass the genre-independent World/Command contract.
Tests authored but NOT RUN. Handoff:
docs/agent-handoffs/physis-editor-live-edit-2026-11-18.md.


### Stage 25 — reliable remote PHYSIS field editing

For an authenticated editor/admin connection use the reusable
engine/physis/PhysisRemoteProtocol.h, PhysisRemoteClient.h and
PhysisRemoteServer.h on net::Conn (UDP, WebSocket or loopback).
ServerGateway::poll requires a real host-authenticated connection
principal and server ACL; call observe after Simulation steps.
GameModule::applyCommand must dispatch the Stage 23 PHYSIS command.
Never treat client player=0 or client instance IDs as permissions.

RemoteClient requests the current revision, then uses a unique
request ID with an exact original payload. After disconnect it
queries the same ID before resending; server dedupe prevents double
submission. Only Applied after actual ECS revision and fingerprint
matches is success; stale/conflicting/dropped requests get NACK.
The editor bridge apps/editor/PhysisRemoteEdit.h must receive a
host-verified Conn; the PHYSIS window exposes remote status/ACK,
refresh and safe retry.

Server cache is bounded and in-memory only. A lost server journal
after restart can leave an uncertain old ACK; do not claim
crash-durable exactly-once. Large maps can exceed Stage 23
32760-byte Command.args and need a later atomic batch protocol.
Do not mix the other agent's TrackedFromMap/RuntimeFieldRegistry
with editable FieldRuntimeState until reconciled.

Tests physis_remote_edit authored and CI registered but NOT RUN.
Handoff docs/agent-handoffs/physis-remote-edit-reliable-2026-11-19.md.


### Stage 26 — PHYSIS durable admin sessions

Use the canonical FieldRuntimeState/EditableFromMap runtime path.
For a trusted local admin server, create
physis::remote::admin::LoopbackAdminEndpoint(privateJournalFile)
and call open(port, reservedPhysisCommandType, keyLookup, readAcl,
writeAcl, error), then poll(authoritativeSimulation, nowMs) and
afterTick(authoritativeSimulation) each committed tick.
Restore World/checkpoint BEFORE FileJournal attachment and open
the listener only when checksummed journal loading succeeds.
The operator must exclusively own the journal path.

For native editor, the trusted host may call
editor::physis_remote::link().connectLocalAdmin(
    "ws://127.0.0.1:port", principal, keyFromOSSecretStore,
    cryptographicallyRandomClientInstance, assignedPlayerSlot,
    reservedPhysisCommandType). The editor UI polls handshake
completion and existing RemoteClient receipt ACK/NACK. Secrets are
never sent; fresh nonce + keyed BLAKE2b validates the principal.
Native AIVERSE currently does not implement WSS, so this endpoint
is loopback only. Off-machine use needs encrypted transport/tunnel.

FileJournal preserves exact original request and authenticated
principal, expected revision, desired field fingerprints, scheduled
tick and status. It persists intent before enqueue, and fails
closed on write/corruption. After server restart recheck persisted
World; unknown outcomes must remain Unknown/Conflict rather than
being reported as definitely Applied. The separate World save +
journal is not a crash-atomic transaction. Do not claim exactly-once
receipt under power loss without a joint checkpoint.
Do not mix other agent's TrackedFromMap/FieldCommandHost with
canonical editable runtime. Keep NEREID, TerraWeave, Box3D separate.

Tests physis_admin_journal authored and CI registered but NOT RUN.
Handoff docs/agent-handoffs/physis-admin-durable-journal-2026-11-19.md.


### Stage 26 queued-receipt restart recovery

When restoring a FileJournal into ServerGateway, persisted Queued
requests are now treated as Unknown: the old process may have lost
its pending command despite its durable sidecar receipt. Restore
the matching authoritative World checkpoint FIRST. Calling observe
with current FieldRuntimeState identifies already-Applied commands
by expected revision plus exact authored fingerprints. Remaining
Unknown intents may be resubmitted ONLY using the same authenticated
principal, request ID and original payload with current ACL/expected
revision checks. Do not mark stale Queued as Rejected just because
its pre-crash scheduled tick passed. This is NOT cross-file
crash-atomicity.

C++ fixtures added for crash before command execution and crash
after ECS commit before the journal Applied flush, NOT RUN.
Stage handoff docs/agent-handoffs/physis-stage26-queued-recovery-2026-11-19.md.


### Stage 26 journal crash-recovery correction
A journal persisted as `Queued` may lose its actual in-memory command on restart. Restore `Queued` to `Unknown`, reconcile against restored `FieldRuntimeState`, and only certify `Applied` when revision and authored fingerprints match. If the original command was not applied, accept a retransmission of the SAME authenticated request and expected revision; never synthesize a successful receipt. The sidecar journal and World checkpoint are not one durable transaction. See `docs/agent-handoffs/physis-stage26-queued-recovery-2026-11-19.md`. Tests added, NOT RUN.
