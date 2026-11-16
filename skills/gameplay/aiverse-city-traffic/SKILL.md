---
name: aiverse-city-traffic
description: Set up and extend city traffic in an AIVERSE top-down game - lane cars, buses, emergency vehicles, signals and light props, pedestrians, incidents and services, population with homes and shifts, gangs, spline-road traffic - and reuse the city-traffic C++ kernels (road net, signals, driver pedals, sidewalk paths, wide routes and detours) from native modules.
---

# city-traffic

Behaviour and format: `docs/traffic.md`. Simulation: `modules/rts/RtsTraffic.*`, `modules/rts/RtsTrafficCity.cpp` (incidents, services), `modules/rts/RtsTrafficPopulation.cpp` / `modules/rts/RtsTrafficCommute.cpp` (population), `modules/rts/RtsTrafficGangs.cpp`, `modules/rts/RtsTrafficRoads.cpp` (spline roads). Kernels: `libraries/include/aiverselib/genre/Traffic.h` and `libraries/include/aiverselib/genre/traffic/TrafficRoutes.h` (`AIVERSE::lib_city_traffic`). Tests: `aiverse_tests traffic_`, `tests/libraries/city_tests.cpp`.

1. Prerequisites: a top-down hero game (`docs/topdown.md`), vehicles in `driving.vehicles` (`skills/gameplay/aiverse-driving`), roads as tiles listed in `traffic.roads.tiles` or a spline `roadNetwork` (`skills/world/aiverse-roads`, `"roads": {"network": true}`).
2. Rules `traffic`: `cars`, `buses` (stop types or bays), `emergency`, `signals` (+ `lights` prop types with `view.light`), `pedestrians` (walk tiles, `panic`). Loader errors name the key; fix them before anything else.
3. Living city: `services` (ambulance / police / fire / tow: `answers`, `stations`, `max`, `range`; `custody` / `carry` flags), `population` (place kinds with `lives`, `jobs`, `shifts`; map `places`), `gangs`. Add one block at a time and run `aiverse_headless --project <dir> --map maps/<map>.json --seed 3`.
4. Lua (UI / view only in games that keep simulation in C++): `game.traffic_light`, `game.traffic_kind`, `game.traffic_gang`, `game.traffic_citizen`.
5. Native modules: build a net with `buildTrafficNet` (or read the engine's), signals with `trafficLight` / `trafficWalk`, pedals with `trafficDriverInput`, sidewalks with `trafficWalkRoute`; large vehicles: `trafficWideExit` (prefers carriageways of `minWidth`, first and last piece always allowed), blockages: `trafficLocalDetour` (bounded lattice, then the normal collision checks). Give cars pedals through `Driving::setAutopilot`, never by moving them.
6. Checks: same seed, same checksum; maps without `traffic` keep theirs; screenshots of a junction (lights, stop lines) and of a crossing on "walk".

Game-specific type ids or variable names never go into `modules/rts`: put them in rules data or the game's `native/`.
