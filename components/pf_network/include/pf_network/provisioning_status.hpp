#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace pf_network {

enum class ProvisioningOperationState : std::uint8_t {
    idle,
    saving,
    committed,
    reboot_pending,
    failed,
};

struct ProvisioningOperationStatus {
    std::uint32_t request_id = 0U;
    ProvisioningOperationState state =
        ProvisioningOperationState::idle;
};

inline bool provisioning_status_matches(
    const ProvisioningOperationStatus& status,
    const std::uint32_t request_id)
{
    return request_id != 0U &&
           status.request_id == request_id &&
           status.state != ProvisioningOperationState::idle;
}

inline const char* to_string(
    const ProvisioningOperationState state)
{
    switch (state) {
        case ProvisioningOperationState::idle:
            return "idle";
        case ProvisioningOperationState::saving:
            return "saving";
        case ProvisioningOperationState::committed:
            return "committed";
        case ProvisioningOperationState::reboot_pending:
            return "reboot_pending";
        case ProvisioningOperationState::failed:
            return "failed";
    }
    return "failed";
}

inline bool provisioning_operation_blocks_submission(
    const ProvisioningOperationState state)
{
    return state != ProvisioningOperationState::idle;
}

inline ProvisioningOperationState provisioning_state_after_ack(
    const ProvisioningOperationState state)
{
    if (state == ProvisioningOperationState::committed) {
        return ProvisioningOperationState::reboot_pending;
    }
    if (state == ProvisioningOperationState::failed) {
        return ProvisioningOperationState::idle;
    }
    return state;
}

inline bool finalize_failed_provisioning_operation(
    ProvisioningOperationStatus& status,
    const std::uint32_t request_id)
{
    if (request_id == 0U ||
        status.request_id != request_id ||
        (status.state != ProvisioningOperationState::failed &&
         status.state != ProvisioningOperationState::idle)) {
        return false;
    }
    status = {};
    return true;
}

inline bool serialize_provisioning_status(
    const ProvisioningOperationStatus& status,
    char* const destination,
    const std::size_t capacity)
{
    if (destination == nullptr || capacity == 0U ||
        status.request_id == 0U ||
        status.state == ProvisioningOperationState::idle) {
        return false;
    }

    int written = 0;
    if (status.state == ProvisioningOperationState::failed) {
        written = std::snprintf(
            destination,
            capacity,
            "{\"ok\":false,\"error\":\"credential_commit_failed\","
            "\"data\":{\"request_id\":%lu,\"state\":\"failed\","
            "\"rebooting\":false}}",
            static_cast<unsigned long>(status.request_id));
    } else {
        written = std::snprintf(
            destination,
            capacity,
            "{\"ok\":true,\"data\":{\"request_id\":%lu,"
            "\"state\":\"%s\",\"rebooting\":%s}}",
            static_cast<unsigned long>(status.request_id),
            to_string(status.state),
            status.state ==
                    ProvisioningOperationState::reboot_pending
                ? "true"
                : "false");
    }
    return written > 0 &&
           static_cast<std::size_t>(written) < capacity;
}

}  // namespace pf_network
