#pragma once

#include <cstdint>
#include <type_traits>

namespace pf_runtime {

enum class CommandKind : std::uint8_t {
    refresh_display,
};

struct FrameToken {
    std::uint8_t slot;
    std::uint32_t generation;
};

struct RuntimeCommand {
    std::uint32_t request_id;
    CommandKind kind;
    FrameToken frame;
};

enum class ResultStatus : std::uint8_t {
    completed,
    rejected,
    failed,
};

enum class RuntimeError : std::uint8_t {
    none,
    invalid_argument,
    invalid_state,
    timeout,
    transport,
};

enum class DisplayOutcome : std::uint8_t {
    none,
    refreshed_and_slept,
    invalid_lease,
    busy_timeout,
    transport_error,
    panel_state_error,
};

constexpr const char* to_string(const DisplayOutcome outcome)
{
    switch (outcome) {
        case DisplayOutcome::none:
            return "none";
        case DisplayOutcome::refreshed_and_slept:
            return "refreshed_and_slept";
        case DisplayOutcome::invalid_lease:
            return "invalid_lease";
        case DisplayOutcome::busy_timeout:
            return "busy_timeout";
        case DisplayOutcome::transport_error:
            return "transport_error";
        case DisplayOutcome::panel_state_error:
            return "panel_state_error";
    }
    return "none";
}

struct RuntimeResult {
    std::uint32_t request_id;
    ResultStatus status;
    RuntimeError error;
    DisplayOutcome display_outcome;
    std::uint8_t driver_stage;
    // Whether the frame reached the panel, which is a different question
    // from whether the command succeeded: the picture is displayed once
    // DISPLAY_REFRESH completes, four steps before the panel is asked to
    // sleep. A failure after that point leaves a correct picture on an
    // awake panel, and redrawing it costs a full ~31 s refresh for no
    // visible change. See
    // docs/adr/0019-separate-frame-displayed-from-panel-slept.md.
    // Deliberately has no default member initialiser so that
    // -Werror=missing-field-initializers catches any construction site
    // that forgets to answer this.
    bool frame_on_panel;
};

static_assert(std::is_trivially_copyable_v<FrameToken>);
static_assert(std::is_trivially_copyable_v<RuntimeCommand>);
static_assert(std::is_trivially_copyable_v<RuntimeResult>);
static_assert(sizeof(RuntimeCommand) <= 16U);

}  // namespace pf_runtime
