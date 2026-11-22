# 104 — Console, dock back by drag, shape in the Inspector (stage 1 of the editor plan)

## Done
- **Console panel** (`panels/ConsolePanel.tsx`, `consoleLogic.ts`): level toggles with counts (info / warning /
  error), source filter, text filter, copy shown lines, clear, follow-tail. Entity ids in a line
  (`entity:@id`) select and frame the object; project paths (`maps/…`, `content/…`, …) open in Assets. The
  status-bar problem counter opens the Console filtered to errors. Snapshot log rows carry `source`.
- **Dock back by dragging a floating window** over the main window: while its title bar is dragged the main
  window shows a drop hint (`window.dockHover`), releasing over it docks every tab back (native watch timer
  in `windowDidMove`).
- **Inspector**: a Transform edit no longer makes an RTS unit look like an editable built-in cube. Mesh
  Renderer is editable only with a StaticModel; otherwise it shows the type's model or
  `Shape · <name> (built-in)` read-only (as the game and the Scene draw it).

## Verified on the Mac (private copy of lua_lab)
- RTS shapes in the Scene; Rotate ring drag snaps (Y = 75°, 15° step); Inspector shows `Shape · hall`.
- Console toolbar; ⋮ → Open in New Window opens a floating Console window; closing it docks the tab back.
- Not checkable with background tools: dragging a window across displays (drop hint / dock on release).
