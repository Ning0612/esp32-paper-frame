#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "pf_config/schema.hpp"
#include "pf_config/weather_settings.hpp"
#include "pf_runtime/diagnostics_event.hpp"
#include "pf_sensors/environment_sensor.hpp"
#include "pf_sensors/light_sensor.hpp"
#include "pf_sensors/presence.hpp"
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
    bool carousel_random;
    const char* timezone;
    bool weather_configured;
    bool weather_api_key_set;
    std::int32_t weather_latitude_e6;
    std::int32_t weather_longitude_e6;
    const char* weather_units;
    const char* weather_ntp_server;
    bool environment_enabled;
    bool light_enabled;
    std::uint16_t light_threshold;
    std::uint32_t away_duration_s;
    std::uint32_t return_duration_s;
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

    const char* const environment_status = pf_sensors::to_string(
        snapshot_valid ? snapshot.environment_status
                        : pf_sensors::SensorStatus::disabled);
    const char* const light_status = pf_sensors::to_string(
        snapshot_valid ? snapshot.light_status
                        : pf_sensors::LightSensorStatus::disabled);
    const char* const presence = pf_sensors::to_string(
        snapshot_valid ? snapshot.presence
                        : pf_sensors::PresenceState::unknown);
    // Disabling the sensor must hide any reading left over from before it
    // was disabled (CLAUDE.md: never report a stale/historical value as
    // if it were current); the cache itself is intentionally not wiped
    // so a re-enable can resume backoff/daily-stat state.
    const bool environment_reading_visible =
        snapshot_valid && snapshot.environment.has_reading &&
        snapshot.environment_status != pf_sensors::SensorStatus::disabled;
    char environment_temperature[16]{};
    char environment_humidity[16]{};
    char light_adc[16]{};
    if (environment_reading_visible) {
        std::snprintf(
            environment_temperature,
            sizeof(environment_temperature),
            "%.1f",
            static_cast<double>(
                snapshot.environment.reading.temperature_c));
        std::snprintf(
            environment_humidity,
            sizeof(environment_humidity),
            "%.1f",
            static_cast<double>(
                snapshot.environment.reading.humidity_percent));
    } else {
        std::snprintf(
            environment_temperature,
            sizeof(environment_temperature),
            "null");
        std::snprintf(
            environment_humidity, sizeof(environment_humidity), "null");
    }
    if (snapshot_valid &&
        snapshot.light_status == pf_sensors::LightSensorStatus::online) {
        std::snprintf(
            light_adc,
            sizeof(light_adc),
            "%u",
            static_cast<unsigned>(snapshot.light_raw_filtered));
    } else {
        std::snprintf(light_adc, sizeof(light_adc), "null");
    }

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

    char current_image[16]{};
    if (snapshot_valid && snapshot.current_image_id != 0U) {
        std::snprintf(
            current_image,
            sizeof(current_image),
            "%lu",
            static_cast<unsigned long>(snapshot.current_image_id));
    } else {
        std::snprintf(current_image, sizeof(current_image), "null");
    }
    char next_refresh_ms[24]{};
    if (snapshot_valid && snapshot.next_carousel_due_ms != 0U) {
        std::snprintf(
            next_refresh_ms,
            sizeof(next_refresh_ms),
            "%llu",
            static_cast<unsigned long long>(
                snapshot.next_carousel_due_ms > uptime_ms
                    ? snapshot.next_carousel_due_ms - uptime_ms
                    : 0U));
    } else {
        std::snprintf(next_refresh_ms, sizeof(next_refresh_ms), "null");
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
        "\"current_image\":%s,\"next_refresh_ms\":%s},"
        "\"weather\":{\"state\":\"%s\",\"temperature\":%s,"
        "\"units\":\"%s\",\"icon\":\"%s\",\"description\":\"%s\","
        "\"humidity_percent\":%s,\"observed_at_epoch_s\":%s,"
        "\"last_failure\":\"%s\"},"
        "\"sensors\":{\"environment_status\":\"%s\","
        "\"temperature_c\":%s,\"humidity_percent\":%s,"
        "\"light_status\":\"%s\",\"light_adc\":%s,"
        "\"presence\":\"%s\"},"
        "\"diagnostics\":{\"reboot_reason\":\"%s\","
        "\"latest_sequence_id\":%lu,"
        "\"command_queue_rejected_count\":%lu,"
        "\"terminal_result_exhausted_count\":%lu,"
        "\"flash_display_lock_timeout_count\":%lu}}}",
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
        current_image,
        next_refresh_ms,
        weather_state,
        weather_temperature,
        weather_units,
        weather_icon,
        weather_description,
        weather_humidity,
        weather_observed_at,
        weather_last_failure,
        environment_status,
        environment_temperature,
        environment_humidity,
        light_status,
        light_adc,
        presence,
        snapshot_valid ? pf_runtime::to_string(snapshot.reboot_reason)
                       : "unknown",
        static_cast<unsigned long>(
            snapshot_valid ? snapshot.diagnostics_latest_sequence_id : 0U),
        static_cast<unsigned long>(
            snapshot_valid ? snapshot.command_queue_rejected_count : 0U),
        static_cast<unsigned long>(
            snapshot_valid ? snapshot.terminal_result_exhausted_count : 0U),
        static_cast<unsigned long>(
            snapshot_valid ? snapshot.flash_display_lock_timeout_count
                           : 0U));

    if (written < 0 || static_cast<std::size_t>(written) >= output_size) {
        output[0] = '\0';
        return {false, 0U};
    }
    return {true, static_cast<std::size_t>(written)};
}

