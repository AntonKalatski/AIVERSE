---
name: aiverse-tiles-2d
description: Build 2D tile-based AIVERSE games and maps through MCP - tilesets, tile layers, painting, sprites and atlases, checked with map_ascii and screenshots.
---

# 2D tiles

1. `tileset_list` - tilesets of the game; images and tile ids live in the game's tileset files (`game_file_read`).
2. New art: `asset_import` the sheet, describe the tileset with `game_file_write`; texture atlases are built by the atlas step of `aiversebuild` (`engine/atlas`), do not pack by hand.
3. `tiles_init` on the open map (layer, tileset, size) -> `tile_paint` (cells, brushes) -> `tile_info` to inspect a cell; `tiles_remove` to drop a layer.
4. Gameplay meaning stays in the map characters / terrain (`terrain_paint`): tiles are the picture, passability comes from terrain unless the tileset declares otherwise.
5. Check: `map_ascii`, `map_validate`, `view_focus` + `view_screenshot` at gameplay zoom; seams and wrong rotations show only on screenshots.
6. Units and buildings as sprites: the type's `view` block (`docs/models.md`), Spine skeletons `docs/spine.md`.
7. Bots: `match_run` - tiles never change pathing silently; if bots stall, compare terrain with the painted picture.

Runtime library: none. Workflow over the engine MCP tile tools.
