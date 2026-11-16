#pragma once
// world-calendar: a game clock made of simulation ticks. Days, minutes, weekdays, work shifts (overnight ones
// included), staggered work weeks, waking hours, needs that grow with the time since they were last met, and a
// daily draw of people for short errands. Integers only: every function is a pure function of the tick and the
// caller's saved values, so pause, seeking, save / restore and replays give the same answers.
//
// Conventions: a day has 1440 minutes; `dayTicks` simulation ticks make one game day; the match starts at
// `startMinute` of day 0, which is weekday `startWeekday` (0 = the first working day of the week). A shift
// [from, to) with from > to runs over midnight and belongs to the day it starts on.
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace aiverse::lib::systems {

inline constexpr int kMinutesPerDay = 1440;

struct CalendarConfig {
    std::uint32_t dayTicks = 1440;   // simulation ticks per game day (>= 1)
    int startMinute = 8 * 60;        // minute of day 0 at tick 0
    int startWeekday = 0;            // weekday of day 0, 0..6
    int workDays = 5;                // working days at the start of each week, 0..7
};

struct CalendarTime {
    std::int64_t day = 0;     // game day since the start (can grow past a week)
    int minute = 0;           // 0..1439
    int weekday = 0;          // 0..6
    bool operator==(const CalendarTime&) const = default;
};

inline int calendarWeekday(std::int64_t day, int startWeekday = 0) {
    return int(((day + startWeekday) % 7 + 7) % 7);
}

// Minutes since minute 0 of day 0. tick * 1440 / dayTicks, floor.
inline std::int64_t calendarAbsoluteMinute(std::uint64_t tick, const CalendarConfig& c) {
    const std::uint64_t perDay = std::max<std::uint32_t>(c.dayTicks, 1);
    const std::uint64_t days = tick / perDay, rest = tick % perDay;   // split: no overflow for long matches
    return std::int64_t(days) * kMinutesPerDay + std::int64_t(rest * kMinutesPerDay / perDay) + c.startMinute;
}

inline CalendarTime calendarAt(std::uint64_t tick, const CalendarConfig& c) {
    const auto absolute = calendarAbsoluteMinute(tick, c);
    const auto day = absolute / kMinutesPerDay;
    return {day, int(absolute % kMinutesPerDay), calendarWeekday(day, c.startWeekday)};
}

// First tick at or after which the clock shows `absoluteMinute` (the inverse of calendarAbsoluteMinute).
inline std::uint64_t calendarTickOf(std::int64_t absoluteMinute, const CalendarConfig& c) {
    const std::int64_t since = std::max<std::int64_t>(0, absoluteMinute - c.startMinute);
    const std::uint64_t perDay = std::max<std::uint32_t>(c.dayTicks, 1);
    const std::uint64_t days = std::uint64_t(since / kMinutesPerDay), rest = std::uint64_t(since % kMinutesPerDay);
    return days * perDay + (rest * perDay + kMinutesPerDay - 1) / kMinutesPerDay;
}

struct WorkShift {
    int from = 9 * 60, to = 17 * 60;   // minutes of day; from > to: over midnight
    bool operator==(const WorkShift&) const = default;
};
inline bool overnight(const WorkShift& s) { return s.from > s.to; }
inline int shiftLength(const WorkShift& s) { return overnight(s) ? kMinutesPerDay - s.from + s.to : s.to - s.from; }

// The day a moment belongs to for this shift: after midnight, before the end of an overnight shift, it is
// still the previous day's shift.
inline std::int64_t shiftDay(std::int64_t day, int minute, const WorkShift& s) {
    return day - (overnight(s) && minute < s.to ? 1 : 0);
}

// Does `person` work on `day`? A fixed week works the first `workDays` weekdays. A rotating week (shops,
// services open every day) shifts each person's days off by person % 7, so the staff is spread over the week.
inline bool workDay(std::int64_t day, std::int64_t person, bool rotating, const CalendarConfig& c = {}) {
    const int weekday = calendarWeekday(day, c.startWeekday);
    const int slot = rotating ? int((weekday + ((person % 7) + 7) % 7) % 7) : weekday;
    return slot < std::clamp(c.workDays, 0, 7);
}

inline bool onShift(std::int64_t day, int minute, const WorkShift& s, std::int64_t person, bool rotating,
                    const CalendarConfig& c = {}) {
    if (s.from == s.to) return false;
    for (int offset = -1; offset <= 0; ++offset) {
        const std::int64_t start = std::int64_t(offset) * kMinutesPerDay + s.from;
        const std::int64_t end = start + shiftLength(s);
        if (minute >= start && minute < end && workDay(day + offset, person, rotating, c)) return true;
    }
    return false;
}

// Minutes until the person's next shift starts: 0 while on shift, -1 if he never works within `horizonDays`.
inline std::int64_t minutesUntilShift(std::int64_t day, int minute, const WorkShift& s, std::int64_t person,
                                      bool rotating, const CalendarConfig& c = {}, int horizonDays = 8) {
    if (onShift(day, minute, s, person, rotating, c)) return 0;
    if (s.from == s.to) return -1;
    for (int offset = 0; offset <= horizonDays; ++offset) {
        const std::int64_t start = std::int64_t(offset) * kMinutesPerDay + s.from;
        if (start >= minute && workDay(day + offset, person, rotating, c)) return start - minute;
    }
    return -1;
}

// Waking hours. Day people are up from `wake` to `sleep`; after an overnight shift a person sleeps until
// `restAfterNight` minutes past its end, then stays up until `sleep`.
struct DayRhythm {
    int wake = 8 * 60, sleep = 22 * 60, restAfterNight = 6 * 60;
};
// Minutes of waking time left now; 0 while asleep.
inline int awakeMinutesLeft(int minute, const WorkShift& s, const DayRhythm& r = {}) {
    if (overnight(s)) return minute < s.to + r.restAfterNight ? 0 : std::max(0, r.sleep - minute);
    return minute >= r.wake && minute < r.sleep ? r.sleep - minute : 0;
}

// A need is the time since it was last met, not a separate meter: urgency in per mille of its period
// (1000 = one full period overdue), clamped to [0, cap]. Persist only the `satisfied` minute.
inline int needUrgency(std::int64_t nowMinute, std::int64_t satisfiedMinute, std::int64_t periodMinutes, int cap = 2000) {
    if (periodMinutes <= 0) return cap;
    return int(std::clamp<std::int64_t>((nowMinute - satisfiedMinute) * 1000 / periodMinutes, 0, cap));
}
// Index of the most urgent need; ties go to the lower index. -1 for none or when all are below `threshold`.
inline int mostUrgent(std::span<const int> urgency, int threshold = 0) {
    int best = -1;
    for (int i = 0; i < int(urgency.size()); ++i)
        if (urgency[size_t(i)] >= threshold && (best < 0 || urgency[size_t(i)] > urgency[size_t(best)])) best = i;
    return best;
}

// Daily errands: once a day, one in `oneIn` people is drawn with a due minute in [from, to]; past due minutes of
// today are skipped. `rng` is the simulation's (below(n), range(lo, hiInclusive)); the draw order is the
// population order, so the result depends only on the RNG state and the population size.
struct DailyErrand {
    std::int64_t person = 0;
    int due = 0;               // minute of the day
    bool started = false;
    bool operator==(const DailyErrand&) const = default;
};
struct DailyErrandConfig {
    int oneIn = 6;               // chance 1 / oneIn per person and day (>= 1)
    int from = 8 * 60, to = 19 * 60;
    int maxPerDay = 24;          // started errands per day
    int maxActive = 4;           // running at the same time
};
struct DailyErrands {   // save all of it (NativeState / components)
    std::int64_t day = -1;
    int startedToday = 0;
    std::vector<DailyErrand> pending;
    bool operator==(const DailyErrands&) const = default;
};
template <class Rng>
void planDailyErrands(DailyErrands& d, std::int64_t day, int minute, std::size_t population,
                      const DailyErrandConfig& c, Rng& rng) {
    if (d.day == day) return;
    d.day = day; d.startedToday = 0; d.pending.clear();
    const auto oneIn = std::uint32_t(std::max(c.oneIn, 1));
    for (std::size_t i = 0; i < population; ++i)
        if (rng.below(oneIn) == 0) {
            const int due = int(rng.range(std::min(c.from, c.to), std::max(c.from, c.to)));
            if (due >= minute) d.pending.push_back({std::int64_t(i), due, false});
        }
}
// Errands due now that may start, in pending order, respecting both caps. The caller tries each one (the
// person may be busy) and calls markStarted only for those it really started.
inline std::vector<std::size_t> dueErrands(const DailyErrands& d, int minute, int active, const DailyErrandConfig& c) {
    std::vector<std::size_t> out;
    int room = std::min(c.maxActive - active, c.maxPerDay - d.startedToday);
    for (std::size_t i = 0; i < d.pending.size() && room > 0; ++i)
        if (!d.pending[i].started && d.pending[i].due <= minute) { out.push_back(i); --room; }
    return out;
}
inline void markStarted(DailyErrands& d, std::size_t index) {
    if (index < d.pending.size() && !d.pending[index].started) { d.pending[index].started = true; ++d.startedToday; }
}
// Give up an errand without starting it (person busy, no place to go): it is not offered again today.
inline void markSkipped(DailyErrands& d, std::size_t index) {
    if (index < d.pending.size()) d.pending[index].started = true;
}

} // namespace aiverse::lib::systems
