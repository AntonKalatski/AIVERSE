# AIVERSE Editor unification — agent handoff and living progress log

**Status:** IN PROGRESS — not verified, not ready to merge.
**PR:** https://github.com/chromealex/AIVERSE/pull/7
**Working branch:** `feature/universal-entity-inspector-fps` (base `main`)
**Owner goal:** The editor must treat RTS, FPS and future genres as ordinary entities with the same Scene, Hierarchy, Inspector, picking, Transform gizmo and outline. Genre-specific gameplay components are allowed; genre-specific editor selection/render/inspector stacks are not the target architecture.

> This file is the source of truth for the next coding agent. Read it with `AGENTS.md` before changes. **Update this file in the same branch after EVERY implementation/verification stage**, including unsuccessful stages. Never claim a feature complete based only on textual code inspection.

## Definition of done

- [ ] Shared entity identity and component/transform contract for both authored document entities and live ECS entities; no FPS-only editor selection state.
- [ ] Both RTS and FPS feed the same editor scene submission path without losing model/animation/material/skin support.
- [ ] Scene picking, hover, selection, Hierarchy, Inspector and undo/redo share identity and work for both genres.
- [ ] Editable native ECS component inspection, add/remove component support where safe, type-aware serialization/writeback. Document-only typed read inventory introduced; live ECS writeback still absent.
- [ ] Interactive Move/Rotate/Scale gizmo (XYZ, Local/World, handles, snapping, multi-selection, undo/redo). Where a legacy map schema cannot express rotation/scale, expose the limitation instead of silently dropping edits.
- [ ] Silhouette outline for selected/hovered authored models and primitive meshes, not just AABB wireframes.
- [ ] CI builds/tests green; macOS editor starts without crash; manual visual and interaction checks completed.
- [ ] PR reviewed, merged only after all gates pass.

## Current architecture and concrete files

| Responsibility | Current location | Notes |
| --- | --- | --- |
| Shared selection | `apps/editor/Editor.h` (`Selection`) | Added `Selection::FpsObject` with `ref`; `fpsSelectedSceneObject_` was removed. |
| Shared source adapter | `apps/editor/EditorEntityAdapter.h` | `DocumentEntityRef` for RTS map entities, FPS colliders/spawns; `readDocumentTransform`, `translateDocumentEntity(ies)`, `setDocumentPosition`, `setDocumentColliderBounds`. **Document adapter, not native ECS reflection.** |
| Common renderable | `apps/editor/SceneRenderable.h` | `SceneRenderable` can reference `Mesh` or `Model`, bounds, selected/hovered, ray/AABB picking. FPS currently uses primitive mesh. |
| FPS scene extraction | `apps/editor/EditorFps.cpp` | Reads `doc().data.fps["colliders"/"spawns"]`; creates box previews, handles FPS Inspector and adapter calls. |
| RTS scene extraction | `apps/editor/Editor.cpp`, `modules/rts/view/RtsViewDraw.cpp` | Still uses `RtsView` and its own model extraction. Must preserve existing RTS materials/animation. |
| Scene interaction | `apps/editor/windows/SceneWindow.cpp` | FPS dragging XZ / Shift-Y, X/Z axis constraints; RTS move calls common document translation. Colored axes are drawn for FPS but **not interactive handles**. |
| Scene picking | `apps/editor/EditorWorld.cpp` | `Editor::pickObject` dispatches FPS ray/AABB or RTS picker, returns shared `Selection`. |
| Hierarchy | `apps/editor/windows/HierarchyWindow.cpp` | FPS collider/spawn groups with shared selection and adapter-based camera focus; RTS still has distinct hierarchy path. |
| Inspector | `apps/editor/windows/InspectorWindow.cpp`, `apps/editor/DetailsPanel.cpp`, `apps/editor/EditorFps.cpp` | FPS currently has a dedicated `drawFpsSceneInspector` branch; RTS `DetailsPanel` has richer component presentation, much of it read-only. |
| Render infrastructure | `engine/render/Renderer3D.h`, `engine/render/Renderer3DModels.cpp` | `ModelDraw.outline` supports model inverted-hull outline; `drawMesh` primitive path has no equivalent silhouette outline. |
| RTS map data | `apps/editor/core/MapDoc.h` | `DocEntity` stores `x,z` (no generic Y/rotation/scale). FPS objects are JSON arrays/bounds. |

## Implemented, but not yet validated by a successful build

1. Shared `Editor::renderScene` renderer lifecycle; FPS preview submits through `SceneRenderable`.
2. Shared FPS picking and selection, hover, Hierarchy row highlighting and Inspector routing.
3. Common `DocumentEntityRef` and source adapters for RTS map entities / FPS collider / FPS spawn.
4. Read and write position; FPS collider bounds editing; RTS and FPS movement through common translation helpers.
5. Multi-selection prevalidation, deduplication, one document revision per batch; invalid collider extents rejected.
6. FPS scene XZ drag, Shift-Y drag, X/Z constrained movement; colored XYZ axis visualization (not a true gizmo).
7. RTS model outline; FPS selected/hovered primitive AABB edges (not a silhouette).
8. Latest known commits prior to this handoff include `169cf293` (FPS collider position Inspector) and `918db3cf` (collider Transform bounds validation).

**Validation evidence:** source-file presence checks only. GitHub Actions runs have repeatedly shown `queued`; there is NO confirmed passing editor build, no confirmed macOS launch and no verified visual test. Check latest CI rather than assuming it passed.

## Risks / likely defects to investigate immediately

- `EditorEntityAdapter.h` is a new header depending on `core/MapDoc.h`; verify all include paths and C++20 compilation. Check `MapDoc::data.fps` JSON methods against actual signatures.
- `SceneWindow.cpp` RTS group drag now uses `translateDocumentEntities`; compare with prior `worldedit::move` behavior, especially planet `at`, snap, locks and undo.
- FPS `Selection::FpsObject` is a document reference, not a runtime ECS entity. Do not label it native ECS support.
- `Editor::pickObject` FPS branch and mouse handling should be tested with scene viewport coordinates and selection modifiers.
- `drawFpsSceneInspector` edits JSON through adapter; ensure no invalid undo snapshots or pointer invalidation during ImGui drag.
- Primitive bounding box overlay must not be described as geometric silhouette outline.
- FPS source refs are based on array index; insertion/deletion/reorder can change identity. Plan stable GUIDs when schema allows.
- FPS camera zoom, off-screen culling, selection and drag should be tested at near-horizontal pitch.

## Next implementation stages (order matters)

**Stage A — Compile/test first.** Get GitHub CI outcome and/or build locally. Fix errors introduced by PR. Run targeted editor tests and launch on macOS if available. Record commands, exit codes and links to CI jobs here.

**Stage B — Common component adapter.** Define an editor-facing component schema/registry with type ID, read-only/editable capabilities, safe read/write, and serialization for native ECS and document entities. Do not fabricate generic reflection that cannot write back. Add tests for RTS/FPS data and selection.

**Stage C — Inspector consolidation.** Route FPS/RTS through a single Inspector panel backed by the common component adapter. Preserve specialized component drawers via registry; delete FPS-specific Inspector branch only after parity.

**Stage D — Scene extraction.** Extend the common renderable to preserve `ModelDraw` data (skin/pose/material overrides/geometry policy). Migrate RTS extraction carefully; add FPS authored models and stable identity.

**Stage E — Gizmo and outline.** Interactive handle picking, Move/Rotate/Scale, Local/World, snap, multi-edit, undo; true silhouette outline for `Mesh` and `Model`.

**Stage F — Release gates.** Full CI, macOS smoke test, Scene/Hierarchy/Inspector consistency, selection and undo tests, screenshot-based UI review, review and merge.

## Mandatory stage-update protocol

After EVERY stage or substantial commit, update the sections above **and append** an entry below. Include:
- Date/time (UTC if known), branch, commit SHA(s), exact files changed.
- What changed, what remains, and any schema/behavior limitations.
- Verification: actual build/test commands and results; link to GitHub workflow/job when available. If not run, say **NOT RUN**.
- New failures, follow-up owner, and next stage.
- Update checkbox statuses ONLY when verified; avoid vague “done” claims.
- Commit the handoff update to the same PR. If a stage is blocked, record the blocker instead of silently moving on.

## Progress log

### 2026-11-17 — Initial handoff document
- Branch: `feature/universal-entity-inspector-fps`; PR #7.
- Added this living agent handoff and the explicit acceptance criteria, architecture inventory, risks and execution plan.
- Code build/tests: **NOT RUN in this handoff stage**. Earlier GitHub workflow checks were observed queued.
- Next: obtain CI results and compile/fix, then implement component adapter/Inspector consolidation.

### 2026-11-17 — Transform validation hardening
- Branch: `feature/universal-entity-inspector-fps`.
- Commits: `ccc5c829` (`apps/editor/EditorEntityAdapter.h`), `27cf8262` (same file).
- Single-object FPS collider translation now rejects malformed/non-finite/inverted min/max extents **before** mutation. The shared bounds setter rejects arrays shorter than three coordinates.
- Validation: source edits committed; targeted code review by inspecting exact replacement blocks. **Build/tests NOT RUN**. GitHub Actions for prior handoff commit `47aa2192`: CI and Studio validation were **queued** at last check, not green.
- Limitations: No generic native ECS reflection, no common Inspector drawer registry, no full Move/Rotate/Scale gizmo or mesh silhouette outline.
- Next: obtain successful build/test results; implement common component adapter and migrate Inspector; verify interactions on macOS.

### 2026-11-17 — Document component inventory and FPS Inspector hookup
- Branch: `feature/universal-entity-inspector-fps`.
- Commits: `231f9890` (`apps/editor/EditorEntityAdapter.h`), `17cc48bb` (`apps/editor/EditorFps.cpp`).
- Added `DocumentComponentKind`, `DocumentComponentDescriptor`, `documentComponents()` and `readDocumentComponent()`. Supported document-backed views: RTS Transform/Renderable/Gameplay; FPS Transform/BoxCollider/SpawnPoint.
- FPS Inspector now renders a shared component inventory and read-only status while retaining existing specialized editable fields.
- Important: this is **not native ECS reflection**. No live-world component add/remove, no generalized writeback, and no RTS Inspector migration yet.
- Verification: source changes committed; **build/tests NOT RUN**. CI and Studio validation for previous `f0bb6d6a` were observed **queued**, not passed.
- Next: compile first; then introduce safe typed component writeback and migrate both genre inspectors to common component drawers; add targeted tests.

### 2026-11-17 — Typed document component writeback
- Branch: `feature/universal-entity-inspector-fps`.
- Commits: `b55c77a1` (`apps/editor/EditorEntityAdapter.h`), `7d3d3d4f` (`apps/editor/EditorFps.cpp`).
- Added `writeDocumentComponent()` with strict numeric vec3 validation and allowed kinds: document Transform, FPS Collider, FPS Spawn. Unsupported kinds (RTS gameplay/renderable, live ECS) return false instead of pretending to be writable.
- FPS Inspector Position XYZ and collider Min/Max fields now write through the typed component API. Undo snapshot creation remains in the existing ImGui activation path.
- Validation: source code inspected; **compilation and interactive tests NOT RUN**. Prior CI `649bb50e` was pending/queued.
- Remaining: native ECS component registry/writeback, common Inspector drawers for RTS and FPS, full gizmo and outline, passing CI.
- Next: obtain CI/build diagnostics; test typed writeback and undo on both FPS object types; then migrate RTS Inspector using typed component registry.

### 2026-11-17 — Shared component inventory UI for RTS and FPS
- Branch: `feature/universal-entity-inspector-fps`.
- Commits: `70e6e6af` (`apps/editor/EditorComponentPanel.h`), `dd8ff1ad` (`apps/editor/EditorFps.cpp`), `77d6c89f` (`apps/editor/DetailsPanel.cpp`).
- Introduced `drawDocumentComponentInventory()` as a common document-backed Inspector UI; FPS Inspector and RTS authored-entity DetailsPanel both call it.
- Live RTS ECS rows retain existing DetailsPanel; component inventory does not yet support native ECS reflection/writeback or shared editable field drawers. No claim of complete Inspector consolidation.
- Verification: changes committed and inspected via GitHub; **build/tests NOT RUN**. Last observed CI for `649bb50e` was pending/queued.
- Next: compile and fix errors; create common typed component field drawers, integrate runtime ECS component adapters and verify undo/UI behavior.

### 2026-11-17 — Shared editable Position XYZ drawer
- Branch: `feature/universal-entity-inspector-fps`.
- Commits: `ea38ef4e` (`apps/editor/EditorComponentPanel.h`), `80154699` (`apps/editor/EditorFps.cpp`), `68af0f2b` (`apps/editor/DetailsPanel.cpp`).
- Introduced `drawDocumentPositionField()` using `readDocumentTransform` and `writeDocumentComponent(Transform)`; connected to RTS authored DocEntity and FPS collider/spawn Inspector.
- Existing RTS DetailsPanel property fields remain; potential duplicated position presentation should be reviewed visually. RTS serialized Y is unsupported and rejected by the writer.
- Validation: source changes committed; **build/tests NOT RUN**. Latest prior CI `ac61f4fb` observed queued.
- Next: build/CI verification, consolidate duplicate RTS Transform drawers, add typed component-specific fields, then native ECS writeback.

### 2026-11-17 — RTS planar Transform UI correctness
- Branch: `feature/universal-entity-inspector-fps`.
- Commit: `f1385f13` (`apps/editor/EditorComponentPanel.h`).
- Shared Transform drawer now shows **Position XZ** with `DragFloat2` for RTS map entities, while FPS collider/spawn retain 3D `Position XYZ`. This avoids exposing an RTS Y edit that the serialized map schema cannot represent.
- Validation: source edit committed; inspected `engine/data/Json.h` to confirm `Json::operator==` exists. **Build/tests NOT RUN**. Prior `acdb57cd` CI and Studio validation observed queued.
- Next: verify build/CI, address duplicate RTS mapFields Transform display, then implement shared typed component drawers and native ECS adapter.

### 2026-11-17 — Remove duplicate RTS position editing and unify centroid translation
- Branch: `feature/universal-entity-inspector-fps`.
- Commit: `05ff3aff` (`apps/editor/DetailsPanel.cpp`).
- RTS single-selection Position XZ now appears only in shared `drawDocumentPositionField`, rather than also in legacy `mapFields`.
- Multi-selection retains the legacy centroid `DragFloat2` UI, but writes via `translateDocumentEntities` with source refs, replacing direct `worldedit::move` for this path.
- Validation: source change committed; **build/tests NOT RUN**. Need verify undo and multi-selection drag after switching between single and multi.
- Next: obtain actual CI/build results, inspect existing component drawer APIs, migrate more fields without breaking RTS runtime ECS DetailsPanel.

### 2026-11-17 — Extract collider bounds editor to shared typed component panel
- Branch: `feature/universal-entity-inspector-fps`.
- Commits: `dea12d99` (`apps/editor/EditorComponentPanel.h`), `456bfd7b` (`apps/editor/EditorFps.cpp`).
- Added `drawDocumentColliderBoundsField()`, which reads/writes the typed Collider component via `readDocumentComponent` / `writeDocumentComponent` and maintains ImGui activation-based undo snapshotting.
- FPS Inspector now calls this common drawer instead of keeping a local 1.5 KB bounds editor. Shared component drawers now include Transform position and Collider bounds.
- Validation: source changes committed; **build/tests NOT RUN**. CI for previous `69e99425` was observed queued.
- Next: CI/build verification; consolidate Inspector component card presentation; implement native ECS component read/write adapters without breaking RTS DetailsPanel.

### 2026-11-17 — Schema-driven editable Inspector cards for FPS
- Branch: `feature/universal-entity-inspector-fps`.
- Commits: `faf8396d` (`apps/editor/EditorComponentPanel.h`), `6d4d40ae` (`apps/editor/EditorFps.cpp`).
- Added `drawDocumentEditableComponents()` that iterates `documentComponents()` and draws typed Transform/Collider/Spawn cards. FPS Inspector now uses this shared dispatch rather than hand-coded per-object Transform section.
- This is a document-backed component UI, **not** native ECS reflection. RTS authored entities still use their existing DetailsPanel plus the shared Position drawer. Read-only component inventory remains separate.
- Validation: source edits committed; **build/tests NOT RUN**. GitHub Actions for `0e818076`: Studio validation queued (Ubuntu, Windows, macOS jobs), CI queued (multiple platform jobs). This is a CI availability blocker, not a passing result.
- Next: get actual build diagnostics, then migrate RTS document Inspector into the same card dispatcher without duplicating fields; build native ECS component registry and writeback.

### 2026-11-17 — RTS authored entities use the same editable component-card dispatcher
- Branch: `feature/universal-entity-inspector-fps`.
- Commit: `90262a21` (`apps/editor/DetailsPanel.cpp`).
- Replaced the RTS-only direct `drawDocumentPositionField` call with `drawDocumentEditableComponents`. Both RTS authored DocEntity and FPS collider/spawn now dispatch editable document components through the same `EditorComponentPanel.h` function.
- Existing RTS DetailsPanel `mapFields` and runtime ECS `sections` remain in place; this does **not** constitute a native ECS Inspector replacement.
- Validation: source change committed; **build/tests NOT RUN**. CI previously observed queued; no passing build claimed.
- Next: obtain CI diagnostics and test UI; implement typed runtime ECS component registry and writeback, then eliminate redundant document Inspector paths safely.

### 2026-11-17 — Document adapter defensive validation
- Branch: `feature/universal-entity-inspector-fps`.
- Commit: `71e586e1` (`apps/editor/EditorEntityAdapter.h`).
- Both mutable and const FPS entity accessors now explicitly accept only Collider/Spawn source kinds; invalid enum values no longer silently fall back to colliders. RTS Transform reads reject non-finite source X/Z values before exposing them to Inspector/Scene.
- Verification: GitHub source inspection only; **build/tests NOT RUN**. CI for previous `42cc0612` observed queued (both CI and Studio validation).
- Next: prioritize a real build and focused adapter tests before further native ECS editor work. Investigate long-running queued Actions and test typed Inspector edits/undo.

### 2026-11-17 — Collider midpoint numeric safety
- Branch: `feature/universal-entity-inspector-fps`.
- Commits: `1a400e7e`, `c1a653bc` (`apps/editor/EditorEntityAdapter.h`).
- Collider midpoint calculation now uses half-scaled endpoints instead of adding two potentially huge finite values, avoiding spurious overflow. FPS source-key index bounds comparison is explicitly 64-bit.
- Verification: source edits committed; **build/tests NOT RUN**. CI for `820ed35c` remained queued on latest check.
- Next: run actual editor build and targeted component adapter tests; inspect CI queue/runner availability before claiming functional readiness.

