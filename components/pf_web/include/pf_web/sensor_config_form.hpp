#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "pf_config/secure_memory.hpp"
#include "pf_config/sensor_settings.hpp"

namespace pf_web {

enum class SensorConfigParseStatus : std::uint8_t {
    ok,
    missing_field,
    duplicate_field,
    unknown_field,
    invalid_value,
};

// HTML checkboxes are only present in a form POST when checked, so
// environment_enabled/light_enabled are optional (absent = false);
// the three numeric fields are required.
struct SensorConfigForm {
    bool environment_enabled = false;
    bool light_enabled = false;
    char light_threshold[8]{};
    char away_duration_s[8]{};
    char return_duration_s[8]{};
};

inline bool parse_sensor_u32(
    const char* const text,
    std::uint32_t& destination)
{
    if (text == nullptr || text[0] == '\0') {
        return false;
    }
    std::uint32_t value = 0U;
    for (std::size_t index = 0U; text[index] != '\0'; ++index) {
        if (text[index] < '0' || text[index] > '9') {
            return false;
        }
        const std::uint32_t digit =
            static_cast<std::uint32_t>(text[index] - '0');
        if (value > (UINT32_MAX - digit) / 10U) {
            return false;
        }
        value = value * 10U + digit;
    }
    destination = value;
    return true;
}

inline SensorConfigParseStatus parse_sensor_config_form(
    const char* const body,
    const std::size_t length,
    SensorConfigForm& destination)
{
    if (body == nullptr || length == 0U) {
        return SensorConfigParseStatus::missing_field;
    }

    SensorConfigForm candidate{};
    const pf_config::SecureZeroGuard candidate_guard(candidate);
    bool threshold_seen = false;
    bool away_seen = false;
    bool return_seen = false;
    bool environment_seen = false;
    bool light_seen = false;

    std::size_t cursor = 0U;
    while (cursor < length) {
        const std::size_t field_start = cursor;
        while (cursor < length && body[cursor] != '&') {
            ++cursor;
        }
        const std::size_t field_end = cursor;
        if (cursor < length) {
            ++cursor;
        }
        std::size_t equals = field_start;
        while (equals < field_end && body[equals] != '=') {
            ++equals;
        }
        if (equals == field_end) {
            return SensorConfigParseStatus::invalid_value;
        }
        const std::size_t name_length = equals - field_start;
        const char* const value = body + equals + 1U;
        const std::size_t value_length = field_end - equals - 1U;
        const char* const name = body + field_start;

        const auto matches_field = [&](const char* const field_name) {
            return name_length == std::strlen(field_name) &&
                   std::memcmp(name, field_name, name_length) == 0;
        };

        if (matches_field("light_threshold")) {
            if (threshold_seen) {
                return SensorConfigParseStatus::duplicate_field;
            }
            threshold_seen = true;
            if (value_length >= sizeof(candidate.light_threshold)) {
                return SensorConfigParseStatus::invalid_value;
            }
            std::memcpy(candidate.light_threshold, value, value_length);
            candidate.light_threshold[value_length] = '\0';
        } else if (matches_field("away_duration_s")) {
            if (away_seen) {
                return SensorConfigParseStatus::duplicate_field;
            }
            away_seen = true;
            if (value_length >= sizeof(candidate.away_duration_s)) {
                return SensorConfigParseStatus::invalid_value;
            }
            std::memcpy(candidate.away_duration_s, value, value_length);
            candidate.away_duration_s[value_length] = '\0';
        } else if (matches_field("return_duration_s")) {
            if (return_seen) {
                return SensorConfigParseStatus::duplicate_field;
            }
            return_seen = true;
            if (value_length >= sizeof(candidate.return_duration_s)) {
                return SensorConfigParseStatus::invalid_value;
            }
            std::memcpy(candidate.return_duration_s, value, value_length);
            candidate.return_duration_s[value_length] = '\0';
        } else if (matches_field("environment_enabled")) {
            if (environment_seen) {
                return SensorConfigParseStatus::duplicate_field;
            }
            environment_seen = true;
            // Browsers submit checked checkboxes as name=on; a present
            // field with any other value is malformed input, not a
            // stronger form of "checked".
            if (value_length != 2U || value[0] != 'o' || value[1] != 'n') {
                return SensorConfigParseStatus::invalid_value;
            }
            candidate.environment_enabled = true;
        } else if (matches_field("light_enabled")) {
            if (light_seen) {
                return SensorConfigParseStatus::duplicate_field;
            }
            light_seen = true;
            if (value_length != 2U || value[0] != 'o' || value[1] != 'n') {
                return SensorConfigParseStatus::invalid_value;
            }
            candidate.light_enabled = true;
        } else {
            return SensorConfigParseStatus::unknown_field;
        }
    }

    if (!threshold_seen || !away_seen || !return_seen) {
        return SensorConfigParseStatus::missing_field;
    }
    destination = candidate;
    return SensorConfigParseStatus::ok;
}

constexpr const char* to_string(const SensorConfigParseStatus status)
{
    switch (status) {
        case SensorConfigParseStatus::ok:
            return "ok";
        case SensorConfigParseStatus::missing_field:
            return "missing_field";
        case SensorConfigParseStatus::duplicate_field:
            return "duplicate_field";
        case SensorConfigParseStatus::unknown_field:
            return "unknown_field";
        case SensorConfigParseStatus::invalid_value:
            return "invalid_value";
    }
    return "invalid_value";
}

}  // namespace pf_web
