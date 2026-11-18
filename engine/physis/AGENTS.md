# PHYSIS — required agent instructions

Scope: `engine/physis/`, `tests/physis_*.cpp`, `docs/agent-handoffs/physis-*.md`.

1. **Genre independence is non-negotiable.** PHYSIS engine files must not include anything from `modules/fps`, `modules/rts`, `libraries/include/aiverselib/genre`, or any game-specific types. Native projects use the common C++ API and ECS components, not forks per genre.
2. Long-lived state lives in `aiverse::World` components/resources, authoritative math uses `Fixed`. Renderer/FX/audio may consume snapshots and emit presentation events but must not own climate state.
3. Preserve deterministic ordering, fixed step scheduling, seed/replay, rollback/COW. No wall clock, platform distributions or float/double in simulation.
4. Integrate Terra, Water, Foliage, Physics, VFX via clean interfaces and opt-in adapters. No hard link to a rendering backend or platform.
5. After **EVERY** implementation stage write an explicit handoff in `docs/agent-handoffs/` with branch, implementation SHA, changed paths, architecture/API, tests ACTUALLY RUN and their results, known gaps, and next concrete task; commit it separately from implementation. Never mark unexecuted tests as passed.
6. Existing gameplay/genre modules must remain unaffected unless their integration is explicitly tested and reviewed. Respect concurrent branches; never force push.

Latest Stage 13: `docs/agent-handoffs/physis-sampling-persistence-2026-11-18.md`; Stage 12: `docs/agent-handoffs/physis-numerical-ci-2026-11-18.md`. Stage 11: `docs/agent-handoffs/physis-forces-box3d-2026-11-18.md`. Stages 8–10: `docs/agent-handoffs/physis-{wind,gravity,storm}-2026-11-18.md`; Stage 7: `docs/agent-handoffs/physis-flood-authority-2026-11-18.md`. Stage 6 role/ownership contract: `docs/agent-handoffs/physis-ownership-terra-water-2026-11-18.md`. Stages 1–5 remain preserved; no stage claims passed native tests.

## PHYSIS / NEREID / TerraWeave coordination (2026-11-18)