### 2026-11-17 — macOS build failure reported and fixed (awaiting rerun)
- User ran `cmake --build build-metal -j 8` on macOS; compiler failed at `apps/editor/EditorEntityAdapter.h:69`: cannot convert `const Json*` to `Json*` when using `fps.member(collection)[ref.index]`.
- Verified `engine/data/Json.h`: `operator[](size_t)` is const-only; `Json::at(size_t)` returns mutable `Json&`.
- Fix commit `9f4493e`: changed to `&fps.member(collection).at(ref.index)`.
- Verification: API signature inspected in source; **macOS rebuild NOT YET RUN after fix**. Ask user to pull branch and rerun `cmake --build build-metal -j 8`; collect subsequent errors.
- Next: fix remaining compiler diagnostics, then launch editor and test RTS/FPS Inspector, selection and Undo.

### 2026-11-17 — macOS compiler error in SceneWindow and Hierarchy warnings
- User's second `cmake --build build-metal -j 8` failed at `apps/editor/windows/SceneWindow.cpp:88`: undeclared identifier `hit`. Also reported unused `box` and `spawn` in `HierarchyWindow.cpp`.
- Fix commit `a6c8e700`: removed obsolete `if (hit < 0) pickSelect(ed, hovered, io);` after the shared selection path already invokes `pickSelect`.
- Cleanup commit `017a5731`: switched FPS Hierarchy collider/spawn iteration to indexed loops, removing unused variables.
- Validation: fixes committed; **macOS rebuild NOT YET RUN after these commits**. User should pull branch and rerun build.
- Next: resolve further build errors, launch editor, test selection/Inspector/Undo; record actual test results.

### 2026-11-17 — macOS clang warnings and FPS Hierarchy correctness
- User reported `HierarchyWindow.cpp:87` double increment of `index` (loop header and body), and `EditorFps.cpp:158` wrong `%d` format for `size_t`.
- Fix commits: `28c8d0c8` removes duplicate body increment (also prevents every second Spawn Point from being skipped); `fa50242c` uses `%zu` for source index.
- Verification: source fixes committed; **macOS rebuild NOT YET RUN after these commits**.
- Next: user pull and rebuild; launch editor and verify all FPS spawn entries appear in Hierarchy and Inspector displays correct indices.

### 2026-11-17 — Inspector UI redesign after user screenshot
- User reported duplicate bullet component inventory, 2D-only Transform, confusing `Карта (правка)` and unwanted Lua Custom Inspector. Screenshot showed Sedan authored RTS entity and native module unavailable warning.
- Commits: `35293b86` (`EditorComponentPanel.h`) removes bullet inventory and renders all document component types as collapsible cards with read-only fields; `27beb65d` (`DetailsPanel.cpp`) removes bullet inventory and renames map editor `RTS Object`; `fb6363f6` (`EditorFps.cpp`) removes duplicate list; `0958cc4f` (`InspectorWindow.cpp`) removes default Lua Custom Inspector; `2b95fc08` (`EditorComponentPanel.h`) introduces native C++ `InspectorDrawerRegistry` with whole-component and per-field callbacks.
- **Partial implementation, not feature complete.** RTS map serialization still stores XZ only; do not invent Y/rotation/scale fields. Native ECS Transform reflection and writeback remain to be implemented. RTS legacy `mapFields` may still duplicate `RTS Object` card: migrate it into the card instead of maintaining two UI sections. Generic recursive editing for arbitrary component fields is not implemented; current fallback is read-only for non-Transform/Collider.
- Validation: GitHub source edits only; **build/tests NOT RUN**. Need macOS rebuild and screenshot review. Lua file remains in repository for compatibility but is not rendered by default Inspector.
- Next: resolve duplicated RTS Object section; build native ECS component field descriptors (3D Transform, rotation, scale only when supported), generic type-driven field editing, then compile/test and inspect UI.

### 2026-11-17 — RTS Object card and editable display name (user screenshot feedback)
- User reported RTS object properties were still outside component cards; no editable display name and no object Enabled toggle.
- Commits: `2a4a0712` (`DetailsPanel.cpp`) wraps authored RTS map fields inside a collapsible `RTS Object` card; `d1f6a41d` (`EditorEntityAdapter.h`) removes duplicate read-only RTS Object card; `6e523726` (`DetailsPanel.cpp`) adds editable `displayName` stored in `DocEntity.extra` and shown in Inspector header, separate from stable script/road `id`.
- **Enabled toggle NOT IMPLEMENTED**: DocEntity schema has no enabled flag and no confirmed runtime disable semantics. Do not add a cosmetic checkbox that only changes JSON but leaves the entity active. Next agent must implement authoring serialization + preview/world instantiation behavior + editor Scene/Hierarchy state + undo tests as one coherent feature.
- Validation: GitHub edits only; **build/tests NOT RUN**. Verify `ui::inputString` overload and edited field persistence, including save/reload.
- Next: implement actual disable semantics end-to-end; migrate other RTS object properties to generic component field editor and validate UI/build.

### 2026-11-17 — Remove synthetic duplicate RTS Object card
- User screenshot showed two `RTS Object` cards: synthetic read-only `DocumentComponentKind::Gameplay` and legacy editable `DocEntity` metadata.
- Commits: `fbeb58e0` (`EditorEntityAdapter.h`) removes synthetic `RtsObject` descriptor from `documentComponents()`; `d8b5e768` (`DetailsPanel.cpp`) labels remaining authored-data editor `Map Entity Metadata` instead of presenting it as an ECS component.
- **Architectural clarification:** `RTS Object` was a UI placeholder, not a native ECS component. `DocEntity` is map-authoring data; the live runtime components are separate. Do not create fake components to display authoring metadata. Future Inspector must show actual runtime ECS component IDs and schema-backed values, with map metadata separated.
- Validation: source inspection and commits; **build/UI tests NOT RUN**. Verify no duplicate cards after rebuild.
- Next: native ECS component introspection and editable reflection, real Transform rotation/scale when present, header name/Enabled semantics with persistence and runtime behavior.

### 2026-11-17 — ECS snapshots become authoritative for RTS Inspector component cards
- User explicitly required REAL ECS component identities/values, not synthetic `DocEntity` components. Reviewed `apps/editor/core/WorldInspect.cpp`: `inspectEntity(Simulation&, RtsModule&, Entity, rulesJson)` iterates actual `World::has<T>` descriptors and serializes live ECS component values under `components`. `DetailsPanel::prepare()` already builds sections using those actual IDs.
- Commit `9d4c1a7c` (`apps/editor/DetailsPanel.cpp`): removes synthetic `drawDocumentEditableComponents()` from RTS Inspector; displays real ECS component `sections(ed)` first, then optional collapsed authoring-only `Map Entity Metadata` after them. Never call map fields real ECS components.
- **Critical limitations:** existing ECS inspection is currently read-only, since the extracted JSON is a snapshot and has no writeback. `WorldInspect.cpp` descriptor serialization is real but RTS-specific; FPS and native project modules still need adapters / reflection. Rotation and Scale must only be displayed when present in actual ECS Transform schema. An unavailable native module means no real native components to inspect; do not synthesize them.
- Verification: GitHub source read/commit only, **compile/UI tests NOT RUN**. Next agent must build and check UI with working native module; then implement typed per-component `World&` writeback with edit/undo policy and serialization persistence.

### 2026-11-17 — Typed field metadata and authored Transform editing on real ECS cards
- Branch: `feature/universal-entity-inspector-fps`.
- Commits: `1f8d7bb6` (`apps/editor/core/WorldInspect.h`), `8d8bc75f` (`apps/editor/core/WorldInspect.cpp`), `019d8fdb` and `62367c2a` (`apps/editor/DetailsPanel.cpp`).
- Exposed `WorldComponentField` and `worldComponentFields(componentId)` backed by typed metadata matching the **actual ECS descriptor serializers** for Transform, Owner and Unit. Unknown component fields continue to display via JSON fallback; this is not a complete native C++ reflection system.
- The RTS Inspector `Transform.pos` value now comes from the real ECS snapshot, with X/Z editable ONLY for a single map-authored entity and persisted via the shared document adapter, with Undo snapshot on gesture activation. The real ECS Y coordinate is displayed **read-only** because `DocEntity` has no serialized Y. Runtime/play objects remain read-only.
- `Transform.yaw` (stored in turns in the ECS) is rendered in degrees from its real value, read-only until there is a rotation persistence contract. Scale is not part of this RTS `Transform` struct and must not be fabricated.
- Limits: There is still no arbitrary native ECS field writeback, runtime ECS Undo, or FPS native component serializer. Persisted document X/Z changes trigger normal preview rebuild; never mutate the throwaway preview's World directly and call it saved.
- Verification: source changes committed via GitHub; **compiler, unit tests and UI test NOT RUN** in this stage. Next: macOS compile, verify ECS Transform vs map coordinates, Undo/redo and scene changes; then implement type-safe persisted field setters for further ECS components and native plugin schema registrations.

### 2026-11-17 — Persisted typed ECS-field bindings (Transform and Owner)
- Branch: `feature/universal-entity-inspector-fps`.
- Commits: `b3a3740e` (`apps/editor/EditorEcsWriteback.h`), `858e953b` (`apps/editor/DetailsPanel.cpp`).
- Introduced explicit allowlisted `canWriteAuthoredEcsField()` and `writeAuthoredEcsField()` functions, keyed by the **actual ECS component IDs and field IDs**, not placeholder document component names. Initial persisted bindings are `Transform.pos` (RTS map X/Z only) and `Owner.player` (map owner 0..N up to 255). All other fields are non-writable until a valid persistence contract exists.
- RTS Inspector field drawers for real `Transform.pos` and `Owner.player` now dispatch via the same typed writeback API. `Owner.player` uses `worldedit::setOwner`, including its document undo transaction; editing X/Z continues to use the shared document Transform setter and requires an activation-based beginEdit.
- **Distinction:** This is persisted map-authoring writeback for fields observed in actual ECS snapshots, not mutation of the running ECS. Editor preview rebuilds the live ECS state from the document. Runtime world entities, unknown native components, and fields lacking persisted schema remain read-only. Do not claim generic native runtime component editing or Undo is implemented.
- Verification: GitHub source re-read confirms new API and callsites. **Mac compiler and UI tests NOT RUN**. In particular test Owner slider, hostile owner (-1) read-only behavior, X/Z undo, preview rebuild and map save/reopen.
- Next: request macOS build results and fix any errors; extend typed schema/registration for actual native C++ ECS components with explicit safe editing policy and persistence.

### 2026-11-17 — Native ECS Inspector reader extension registry
- Branch: `feature/universal-entity-inspector-fps`.
- Commits: `abccd3ea`, `8455e68c`, `922dc9a1` (`apps/editor/core/WorldInspect.h`); `7d233bdd`, `fccf3996` (`apps/editor/core/WorldInspect.cpp`); `9f915abf` (`apps/editor/DetailsPanel.cpp`); `60618a9e` (`docs/editor-native-ecs-inspector.md`).
- Added `WorldInspectorExtension` with native C++ `has(const World&, Entity)`, `read(World&, Entity)`, owned field descriptions and a user-visible title; `registerWorldInspectorExtension` / `unregisterWorldInspectorExtension`. `inspectEntity` now includes only **present real ECS pools** supplied by extension readers; it does not infer components from `DocEntity`. The component UI reads registered names and field schemas, with a default JSON fallback.
- Registry access is guarded by mutex and callbacks are copied into an inspection snapshot. Dynamic module unloading requires an external synchronization barrier to avoid callbacks from unloaded code. Registration is **in-process C++ only**, not wired to the existing runtime native module loading/export ABI.
- C++ `World::tryGet<T>` does not expose a const overload, so `read` receives mutable `World&` but must be observational. Built-in `World::has<T>()` remains the authoritative component existence check.
- `worldComponentFields` now returns a copy and `WorldComponentField` owns string metadata to keep dynamic registration/unregistration from leaving dangling pointers; `DetailsPanel` uses `std::optional` to own the selected schema.
- Verification: examined ECS header signatures and inspected committed source through GitHub; **compiler/tests/native module integration/UI NOT RUN**. No native component is auto-registered by this stage. The world tree component bitset/filter still enumerates only built-in RTS types.
- Next: get macOS build result, add in-process registry tests and then connect project native-module lifecycle safely; implement writeback hooks with real project persistence and Undo before making native fields editable.

### 2026-11-17 — NativeModule lifecycle integration for ECS Inspector
- Branch: `feature/universal-entity-inspector-fps`. Commits: `260a9c62` (`modules/rts/RtsNative.h`), `c7dfa114` (`modules/rts/RtsNative.cpp`), `3227d1d1` (`apps/editor/core/WorldInspect.cpp`), `008b91b4` (`apps/editor/DetailsPanel.cpp`), `1e657929` (`docs/editor-native-ecs-inspector.md`), `0c1f4b29` (`docs/native-modules.md`).
- Architecture verified against `docs/native-modules.md`: AIVERSE C++ modules are **statically linked into project-specific editor builds**, not `dlopen`/hot-reloaded. `NativeModules::load` creates module instances from `compiledNativeModules` for `rules.json/nativeModules`; editor preview uses that Simulation.
- Added optional virtual `NativeModule::inspectComponents(World&, Entity, Json& components, Json& schemas) const` with a no-op default. `NativeModules::inspectComponents(Simulation&, Entity, ...)` calls the **loaded module instances only**; it aggregates real reported ECS components, refuses overwrite of existing builtin/component names, and forwards per-field schema descriptors.
- `inspectEntity` now merges the current Simulation's `mod.native().inspectComponents` output with existing ECS pool snapshots, optionally adding `componentSchemas`. `DetailsPanel` uses these scoped schema titles for component cards and field labels, without creating `DocEntity` placeholders. The earlier global C++ registration API remains optional for host-side extensions; module hooks avoid stale global callbacks on switching games.
- **Status: partial, read-only native ECS inspection.** No currently existing project module has been updated to implement this new optional method; native module-specific components will appear only once their owning modules opt in. A schema is not actual runtime component reflection and does not imply writeback. Native component writes, persistence, undo, complex field drawers, and generic FPS ECS inspection remain outstanding.
- **Validation: GitHub source edits only; compiler, tests, module integration, and UI NOT RUN.** Next step is `cmake --build build-metal -j 8` on macOS with an actual project's native module, followed by smoke checks: one present component produces exactly one card, missing component produces none, schema title is correct, switching projects does not retain old native component callbacks, existing RTS components remain correct.

### 2026-11-17 — macOS compiler error: WorldComponentField C-string misuse
- User's `cmake --build build-metal -j 8` failed at `apps/editor/core/WorldInspect.cpp:361`: `field.id.empty()` called on `const char*` (`WorldComponentField::id`).
- Root cause: field metadata was described as owned strings in the native extension design, but `WorldInspect.h` still declared `const char* id/title`.
- Fix commit `e70871e9` (`apps/editor/core/WorldInspect.h`): change field ID/title to `std::string` to match the registry validation and prevent dangling name pointers when native extensions are unregistered.
- Verification: verified old declaration in GitHub source and committed replacement. **macOS rebuild after fix NOT RUN**.
- Next: user pulls branch, reruns `cmake --build build-metal -j 8`, reports any subsequent compiler errors. After successful build, test native component inspector appearance, data correctness and project switching.

### 2026-11-17 — Fix cascading macOS compiler errors from unclosed namespace
- User's macOS build reported `DetailsPanel.cpp:83` ('cannot define or redeclare collect here because namespace editor does not enclose namespace DetailsPanel') followed by many cascading undefined `ed`, `items_`, `cacheKey_`, etc.
- Root cause: `apps/editor/EditorEcsWriteback.h` opened `namespace aiverse::editor {` but did not close it. Since `DetailsPanel.cpp` includes this header before opening its own namespace, the implementation ended up in a nested namespace.
- Fix commit `393e778f`: appended `} // namespace aiverse::editor` at the end of `apps/editor/EditorEcsWriteback.h`.
- Verification: exact header/source inspected in GitHub and fix committed. **Mac rebuild after fix NOT RUN**. The 20 reported diagnostics likely cascade from this one issue, but must check for further compiler errors.
- Next: user pulls branch and runs `cmake --build build-metal -j 8`; then launch editor and test ECS Inspector for real native components, Undo and scene selection.

### 2026-11-17 — Regression report: RTS Inspector component cards disappeared
- User screenshot shows no component cards after ECS-first conversion. Confirmed in source: `DetailsPanel::draw` no longer submits synthetic map-backed components; it displays cards ONLY from `DetailsPanel::collect` → `Editor::worldDetails` → `inspectEntity` of a valid ECS World preview.
- A missing native module, failed/stale preview, or missing `WorldRow` ↔ `DocEntity` mapping can make `sections_` empty. The prior `DetailsPanel::sections` displayed no feedback for the particular case `withData_ == 0` with an apparently ready preview; it looked as though components silently vanished. Inspect `EditorWorld.cpp::worldRow` (map selection only resolves when `worldFresh()`), `worldDetails`, `WorldPreview::error()`, and `linkMapEntities()` when debugging.
- Commit `9b6b9709` (`apps/editor/DetailsPanel.cpp`): add explicit ECS-preview-loading, native-module-error, missing world entity/mapping, and truly empty ECS diagnostics, plus `Обновить ECS предпросмотр` action. **Does not fabricate fake components or solve missing native module/mapping itself.**
- Verification: source reviewed and committed, **build, native module integration and UI NOT RUN**. User needs to rebuild, inspect the exact displayed warning, and check whether the project-specific editor includes native modules.
- Next high-priority correction: trace missing ECS entity mapping and preview errors on user's project; if a native module is absent, build the project editor using `-DAIVERSE_CONTENT_DIR=<project>`. If ECS exists but no row, fix `linkMapEntities` or the relevant runtime spawn code. Complete universal RTS/FPS audit: FPS still uses JSON-only Inspector and Hierarchy, and RTS still uses `RtsView`; conversion is not complete.

