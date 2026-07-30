#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "pf_config/secure_memory.hpp"
#include "pf_config/weather_settings.hpp"
#include "pf_web/provisioning_form.hpp"

namespace pf_web {

enum class WeatherConfigParseStatus : std::uint8_t {
    ok,
    missing_field,
    duplicate_field,
    unknown_field,
    bad_encoding,
    invalid_value,
};

struct WeatherConfigForm {
    bool api_key_seen = false;
    char api_key[pf_config::kWeatherApiKeyCapacity]{};
    char latitude_e6[16]{};
    char longitude_e6[16]{};
    char interval_minutes[8]{};
    char location[pf_config::kWeatherLocationCapacity]{};
    char units[pf_config::kWeatherUnitsCapacity]{};
    char language[pf_config::kWeatherLanguageCapacity]{};
    char ntp_server[pf_config::kWeatherNtpServerCapacity]{};
};

inline WeatherConfigParseStatus decode_weather_field(
    const char* const value,
    const std::size_t length,
    char* const destination,
    const std::size_t capacity)
{
    if (value == nullptr || destination == nullptr || capacity == 0U) {
        return WeatherConfigParseStatus::invalid_value;
    }
    std::size_t output = 0U;
    for (std::size_t input = 0U; input < length; ++input) {
        std::uint8_t byte = static_cast<std::uint8_t>(value[input]);
        if (byte == '%') {
            if (input + 2U >= length) {
                return WeatherConfigParseStatus::bad_encoding;
            }
            const int high = hex_value(value[input + 1U]);
            const int low = hex_value(value[input + 2U]);
            if (high < 0 || low < 0) {
                return WeatherConfigParseStatus::bad_encoding;
            }
            byte = static_cast<std::uint8_t>((high << 4U) | low);
            input += 2U;
        } else if (byte == '+') {
            byte = ' ';
        }
        if (byte == 0U || byte < 0x20U || byte == 0x7FU ||
            output + 1U >= capacity) {
            return WeatherConfigParseStatus::invalid_value;
        }
        destination[output++] = static_cast<char>(byte);
    }
    destination[output] = '\0';
    return pf_config::valid_utf8_text(destination, output)
               ? WeatherConfigParseStatus::ok
               : WeatherConfigParseStatus::invalid_value;
}

inline WeatherConfigParseStatus parse_weather_config_form(
    const char* const body,
    const std::size_t length,
    WeatherConfigForm& destination)
{
    if (body == nullptr || length == 0U) {
        return WeatherConfigParseStatus::missing_field;
    }

    WeatherConfigForm candidate{};
    const pf_config::SecureZeroGuard candidate_guard(candidate);
    bool latitude_seen = false;
    bool longitude_seen = false;
    bool interval_seen = false;
    bool location_seen = false;
    bool units_seen = false;
    bool language_seen = false;
    bool ntp_seen = false;

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
            return WeatherConfigParseStatus::bad_encoding;
        }
        const std::size_t name_length = equals - field_start;
        const char* const value = body + equals + 1U;
        const std::size_t value_length = field_end - equals - 1U;
        char* destination_field = nullptr;
        std::size_t destination_capacity = 0U;
        bool* seen = nullptr;

        if (name_length == 7U &&
            std::memcmp(body + field_start, "api_key", 7U) == 0) {
            destination_field = candidate.api_key;
            destination_capacity = sizeof(candidate.api_key);
            seen = &candidate.api_key_seen;
        } else if (name_length == 11U &&
                   std::memcmp(body + field_start, "latitude_e6", 11U) == 0) {
            destination_field = candidate.latitude_e6;
            destination_capacity = sizeof(candidate.latitude_e6);
            seen = &latitude_seen;
        } else if (name_length == 12U &&
                   std::memcmp(body + field_start, "longitude_e6", 12U) == 0) {
            destination_field = candidate.longitude_e6;
            destination_capacity = sizeof(candidate.longitude_e6);
            seen = &longitude_seen;
        } else if (name_length == 16U &&
                   std::memcmp(
                       body + field_start,
                       "interval_minutes",
                       16U) == 0) {
            destination_field = candidate.interval_minutes;
            destination_capacity = sizeof(candidate.interval_minutes);
            seen = &interval_seen;
        } else if (name_length == 8U &&
                   std::memcmp(body + field_start, "location", 8U) == 0) {
            destination_field = candidate.location;
            destination_capacity = sizeof(candidate.location);
            seen = &location_seen;
        } else if (name_length == 5U &&
                   std::memcmp(body + field_start, "units", 5U) == 0) {
            destination_field = candidate.units;
            destination_capacity = sizeof(candidate.units);
            seen = &units_seen;
        } else if (name_length == 8U &&
                   std::memcmp(body + field_start, "language", 8U) == 0) {
            destination_field = candidate.language;
            destination_capacity = sizeof(candidate.language);
            seen = &language_seen;
        } else if (name_length == 10U &&
                   std::memcmp(body + field_start, "ntp_server", 10U) == 0) {
            destination_field = candidate.ntp_server;
            destination_capacity = sizeof(candidate.ntp_server);
            seen = &ntp_seen;
        } else {
            return WeatherConfigParseStatus::unknown_field;
        }

        if (*seen) {
            return WeatherConfigParseStatus::duplicate_field;
        }
        *seen = true;
        const WeatherConfigParseStatus decoded = decode_weather_field(
            value,
            value_length,
            destination_field,
            destination_capacity);
        if (decoded != WeatherConfigParseStatus::ok) {
            return decoded;
        }
    }

    if (!latitude_seen || !longitude_seen || !interval_seen ||
        !location_seen || !units_seen || !language_seen || !ntp_seen) {
        return WeatherConfigParseStatus::missing_field;
    }
    destination = candidate;
    return WeatherConfigParseStatus::ok;
}

inline bool parse_weather_i32(
    const char* const text,
    std::int32_t& destination)
{
    if (text == nullptr || text[0] == '\0') {
        return false;
    }
    std::size_t index = 0U;
    bool negative = false;
    if (text[index] == '+' || text[index] == '-') {
        negative = text[index] == '-';
        ++index;
    }
    if (text[index] == '\0') {
        return false;
    }
    std::uint32_t value = 0U;
    for (; text[index] != '\0'; ++index) {
        if (text[index] < '0' || text[index] > '9') {
            return false;
        }
        const std::uint32_t digit =
            static_cast<std::uint32_t>(text[index] - '0');
        const std::uint32_t limit = negative
                                        ? 2'147'483'648U
                                        : 2'147'483'647U;
        if (value > (limit - digit) / 10U) {
            return false;
        }
        value = value * 10U + digit;
    }
    if (negative) {
        if (value == 2'147'483'648U) {
            destination = INT32_MIN;
        } else {
            destination = -static_cast<std::int32_t>(value);
        }
    } else {
        destination = static_cast<std::int32_t>(value);
    }
    return true;
}

inline bool parse_weather_u32(
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

constexpr const char* to_string(const WeatherConfigParseStatus status)
{
    switch (status) {
        case WeatherConfigParseStatus::ok:
            return "ok";
        case WeatherConfigParseStatus::missing_field:
            return "missing_field";
        case WeatherConfigParseStatus::duplicate_field:
            return "duplicate_field";
        case WeatherConfigParseStatus::unknown_field:
            return "unknown_field";
        case WeatherConfigParseStatus::bad_encoding:
            return "bad_encoding";
        case WeatherConfigParseStatus::invalid_value:
            return "invalid_value";
    }
    return "invalid_value";
}

}  // namespace pf_web