Read `docs/physis-nereid-terraweave-integration.md` (root `AGENTS.md` ownership matrix) before editing flood/water/terrain integrations.
- PHYSIS agent owns weather, dynamic precipitation, surface wetness/snow/ice, fire/quake and independent nature ECS. NEREID Water agent (PR #23, `feature/nereid-water-core`) exclusively owns canonical lake/river/ocean ECS water, water-body geometry, flow, water sampling, buoyancy and water surface pipeline. TerraWeave agent (`feature/terraweave`) owns terrain heights/biomes/foliage and later consumes both through **optional** adapters; PHYSIS PR #25 is `feature/physis-nature-simulation`.
- The local `HazardCell::waterDepth` shallow flood is **experimental standalone gameplay simulation**. For NEREID-managed water bodies, opt in to `FloodMode::ExternalAuthority` via `InstallOptions::floodMode`: local depth is cleared and flood calculations bypassed; PHYSIS fire/quake still run. This switch and tests are authored but native verification is NOT RUN. Never invent competing water sources or integrate normalized PHYSIS rain intensity directly into NEREID metres.
- PHYSIS core must NOT include NEREID or TerraWeave headers; TerraWeave core must not depend on PHYSIS/NEREID. Host-level integration owns conversions: Terra signed centimetres -> common signed Fixed metres, X/Z horizontal and Y vertical; floor division for negative world positions, elevation and shoreline coherence, explicit scheduler and persistence ownership.
- PHYSIS read-only `EnvironmentalResponse` is the input for Terra terrain material, wetness/snow masks and foliage wind; NEREID `WaterSample`/water bodies are the source for Terra waterline, exclusion/shoreline and buoyancy. No presentation feedback into authoritative state.
- On each subsequent implementation stage: code + unit/cross-system tests in a code commit, **separate** handoff documentation commit, scope/owner coordination and verified native build status. Do not edit the Water agent's or Terra agent's active branches and do not merge their PRs automatically.

## Stage 14 — diagnostics ownership (2026-11-18)

- `PhysisPresentation.h` and `PhysisDiagnostics.h` are **read-only**, renderer-independent consumer APIs. They return copies/arrays for external editor/VFX/physics/Terra host code. Do not add ImGui/RHI/Box3D/genre includes here, or treat a PHYSIS heatmap as a visual renderer integration.
- `DiagnosticRaster::rawValues` are `Fixed::raw` and `valid[i]` marks active sampled cells. Do not infer real NEREID water levels from `LocalFloodMeters`. `diagnosticNormalized` is presentation-only; it must not influence authoritative simulation.
- Add post-stage handoffs; Stage 14 `docs/agent-handoffs/physis-diagnostics-2026-11-18.md`. The tests and CI are authored but native status is **NOT RUN**.

## Stage 15–16 spatial field reuse (2026-11-18)

- `PhysisDiagnostics.h` now samples basic scalar environment/hazard layers without full per-cell 3D field evaluation. New `PhysisSpatialIndex.h` builds a transient `World` COW snapshot, 2D candidate bins, and global fallback for huge volumes/uniform gravity; `sampleDiagnosticRasterIndexed` uses the same source snapshot.
- Do not change fixed-point math, Earth/Box3D gravity authority, and wind/storm candidate order in the unindexed reference. Existing `windAt`, `gravityAt`, `stormsAt`, and `sampleNatureField` remain supported. The indexed API invokes their **shared policy** through ordered visitors to make parity testable.
- The index is a derived read-only **snapshot**, not a mutable ECS mirror; rebuild for a new authoritative tick or after edits/rollback/load. It does not accelerate WeatherFront/Quake simulation, terrain/water, or external physics. Avoid retained historical snapshots.
- Handoff Stage 15: `docs/agent-handoffs/physis-diagnostic-fastpath-2026-11-18.md`; Stage 16: `docs/agent-handoffs/physis-spatial-index-2026-11-18.md`. All tests added, native compile/CTest/CI **NOT RUN** in connector environment.

## Stage 17 — editor debug view integration

- `engine/physis/PhysisDebugPalette.h` supplies only read-only display colour helpers. The editor PHYSIS window is `apps/editor/windows/PhysisWindow.cpp`, using the existing window plugin registrar; **do not** put ImGui or editor includes into `engine/physis`.
- Editor live PHYSIS is strictly read-only. Demo simulation is a fresh private `aiverse::World`, stepped only with explicit controls; never use it as a surrogate for actual game integration. The 2D heatmap outlines storm/wind/gravity volumes but is not a 3D Scene overlay or editable Inspector.
- Stage 17 test `physis_editor_view` / CMake / CI: authored, **NOT RUN**. Full editor build and macOS visual QA still mandatory. Handoff: `docs/agent-handoffs/physis-editor-debug-view-2026-11-18.md`.

## Stage 18 — Scene/Inspector and generic map config handoff

- `PhysisSceneGeometry.h` outputs reusable Fixed-position diagnostics only. The graphical projection and controls live in `apps/editor/PhysisSceneOverlay.h`, `apps/editor/windows/PhysisWindow.cpp`, and the shared `InspectorWindow.cpp`. Keep all ImGui/RHI/Box3D/editor includes OUT of PHYSIS core.
- `PhysisProjectConfig.h` may parse a map-level `physis` JSON block and call `installFromMap` explicitly from any native `GameModule::setup`. No per-genre automatic registration or direct renderer state. Inspector authoring uses `MapDoc` snapshots and validations and must not mutate live `Simulation::world()`.
- `physis_scene_geometry`, `physis_project_config`, and (when `aiverse_editor_core` exists) `physis_authoring` CTests are authored and registered, **NOT RUN**. The Scene GUI still requires native compilation and macOS visual/click tests before calling it functional.
- Merge prerequisite from main: `810feba70f01cdc35003ac80ea8486f39dc16049`, handoff `docs/agent-handoffs/physis-main-merge-2026-11-18.md`. Stage 18 handoff: `docs/agent-handoffs/physis-editor-scene-inspector-2026-11-18.md`. Continue to document every implementation stage in a separate commit with real test results.

## Stage 19 — field asset importer and editor owned gizmos

`PhysisFieldAssets.h` is the canonical typed parser of authored `windZones`, `storms`, `gravityFields`. Validate stable IDs and fixed-point bounds before `installFromMap` modifies World; sort IDs across types before allocating entities. Keep it independent from all genres, editors, renderer, NEREID, TerraWeave, Box3D. Opt-in `enableWindZones`, `enableStorms`, `enableGravityFields` must match nonempty collections to preserve persistent component schemas.

The editor `apps/editor/PhysisFieldEditor.h` owns map-authored markers, click/drag and selected properties in shared MapProps Inspector, using `PhysisAuthoring.h` (same native field parser for validation). One drag opens exactly one MapDoc Undo snapshot; never write directly into running `Simulation::world()` from Inspector/Gizmo. Authored IDs are document keys, not replicated/persisted ECS IDs.

Tests `physis_field_assets`, `physis_field_authoring` and extended `physis_project_config` have source + CTest + CI registration, **NOT RUN**. Stage handoff: `docs/agent-handoffs/physis-field-assets-gizmos-2026-11-18.md`. Run real editor/Box3D/PHYSIS tests before PR #25 is ready.

## Stage 20 — resize and snapping authoring invariants
- The editor offers opt-in Move/Radius gizmos and metre-grid snap; only `apps/editor/PhysisFieldEditor.h` uses ImGui, camera projection and mouse input. Keep `engine/physis` renderer/editor independent.
- `PhysisAuthoring.h::resizeField` edits outer storm radius or wind/gravity radius after the shared native parser validates the complete map. Move/resize drags call `MapDoc::beginEdit` only for the first effective edit, `MapDoc::touched` thereafter; preserve one Undo per drag. Never mutate the live World.
- Snap is a transient editor-only preference. Do not serialize it into PHYSIS ECS components or map assets. Existing map/save schemas and authored IDs remain unchanged.
- Handoff: `docs/agent-handoffs/physis-editor-gizmo-snapping-resize-2026-11-18.md`. New test fixtures were authored, **NOT RUN**; build/CTest/macOS Scene QA still required.


## Stage 21 — multi-field authoring guardrails
- apps/editor/PhysisAuthoring.h moveFields, duplicateFields and removeFields
  are editor MapDoc operations, never a live ECS write. Batch validate all
  field types before ONE Undo transaction. IDs belong to the authored map;
  they are not persisted ECS entity IDs.
- apps/editor/PhysisFieldEditor.h uses transient sorted multi-selection and
  a frozen drag-origin snapshot; one delta is applied to all fields and
  an anchor-grid snap must preserve relative offsets. Radius changes the
  active field alone. Switching Shift during group drag must not reset
  already moved axes.
- Duplication preserves unknown JSON properties while generating unique
  ASCII IDs under the 64-character limit. Enforce native parser shape,
  coordinates, 2048-field cap and per-category persistence enable flags.
- Preserve the genre-independent PHYSIS/NEREID/TerraWeave/Box3D boundaries.
  Source tests are authored but NOT RUN. Stage handoff:
  docs/agent-handoffs/physis-editor-group-authoring-2026-11-18.md.
