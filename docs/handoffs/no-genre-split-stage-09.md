# No genre split — stage 09 handoff: the core, the systems, the presentation and the actor host leave the genre modules (2026-11-22)

Branch `feature/aiv20261121_no-genre`. Previous: `docs/handoffs/no-genre-split-stage-08.md`.

**NOT BUILT YET.** By the owner's order the code is moved first and built / tested once at the end; every commit
below was only syntax-checked file by file. First thing to do: KINGSTON build, CTest, template checksums.

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

## Still to do
- E: one scene document (map `fps` / `extra` blocks, 151 uses in apps/engine).
- G5: camera rigs as modes of one camera component (GameCamera, ChaseCamera, FirstPersonCamera).
- G6: editor without `worldSpaceMaps()` / `actorSessionPlay()` render paths (85 uses), with E.
- D in full: the actor host still runs as its own GameModule ("fps"); one simulation means its systems on GameCore.
- F: data-format names (`"module": "rts"/"fps"`, Lua `rts` table, i18n keys `fps.*`).
- GTA2 migration (separate repository).
