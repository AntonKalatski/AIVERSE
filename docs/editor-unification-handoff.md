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
