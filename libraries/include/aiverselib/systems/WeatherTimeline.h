#pragma once
// weather-timeline: weather for the look of a world (rain, wind gusts, wet ground, puddles) as a pure function of
// the simulation time. No state, no cache: pause, seeking, save / restore and replays show the same sky. Showers
// and gusts are placed by a seeded hash in fixed time slots (one shower and one gust per slot by default), with a
// fade in / fade out envelope; the ground gets wet while it rains and dries afterwards; a new world can open just
// after a shower. Presentation only (double / float): never feed it back into the simulation; for weather that
// changes gameplay use the map environment (docs/game-format.md § Map environment).
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace aiverse::lib::systems {

struct WeatherTimelineConfig {
    std::uint32_t seed = 0;
    double slot = 3600;                                 // seconds per slot: at most one shower and one gust each
    double rainOffset = 600, rainJitter = 1200;         // shower start inside its slot: offset + [0, jitter)
    double rainDuration = 90, rainDurationJitter = 91;  // seconds: duration + [0, jitter)
    double fadeIn = 12, fadeOut = 18;                   // envelope of a shower or a gust
    double gustOffset = 2500, gustJitter = 400, gustDuration = 100, gustDurationJitter = 61;
    double wetRise = 45, puddleRise = 80;               // seconds of rain to soak the ground / fill puddles
    double wetDry = 720, puddleDry = 900;               // seconds to dry after the rain
    bool openWet = true;                                // the world starts just after a shower
    float windFromRain = 0.65f;                         // rain brings at least this much wind
    float rainChance = 1.0f;                            // share of slots with a shower (0..1)
};

struct WeatherSample {
    float rain = 0, wind = 0, wet = 0, puddles = 0;     // 0..1
};

namespace weatherDetail {
inline std::uint32_t hash(std::uint32_t n) {
    n ^= n >> 16; n *= 0x7feb352du; n ^= n >> 15; n *= 0x846ca68bu; return n ^ (n >> 16);
}
inline std::uint32_t pick(std::int64_t slot, std::uint32_t salt, std::uint32_t seed) {
    return hash(std::uint32_t(slot) ^ salt ^ (seed ? hash(seed) : 0u));
}
inline double jitter(std::int64_t slot, std::uint32_t salt, const WeatherTimelineConfig& c, double range) {
    const auto n = std::uint32_t(std::max(range, 0.0));
    return n ? double(pick(slot, salt, c.seed) % n) : 0.0;
}
inline float envelope(double age, double duration, const WeatherTimelineConfig& c) {
    return float(std::clamp(std::min(age / std::max(c.fadeIn, 1e-9), (duration - age) / std::max(c.fadeOut, 1e-9)), 0.0, 1.0));
}
} // namespace weatherDetail

inline bool weatherShowerIn(std::int64_t slot, const WeatherTimelineConfig& c = {}) {
    if (c.rainChance >= 1) return true;
    if (c.rainChance <= 0) return false;
    return float(weatherDetail::pick(slot, 0x2c1bu, c.seed) % 10000) < c.rainChance * 10000;
}
inline double weatherShowerStart(std::int64_t slot, const WeatherTimelineConfig& c = {}) {
    return double(slot) * c.slot + c.rainOffset + weatherDetail::jitter(slot, 0x54a3u, c, c.rainJitter);
}
inline double weatherShowerDuration(std::int64_t slot, const WeatherTimelineConfig& c = {}) {
    return c.rainDuration + weatherDetail::jitter(slot, 0x781bu, c, c.rainDurationJitter);
}

inline WeatherSample weatherAt(double seconds, const WeatherTimelineConfig& c = {}) {
    using namespace weatherDetail;
    WeatherSample w;
    if (!std::isfinite(seconds) || seconds < 0 || c.slot <= 0) return w;
    if (c.openWet) {   // drying of the opening shower runs on simulation time: seeking never restarts it
        w.wet = float(std::max(0.0, 1 - seconds / c.wetDry));
        w.puddles = float(std::max(0.0, 1 - seconds / c.puddleDry));
    }
    const auto slot = std::int64_t(seconds / c.slot);
    for (auto i = std::max<std::int64_t>(0, slot - 1); i <= slot; ++i) {
        const double age = seconds - weatherShowerStart(i, c), duration = weatherShowerDuration(i, c);
        if (age >= 0 && weatherShowerIn(i, c)) {
            w.rain = std::max(w.rain, envelope(age, duration, c));
            w.wet = std::max(w.wet, float(age <= duration ? std::min(1.0, age / c.wetRise) : std::max(0.0, 1 - (age - duration) / c.wetDry)));
            w.puddles = std::max(w.puddles, float(age <= duration ? std::min(1.0, age / c.puddleRise) : std::max(0.0, 1 - (age - duration) / c.puddleDry)));
        }
        const double gust = seconds - (double(i) * c.slot + c.gustOffset + jitter(i, 0x193u, c, c.gustJitter));
        w.wind = std::max(w.wind, envelope(gust, c.gustDuration + jitter(i, 0x992u, c, c.gustDurationJitter), c));
    }
    w.wind = std::max(w.wind, w.rain * c.windFromRain);
    return w;
}

// Phase of the day 0..1 (0 = midnight) for sky colour / sun angle, from the tick and the game clock.
inline double weatherDayPhase(std::uint64_t tick, std::uint32_t dayTicks, int startMinute) {
    const auto perDay = std::max<std::uint32_t>(1, dayTicks);
    return std::fmod(double(startMinute) / 1440.0 + double(tick % perDay) / perDay, 1.0);
}

} // namespace aiverse::lib::systems
