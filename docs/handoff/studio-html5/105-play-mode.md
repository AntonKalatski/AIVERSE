# 105 — Play mode (stage 2 of the editor plan)

## Done
- **Live Hierarchy / Inspector while the game runs**: both panels get a *Scene | Live* switch (Live by
  default while playing). Live Hierarchy lists the running world's entities (`studio.gameEntities`, search
  `t:type p:owner c:component #id`, owner dots, kind and hp, polled 600 ms); Live Inspector shows the selected
  entity's components and effective stats (`studio.gameInspect`, 500 ms). Read only; refs belong to one game
  session and are dropped on Stop. Store: `panels/livePlay.ts`; UI: `panels/LivePanels.tsx`.
- **Play tint**: while playing, the title bar gets an accent line and tab strips a tint (`.studio.is-playing`).
- **Play from here**: title-bar button (and ⌥-click on Play) starts the saved map with the Scene view's
  camera (target, distance, pitch, yaw). `studio.gameStart {fromScene}` → `StudioGameLaunch::camera` →
  `HostedClient::setStartCamera` → `GameClient::setStartCamera`, applied once in `fitCamera`.

## Verified on the Mac (private copy of lua_lab)
- Play from here: the game opened with the Scene view's framing; Live Hierarchy listed Town Hall, Gold Mine,
  Peasants, Trees with owners and hp; selecting the Town Hall showed Transform/Owner/Unit/Production/
  Footprint/Buffs live; Stop returned both panels to the map.

## Not done
- *Keep changes* (copy runtime values back to the map): runtime entities carry no stable link to map
  entities yet (no map-id component), so there is nothing reliable to write back to.
