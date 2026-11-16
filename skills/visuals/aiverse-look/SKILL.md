---
name: aiverse-look
description: Set the look of an AIVERSE game through MCP - lighting and sky, point lights, post effects with phone cost, materials and textures, model view blocks, fog of war style, checked on screenshots.
---

# Look of the game

Always look at the result: `view_focus` + `view_screenshot` after every step.

1. Lighting: `lighting_set` (sun, sky, day/night, presets); local lights `light_add` / `light_update` / `light_list`. `view_lighting` to preview.
2. Post effects: `postfx_catalog` -> `postfx_preset` (cinematic, vivid, mobile, noir, retro, miniature, sunset, winter, nightVision, clean; `scope` game or map) or `postfx_add` / `postfx_set`. Answers carry `warnings` with ms per frame for heavy effects (bloom, fxaa, sunShafts) - pass them to the user. `postfx_estimate` for the budget, `view_postfx {quality: low}` + screenshot for weak phones. Neutral LUT: `postfx_lut_neutral`.
3. Materials: `texture_patterns` -> `texture_generate` (bricks, panels, wood, camo, scales, normal maps, team masks) -> `material_set` (shared `materials.json`) -> `material_apply {type, material, target|node}`.
4. Models: `asset_import`, then the type's `view` block via `rules_merge` (model, material overrides, team colour, clips; `docs/models.md`).
5. Terrain surface: `terrain_material`, `terrain_paint_material` (`docs/terrain-textures.md`).
6. Fog of war look: `rules_merge {path: "fogOfWar"}` (mode, colour, style, softness, lineOfSight).
7. Phone check: `device_set` + `view_screenshot {window: game}` on a low-end device profile.

Report: what changed, screenshots compared, per-frame costs from the warnings.

Runtime library: none. Workflow over the engine MCP view tools.
