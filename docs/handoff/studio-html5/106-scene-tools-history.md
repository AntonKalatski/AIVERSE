# 106 — Scene tools and History (stage 3 of the editor plan)

## Done
- **History panel** (`panels/HistoryPanel.tsx`, Scene workspace right, Window → History): every map edit is a
  step (Move / Rotate / Scale / Set key / Delete / Duplicate / Align / Place asset / …), the redo branch is
  greyed; clicking a step (or *Opened*) undoes/redoes to it. Native: `StudioSession::dispatch` wraps
  `dispatchImpl` and keeps a label per MapDoc undo/redo snapshot (`undoLabels_`/`redoLabels_`, reconciled with
  `MapDoc::undoDepth/redoDepth` so edits made by agent tools appear as "Edit"); `studio.history`,
  `studio.historyJump {position}`. Nested internal calls use `dispatchImpl` (one label per user action).
- **Align** (Edit menu): Align min/centre/max X and Z, Distribute along X/Z (3+), Drop to Ground —
  `studio.entitiesAlign {entityIds, mode}`, one undo step.
- **Surface snap** (toolbar mountain toggle): a Move drop sets the object's Y to the terrain height at its new
  spot (same setProperties commit, one undo step).
- **Pivot snap**: hold V while moving — the dragged object jumps to the nearest other object's pivot within
  28 px of the cursor (pivots = Move-gizmo pivots of last frame).
- **Frame All**: ⇧F, toolbar button, Edit menu (`viewportInput {kind:'frameAll'}`).
- **Camera bookmarks**: toolbar bookmark menu, ⌥⇧1…9 store, ⌥1…9 go (`viewportInput {kind:'camera', set?}`,
  per project in browser storage, `components/sceneBookmarks.ts`).
- Scene toolbar stays one row (Surface / Gizmos / Frame All / bookmarks are icons with tooltips).

## Verified on the Mac (private copy of lua_lab)
- Move drag → History shows "Move"; clicking *Opened · River Valley* undid it (step greyed as redo);
  Frame All framed the map; toolbar fits one row.
- Not checked on screen: V pivot snap and ⌥ bookmarks (background tools cannot hold keys),
  Edit → Align menu (HTML menus cannot be opened in the background); covered by the session test (Align +
  History jump).