### 2026-11-17 — Consolidate FPS Inspector entry and shared authored scene node extraction
- Branch: `feature/universal-entity-inspector-fps`. Commits: `8b3d4e35` (`DetailsPanel.h`), `b1f5f0bb` (`DetailsPanel.cpp`), `d6e926dc` (`InspectorWindow.cpp`), `7bfd70f1` (`Editor.h`), `8b18916d` (`EditorFps.cpp`), `c5b9a08a` (`EditorSceneNodes.h`), `0d787b1a` (`HierarchyWindow.cpp`), `00d0b0e6` (`EditorFps.cpp`).
- FPS, RTS authored and RTS live selections enter **one `DetailsPanel::draw` API**; removed the separate FPS-only `Editor::drawFpsSceneInspector` and its routing. FPS's document component fields are still document-backed because its editor does not currently expose FPS runtime ECS entities.
- Added `enumerateDocumentSceneNodes(map, fpsProject)`, a single authored-source descriptor layer with source identity, label, group, and shared Transform read. FPS Hierarchy and FPS scene render extraction now both use the same authored node inventory. RTS authored entities are also supported by the descriptor, but RTS rich world Hierarchy still uses its own world-row stack.
- The source abstraction explicitly does NOT pretend document entries are live ECS components.
- Verification: GitHub source changes only; **macOS build/tests/UI NOT RUN**. Risks: FPS source enumeration now happens per Scene refresh; check FPS collider geometry, spawn selection, and FPS Hierarchy labels. Need confirm build and runtime behavior.
- Remaining major separate genre paths: RTS `RtsView` vs FPS primitive `SceneRenderable` extraction, RTS selection hit algorithms vs FPS ray/AABB, genre-specific Scene camera/drag interactions, RTS World Hierarchy vs FPS authored hierarchy, and FPS absence of a runtime editor ECS inspector. Generic gizmo/outline are still missing. This stage should not be described as complete universalization.

### 2026-11-17 — Shared document selection identity adapter
- Commits: `4cd2b410` (`apps/editor/EditorSelectionAdapter.h`), `58c81817` (`apps/editor/EditorFps.cpp`), `61d6822e` (`apps/editor/EditorWorld.cpp`), `5f7cbca3` (`apps/editor/DetailsPanel.cpp`).
- Added `selectionForDocumentEntity()` and `documentEntityFromSelection()`. RTS `Selection::Entity` and FPS `Selection::FpsObject` now have one shared compatibility conversion to `DocumentEntityRef`; the result is used for FPS scene picking, selection actions, dragging and Inspector.
- This is a compatibility layer, NOT yet one universal runtime entity ID. Legacy FPS and RTS selection enum values still exist. There are no FPS per-object ECS entities in the author's current world: `fps::State::actors` is a World **resource**, FPS authored colliders/spawns are JSON, while RTS uses real World entity/component pools.
- Verification: source edits committed, compile/GUI **NOT RUN**. Next: adapt all scene interaction handling to common handlers, keep FPS/RTS source adapter explicit, and implement a cross-genre component inspector provider without inventing native FPS ECS objects.

### 2026-11-17 — Regressions covered with CTest for shared scene adapters
- Commits: `a1694952` (`tests/editor/scene_entity_adapter_tests.cpp`), `40938b8b` (root `CMakeLists.txt`), `ff98fbde` (`apps/editor/EditorSceneNodes.h`).
- Added `aiverse_editor_scene_adapter_tests` / CTest `editor_scene_adapter`, exercising RTS and FPS authored scene enumeration, source IDs, Transform read/write, invalid RTS Y edits, FPS collider/spawn refs, deduplicated group movement, and no-partial-mutation validation.
- These are regression tests of **document scene adapters**, not end-to-end tests of FPS runtime ECS, Metal rendering, live RTS ECS or Inspector ImGui. Linking uses `aiverse_editor_core`, available for builds with editor/tests.
- Verification: test and CMake entries committed, test command **NOT RUN**; macOS build still required. Run `cmake -S . -B build-metal` only if CMake does not automatically regenerate, then `cmake --build build-metal --target aiverse_editor_scene_adapter_tests -j 8 && ctest --test-dir build-metal -R '^editor_scene_adapter$' --output-on-failure`.
- Next: resolve any compiler/test failure and continue universal component provider, scene interaction, and outline migration.

### 2026-11-17 — Shared RTS/FPS authored ray picking and regression coverage
- Commits: `31b202f8` (`apps/editor/Editor.cpp`), `46a0ba35` (`tests/editor/scene_picking_tests.cpp`), `0bdf1403` (root `CMakeLists.txt`).
- RTS authored objects' first-pass 3D ray/AABB intersection now creates generic `SceneRenderable` picking proxies and calls `pickSceneRenderable` — the same closest-hit helper as FPS. RTS source still computes its own bounds from its type/radius/terrain; planetary and visual screen-space/ground fallbacks remain for compatibility.
- Added `editor_scene_picking` CTest to exercise closest-hit ordering, parallel rays, degenerate/NaN ray rejection, non-pickable and invalid bounds. This tests the common picking geometry but not full Scene mouse event/UI pipeline.
- Verification: code review and GitHub commits; **macOS compile/tests NOT RUN**. Check with `cmake --build build-metal --target aiverse_editor_scene_picking_tests -j 8 && ctest --test-dir build-metal -R editor_scene_picking --output-on-failure` after CMake regenerate.
- Outstanding: RTS planet hit-test is specialized, runtime live ECS entity picking is separate, and FPS authored colliders/spawns remain document-backed. Single universal runtime entity/component provider across genre modules remains a large open architectural task. Render extraction (`RtsView` vs FPS), 3D gizmo and mask-based post-effect outline remain unfinished.

### 2026-11-17 — FPS multi-selection parity with RTS authored object dragging
- Commits: `89d50c96` (`apps/editor/EditorFps.cpp`), `39a2034f` (`apps/editor/windows/SceneWindow.cpp`).
- FPS Scene/Hierarchy now highlight **all** selected colliders/spawns from `selectionSet` instead of only the primary item; `moveSelectedFpsSceneObject()` now batches all selected FPS document sources through `translateDocumentEntities()` with one revision change and prevalidation.
- FPS Scene drag preserves a multi-selection when starting a drag on an already-selected member; clicking without moving selects only that member on release. Ctrl/Cmd/Shift selection stays routed through the shared editor `Selection` API.
- Validation: source edits committed, **macOS compile/UI tests NOT RUN**. Manual FPS regression checks: Shift-click mixed collider/spawn, drag group, Undo once, click member without dragging, confirm all selections/labels and no duplicate movement.
- Remaining: generic gizmo handles, snap/rotate/scale with schema-backed persistence, full FPS runtime ECS provider, post-process geometry silhouette and shared RTS/FPS world hierarchy.

### 2026-11-17 — Shared Move gizmo axis selection (RTS X/Z; FPS X/Y/Z)
- Commits: `282ec92e` (`apps/editor/EditorTransformGizmo.h`), `ef7356f1` and `0f34a491` (`apps/editor/windows/SceneWindow.cpp`), `c13a1508` and `e9b73dbf` (`apps/editor/EditorFps.cpp`).
- Added `pickMoveGizmoAxis()`: one screen-space projected-segment picker for all 3 world axes, constrained via mask by what the author's schema can persist. RTS now uses it for X/Z; FPS now uses it for X/Y/Z. FPS visible axes and hit tests both use the actual source's `readDocumentTransform` pivot; group gizmo remains on primary selected source.
- FPS clicking a move handle preserves the selection while dragging; X/Z constrained movement uses ground ray differences, Y uses vertical screen delta, and batch translation is shared with RTS. Snap, local/world gizmo axes, Rotate/Scale, true geometry outline and runtime component writeback remain pending.
- Verification: source reviewed and committed; **no successful macOS build, automated UI test or interactive gizmo test yet**. User should test Y handle, perspective/orthographic camera pitches, group translation/Undo and handle occlusion.

### 2026-11-17 — Move gizmo projected-handle regression test
- Commits: `92dc34c3` (`tests/editor/scene_gizmo_tests.cpp`), `832d2d64` (`CMakeLists.txt`).
- Registered CTest `editor_scene_gizmo`, checking projected axis handle hit selection, missing/disabled axes, invalid extent and offscreen cursor rejection. This exercises common mathematical gizmo picking, not full mouse gesture/Undo.
- Verification: committed; **NOT COMPILED/RUN**. Build with `cmake --build build-metal --target aiverse_editor_scene_gizmo_tests -j 8` and run `ctest --test-dir build-metal -R editor_scene_gizmo --output-on-failure`.

### 2026-11-17 — Static compile review and consistent map display names
- Commits: `e328df1d` (`apps/editor/EditorFps.cpp` adds explicit `<algorithm>` dependency for FPS multi-selection) and `3aaa41f8` (`apps/editor/core/WorldInspect.cpp`).
- World Hierarchy naming now respects `DocEntity.extra.displayName` when no explicit ECS script label is present. The display name remains authored metadata, not a fabricated ECS component.
- Attempted to reach GitHub from the model's isolated build container, but the container could not resolve `github.com`; no repository checkout or compilation was possible there. GitHub connector changes were reviewed statically.
- **Validation not completed:** full CMake/editor compile and all new CTest cases are still unverified by execution. User should pull and run `cmake --build build-metal --target aiverse_editor aiverse_editor_scene_adapter_tests aiverse_editor_scene_picking_tests aiverse_editor_scene_gizmo_tests -j 8` followed by `ctest --test-dir build-metal -R 'editor_scene_' --output-on-failure`. Check latest GitHub Actions independently (earlier runs were queued).
- **Not yet universal:** FPS uses external-client runtime and `fps::State::actors` (a World resource, not ECS entities), FPS authored colliders/spawns are JSON; RTS uses `World` ECS plus `RtsView`. Editor runtime entity inspection, hierarchy and render extraction still differ; Rotate/Scale, native writeback, true screen-space object-mask outline and end-to-end macOS UI validation are missing. Do NOT mark the PR done.

### 2026-11-17 — Ensure editor regression tests run in Release configurations
- Commits: `06de5e88`, `8adaa963`, `f273b398` (the three `tests/editor/scene_*_tests.cpp` test files).
- Replaced `assert(...)` with an always-active `require(...)` helper. On Release/NDEBUG builds, plain assert was compiled out, causing tests to falsely pass and skip side-effectful setup. Failed checks now call `std::abort()` regardless of build type.
- Validation: GitHub source edits committed; **CTest run still outstanding**.

### 2026-11-18 — FPS runtime resource actor provider (read-only)
- Commits: `ae877c37` (`Editor.h`), `95233127` and `9c9b2c25` and `a09febee` (`EditorFps.cpp`), `292bfc80` (`HierarchyWindow.cpp`), `ebf39145` (`DetailsPanel.cpp`), `c48d6fc5` (`InspectorWindow.cpp`), `693826b1` (`SceneRenderable.h`), `0272aadc` (`EditorWorld.cpp`).
- Important architectural fact: FPS actors are stored in `fps::State::actors` (World resource), NOT individual ECS entity pools. Added `Editor::fpsRuntimeSnapshot()` which runs actual `fps::Module::setup`, reads actor state from its World resource, and caches by document revision, project/map path and rules revision; errors are surfaced rather than replaced with fake components.
- FPS Hierarchy shows `Runtime Actors` after authored colliders/spawns. Shared DetailsPanel supports `Selection::FpsActor`, displaying actual runtime actor fields **read-only**; Scene draws selectable markers through `SceneRenderable::runtimeActor` without reusing FPS document source IDs. FPS actor selections can be made by Hierarchy or Scene picker; no writable snapshot-to-world storage contract exists yet.
- No generic structural ECS conversion was performed on `fps::State::actors`; reworking FPS deterministic netcode, snapshots and rollback requires a separate migration. Runtime snapshot is a provider/bridge, not per-actor ECS reflection.
- Verification: GitHub sources committed, **CMake build, tests and runtime scene check NOT RUN**. Check `fpsRuntimeSnapshot` with valid/invalid FPS map, rules change, map switching, runtime actor markers/selection; ensure expensive setup is cached and not called each frame.
- Further work: shared runtime provider interface for RTS ECS and FPS resource records; unified runtime Hierarchy/Inspector field adapters, persisted native ECS editing, Rotate/Scale, geometry mask-based post outline. Do NOT mark migration complete.

### 2026-11-18 — Scene post-effect selection outline (first integrated implementation, unverified)
- Commits: `d9f520d0` (`engine/render/Renderer3D.h`), `0f04b230` (`Renderer3DModels.cpp`), `6cde0a13` (`SelectionOutlineShaders.h`), `e18576d3`, `2e9fd85a` (`Renderer3D.cpp`), `d9c6577f` (`apps/editor/Editor.cpp`), `371d9ba9` (`apps/editor/SceneRenderable.h`).
- Scene now requests `setSceneSelectionOutline(true)` only around the editor Scene rendering; Game keeps the previous legacy renderer behavior. Renderer3D stores per-primitive selection tint and uses model `ModelDraw.outline` as a selection flag, but suppresses the legacy inverted-hull `renderModels(3)` phase in normal Scene color rendering.
- Added a separate RGBA8 selection mask pass that reuses the scene depth, disables depth clearing, renders selected model geometry via the existing model pipeline with **zero normal extrusion**, renders primitive selection geometry and composites **exterior mask edges** with a 2-pixel full-screen shader (GLSL + MSL). Main color/depth passes are resumed with load/no-clear. Selected models' materials and all submeshes are expected to contribute to one combined mask, not individual model wire edges.
- IMPORTANT: this change is **NOT confirmed working**. Shaders, Metal/GL pipelines, mask depth compatibility, HDR postfx interaction, alpha-masked/transparent geometry, occlusion, and final screenshot quality must be checked on macOS and other render backends. The mask is Scene-only; no X-Ray mode or configurable thickness yet. If the shader fails to initialize, editor selection outline can be absent.
- Validation: GitHub source edits and static inspection only. **CMake/Metal compilation and image tests NOT RUN**. Do not merge until green. Test representative Sedan with separate wheels, body and glass, overlapping geometry, distant camera, FPS primitives, transparent materials and custom PostFx enabled. Also ensure Game view unchanged and no extra full-screen pass when selection is absent.

### 2026-11-18 — Schema-backed FPS oriented box Rotation and Scale (Inspector + Scene Gizmo)
- Commits: `6a529bb8`, `7dd68317`, `5e3cef36` (`apps/editor/EditorEntityAdapter.h`); `badc909b`, `1655a2de` (`apps/editor/EditorFps.cpp`); `09284a25` (`apps/editor/EditorComponentPanel.h`); `daf3f07c` (`tests/editor/scene_oriented_collider_tests.cpp`); `1bc7cd5a` (root `CMakeLists.txt`); `8991f4ab` (`Editor.h`); `532ab6c1` (`EditorTransformGizmo.h`); `7d48e766`, `b2592f65`, `8c3e264d` (`SceneWindow.cpp`).
- FPS already supports real oriented colliders in its runtime schema `{id,center,half,yaw}`, with yaw in turns. Shared document adapter now reads/writes their true center, validates half extents and yaw, translates in multi-entity batch and exposes `Position XYZ`, `Rotation Y` (degrees UI) and `Scale XYZ` (twice half extents) only when present. The object remains in its original FPS map format; it is NOT an ECS component.
- FPS scene now draws an actual rotated oriented-box primitive and computes conservative world AABB for picking. The selected oriented collider gains Move, Rotate and Scale toolbar modes in Scene: projected yaw ring hit-testing and drag updates yaw; scale axes drag updates corresponding half extents. All edits use MapDoc undo and trigger preview rebuild. RTS and plain FPS colliders do not fabricate unsupported rotation/scale controls.
- Added `editor_oriented_collider` CTest for authentic oriented collider position, rotation, scale, duplicate batch movement, invalid nonpositive dimensions and retained collider id.
- These Rotate/Scale gizmos are **schema-specific initial implementations**, not full universal 3D rotation/scale for arbitrary ECS component types, world/local axes or multi-selection rotation/scale.
- Validation: GitHub source and test entries committed; **compilation, CTest and macOS visual verification NOT RUN**. Test selection ring handling at top-down/side camera angles, ensure rigid-box yaw matches runtime `orientedBoxTriangles`, confirm Undo/Redo/save/load. Fix any build errors before merge.

### 2026-11-18 — Native ECS field persistence hooks and type-aware default editors
- Commits: `a17de986` (`modules/rts/RtsNative.h`), `17a708cf` (`modules/rts/RtsNative.cpp`), `03bce05d` (`apps/editor/EditorEcsWriteback.h`), `bd39cb3b` (`apps/editor/DetailsPanel.cpp`).
- Loaded native module instances may implement `inspectWriteback(componentId, fieldId, value, currentExtra, updatedExtra)` to validate and map an **actual ECS field** back to authored `DocEntity.extra`. The editor commits the updated JSON with `MapDoc::touched()` and uses `MapDoc::beginEdit()` at ImGui control activation for Undo. No editor-owned arbitrary in-memory World writes or unsupported generic persistence are performed.
- `componentSchemas[componentId].fields[fieldId]` can opt into `editable:true` and supply a type (`integer`, scalar number, bool, string, numeric Vec3); the DetailsPanel draws a typed control and calls the loaded module's writer. Unknown/native-unregistered fields remain read-only, preserving safety when schemas change.
- Crucial contract: module writer must ensure changed authoring extras are consumed at next Simulation::setup to reconstruct the ECS component; a temporary runtime state is never treated as persistent. Only statically linked and actually loaded project modules can supply this callback. Build/integration/Undo behavior are NOT verified yet; require tests and documentation example before merge.

### 2026-11-18 — RTS facing and schema-backed Rotation Y
- Commits: `770832db` (`EditorEntityAdapter.h`), `e27c945f` (`EditorComponentPanel.h`), `53627d74` (`DetailsPanel.cpp`), `e0db4e10`, `fdde9a58`, `edb0b219`, `bcf54661` (`SceneWindow.cpp`), `eec6c1ab` (`tests/editor/scene_rotation_tests.cpp`), `4cafb432` (`CMakeLists.txt`).
- RTS game setup consumes `DocEntity.extra.facing` (degrees) to construct the real ECS `Unit.facing` direction vector. Shared Transform adapter now exposes optional Rotation Y only if the authoring facing property really exists, and persists edits back to that property. The real `Unit.facing` Inspector field renders an editable angle mapped through this authoring source rather than modifying an ephemeral ECS snapshot.
- Shared Scene Transform toolbar now offers Rotation for RTS entities with real facing; projected rotation-ring picking and mouse delta edit the same field with Undo. RTS Move shows only X/Z handles, not fake Y. RTS still has no per-object Scale in `DocEntity`; it is deliberately unavailable.
- Added CTest `editor_scene_rotation` for actual facing preservation, absent-field rejection and yaw ring picking. **Neither macOS build nor test run is verified**.

