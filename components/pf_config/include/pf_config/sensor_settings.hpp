#pragma once

#include <cstdint>

namespace pf_config {

// Blob layout mirrors WeatherSettings/WeatherSettingsBlob exactly (see
// weather_settings.hpp) -- magic/version/CRC-guarded NVS record with a
// separate in-memory struct for the UI/runtime shape.
inline constexpr std::uint32_t kSensorSettingsMagic = 0x31534650U;
// v2 (ADR-0018) added the second photoresistor channel. v1 records are
// still readable and are migrated on load; see decode_sensor_settings_v1.
inline constexpr std::uint16_t kSensorSettingsVersion = 2U;

// The ESP32 default ADC width is 12-bit (0-4095).
inline constexpr std::uint16_t kMinLightThreshold = 0U;
inline constexpr std::uint16_t kMaxLightThreshold = 4095U;
inline constexpr std::uint32_t kMinAwayDurationSeconds = 10U;
inline constexpr std::uint32_t kMaxAwayDurationSeconds = 3600U;
inline constexpr std::uint32_t kMinReturnDurationSeconds = 1U;
inline constexpr std::uint32_t kMaxReturnDurationSeconds = 3600U;

// Channel 1 = GPIO5 / ADC1_CH4, channel 2 = GPIO7 / ADC1_CH6 (ADR-0018).
// Each channel is enabled and calibrated on its own, because two
// photoresistors mounted in different spots see different light.
struct SensorSettings {
    bool environment_enabled = false;
    bool light1_enabled = false;
    bool light2_enabled = false;
    std::uint16_t light1_threshold = 2000U;
    std::uint16_t light2_threshold = 2000U;
    std::uint32_t away_duration_s = 180U;
    std::uint32_t return_duration_s = 30U;
};

// Explicitly padding-free, so the stored length nvs_get_blob reports is
// enough to tell a v1 record (24 bytes) from a v2 one (28 bytes).
struct SensorSettingsBlob {
    std::uint32_t magic = kSensorSettingsMagic;
    std::uint16_t version = kSensorSettingsVersion;
    std::uint16_t reserved = 0U;
    std::uint8_t environment_enabled = 0U;
    std::uint8_t light1_enabled = 0U;
    std::uint8_t light2_enabled = 0U;
    std::uint8_t reserved2 = 0U;
    std::uint16_t light1_threshold = 0U;
    std::uint16_t light2_threshold = 0U;
    std::uint32_t away_duration_s = 0U;
    std::uint32_t return_duration_s = 0U;
    std::uint32_t crc32 = 0U;
};

// The pre-ADR-0018 single-channel record, kept verbatim so an existing
// device keeps its durations and threshold across the upgrade instead of
// silently reverting to defaults. Never written -- save always emits v2.
struct SensorSettingsBlobV1 {
    std::uint32_t magic = kSensorSettingsMagic;
    std::uint16_t version = 1U;
    std::uint16_t reserved = 0U;
    std::uint8_t environment_enabled = 0U;
    std::uint8_t light_enabled = 0U;
    std::uint16_t light_threshold = 0U;
    std::uint32_t away_duration_s = 0U;
    std::uint32_t return_duration_s = 0U;
    std::uint32_t crc32 = 0U;
};

// load_sensor_settings() tells the two record versions apart by the
// stored length alone, so the layouts must stay padding-free and must
// never collide in size.
static_assert(
    sizeof(SensorSettingsBlob) == 28U,
    "v2 sensor settings blob must stay padding-free at 28 bytes");
static_assert(
    sizeof(SensorSettingsBlobV1) == 24U,
    "v1 sensor settings blob layout is frozen at 24 bytes");

// Field-wise CRC-32 rather than a raw byte span, so the digest stays
// independent of any padding a compiler might introduce.
class SensorSettingsCrc32 {
public:
    void update(const std::uint8_t value)
    {
        crc_ ^= value;
        for (std::uint8_t bit = 0U; bit < 8U; ++bit) {
            const std::uint32_t mask = 0U - (crc_ & 1U);
            crc_ = (crc_ >> 1U) ^ (0xEDB88320U & mask);
        }
    }

    void update_u16(const std::uint16_t value)
    {
        update(static_cast<std::uint8_t>(value));
        update(static_cast<std::uint8_t>(value >> 8U));
    }

