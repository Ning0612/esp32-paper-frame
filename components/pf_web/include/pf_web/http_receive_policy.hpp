#pragma once

#include <cstdint>

namespace pf_web {

inline bool body_receive_deadline_expired(
    const std::uint64_t now_ms,
    const std::uint64_t started_ms,
    const std::uint32_t maximum_ms)
{
    return now_ms - started_ms >= maximum_ms;
}

inline bool body_receive_idle_limit_reached(
    const std::uint8_t timeout_count,
    const std::uint8_t maximum_timeouts)
{
    return maximum_timeouts == 0U ||
           timeout_count >= maximum_timeouts;
}

}  // namespace pf_web
