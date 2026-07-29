#include "pf_runtime/runtime_coordinator.hpp"

namespace pf_runtime {

esp_err_t RuntimeCoordinator::initialize(
    const RuntimeSnapshot& initial_snapshot)
{
    if (command_queue_ != nullptr || result_queue_ != nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    publish_snapshot(initial_snapshot);

    command_queue_ = xQueueCreateStatic(
        kQueueLength,
        sizeof(RuntimeCommand),
        command_queue_storage_,
        &command_queue_control_);
    if (command_queue_ == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    result_queue_ = xQueueCreateStatic(
        kQueueLength,
        sizeof(RuntimeResult),
        result_queue_storage_,
        &result_queue_control_);
    if (result_queue_ == nullptr) {
        command_queue_ = nullptr;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

bool RuntimeCoordinator::read_snapshot(
    RuntimeSnapshot& destination) const
{
    portENTER_CRITICAL(&snapshot_lock_);
    const bool valid = snapshot_valid_;
    if (valid) {
        destination = snapshot_;
    }
    portEXIT_CRITICAL(&snapshot_lock_);
    return valid;
}

void RuntimeCoordinator::publish_snapshot(
    const RuntimeSnapshot& snapshot)
{
    portENTER_CRITICAL(&snapshot_lock_);
    snapshot_ = snapshot;
    snapshot_valid_ = true;
    portEXIT_CRITICAL(&snapshot_lock_);
}

bool RuntimeCoordinator::try_submit_command(
    const RuntimeCommand& command)
{
    return command_queue_ != nullptr &&
           xQueueSend(command_queue_, &command, 0) == pdTRUE;
}

bool RuntimeCoordinator::try_receive_command(RuntimeCommand& command)
{
    return command_queue_ != nullptr &&
           xQueueReceive(command_queue_, &command, 0) == pdTRUE;
}

bool RuntimeCoordinator::try_publish_result(const RuntimeResult& result)
{
    return result_queue_ != nullptr &&
           xQueueSend(result_queue_, &result, 0) == pdTRUE;
}

bool RuntimeCoordinator::try_receive_result(RuntimeResult& result)
{
    return result_queue_ != nullptr &&
           xQueueReceive(result_queue_, &result, 0) == pdTRUE;
}

RuntimeCoordinator& coordinator()
{
    static RuntimeCoordinator instance;
    return instance;
}

}  // namespace pf_runtime
