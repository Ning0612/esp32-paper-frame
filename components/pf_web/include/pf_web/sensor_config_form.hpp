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

// HTML checkboxes are only present in a form POST when checked, so the
// three enable flags are optional (absent = false); the four numeric
// fields are required. Field names are the wire contract shared with
// data/web/ui.js -- test/web/test_sensor_form_contract.mjs keeps the two
// sides from drifting apart.
struct SensorConfigForm {
    bool environment_enabled = false;
    bool light1_enabled = false;
    bool light2_enabled = false;
    char light1_threshold[8]{};
    char light2_threshold[8]{};
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
    bool threshold1_seen = false;
    bool threshold2_seen = false;
    bool away_seen = false;
    bool return_seen = false;
    bool environment_seen = false;
    bool light1_seen = false;
    bool light2_seen = false;

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
        const auto take_text = [&](
            char* const field,
            const std::size_t capacity,
            bool& seen) {
            if (seen) {
                return SensorConfigParseStatus::duplicate_field;
            }
            seen = true;
            if (value_length >= capacity) {
                return SensorConfigParseStatus::invalid_value;
            }
            std::memcpy(field, value, value_length);
            field[value_length] = '\0';
            return SensorConfigParseStatus::ok;
        };
        const auto take_checkbox = [&](bool& field, bool& seen) {
            if (seen) {
                return SensorConfigParseStatus::duplicate_field;
            }
            seen = true;
            // Browsers submit checked checkboxes as name=on; a present
            // field with any other value is malformed input, not a
            // stronger form of "checked".
            if (value_length != 2U || value[0] != 'o' || value[1] != 'n') {
                return SensorConfigParseStatus::invalid_value;
            }
            field = true;
            return SensorConfigParseStatus::ok;
        };

        SensorConfigParseStatus field_status = SensorConfigParseStatus::ok;
        if (matches_field("light1_threshold")) {
            field_status = take_text(
                candidate.light1_threshold,
                sizeof(candidate.light1_threshold),
                threshold1_seen);
        } else if (matches_field("light2_threshold")) {
            field_status = take_text(
                candidate.light2_threshold,
                sizeof(candidate.light2_threshold),
                threshold2_seen);
        } else if (matches_field("away_duration_s")) {
            field_status = take_text(
                candidate.away_duration_s,
                sizeof(candidate.away_duration_s),
                away_seen);
        } else if (matches_field("return_duration_s")) {
            field_status = take_text(
                candidate.return_duration_s,
                sizeof(candidate.return_duration_s),
                return_seen);
        } else if (matches_field("environment_enabled")) {
            field_status = take_checkbox(
                candidate.environment_enabled, environment_seen);
        } else if (matches_field("light1_enabled")) {
            field_status =
                take_checkbox(candidate.light1_enabled, light1_seen);
        } else if (matches_field("light2_enabled")) {
            field_status =
                take_checkbox(candidate.light2_enabled, light2_seen);
        } else {
            return SensorConfigParseStatus::unknown_field;
        }
        if (field_status != SensorConfigParseStatus::ok) {
            return field_status;
        }
    }

    if (!threshold1_seen || !threshold2_seen || !away_seen ||
        !return_seen) {
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