// GET /api/v1/system/ota/status. ota_latest_version originates from a
// GitHub Release tag_name (external data); ota_last_error is always one of
// our own fixed literal strings. Both are still defensively validated
// before interpolation, same as the weather text fields above, rather than
// assumed safe just because the extractor is well-behaved today.
inline SerializeResult serialize_ota_status(
    const pf_runtime::RuntimeSnapshot& snapshot,
    const bool snapshot_valid,
    char* const output,
    const std::size_t output_size)
{
    if (output == nullptr || output_size == 0U) {
        return {false, 0U};
    }

    const auto safe_ota_text = [](
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

    const bool has_valid_version =
        snapshot_valid &&
        safe_ota_text(snapshot.ota_latest_version, pf_runtime::kOtaVersionCapacity) &&
        snapshot.ota_latest_version[0] != '\0';
    const char* const latest_version =
        has_valid_version ? snapshot.ota_latest_version : "unknown";
    const bool has_valid_error =
        snapshot_valid &&
        safe_ota_text(snapshot.ota_last_error, pf_runtime::kOtaErrorCapacity);
    const char* const last_error =
        has_valid_error ? snapshot.ota_last_error : "";

    const int written = std::snprintf(
        output,
        output_size,
        "{\"ok\":true,\"data\":{"
        "\"check_state\":\"%s\","
        "\"latest_version\":\"%s\","
        "\"last_check_epoch_s\":%llu,"
        "\"update_state\":\"%s\","
        "\"progress_percent\":%u,"
        "\"last_error\":\"%s\"}}",
        snapshot_valid ? pf_runtime::to_string(snapshot.ota_check_state)
                       : "unknown",
        latest_version,
        static_cast<unsigned long long>(
            snapshot_valid ? snapshot.ota_last_check_epoch_s : 0U),
        snapshot_valid ? pf_runtime::to_string(snapshot.ota_update_state)
                       : "idle",
        static_cast<unsigned>(
            snapshot_valid ? snapshot.ota_update_progress_percent : 0U),
        last_error);

    if (written < 0 || static_cast<std::size_t>(written) >= output_size) {
        output[0] = '\0';
        return {false, 0U};
    }
    return {true, static_cast<std::size_t>(written)};
}

// GET /api/v1/events?since=<sequence_id>. events/count come from
// RuntimeCoordinator::read_diagnostics_since. Event messages are always
// short caller-built ASCII (see DiagnosticEvent), so unlike the free-text
// fields elsewhere in this file they are interpolated without escaping.
inline SerializeResult serialize_events(
    const pf_runtime::DiagnosticEvent* const events,
    const std::size_t count,
    const std::uint32_t highest_sequence_id,
    const bool more_available,
    char* const output,
    const std::size_t output_size)
{
    if (output == nullptr || output_size == 0U ||
        (events == nullptr && count != 0U)) {
        return {false, 0U};
    }

    std::size_t offset = 0U;
    int written = std::snprintf(
        output + offset,
        output_size - offset,
        "{\"ok\":true,\"data\":{\"highest_sequence_id\":%lu,"
        "\"more_available\":%s,\"events\":[",
        static_cast<unsigned long>(highest_sequence_id),
        more_available ? "true" : "false");
    if (written < 0 ||
        static_cast<std::size_t>(written) >= output_size - offset) {
        output[0] = '\0';
        return {false, 0U};
    }
    offset += static_cast<std::size_t>(written);

    for (std::size_t index = 0U; index < count; ++index) {
        const pf_runtime::DiagnosticEvent& event = events[index];
        written = std::snprintf(
            output + offset,
            output_size - offset,
            "%s{\"sequence_id\":%lu,\"uptime_ms\":%llu,"
            "\"category\":\"%s\",\"severity\":\"%s\",\"message\":\"%s\"}",
            index == 0U ? "" : ",",
            static_cast<unsigned long>(event.sequence_id),
            static_cast<unsigned long long>(event.uptime_ms),
            pf_runtime::to_string(event.category),
            pf_runtime::to_string(event.severity),
            event.message);
        if (written < 0 ||
            static_cast<std::size_t>(written) >= output_size - offset) {
            output[0] = '\0';
            return {false, 0U};
        }
        offset += static_cast<std::size_t>(written);
    }

    written = std::snprintf(output + offset, output_size - offset, "]}}");
    if (written < 0 ||
        static_cast<std::size_t>(written) >= output_size - offset) {
        output[0] = '\0';
        return {false, 0U};
    }
    offset += static_cast<std::size_t>(written);

    return {true, offset};
}

// GET /api/v1/sensors readings (distinct from serialize_masked_config's
// *settings*): see docs/adr/0006-sensor-drivers-and-presence.md for the
// schema. GPIO numbers are the ADR-0003 pin assignments, not read from
// the snapshot (they are fixed at build time).
inline SerializeResult serialize_sensors(
    const pf_runtime::RuntimeSnapshot& snapshot,
    const bool snapshot_valid,
    const std::uint64_t now_epoch_s,
    char* output,
    const std::size_t output_size)
{
    if (output == nullptr || output_size == 0U) {
        return {false, 0U};
    }

    const char* const environment_status = pf_sensors::to_string(
        snapshot_valid ? snapshot.environment_status
                        : pf_sensors::SensorStatus::disabled);
    const char* const light_status = pf_sensors::to_string(
        snapshot_valid ? snapshot.light_status
                        : pf_sensors::LightSensorStatus::disabled);
    const char* const presence = pf_sensors::to_string(
        snapshot_valid ? snapshot.presence
                        : pf_sensors::PresenceState::unknown);
    const bool environment_online =
        snapshot_valid &&
        snapshot.environment_status == pf_sensors::SensorStatus::online;
    const bool environment_disabled =
        !snapshot_valid ||
        snapshot.environment_status == pf_sensors::SensorStatus::disabled;
    // Disabling the sensor must hide any reading/daily-stat left over
    // from before it was disabled (CLAUDE.md: never report a
    // stale/historical value as if it were current); the cache itself is
    // intentionally not wiped so a re-enable can resume backoff/daily-
    // stat state.
    const bool environment_has_reading =
        !environment_disabled && snapshot.environment.has_reading;
    const bool environment_stale_flag =
        environment_has_reading &&
        pf_sensors::environment_stale(snapshot.environment, now_epoch_s);
    const bool light_online =
        snapshot_valid &&
        snapshot.light_status == pf_sensors::LightSensorStatus::online;
    const bool light_saturated =
        snapshot_valid &&
        snapshot.light_status == pf_sensors::LightSensorStatus::saturated;

    char temperature_c[16]{};
    char humidity_percent[16]{};
    if (environment_has_reading) {
        std::snprintf(
            temperature_c,
            sizeof(temperature_c),
            "%.1f",
            static_cast<double>(
                snapshot.environment.reading.temperature_c));
        std::snprintf(
            humidity_percent,
            sizeof(humidity_percent),
            "%.1f",
            static_cast<double>(
                snapshot.environment.reading.humidity_percent));
    } else {
        std::snprintf(temperature_c, sizeof(temperature_c), "null");
        std::snprintf(humidity_percent, sizeof(humidity_percent), "null");
    }

    char today_temp_min[16]{};
    char today_temp_max[16]{};
    char today_temp_avg[16]{};
    char today_humidity_min[16]{};
    char today_humidity_max[16]{};
    char today_humidity_avg[16]{};
    const bool has_daily =
        !environment_disabled &&
        snapshot.environment_daily.temperature_c.has_samples;
    if (has_daily) {
        std::snprintf(
            today_temp_min,
            sizeof(today_temp_min),
            "%.1f",
            static_cast<double>(
                snapshot.environment_daily.temperature_c.min_value));
        std::snprintf(
            today_temp_max,
            sizeof(today_temp_max),
            "%.1f",
            static_cast<double>(
                snapshot.environment_daily.temperature_c.max_value));
        std::snprintf(
            today_temp_avg,
            sizeof(today_temp_avg),
            "%.1f",
            static_cast<double>(pf_sensors::daily_stat_average(
                snapshot.environment_daily.temperature_c)));
        std::snprintf(
            today_humidity_min,
            sizeof(today_humidity_min),
            "%.1f",
            static_cast<double>(
                snapshot.environment_daily.humidity_percent.min_value));
        std::snprintf(
            today_humidity_max,
            sizeof(today_humidity_max),
            "%.1f",
            static_cast<double>(
                snapshot.environment_daily.humidity_percent.max_value));
        std::snprintf(
            today_humidity_avg,
            sizeof(today_humidity_avg),
            "%.1f",
            static_cast<double>(pf_sensors::daily_stat_average(
                snapshot.environment_daily.humidity_percent)));
    } else {
        std::snprintf(today_temp_min, sizeof(today_temp_min), "null");
        std::snprintf(today_temp_max, sizeof(today_temp_max), "null");
        std::snprintf(today_temp_avg, sizeof(today_temp_avg), "null");
        std::snprintf(
            today_humidity_min, sizeof(today_humidity_min), "null");
        std::snprintf(
            today_humidity_max, sizeof(today_humidity_max), "null");
        std::snprintf(
            today_humidity_avg, sizeof(today_humidity_avg), "null");
    }

    char light_raw[16]{};
    if (light_online) {
        std::snprintf(
            light_raw,
            sizeof(light_raw),
            "%u",
            static_cast<unsigned>(snapshot.light_raw_filtered));
    } else {
        std::snprintf(light_raw, sizeof(light_raw), "null");
    }
    char light_threshold[16]{};
    if (snapshot_valid) {
        std::snprintf(
            light_threshold,
            sizeof(light_threshold),
            "%u",
            static_cast<unsigned>(snapshot.light_threshold));
    } else {
        // 0 would look like a genuinely configured threshold rather than
        // "the whole snapshot is unavailable" -- unlike light_status,
        // which already falls back to a distinguishable "disabled".
        std::snprintf(light_threshold, sizeof(light_threshold), "null");
    }

    const int written = std::snprintf(
        output,
        output_size,
        "{\"ok\":true,\"data\":{\"environment\":{"
        "\"status\":\"%s\",\"gpio\":6,\"driver\":\"dht22\","
        "\"temperature_c\":%s,\"humidity_percent\":%s,\"stale\":%s,"
        "\"today\":{\"temperature_min_c\":%s,\"temperature_max_c\":%s,"
        "\"temperature_avg_c\":%s,\"humidity_min_percent\":%s,"
        "\"humidity_max_percent\":%s,\"humidity_avg_percent\":%s}},"
        "\"light\":{\"status\":\"%s\",\"gpio\":5,\"raw\":%s,"
        "\"threshold\":%s,\"saturated\":%s},"
        "\"presence\":\"%s\"}}",
        environment_status,
        temperature_c,
        humidity_percent,
        environment_online && environment_stale_flag ? "true" : "false",
        today_temp_min,
        today_temp_max,
        today_temp_avg,
        today_humidity_min,
        today_humidity_max,
        today_humidity_avg,
        light_status,
        light_raw,
        light_threshold,
        light_saturated ? "true" : "false",
        presence);

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
        config.timezone == nullptr ||
        config.weather_units == nullptr ||
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
    const char* const units = safe_text(
                                  config.weather_units,
                                  pf_config::kWeatherUnitsCapacity)
                                  ? config.weather_units
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
        "\"refresh_minutes\":%s,\"random\":%s},\"time\":{"
        "\"timezone\":\"%s\"},\"weather\":{"
        "\"configured\":%s,\"api_key_set\":%s,"
        "\"latitude_e6\":%ld,\"longitude_e6\":%ld,"
        "\"units\":\"%s\","
        "\"ntp_server\":\"%s\"},\"sensors\":{"
        "\"environment_enabled\":%s,\"light_enabled\":%s,"
        "\"light_threshold\":%u,\"away_duration_s\":%lu,"
        "\"return_duration_s\":%lu}}}",
        config.wifi_configured ? "true" : "false",
        config.wifi_password_configured ? "true" : "false",
        config.management_password_configured ? "true" : "false",
        refresh_value,
        config.carousel_random ? "true" : "false",
        timezone,
        config.weather_configured ? "true" : "false",
        config.weather_api_key_set ? "true" : "false",
        static_cast<long>(config.weather_latitude_e6),
        static_cast<long>(config.weather_longitude_e6),
        units,
        ntp_server,
        config.environment_enabled ? "true" : "false",
        config.light_enabled ? "true" : "false",
        static_cast<unsigned>(config.light_threshold),
        static_cast<unsigned long>(config.away_duration_s),
        static_cast<unsigned long>(config.return_duration_s));

    if (written < 0 || static_cast<std::size_t>(written) >= output_size) {
        output[0] = '\0';
        return {false, 0U};
    }
    return {true, static_cast<std::size_t>(written)};
}

}  // namespace pf_web
