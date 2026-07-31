#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "pf_config/schema.hpp"
#include "pf_config/weather_settings.hpp"
#include "pf_weather/weather.hpp"
#include "pf_web/health_serializer.hpp"

namespace pf_web {

struct DeviceInfo {
    const char* product;
    const char* model;
    const char* firmware;
};

struct MaskedConfig {
    bool wifi_configured;
    bool wifi_password_configured;
    bool management_password_configured;
    std::uint32_t refresh_minutes;
    const char* timezone;
    bool weather_configured;
    bool weather_api_key_set;
    std::int32_t weather_latitude_e6;
    std::int32_t weather_longitude_e6;
    std::uint32_t weather_interval_minutes;
    const char* weather_location;
    const char* weather_units;
    const char* weather_language;
    const char* weather_ntp_server;
};

inline SerializeResult serialize_device(
    const DeviceInfo& device,
    const pf_runtime::RuntimeSnapshot& snapshot,
    const bool snapshot_valid,
    char* output,
    const std::size_t output_size)
{
    if (output == nullptr || output_size == 0U ||
        device.product == nullptr || device.model == nullptr ||
        device.firmware == nullptr) {
        return {false, 0U};
    }

    char flash_capacity[16]{};
    char psram_capacity[16]{};
    if (snapshot_valid &&
        snapshot.flash == pf_runtime::ServiceState::ready &&
        snapshot.flash_bytes != 0U) {
        std::snprintf(
            flash_capacity,
            sizeof(flash_capacity),
            "%lu",
            static_cast<unsigned long>(snapshot.flash_bytes));
    } else {
        std::snprintf(flash_capacity, sizeof(flash_capacity), "null");
    }
    if (snapshot_valid &&
        snapshot.psram == pf_runtime::ServiceState::ready &&
        snapshot.psram_bytes != 0U) {
        std::snprintf(
            psram_capacity,
            sizeof(psram_capacity),
            "%lu",
            static_cast<unsigned long>(snapshot.psram_bytes));
    } else {
        std::snprintf(psram_capacity, sizeof(psram_capacity), "null");
    }

    const int written = std::snprintf(
        output,
        output_size,
        "{\"ok\":true,\"data\":{\"product\":\"%s\","
        "\"model\":\"%s\",\"firmware\":\"%s\","
        "\"api_version\":\"v1\",\"display\":{"
        "\"width\":800,\"height\":480,\"palette\":\"e6\"},"
        "\"capacity\":{\"flash_bytes\":%s,\"psram_bytes\":%s}}}",
        device.product,
        device.model,
        device.firmware,
        flash_capacity,
        psram_capacity);

    if (written < 0 || static_cast<std::size_t>(written) >= output_size) {
        output[0] = '\0';
        return {false, 0U};
    }
    return {true, static_cast<std::size_t>(written)};
}

inline SerializeResult serialize_status(
    const pf_runtime::RuntimeSnapshot& snapshot,
    const bool snapshot_valid,
    const std::uint64_t uptime_ms,
    const std::uint64_t now_epoch_s,
    char* output,
    const std::size_t output_size)
{
    if (output == nullptr || output_size == 0U) {
        return {false, 0U};
    }

    const auto safe_weather_text = [](
        const char* const value,
        const std::size_t capacity) {
        if (value == nullptr || capacity == 0U) {
            return false;
        }
        const std::size_t length =
            pf_config::bounded_text_length(value, capacity);
        if (length >= capacity) {
            return false;
        }
        for (std::size_t index = 0U; index < length; ++index) {
            const unsigned char byte =
                static_cast<unsigned char>(value[index]);
            if (byte < 0x20U || byte == '"' || byte == '\\') {
                return false;
            }
        }
        return true;
    };
    const bool weather_has_observation =
        snapshot_valid && snapshot.weather.has_observation;
    const bool weather_is_stale =
        weather_has_observation &&
        pf_weather::stale(snapshot.weather, now_epoch_s);
    const char* const weather_state =
        !weather_has_observation
            ? "unavailable"
            : (weather_is_stale ? "stale" : "available");
    char weather_temperature[24]{};
    char weather_humidity[8]{};
    char weather_observed_at[24]{};
    if (weather_has_observation) {
        std::snprintf(
            weather_temperature,
            sizeof(weather_temperature),
            "%.1f",
            static_cast<double>(
                snapshot.weather.observation.temperature));
        if (snapshot.weather.observation.humidity_percent >= 0) {
            std::snprintf(
                weather_humidity,
                sizeof(weather_humidity),
                "%d",
                static_cast<int>(
                    snapshot.weather.observation.humidity_percent));
        } else {
            std::snprintf(weather_humidity, sizeof(weather_humidity), "null");
        }
        std::snprintf(
            weather_observed_at,
            sizeof(weather_observed_at),
            "%llu",
            static_cast<unsigned long long>(
                snapshot.weather.last_success_epoch_s));
    } else {
        std::snprintf(
            weather_temperature, sizeof(weather_temperature), "null");
        std::snprintf(weather_humidity, sizeof(weather_humidity), "null");
        std::snprintf(
            weather_observed_at, sizeof(weather_observed_at), "null");
    }
    const char* const weather_units =
        weather_has_observation &&
                safe_weather_text(
                    snapshot.weather_units, pf_weather::kUnitsCapacity)
            ? snapshot.weather_units
            : "unknown";
    const char* const weather_icon =
        weather_has_observation &&
                safe_weather_text(
                    snapshot.weather.observation.icon,
                    pf_weather::kIconCapacity)
            ? snapshot.weather.observation.icon
            : "unknown";
    const char* const weather_description =
        weather_has_observation &&
                safe_weather_text(
                    snapshot.weather.observation.description,
                    pf_weather::kDescriptionCapacity)
            ? snapshot.weather.observation.description
            : "unknown";
    const char* const weather_last_failure =
        snapshot_valid
            ? pf_weather::to_string(snapshot.weather.last_failure)
            : "none";

    char flash_bytes[16]{};
    char psram_bytes[16]{};
    char webfs_total[16]{};
    char webfs_used[16]{};
    char imagefs_total[16]{};
    char imagefs_used[16]{};
    char refresh_minutes[16]{};
    const auto format_capacity = [](
        char* const destination,
        const std::size_t capacity,
        const pf_runtime::ServiceState state,
        const std::uint32_t value) {
        if (state == pf_runtime::ServiceState::ready && value != 0U) {
            std::snprintf(
                destination,
                capacity,
                "%lu",
                static_cast<unsigned long>(value));
        } else {
            std::snprintf(destination, capacity, "null");
        }
    };
    format_capacity(
        flash_bytes,
        sizeof(flash_bytes),
        snapshot_valid ? snapshot.flash : pf_runtime::ServiceState::unknown,
        snapshot_valid ? snapshot.flash_bytes : 0U);
    format_capacity(
        psram_bytes,
        sizeof(psram_bytes),
        snapshot_valid ? snapshot.psram : pf_runtime::ServiceState::unknown,
        snapshot_valid ? snapshot.psram_bytes : 0U);
    format_capacity(
        webfs_total,
        sizeof(webfs_total),
        snapshot_valid ? snapshot.webfs : pf_runtime::ServiceState::unknown,
        snapshot_valid ? snapshot.webfs_total_bytes : 0U);
    format_capacity(
        webfs_used,
        sizeof(webfs_used),
        snapshot_valid ? snapshot.webfs : pf_runtime::ServiceState::unknown,
        snapshot_valid ? snapshot.webfs_used_bytes : 0U);
    format_capacity(
        imagefs_total,
        sizeof(imagefs_total),
        snapshot_valid ? snapshot.imagefs : pf_runtime::ServiceState::unknown,
        snapshot_valid ? snapshot.imagefs_total_bytes : 0U);
    format_capacity(
        imagefs_used,
        sizeof(imagefs_used),
        snapshot_valid ? snapshot.imagefs : pf_runtime::ServiceState::unknown,
        snapshot_valid ? snapshot.imagefs_used_bytes : 0U);
    if (snapshot_valid && snapshot.carousel_refresh_minutes != 0U) {
        std::snprintf(
            refresh_minutes,
            sizeof(refresh_minutes),
            "%lu",
            static_cast<unsigned long>(
                snapshot.carousel_refresh_minutes));
    } else {
        std::snprintf(refresh_minutes, sizeof(refresh_minutes), "null");
    }

    /*
     * Keep the status JSON bounded and deterministic. The six capacity
     * strings above are either decimal numbers or the JSON literal null.
     */
    const int written = std::snprintf(
        output,
        output_size,
        "{\"ok\":true,\"data\":{\"sequence\":%lu,"
        "\"uptime_ms\":%llu,\"services\":{"
        "\"flash\":\"%s\",\"psram\":\"%s\","
        "\"config\":\"%s\",\"webfs\":\"%s\","
        "\"imagefs\":\"%s\"},\"network\":{"
        "\"wifi\":\"%s\",\"internet\":\"%s\","
        "\"sntp\":\"%s\"},\"storage\":{"
        "\"flash_bytes\":%s,\"psram_bytes\":%s,"
        "\"webfs_total_bytes\":%s,\"webfs_used_bytes\":%s,"
        "\"imagefs_total_bytes\":%s,\"imagefs_used_bytes\":%s},"
        "\"display\":{\"state\":\"%s\","
        "\"active_request_id\":%lu,\"queued_count\":%u,"
        "\"last_request_id\":%lu,\"last_outcome\":\"%s\","
        "\"last_stage\":%u},\"carousel\":{"
        "\"state\":\"%s\",\"refresh_minutes\":%s,"
        "\"current_image\":null,\"next_refresh_ms\":null},"
        "\"weather\":{\"state\":\"%s\",\"temperature\":%s,"
        "\"units\":\"%s\",\"icon\":\"%s\",\"description\":\"%s\","
        "\"humidity_percent\":%s,\"observed_at_epoch_s\":%s,"
        "\"last_failure\":\"%s\"},"
        "\"sensors\":{\"temperature_c\":null,"
        "\"humidity_percent\":null,\"light_adc\":null,"
        "\"presence\":null}}}",
        static_cast<unsigned long>(
            snapshot_valid ? snapshot.sequence : 0U),
        static_cast<unsigned long long>(snapshot_valid ? uptime_ms : 0U),
        snapshot_valid ? pf_runtime::to_string(snapshot.flash) : "unknown",
        snapshot_valid ? pf_runtime::to_string(snapshot.psram) : "unknown",
        snapshot_valid ? pf_runtime::to_string(snapshot.config) : "unknown",
        snapshot_valid ? pf_runtime::to_string(snapshot.webfs) : "unknown",
        snapshot_valid ? pf_runtime::to_string(snapshot.imagefs) : "unknown",
        snapshot_valid ? pf_runtime::to_string(snapshot.wifi) : "unknown",
        snapshot_valid ? pf_runtime::to_string(snapshot.internet) : "unknown",
        snapshot_valid ? pf_runtime::to_string(snapshot.time_sync) : "unknown",
        flash_bytes,
        psram_bytes,
        webfs_total,
        webfs_used,
        imagefs_total,
        imagefs_used,
        snapshot_valid ? pf_runtime::to_string(snapshot.display) : "unknown",
        static_cast<unsigned long>(
            snapshot_valid ? snapshot.active_display_request_id : 0U),
        static_cast<unsigned>(
            snapshot_valid ? snapshot.queued_display_count : 0U),
        static_cast<unsigned long>(
            snapshot_valid ? snapshot.last_display_request_id : 0U),
        pf_runtime::to_string(
            snapshot_valid ? snapshot.last_display_outcome :
                              pf_runtime::DisplayOutcome::none),
        static_cast<unsigned>(
            snapshot_valid ? snapshot.last_display_stage : 0U),
        snapshot_valid && snapshot.carousel_refresh_minutes != 0U
            ? "ready"
            : "unavailable",
        refresh_minutes,
        weather_state,
        weather_temperature,
        weather_units,
        weather_icon,
        weather_description,
        weather_humidity,
        weather_observed_at,
        weather_last_failure);

    if (written < 0 || static_cast<std::size_t>(written) >= output_size) {
        output[0] = '\0';
        return {false, 0U};
    }
    return {true, static_cast<std::size_t>(written)};
}

inline SerializeResult serialize_masked_config(
    const MaskedConfig& config,
    char* output,
    const std::size_t output_size)
{
    if (output == nullptr || output_size == 0U ||
        config.timezone == nullptr || config.weather_location == nullptr ||
        config.weather_units == nullptr || config.weather_language == nullptr ||
        config.weather_ntp_server == nullptr) {
        return {false, 0U};
    }

    const auto safe_text = [](const char* const value,
                              const std::size_t capacity) {
        if (value == nullptr || capacity == 0U) {
            return false;
        }
        const std::size_t length =
            pf_config::bounded_text_length(value, capacity);
        if (length >= capacity) {
            return false;
        }
        for (std::size_t index = 0U; index < length; ++index) {
            const unsigned char byte =
                static_cast<unsigned char>(value[index]);
            if (byte < 0x20U || byte == '"' || byte == '\\') {
                return false;
            }
        }
        return true;
    };
    const char* const timezone = safe_text(
                                     config.timezone,
                                     pf_config::kTimezoneCapacity)
                                    ? config.timezone
                                    : "unknown";
    const char* const location = safe_text(
                                     config.weather_location,
                                     pf_config::kWeatherLocationCapacity)
                                    ? config.weather_location
                                    : "unknown";
    const char* const units = safe_text(
                                  config.weather_units,
                                  pf_config::kWeatherUnitsCapacity)
                                  ? config.weather_units
                                  : "unknown";
    const char* const language = safe_text(
                                    config.weather_language,
                                    pf_config::kWeatherLanguageCapacity)
                                    ? config.weather_language
                                    : "unknown";
    const char* const ntp_server = safe_text(
                                      config.weather_ntp_server,
                                      pf_config::kWeatherNtpServerCapacity)
                                      ? config.weather_ntp_server
                                      : "unknown";

    char refresh_value[16]{};
    if (config.refresh_minutes != 0U) {
        std::snprintf(
            refresh_value,
            sizeof(refresh_value),
            "%lu",
            static_cast<unsigned long>(config.refresh_minutes));
    } else {
        std::snprintf(refresh_value, sizeof(refresh_value), "null");
    }

    const int written = std::snprintf(
        output,
        output_size,
        "{\"ok\":true,\"data\":{\"wifi\":{"
        "\"ssid_set\":%s,\"password_set\":%s},"
        "\"management_password_set\":%s,\"display\":{"
        "\"refresh_minutes\":%s},\"time\":{"
        "\"timezone\":\"%s\"},\"weather\":{"
        "\"configured\":%s,\"api_key_set\":%s,"
        "\"latitude_e6\":%ld,\"longitude_e6\":%ld,"
        "\"interval_minutes\":%lu,\"location\":\"%s\","
        "\"units\":\"%s\",\"language\":\"%s\","
        "\"ntp_server\":\"%s\"}}}",
        config.wifi_configured ? "true" : "false",
        config.wifi_password_configured ? "true" : "false",
        config.management_password_configured ? "true" : "false",
        refresh_value,
        timezone,
        config.weather_configured ? "true" : "false",
        config.weather_api_key_set ? "true" : "false",
        static_cast<long>(config.weather_latitude_e6),
        static_cast<long>(config.weather_longitude_e6),
        static_cast<unsigned long>(config.weather_interval_minutes),
        location,
        units,
        language,
        ntp_server);

    if (written < 0 || static_cast<std::size_t>(written) >= output_size) {
        output[0] = '\0';
        return {false, 0U};
    }
    return {true, static_cast<std::size_t>(written)};
}

}  // namespace pf_web
