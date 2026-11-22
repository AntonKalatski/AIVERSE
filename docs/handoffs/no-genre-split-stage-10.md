# No genre split, stage 10 (2026-11-22)

Branch `feature/aiv20261121_no-genre`. origin/main merged first (361 commits: planet flora, NEREID map water,
tornado damage, TerraWeave sparse terrain).

## Done

- **D, one host.** `actor::Module` is no longer a `GameModule`. It is `actor::ActorHost`: the actor systems
  (characters, weapons, projectiles, vehicles, royale, race, bots). `game::GameCore` owns one and hands every entry
  point to it when the map is world-space (`colliders` + `spawns`) (`engine/game/GameActorHost.cpp`). Every
  `Simulation` is built with `GameCore`; consumers use `actor::host(sim)` / `actor::tryHost(sim)`.
  `aiverse_game` links `aiverse_fps`; `aiverse_authority` and `AIVERSE::fps_bots` link `aiverse_game`.
- **F, names in data formats.**
  - game.json `"module"`: `"rts"` -> `"game"`, `"fps"` -> `"actor"`. `GameCore::name()` is `"game"` (replays).
  - Rules block `"fps"` -> `"actor"`; error texts, system names, i18n keys `fps.*` -> `actor.*`.
  - Editor / Studio: ops `actor_get` / `actor_set`, panel / window id `actor`, `Project::actorSession()`. The
    new-project hub lists every template (genre picker removed).
  - Lua native library global `rts` -> `core`; registry keys and the persistent component use `game.*`.
  - Packages: `bots-commander`, `bots-combatant`, `action-bots`, `template-kits`. The 145 `aiverselib/genre`
    forwarders and the `lib::genre` alias are deleted.
- Merge fixes: terraweave_native_micro_compile links `aiverse_runtime` (WorldScene); main code moved to
  `GamePlanetFlora`, `GameWaterNereid`; `GameCore::environmentDamage`.

## Breaks (owner rule: allowed)

Old projects (`"module": "rts"/"fps"`, rules `"fps"`), replays (module `"rts"`), saves with
`rts.authored_pose`, Lua scripts that call `rts.*` directly, and editor layouts with panel `fps` no longer load.
External repositories (GTA2, Kingston) must rename their game.json / rules the same way.

## Left

- Board, Motion (`modules/genres`), Voxel, Strategic are separate `GameModule` hosts.
- GTA2 migration (separate repository).
- `tools/libraries/catalog.py check` already failed before this stage (unknown package status).

## Tests

See the table below (filled after the Mac run).
