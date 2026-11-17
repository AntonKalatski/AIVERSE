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
- [ ] Editable native ECS component inspection, add/remove component support where safe, type-aware serialization/writeback.
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