### 2026-11-18 — Source-level structural integrity check
- Fetched 18 affected C++ headers/translation units from PR branch and ran a lexical brace-balance pass that ignores C++ comments, ordinary string literals and character literals. All reported balanced braces with no premature namespace closing. This is **NOT a compilation, linkage, shader validation, CTest pass or runtime quality confirmation**; it only catches structural mistakes such as the earlier missing namespace brace.
- Latest confirmed GitHub Actions for the handoff revision were queued/pending. Local isolated container cannot resolve github.com for a checkout, so a full macOS/Metal build and visual testing require a project runner/user machine.
- Before merging: compile `aiverse_editor`, run `editor_scene_adapter`, `editor_scene_picking`, `editor_scene_gizmo`, `editor_oriented_collider`, `editor_scene_rotation` and platform suites; visually test selected Sedan (multiple submeshes) outline vs Game tab, transparent/alpha-masked meshes, FPS authored actor preview and oriented collider Rotate/Scale with Undo/Redo. The new modules should not be described as production-verified until these tests pass.

### 2026-11-18 — Common runtime entity view for RTS ECS rows and FPS World resource actors
- Commits: `12339ea7` and `d11b3ba8` (`apps/editor/EditorRuntimeEntityProvider.h`), `472297e5` (`HierarchyWindow.cpp`), `ce328c07` (`EditorFps.cpp`), `b48fbcc7` (`DetailsPanel.cpp`), `c4a4ac13` (`EditorFps.cpp` FPS snapshot refresh during drag), `247756f5` (`EditorEcsWriteback.h` include hygiene).
- `enumerateEditorRuntimeEntities(Editor&)` returns a common editor-facing `EditorRuntimeEntity` record for either genuine RTS ECS WorldRows (selection, name, world position) or FPS actors stored in World `fps::State` resource (selection, name, snapshot fields, world position). The source explicitly marks which entries represent actual ECS entities: an FPS actor record does NOT become a fake ECS `Entity`.
- FPS Hierarchy, Scene runtime actor markers, and the common Inspector now use this same provider rather than independently parsing the actor array. The existing RTS Hierarchy is still a richer grouped WorldModel renderer; provider unification is a data API, not yet a complete replacement for its specialized grouping UI.
- FPS actor preview creation is cached and drag-time rebuild is deferred until mouse release, preventing per-frame `Simulation::setup()` while moving colliders or spawn points. Cache also invalidates on project/map change and rules revision.
- Verification: GitHub source edits committed, **build/tests/UI NOT RUN**. Check real FPS project map actor creation, failed FPS setup, missing modules, selection from both Scene and Hierarchy, cursor focus and no repeated expensive preview setups.
- Remaining architecture: RTS rich Scene extraction is in `RtsView`, FPS uses primitive renderables; these are valid genre extractors into common Renderer3D but not a single `SceneRenderable` submission layer for every animated model. Generic native ECS writeback relies on real per-module persistence hooks and is not automatically available for arbitrary fields. Runtime FPS per-actor ECS pool migration would affect authoritative FPS save/rollback and has NOT been performed.

### 2026-11-18 — Final source integrity audit for current integration batch
- Commit `03687cd2` (`SceneWindow.cpp`): automatically resets unsupported Rotate/Scale mode to Move when changing selected entity type (e.g. FPS oriented collider → spawn, RTS entity without facing).
- Checked brace/namespace nesting in 13 updated C++ headers and source files by tokenizing ordinary comments/string literals, counting braces and testing for premature closing braces. All 13 were structurally balanced; this is **NOT a C++ compile or GPU shader test**.
- CI for earlier commits remains queued/pending. There is no confirmed passing PR build, CTest run, or visual verification for this integration batch.
- Current remaining hard limitations: FPS actors still live in an authoritative World **resource**, not per-actor ECS pools (full migration requires save/rollback/netcode redesign); RTS rich World Hierarchy is separate internally from FPS document grouping although FPS actors and RTS rows have a common view API; generic native component editing requires opt-in persistence hooks from the project's actual modules; game genres may have no persisted Rotation/Scale; model+primitive mask outline must be tested against hardware backends and transparent materials.
- Do NOT mark PR fully finished/merge until the user runs a successful build, all editor_scene_* and editor_oriented_collider CTests, and Scene visual checks. Follow-up agent should fix any errors found by the build before extending the renderer further.

### 2026-11-18 — Review and harden new screen-space selection outline
- Commits: `432f4a9d` (`engine/render/Renderer3D.h`), `c517f4d2` (`engine/render/Renderer3D.cpp`), `37c73832` (`engine/render/Renderer3DModels.cpp`), `2fdabbd3` (`engine/render/ModelShaders.h`).
- Inspected the branch's new `renderSelectionOutline` implementation. Selected-model primitives render a color mask against the current scene depth, then `SelectionOutlineShaders.h` composites a screen-space exterior border. It explicitly sets zero normal displacement for the mask (legacy game inverted-hull remains separate).
- Fixed mask primitive preview rendering to use a dedicated **depth-test ON, depth-write OFF** pipeline; the former reuse of `litPipe_` could overwrite scene depth while writing the selection mask, corrupting later depth-based visibility.
- The mask pass also now requests alpha-tested model shaders for `AlphaMode::Mask`; GLSL and Metal OUTLINE branches discard cutout fragments when generating the selection mask. This preserves transparent holes on fences/foliage and avoids painting their bounding hull.
- Verification: code inspection and GitHub commits only; **NO Metal/macOS build, shader compilation or screenshot confirmation**. Test selection/hover on whole multi-mesh Sedan, depth-occluded objects, cutout materials, FPS collider primitives, HDR PostFx on/off, and resize/recreate GPU resources.
- Remaining: explicit X-ray mode is not wired; visual QA, mask sampler orientation across Metal/GL, and robustness against shader compilation failure still need validation. Never mark outline finished until those pass.

### 2026-11-18 — Reuse custom field/component drawers for actual ECS Inspector cards
- Commit `7752488d` (`apps/editor/DetailsPanel.cpp`): the existing `InspectorDrawerRegistry` now also overrides real `WorldInspect`-derived ECS component cards and individual fields. Registration by true component ID or `Component.field` path; the fallback remains the generic schema/JSON view.
- Overrides are available **only for a single authored map-backed ECS entity in edit mode**; runtime entities, multi-selection, stats/rules sections remain guarded/read-only. Document source is an editing persistence target, not used to fake ECS component presence. Project-specific trusted C++ drawers must handle their own validation and undo semantics.
- Not an exported stable C ABI or a hot-reloadable editor plugin: this is an in-process C++ drawer registry. Native gameplay modules should expose data via `NativeModule::inspectComponents`, and use a project/editor bridge for ImGui-specific custom drawers if needed.
- Verification: source-level review and commit only; macOS build and runtime registration tests **NOT RUN**. Test component override, per-field override, default fallback, project change and undo.

### 2026-11-18 — Regression test for selection-mask GPU pipeline depth safety
- Commit `6d5f1f68` (`tests/tests.cpp`): added `selection_outline_mask_pipeline_preserves_scene_depth` using existing `entityfxtest::RecordingDevice`.
- The test inspects the pipeline created by `Renderer3D::initSelectionOutline()` and asserts it is RGBA8, color-writing, depth-tested, **depth-write disabled**. It is part of the existing `aiverse_tests` executable, so it runs through the standard CTest suite when executed.
- Source-level checks were performed, but **the test and macOS editor build have NOT been executed** in this environment. This test validates a pipeline descriptor, not Metal shader compilation, mask occlusion, or final visual quality.

### 2026-11-18 — FPS Hierarchy interaction parity with universal selection
- Commit `d9de10a5` (`apps/editor/windows/HierarchyWindow.cpp`): FPS document nodes and runtime actor rows now use the same `Selection` modifier handling as RTS (Cmd/Ctrl toggles, Shift adds, unmodified selects), common `ed.isSelected` highlighting, a search field over node/group labels, and double-click camera focus.
- This removes FPS-only `selectFpsSceneSource` branching from the Hierarchy click path without fabricating runtime ECS components. It keeps the RTS rich world-tree implementation intact to avoid losing its owner/type grouping, hide/lock, drag/drop and keyboard functionality.
- Verification: source-level inspection only; macOS compilation and UI tests **NOT RUN**. Check mixed FPS collider/spawn selection, Ctrl/Shift, search, double-click and runtime actor selection. A true shared visual row tree and unified backend provider across all genres are still open.

### 2026-11-18 — FPS real ECS entity migration, compatibility stage 1
- User requested moving ALL RTS/FPS runtime data to ECS. This stage starts with FPS without changing deterministic gameplay, network framing, or published save v9.
- Commits: `0b3ff8be` (`modules/fps/FpsModule.h`) and `1e885bdd` (`modules/fps/FpsModule.cpp`) add actual `World::Entity` identities and ECS component pools `fps::ActorEntityIdentity`, `fps::ActorEntityTransform`, `fps::ActorEntityVitals`, plus `ActorEntityIndex` (slot→Entity resource). Every FPS player is assigned a stable ECS Entity at `fps::Module::setup`. `fps.ecsMirror` runs after FPS tick and updates components, including when match is over. Save loading rehydrates ECS values; rollback snapshots clone ECS pools with the World.
- Critical compatibility: legacy `State::actors` is still authoritative and unchanged for gameplay, lag compensation (`HistoryFrame`), network packets and save v9. ECS components are a **real but derived** world view, not yet a source-of-truth migration. Never imply the gameplay uses ECS pools exclusively. No hash/checksum format changes were made.
- Old FPS save allocator compatibility: `e4e7db20` recreates missing actor ECS identities when a save predating these pools loads. `7f0f2167` extends the tests to exercise a pre-ECS empty allocator. Validation still pending real execution.
- Editor: `f0c084ac` (`apps/editor/EditorFps.cpp`) reads real ActorIdentity/Transform/Vitals from the runtime ECS World, placing each in the `components` snapshot rather than copying from `State::actors`. `d5de5c36` (`DetailsPanel.cpp`) draws these ECS component cards for FPS actors. Tests `3e8c073f` (`tests/fps/tests.cpp`) validate identity, pose, vitals, ticks, save/load, and `SimSnapshot` rollback.
- **Verification: source changes committed; compilation, FPS tests, network tests and UI not run in this session.** Run `cmake --build build-metal --target aiverse_fps_tests aiverse_editor -j 8`; `ctest --test-dir build-metal -R '^fps$|^fps_transport$|editor_scene_' --output-on-failure`. For gameplay parity compare baseline deterministic match checksums and old save v9 load tests.

### 2026-11-18 — FPS ECS runtime projectile/loot/vehicle identity migration
- Commits: `eca53c76`, `a040cf45`, `efaf4f7a`, `31fa4cf2` (`modules/fps/FpsModule.h/.cpp`). New real ECS components `ProjectileEntity`, `LootEntity`, `VehicleEntity`, with `DynamicEntityIndex` mapping stable kind/id keys to real entities. A deterministic post-tick sync creates/updates/destroys these ECS entities to mirror objects in `State`. Load/save and rollback preserve/recreate their ECS views.
- `934f4a4b` / `b259cda5` add FPS test coverage for creation, field values, removal and rollback of these dynamic ECS entities.
- Editor integration: `17fee21e` (`EditorFps.cpp`) gathers actual World ECS dynamic pools into runtime component snapshots; `9229761b` (`EditorRuntimeEntityProvider.h`) exposes them through the shared runtime provider; `51239e6a` (`SceneRenderable.h`), `4b90970c` (`EditorFps.cpp`), `0a425a74` (`EditorWorld.cpp`) add scene visualization, selection and ray picking; `15c777a5` / `ea05bb95` (`Editor.h`, `EditorWorld.cpp`) add stable `FpsRuntimeObject` selection and position lookup; `16cffefb` / `eb7f4776` (`DetailsPanel.cpp`, `InspectorWindow.cpp`) show real FPS object ECS cards; `0344eac4` / `b705ae80` (`HierarchyWindow.cpp`) group projectiles, loot and vehicles with 64-bit stable IDs; `5df30868` updates provider comments.
- **Important:** all three dynamic object arrays still live in `fps::State` as the authoritative simulation representation. The new ECS entities are real component pools with deterministic IDs but are a **compatibility mirror**. FPS authored JSON colliders/spawns remain map authoring sources, physics collision remains `SpatialWorld`, and FPS combat systems still update State rather than querying/mutating ECS pools. RTT/FPS whole-game migration is NOT complete.
- This staged approach avoids immediately breaking published FPS save v9, rewind history, prediction/replication and deterministic checksums. The follow-up required for full ECS authority is to refactor FPS gameplay systems to read/update the ECS pools as the source of truth, with a versioned save/network migration and exhaustive parity tests.
- **Verification: none of the new stage has compiled or been run on Metal in this session.** Compile `aiverse_fps_tests`, `aiverse_editor` and `aiverse_fps_transport_tests`, run CTest, and check visible ECS component names and actor/projectile/loot/vehicle scene selection. Never merge PR before passing tests.

### 2026-11-18 — Legacy FPS allocator restoration safety
- Commits `2b518d10` (`modules/fps/FpsModule.cpp`) and `4a08475f` (`tests/fps/tests.cpp`).
- `World::restoreEntityState` restores allocator generation/alive/free vectors **without clearing component pools**. Before this fix, loading a pre-ECS save could recycle a component-pool slot for a different derived FPS object, yielding incorrect mixed Actor/Projectile/Loot/Vehicle component membership.
- `syncActorEntities` removes stale dynamic FPS components on actor identities; `syncDynamicEntities` removes stale actor and wrong dynamic-kind components before assigning a fresh object's type. A new test simulates the legacy empty allocator and checks every dynamic ECS entity has only its correct component kind.
- This preserves game state and network save v9 while maintaining true ECS pool membership. **Build/tests not executed**. Particularly verify legacy save loading with map-initial loot/vehicles and full save↔reload across active projectiles.

### 2026-11-18 — Import ECS 2.0 source branch b75b873 into universal Editor PR #7
- User explicitly requested importing `feature/ecs-architecture-benchmarks` at `b75b873b1b2091a4a4a9bb386d39f3237f5d2585` into `feature/universal-entity-inspector-fps`, preserving ongoing editor/FPS migration.
- Source and target were divergent (185 source-only and 304 target-only commits at inspection); naive branch overwrite would discard editor ECS migration.
- **Core commit `f4cf94d1`**: transferred 42 source blob-exact files including `engine/core/ECS.h`, `engine/sim/{CommonComponents,Simulation,SceneHierarchy,SceneHierarchySystem}`, `engine/net/Rollback.*`, experimental scheduler/guarded world/snapshot code, render hierarchy adapter, benchmark/correctness tests/scripts, GitHub ECS workflow, and the two ECS docs. Used base-target Git tree and lease-checked branch update, not a replace of entire source branch.
- **Build integration `5866850a`**: wired opt-in ECS CTest suites, experimental RTS preprocessing flag (OFF), and COW rollback session test into our existing `CMakeLists.txt` without overwriting editor-specific CMake changes.
- **Overlap resolution commits `3cf9aee4`, `48fae9dd`, `a272d1c4`**: manually merged the source FPS experimental parallel-motor (speculative gather + canonical sequential commit, OFF by default) into our FPS actor ECS mirror code, retaining `syncActorEntities`, `syncDynamicEntities`, current authoritative `State`, save v9/rollback, and existing Editor runtime provider. Counters and opt-in CMake flag preserved.
- **Additional source ports `09644bf5` (RTS `RtsUnits.cpp`), `271ed678` (headless COW flags), `7d5876cf` (FPS tests incl. COW/motor parity)**.
- **Editor conflict resolution `5aacafd3`**: registered real `Parent`, `LocalTransform`, `WorldTransform` component dumps in the existing `WorldInspect.cpp` registry, preserving native module inspection and universal editor changes.
- **Agents guidance `b2b874e6`**: integrated ECS 2.0 coordination / determinism restrictions into existing `AGENTS.md`.
- **Audit:** among the 51 ECS-source-changed files, 44 are blob-identical to source commit and 7 are intentional merged versions: `AGENTS.md`, root and FPS `CMakeLists.txt`, `WorldInspect.cpp`, `FpsModule.cpp/.h`, `tests/fps/tests.cpp`. None omitted.
- **Verification: NOT COMPILED OR TESTED by this assistant.** Model container cannot resolve github.com for local git fetch; source ECS handoff warns the most recent COW/motor/hierarchy changes have not passed latest native Mac tests, despite prior older suites passing. Keep experimental flags OFF; do not merge main until Mac editor/FPS/RTS build, CTest/normal+sanitizer ECS suites, and gameplay parity comparison pass.
- **Commands:** `cmake -S . -B build-metal` (to refresh new test targets); `cmake --build build-metal -j 8`; `ctest --test-dir build-metal -R 'ecs_|fps|aiverse_tests|editor_scene_' --output-on-failure`; `bash tests/run_ecs_experiment_suite.sh` and `bash tests/run_ecs_gameplay_compare.sh` (see script help for args). Build with `-DAIVERSE_FPS_EXPERIMENTAL_PARALLEL_MOTOR=ON` and `-DAIVERSE_ECS_EXPERIMENTAL_RTS_TRANSFORM_JOBS=ON` only for separate parity runs.

### 2026-11-18 — FPS ECS authority migration: stage A (declaration)
- User explicitly requested continuing FPS runtime ECS migration on PR #7, with handoff updated **after every implementation action**. Keep PR #7 target branch `feature/universal-entity-inspector-fps` and preserve save v9/lockstep.
- Commit `b11fa4f`: `modules/fps/FpsModule.h` defines `ActorRuntime { Actor value; }` as the full gameplay payload for an ECS Entity; adds opt-in `Config::ecsAuthority=false`, `Module::actorEcsAuthority()`, and `hydrateActorsFromEcs` declaration. This stage declares the interface **only**, it is not active yet; no gameplay behavior change.
- Next: wire parsing, setup pool, command writes, tick hydrate/commit, save load rollback rehydration and exact same-hash parity tests. Old actor inspector components remain projections.
- **Verification:** not compiled/tested. No changes to main.

