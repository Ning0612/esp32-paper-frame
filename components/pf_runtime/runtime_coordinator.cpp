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

    flash_display_mutex_ =
        xSemaphoreCreateMutexStatic(&flash_display_mutex_control_);
    if (flash_display_mutex_ == nullptr) {
        command_queue_ = nullptr;
        result_queue_ = nullptr;
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
    if (command_queue_ == nullptr) {
        return false;
    }

    DisplayState previous_display = DisplayState::unknown;
    if (command.kind == CommandKind::refresh_display) {
        portENTER_CRITICAL(&snapshot_lock_);
        previous_display = snapshot_.display;
        ++snapshot_.queued_display_count;
        if (snapshot_.display != DisplayState::refreshing) {
            snapshot_.display = DisplayState::queued;
        }
        ++snapshot_.sequence;
        portEXIT_CRITICAL(&snapshot_lock_);
    }

    const bool submitted =
        xQueueSend(command_queue_, &command, 0) == pdTRUE;
    if (!submitted && command.kind == CommandKind::refresh_display) {
        portENTER_CRITICAL(&snapshot_lock_);
        if (snapshot_.queued_display_count > 0) {
            --snapshot_.queued_display_count;
        }
        if (snapshot_.queued_display_count == 0 &&
            snapshot_.display != DisplayState::refreshing) {
            snapshot_.display = previous_display;
        }
        ++snapshot_.sequence;
        portEXIT_CRITICAL(&snapshot_lock_);
    }
    return submitted;
}

bool RuntimeCoordinator::try_receive_command(RuntimeCommand& command)
{
    return receive_command(command, 0);
}

bool RuntimeCoordinator::receive_command(
    RuntimeCommand& command,
    const TickType_t wait_ticks)
{
    return command_queue_ != nullptr &&
           xQueueReceive(
               command_queue_,
               &command,
               wait_ticks) == pdTRUE;
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

void RuntimeCoordinator::update_display_started(
    const std::uint32_t request_id)
{
    portENTER_CRITICAL(&snapshot_lock_);
    if (snapshot_.queued_display_count > 0) {
        --snapshot_.queued_display_count;
    }
    snapshot_.display = DisplayState::refreshing;
    snapshot_.active_display_request_id = request_id;
    ++snapshot_.sequence;
    portEXIT_CRITICAL(&snapshot_lock_);
}

void RuntimeCoordinator::update_display_finished(
    const std::uint32_t request_id,
    const DisplayOutcome outcome,
    const std::uint8_t driver_stage)
{
    portENTER_CRITICAL(&snapshot_lock_);
    snapshot_.active_display_request_id = 0;
    snapshot_.display =
        snapshot_.queued_display_count > 0
            ? DisplayState::queued
            : (outcome == DisplayOutcome::refreshed_and_slept
                   ? DisplayState::deep_sleep
                   : DisplayState::failed);
    snapshot_.last_display_request_id = request_id;
    snapshot_.last_display_outcome = outcome;
    snapshot_.last_display_stage = driver_stage;
    ++snapshot_.sequence;
    portEXIT_CRITICAL(&snapshot_lock_);
}

bool RuntimeCoordinator::lock_flash_display(
    const TickType_t wait_ticks)
{
    return flash_display_mutex_ != nullptr &&
           xSemaphoreTake(flash_display_mutex_, wait_ticks) == pdTRUE;
}

void RuntimeCoordinator::unlock_flash_display()
{
    if (flash_display_mutex_ != nullptr) {
        xSemaphoreGive(flash_display_mutex_);
    }
}

RuntimeCoordinator& coordinator()
{
    static RuntimeCoordinator instance;
    return instance;
}

}  // namespace pf_runtime
