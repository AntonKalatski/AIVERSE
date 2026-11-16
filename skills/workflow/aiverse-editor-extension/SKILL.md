---
name: aiverse-editor-extension
description: Extend the AIVERSE editor - add a dockable Dear ImGui window, register it for menus, layout and MCP `editor_window`, and expose game-specific grouping in the Hierarchy and the «Детали» inspector through game.json.
---

# Editor windows and the world tree

Use when a feature needs editor UI. Game UI is `skills/ui/aiverse-game-screens`; MCP-only operations without a
window still go through `apps/editor` / `tools/mcp` ops (main agent area in AGENTS.md).

1. A window (`docs/editor-windows.md`): a small class in `apps/editor/windows/`, registered once, with a menu
   `group()`; the editor docks it, saves the layout and lets agents open it with `editor_windows` / `editor_window`.
   Icons: `apps/editor/EditorIcons.h`; theme: `editor_theme`.
2. Keep logic GPU-free in `apps/editor/core` so it is unit-tested and shared with MCP.
3. World tree (`docs/editor-world-tree.md`): the Hierarchy lists every world object
   (`apps/editor/windows/HierarchyWindow.cpp`, `apps/editor/EditorWorld.cpp`); a game adds grouping and row names
   in game.json; MCP `world_entities`, `world_entity`, `editor_select`.
4. Check: `aiverse_editor --project <dir> --play-client --device iphone-se:portrait --window game` for automated
   runs, editor screenshots (`skills/verification/aiverse-visual-qa`).

Workflow recipe over existing editor code and MCP tools; no runtime library.
