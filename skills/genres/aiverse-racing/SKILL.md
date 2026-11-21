---
name: aiverse-racing
description: Make or change an AIVERSE racing game - lap races with a grid and countdown, ordered checkpoints, standings, wrong way and respawn, race bots, chase camera and race HUD - on the FPS module (fps.mode "race") with the vehicle-controllers race session, and check it.
---

# racing

Behaviour and format: `docs/fps.md` § Races. Simulation: `modules/fps/FpsRace.cpp` over the host-independent race
session `libraries/include/aiverselib/genre/RaceSession.h` (dynamics, checkpoints and the race bot: skill
`skills/gameplay/aiverse-vehicle-controllers`). Client: `apps/fps/RaceHud.h` (chase camera, HUD). Sample game:
`templates/racing` (Oval Sprint).

1. Start from the sample: copy `templates/racing` into the game folder (games live outside the engine repository), or
   generate a new oval with `python3 templates/_tools/racegen.py --out <game> [--racers N --bots B --straight M
   --radius M --width M --laps N]`. It writes `rules/race.json` (car, track, race knobs) and `maps/start.json`
   (ground, walls, one typed car per racer); add `game.json` / `rules.json` like the sample (`module` `fps`,
   `fps.mode` `"race"`, `mechanics.enabled`).
2. Track (`fps.vehicleTypes.tracks[]`, milli-units): `checkpoints` in driving order with `centerMilli`, `normalMilli`
   (crossing direction), `radiusMilli` (half the gate width), `respawnMilli` / `respawnYawMilli`; the last checkpoint
   is the finish line and the `grid` lies just after it (one slot per player); `line` = closed racing line for the
   bots (points every ~5 m along the ideal path). Walls are map colliders (boxes; rotated boxes with `yaw` in bends).
3. Cars: `fps.vehicleTypes.vehicles[]` (`kind` wheeled / tracked / hover / flight with a driver seat); map
   `vehicles[i]` with `"type"` is the car of player `i + 1`. Handling numbers: vehicle-controllers skill.
4. Race knobs `fps.race`: `track`, `laps`, `countdownTicks`, `finishGraceTicks`, `timeoutTicks`, `crashDamage`,
   `vehicles` (generated settings section: `wrongWayTicks`, `respawnWrongWayTicks`, `stuckTicks`, `stuckSpeedMilli`,
   bot `botMaxSpeedMilli`, `botCornerAccelMilli`, `botSkillMilli`, `botRubberBandMilli`, ...). Players with
   `"bot": true` race with the race bot (needs `line`).
5. A host other than FPS (top-down, native module): call `makeRaceSession`, then per tick `raceSessionInputs` before
   the vehicles step and `stepRaceSession` after the collisions; teleport `report.race.respawns` with
   `respawnVehicle`; persist the session (record kind 128) and fold `vehicleHash(session)` into the checksum.
6. Check: `aiverse_headless --project <game> --seed 3 --verify-restore-at 30 --verify-restore-ticks 600` ends with a
   winner (a bot when player 1 idles); `aiverse_fps_tests` (`raceMode`) and the vehicle library tests
   (`raceSession`); a screenshot at the wheel: `AIVERSE_SCREENSHOT=/tmp/race.ppm AIVERSE_EXIT_AFTER_FRAMES=400
   aiverse_fps_client --project <game>` (Linux: under Xvfb). Tune bot pace until bots finish without respawns.

Not available yet: car-to-car collisions, car models / engine sounds, pits, penalties, flags, ghosts, track editor.

Runtime library: none of its own (FPS module + `AIVERSE::lib_vehicle_controllers`).
