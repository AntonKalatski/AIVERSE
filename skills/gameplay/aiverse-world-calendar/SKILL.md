---
name: aiverse-world-calendar
description: Give an AIVERSE game a clock of days, weekdays and work shifts from simulation ticks - daily routines, night shifts, staggered work weeks, waking hours, needs as time since last met, daily errands - with the world-calendar library instead of hand-written minute arithmetic.
---

# world-calendar

Header: `libraries/include/aiverselib/systems/Calendar.h` (`AIVERSE::lib_world_calendar`, native modules: `aiverse_native_use_libraries(<target> world-calendar)`). Tests: `tests/libraries/city_tests.cpp`. The traffic population (`docs/traffic.md` § Population) has its own clock keys (`dayTicks`, `startMinute`): build a `CalendarConfig` from them so a native module and the engine agree on the time.

1. One `CalendarConfig` per game (`dayTicks`, `startMinute`, `startWeekday`, `workDays`). `calendarAt(tick, c)` gives day / minute / weekday; `calendarTickOf(minute, c)` the first tick showing a minute (schedule wake-ups instead of polling).
2. Shifts: `WorkShift{from, to}` in minutes; `from > to` runs over midnight and belongs to the day it started (`shiftDay`). `workDay(day, person, rotating)`: fixed weeks or a rotating week that spreads days off by person id. `onShift`, `minutesUntilShift` (-1: never within the horizon).
3. Waking hours: `awakeMinutesLeft(minute, shift, DayRhythm)`; night workers sleep `restAfterNight` after their shift.
4. Needs: store only the minute a need was last met; `needUrgency(now, satisfied, period)` is per mille of the period, `mostUrgent` picks one (ties: lower index). No decaying meters to save.
5. Daily errands: `planDailyErrands(state, day, minute, population, config, sim.rng())` once per tick is enough (it acts once per day); `dueErrands` respects `maxPerDay` / `maxActive`; call `markStarted` only for errands that really started, `markSkipped` for those given up. Save `DailyErrands` in NativeState.
6. Integers only; draw with the simulation RNG in a fixed order; never use wall time. Re-run save / restore and replay checks after wiring it into a module.

Not a scheduler: the owner moves people and decides what an errand is. Presentation of the time of day (sky, lamps): `skills/visuals/aiverse-weather-timeline` (`weatherDayPhase`).
