#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>

namespace pf_sensors {

// Mirrors Guild.md 4.8's environment sensor state model.
enum class SensorStatus : std::uint8_t {
    disabled,
    probing,
    online,
    stale,
    not_detected,
    error,
};

constexpr const char* to_string(const SensorStatus status)
{
    switch (status) {
        case SensorStatus::disabled:
            return "disabled";
        case SensorStatus::probing:
            return "probing";
        case SensorStatus::online:
            return "online";
        case SensorStatus::stale:
            return "stale";
        case SensorStatus::not_detected:
            return "not_detected";
        case SensorStatus::error:
            return "error";
    }
    return "disabled";
}

struct EnvironmentReading {
    float temperature_c = 0.0F;
    float humidity_percent = 0.0F;
};

// DHT22/AM2302 datasheet range (see docs/adr/0006-sensor-drivers-and-presence.md).
inline constexpr float kMinTemperatureC = -40.0F;
inline constexpr float kMaxTemperatureC = 80.0F;
inline constexpr float kMinHumidityPercent = 0.0F;
inline constexpr float kMaxHumidityPercent = 100.0F;

constexpr bool environment_reading_in_range(
    const EnvironmentReading& reading)
{
    return reading.temperature_c >= kMinTemperatureC &&
           reading.temperature_c <= kMaxTemperatureC &&
           reading.humidity_percent >= kMinHumidityPercent &&
           reading.humidity_percent <= kMaxHumidityPercent;
}

// probe()/read() contract from Guild.md 4.8's suggested EnvironmentSensor
// interface.
class EnvironmentSensor {
public:
    virtual ~EnvironmentSensor() = default;
    virtual SensorStatus probe() = 0;
    virtual bool read(EnvironmentReading& output) = 0;
};

class NullEnvironmentSensor final : public EnvironmentSensor {
public:
    SensorStatus probe() override
    {
        return SensorStatus::not_detected;
    }

