#pragma once

#include <cstdint>
#include <type_traits>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "pf_runtime/runtime_messages.hpp"
#include "pf_runtime/runtime_snapshot.hpp"

namespace pf_runtime {

static_assert(std::is_trivially_copyable_v<RuntimeSnapshot>);

class RuntimeCoordinator {
public:
    esp_err_t initialize(const RuntimeSnapshot& initial_snapshot);

    bool read_snapshot(RuntimeSnapshot& destination) const;
    void publish_snapshot(const RuntimeSnapshot& snapshot);

    bool try_submit_command(const RuntimeCommand& command);
    bool try_receive_command(RuntimeCommand& command);
    bool receive_command(RuntimeCommand& command, TickType_t wait_ticks);
    bool try_publish_result(const RuntimeResult& result);
    bool try_receive_result(RuntimeResult& result);
    bool retain_terminal_result(const RuntimeResult& result);
    bool try_take_terminal_result(
        std::uint32_t request_id,
        RuntimeResult& result);
    std::uint32_t allocate_request_id();

    void update_network(
        WifiState wifi,
        InternetState internet);
    void update_display_started(std::uint32_t request_id);
    void update_display_finished(
        std::uint32_t request_id,
        DisplayOutcome outcome,
        std::uint8_t driver_stage);

    bool lock_flash_display(TickType_t wait_ticks);
    void unlock_flash_display();

private:
    static constexpr UBaseType_t kQueueLength = 4;
    static constexpr std::size_t kTerminalResultCapacity = 8U;

    struct TerminalResultSlot {
        bool occupied = false;
        bool completed = false;
        std::uint32_t request_id = 0U;
        RuntimeResult result{};
    };

    RuntimeCoordinator() = default;
    RuntimeCoordinator(const RuntimeCoordinator&) = delete;
    RuntimeCoordinator& operator=(const RuntimeCoordinator&) = delete;
    RuntimeCoordinator(RuntimeCoordinator&&) = delete;
    RuntimeCoordinator& operator=(RuntimeCoordinator&&) = delete;

    friend RuntimeCoordinator& coordinator();

    bool reserve_terminal_result(std::uint32_t request_id);
    void release_terminal_reservation(std::uint32_t request_id);

    mutable portMUX_TYPE snapshot_lock_ = portMUX_INITIALIZER_UNLOCKED;
    RuntimeSnapshot snapshot_{};
    bool snapshot_valid_ = false;
    std::uint32_t next_request_id_ = 1U;
    TerminalResultSlot terminal_results_[kTerminalResultCapacity]{};
    StaticQueue_t command_queue_control_{};
    StaticQueue_t result_queue_control_{};
    StaticSemaphore_t flash_display_mutex_control_{};
    std::uint8_t command_queue_storage_[
        kQueueLength * sizeof(RuntimeCommand)]{};
    std::uint8_t result_queue_storage_[
        kQueueLength * sizeof(RuntimeResult)]{};
    QueueHandle_t command_queue_ = nullptr;
    QueueHandle_t result_queue_ = nullptr;
    SemaphoreHandle_t flash_display_mutex_ = nullptr;
};

RuntimeCoordinator& coordinator();

}  // namespace pf_runtime
