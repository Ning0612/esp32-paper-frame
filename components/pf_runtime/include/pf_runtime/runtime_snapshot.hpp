#pragma once

#include <cstdint>

#include "pf_runtime/runtime_messages.hpp"
#include "pf_sensors/daily_stats.hpp"
#include "pf_sensors/environment_sensor.hpp"
#include "pf_sensors/light_sensor.hpp"
#include "pf_sensors/presence.hpp"
#include "pf_weather/weather.hpp"

namespace pf_runtime {

enum class ServiceState : std::uint8_t {
    unknown,
    ready,
    degraded,
};

enum class DisplayState : std::uint8_t {
    unknown,
    queued,
    refreshing,
    deep_sleep,
    failed,
};

constexpr const char* to_string(const DisplayState state)
{
    switch (state) {
        case DisplayState::unknown:
            return "unknown";
        case DisplayState::queued:
            return "queued";
        case DisplayState::refreshing:
            return "refreshing";
        case DisplayState::deep_sleep:
            return "deep_sleep";
        case DisplayState::failed:
            return "failed";
    }
    return "unknown";
}

enum class WifiState : std::uint8_t {
    unknown,
    connecting,
    connected,
    starting_ap,
    provisioning,
    failed,
};

enum class InternetState : std::uint8_t {
    unknown,
    reachable,
    unreachable,
};

// Mirrors pf_network::TimeSyncState; pf_network translates into this type
// when publishing so pf_runtime stays free of a pf_network dependency.
enum class TimeSyncState : std::uint8_t {
    unsynced,
    syncing,
    synced,
};

struct RuntimeSnapshot {
    std::uint32_t sequence;
    ServiceState flash;
    ServiceState psram;
    ServiceState config;
    ServiceState webfs;
    ServiceState imagefs;
    WifiState wifi;
    InternetState internet;
    TimeSyncState time_sync;
    DisplayState display;
    std::uint32_t active_display_request_id;
    std::uint8_t queued_display_count;
    std::uint32_t last_display_request_id;
    DisplayOutcome last_display_outcome;
    std::uint8_t last_display_stage;
    // Capacity values are captured during startup and copied with the
    // snapshot. A zero value is rendered as unknown unless its service is
    // ready; no handler probes storage or hardware on the request path.
    std::uint32_t flash_bytes = 0;
    std::uint32_t psram_bytes = 0;
    std::uint32_t webfs_total_bytes = 0;
    std::uint32_t webfs_used_bytes = 0;
    std::uint32_t imagefs_total_bytes = 0;
    std::uint32_t imagefs_used_bytes = 0;
    std::uint32_t carousel_refresh_minutes = 0;
    // Value copy of the latest weather fetch cache; units records what the
    // cached observation was fetched in, since Observation itself does not
    // carry that (the API response never echoes back the requested units).
    pf_weather::Cache weather{};
    char weather_units[pf_weather::kUnitsCapacity]{};
    // Phase 7 sensor/presence state; see
    // docs/adr/0006-sensor-drivers-and-presence.md.
    pf_sensors::EnvironmentCache environment{};
    pf_sensors::SensorStatus environment_status =
        pf_sensors::SensorStatus::disabled;
    pf_sensors::DailyStats environment_daily{};
    pf_sensors::LightSensorStatus light_status =
        pf_sensors::LightSensorStatus::disabled;
    std::uint16_t light_raw_filtered = 0U;
    std::uint16_t light_threshold = 0U;
    pf_sensors::PresenceState presence = pf_sensors::PresenceState::unknown;
    // Set by RuntimeCoordinator::request_manual_carousel_activation() when a
    // WebUI "activate this image" request commits successfully. The carousel
    // poll loop compares manual_activate_request_id against the value it last
    // observed (not manual_activate_image_id alone) so a repeat request for
    // the same id is still detected as a new request, mirroring the
    // presence-transition detection pattern above. 0 means "none yet".
    std::uint32_t manual_activate_request_id = 0U;
    std::uint32_t manual_activate_image_id = 0U;
};

constexpr const char* to_string(const WifiState state)
{
    switch (state) {
        case WifiState::unknown:
            return "unknown";
        case WifiState::connecting:
            return "connecting";
        case WifiState::connected:
            return "connected";
        case WifiState::starting_ap:
            return "starting_ap";
        case WifiState::provisioning:
            return "provisioning";
        case WifiState::failed:
            return "failed";
    }
    return "unknown";
}

constexpr const char* to_string(const InternetState state)
{
    switch (state) {
        case InternetState::unknown:
            return "unknown";
        case InternetState::reachable:
            return "reachable";
        case InternetState::unreachable:
            return "unreachable";
    }
    return "unknown";
}

constexpr const char* to_string(const TimeSyncState state)
{
    switch (state) {
        case TimeSyncState::unsynced:
            return "unsynced";
        case TimeSyncState::syncing:
            return "syncing";
        case TimeSyncState::synced:
            return "synced";
    }
    return "unsynced";
}

constexpr const char* to_string(const ServiceState state)
{
    switch (state) {
        case ServiceState::unknown:
            return "unknown";
        case ServiceState::ready:
            return "ready";
        case ServiceState::degraded:
            return "degraded";
    }
    return "unknown";
}

}  // namespace pf_runtime