    bool read(EnvironmentReading&) override
    {
        return false;
    }
};

// Pure translation from "did the underlying driver call succeed, and is
// the value plausible" to the Guild.md 4.8 status model. Kept separate
// from any concrete driver (e.g. pf_sensors::Dht22EnvironmentSensor) so the
// mapping itself stays host-testable even though the actual GPIO read
// is not.
constexpr SensorStatus classify_environment_read(
    const bool read_succeeded,
    const EnvironmentReading& reading)
{
    if (!read_succeeded) {
        return SensorStatus::not_detected;
    }
    return environment_reading_in_range(reading) ? SensorStatus::online
                                                   : SensorStatus::error;
}

enum class EnvironmentFailure : std::uint8_t {
    none,
    not_detected,
    out_of_range,
    read_error,
};

constexpr const char* to_string(const EnvironmentFailure failure)
{
    switch (failure) {
        case EnvironmentFailure::none:
            return "none";
        case EnvironmentFailure::not_detected:
            return "not_detected";
        case EnvironmentFailure::out_of_range:
            return "out_of_range";
        case EnvironmentFailure::read_error:
            return "read_error";
    }
    return "none";
}

// Cache/backoff shape deliberately mirrors pf_weather::Cache (see
// components/pf_weather/include/pf_weather/weather.hpp); the same design
// already shipped and was codex-cowork reviewed in Phase 6, so Phase 7
// reuses it instead of inventing a second scheme.
struct EnvironmentCache {
    EnvironmentReading reading{};
    bool has_reading = false;
    std::uint64_t last_success_epoch_s = 0U;
    std::uint64_t next_attempt_ms = 0U;
    std::uint32_t consecutive_failures = 0U;
    EnvironmentFailure last_failure = EnvironmentFailure::none;
};

// DHT22's datasheet floor is a 2s sampling period; polling far above that
// avoids self-heating error and bus noise accumulation from back-to-back
// reads.
inline constexpr std::uint64_t kEnvironmentUpdateIntervalMs =
    120U * 1000U;
inline constexpr std::uint64_t kEnvironmentInitialRetryMs = 10U * 1000U;
inline constexpr std::uint64_t kEnvironmentMaximumRetryMs =
    30U * 60U * 1000U;
// Matches pf_weather::kDefaultCacheMaxAgeSeconds. A single failed read (or a
// short run of them while the retry backoff climbs) used to make the whole
// indoor block disappear for however long the next carousel refresh was away
// -- up to the full 30-minute default interval -- even though the sensor
// typically recovers within a minute or two. Widening this to match the
// outdoor weather tolerance means a display refresh landing mid-hiccup still
// has a reading to show; environment_reading_current()'s stale flag (see
// below) is what keeps that reading from being presented as if it were
// fresh.
inline constexpr std::uint64_t kEnvironmentDefaultCacheMaxAgeSeconds =
    3600U;

// Callers must pass an already-validated reading (see
// environment_reading_in_range); this does not re-validate.
inline void record_environment_success(
    EnvironmentCache& cache,
    const EnvironmentReading& reading,
    const std::uint64_t success_epoch_s,
    const std::uint64_t now_ms,
    const std::uint64_t interval_ms = kEnvironmentUpdateIntervalMs)
{
    cache.reading = reading;
    cache.has_reading = true;
    cache.last_success_epoch_s = success_epoch_s;
    cache.next_attempt_ms = now_ms > std::numeric_limits<std::uint64_t>::max() - interval_ms
                                 ? std::numeric_limits<std::uint64_t>::max()
                                 : now_ms + interval_ms;
    cache.consecutive_failures = 0U;
    cache.last_failure = EnvironmentFailure::none;
}

inline void record_environment_failure(
    EnvironmentCache& cache,
    const EnvironmentFailure failure,
    const std::uint64_t now_ms)
{
    cache.consecutive_failures =
        cache.consecutive_failures >= 31U ? 31U : cache.consecutive_failures + 1U;
    const std::uint32_t shift =
        std::min<std::uint32_t>(cache.consecutive_failures - 1U, 6U);
    const std::uint64_t delay = std::min<std::uint64_t>(
        kEnvironmentMaximumRetryMs, kEnvironmentInitialRetryMs << shift);
    cache.next_attempt_ms =
        now_ms > std::numeric_limits<std::uint64_t>::max() - delay
            ? std::numeric_limits<std::uint64_t>::max()
            : now_ms + delay;
    cache.last_failure = failure;
}

inline bool environment_retry_due(
    const EnvironmentCache& cache,
    const std::uint64_t now_ms)
{
    return now_ms >= cache.next_attempt_ms;
}

inline bool environment_stale(
    const EnvironmentCache& cache,
    const std::uint64_t now_epoch_s,
    const std::uint64_t max_age_seconds = kEnvironmentDefaultCacheMaxAgeSeconds)
{
    if (!cache.has_reading || cache.last_success_epoch_s == 0U ||
        max_age_seconds == 0U || now_epoch_s < cache.last_success_epoch_s) {
        return true;
    }
    return (now_epoch_s - cache.last_success_epoch_s) > max_age_seconds;
}

// Whether the cached reading may be presented as the sensor's current
// value.
//
// Age alone is not enough. A single failed read already proves the sensor
// is not answering, and from that moment the cached value is history --
// serving it as `online` (and with stale=false) would be exactly the
// "沿用歷史值" the optional-sensor contract forbids. Measured on hardware
// 2026-08-23: unplugging a DHT22 that had just read successfully left the
// API reporting `online` with the pre-unplug 26.8C/64.2% on every tick
// that did not happen to be a retry tick, for the whole five-minute cache
// window.
//
// Both the status the sensor task publishes and the `stale` flag the API
// serialises must come from this one predicate, or they can contradict
// each other.
inline bool environment_reading_current(
    const EnvironmentCache& cache,
    const std::uint64_t now_epoch_s,
    const std::uint64_t max_age_seconds = kEnvironmentDefaultCacheMaxAgeSeconds)
{
    return cache.has_reading && cache.consecutive_failures == 0U &&
           !environment_stale(cache, now_epoch_s, max_age_seconds);
}

// The status to report on a tick where no read was attempted.
inline SensorStatus environment_cached_status(
    const EnvironmentCache& cache,
    const std::uint64_t now_epoch_s,
    const std::uint64_t max_age_seconds = kEnvironmentDefaultCacheMaxAgeSeconds)
{
    if (!cache.has_reading) {
        return SensorStatus::probing;
    }
    return environment_reading_current(cache, now_epoch_s, max_age_seconds)
               ? SensorStatus::online
               : SensorStatus::stale;
}

// Whether the status bar should show the cached indoor reading, and whether
// it should be marked stale while doing so.
//
// `EnvironmentCache::has_reading` is sticky: record_environment_success()
// sets it once and nothing ever clears it back to false for the rest of the
// boot cycle, even if the sensor is later disabled in settings or never
// answers again. Gating display on `has_reading` alone would therefore keep
// showing a stale-marked reading forever after either of those -- exactly
// the "沿用歷史值" the optional-sensor contract forbids, just delayed
// instead of avoided. Restricting display to the `online`/`stale` statuses
// (never `disabled`, `not_detected` or `probing`) is what makes "sensor
// briefly stopped answering" (show cached value, mark it stale -- the
// 2026-09-03 fix) and "sensor turned off/never worked" (hide entirely --
// unchanged from before that fix) both come out correctly from the same
// cache state.
struct EnvironmentDisplayDecision {
    bool available = false;
    bool stale = false;
};

inline EnvironmentDisplayDecision environment_display_decision(
    const bool has_reading,
    const SensorStatus status)
{
    EnvironmentDisplayDecision decision{};
    decision.available = has_reading &&
        (status == SensorStatus::online || status == SensorStatus::stale);
    decision.stale = decision.available && status == SensorStatus::stale;
    return decision;
}

}  // namespace pf_sensors
