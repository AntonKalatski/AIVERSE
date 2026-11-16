---
name: aiverse-driving
description: Let the top-down hero of an AIVERSE game get into and drive vehicles - rules driving (vehicles, controls, crashes, run-over), per-vehicle handling from a table, steering wheels in the view, bridges and tunnels as decks, computer drivers through the autopilot - and check it.
---

# driving

Behaviour and format: `docs/driving.md`. Simulation: `modules/rts/RtsDriving.*` (dynamics: `libraries/include/aiverselib/genre/VehicleControllers.h`, `skills/gameplay/aiverse-vehicle-controllers`). Client camera: `apps/client/ClientTopDown.cpp`. Tests: `aiverse_tests driving_`.

1. Needs a top-down hero game (`docs/topdown.md`, `skills/gameplay/aiverse-special-modes`).
2. Rules `driving`: `metersPerCell`, `enterRange`, `exitSpeed`, `controls` (`tank` for keyboard / gamepad, `stick` for touch), `crash`, `runOver`, `wreckDamage`, `doorTime`, and `vehicles.<type>` with `length`, `width` (cells) and the library's `wheeled` keys. Vehicle types are ordinary units placed by the map or spawned.
3. Handling per vehicle: `python3 tools/citygen/vehicle_handling.py <project>` writes `wheeled` from its table and the wheel positions from `wheel_<axle>_<side>` model nodes, plus the `steer` / `wheel` procedurals. Library defaults are a road car; a bus or a lorry needs its own numbers.
4. Wheels in the view: procedural `"steer"` before `"wheel"` on the front wheel nodes; mirrored left wheels need the opposite spin axis.
5. Levels: map `decks` (bridges over roads, tunnels: `path` with heights in metres, `width`); keep both mouths passable and other roads two cells away from them.
6. Computer drivers: `Driving::setAutopilot` / `clearAutopilot` from a system or script effect (simulation state). City traffic builds on it (`skills/gameplay/aiverse-city-traffic`).
7. Check: headless with rules `"ai": {"controller": "scripted"}` steers the hero too (measure turn rate, top speed, reverse); same seed, same checksum; maps without vehicles keep theirs; screenshot at the wheel (camera follows the car). Lamps and tyre marks: `skills/visuals/aiverse-vehicle-fx`.

Runtime library: none (engine module). Workflow over existing engine code.
