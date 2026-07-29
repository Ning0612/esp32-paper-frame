#pragma once

#include <cstdint>

namespace pf_runtime {

enum class ServiceState : std::uint8_t {
    unknown,
    ready,
    degraded,
};

struct RuntimeSnapshot {
    std::uint32_t sequence;
    ServiceState flash;
    ServiceState psram;
    ServiceState config;
    ServiceState webfs;
    ServiceState imagefs;
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
