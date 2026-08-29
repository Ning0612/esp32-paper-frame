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

// Feeds one filtered light sample in. A `light_status` with no usable
// reading (disabled/not_detected/error -- light_status_is_decision_capable()
// is false) never advances a candidate toward `away`; presence collapses to
// `unknown` immediately in that case, matching Guild.md's "浮動、saturated
// 或 error ADC 不得觸發離席" for a genuinely absent reading.
//
// A channel pinned against a rail (low_clipped/high_clipped) is *not*
// treated as absent (docs/adr/0020-light-clip-is-diagnostic-not-presence-gate.md):
// low_clipped always reads as away (dark) and high_clipped always reads as
// present (light), decided directly from the status rather than by
// comparing the clipped raw against `threshold`. `threshold` is a
// user-editable 0-4095 field (SensorSettings, WebUI) with nothing pinning
// it inside the 11-4084 band ADR-0018 only recommends for the *achievable
// raw range* -- a threshold a user set at or beyond a rail (0, or
// 4085-4095) would otherwise flip the comparison against what the rail
// actually means (e.g. threshold=4095 would read a high_clipped/brightest-
// possible raw=4085 as "away"). Deciding by status sidesteps that entirely.
// Treating clip as unknown was the shipped bug this ADR fixes: both
// channels clipping dark at once (measured 2026-08-27, see HARDWARE.md)
// collapsed presence to `unknown` and reset an in-progress away countdown,
// leaving the panel refreshing instead of blanking.
//
// For `online`, below `threshold` reads as away (dark), at/above reads as
// present.
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

    if (!light_status_is_decision_capable(light_status)) {
        tracker.candidate = PresenceState::unknown;
        tracker.candidate_since_ms = now_ms;
        tracker.state = PresenceState::unknown;
        return {tracker.state, tracker.state != previous_state};
    }

    PresenceState instantaneous;
    if (light_status == LightSensorStatus::low_clipped) {
        instantaneous = PresenceState::away;
    } else if (light_status == LightSensorStatus::high_clipped) {
        instantaneous = PresenceState::present;
    } else {
        instantaneous = light_raw_filtered < threshold
                             ? PresenceState::away
                             : PresenceState::present;
    }
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

// What the panel owner has to do about a presence change.
enum class PresencePanelAction : std::uint8_t {
    none,
    blank_for_away,
    restore_after_away,
};

// The transition table lives here rather than inline in app_main so it can
// be tested without hardware.
//
// `blank_on_panel` is what stops the boot case from being read as a
// return: presence starts `unknown` and converges to `present` a few
// seconds into every boot, and treating that as "the user came back" spent
// a full ~31 s panel refresh restoring a panel that was never blanked
// (2026-08-23, reproduced across two reboots). Keying on what the panel is
// actually showing -- rather than on which state it came from -- also
// keeps away -> unknown -> present working: a sensor fault mid-debounce
// leaves the blank on the panel, and that really does need restoring.
inline constexpr PresencePanelAction presence_panel_action(
    const PresenceState previous,
    const PresenceState current,
    const bool blank_on_panel)
{
    if (current == previous) {
        return PresencePanelAction::none;
    }
    if (current == PresenceState::away) {
        return PresencePanelAction::blank_for_away;
    }
    if (current == PresenceState::present) {
        return blank_on_panel ? PresencePanelAction::restore_after_away
                              : PresencePanelAction::none;
    }
    // Collapsing to `unknown` (sensor error) neither blanks nor restores:
    // presence is simply not known, and whatever is on the panel stays.
    return PresencePanelAction::none;
}

}  // namespace pf_sensors
