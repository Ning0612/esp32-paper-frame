#pragma once

#include <cstdint>

namespace pf_config {

// Blob layout mirrors WeatherSettings/WeatherSettingsBlob exactly (see
// weather_settings.hpp) -- magic/version/CRC-guarded NVS record with a
// separate in-memory struct for the UI/runtime shape.
inline constexpr std::uint32_t kSensorSettingsMagic = 0x31534650U;
inline constexpr std::uint16_t kSensorSettingsVersion = 1U;

// ESP32's default ADC width is 12-bit (0-4095).
inline constexpr std::uint16_t kMinLightThreshold = 0U;
inline constexpr std::uint16_t kMaxLightThreshold = 4095U;
inline constexpr std::uint32_t kMinAwayDurationSeconds = 10U;
inline constexpr std::uint32_t kMaxAwayDurationSeconds = 3600U;
inline constexpr std::uint32_t kMinReturnDurationSeconds = 1U;
inline constexpr std::uint32_t kMaxReturnDurationSeconds = 3600U;

struct SensorSettings {
    bool environment_enabled = false;
    bool light_enabled = false;
    std::uint16_t light_threshold = 2000U;
    std::uint32_t away_duration_s = 180U;
    std::uint32_t return_duration_s = 30U;
};

struct SensorSettingsBlob {
    std::uint32_t magic = kSensorSettingsMagic;
    std::uint16_t version = kSensorSettingsVersion;
    std::uint16_t reserved = 0U;
    std::uint8_t environment_enabled = 0U;
    std::uint8_t light_enabled = 0U;
    std::uint16_t light_threshold = 0U;
    std::uint32_t away_duration_s = 0U;
    std::uint32_t return_duration_s = 0U;
    std::uint32_t crc32 = 0U;
};

inline bool sensor_settings_valid(const SensorSettings& settings)
{
    return settings.light_threshold >= kMinLightThreshold &&
           settings.light_threshold <= kMaxLightThreshold &&
           settings.away_duration_s >= kMinAwayDurationSeconds &&
           settings.away_duration_s <= kMaxAwayDurationSeconds &&
           settings.return_duration_s >= kMinReturnDurationSeconds &&
           settings.return_duration_s <= kMaxReturnDurationSeconds;
}

inline void default_sensor_settings(SensorSettings& destination)
{
    destination = SensorSettings{};
}

inline std::uint32_t sensor_settings_crc32(const SensorSettingsBlob& blob)
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
    update_u32(blob.magic);
    update_u16(blob.version);
    update_u16(blob.reserved);
    update(blob.environment_enabled);
    update(blob.light_enabled);
    update_u16(blob.light_threshold);
    update_u32(blob.away_duration_s);
    update_u32(blob.return_duration_s);
    return ~crc;
}

inline bool encode_sensor_settings(
    const SensorSettings& settings,
    SensorSettingsBlob& destination)
{
    if (!sensor_settings_valid(settings)) {
        return false;
    }
    SensorSettingsBlob candidate{};
    candidate.environment_enabled = settings.environment_enabled ? 1U : 0U;
    candidate.light_enabled = settings.light_enabled ? 1U : 0U;
    candidate.light_threshold = settings.light_threshold;
    candidate.away_duration_s = settings.away_duration_s;
    candidate.return_duration_s = settings.return_duration_s;
    candidate.crc32 = sensor_settings_crc32(candidate);
    destination = candidate;
    return true;
}

inline bool decode_sensor_settings(
    const SensorSettingsBlob& blob,
    SensorSettings& destination)
{
    if (blob.magic != kSensorSettingsMagic ||
        blob.version != kSensorSettingsVersion ||
        blob.reserved != 0U ||
        (blob.environment_enabled != 0U && blob.environment_enabled != 1U) ||
        (blob.light_enabled != 0U && blob.light_enabled != 1U) ||
        blob.crc32 != sensor_settings_crc32(blob)) {
        return false;
    }
    SensorSettings candidate{};
    candidate.environment_enabled = blob.environment_enabled != 0U;
    candidate.light_enabled = blob.light_enabled != 0U;
    candidate.light_threshold = blob.light_threshold;
    candidate.away_duration_s = blob.away_duration_s;
    candidate.return_duration_s = blob.return_duration_s;
    if (!sensor_settings_valid(candidate)) {
        return false;
    }
    destination = candidate;
    return true;
}

}  // namespace pf_config
