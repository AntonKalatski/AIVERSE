# 111 — Scene brushes, Keep play changes, the last legacy windows, Studio as the main editor

## Done
- **Scene brushes**: `studio.sceneBrush` replaces `terrainBrush`; the logic is `StudioSession::stroke*`.
  - Dab tools apply live: sculpt (`terrain_sculpt`), paint material layer (`terrain_paint_material`), tiles (`tile_paint`), field (`field_paint`), objects (`entity_place` with count and spread).
  - Path tools collect a point every 2 cells and build on release: road (`road_add`, smooth curve) and river (`water_set`, kind river, width).
  - Every stroke is one History step ("Terrain brush", "Paint tiles", "Scatter objects", "Draw road", …).
  - The ops run with source "editor", so there is no Console line per dab; one summary line is logged per stroke.
  - Where the brushes live: the Terrain panel offers Sculpt / Paint / Objects; Tiles, Fields, Roads and Water have their own tool.
  - The ring under the cursor is `StudioMetalScene::setBrushCursor`.
  - `[` / `]` resize the brush, Esc puts it down (physical keys); `brush.changed` keeps the panel in sync.
  - Moving the nodes of existing roads or rivers with a gizmo is not done.
- **Keep play changes**: a title-bar button while playing.
  - Map entities take their in-game position and resource amount; ⌥-click also removes the entities that died.
  - One History step "Keep play changes".
  - Matched by map index AND type: `RtsModule::mapPlaced()` records the entity spawned for each map item. It is editor-only and not part of the checksum.
  - Road index pairs are fixed when entities are removed.
- **Agent context**: each request carries the Scene selection and the last 8 Console errors.
- **The last legacy windows**:
  - **Frame Capture**: frame times, fps / avg / p95 / worst, draw calls, freeze with Capture.
  - **Texture Resources**: live GPU textures of the Scene renderer with a format-based estimate (`gfx::Device::textureResources`). Served by the host method `studio.gpuStats`.
  - **Extensions**: `StudioLuaPanel.h` ports the declarative Lua editor API, sandboxed with an instruction budget. It covers `window(ctx)` widgets, `on_action` / `on_change` commands (focus / select / open panel / status) and `scene_gizmos` drawn in the Scene (lines, circles, points). Labels are not drawn.
  - **Attachments**: edits the layout JSON (parent, model, socket, id, offset / yaw / scale, animated graph) through `view::AttachmentLayoutCodec`. There is no live GPU preview.
  - `LEGACY_WINDOWS_NOT_MIGRATED` is now empty, and the Window menu hides the legacy section.
- **Studio as the main editor**:
  - `studio_mac.command [project]` builds the web UI and the native Studio and opens it.
  - `AIVERSE_BUILD_STUDIO_HTML5` defaults to ON on macOS when `apps/studio-web/dist` exists.
  - README and overview quick starts point to Studio; the ImGui editor remains for Linux / Windows.
  - First-run **Welcome Tour** (5 steps, Window ▸ Welcome Tour…).
  - `docs/studio-html5-parity.md` has the real status of every window.

## Verified
- Session tests:
  - The object scatter stroke is one step, its undo works, and a road without a type is refused.
  - Keep play changes: move by index + type, out-of-range skipped, removal with undo.
  - Extensions: example widgets / commands / gizmo.
  - Attachments: invalid layout refused, valid round trip, path outside the project refused.
- Web tests: JSON diff, frame summary, TRS ↔ matrix.

## Not done
- Road and river node gizmos.
- Attachments live preview.
- Extension label gizmos.
- A real agent run (the claude CLI on the Mac is not signed in).
