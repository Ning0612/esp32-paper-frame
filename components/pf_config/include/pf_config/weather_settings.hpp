#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "pf_config/network_credentials.hpp"
#include "pf_config/secure_memory.hpp"

namespace pf_config {

inline constexpr std::uint32_t kWeatherSettingsMagic = 0x31574650U;
inline constexpr std::uint16_t kWeatherSettingsVersion = 2U;
inline constexpr std::size_t kWeatherApiKeyCapacity = 128U;
inline constexpr std::size_t kWeatherUnitsCapacity = 9U;
inline constexpr std::size_t kWeatherNtpServerCapacity = 64U;
inline constexpr std::int32_t kMinimumLatitudeE6 = -90'000'000;
inline constexpr std::int32_t kMaximumLatitudeE6 = 90'000'000;
inline constexpr std::int32_t kMinimumLongitudeE6 = -180'000'000;
inline constexpr std::int32_t kMaximumLongitudeE6 = 180'000'000;

// Display language is fixed to English (see ADR-0014) and the update
// cadence is driven by the carousel's panel-refresh cycle instead of a
// user-configured interval, so neither is part of the persisted schema.
struct WeatherSettings {
    std::int32_t latitude_e6 = 25'033'000;
    std::int32_t longitude_e6 = 121'565'000;
    char api_key[kWeatherApiKeyCapacity]{};
    char units[kWeatherUnitsCapacity] = "metric";
    char ntp_server[kWeatherNtpServerCapacity] = "pool.ntp.org";
};

struct WeatherSettingsBlob {
    std::uint32_t magic = kWeatherSettingsMagic;
    std::uint16_t version = kWeatherSettingsVersion;
    std::uint16_t reserved = 0U;
    std::int32_t latitude_e6 = 0;
    std::int32_t longitude_e6 = 0;
    char api_key[kWeatherApiKeyCapacity]{};
    char units[kWeatherUnitsCapacity]{};
    char ntp_server[kWeatherNtpServerCapacity]{};
    std::uint32_t crc32 = 0U;
};

inline std::size_t weather_text_length(
    const char* const text,
    const std::size_t capacity)
{
    return bounded_text_length(text, capacity);
}

inline bool valid_weather_text(
    const char* const text,
    const std::size_t capacity,
    const bool allow_empty)
{
    const std::size_t length = weather_text_length(text, capacity);
    return text != nullptr && length < capacity &&
           (allow_empty || length > 0U) &&
           valid_utf8_text(text, length);
}

inline bool weather_settings_valid(const WeatherSettings& settings)
{
    const std::size_t api_key_length =
        weather_text_length(settings.api_key, sizeof(settings.api_key));
    const std::size_t units_length =
        weather_text_length(settings.units, sizeof(settings.units));
    const bool units_valid =
        (units_length == 6U &&
         std::memcmp(settings.units, "metric", 6U) == 0) ||
        (units_length == 8U &&
         std::memcmp(settings.units, "imperial", 8U) == 0);
    return settings.latitude_e6 >= kMinimumLatitudeE6 &&
           settings.latitude_e6 <= kMaximumLatitudeE6 &&
           settings.longitude_e6 >= kMinimumLongitudeE6 &&
           settings.longitude_e6 <= kMaximumLongitudeE6 &&
           valid_weather_text(
               settings.api_key,
               sizeof(settings.api_key),
               true) &&
           (api_key_length == 0U || api_key_length >= 8U) &&
           units_valid &&
           valid_weather_text(
               settings.ntp_server,
               sizeof(settings.ntp_server),
               false);
}

inline void default_weather_settings(WeatherSettings& destination)
{
    destination = WeatherSettings{};
}

inline std::uint32_t weather_settings_crc32(
    const WeatherSettingsBlob& blob)
{
    std::uint32_t crc = 0xFFFFFFFFU;
    const auto update = [&crc](const std::uint8_t value) {
        crc ^= value;
        for (std::uint8_t bit = 0U; bit < 8U; ++bit) {
            const std::uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
    };
    const auto update_u16 = [&update](const std::uint16_t value) {
        update(static_cast<std::uint8_t>(value));
        update(static_cast<std::uint8_t>(value >> 8U));
    };
    const auto update_u32 = [&update](const std::uint32_t value) {
        update(static_cast<std::uint8_t>(value));
        update(static_cast<std::uint8_t>(value >> 8U));
        update(static_cast<std::uint8_t>(value >> 16U));
        update(static_cast<std::uint8_t>(value >> 24U));
    };
    const auto update_i32 = [&update_u32](const std::int32_t value) {
        update_u32(static_cast<std::uint32_t>(value));
    };
    update_u32(blob.magic);
    update_u16(blob.version);
    update_u16(blob.reserved);
    update_i32(blob.latitude_e6);
    update_i32(blob.longitude_e6);
    for (const char value : blob.api_key) {
        update(static_cast<std::uint8_t>(value));
    }
    for (const char value : blob.units) {
        update(static_cast<std::uint8_t>(value));
    }
    for (const char value : blob.ntp_server) {
        update(static_cast<std::uint8_t>(value));
    }
    return ~crc;
}

inline bool encode_weather_settings(
    const WeatherSettings& settings,
    WeatherSettingsBlob& destination)
{
    if (!weather_settings_valid(settings)) {
        return false;
    }
    WeatherSettingsBlob candidate{};
    const SecureZeroGuard candidate_guard(candidate);
    candidate.latitude_e6 = settings.latitude_e6;
    candidate.longitude_e6 = settings.longitude_e6;
    const auto copy_text = [](char* const destination,
                              const std::size_t capacity,
                              const char* const source) {
        const std::size_t length = bounded_text_length(source, capacity);
        if (length >= capacity) {
            return false;
        }
        if (length > 0U) {
            std::memcpy(destination, source, length);
        }
        destination[length] = '\0';
        return true;
    };
    if (!copy_text(candidate.api_key, sizeof(candidate.api_key), settings.api_key) ||
        !copy_text(candidate.units, sizeof(candidate.units), settings.units) ||
        !copy_text(candidate.ntp_server, sizeof(candidate.ntp_server), settings.ntp_server)) {
        return false;
    }
    candidate.crc32 = weather_settings_crc32(candidate);
    destination = candidate;
    return true;
}

inline bool decode_weather_settings(
    const WeatherSettingsBlob& blob,
    WeatherSettings& destination)
{
    if (blob.magic != kWeatherSettingsMagic ||
        blob.version != kWeatherSettingsVersion ||
        blob.reserved != 0U ||
        blob.crc32 != weather_settings_crc32(blob)) {
        return false;
    }
    WeatherSettings candidate{};
    const SecureZeroGuard candidate_guard(candidate);
    candidate.latitude_e6 = blob.latitude_e6;
    candidate.longitude_e6 = blob.longitude_e6;
    const auto copy_text = [](char* const destination,
                              const std::size_t capacity,
                              const char* const source) {
        const std::size_t length = bounded_text_length(source, capacity);
        if (length >= capacity) {
            return false;
        }
        if (length > 0U) {
            std::memcpy(destination, source, length);
        }
        destination[length] = '\0';
        return true;
    };
    if (!copy_text(candidate.api_key, sizeof(candidate.api_key), blob.api_key) ||
        !copy_text(candidate.units, sizeof(candidate.units), blob.units) ||
        !copy_text(candidate.ntp_server, sizeof(candidate.ntp_server), blob.ntp_server)) {
        return false;
    }
    if (!weather_settings_valid(candidate)) {
        return false;
    }
    destination = candidate;
    return true;
}

}  // namespace pf_config
