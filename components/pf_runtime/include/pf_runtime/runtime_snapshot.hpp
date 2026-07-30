#pragma once

#include <cstdint>

#include "pf_runtime/runtime_messages.hpp"

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

struct RuntimeSnapshot {
    std::uint32_t sequence;
    ServiceState flash;
    ServiceState psram;
    ServiceState config;
    ServiceState webfs;
    ServiceState imagefs;
    WifiState wifi;
    InternetState internet;
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
