#pragma once

#include <cstdint>

#include "pf_sensors/environment_sensor.hpp"

namespace pf_sensors {

// Today's min/max/avg for one measured quantity (Guild.md 五's environment
// page: "今日 min／max／avg"). Day boundaries are tracked in UTC, matching
// the UTC-only wall-clock decision already made for SNTP in Phase 6.
struct DailyStat {
    float min_value = 0.0F;
    float max_value = 0.0F;
    double sum_value = 0.0;
    std::uint32_t sample_count = 0U;
    bool has_samples = false;
};

struct DailyStats {
    DailyStat temperature_c{};
    DailyStat humidity_percent{};
    std::uint32_t day_index = 0U;
    bool has_day = false;
};

constexpr std::uint32_t epoch_seconds_to_day_index(
    const std::uint64_t epoch_s)
{
    return static_cast<std::uint32_t>(epoch_s / 86400U);
}

inline void record_daily_stat(DailyStat& stat, const float value)
{
    if (!stat.has_samples) {
        stat.min_value = value;
        stat.max_value = value;
        stat.has_samples = true;
    } else {
        stat.min_value = value < stat.min_value ? value : stat.min_value;
        stat.max_value = value > stat.max_value ? value : stat.max_value;
    }
    stat.sum_value += static_cast<double>(value);
    ++stat.sample_count;
}

inline float daily_stat_average(const DailyStat& stat)
{
    return stat.sample_count == 0U
               ? 0.0F
               : static_cast<float>(stat.sum_value / stat.sample_count);
}

// Feeds one valid reading in; resets all stats when epoch_s falls on a
// different UTC day than the one currently being tracked. Callers must
// pass an already-validated reading (see environment_reading_in_range);
// this does not re-validate.
inline void record_daily_reading(
    DailyStats& stats,
    const EnvironmentReading& reading,
    const std::uint64_t epoch_s)
{
    const std::uint32_t day = epoch_seconds_to_day_index(epoch_s);
    if (!stats.has_day || day != stats.day_index) {
        stats = DailyStats{};
        stats.day_index = day;
        stats.has_day = true;
    }
    record_daily_stat(stats.temperature_c, reading.temperature_c);
    record_daily_stat(stats.humidity_percent, reading.humidity_percent);
}

}  // namespace pf_sensors
