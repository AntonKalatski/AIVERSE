---
name: aiverse-game-screens
description: Build the game's own AIVERSE screens (main menu, levels, shop, profile, settings, results, popups) and HUD with RmlUi and Lua through MCP, validated and previewed on phone profiles.
---

# Game screens and HUD

1. `ui_api` - templates and bindings; `ui_contract` - HUD documents, data bindings, events. Read `docs/ui-scripting.md`.
2. `ui_screen_create {name, template, start?, popup?}` (blank, home, levels, shop, profile, settings, result, dialog). It writes `ui/screens.json` and profile/shop data when missing; `start: true` replaces the engine main menu, `result` becomes the after-match screen.
3. Edit `ui/screens/<name>.rml`, `ui/skin.rcss`, `ui/hud.rml` with `game_file_write`; the game's `ui/` overrides the engine's. Texts are lang keys (`@lang.key`) in `lang/*.json`, never concatenated sentences.
4. `ui_validate` - fix every error.
5. `ui_preview {screen, device}` on several profiles: `device_list` (iPhone, iPad, Pixel, Galaxy, Redmi, desktop) -> `device_set {device, orientation, safeArea: true}`; check cut-outs and safe areas.
6. HUD in a match: `play_start` + `view_screenshot {window: game}`.
7. Screen list: `ui_screen_list`, `ui_screens_config`; remove with `ui_screen_delete`.

Runtime library: none. Workflow over the engine MCP UI tools.
