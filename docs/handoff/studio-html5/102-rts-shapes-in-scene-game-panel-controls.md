# 102 — RTS shapes in the Scene, Game panel controls, centred play controls

Branch `feature/studio-html5`.

## Done
- **Procedural RTS shapes in the Scene.** RTS types without a model file (`"view": {"shape": "tree"}`, e.g.
  the Iron and Gold templates) used to be grey proxy boxes in the Studio Scene. The shape builder moved
  out of `RtsView` into `game::buildRtsShapeModels()` (`modules/rts/view/RtsShapeModels.h`), used by both the
  game view (uploads into its resources as before) and `StudioMetalScene` (built once, uploaded with CPU
  copies for picking). Placement follows the game: structures stretch over their footprint
  (`sx, max(sx,sz)·0.75, sz` × `view.scale`), units take `view.scale` and the facing yaw; team parts are tinted
  by the owner. Entities with a StaticModel component (built-in primitives) keep their own visual.
  Projects without `content/` now still show shapes.
- **Game panel**: removed the duplicate Play / Pause / Step / Stop (they are in the title bar, ⌘P); the panel
  keeps *Project menu* and *Restart*. A device profile now shows a device frame around the screen area.
- **Title bar**: Play / Pause / Step are in the exact centre (static below 1180 px wide).

## Findings (not changed)
- "Fire at start" in Iron and Gold: Scripted (`lua_lab`): the enemy AI casts the player ability *Ignite*
  (`smart: densestEnemy`, `RtsAI.cpp`) on your base after ~30 s; the glowing points in the Scene are the
  map's authored lights (lanterns, campfire, searchlight).
- Publish workspace: only the panel layout exists; the build / branding / asset-audit windows are still
  legacy-only (listed under Window → Legacy editor).
- Game HUD at iPhone landscape (874×402, touch): the `#card` (340dp) inline-block cells shrink to a word per
  line and button labels overlap — engine `theme.rcss` touch layout, needs a repro outside Studio.

## Verified
- tsc, test:web, tools/studio contracts; Mac rebuild: BUILD 0, session tests PASS, RTS pose test PASS.
- Not visually verified (the screen tools only see another agent's Studio window).
