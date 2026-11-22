# No genre split — stage 08 handoff: C0b, the data model moves to engine/game (2026-11-22)

Branch `feature/aiv20261121_no-genre`. Previous: `docs/handoffs/no-genre-split-stage-07.md`.

**Tested on the Mac (KINGSTON, f181a67e5):** CTest 415/415; template checksums: see the audit row.

## Code
- 4657a4744 merge of origin/main.
- b0700848f the rules data model: `modules/rts/Rts{,Ability,GridField,Mech,Projectile,Raid,Routes,Effects,
  Progression}Data.h`, `RtsModes.h` and the parsers `Rts*Data.cpp` are now `engine/game/{Game,Ability,GridField,Mech,
  ProjectileFlight,Raid,Routes,Effects,Progression,Modes,Fidelity,Originals}Data.*`; the AI controller registry is
  `engine/game/AIControllers.*` (`AIController::update(Simulation&)`; a controller reaches its module through
  `sim.module()`). Every user includes the new paths; no forwarders.
- e2b71cbb6 commands, events and components (Unit, Orders, Combat, Production, Projectile, Buffs, Statuses, Area,
  Needs, Lanes ...) leave `RtsModule.h` for `engine/game/Components.h`.
- 19b2bf1e4 the map grid, HPA* pathfinding and planet surfaces: `RtsMap` / `RtsHpa` / `RtsHpaSphere` / `RtsMapSphere`
  / `RtsSurface` / `RtsSphere` -> `engine/game/MapGrid`, `MapHpa`, `MapHpaSphere`, `MapGridSphere`, `Surface`, `Sphere`.
- f181a67e5 `engine/game/MechData.cpp` listed in `aiverse_game` (the `RtsMech*` glob stopped finding it).

`engine/game` has no genre names (CTest `genre_free_engine`). No behaviour change; saves unchanged.

## Not moved yet (next C0b steps)
The logic on these types is still `RtsModule` member code: stats (`RtsModule::stat`), damage and armor
(`RtsCombat.cpp`), statuses / effects / abilities (`RtsEffects*.cpp`, `RtsAbilities.cpp`), projectiles
(`RtsProjectilePrograms.cpp`), progression perks (`RtsProgression*.cpp`). Moving them means taking them off the
module class first (free functions over the world + rules), then C1..C7.
