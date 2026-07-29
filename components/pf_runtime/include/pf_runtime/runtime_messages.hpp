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

struct RuntimeResult {
    std::uint32_t request_id;
    ResultStatus status;
    RuntimeError error;
    DisplayOutcome display_outcome;
    std::uint8_t driver_stage;
};

static_assert(std::is_trivially_copyable_v<FrameToken>);
static_assert(std::is_trivially_copyable_v<RuntimeCommand>);
static_assert(std::is_trivially_copyable_v<RuntimeResult>);
static_assert(sizeof(RuntimeCommand) <= 16U);

}  // namespace pf_runtime
