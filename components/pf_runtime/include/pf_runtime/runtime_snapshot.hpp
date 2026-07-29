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

struct RuntimeSnapshot {
    std::uint32_t sequence;
    ServiceState flash;
    ServiceState psram;
    ServiceState config;
    ServiceState webfs;
    ServiceState imagefs;
    DisplayState display;
    std::uint32_t active_display_request_id;
    std::uint8_t queued_display_count;
    std::uint32_t last_display_request_id;
    DisplayOutcome last_display_outcome;
    std::uint8_t last_display_stage;
};

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