### 2026-11-18 — FPS ECS authority migration: stage B (command + tick bridge)
- Commit `e1a5da2` (`modules/fps/FpsModule.cpp`): parse `fps.ecsAuthority` (OFF by default). `syncActorEntities` now creates/updates full `ActorRuntime` components when enabled, while preserving Editor ActorIdentity/Transform/Vitals projections. `applyCommand` validates and writes the full ECS ActorRuntime first (then updates compatibility vector for callers). New deterministic system `fps.ecsHydrate` reads canonical ActorRuntime into the legacy contiguous actor working vector immediately before `fps` tick; `fps.ecsMirror` commits the tick results back to ECS afterwards.
- **Precise migration status:** canonical Actor data lives in ECS **between ticks**, and commands mutate the ECS component, but existing motor/combat/AI algorithms still access the `State::actors` tick workspace. This is a staged ECS authority bridge, **not** full direct component-based gameplay yet. Do not describe it as complete migration.
- Legacy `State` save v9, history/replay, network snapshots, editor runtime views remain unchanged with opt-in OFF. Deserialization already calls syncActorEntities to rebuild ECS ActorRuntime from save v9; SimSnapshot includes both since stored in World.
- Next stage: consistency and rollback parity tests, then migrate one system at a time to query/mutate ActorRuntime directly, remove mirror dependency, then migrate other object types. Validate with `fps.ecsAuthority=true` side-by-side checksums against old mode.
- **Verification:** not built/run. Keep this commit isolated from main until tested.

### 2026-11-18 — FPS ECS authority migration: stage C (parity regression)
- Commit `01055b4` adds `ecsActorAuthorityParity` to `tests/fps/tests.cpp`, invoked by the existing `aiverse_fps_tests` runner. Runs old vector-authoritative vs `fps.ecsAuthority=true` under the same 160-tick command stream, compares authoritative ECS ActorRuntime full Actor payload with the legacy workspace, checks legacy v9 save/reload and SimSnapshot rollback continuation, and exercises direct ECS mutation across a tick barrier.
- **Important finding:** `Module::setup` originally hashes the entire rules JSON into `contentHash_`. The opt-in implementation flag would incorrectly change checksum/save compatibility even when gameplay is byte-identical. Next commit must exclude only `fps.ecsAuthority` from the gameplay content hash, while preserving all actual game rules. This is a deterministic compatibility fix.
- Validation pending; the new test has not run on Mac Studio or CI and will expose any state divergence.

### 2026-11-18 — FPS ECS authority migration: stage D (content hash parity)
- Commit `cf561b1` in `modules/fps/FpsModule.cpp`: `fps.ecsAuthority` is removed from a temporary rules JSON copy **only** before computing `contentHash_`. The actual rules object remains unchanged. The toggle is an implementation/storage choice and must not change legacy save v9 content checks or authoritative cross-worker/network hashes. All other game-rule fields are still hashed exactly as before.
- No changes to the binary save format, command protocol, or actual gameplay rules. `ecsActorAuthorityParity` from stage C should now compare identical checksums across authority modes.
- **Validation:** compile/execute tests pending. Next migration target is full ECS dynamic object ownership (projectile, loot, vehicle), preserving their original order explicitly before changing gameplay algorithms.

### 2026-11-18 — FPS ECS authority migration: stage E (dynamic payload schema)
- Commit `1cb6283` (`modules/fps/FpsModule.h`): declares full payload components `ProjectileRuntime{Projectile value, uint32_t order}`, `LootRuntime{Loot value, order}`, and `VehicleRuntime{Vehicle value, order}`. The order is essential because existing projectile/loot/vehicle vectors are deterministic and gameplay behavior depends on iteration order. Declares `hydrateDynamicsFromEcs`.
- This commit is **only API/schema**, not a running authority migration yet. Next implementation commit will integrate it into `syncDynamicEntities` and `fps.ecsHydrate`, preserving save v9 and existing inspector projection components.
- **Verification:** not compiled or tested.

### 2026-11-18 — FPS ECS authority migration: stage F (dynamic objects live)
- Commit `8883e0a` (`modules/fps/FpsModule.cpp`): when `fps.ecsAuthority=true`, `syncDynamicEntities` commits the **full** Projectile, Loot and Vehicle value into their matching ECS runtime payload component and stores the original deterministic `order`. `hydrateDynamicsFromEcs` reconstructs the legacy contiguous gameplay arrays from ECS runtime components immediately before the FPS tick, sorting by `order` with deterministic ID tie-break. ECS object **membership, values, and order are authoritative between ticks**; vector arrays are compatibility workspaces during the FPS tick.
- On recycled Entity indices the sync removes mismatched runtime component kinds along with former inspector components; stale vanished dynamic objects are destroyed in deterministic source-ID order. On save v9 load, the existing `syncActorEntities/syncDynamicEntities` creates all authoritative payloads again from the deserialized legacy state; SimSnapshot includes the ECS pools.
- Does **not** yet rewrite projectile collision/damage, loot pickup or vehicle simulation algorithms to operate directly on ECS pools; these continue using State arrays in the tick. Does not change default behavior or FPS save format. This is **stage F**, not final complete migration.
- **Verification:** compilation, CI, FPS deterministic checks, save/restore parity pending. Next add direct ECS dynamics mutation+rollback tests, then eliminate gameplay dependency on temporary arrays system by system.

### 2026-11-18 — FPS ECS authority migration: stage G (canonical save/hash interface)
- Commit `8ca50de`: declares `Module::authoritativeStateCopy(Simulation&)` to construct legacy save v9 / checksum state **from authoritative ECS ActorRuntime/ProjectileRuntime/LootRuntime/VehicleRuntime components**, while preserving non-entity round/history/meta data from `State`.
- This commit is declaration only; next action must implement and route `hashState` and `serializeState` through the materialized canonical snapshot under `fps.ecsAuthority`.
- **Verification:** not run. Do not turn the storage option on by default until paired runtime and save/load checks pass.

### 2026-11-18 — FPS ECS authority migration: stage H (canonical hash and save v9)
- Commit `117b4e9` in `modules/fps/FpsModule.cpp`: implemented `authoritativeStateCopy`. In `fps.ecsAuthority`, it reads full `ActorRuntime` by deterministic slot, and builds projectile/loot/vehicle vectors from **ECS runtime component pools** sorted by explicit source order (+ID tie break). Non-entity data (round, rewind history, gear/bot state, match, royale) comes from compatibility `State`.
- `Module::hashState` now hashes the materialized canonical ECS payloads in authority mode, preserving the old wire/save layout and previous checksum algorithm; `Module::serializeState` writes the canonical materialized values in the existing FPS save v9 format, and on read the prior `syncActorEntities/syncDynamicEntities` creates the ECS payloads.
- Important: direct mutation of an authoritative ECS entity between ticks now affects hash/save immediately, whereas the temporary legacy working vector is ignored for entity payloads. Direct mutation through State vectors in `fps.ecsAuthority` mode is no longer a reliable write path: use ECS runtime components or move the algorithm into an ECS system.
- FPS physics/combat/AI still access ordered `State` working vectors *during* the tick; full elimination of these buffers remains. This commits a testable ECS canonical boundary rather than pretending full FPS gameplay ECS rewrite is done.
- **Verification:** no native build or CI PASS yet. Next stage: expand parity tests for dynamic objects and direct ECS edits, then migrate a selected gameplay subsystem.

### 2026-11-18 — FPS ECS authority migration: stage I (legacy load ordering fix)
- Commit `381b297`: in `Module::serializeState`, materialize canonical Actor/Dynamic ECS payloads **only when Archive is writing**. When reading save v9, `Simulation::deserialize` restores the saved allocator *before* calling module deserializer; a pre-ECS save may contain zero actor entities at that point. Read from the old State workspace first, validate save, then rebuild ECS identities and authoritative runtime payloads via existing sync functions.
- This prevents falsely rejecting legacy saves due to missing authoritative ECS entities during deserialization. `checksum()` following successful read still reads canonical ECS payloads, matching the old legacy v9 hash format (storage-only flag excluded from contentHash).
- **Verification:** not compiled/run; specifically run pre-ECS allocator restore, FPS legacy save validation, and authoritative-mode cross-snapshot tests.

### 2026-11-18 — FPS ECS authority migration: stage J (dynamic parity and lifecycle)
- Commit `f7325bb` adds `ecsDynamicAuthorityParity` to `tests/fps/tests.cpp`, comparing legacy State objects with opt-in ECS-authoritative Projectile/Loot/Vehicle runtime payloads. The test creates stable ECS entities and indexed source IDs, checks canonical checksum immediately (before hydration), steps 8 ticks, directly edits authoritative ECS LootRuntime with immediate hash visibility, saves/reloads v9, deletes an ECS LootRuntime entity and checks the legacy workspace reflects that membership change, and rolls back a pre-removal SimSnapshot.
- Commit `345d809` replaces an invalid full `std::vector<Projectile>` equality check (Projectile has no `operator==`) with size and ID verification; other payload equality is enforced by full deterministic checksum already checked every tick.
- This is an **unverified test**, not proof that FPS gameplay now directly uses ECS in combat systems. Actor and dynamic ECS payloads are authoritative at tick barriers; gameplay uses State vectors as a compatibility workspace until later direct ECS system migration.
- Next: validate compilation/tests, add Inspector coverage for full runtime components, then migrate one combat/AI system at a time.

### 2026-11-18 — FPS ECS authority migration: stage K (Inspector runtime fields)
- Commit `a160612` in `apps/editor/EditorFps.cpp`: shared FPS Runtime snapshot now includes actual `ActorRuntime` fields (kills, shots, hits, weapon index, reload/fire timers, input sequence, vehicle, etc.) alongside legacy ActorIdentity/Transform/Vitals projections; projectile/loot/vehicle runtime payload component cards show source order and additional authoritative fields if present. The existing EditorRuntimeEntityProvider/Hierarchy stable Entity IDs and selection logic remain unchanged.
- UI is read-only; mutations must go through ECS-authoritative gameplay APIs rather than direct editor-side mutation of simulation memory. For projects without `fps.ecsAuthority`, full runtime components are absent and existing mirror panels continue to work.
- Verification: source code committed, no Metal editor visual test or native build yet.

### 2026-11-18 — FPS ECS authority migration: stage L (clear ownership contract)
- Commits `a3772e9` (`modules/fps/FpsModule.h`) and `cf37e04` (`apps/editor/EditorRuntimeEntityProvider.h`) correct stale comments that said `State::actors` is *always* the gameplay authority. With `fps.ecsAuthority=false` (default), that remains true. With `fps.ecsAuthority=true`, the full `ActorRuntime` / `ProjectileRuntime` / `LootRuntime` / `VehicleRuntime` components are authoritative **between ticks**, and `State` vectors are temporary ordered gameplay workspaces.
- Stable ECS IDs and lightweight ActorEntity* / dynamic *Entity projections remain dedicated to Editor/Inspector. Legacy save/network stays v9; `authoritativeStateCopy` marshals ECS data into the old v9 wire/save layout.
- Key next step: remove remaining `State` gameplay workspace dependency one subsystem at a time; avoid parallelizing actor interactions/damage without canonical gather/apply barriers. No native build/CI PASS reported for recent commits.

### 2026-11-18 — FPS ECS authority migration: stage M (direct motor declaration)
- Commit `4dc4a2f` declares `Module::moveActorEcs(Simulation&,Actor&,const Input&)` in `FpsModule.h`. It is a dedicated entry to move actor motor calculation **onto the actual writable ECS ActorRuntime component during a tick**, then write only changed motor fields back into ordered State workspace for cross-actor combat and networking compatibility.
- Declaration only in this commit. Implementation and collision/determinism tests must follow. No change to default FPS mode.

### 2026-11-18 — FPS ECS authority migration: stage N (first direct gameplay system)
- Commit `f9a254e` (`modules/fps/FpsModule.cpp`) implements `Module::moveActorEcs`: in `fps.ecsAuthority=true` and when the speculative parallel-motor shortcut is not taking over, the actual fixed-point `moveActor` algorithm runs on **writable `ActorRuntime::value` in the ECS pool**, not on `State::actors[i]`. Only motor input/output state (position, velocity, yaw/pitch, health/vehicle/stance flags) crosses the bridge; no expensive whole Actor bag/melee copies per move. The result's motor fields are reflected into `State::actors[i]` for later canonical-order combat, target selection, network projection and old save v9 algorithms.
- Compatibility: OFF mode continues calling legacy `moveActor` directly. With experimental parallel motor ON, precomputed actor results are still applied through the old buffer and committed to ECS at the end-of-tick; the direct motor method is used on the safe sequential fallback. This is **a real migrated gameplay subsystem**, but shooting/AI/loot/projectile/vehicle logic still use workspace arrays.
- Existing `ecsActorAuthorityParity` drives movement/jump/sprint and compares complete 160-tick authoritative checksum histories; full native compilation and sanitizer checks remain pending. Preserve original actor iteration and combat ordering.

### 2026-11-18 — FPS ECS authority migration: stage O (direct combat damage and scoring)
- Commit `e59b298` (`modules/fps/FpsSystems.cpp`): `Module::damage` now calculates authoritative damage directly into `ActorRuntime::value` for the target and (if distinct) attacker when `fps.ecsAuthority=true`. Pre-damage actor workspaces are copied into ECS to preserve same-tick interaction order, and results are immediately reflected back into legacy State actor slots for subsequent targeting, knockdown/team queries, FPS snapshots and net prediction.
- Preserves existing damage sequence: friendly fire, gear/armor absorption, action cancel, lastAttacker/weapon, hit counter, knockdown, death/kill/money, drops and SimEvents. Lambda wrapping preserves early-return cases while ensuring authoritative ECS results are committed to the temporary workspace.
- Default `ecsAuthority=false` still operates exclusively on legacy State. The centralized damage method is one **direct gameplay** ECS subsystem, alongside direct actor motor and command validation; other FPS systems still use tick workspace arrays.
- Verification: compile/firing parity tests and ASan not yet run. Next add a two-team damage/kill regression under both authority modes, check same-tick event ordering, and update read-only editor schema as needed.

### 2026-11-18 — Universal mechanics audit: build preflight
- User requires proving that RTS/FPS mechanics are reusable by **any genre**, not merely present in the same repository. Universal ECS primitives and pure policy libraries exist, but RTS/FPS module-specific business logic still has coupling; comprehensive genre-agnostic integration is not yet complete.
- Commit `0b8ad00` fixed three invalid `//` comments in root `CMakeLists.txt` near ECS test/RTS option blocks. CMake uses `#` for comments, so previous imported PR #7 configuration could fail to parse before tests could run. This is a syntax-level fix; **native CMake configuration not yet executed**.
- Next: add explicit independent cross-genre ECS policy consumer tests + reusable adapter API; audit genre-bound `RtsModule&`/ `fps::Actor` dependencies; report remaining blockers without claiming complete universality.

### 2026-11-18 — FPS ECS authority migration: stage P (direct damage parity regression)
- Commit `2413b7a` (`tests/fps/tests.cpp`) adds `ecsDamageAuthorityParity`. Creates identical two-team deathmatch simulations in default and `fps.ecsAuthority=true` mode, sends 30 ticks of deterministic fire, compares `Simulation::checksum` after each tick, target health, attacker hits/kills, and the complete `ActorRuntime::value` of both participants against the compatibility workspace. Explicitly verifies the victim dies and the attacker is credited with a kill.
- This tests the newly migrated direct ECS `Module::damage` and earlier direct ECS motor/command paths together, while retaining canonical save v9/network semantics. No tested performance advantage is claimed; legacy API compatibility remains.
- **Verification status:** code pushed but still uncompiled on Mac Studio; run FPS CTest, FPS network transport, and ECS normal/ASan suites before merging or changing the default flag.

### 2026-11-18 — FPS ECS authority migration: stage Q (native direct ECS spawn)
- Commit `58808d8` (`modules/fps/FpsModule.cpp`): `hydrateDynamicsFromEcs` now enumerates all live `ProjectileRuntime`, `LootRuntime`, and `VehicleRuntime` components at the authoritative pre-tick barrier, registers each typed source ID in `DynamicEntityIndex` (kind+ID -> real Entity), and rejects zero IDs, duplicate IDs per kind, or conflicting live mappings.
- Native projects can now create an ECS Entity and `world.add<LootRuntime>/add<ProjectileRuntime>/add<VehicleRuntime>` directly **without writing legacy State vectors or manually populating index.byKey**. Hydration reconstructs deterministic ordered workspaces; existing post-tick `syncDynamicEntities` populates lightweight Inspector/Hierarchy component projections on the **same Entity**, not a duplicate.
- Important: new ECS dynamic mutations are applied at the next tick barrier, not mid-tick. Authoritative direct ECS creation in projects should be done outside concurrent job waves. Existing legacy default mode unchanged.
- Tests must verify native runtime spawn without pre-registered index and duplicate-ID failure. Native compile/CI not yet confirmed.

### 2026-11-18 — Cross-genre audit: stage 1, ECS policy adapter
- Commit `cb9a43e`: `libraries/include/aiverselib/genre/EcsMechanicSystems.h` introduces genre-independent `eachEcsMechanic<Component>(World&,policy)` and `addEcsMechanicSystem<Component>(Simulation&,name,policy)`. Game code chooses an arbitrary ECS component type, binds its existing policy, and gets deterministic dense-order iteration and COW write preparation. No dependency on `RtsModule` or `fps::Module`.
- This is **an adapter, not universalization of all FPS/RTS functions**. Any mechanic requiring `RtsModule&`, `fps::Actor`, global RNG, actor ownership, collision or format-specific state must be extracted/refactored separately. No native compilation yet.
- Next: compile and run the same pure policies against two different GameModules via this common adapter; add coverage and an explicit capability matrix for each mechanic.

### 2026-11-18 — FPS ECS authority migration: stage R (direct ECS spawn regression)
- Commit `201b75e` modifies `ecsDynamicAuthorityParity` to create ProjectileRuntime, LootRuntime and VehicleRuntime directly on fresh ECS Entity handles **without** modifying `DynamicEntityIndex`; verifies the index initially has zero entries, and after one FPS tick that it contains the exact three original Entity handles by typed source ID. The remaining test still checks state checksums, save/restore, deletion and snapshot rollback.
- This specifically guards against an accidentally duplicated ECS entity for a native-created object, which would break Inspector/Hierarchy identity and deterministic object ownership.
- **Verification:** test code committed; no native compile/CI PASS yet. Next add error-path duplicate-ID coverage and audit save v9 old allocator behavior.

### 2026-11-18 — FPS ECS authority migration: stage S (save-crossmode and duplicate-ID rejection)
- Commit `2e8de0c` adds two negative/compatibility regressions to `tests/fps/tests.cpp`: `ecsDuplicateDynamicIdRejected` creates two LootRuntime ECS entities with identical typed ID and asserts `sim.step` rejects the ambiguity before tick advances; `ecsActorAuthorityParity` now writes legacy FPS save v9 in default storage mode, loads it into a fresh `fps.ecsAuthority=true` simulation, and compares checksums.
- This explicitly covers the storage-only toggle's unchanged `contentHash_`, save version and Entity rehydration semantics. The replay contract is still ordered by original slot / dynamic object's explicit source order.
- Tests remain **unverified by native compilation/CI**. If they fail, fix within PR #7 and document exact failure/commit before considering main merge.

