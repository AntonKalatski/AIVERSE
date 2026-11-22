# No genre split — stage 09 handoff: the core, the systems, the presentation and the actor host leave the genre modules (2026-11-22)

Branch `feature/aiv20261121_no-genre`. Previous: `docs/handoffs/no-genre-split-stage-08.md`.

**Built and tested on the Mac (KINGSTON):** dd20dd57e + ea5411f92 (build fixes): CTest 411/415, template
checksums equal except `boss_arena` seed 2 (known) - the moved core is bit-identical. The 4 failures were checks that
did not know the new paths (native tests, skills, View boundary), fixed in a7030d100 / fe0a4ba94; a7030d100: CTest
414/415 (native_project_build, fixed in fe0a4ba94). E / G6 commits after it: see the next run.

## Code
- aee35ff4c `engine/game/Stats.h` (effectiveStat), `Damage.h` (applyDamage): the generic pipelines, the game's own
  steps passed as inlined hooks.
- f78c226c5 `GameState`, per-player state, mech state, colliders, triggers, routes -> `engine/game`.
- 7550e98e2 `engine/game/GameHost.h` (rules, match state, unit queries / actions, the systems; implemented by the
  core) and `ScriptHooks.h`; the systems move to `engine/game` on it: TopDown, Driving, Arsenal, Traffic (+ City,
  Commute, Gangs, Population, Roads, Priority, Service*), Progression, Raid, Commerce, Career, Encounters, Sensors,
  FieldOps, Environment, Prompts, GridFields.
- cabf37bb9 the simulation core: all of `modules/rts` except Bunker Wars and the presentation -> `engine/game`
  (`RtsModule` -> `GameCore`, `RtsAI` -> `ComputerPlayer`, scripting, `gen/`, `lua/`).
- 468df648b presentation: `modules/rts/view` -> `engine/view` (`RtsView` -> `GameView`, `RtsScene` -> `GameScene`,
  all `Rts*` -> `Game*`), `audio` -> `engine/audio/GameAudio`, `fx` -> `engine/fx/GameFx`; disabled-build stubs ->
  `engine/game/disabled`.
- 5dfc6b8dd actor host: `modules/fps` -> `engine/actor` (`aiverse::fps` -> `aiverse::actor`, `Fps*` -> `Actor*`).
  `genre_free_engine`: no debt left.
- 00eb047cc one game client: `aiverse_fps_client` / `aiverse_voxel_client` removed, `AiverseGame` picks the host by
  game.json "module".

`modules/` now: `rts` = Bunker Wars only (RtsBwAI*, RtsGameExtensions.cpp, disabled/BwAI.cpp) + native-module build
helper; `voxel`, `strategic`, `genres` unchanged.

- ea5411f92 build fixes; a7030d100 game presentation in `engine/view/game` (canonical View check skips it only);
  fe0a4ba94 generated `gen/NativeRegistry.cpp`.
- dd20dd57e G5: `engine/render/CameraModes.h` (overhead / chase / first person) next to the ECS rigs (`CameraRig.h`).
- 3cd855712 E: one scene document - `MapData::fps` removed; a world-space scene is kept whole in `extra` with
  `MapData::worldSpace`; scene nodes / authored lights / preview lighting decide by the document.
- 193a65e4c G6: `Project::worldSpaceMaps()` / `gridMaps()` removed, editor and Studio read the document.

## Still to do
- D in full: the actor host still runs as its own GameModule ("fps"); one simulation means its systems on GameCore.
- F: data-format names (`"module": "game"/"fps"`, Lua `rts` table, i18n keys `fps.*`).
- GTA2 migration (separate repository).
