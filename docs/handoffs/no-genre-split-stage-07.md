# No genre split — stage 07 handoff: C0a, the GTA2 trigger-variable protocol on ECS (2026-11-22)

Branch `feature/aiv20261121_no-genre`. Previous: `docs/handoffs/no-genre-split-stage-06.md`.

**Tested on the Mac (KINGSTON, 0783d0313):** CTest 409/410 (`skill_audit` only, fixed since in 7f2863442);
template checksums equal except `boss_arena` seed 2 (known non-determinism from main).

## Code
- d301cfab2 merge of origin/main (StudioMetalScene RTSCamera -> Camera, studio `isFps()` -> `worldSpaceMaps()`).
- 0ac095413 custody / carry state on ECS: `modules/rts/RtsServiceState.h` — CustodyCar, InCustody, StandIn,
  ServicePassenger, CarryCar, VehicleSignals, VehicleDoor, ServiceHold, ServiceCrew, CrossingWait (RtsDriving.h)
  and the ServiceLedger resource; registered as persistent ECS by `Traffic::setup` (games with traffic only, so
  no template checksum moves). The custody guard (vehicle destroyed: prisoner free) is engine code.
- 0783d0313 the rest: TrafficCitizen `held` / `off` / `journey`, TopDownPilot `controlHeld`, ArsenalHand aim
  fields, TrafficStore `busDrivers` / `buses`; data instead of ids: `traffic.incidents.bodies`,
  `traffic.services.<id>.wards`, `traffic.pedestrians.grace`, weapon `attackLight`, arsenal weapon `windup`;
  Lua `services` table (`RtsServicesApi.cpp`, docs/scripting.md).
- 7f2863442 `skills/editor/studio-ui` names the handoff folder instead of a glob (CTest `skill_audit` green).

## Behaviour changes
- A fire crew finishes its work at the scene on its own; a game that drives it from a script sets
  `services.hold(car, true)` (was: fire crews always waited for the GTA2 controller).
- The player medical sync (`medical_<p>_*`) left the engine: GTA2 follows `CarryCar` / `services.carried`.
- No trigger variable of these names is written by the engine any more: `police_*`, `ambulance_*`,
  `resident_transport_*`, `vehicle_beacon_*`, `vehicle_siren_*`, `drive_door_*`, `drive_<p>_phase`,
  `service_exit_*`, `service_return_*`, `fire_service_*`, `tow_*`, `resident_journey_*`, `resident_schedule_off_*`,
  `resident_visit_active_*`, `resident_transit_available`, `crossing_*`, `kingston_aim_*`, `traffic_bus*`,
  `dispatch_reused`, `npc_body_*` (type naming).

## Saves that break
All saves of games with traffic (new persistent components), top-down saves (pilot `controlHeld`),
arsenal saves (hand aim fields).

## GTA2 — not migrated yet (it does not build against this branch)
Branch `feature/aiv20261121_no-genre` in GTA2, worktree `/Volumes/KINGSTON/gta2-worktrees/no-genre`, no changes yet.
To do: retire the five `prepare_engine.py` scripts (police, ambulance, tow-service, building-fire,
resident-traffic) that patch engine sources at configure time; move ~30 scripts / native modules to the
components and `services` API; data patch `_tmp/gta2-c0a-data.py` plus `bodies`, `wards`, `grace`, `attackLight`,
`windup`, tow gate rectangle [118, 120.25, 121, 126.25] in `traffic.noParking`. Build with
`build_city.py --no-publish --build-root <KINGSTON dir>` (never publish over `_build/KingstonCity.app`).