### 2026-11-18 — Cross-genre audit: stage 2, executable contract
- Commit `6dc7a89`: added `tests/ecs_cross_genre_mechanics_test.cpp`. It runs **identical** reusable `stepMotion`, `stepRecoil`, `stepCapture` policies through `addEcsMechanicSystem<Component>` in two independent GameModule types with distinct ECS components (`ShooterCharacter`, `StrategySoldier`). Checks matching deterministic checksums, rollback replay, and optional page COW state.
- Scope: this proves generic C++ policies can be bound by independently authored game modules **once compiled and executed**. It does **not** prove that every existing RTS/FPS gameplay function can be moved to a new genre unchanged; those still refer to `RtsModule&`, `fps::Actor`, domain-specific state and configuration.
- New test is not yet wired into build/test runners; no native compilation or passing CI observed.

### 2026-11-18 — Cross-genre audit: stage 3, public header contract
- Commit `561bafa`: cross-genre test imports `<aiverselib/genre/EcsMechanicSystems.h>`, `Locomotion.h`, `Weapons.h`, `Match.h` via published `AIVERSE::libraries` include conventions instead of repository-internal relative include paths. This makes the test a useful native-project consumer contract.
- Build and execution still pending. Next action: register the test in root CMake and standalone ECS runner.

### 2026-11-18 — Cross-genre audit: stage 4, CMake consumer target
- Commit `620920b`: root `CMakeLists.txt` now adds `aiverse_ecs_cross_genre_mechanics_test` after `add_subdirectory(libraries)`. Links only `aiverse_engine` and public `AIVERSE::libraries`, deliberately **not** `aiverse_fps` or `aiverse_game`. Registered as `ecs_cross_genre_mechanics_test` in CTest. This verifies that gameplay policies and ECS adapter do not require either genre module.
- Build/CTest has not been run on native toolchain. The absence of genre linkage is a source/CMake check, not proof of runtime correctness until CI passes.

### 2026-11-18 — FPS ECS authority migration: stage T (agent instructions)
- Commit `0c57aff` updates `AGENTS.md` with current integrated PR #7 branch, the staged `fps.ecsAuthority` source-of-truth contract, already-migrated direct ECS systems (command, motor, damage), and explicit acknowledgement that unconverted AI/projectiles/vehicles/loot still use `State` as a tick workspace.
- It instructs any successor agent to update **this exact handoff after every implementation commit**, preserve save v9/net compatibility and deterministic side-effect order, and avoid replacing merged PR7 Editor/FPS code with stale PR8 file copies.
- New code remains unverified by a completed native CI/Metal/FPS test run. Future tasks: compile, inspect any actual failures, migrate remaining FPS systems directly to ECS and eventually remove temporary ordered workspace vectors.

### 2026-11-18 — Cross-genre audit: stage 5, standalone regression runner
- Commit `835c24b`: `tests/run_ecs_experiment_suite.sh` runs a public-include cross-genre consumer alongside existing ECS correctness checks; compiles with `-Ilibraries/include` and Simulation sources, independent of RTS/FPS link libraries.
- **Verification:** not yet executed. Next: create an explicit reuse matrix/definition of done; review GitHub CI outcomes and any build errors. Run `bash tests/run_ecs_experiment_suite.sh` and `ctest -R '^ecs_cross_genre_mechanics_test$'` once a native toolchain is available.

### 2026-11-18 — Cross-genre mechanic contract and PR #17 integration

- Commit `5737403` added a mandatory **genre-independent ECS Components + Systems architecture contract** to `AGENTS.md`. Every mechanic/library must expose reusable ECS component definitions, configurable independently scheduled systems, explicit read/write sets, deterministic order/commands, persistence/rollback, native public C++ APIs and corresponding tests/AI skill/docs. Genre modules only adapt input/configuration/presentation; they must not duplicate shared algorithms.
- Examples: RTS and FPS ballistics/rockets use one reusable trajectory, collision, hit/penetration/splash/damage implementation with different configurations, not separate projectile solvers. Apply the same requirement to health, inventory, AI, steering, vehicles, economy and networking.
- User requested importing **PR #17** (`fix/pcss-penumbra-sampling-20261117`, temporal PCSS/HDR TAA) into `feature/universal-entity-inspector-fps`/PR #7 without overwriting existing editor/FPS changes. The PR has five renderer source-file patches and one new render validation document. Source-level dry-run of all five patches against PR #7 showed **unique exact context matches**; integration is the next action.
- **Verification:** policy/docs only; shaders/macOS build and real FPS runtime integration tests remain unverified. Keep PR #7 open and do not merge main.

### 2026-11-18 — Imported PR #17 temporal PCSS into this branch
- Explicit user request: bring GitHub PR #17 (`fix/pcss-penumbra-sampling-20261117`, head `e4f23850b0c123e8004232e49438143a5581d5f4`) into universal ECS/editor branch `feature/universal-entity-inspector-fps` (PR #7).
- **Merge commit `0a6abe5f114851ed6bf27b4a020ae9953b75ff95`** has the current PR #7 head as first parent and PR #17 head as second parent. Applied all six PR #17 file changes on top of the newer PR #7 versions using exact-context patching, without replacing existing renderer changes: `Renderer3D.cpp/.h`, `ScreenLighting.h`, `ScreenLightingShaders.h`, `Shaders.h`, `tests/render/temporal-pcss.md`.
- Changes: sample rotation for PCSS is gated by a live HDR temporal resolve; GLSL/MSL sample scrambling matches; history validation compares previous-camera receiver depth and bounds edge ghosting. The shader backend builds/visual checks **have NOT been performed**.
- This is a merge of PR #17 **into PR #7 feature branch**, NOT into `main`. Preserve the PCSS test matrix (`tests/render/temporal-pcss.md`) and run Metal/GLES + HDR/without-HDR captures and performance checks before merging PR #7 into main.
- User also ordered a stronger, genre-independent mechanic architecture. The required architecture will be documented in `AGENTS.md`, `docs/ecs-mechanic-composition.md` and `skills/workflow/aiverse-new-mechanic/SKILL.md`. These rules supersede old genre-specific new-mechanic instructions.

### 2026-11-18 — Architecture rule enforcement + PR #17 confirmed in PR #7

- `5737403` — `AGENTS.md` now mandates **genre-neutral ECS component contracts + independently configurable systems** for every mechanic. RTS/FPS must share ballistic/trajectory, collision, impact, penetration, splash and damage systems; apply this composition contract to AI, locomotion, vehicles, physics, economy, inventory and meta. Native C++ public API, tests, skill and persistence are mandatory.
- `985ae46` documented the PR #17 import plan. Concurrent agent completed the actual merge as **`0a6abe5f114851ed6bf27b4a020ae9953b75ff95`**; all six PCSS/TAA files were patched onto PR #7 while retaining its newer renderer tree, and PR #17 original head `e4f23850` is recorded as second parent. The assistant independently built an equivalent merge tree, but did **not** force an extra merge after detecting the destination had already advanced.
- GitHub compare `fix/pcss-penumbra-sampling-20261117...feature/universal-entity-inspector-fps` reported **ahead, behind=0**. PR #17 is included in the PR #7 branch by Git ancestry. This is **not** a merge to `main`.
- `7e75be8` — updated **`skills/workflow/aiverse-new-mechanic/SKILL.md`**. Removed old RTS-only implementation mandates (`RtsData.cpp`, `Rts<Mechanic>.cpp`) and replaced them with shared ECS Components+Systems, optional genre adapters, native-project linkage, deterministic serialization/rollback, cross-genre fixtures and mandatory per-step handoff.
- **Validation remains unverified:** latest Metal/GLES PCSS shader compile, shadow captures, FPS/RTS checksums and sanitizer tests have not been run in this session. Keep PCSS temporal sampling TAA-gated and ECS migration opt-in.
- **Next agent:** use the shared-library registry first; do not create duplicate RTS/FPS projectile/damage implementations. Follow the updated skill and `AGENTS.md` for all new game mechanics.

### 2026-11-18 — Universal ECS projectile + damage resolution, stage A
- Commit `3dfe73e`: added `libraries/include/aiverselib/genre/EcsDamageSystems.h`, a genre-neutral **Components + Systems** damage layer. Components: `HitPointsComponent`, `ProjectileDamageComponent`, `ColliderEntityIndex` (collision ID→generation-safe Entity), `DamageQueue`; event `DamageRequest`; applied report `DamageApplied`.
- `enqueueProjectileDamage` translates a `ProjectileImpactEvent` from the existing common `addProjectileFlightSystem` into an ordered damage request. `addDamageResolutionSystem` consumes requests at a later fixed system barrier in insertion order; callbacks can emit events but not mutate projectile traversal or structural state. Default policy applies direct damage; custom policies can add armor/factions/resistance without modifying the shared system.
- **No RTS/FPS includes**, public `AIVERSE::libraries` header. Supports any genre using ECS projectile/health components. This creates the shared capability; genre-specific implementations have NOT yet been ported and no tests/build have passed.
- Next: executable shooter/strategy host test using the SAME flight/damage systems, rollback/COW checks, CMake and standalone runner wiring, then measure parity before adapting actual RTS/FPS gameplay.

### 2026-11-18 — Universal ECS projectile + damage resolution, stage B
- Commit `d58435d`: `tests/ecs_cross_genre_projectile_test.cpp` defines two unrelated native `GameModule` types using `ShooterTag` and `ArtilleryTag`. Both install identical shared `addProjectileFlightSystem` + `addDamageResolutionSystem` to simulate an ECS projectile against the same collision AABB, convert impact to `DamageQueue`, and apply 35 damage to a `HitPointsComponent` in a deterministic order. Checks impact count, projectile lifetime/despawn, checksum parity, and full `SimSnapshot` rollback/replay with and without COW.
- This test deliberately links no FPS or RTS gameplay modules: it proves one mechanic combination can work for different genres once compiled.
- **Status:** source committed, not compiled or run yet. Next action: CMake/standalone ECS test runner integration and native build verification.

### 2026-11-18 — User-mandated universal ECS mechanic contract (all genres)
- User explicitly requires **every reusable gameplay mechanic** to be built from (1) persistent ECS **components**, (2) composable ECS **systems** implementing feature stages. Genre is a project preset, never a gate: a vehicle may shoot, RTS objects may use first-person weapons, inventory and AI systems mix freely.
- Commit `304e847` (`AGENTS.md`): high-priority normative rules require data-only ECS components, independent systems with explicit read/write/resource dependencies, pure policies, deterministic ordered effects, public native-project API, composability and cross-genre tests. Disallows duplicating trajectory/damage formulas in RTS/FPS and treats RtsModule/fps::Module code as adapters rather than universal APIs.
- Commit `76d7a69` (`docs/ecs-mechanic-composition.md`): detailed architecture, per-mechanic deliverables/acceptance gates and concrete launch -> flight -> collision -> damage -> effects/despawn design, plus candid migration status.
- Commit `035aae4` (`skills/workflow/aiverse-new-mechanic/SKILL.md`): rewrote older RTS-only instructions. All future features must follow shared ECS component + system + policy + host-adapter architecture.
- Commit `bc6ff19` (`skills/gameplay/aiverse-weapon-policies/SKILL.md`) requires shared ECS weapon/projectile systems for all genres. Commit `3cd63b6` created `skills/gameplay/aiverse-ecs-projectile-systems/SKILL.md` for the new shared flight API.
- Commit `8f36243` (`libraries/include/aiverselib/genre/EcsProjectileSystems.h`) adds reusable `ProjectileFlightComponent`, `BallisticFlightPolicy`, `ProjectileImpactEvent`, `addProjectileFlightSystem`: deterministic ECS ballistic updates, read-only collision source provided by ANY game, ordered impact sink after component iteration, deferred despawn. This is a reusable **flight stage**, not complete combat/damage/AI migration.
- Commit `68d92e0` (`tests/ecs_projectile_composition_test.cpp`) exercises the same flight ECS system in unrelated Shooter and Strategy GameModule hosts, with stable ordered impacts and deep-vs-page-COW rollback checksum comparison. Commits `bd94083`, `786941f` register it in CTest and the standalone ECS suite, without linking aiverse_game or aiverse_fps.
- Commit `741c292` registers package `ecs-projectile-systems` and its public `AIVERSE::lib_ecs_projectile_systems` target in `libraries/catalog.json`. Commit `41d6fb3` updates the composition guide to point to actual implementations.
- **Native build and shader test results are NOT confirmed.** These are code, test and guideline changes, NOT proof every legacy RTS/FPS mechanic is universal. Outstanding: replace genre-specific projectile/collision/damage logic with shared systems, extract RTS-only NativeModule hooks, FPS Actor State workspace migration, generic Inspector/persistence integration, and pass cross-genre/native/ASan/TSan tests.

### PR #17 integration proof (same feature branch)
- User requested import of PR #17 (`fix/pcss-penumbra-sampling-20261117`). Commit `0a6abe5f114851ed6bf27b4a020ae9953b75ff95` has PR #17 head `e4f23850b0c123e8004232e49438143a5581d5f4` as its **second parent**, previous PR #7 head as first. Imported five render source/shader changes through exact-context patching and added `tests/render/temporal-pcss.md`, preserving unrelated PR #7 renderer changes.
- Verified Git comparison: PR #7 branch is **ahead** of PR #17, **behind = 0**. This is an ancestry merge into PR #7, NOT a merge to main and does not necessarily close PR #17.
- Still unverified: actual Metal/GLES shader compilation, HDR TAA render captures, no-TAA stability, GPU performance. See `tests/render/temporal-pcss.md`.

### Immediate next actions
1. Run `bash tests/run_ecs_experiment_suite.sh` and CTest for `ecs_projectile_composition_test`, `ecs_cross_genre_mechanics_test`, FPS/RTS parity and page-COW rollback. Fix compiler or runtime failures without changing default genre behavior.
2. Run `python3 tools/libraries/catalog.py check` and public native consumer tests; validate new library and skill/catalog packaging.
3. Compile renderer/shaders on macOS Metal and GLES and inspect PCSS penumbra ghosting with/without HDR TAA.
4. Incrementally migrate RTS + FPS launch, projectile, hit/damage and weapon states to the same ECS systems. Document which exact adapters remain; do not claim all mechanics universal until cross-genre acceptance gates pass.
5. Update this handoff after every substantive code change (exact commit, result, next owner).


### 2026-11-18 — Universal ECS projectile + damage resolution, stage C
- Commit `c957986`: registered `aiverse_ecs_cross_genre_projectile_test` in root `CMakeLists.txt` (after `modules/fps` creates `aiverse_world`), with public `AIVERSE::libraries`, `aiverse_engine` and **collision-only** `aiverse_world` linkage. No `aiverse_fps` or `aiverse_game`. This complements the existing flight-only `ecs_projectile_composition_test` with health/damage queue coverage.
- **Verification:** no compiler/CTest result has yet been obtained. Next register standalone suite and add an explicit documented mechanics-composition recipe.

### 2026-11-18 — Universal ECS projectile + damage resolution, stage D
- Commit `c2e57ee`: added `ecs-cross-genre-projectile` to `tests/run_ecs_experiment_suite.sh` with public headers, `SpatialWorld/TriangleCollision`, `Simulation`, deterministic Fixed/Json source compilation. Keeps existing flight-only ECS composition test.
- Still not verified by execution. Next: inspect shell continuation formatting and add a new agent-facing component/system composition recipe.

### 2026-11-18 — Universal ECS projectile + damage resolution, stage E
- Commit `008f01c`: created `skills/gameplay/aiverse-ecs-impact-damage/SKILL.md` for agent reuse. It specifies data-only health/damage/collider-index/queue components, flight → impact staging → ordered damage resolve → optional project-specific policy, deterministic ownership and save/rollback, native C++ usage, tests, and explicit scope limits.
- This skill **does not** imply the existing RTS/FPS combat modules have been migrated; they still require adapters and parity tests.
- Next: add catalog entry to `libraries/catalog.json`, update normative composition docs and run the tests.

### 2026-11-18 — Universal ECS projectile + damage resolution, stage F
- Commit `259258b`: registered public package `ecs-impact-damage` in `libraries/catalog.json`, with target `AIVERSE::lib_ecs_impact_damage`, source `EcsDamageSystems.h`, workflow skill, CTest fixture and dependency on existing `ecs-projectile-systems`. The library catalog's generated `AIVERSE::lib_*` targets make it accessible to native C++ game modules without linking FPS/RTS combat modules.
- JSON structure was parsed and validated before commit (119 packages; package at end); **not** tested with the catalog CLI or a native CMake configure yet.
- Next: update normative composition guide/skill and verify build/results; do not advertise automatic integration into legacy `fps::Module` / `RtsModule`.

### 2026-11-18 — Universal ECS projectile + damage resolution, stage G
- Commit `95025b3`: updated normative `docs/ecs-mechanic-composition.md` with the actual `EcsDamageSystems.h` component schema, registration recipe, ordered flight→impact queue→damage resolution→deferred despawn barrier, public target `AIVERSE::lib_ecs_impact_damage`, and shooter/artillery rollback fixture.
- Explicit limits: default only implements direct hits; armor, AoE, penetration and status require new composable systems/policies. Existing RTS/FPS damage systems have **not** been migrated or equivalence-tested. No native compilation/visual checks passed yet.

### 2026-11-18 — Universal ECS projectile + damage resolution, stage H
- Commit `0f3c4d8`: replaced `assert`-only checks in `tests/ecs_cross_genre_projectile_test.cpp` with throwing `require` runtime assertions. C++ `NDEBUG` must not disable collider setup or make Release CTest silently pass if impact, damage, rollback, COW or cross-genre checks fail.
- **Verification:** source-level inspection only; native test builds still pending. Keep both CTest and standalone suite as required gates.

