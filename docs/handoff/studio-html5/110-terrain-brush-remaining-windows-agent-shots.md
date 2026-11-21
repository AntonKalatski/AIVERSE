# 110 — Terrain brush in the Scene, the remaining legacy windows, agent screenshots, Statistics, Snapshot Diff

## Done
- **Terrain brush**:
  - The Terrain panel has a **Brush** bar: Raise / Lower / Smooth / Flatten / Noise / Erode, radius, strength (`studio.terrainBrush`).
  - While the brush is armed, LMB drag in the Scene sculpts live (`terrain_sculpt` dabs at least a third of the radius apart, at the ground point). Alt+drag still orbits.
  - On mouse up the stroke is merged into **one History step "Terrain brush"** (`MapDoc::mergeUndoSince`).
  - Leaving the Terrain panel disarms the brush.
  - Code: `StudioSession::terrainStroke*`, `StudioMetalView` mouse path, `TerrainBrushBar.tsx`.
- **Remaining op-backed windows**: the same schema-driven ToolPanel, with groups in `StudioTools.h`.
  - Fields; PHYSIS; FPS & Cameras (fps + LENS); Raid; Balance (duel / matrix / batch / match / scenario / lint, all read-only).
  - SpectraFX Graph; SpectraFX Functions; Modeling (plus generated textures).
  - Animation Clips; Animation Graph; Rig; Model Animation; Import Project (port_*).
  - The read-only suffix list grew (`_run`, `_scan`, `_stats`…).
  - The Animation centre tab now links to the four animation panels.
  - Camera Bookmarks and Tools left the legacy list: both are on the Scene toolbar.
- **Statistics** (`studio.stats`, `InsightPanels.tsx`):
  - Objects, map size, players, roads and rule types.
  - Scene fps and ms, smoothed in `Main.mm tick`.
  - Counts by owner and by type (bars).
- **Snapshot Diff**: compares two JSON files of the project (maps listed) path by path. The diff is `jsonDiff.ts`, tested: added / removed / changed, a 1e-6 tolerance for floats, and a filter.
- **Agent screenshots**:
  - `view_screenshot` from the agent is answered by the host with the Scene's next frame (PNG, at most 1280 px wide, base64) through `finishAgentScreenshot`.
  - The answer is an error when the Scene panel is hidden.
  - Only one screenshot can be in flight at a time.
- Still legacy (genuinely GUI-only):
  - Attachments (GPU attachment authoring).
  - Extensions (project Lua panels).
  - Frame Capture (CPU frame capture with draw calls).
  - Texture Resources (live GPU texture list).

## Verified
- **Session test** (Mac):
  - The brush rejects an unknown op.
  - Three dabs make one undo step labelled "Terrain brush"; undo returns to the previous depth.
  - The stats entity count is right.
  - The agent screenshot is pending until the host answers, and the reply carries `png_base64`.
- **On screen** (private lua_lab):
  - A Brush stroke across the map raised a ridge live; History showed one step "Terrain brush".
  - PHYSIS state rendered.
  - Statistics showed 534 objects, 64×48, 54 fps · 18.4 ms, and owner and type counts.
  - The Model Animation and Rig panels rendered.
