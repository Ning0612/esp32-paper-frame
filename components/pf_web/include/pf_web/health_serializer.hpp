#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "pf_runtime/runtime_snapshot.hpp"

namespace pf_web {

struct SerializeResult {
    bool ok;
    std::size_t length;
};

constexpr bool is_ready(
    const pf_runtime::RuntimeSnapshot& snapshot)
{
    return snapshot.flash == pf_runtime::ServiceState::ready &&
           snapshot.psram == pf_runtime::ServiceState::ready &&
           snapshot.config == pf_runtime::ServiceState::ready &&
           snapshot.webfs == pf_runtime::ServiceState::ready &&
           snapshot.imagefs == pf_runtime::ServiceState::ready;
}

inline SerializeResult serialize_health(
    const pf_runtime::RuntimeSnapshot& snapshot,
    const bool snapshot_valid,
    const std::uint64_t uptime_ms,
    char* output,
    const std::size_t output_size)
{
    if (output == nullptr || output_size == 0) {
        return {false, 0};
    }

    const int written = std::snprintf(
        output,
        output_size,
        "{\"status\":\"%s\",\"sequence\":%lu,\"uptime_ms\":%llu,"
        "\"services\":{\"flash\":\"%s\",\"psram\":\"%s\","
        "\"config\":\"%s\",\"webfs\":\"%s\",\"imagefs\":\"%s\"}}",
        !snapshot_valid
            ? "unknown"
            : (is_ready(snapshot) ? "ready" : "degraded"),
        static_cast<unsigned long>(snapshot.sequence),
        static_cast<unsigned long long>(uptime_ms),
        pf_runtime::to_string(snapshot.flash),
        pf_runtime::to_string(snapshot.psram),
        pf_runtime::to_string(snapshot.config),
        pf_runtime::to_string(snapshot.webfs),
        pf_runtime::to_string(snapshot.imagefs));

    if (written < 0 ||
        static_cast<std::size_t>(written) >= output_size) {
        output[0] = '\0';
        return {false, 0};
    }
    return {true, static_cast<std::size_t>(written)};
}

}  // namespace pf_web
