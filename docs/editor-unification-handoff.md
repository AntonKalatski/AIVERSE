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
