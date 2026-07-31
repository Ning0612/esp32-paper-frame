#pragma once

#include <cstdint>

#include "pf_sensors/light_sensor.hpp"

namespace pf_sensors {

enum class PresenceState : std::uint8_t {
    unknown,
    present,
    away,
};

constexpr const char* to_string(const PresenceState state)
{
    switch (state) {
        case PresenceState::unknown:
            return "unknown";
        case PresenceState::present:
            return "present";
        case PresenceState::away:
            return "away";
    }
    return "unknown";
}

// Duration-based debounce, not an event-driven state machine (see
// docs/adr/0006-sensor-drivers-and-presence.md): tracks a candidate state
// and how long it has held; only promotes the candidate to the stable
// `state` once it has been continuously observed for the required
// duration.
struct PresenceTracker {
    PresenceState state = PresenceState::unknown;
    PresenceState candidate = PresenceState::unknown;
    std::uint64_t candidate_since_ms = 0U;
};

struct PresenceUpdateResult {
    PresenceState state = PresenceState::unknown;
    bool transitioned = false;
};

// Feeds one filtered light sample in. `light_status` other than `online`
// (disabled/not_detected/error/saturated) never advances a candidate
// toward `away`; presence collapses to `unknown` immediately in that
// case, matching Guild.md's "浮動、saturated 或 error ADC 不得觸發離席".
// Below `threshold` reads as away (dark), at/above reads as present.
inline PresenceUpdateResult update_presence(
    PresenceTracker& tracker,
    const LightSensorStatus light_status,
    const std::uint16_t light_raw_filtered,
    const std::uint16_t threshold,
    const std::uint64_t now_ms,
    const std::uint64_t away_duration_ms,
    const std::uint64_t return_duration_ms)
{
    const PresenceState previous_state = tracker.state;

    if (light_status != LightSensorStatus::online) {
        tracker.candidate = PresenceState::unknown;
        tracker.candidate_since_ms = now_ms;
        tracker.state = PresenceState::unknown;
        return {tracker.state, tracker.state != previous_state};
    }

    const PresenceState instantaneous = light_raw_filtered < threshold
                                             ? PresenceState::away
                                             : PresenceState::present;
    if (instantaneous != tracker.candidate) {
        tracker.candidate = instantaneous;
        tracker.candidate_since_ms = now_ms;
    }

    if (tracker.candidate == tracker.state) {
        return {tracker.state, false};
    }

    const std::uint64_t required_duration_ms =
        tracker.candidate == PresenceState::away ? away_duration_ms
                                                   : return_duration_ms;
    const std::uint64_t held_ms = now_ms >= tracker.candidate_since_ms
                                       ? now_ms - tracker.candidate_since_ms
                                       : 0U;
    if (held_ms >= required_duration_ms) {
        tracker.state = tracker.candidate;
    }

    return {tracker.state, tracker.state != previous_state};
}

}  // namespace pf_sensors
