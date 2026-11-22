# No genre split — stage 04 handoff: stage B, one vehicle system (2026-11-21)

Branch `feature/aiv20261121_no-genre` (written in the cloud branch `feature/aiv20261121_no-genre-cloud`).
Plan and progress table: `docs/no-genre-split-audit.md`. Previous: `docs/handoffs/no-genre-split-stage-03.md`.

## Code
- d31a3e46e merge of origin/main (main's fix of `aiverse_rts_authored_pose_test` linking taken; our temporary
  exclusion removed).
- dedd54a9d `engine/vehicles` (`AIVERSE::vehicles`, linked by `aiverse_game` and `aiverse_fps`):
  - `Vehicles.h/.cpp`: components `vehicles::VehicleDrive` (type, controller state, driver, player, doorUntil,
    locked, deck, autopilot + pilot, seat list, per-tick requests) and `vehicles::VehicleOccupant` (on the driver);
    `VehicleType` / `DriveRules` (from rules `driving` via `parseDriving`, and from `vehicleTypes` on the FPS host);
    one step `stepVehicles` in ascending entity index (pedals race > driver > host bot > autopilot > parked; parked
    empty vehicles at rest sleep; controller step; host sweep; crash / run-over through `VehicleImpacts`);
    host interface `VehicleHost` (active, driverPedals, botPedals, parked, controllable, ground, sweep, placed,
    damage); API setAutopilot / clearAutopilot / setLocked / isLocked / speedOf / vehicleOf / vehicleEntities /
    addVehicle / seatDriver / unseatDriver / stickPedals; `hashVehicles` (old RTS byte layout).
  - `VehicleRace.h/.cpp`: the one race adapter `VehicleRace` with state in the `RaceState` world resource; hosts
    implement `RaceHost` (placeOnGrid, seat, pedals, wantsRespawn, respawned, raceOver).
  - RTS host `RtsDriving`: only the grid-map world query (three-circle sweep vs grid, units, colliders, decks in a
    `DrivingDecks` resource), cells<->metres, hero stick, `kCmdTopDownEnter` door rules, adoption of vehicle units,
    race seats. Deleted: `DrivingStore`, `DrivingCar`, `DrivingRules`, race hooks, `RtsRace.h`. RTS pools gained
    `VehicleDrive`, `VehicleOccupant`.
  - FPS host `FpsVehicles.cpp`: metre-map world query (collision casts, groundBelow, box sweep), seat-list glue,
    race seats. Deleted: `FpsRace.cpp`, `fps::VehicleDrive`, `CharacterRacer`, `State::race`.
  - Road forwarders `modules/rts/RtsRoadNet.h`, `view/RtsRoadView.h`, `view/RtsRoadKit.h` and the
    `game::RtsRoadView` alias deleted; code includes `engine/roads` (`aiverse::roads`).
- a008c6e16 docs (driving.md, races.md, fps.md), skills (aiverse-driving, aiverse-vehicle-controllers,
  aiverse-racing, aiverse-city-traffic), AGENTS.md, audit table.

## Verification (Mac Studio, worktree `/Volumes/KINGSTON/aiverse-worktrees/no-genre`, build dir
`/Volumes/KINGSTON/aiverse-build/no-genre-k`, commit a008c6e16)
- Build: PASS, all targets incl. editor.
- CTest: 365/367 PASS (`aiverse_tests` 1213 s incl. `driving_*`, `traffic_*`, `city_*`; `fps` incl. `typedVehicles`
  and `raceMode`; `fps_transport`, `world_async`, `skill_library_isolation`). Failures, both from main:
  `skill_audit` (studio-ui placeholder path `docs/handoff/studio-html5/NN-*.md`) and `rts_authored_pose_test`
  ("authored entities not spawned"; main fixed the test after our merge in 59b415a49 / ae284d2b7 - comes in with the
  next main merge).
- Template checksums (`tests/checksums/check.sh`, seeds 1-3): 95/96 identical to `tests/checksums/baseline.txt`,
  including `road_race` (the only template with `driving` and a race) - the vehicle unification kept its
  checksums. The only difference is `boss_arena` seed 2, main's own nondeterminism (stage 02 handoff).

## Behaviour / save changes
- Old RTS saves of every game no longer load (two new pools); FPS saves with typed vehicles or a race neither.
- FPS: hash uses the shared vehicle layout + race state; crash damage `((impact-8)*10)` rounded at the end; empty
  typed vehicles at rest sleep; seats of all vehicles are processed before all vehicles step; after a race ends
  players may leave cars and non-racers can take crash damage on race maps.
- Traffic scans no longer see cars whose entity is already dead.

## Left for the next stages
- Rules `driving` (grid maps) and `vehicleTypes` (FPS) are still two blocks with two parsers into one `DriveRules`;
  getting in on foot keeps two seat models (driver's door vs seat list). Unify with stage D (one simulation).
- FPS drivers have no `VehicleOccupant` (they use the character's `vehicle` id).
- Editor hub groups templates by `genre=="fps"` (racing templates land on the wrong tab).
- `docs/fps.md` save version text.
- Next stage per plan: C - move the remaining `modules/rts`-only systems to `engine/<capability>` (roads model+view
  are already in `engine/roads`; next: direct control `RtsTopDown`, traffic `RtsTraffic*`, raids, commerce /
  careers / arsenal, encounters, progression, sensors / flow economy, map generator + city planner, campaign,
  side-commander bot), one or a few per stage, deleting the module copy.