### 2026-11-18 — Universal ECS weapon launch, stage A
- Commit `3e146a3`: added shared `libraries/include/aiverselib/genre/EcsWeaponLaunchSystems.h`. Data-only ECS components: `WeaponParametersComponent` (Fixed speed/gravity/radius, lifetime, cooldown, damage), `WeaponStateComponent` (ammo/nextFireTick), `FireIntentComponent` (origin/direction/one-shot request), `ProjectileSpawnQueue` and `ProjectileSpawnCommand`.
- `addWeaponLaunchSystem` validates settings, cooldown/ammo, consumes requests in stable `WeaponState` dense order and emits spawn commands without structural changes during iteration. `addProjectileSpawnSystem` consumes them in a separate deterministic barrier and creates `ProjectileFlightComponent`/`ProjectileDamageComponent` pairs for the shared flight→impact→damage chain. Host genre adapters supply inputs and configuration; no `RtsModule` or `fps::Module` dependencies.
- **Status:** initial implementation only, not compiled/run or integrated with legacy RTS/FPS gameplay. Next: full cross-genre weapon→spawn→flight→damage test, CMake/standalone registration, catalog, skill and documentation. Keep existing host combat untouched until equivalence is proven.

### 2026-11-18 — Universal ECS weapon launch, stage B (full pipeline proof)
- Commit `95c4a08`: `tests/ecs_cross_genre_weapon_pipeline_test.cpp` installs identical **WeaponLaunch → ProjectileSpawn → ProjectileFlight → ImpactQueue → DamageResolution** ECS systems into unrelated ShooterTag/ArtilleryTag `GameModule` hosts. Checks one request consumes exactly one ammo, stable generated shot ID, 35 damage, projectile despawn, identical checksums and snapshot rollback with/without COW.
- The test depends on no FPS/RTS game module header or linkage, only `AIVERSE::libraries` + shared `aiverse_world` collision. It complements existing flight-only and flight-to-damage cross-genre tests.
- **Status:** new test not yet built/executed. Next wire CMake/standalone runner, register public package and skill, and preserve deterministic event barriers.

### 2026-11-18 — Universal ECS weapon launch, stage C (CMake)
- Commit `282a299`: registers `aiverse_ecs_cross_genre_weapon_pipeline_test` after `modules/fps` creates `aiverse_world`, linking only `aiverse_engine`, `aiverse_world` and `AIVERSE::libraries` (not `aiverse_fps` or `aiverse_game`). CTest case `ecs_cross_genre_weapon_pipeline_test` covers full shared shooting and damage composition.
- Not compiled on Mac/Linux yet; CI must confirm.

### 2026-11-18 — Universal ECS weapon launch, stage D (standalone suite)
- Commit `7e3fe6d`: `tests/run_ecs_experiment_suite.sh` now compiles and runs `ecs-cross-genre-weapon-pipeline` with public library includes, shared SpatialWorld/TriangleCollision and simulation sources. It checks the full weapon→flight→damage chain without linking any genre gameplay module, including normal/sanitized build modes.
- The source and shell continuation lines were assembled explicitly. Runtime tests remain unverified; check `bash -n` and execute suite on native toolchain.

### 2026-11-18 — Universal ECS weapon launch, stage E (agent skill)
- Commit `ac54b13`: created `skills/gameplay/aiverse-ecs-weapon-launch/SKILL.md`. Agents must assemble shared WeaponLaunch, Spawn, ProjectileFlight and DamageResolution components/systems, independent of RTS/FPS genre; state/queues/shot IDs in ECS, canonical side effects and COW/replay proof. New capabilities such as reload, guided rockets and spread are **separate systems**, not genre branches.
- Skill explicitly warns that legacy RTS/FPS combat modules still need stepwise migration and old save/network parity checks.
- Next: catalog public native target and update normative mechanics specification.

### 2026-11-18 — Universal ECS weapon launch, stage F (public native package)
- Commit `e1d286f`: registered `ecs-weapon-launch` package in `libraries/catalog.json`, exported as `AIVERSE::lib_ecs_weapon_launch`. It depends on `ecs-impact-damage` (and transitively on `ecs-projectile-systems` and collision), with public header/skill/test dependencies.
- Catalog parsed and validated as JSON (120 packages, new package last), but native `tools/libraries/catalog.py check` and CMake test **have not been run**. Native projects can request this package without importing FPS/RTS gameplay modules.
- Next: update composition spec + agent rules and inspect CI results.

### 2026-11-18 — Universal ECS weapon launch, stage G (normative composition doc)
- Commit `c458af0`: `docs/ecs-mechanic-composition.md` documents the complete public ECS `WeaponLaunch → ProjectileSpawn → Flight → Damage` component/system chain, order/barriers and native registration example. A single shared implementation works with any shooting Entity regardless of genre, with `AIVERSE::lib_ecs_weapon_launch` and `ecs_cross_genre_weapon_pipeline_test` as proof points.
- **Explicit non-completeness:** published FPS/RTS gameplay still has original combat implementations. The generic pipeline is reusable in independent hosts but requires migration of existing engines, tuning and legacy save/network parity tests. No native test success reported yet.

### 2026-11-18 — Universal ECS weapon launch, stage H (agent policy)
- Commit `8421961`: `AGENTS.md` explicitly names the required shared shooting packages `AIVERSE::lib_ecs_weapon_launch`, `ecs-projectile-systems` and `ecs-impact-damage`. Agents must reuse the same ECS projectile trajectory, collision and damage stages for FPS, RTS, vehicle turrets, RPG spells, etc., and extend systems through composable components/policies. Do not create new genre-specific duplicate solvers.
- The required cross-genre test cases are listed; published RTS/FPS save/rollback and real-game checksum migration remains a separate, unverified task.

### 2026-11-18 — Universal ECS weapon launch, stage I (skill cross-links)
- Commit `2363b7d`: updated the existing `skills/gameplay/aiverse-ecs-projectile-systems/SKILL.md` to direct agents to `aiverse-ecs-weapon-launch` and `aiverse-ecs-impact-damage`, the complete public header chain and three cross-genre regression tests. This prevents a future agent from implementing duplicate RTS/FPS weapon or damage stages when reading the older flight-only skill.
- Work in this checkpoint is limited to independent reusable packages and tests. **Existing** `RtsModule` and `fps::Module` shooting/AI/netcode remain to be migrated, one stage at a time with real checksum and legacy save parity checks. CI not confirmed green.


### 2026-11-18 — Shared weapon dry-fire regression checkpoint
- Commits `51d53d4553c5034f2a124b5511f107b97d377df2` and `a302652200d196b69b0f4057ae4071a52aa5653d`: extended `tests/ecs_cross_genre_weapon_pipeline_test.cpp` to request another shot after ammo reaches zero and after the initial 8-tick snapshot replay checksum parity checkpoint. It checks request consumption, unchanged ammo, unchanged nextShotId and empty pending spawn queue for both shooter and artillery hosts, including COW runs.
- The second commit places the dry-fire scenario **after** the original replay checksum assertion to avoid comparing different tick counts.
- Verification: GitHub source commits only; native compile, CTest, sanitizer and real FPS/RTS gameplay integration **not executed**. Next: compile and run the suite, address failures, then migrate real FPS/RTS launch paths to the shared weapon systems one at a time with save/network parity.


### 2026-11-18 — Agent continuity protocol (user instruction)
- User requires a durable handoff entry for **every completed implementation step**, not only at task completion. Record exact implementation SHA, touched paths, observable behavior, tests actually executed and their outcomes, limitations, and the next action. Commit this handoff entry separately immediately after the implementation commit.
- Keep unverified code changes explicitly marked as unverified; do not claim successful native build, sanitizer, Metal rendering or FPS/RTS parity without evidence.
- Current branch: `feature/universal-entity-inspector-fps`. Most recent code commits: `51d53d4553c5034f2a124b5511f107b97d377df2`, `a302652200d196b69b0f4057ae4071a52aa5653d`; preceding handoff commit: `6a780676d307b6121a76df23082d87088da064c4`.
- Next: run cross-genre weapon pipeline tests in native environment, then integrate actual FPS/RTS adapters in small, parity-checked increments.


### 2026-11-18 — Universal weapon dry-fire rollback replay (next checkpoint)
- Implementation SHA: `3c30e16a89897fb9f77c196dfa796686fb86de0c`.
- Changed: `tests/ecs_cross_genre_weapon_pipeline_test.cpp`. Saves a snapshot after the first shot and successful 8-tick replay, requests a dry fire with empty ammo, checks no projectile ID allocation or pending spawn and that the request is consumed; restores the post-shot snapshot, repeats the request and checks byte-stable simulation checksum. The template runs both ShooterTag and ArtilleryTag, with and without COW.
- Verification: GitHub file update succeeded. **No native compilation, CTest, sanitizer or gameplay runtime verification executed**. Do not mark test passed.
- Next action: run `bash tests/run_ecs_experiment_suite.sh`, targeted CTest and native consumer/catalog checks. Diagnose any failures before connecting real FPS/RTS weapon paths to the shared ECS systems.


