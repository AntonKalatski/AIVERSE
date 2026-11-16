---
name: aiverse-weather-timeline
description: Show rain, wind gusts, wet roads and puddles in an AIVERSE client as a pure function of simulation time (seeded showers, soak and dry), with the weather-timeline library; for weather that changes gameplay use the map environment instead.
---

# weather-timeline

Header: `libraries/include/aiverselib/systems/WeatherTimeline.h` (`AIVERSE::lib_weather_timeline`). Tests: `tests/libraries/city_tests.cpp` (the default config reproduces the formula the city sample game shipped with).

1. Decide the layer. Looks only (wet asphalt, rain particles, tree sway, rain sound): this library. Rain that changes fire spread, puddles that slow units, lightning that kills: the map environment (`modules/rts/RtsEnv.h`, `docs/game-format.md` § Map environment).
2. `WeatherTimelineConfig`: `seed`, `slot` (seconds per slot: at most one shower and one gust), shower offset / jitter / duration, `fadeIn` / `fadeOut`, gust timing, `wetRise` / `puddleRise`, `wetDry` / `puddleDry`, `openWet` (the world opens just after a shower), `rainChance`.
3. Every frame: `auto w = weatherAt(sceneSeconds, cfg);` and drive the view from `w.rain`, `w.wind`, `w.wet`, `w.puddles` (0..1). Use the simulation time of the scene, not wall time, so pause, seek, replays and restores show the same sky without any cache.
4. Sky / sun: `weatherDayPhase(tick, dayTicks, startMinute)` (0 = midnight).
5. Check on screenshots at a time inside a shower (`weatherShowerStart(slot)` + half of `weatherShowerDuration(slot)`) and after it dried; shader changes follow `docs/shader-graph-agents.md` (GLSL and MSL together, quality tiers).

Presentation only (double / float); never read it in the simulation.
