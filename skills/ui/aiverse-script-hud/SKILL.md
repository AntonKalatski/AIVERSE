---
name: aiverse-script-hud
description: Add data-only AIVERSE HUD bindings in game.json - `scriptHud` panels driven by trigger variables, `worldCounters` numeric labels in the world, and world-anchored entity action controls - shared by the client and the editor's game view.
---

# Script HUD, world counters, entity actions

Use when a HUD element only shows trigger variables or calls an ability and does not need a full RmlUi screen.
Full screens and menus are `skills/ui/aiverse-game-screens`.

| Block | Authority | Code | Test |
|---|---|---|---|
| `scriptHud` (+ `scriptHudOptions`) | `docs/script-hud.md` | `modules/rts/view/ScriptHud.h`, `apps/editor/ScriptHudDraw.h` | client and editor preview |
| `worldCounters` | `docs/world-counters.md` | client / editor game view | aiverse_tests world_counter_script_bindings |
| Entity actions | `docs/entity-action-controls.md` | `modules/rts/view/EntityActionControls.h` | region-input paths |

1. Values come from `game.set_var` (already hashed and replayed); strings are localization keys
   (`skills/ui/aiverse-localization`).
2. Buttons fire existing abilities; the simulation validates them. The HUD itself is read-only.
3. Check in the editor game view and the client (`view_screenshot`), on a phone profile.

Not verified (from the docs): Android, Windows and web execution, device touch; entity actions are still being
integrated.

Workflow recipe over existing engine code; no runtime library.
