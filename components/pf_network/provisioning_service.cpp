#include "pf_network/provisioning_service.hpp"

#include "esp_log.h"
#include "esp_system.h"
#include "pf_config/config_manager.hpp"
#include "pf_config/secure_memory.hpp"
#include "pf_runtime/runtime_coordinator.hpp"

namespace pf_network {
namespace {

constexpr char kTag[] = "pf_provisioning";

}  // namespace

esp_err_t ProvisioningService::start(
    pf_runtime::RuntimeCoordinator& runtime)
{
    if (queue_ != nullptr || task_handle_ != nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    status_mutex_ =
        xSemaphoreCreateMutexStatic(&status_mutex_control_);
    if (status_mutex_ == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    queue_ = xQueueCreateStatic(
        1U,
        sizeof(QueuedRequest),
        queue_storage_,
        &queue_control_);
    if (queue_ == nullptr) {
        status_mutex_ = nullptr;
        return ESP_ERR_NO_MEM;
    }
    runtime_ = &runtime;
    task_handle_ = xTaskCreateStatic(
        &ProvisioningService::task_entry,
        "ProvisioningStore",
        kTaskStackWords,
        this,
        kTaskPriority,
        task_stack_,
        &task_control_);
    if (task_handle_ == nullptr) {
        queue_ = nullptr;
        status_mutex_ = nullptr;
        runtime_ = nullptr;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

ProvisioningSubmitResult ProvisioningService::submit(
    const pf_config::NetworkCredentials& credentials)
{
    if (!pf_config::network_credentials_valid(credentials)) {
        return {ProvisioningSubmitStatus::invalid, 0U};
    }
    if (queue_ == nullptr || task_handle_ == nullptr ||
        status_mutex_ == nullptr || runtime_ == nullptr) {
        return {ProvisioningSubmitStatus::unavailable, 0U};
    }
    if (xSemaphoreTake(
            status_mutex_,
            pdMS_TO_TICKS(50U)) != pdTRUE) {
        return {ProvisioningSubmitStatus::busy, 0U};
    }
    if (provisioning_operation_blocks_submission(
            operation_status_.state)) {
        xSemaphoreGive(status_mutex_);
        return {ProvisioningSubmitStatus::busy, 0U};
    }

    std::uint32_t request_id = next_request_id_++;
    if (request_id == 0U) {
        request_id = next_request_id_++;
    }
    if (next_request_id_ == 0U) {
        ++next_request_id_;
    }
    QueuedRequest request{
        .request_id = request_id,
        .credentials = credentials,
    };
    const bool queued =
        xQueueSend(queue_, &request, 0) == pdTRUE;
    pf_config::secure_zero(request.credentials);
    if (!queued) {
        xSemaphoreGive(status_mutex_);
        return {ProvisioningSubmitStatus::busy, 0U};
    }
    operation_status_ = {
        .request_id = request_id,
        .state = ProvisioningOperationState::saving,
    };
    xSemaphoreGive(status_mutex_);
    return {ProvisioningSubmitStatus::accepted, request_id};
}

bool ProvisioningService::status(
    ProvisioningOperationStatus& destination)
{
    if (status_mutex_ == nullptr ||
        xSemaphoreTake(
            status_mutex_,
            pdMS_TO_TICKS(50U)) != pdTRUE) {
        return false;
    }
    destination = operation_status_;
    xSemaphoreGive(status_mutex_);
    return true;
}

bool ProvisioningService::acknowledge_terminal(
    const std::uint32_t request_id)
{
    if (request_id == 0U || status_mutex_ == nullptr ||
        task_handle_ == nullptr ||
        xSemaphoreTake(
            status_mutex_,
            pdMS_TO_TICKS(50U)) != pdTRUE) {
        return false;
    }
    const bool acknowledged =
        operation_status_.request_id == request_id &&
        (operation_status_.state ==
             ProvisioningOperationState::committed ||
         operation_status_.state ==
             ProvisioningOperationState::failed);
    if (acknowledged) {
        operation_status_.state =
            provisioning_state_after_ack(
                operation_status_.state);
        xTaskNotifyGive(task_handle_);
    }
    xSemaphoreGive(status_mutex_);
    return acknowledged;
}

void ProvisioningService::task_entry(void* context)
{
    static_cast<ProvisioningService*>(context)->task_main();
}

void ProvisioningService::task_main()
{
    while (true) {
        QueuedRequest request{};
        if (xQueueReceive(
                queue_,
                &request,
                portMAX_DELAY) != pdTRUE) {
            continue;
        }
        pf_config::secure_zero(
            queue_storage_,
            sizeof(queue_storage_));
        esp_err_t result = ESP_ERR_INVALID_STATE;
        bool flash_gate_locked = false;
        if (runtime_ != nullptr &&
            runtime_->lock_flash_display(portMAX_DELAY)) {
            flash_gate_locked = true;
            result = pf_config::save_network_credentials(
                request.credentials);
        }
        if (status_mutex_ != nullptr &&
            xSemaphoreTake(
                status_mutex_,
                portMAX_DELAY) == pdTRUE) {
            operation_status_ = {
                .request_id = request.request_id,
                .state =
                    result == ESP_OK
                        ? ProvisioningOperationState::committed
                        : ProvisioningOperationState::failed,
            };
            xSemaphoreGive(status_mutex_);
        }
        if (flash_gate_locked) {
            runtime_->unlock_flash_display();
        }
        pf_config::secure_zero(request.credentials);
        if (result != ESP_OK) {
            ESP_LOGE(
                kTag,
                "credential_commit_failed=%s",
                esp_err_to_name(result));
            const std::uint32_t acknowledged =
                ulTaskNotifyTake(
                    pdTRUE,
                    kRebootFallbackTicks);
            if (status_mutex_ != nullptr &&
                xSemaphoreTake(
                    status_mutex_,
                    portMAX_DELAY) == pdTRUE) {
                const bool drain_notification =
                    finalize_failed_provisioning_operation(
                        operation_status_,
                        request.request_id);
                if (drain_notification) {
                    ulTaskNotifyTake(pdTRUE, 0U);
                }
                xSemaphoreGive(status_mutex_);
            }
            ESP_LOGI(
                kTag,
                "credential_failure_released status_acknowledged=%s",
                acknowledged > 0U ? "true" : "false");
            continue;
        }
        ESP_LOGI(
            kTag,
            "credential_commit_complete awaiting_status_ack=true");
        const std::uint32_t acknowledged =
            ulTaskNotifyTake(
                pdTRUE,
                kRebootFallbackTicks);
        if (status_mutex_ != nullptr &&
            xSemaphoreTake(
                status_mutex_,
                portMAX_DELAY) == pdTRUE) {
            if (operation_status_.request_id ==
                    request.request_id &&
                operation_status_.state ==
                    ProvisioningOperationState::committed) {
                operation_status_.state =
                    ProvisioningOperationState::reboot_pending;
            }
            xSemaphoreGive(status_mutex_);
        }
        ESP_LOGI(
            kTag,
            "credential_reboot_scheduled status_acknowledged=%s",
            acknowledged > 0U ? "true" : "false");
        vTaskDelay(kRebootDelayTicks);
        esp_restart();
    }
}

ProvisioningService& provisioning_service()
{
    static ProvisioningService instance;
    return instance;
}

}  // namespace pf_network
