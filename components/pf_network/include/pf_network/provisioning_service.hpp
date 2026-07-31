#pragma once

#include <cstdint>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "pf_config/network_credentials.hpp"
#include "pf_network/provisioning_status.hpp"

namespace pf_runtime {
class RuntimeCoordinator;
}

namespace pf_network {

enum class ProvisioningSubmitStatus : std::uint8_t {
    accepted,
    busy,
    unavailable,
    invalid,
};

struct ProvisioningSubmitResult {
    ProvisioningSubmitStatus status =
        ProvisioningSubmitStatus::unavailable;
    std::uint32_t request_id = 0U;
};

class ProvisioningService {
public:
    esp_err_t start(pf_runtime::RuntimeCoordinator& runtime);
    ProvisioningSubmitResult submit(
        const pf_config::NetworkCredentials& credentials);
    bool status(ProvisioningOperationStatus& destination);
    bool acknowledge_terminal(std::uint32_t request_id);

private:
    static constexpr UBaseType_t kTaskPriority = 4U;
    static constexpr std::uint32_t kTaskStackWords = 3072U;
    static constexpr TickType_t kRebootDelayTicks =
        pdMS_TO_TICKS(1000U);
    static constexpr TickType_t kRebootFallbackTicks =
        pdMS_TO_TICKS(20000U);

    static void task_entry(void* context);
    void task_main();

    struct QueuedRequest {
        std::uint32_t request_id = 0U;
        pf_config::NetworkCredentials credentials{};
    };

    pf_runtime::RuntimeCoordinator* runtime_ = nullptr;
    QueueHandle_t queue_ = nullptr;
    TaskHandle_t task_handle_ = nullptr;
    SemaphoreHandle_t status_mutex_ = nullptr;
    StaticQueue_t queue_control_{};
    std::uint8_t queue_storage_[
        sizeof(QueuedRequest)]{};
    StaticSemaphore_t status_mutex_control_{};
    StaticTask_t task_control_{};
    StackType_t task_stack_[kTaskStackWords]{};
    ProvisioningOperationStatus operation_status_{};
    std::uint32_t next_request_id_ = 1U;
};

ProvisioningService& provisioning_service();

}  // namespace pf_network