### 2026-11-18 — Cross-host dry-fire damage and checksum regression
- Implementation SHA: `c75ff4e4f71da11f0bb8af2e55cb65e4797140c4`.
- Changed: `tests/ecs_cross_genre_weapon_pipeline_test.cpp`. After snapshot restore and dry-fire replay, assert target health remains 65 and damage event summary remains exactly one 35-point hit. Return post-dry-fire checksum from each template host so ShooterTag vs ArtilleryTag and deep vs COW paths compare the **full** scenario, not only the first firing checkpoint. Original first-shot replay checksum equality is still asserted within each host.
- Verification: GitHub source commit confirmed; C++ compilation and runtime test still **not verified**.
- CI observation for prior documentation commit `a07dc450d875f1438d707bf2dd211a463dea9e42`: four GitHub Actions runs were queued (ECS 2.0 experimental checks #318, CI #1168, AIVERSE Studio validation #835, GPU batching focused regression #439), with no conclusions at time of inspection. This is not a passing test report.
- Next: inspect the workflow outcomes and logs; run native suite/CTest, fix failures, and migrate FPS/RTS weapon adapters in isolated parity-checked increments.


### 2026-11-18 — FPS ECS authority post-tick invariant
- Implementation commits: `671c5e9680438bbf9aac4285cbc3d3bf66d7a292` and corrective `ec34d1c0b4735311b605da1f2518c1b8b3655f8f`.
- Changed: `modules/fps/FpsModule.cpp`, the `fps.ecsMirror` system. When `fps.ecsAuthority` is ON, after syncing actors and dynamic objects it now checks that every actor slot has an `ActorRuntime` component whose slot, position, velocity, health, armor, weapon index, ammo and reserve agree with the just-committed legacy tick workspace. A missing/divergent runtime throws `std::logic_error` instead of silently allowing authority drift. No extra checks run with the default OFF gate. The second commit corrects an initially mistaken `Actor::magazine` member to the real `Actor::ammo` member.
- Verification: checked `Actor` declaration in `modules/fps/FpsModule.h` and committed source; **no compilation or runtime test**. The invariant checks selected fields, not every nested actor field; it is a migration guard, **not** completion of direct ECS combat or actor-system migration.
- Next: verify `aiverse_fps_tests` with ecsAuthority both OFF and ON, save v9 and transport checksum parity; expand direct ECS systems and then remove the legacy tick workspace once complete. Check Editor regressions before merge.


### 2026-11-18 — Full actor payload ECS commit invariant
- Implementation SHA: `18104af8d6b2b3ee51ec06444bb03abbeb4af230`.
- Changed: `modules/fps/FpsModule.cpp`. Replaced the selective field checks in `fps.ecsMirror` with `Actor::operator==` against the complete `ActorRuntime::value`. `modules/fps/FpsModule.h` already declares defaulted `Actor::operator==`. This covers inventory, melee state, input, timers, weapon slots, actions and movement alongside the previous vitals, under the opt-in `fps.ecsAuthority` gate. No save/network wire-format changes.
- Verification: GitHub source inspection and commit only; native compile and runtime behavior **not verified**. Potential compile dependency: all nested Actor members must support equality, as required by its defaulted operator. Do not claim actual FPS/RTS migration is complete.
- Next: validate native build, replay and transport; migrate remaining tick algorithms to direct ECS components, while keeping versioned save/network compatibility.


### 2026-11-18 — FPS Editor ECS projection consistency guard
- Implementation SHAs: `75b3673b55ac10d6028130bf570d42277b9e718f`, correction `a1e6dacfc3af1db18dde1241944a4dc2f08c7745`.
- Changed `modules/fps/FpsModule.cpp`: after the full ActorRuntime equality guard, `fps.ecsMirror` now also verifies the derived `ActorEntityIdentity`, `ActorEntityTransform` and `ActorEntityVitals` projections against canonical Actor state (slot/team/life, position/velocity/yaw/pitch, health/armor/ammo/reserve and stance flags). This detects editor-facing stale projections while `fps.ecsAuthority` is enabled. Default OFF path unchanged. Corrective SHA uses actual `ActorEntityTransform::position` field, verified in `modules/fps/FpsModule.h`.
- Verification: source inspection and GitHub commits only. Native C++ compilation, runtime save/replay/network tests and visual editor regression **not run**.
- CI observation: at inspection, commit `858c575d31bc91f5efdf649600263ea9b6ffea22` had four queued Actions workflows (ECS 2.0, CI, Studio, GPU batching); no success reported.
- Next: run native build/CI and FPS ecsAuthority on/off replay tests. Complete actual direct ECS systems and RTS integration; Editor Inspector live ECS writeback, hierarchy/gizmo and rollback remain separate unfinished gates.


### 2026-11-18 — complete ECS feature inventory and migration roadmap (planning-only audit)
- **Planning document commit:** `0141efc431b5d852a633e627bb05b506fe7a43a7`; created `docs/ecs-feature-migration-plan.md`. It inventories all 120 registered packages (78 simulation, 21 presentation, 21 tools), describes real RTS/FPS host ownership and outlines cross-genre component/system migration epics E00–E15, execution stages, tests and agent boundaries. Categorization is a *proposed target architecture*, not a declaration that features are already converted.
- **Machine-readable tracker commit:** `be15ce4e0e6b01713a7fc7a35387e3e0c505b095`; created `docs/ecs-feature-migration-status.csv` with 120 package rows and explicit initial migration statuses. Follow-on agents must update per-feature status only when source and real-host tests support it.
- **Audited sources:** `libraries/catalog.json`, `AGENTS.md`, `docs/ecs-mechanic-composition.md`, `engine/core/ECS.h`, `engine/sim/Simulation.h`, `engine/sim/CommonComponents.h`, `modules/rts/RtsModule.cpp`, `RtsModule.h`, `RtsData.h`, `modules/fps/FpsModule.cpp`, `FpsModule.h`, `FpsSystems.h`, `engine/meta/Meta.h`, existing ECS and editor handoffs.
- **Finding:** independent shared ECS launch/flight/damage libraries exist but production RTS/FPS still use legacy gameplay; FPS ECS full-runtime payloads are canonical only between tick boundaries under an opt-in flag; RTS simulation registration is not itself genre-neutral component composition. Rendering, UI, offline tools and account meta are not simulation ECS migration targets.
- **Verification executed:** successful GitHub connector source reads, `libraries/catalog.json` JSON parse and GitHub file creation responses. **No** native CMake build, CTest, sanitizer, FPS/RTS gameplay, checksum parity, Metal/GLES validation, editor visual test or CI results were run/verified here.
- **Next action:** E00 baseline and golden RTS/FPS replay/event/save snapshots; coordinate ownership with active FPS and ECS agents before any implementation. Start incremental E01/E02/E03 integration, never mass-rewrite or replace live logic without parity proof. Keep this handoff updated after each subsequent implementation commit.


### 2026-11-18 — Universal ECS whole-engine migration register
- Implementation SHA: `0586878c1047c77401c870830330dfc32ab299b8`.
- Changed: `docs/ecs-all-features-migration-plan.md`; catalog-based review of all 120 packages (78 simulation, 21 presentation, 21 tools), 41 RTS `Simulation` system registrations, FPS opt-in `ecsAuthority` and the three existing generic weapon packages. Records waves, source-of-truth contract, host parity gates, native/API/editor requirements and per-package dispositions.
- Verification: read source via linked GitHub branch and parsed the catalog; **no native build, CTest or real RTS/FPS gameplay parity executed**. A migration plan is not a code migration. Library data may have changed since inspection; re-read before editing.
- Next: add reusable ECS gameplay components/systems (weapon reload, timers, stats) and independent native/cross-genre rollback fixtures; integrate actual RTS/FPS paths incrementally with legacy-save parity.


### 2026-11-18 — Generic ECS timed mechanics header
- Implementation SHA: `5907d5f893f91bd4e7989ed2eb8eaa3109ba2959`.
- Added `libraries/include/aiverselib/genre/EcsTimedMechanics.h`: `WeaponReloadComponent`/`ReloadIntentComponent` with reload and fire-intent gate stages; `HealthRegenerationComponent` over the canonical shared `HitPointsComponent`; `EnergyRegenerationComponent`; `TimedExpiryComponent`, all independently registered with `Simulation` and without RTS/FPS dependencies.
- Verification: source uploaded via GitHub; **native compile and CTest not run**, host integrations not yet switched. Shared health/weapon components remain additive/opt-in and are not asserted to replace legacy modules.
- Next: add two-host COW/save/rollback fixture, package registration, skill, catalog and CMake, then verify natively before any default host switch.


### 2026-11-18 — Shared timed mechanics cross-genre regression fixture
- Implementation SHA: `337cd0e9faea659b5ddedb75c6eb7eeecf0ce77a`.
- Added `tests/ecs_cross_genre_timed_mechanics_test.cpp`: shooter + strategy hosts exercise the identical reload/fire-gate, health/energy regeneration and timed expiry systems; verifies ammo/reserve, stable ticks, suppression/consumption, full reload, TTL and snapshot replay with/without COW.
- Verification: fixture committed through GitHub; **not compiled nor run** (no checked-out native toolchain in this environment). Assertions reflect expected semantics, not observed pass results.
- Next: register this test in CMake and package in catalog, add agent skill; then run compiler/CTest before integrating RTS/FPS gameplay.


### 2026-11-18 — Timed ECS package, skill and CTest wiring
- Commits: `939f8e89d039c443bb802ce66cd2cc93f60eb17c` (new agent skill), `9cb1be3ca61432eb98b60b9c8fd1b95764e643bf` (`libraries/catalog.json`, package #121), `a6aac3026dfc058de780baaaaab34a8708a5570c` (CTest registration in `CMakeLists.txt`).
- New public target `AIVERSE::lib_ecs_timed_mechanics` depends on `ecs-weapon-launch`; native registration example and compatibility requirements in `skills/gameplay/aiverse-ecs-timed-mechanics/SKILL.md`. Test executable `aiverse_ecs_cross_genre_timed_mechanics_test` added to CTest; test source already added in prior checkpoint.
- Verification: programmatically parsed `libraries/catalog.json` as 121 valid JSON entries at the catalog edit point. **No CMake configure/build, no CTest execution, no package catalog filesystem check, no FPS/RTS runtime parity**; avoid claims of readiness.
- Next: run `python3 tools/libraries/catalog.py check`, configure native CMake, build `aiverse_ecs_cross_genre_timed_mechanics_test` and run `ctest -R ecs_cross_genre_timed_mechanics_test --output-on-failure`. Investigate compile errors and cross-genre replay. Then integrate one legacy host (FPS or RTS) behind an explicit gate and compare tick/order/checksum/save/network before enabling.


### 2026-11-18 — Standalone ECS experiment suite wired
- Implementation SHA: `515536b3793c973b22a2b1e72e2dd33ecc64054e`.
- Changed `tests/run_ecs_experiment_suite.sh` to compile/run the cross-genre timed mechanics test from public headers with only shared engine/world link inputs; no RTS/FPS game library dependency.
- Verification: GitHub source edit succeeded. **No shell syntax check, native compilation, sanitizer or test execution available in this environment.** GitHub Actions on previous commit `3ad66dc1` returned all runs queued; not passing evidence.
- Next: check CI completion and target build errors; proceed to controlled legacy host integration only after baseline parity and source-of-truth mapping.


### 2026-11-18 — Universal ECS locomotion policy adapter
- Implementation SHA: `7e48fd23d31cc82fbd791c755f7301bb18635a48`.
- Added `libraries/include/aiverselib/genre/EcsLocomotionSystems.h`: `LocomotionParametersComponent`, `LocomotionStateComponent`, `LocomotionInputComponent`, `LocomotionOutputComponent` and `addLocomotionPolicySystem`. Uses the existing shared `stepMotion` fixed-point policy instead of duplicating movement math. Consumes jump edge, leaves persistent movement wishes, returns velocity; host collision/position integration is intentionally separate.
- Verification: GitHub source committed, **native compiler and behavioral tests not executed**. Does not replace FPS `moveActor` or RTS `sysMovement` yet.
- Next: native cross-genre tests and CMake/catalog/skill registration; then integrate collision sampling and movement into actual hosts behind parity gates.


### 2026-11-18 — Locomotion ECS two-host regression
- Implementation SHA: `9c7b593dad3755b89fec0186e2e03dfd2a92466e`.
- Added `tests/ecs_cross_genre_locomotion_test.cpp`: shooter and strategy modules use the exact same ECS locomotion system, verify jump edge, deterministic ground/air acceleration, output publishing, rollback/replay checksums and COW equivalence.
- Verification: source committed; **no native C++ compilation or runtime assertions executed**. Tests do not cover collision, ladders, mantling, swimming or legacy FPS/RTS integration.
- Next: publish native package target, skill, CMake/standalone test runner; add separate collider/transform integration and nontrivial locomotion mode tests before changing default games.


### 2026-11-18 — ECS locomotion package and test registration
- Commits: `278cd8384985c74519940f4d61cae1ecf0290c94` (skill), `40211d94b72739f3127bb58c1415ef417082d19a` (catalog #122), `c9ef4fe369e1fcc26aaabfb7fcbf321d4cb857cc` (CMake/CTest), `0bf6a9a7ac8f52fe5c6ff1fc7635ebcf24bc40a3` (standalone experiment runner).
- New public target `AIVERSE::lib_ecs_locomotion` depends on existing `character-locomotion`; public `EcsLocomotionSystems.h` and skill cover stable state/input/result policy binding without FPS/RTS module linkage. Two-host test checks replay and COW; `stepMotion` is reused, not duplicated.
- Verification: catalog parsed to 122 JSON entries during edit. **No native compile, native consumer build, CTest, nor runtime parity run**. Remaining collision, transformations and advanced locomotion modes are not yet ECS-integrated in built-in games.
- Next: run catalog/check, complete CMake/test suite, and convert one actual RTS/FPS consumer with exact saved-state and deterministic replay parity before deleting a legacy path.


### 2026-11-18 — Shared ECS inventory transaction system
- Implementation SHA: `bbda102d6635edf170d83fe5a79280351a53d159`.
- Added `libraries/include/aiverselib/genre/EcsInventorySystems.h`: per-Entity `InventoryComponent`; immutable `InventoryCatalog` world resource; ordered `InventoryCommandQueue`, `InventoryResultQueue`, generation-safe source/destination entities, and `addInventoryTransactionSystem`. Reuses transactional `gameplay::addItem/removeItem/transferItem` policies, preserving capacity and no-partial-write guarantees without FPS/RTS dependencies.
- Verification: source committed; **not compiled or run**, not yet integrated into legacy built-in FPS `Actor::bag` or RTS inventories.
- Next: cross-genre transaction/rollback tests, public package and CMake runner, skill; then migrate FPS loot/pickups and RTS item transfer by explicit authority cutover.


### 2026-11-18 — Cross-genre ECS inventory transaction fixture
- Implementation SHA: `b52131d3db13dc61c9a1c4ab23dc30454b4a3a25`.
- Added `tests/ecs_cross_genre_inventory_test.cpp`: same ECS inventory queue and transaction system in an RTS store and an FPS bag host; transfer, stack expansion, weight-cap rejection without partial mutation, ordered results, id sequence and COW snapshot rollback parity.
- Verification: source committed through GitHub. **No compiler execution or CTest results**. Built-in FPS and RTS gameplay still use their legacy inventory paths; test does not assert they have been migrated.
- Next: skill/catalog/CMake/standalone runner, then parity-checked native and built-in host adoption.


### 2026-11-18 — ECS inventory native package, skill and test registration
- Commits: `f59e7c888dd690ca4e76a9169f5556b89511e558` (agent skill), `30828e37f64e1d0f5d9794b8ec50ae9d77ee2fc3` (catalog #123), `0a311f24c98e9d63f212ccd4f380c1cd0d866ef3` (CTest), `513bffd59e84c4331a4399eb112fa2df18218343` (standalone experiment script).
- Published public native target `AIVERSE::lib_ecs_inventory` and `skills/gameplay/aiverse-ecs-inventory/SKILL.md`, reusing existing Inventory algorithms. Registered `ecs_cross_genre_inventory_test` on two unrelated game hosts, stable ordered commands, weight rejection, rollback and COW.
- Verification: catalog JSON parsed to 123 records in editor operation. **No compiler/CTest/CI success yet**. FPS/RTS built-in inventories are not migrated; authorization remains host responsibility.
- Next: run native CI, resolve issues, then migrate FPS loot/inventory and RTS gather/storehouses via explicit ECS owners and full save/network/checksum regression gates.


### 2026-11-18 — Agents routing updated for all-feature ECS migration
- Implementation SHA: `853a1f5d5f19c3aff06754622f7d74e838dc4d8f`.
- Changed `AGENTS.md` to link the 120-package baseline audit, newly published public ECS packages (timed mechanics, locomotion, inventory), their skills and required real-world RTS/FPS migration gates. Explicitly distinguishes library availability from production gameplay migration.
- Verification: GitHub source committed only. CI/build and user gameplay tests are still pending; latest workflow status check reported queued runs.
- Next: check compilation/CTests; migrate an actual built-in host with source-of-truth ownership and validate golden save/network/replay parity before turning on defaults.


### 2026-11-18 — Whole-engine ECS migration tracker
- Created persistent work tracker: https://github.com/chromealex/AIVERSE/issues/19 (epic with phased checklists, canonical authority gates and links to audit/AGENTS/this handoff).
- The epic marks reusable ECS packages as **implemented but unverified and not yet adopted by built-in RTS/FPS**. Do not close it merely because catalog entries exist.
- Next: obtain native build/CI evidence, then migrate actual FPS/RTS systems one behavior at a time, update the linked issue checklist and this handoff separately.


### 2026-11-18 — Shared ECS weapon cooldown snapshot regression
- Implementation SHA: `b703c3937a7397a1fe71987b29e7e90a5bf5e602`.
- Changed: `tests/ecs_cross_genre_weapon_pipeline_test.cpp`. New `exerciseCooldown<Tag>(cow)` fixture initializes two rounds, fires once, snapshots, requests another shot one tick later (inside the six-tick fire interval), and checks ammo remains 1, nextShotId remains 2, no pending spawn and fire intent is consumed. Restores the snapshot and compares deterministic checksum after replay. Compares ShooterTag/ArtilleryTag and deep/COW snapshot modes.
- Verification: GitHub commit succeeded. C++ compilation and test execution not performed; expected outcome is an assertion, not an observed pass.
- CI status observed for previous handoff SHA `4de1319dfad9589975ed885fdd7565ba1e8d2bf1`: CI #1178 was **cancelled**, while Studio #845, GPU batching #449 and ECS experimental #328 were queued. Cancelled is not a pass.
- Next: run the new fixture in native CTest, inspect Actions outcomes, then wire real FPS/RTS launch and projectile paths to shared systems behind opt-in gates with byte-compatible saves and rollback. Keep Editor regression testing on the acceptance checklist.


### 2026-11-18 — Shared ECS weapon cooldown expiry and replay
- Implementation SHA: `fbda37412977436a639c26230c1661f16478a008`.
- Changed: `tests/ecs_cross_genre_weapon_pipeline_test.cpp`, `exerciseCooldown<Tag>(cow)`. Following rejected early fire and snapshot replay, advances to tick 6 (six-tick fire interval), snapshots, requests the next shot, and checks the final ammo is consumed, nextShotId advances to 3, spawn queue is drained, and fire intent is consumed. Restores the ready snapshot, repeats the shot and compares checksum. All existing ShooterTag/ArtilleryTag and deep/COW variants exercise this path.
- Verification: GitHub source commit succeeded. **No native compilation or execution yet**; tick boundary semantics and test success must be confirmed by the native runner. This is a shared library test extension, not proof of actual FPS/RTS combat migration.
- Next: run `aiverse_ecs_cross_genre_weapon_pipeline_test`, inspect failures, then integrate real genre adapters with save v9, transport rollback and Editor regression gates. Preserve existing deterministic gameplay until parity proven.


### 2026-11-18 — First live FPS Editor projection read from ECS authority
- Implementation SHA: `b781438fd76808a24cda6de944bb1db97376c9c4`.
- Changed: `modules/fps/FpsModule.cpp::syncActorEntities`. After the authoritative `ActorRuntime` commit, `ActorEntityIdentity`, `ActorEntityTransform` and `ActorEntityVitals` are now populated from the ECS runtime payload when `fps.ecsAuthority` is enabled. Default legacy OFF mode still reads the legacy Actor. This removes a second read path for editor-facing actor projections and establishes ECS as their input source. The legacy gameplay tick and save v9 workspace are **still present**.
- Verification: source change pushed to GitHub; **not compiled or executed**. Existing post-mirror projection guard should catch divergence when ECS authority is enabled, but its actual test outcome is unverified.
- Next: migrate another actual gameplay stage (weapon launch / damage) to shared ECS components behind parity gate, preserving FPS-specific hitscan/rewind rules; do not replace those with ballistic flight without matching legacy semantics. Independently migrate RTS adapter, then validate save v9, network rollback and Editor.


### 2026-11-18 — FPS live fire accounting writes canonical ActorRuntime
- Implementation SHA: `0a17b92c808db73fc39877004bef3c2ed4e4e0d3`.
- Changed: `modules/fps/FpsModule.cpp::shoot`. Under opt-in `fps.ecsAuthority`, the actual gameplay firing path locates the shooter ECS Entity and its `ActorRuntime`, seeds its ammo/shots/nextFire from the current compatibility workspace (which may contain same-tick reload or other changes), applies the shot accounting to the runtime component, then writes the three resulting values back to the legacy workspace. Default OFF branch preserves the previous exact `--ammo; ++shots; nextFire=tick+interval` sequence. Existing hitscan/lag compensation, ballistic projectile creation, RNG, damage and save v9 remain untouched.
- This is **partial direct ECS mutation**, not adoption of the shared `EcsWeaponLaunchSystems` yet. The same-tick legacy workspace still carries the rest of combat, and `fps.ecsMirror` commits the full actor at the end. Do not describe this as universal RTS/FPS shooting.
- Verification: GitHub source commit only; no C++ compilation, replay checksum, FPS transport, save v9 or Editor test executed. Pay particular attention to same-tick reload and weapon switch ordering.
- Next: extract common weapon state/launch logic with real ECS systems and adapt FPS hitscan/rewind without changing behavior; add parity tests and integrate RTS. Run native build before production use.


### 2026-11-18 — FPS dynamic authority regression compile blocker fixed
- Implementation SHA: `cf9acb846fe018eb90198b0452bb1da48d9c6e0e`.
- Changed: `tests/fps/tests.cpp`. Moved accidental `if(i==0)` block (which referenced undefined `i`, `world`, `projectile`, `loot`, `vehicle`, `p`, `l`, `v`) out of `ecsActorAuthorityParity` and into the correct `ecsDynamicAuthorityParity` fixture, after its first synchronized tick. Added map `contains` checks before `at` lookups to report missing indexes as fixture assertions instead of uncaught out_of_range.
- Verification: static inspection of current branch + exact anchored GitHub edit; **native compile, CTest, sanitizer and gameplay parity still unverified**. Git clone from this execution environment failed DNS resolution. GitHub Actions check was previously queued/cancelled, not a pass.
- Next action: compile `aiverse_fps_tests` on native macOS/Linux; validate existing ECS actor/dynamic authority fixtures; inspect any additional malformed merged test blocks before replacing real gameplay authority.


### 2026-11-18 — Shared reload policy extracted for direct native host integration
- Implementation SHA: `930cdf4ae80aa0cc80a845d3c5d2a31f086119fa`.
- Changed `libraries/include/aiverselib/genre/EcsTimedMechanics.h` to extract `stepWeaponReload(WeaponStateComponent&,WeaponReloadComponent&,tick,request)`; `addWeaponReloadSystem` now calls the same deterministic function. Explicitly preserves normal FPS behavior including completion before a same-tick new request. No added system-level double-owner.
- Verification: static source modification only. **Native compiler/CTest unavailable so far**, no live FPS parity yet. The shared function uses the pre-existing FPS transfer arithmetic, so any invalid negative state handling must be checked in host validators.
- Next: convert the legacy FPS reload step to a temporary typed adapter invoking this policy with canonical fields, then add native golden parity fixtures covering weapon switching, ammo/reserve, replay and save v9.


### 2026-11-18 — First real built-in FPS mechanic uses the universal shared reload policy
- Implementation SHA: `a497f62f553680a456e1d7db645188204e677cad`.
- Changed `modules/fps/FpsModule.cpp`: the FPS actor's real per-tick reload completion, reserve transfer and reload-request decision now run via `lib::genre::stepWeaponReload` defined in `EcsTimedMechanics.h`, which is also used by generic `addWeaponReloadSystem`. Adapted `Actor::ammo/reserve/nextFire/reloadUntil` to temporary value structs in the same place inside tick, preserving weapon selection and shoot ordering and retaining legacy save v9 bytes. There is **no second mutable canonical reload ECS component** yet; under `fps.ecsAuthority`, `ActorRuntime` remains canonical at tick boundaries. This is actual shared-algorithm adoption, **not completion of full FPS ECS system extraction**.
- Verification: GitHub edit committed; **native compilation, FPS tests, direct reload parity, save/network and rollback not yet executed**. Generic policy used same legacy reload transfer expression and finish-then-request ordering; boundary cases near uint32 tick overflow remain a known parity gate.
- Next: add an FPS ECS-authority ON/OFF reload-fire-switch replay fixture, run native FPS/CTest and resolve any compiler behavior differences before switching authoritative weapon state to standalone ECS components.


### 2026-11-18 — Real FPS reload tick parity regression added
- Implementation SHA: `92a165d34c865a26955d551d2a1919946c9ea9b7`.
- Changed `tests/fps/tests.cpp`: new `ecsReloadAuthorityParity` drives 25 tick-accurate FPS inputs in two independent built-in modules, one legacy and one with `fps.ecsAuthority=true`. It checks authoritative ActorRuntime after every tick, source-state equality, shared reload start/completion/fire suppression and reserve accounting, weapon selection, v9 save cross-mode load, pre-reload-completion snapshot restoration, replay checksums and per-tick parity.
- Verification: static authored fixture/commit only. **No native compile or actual assertions executed**. Evaluate its tick-0 and weapon-switch expectations through the native test runner, and resolve any failure before claiming parity.
- Next: compile `aiverse_fps_tests` and run FPS tests; then continue separating the real FPS fire/damage and dynamic state systems from `State::actors` behind explicit per-feature gates.


### 2026-11-18 — Shared accepted-shot accounting extraction
- Implementation SHA: `f8b41cf113dc9042824716192073068fb429a9e8`.
- Changed `libraries/include/aiverselib/genre/EcsWeaponLaunchSystems.h`: extracted pure `commitAcceptedShot(ammo,nextFireTick,now,interval,shots?)` policy, used by generic ECS weapon launch after its existing acceptance checks. No RNG or structural writes, and no new validation inside the helper. Existing generic accepted-shot semantics remain decrement ammo and set nextFireTick.
- Verification: GitHub commit only; native C++ compilation and test execution **not run**.
- Next: call the same policy from real FPS `Module::shoot` while preserving its prior validation/hitscan/rewind and save v9 ordering; then document that separate commit.


### 2026-11-18 — Real FPS shot accounting now calls shared weapon policy
- Implementation SHA: `25917da11cf6cb5a2ea32c436ae891f6b4c2ee0d`.
- Changed `modules/fps/FpsModule.cpp`: includes the public shared weapon launch header and replaces duplicated post-acceptance ammo/cooldown/shots mutation in both `fps.ecsAuthority` ON and OFF paths of real `Module::shoot` with `lib::genre::commitAcceptedShot`. ON path still writes `ActorRuntime` first and copies three fields back to the legacy tick workspace. OFF path still mutates the legacy actor. The helper performs the same arithmetic as the removed inline statements; shot eligibility, RNG, pellets, hitscan, lag compensation, projectile creation, impact/damage, save v9 and network formats are unchanged.
- Verification: GitHub source commits only; C++ compile and runtime tests **not executed**. Shared policy may expose include/link issues; check native FPS target and cross-genre weapon test before merge.
- This is a **real shared gameplay algorithm**, but FPS/RTS are NOT yet fully consuming common ECS WeaponLaunch/Flight/Damage systems. Next: connect actual RTS fire accounting and/or extract shared collision/damage stages with parity fixtures; then migrate authoritative ECS state and test rollback.
