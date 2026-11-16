---
name: aiverse-vehicle-fx
description: Light up an AIVERSE vehicle's lamps (headlights, brake lamps with hold, reverse, indicators and hazards, emergency beacon strobes, roof sign) and draw tyre marks from slipping wheels, using the vehicle-fx presentation library.
---

# vehicle-fx

Headers: `libraries/include/aiverselib/genre/vehicle-fx/VehicleLights.h`, `libraries/include/aiverselib/genre/vehicle-fx/TyreTrails.h` (`AIVERSE::lib_vehicle_fx`). Tests: `tests/libraries/city_tests.cpp`. Vehicle state comes from driving (`docs/driving.md`, `VehicleState`) and traffic (`game.traffic_kind` for sirens).

## Lamps
1. Name emissive parts of the model by channel: `headlight`, `tail`, `brake`, `reverse`, `indicator*`, `beacon*` / `strobe`, `roof_sign` (alias `taxi_available`), `destination`. `vehicleLampOf(name)` maps a part name once at load.
2. Keep one `VehicleLights` per vehicle in the view. Each frame: `motion(oldSpeed, speed, signedSpeed, dt, now, brakePedal)`, set `headlights` (night, tunnels), `left` / `right` (`indicateFromSteer` or the AI's turn), `hazards`, `beacons` / `siren` (emergency on a call).
3. `level(lamp, leftSide, time)` is 0 / 1 per part; multiply the part's emissive. `VehicleLightsConfig` tunes blink, brake hold and the strobe double flash.

## Tyre marks
1. One `TyreTrails` for the view. Per vehicle and wheel: `TyreTrails::slipping(speed, yawRate, steer, grounded, drifting, slipMask)`, contact point `wheelContact(pos, forward, halfTrack, halfBase, wheel)`, then `sample(id, point, slipping, now)` with a stable id (entity * 4 + wheel).
2. After sampling: `keepOnly(wheelsSeenThisFrame)`, `expire(now)`. Draw each segment as two quads (soft wide, dark narrow) with alpha `fade(segment, now)`, just above the road surface (spline roads lie above the ground).
3. Clear on map change or when the scene time goes backwards (seek / restore).

Presentation only: never write lamp or trail state into the simulation. Check on night and drift screenshots.
