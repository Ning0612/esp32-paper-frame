#pragma once

#include <cstddef>
#include <cstdint>

namespace pf_weather {

inline constexpr std::size_t kDescriptionCapacity = 64U;
inline constexpr std::size_t kIconCapacity = 8U;
inline constexpr std::size_t kLocationCapacity = 48U;
// Units ("metric"/"imperial") the observation in a Cache was fetched with;
// not part of Observation itself since the OpenWeatherMap response never
// echoes back the units it was requested with.
inline constexpr std::size_t kUnitsCapacity = 9U;
inline constexpr std::uint64_t kDefaultCacheMaxAgeSeconds = 3600U;
// record_success()'s interval_ms for "schedule no automatic follow-up":
// next_attempt_ms saturates, retry_due() stays false, and only
// request_immediate_refresh() wakes the worker. This is what the firmware
// passes -- ADR-0014 replaced the periodic weather timer with a fetch driven
// by the carousel's panel refresh, so a finite interval here would silently
// bring the retired polling behaviour back.
inline constexpr std::uint64_t kNoAutomaticRetry = UINT64_MAX;
inline constexpr std::uint64_t kInitialRetryMs = 10U * 1000U;
inline constexpr std::uint64_t kMaximumRetryMs = 60U * 60U * 1000U;

enum class ParseError : std::uint8_t {
    none = 0U,
    invalid_argument,
    malformed_json,
    api_rejected,
    missing_field,
    invalid_value,
};

const char* to_string(ParseError error);

struct Observation {
    float temperature = 0.0F;
    std::int16_t humidity_percent = -1;
    std::int32_t weather_id = 0;
    std::uint64_t observed_at_epoch_s = 0U;
    char description[kDescriptionCapacity]{};
    char icon[kIconCapacity]{};
    char location[kLocationCapacity]{};
};

struct ParseResult {
    ParseError error = ParseError::none;
    Observation observation{};

    bool ok() const
    {
        return error == ParseError::none;
    }
};

ParseResult parse_current_weather(
    const char* json,
    std::size_t length);

enum class Failure : std::uint8_t {
    none = 0U,
    api_key_invalid,
    network,
    http_error,
    parse_error,
};

const char* to_string(Failure failure);

struct Cache {
    Observation observation{};
    bool has_observation = false;
    std::uint64_t last_success_epoch_s = 0U;
    std::uint64_t next_attempt_ms = 0U;
    std::uint32_t consecutive_failures = 0U;
    Failure last_failure = Failure::none;
};

// `interval_ms` schedules the next automatic attempt (now_ms + interval_ms,
// saturating). It is deliberately NOT defaulted: since ADR-0014 the firmware
// has no periodic weather timer -- the worker passes UINT64_MAX so retry_due()
// stays false and the next fetch is woken by request_immediate_refresh() after
// a panel refresh is accepted. Callers must therefore say which they mean.
void record_success(
    Cache& cache,
    const Observation& observation,
    std::uint64_t success_epoch_s,
    std::uint64_t now_ms,
    std::uint64_t interval_ms);

void record_failure(
    Cache& cache,
    Failure failure,
    std::uint64_t now_ms);

bool retry_due(const Cache& cache, std::uint64_t now_ms);

// Classifies an OpenWeatherMap HTTP response status into a cache failure
// category once headers have been received (connection-level failures
// before a status code exists are always Failure::network).
Failure classify_http_status(int status_code);

bool stale(
    const Cache& cache,
    std::uint64_t now_epoch_s,
    std::uint64_t max_age_seconds = kDefaultCacheMaxAgeSeconds);

}  // namespace pf_weather
