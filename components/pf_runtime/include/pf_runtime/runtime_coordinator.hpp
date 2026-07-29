#pragma once

#include <cstdint>
#include <type_traits>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "pf_runtime/runtime_snapshot.hpp"

namespace pf_runtime {

enum class CommandKind : std::uint8_t {
    refresh_display,
};

struct RuntimeCommand {
    std::uint32_t request_id;
    CommandKind kind;
};

enum class ResultStatus : std::uint8_t {
    completed,
    rejected,
    failed,
};

struct RuntimeResult {
    std::uint32_t request_id;
    ResultStatus status;
    esp_err_t error;
};

static_assert(std::is_trivially_copyable_v<RuntimeSnapshot>);
static_assert(std::is_trivially_copyable_v<RuntimeCommand>);
static_assert(std::is_trivially_copyable_v<RuntimeResult>);

class RuntimeCoordinator {
public:
    esp_err_t initialize(const RuntimeSnapshot& initial_snapshot);

    bool read_snapshot(RuntimeSnapshot& destination) const;
    void publish_snapshot(const RuntimeSnapshot& snapshot);

    bool try_submit_command(const RuntimeCommand& command);
    bool try_receive_command(RuntimeCommand& command);
    bool try_publish_result(const RuntimeResult& result);
    bool try_receive_result(RuntimeResult& result);

private:
    static constexpr UBaseType_t kQueueLength = 4;

    RuntimeCoordinator() = default;
    RuntimeCoordinator(const RuntimeCoordinator&) = delete;
    RuntimeCoordinator& operator=(const RuntimeCoordinator&) = delete;
    RuntimeCoordinator(RuntimeCoordinator&&) = delete;
    RuntimeCoordinator& operator=(RuntimeCoordinator&&) = delete;

    friend RuntimeCoordinator& coordinator();

    mutable portMUX_TYPE snapshot_lock_ = portMUX_INITIALIZER_UNLOCKED;
    RuntimeSnapshot snapshot_{};
    bool snapshot_valid_ = false;
    StaticQueue_t command_queue_control_{};
    StaticQueue_t result_queue_control_{};
    std::uint8_t command_queue_storage_[
        kQueueLength * sizeof(RuntimeCommand)]{};
    std::uint8_t result_queue_storage_[
        kQueueLength * sizeof(RuntimeResult)]{};
    QueueHandle_t command_queue_ = nullptr;
    QueueHandle_t result_queue_ = nullptr;
};

RuntimeCoordinator& coordinator();

}  // namespace pf_runtime
