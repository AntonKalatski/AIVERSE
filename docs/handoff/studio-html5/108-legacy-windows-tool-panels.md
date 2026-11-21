# 108 — Legacy editor windows as HTML panels (stage 5 of the editor plan)

## Done
- **15 legacy ImGui windows now have HTML5 panels** and left `legacyWindows.ts`:
  - World: Terrain, Water, Roads, Tiles, Fog of War, Generate.
  - Look: Post FX, Effects, Light Bake, Models.
  - Logic: Scripts, Input, Overlays.
  - Publish: Branding, Asset Audit.

  Every workspace layout gains them once (`WORKSPACE_ADDED`). The default dock tree carries them all; the nested-dock tests are updated.
- **Schema-driven tool panels** (`panels/ToolPanel.tsx`, `toolSchema.ts`, `toolPanels.ts`, `JsonTree.tsx`). Used by Terrain, Water, Roads, Tiles, Generate, PostFX, Effects, Light Bake, Models, Overlays and Branding.
  - The panel asks `studio.ops {panel}` for its Workspace ops: the same ops the MCP agent runs, with their JSON schemas.
  - It shows the read-only "state" ops on top (map_info, road_network, postfx_get, …), re-read after every change, undo or redo.
  - Below that, one card per op, with a form generated from the schema: number, integer, boolean, enum, string, and JSON for objects and arrays.
  - Empty optional fields are omitted, so the op keeps its own defaults.
  - `op` / `mode` / `kind` choices start on their first option. terrain_sculpt drops `required` for planet calls.
- **Native side** (`StudioTools.h`, `StudioSession.cpp`):
  - `studio.op {op, params}` runs only allowlisted ops through `ws_.call` (the same validation and undo as an agent edit) and returns `{result, changed}`.
  - Read-only ops are recognised by suffix.
  - A changing op becomes a History step labelled after the op ("Terrain sculpt"). Main.mm then emits `document.changed`, so the Scene redraws (terrain sculpt shows immediately).
  - The selection is dropped only when the entity count changes.
- **Hand-made panels** (`panels/ProjectToolPanels.tsx`):
  - **Fog of War**: the same controls as the old window (mode, allies, line of sight, look sliders, style, cell grid, reset).
    - Reads rules via `rules_get` and edits via `rules_merge` / `rules_delete`.
    - The rules ops validate and save the rules file immediately, as before.
  - **Scripts**: the list from `script_list` (role, missing files) and a Lua editor with a gutter.
    - Tab inserts two spaces; ⌘S saves.
    - Save goes through `script_write` (compiled first, not saved on a syntax error); New registers the file.
    - Check runs `script_check` (all scripts plus a 30 s bot match) and shows errors and the log.
  - **Input**: `studio.inputGet` / `studio.inputSet` read and write `input/actions.json` through the engine's InputFlow `readAsset` / `saveAssetFile`, so invalid assets are refused with the engine's message.
    - Editing covers actions (rename updates bindings) and contexts (priority, active).
    - Bindings take a device and a named code (keys, pad buttons and axes from the engine tables) plus consume.
    - Modifiers and triggers are kept and shown as counts.
  - **Asset Audit**: `studio.assetAudit` is the old window's scan (quoted asset paths in json, lua, rml and rcss that point at no file). Paths that the engine provides itself are no longer reported as missing.
- **Dock tabs**: the active tab scrolls into view in an overflowing strip, for panels opened from the palette, and the mouse wheel scrolls the strip.
- `build/claude/studio-rebuild-run.command` now launches on the private `qa-build-lab` copy by default, never another agent's QA project.

## Verified on the Mac (private lua_lab copy)
- **Terrain**:
  - Map info shows.
  - Sculpt raise at 20,16 r8 s5 put a hill in the Scene; the map turned unsaved and the Console logged "Рельеф: raise".
  - The first attempt surfaced the engine error (op missing), which led to the default-choice fix.
- **Fog of War**: Explored mode saved to rules and read back after a relaunch.
- **Scripts**: the list shows roles; bounty.lua opens; Check reports "No errors".
- **Input**: added an action, a context and a Space binding; saved `input/actions.json`.
- **Asset Audit**: found `game.json → icon.png` missing (true for lua_lab).
- **Post FX**: added a vignette; the state updated (game chain 1).
- **Generate, Models, Branding and Overlays**: state and cards render.
- Session test covers ops, the allowlist, the History flag, fog merge, Input validation and save, and Audit.

## Not done / next
- Brush-style terrain painting in the Scene viewport (the panel takes coordinates). Roads and water splines are still edited through op forms, not gizmos.
- Remaining legacy windows: Fields, PHYSIS, FPS Authoring, Raid, Agent, Stats, SpectraFX, Modeling and the animation windows.
- Stage 6: the in-editor agent panel.
