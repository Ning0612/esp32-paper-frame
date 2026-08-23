#include "pf_display/display_task_esp_idf.hpp"

#include "esp_heap_caps.h"
#include "esp_log.h"

namespace pf_display {
namespace {

constexpr char kTag[] = "display_task";

pf_runtime::RuntimeResult failed_result(
    const pf_runtime::RuntimeCommand& command,
    const pf_runtime::RuntimeError error,
    const pf_runtime::DisplayOutcome outcome)
{
    return {
        command.request_id,
        pf_runtime::ResultStatus::failed,
        error,
        outcome,
        static_cast<std::uint8_t>(DriverStage::validate),
        false,
    };
}

}  // namespace

DisplayTask::DisplayTask()
    : submitter_(pool_, publisher_),
      panel_(transport_),
      processor_(panel_)
{
}

esp_err_t DisplayTask::start(
    pf_runtime::RuntimeCoordinator& runtime)
{
    if (task_handle_ != nullptr || runtime_ != nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    for (std::size_t index = 0;
         index < DisplayFramePool::kSlotCount;
         ++index) {
        frame_slots_[index] = static_cast<std::uint8_t*>(
            heap_caps_malloc(
                kFullFramebufferBytes,
                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (frame_slots_[index] == nullptr) {
            for (std::size_t allocated = 0;
                 allocated < index;
                 ++allocated) {
                heap_caps_free(frame_slots_[allocated]);
                frame_slots_[allocated] = nullptr;
            }
            return ESP_ERR_NO_MEM;
        }
    }

    if (!pool_.initialize(frame_slots_[0], frame_slots_[1])) {
        for (std::uint8_t*& slot : frame_slots_) {
            heap_caps_free(slot);
            slot = nullptr;
        }
        return ESP_ERR_INVALID_STATE;
    }

    runtime_ = &runtime;
    publisher_.bind(runtime);
    task_handle_ = xTaskCreateStatic(
        &DisplayTask::task_entry,
        "DisplayTask",
        kTaskStackWords,
        this,
        kTaskPriority,
        task_stack_,
        &task_control_);
    if (task_handle_ == nullptr) {
        rollback_start();
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

FrameWriteLease DisplayTask::try_acquire_frame()
{
    return started() ? pool_.try_acquire_write() : FrameWriteLease{};
}

SubmitStatus DisplayTask::try_submit_refresh(
    const std::uint32_t request_id,
    FrameWriteLease& lease)
{
    return started()
               ? submitter_.try_submit(request_id, lease)
               : SubmitStatus::not_started;
}

bool DisplayTask::started() const
{
    return task_handle_ != nullptr;
}

void DisplayTask::task_entry(void* const context)
{
    static_cast<DisplayTask*>(context)->run();
}

void DisplayTask::rollback_start()
{
    publisher_.unbind();
    runtime_ = nullptr;
    pool_.reset();
    for (std::uint8_t*& slot : frame_slots_) {
        heap_caps_free(slot);
        slot = nullptr;
    }
}

void DisplayTask::run()
{
    const esp_err_t transport_result = transport_.initialize();
    transport_ready_.store(
        transport_result == ESP_OK,
        std::memory_order_release);
    if (transport_result == ESP_OK) {
        ESP_LOGI(kTag, "panel transport ready; waiting for refresh commands");
    } else {
        ESP_LOGE(
            kTag,
            "panel transport initialization failed: %s",
            esp_err_to_name(transport_result));
    }

    while (true) {
        pf_runtime::RuntimeCommand command{};
        if (runtime_ != nullptr &&
            runtime_->receive_command(command, portMAX_DELAY)) {
            process_command(command);
        } else {
            vTaskDelay(pdMS_TO_TICKS(10U));
        }
    }
}

void DisplayTask::process_command(
    const pf_runtime::RuntimeCommand& command)
{
    runtime_->update_display_started(command.request_id);
    FrameReadLease frame = pool_.try_begin_display(command.frame);
    if (!frame.valid()) {
        const pf_runtime::RuntimeResult result{
            command.request_id,
            pf_runtime::ResultStatus::rejected,
            pf_runtime::RuntimeError::invalid_state,
            pf_runtime::DisplayOutcome::invalid_lease,
            static_cast<std::uint8_t>(DriverStage::validate),
            false,
        };
        runtime_->update_display_finished(
            result.request_id,
            result.display_outcome,
            result.driver_stage);
        publish_result(result);
        return;
    }

    pf_runtime::RuntimeResult result{};
    if (!transport_ready_.load(std::memory_order_acquire)) {
        result = failed_result(
            command,
            pf_runtime::RuntimeError::invalid_state,
            pf_runtime::DisplayOutcome::transport_error);
    } else if (!runtime_->lock_flash_display(portMAX_DELAY)) {
        result = failed_result(
            command,
            pf_runtime::RuntimeError::invalid_state,
            pf_runtime::DisplayOutcome::panel_state_error);
    } else {
        result = processor_.process(
            command,
            frame.data(),
            frame.size());
        runtime_->unlock_flash_display();
    }

    frame.release();
    runtime_->update_display_finished(
        result.request_id,
        result.display_outcome,
        result.driver_stage);
    publish_result(result);
}

void DisplayTask::publish_result(
    const pf_runtime::RuntimeResult& result)
{
    if (!runtime_->retain_terminal_result(result)) {
        ESP_LOGE(
            kTag,
            "terminal result retention failed; request=%lu snapshot only",
            static_cast<unsigned long>(result.request_id));
    }
    if (!runtime_->try_publish_result(result)) {
        ESP_LOGW(
            kTag,
            "result event queue full; request=%lu outcome=%u",
            static_cast<unsigned long>(result.request_id),
            static_cast<unsigned>(result.display_outcome));
    }
}

DisplayTask& display_task()
{
    static DisplayTask instance;
    return instance;
}

}  // namespace pf_display