    void update_u32(const std::uint32_t value)
    {
        update(static_cast<std::uint8_t>(value));
        update(static_cast<std::uint8_t>(value >> 8U));
        update(static_cast<std::uint8_t>(value >> 16U));
        update(static_cast<std::uint8_t>(value >> 24U));
    }

    std::uint32_t value() const
    {
        return ~crc_;
    }

private:
    std::uint32_t crc_ = 0xFFFFFFFFU;
};

inline bool sensor_settings_valid(const SensorSettings& settings)
{
    return settings.light1_threshold >= kMinLightThreshold &&
           settings.light1_threshold <= kMaxLightThreshold &&
           settings.light2_threshold >= kMinLightThreshold &&
           settings.light2_threshold <= kMaxLightThreshold &&
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
    SensorSettingsCrc32 crc{};
    crc.update_u32(blob.magic);
    crc.update_u16(blob.version);
    crc.update_u16(blob.reserved);
    crc.update(blob.environment_enabled);
    crc.update(blob.light1_enabled);
    crc.update(blob.light2_enabled);
    crc.update(blob.reserved2);
    crc.update_u16(blob.light1_threshold);
    crc.update_u16(blob.light2_threshold);
    crc.update_u32(blob.away_duration_s);
    crc.update_u32(blob.return_duration_s);
    return crc.value();
}

// Must keep hashing exactly the v1 field order; changing it would make
// every stored v1 record fail its CRC check and lose the settings the
// migration exists to preserve.
inline std::uint32_t sensor_settings_v1_crc32(
    const SensorSettingsBlobV1& blob)
{
    SensorSettingsCrc32 crc{};
    crc.update_u32(blob.magic);
    crc.update_u16(blob.version);
    crc.update_u16(blob.reserved);
    crc.update(blob.environment_enabled);
    crc.update(blob.light_enabled);
    crc.update_u16(blob.light_threshold);
    crc.update_u32(blob.away_duration_s);
    crc.update_u32(blob.return_duration_s);
    return crc.value();
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
    candidate.light1_enabled = settings.light1_enabled ? 1U : 0U;
    candidate.light2_enabled = settings.light2_enabled ? 1U : 0U;
    candidate.light1_threshold = settings.light1_threshold;
    candidate.light2_threshold = settings.light2_threshold;
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
        blob.reserved != 0U || blob.reserved2 != 0U ||
        (blob.environment_enabled != 0U && blob.environment_enabled != 1U) ||
        (blob.light1_enabled != 0U && blob.light1_enabled != 1U) ||
        (blob.light2_enabled != 0U && blob.light2_enabled != 1U) ||
        blob.crc32 != sensor_settings_crc32(blob)) {
        return false;
    }
    SensorSettings candidate{};
    candidate.environment_enabled = blob.environment_enabled != 0U;
    candidate.light1_enabled = blob.light1_enabled != 0U;
    candidate.light2_enabled = blob.light2_enabled != 0U;
    candidate.light1_threshold = blob.light1_threshold;
    candidate.light2_threshold = blob.light2_threshold;
    candidate.away_duration_s = blob.away_duration_s;
    candidate.return_duration_s = blob.return_duration_s;
    if (!sensor_settings_valid(candidate)) {
        return false;
    }
    destination = candidate;
    return true;
}

// Reads a pre-ADR-0018 record. The single stored channel becomes channel
// 1; channel 2 comes up disabled at the default threshold, which is the
// truthful state for a device whose second photoresistor is not wired yet.
inline bool decode_sensor_settings_v1(
    const SensorSettingsBlobV1& blob,
    SensorSettings& destination)
{
    if (blob.magic != kSensorSettingsMagic || blob.version != 1U ||
        blob.reserved != 0U ||
        (blob.environment_enabled != 0U && blob.environment_enabled != 1U) ||
        (blob.light_enabled != 0U && blob.light_enabled != 1U) ||
        blob.crc32 != sensor_settings_v1_crc32(blob)) {
        return false;
    }
    SensorSettings candidate{};
    candidate.environment_enabled = blob.environment_enabled != 0U;
    candidate.light1_enabled = blob.light_enabled != 0U;
    candidate.light1_threshold = blob.light_threshold;
    candidate.away_duration_s = blob.away_duration_s;
    candidate.return_duration_s = blob.return_duration_s;
    if (!sensor_settings_valid(candidate)) {
        return false;
    }
    destination = candidate;
    return true;
}

}  // namespace pf_config
