#pragma once

#include <cstddef>
#include <cstdint>

#include "pf_runtime/reboot_reason.hpp"
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

// Phase 8 OTA (docs/adr/0008-ota-github-releases-and-rollback.md). Firmware
// image updates only; webfs continues to use the separate manual esptool
// flow documented in CLAUDE.md.
enum class OtaCheckState : std::uint8_t {
    unknown,
    checking,
    up_to_date,
    update_available,
    check_failed,
};

enum class OtaUpdateState : std::uint8_t {
    idle,
    downloading,
    writing,
    // The image was downloaded, verified, and the boot partition switched
    // (esp_https_ota_finish() succeeded) -- this IS the success state.
    // ota_last_error may still be non-empty here, specifically
    // "manual_reboot_required" when the automatic post-update reboot
    // failed to arm: that is an actionable warning meaning "succeeded, new
    // firmware will run on whatever the next reboot is, but the automatic
    // one didn't fire" -- NOT an update failure. UI/API consumers must not
    // render ready_pending_reboot + non-empty ota_last_error as "OTA
    // failed".
    ready_pending_reboot,
    failed,
};

constexpr const char* to_string(const OtaCheckState state)
{
    switch (state) {
        case OtaCheckState::unknown:
            return "unknown";
        case OtaCheckState::checking:
            return "checking";
        case OtaCheckState::up_to_date:
            return "up_to_date";
        case OtaCheckState::update_available:
            return "update_available";
        case OtaCheckState::check_failed:
            return "check_failed";
    }
    return "unknown";
}

constexpr const char* to_string(const OtaUpdateState state)
{
    switch (state) {
        case OtaUpdateState::idle:
            return "idle";
        case OtaUpdateState::downloading:
            return "downloading";
        case OtaUpdateState::writing:
            return "writing";
        case OtaUpdateState::ready_pending_reboot:
            return "ready_pending_reboot";
        case OtaUpdateState::failed:
            return "failed";
    }
    return "idle";
}

inline constexpr std::size_t kOtaVersionCapacity = 24U;
inline constexpr std::size_t kOtaErrorCapacity = 32U;
inline constexpr std::size_t kIpAddressCapacity = 16U;

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
    bool carousel_random = false;
    // A non-zero request id means the WebUI has requested a new carousel
    // mode and interval. The app_main carousel owner applies them once no
    // refresh is in flight.
    std::uint32_t carousel_mode_request_id = 0U;
    bool carousel_mode_request_random = false;
    std::uint32_t carousel_mode_request_refresh_minutes = 0U;
    // Current device IPv4 address. Empty means the active network interface
    // does not have an address yet; the display renders an explicit
    // placeholder instead of fabricating one.
    char ip_address[kIpAddressCapacity]{};
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
    // Phase 8 diagnostics/observability (docs/IMPLEMENTATION_PLAN.md Phase 8).
    // reboot_reason is captured once at boot from esp_reset_reason() and
    // never changes for the lifetime of the process.
    RebootReason reboot_reason = RebootReason::unknown;
    std::uint32_t diagnostics_latest_sequence_id = 0U;
    std::uint32_t command_queue_rejected_count = 0U;
    std::uint32_t terminal_result_exhausted_count = 0U;
    std::uint32_t flash_display_lock_timeout_count = 0U;
    // Phase 8 OTA (docs/adr/0008-ota-github-releases-and-rollback.md).
    OtaCheckState ota_check_state = OtaCheckState::unknown;
    char ota_latest_version[kOtaVersionCapacity]{};
    std::uint64_t ota_last_check_epoch_s = 0U;
    OtaUpdateState ota_update_state = OtaUpdateState::idle;
    std::uint8_t ota_update_progress_percent = 0U;
    char ota_last_error[kOtaErrorCapacity]{};
    // Phase 8 Dashboard completion. current_image_id is 0 when the
    // carousel is showing the built-in welcome/status frame rather than a
    // catalogued image. next_carousel_due_ms is a monotonic ms-since-boot
    // timestamp (comparable against uptime_ms from the same snapshot
    // read), 0 meaning "not yet known".
    std::uint32_t current_image_id = 0U;
    std::uint64_t next_carousel_due_ms = 0U;
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
