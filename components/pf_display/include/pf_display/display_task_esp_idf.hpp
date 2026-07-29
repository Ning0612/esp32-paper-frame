#pragma once

#include <atomic>
#include <cstdint>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pf_display/display_task.hpp"
#include "pf_display/epd7in3e_esp_idf.hpp"
#include "pf_runtime/runtime_coordinator.hpp"

namespace pf_display {

class DisplayTask final {
public:
    esp_err_t start(pf_runtime::RuntimeCoordinator& runtime);

    FrameWriteLease try_acquire_frame();
    SubmitStatus try_submit_refresh(
        std::uint32_t request_id,
        FrameWriteLease& lease);
    bool started() const;

private:
    static constexpr std::uint32_t kTaskStackWords = 6144U;
    static constexpr UBaseType_t kTaskPriority = 4U;

    class RuntimePublisher final : public DisplayCommandPublisher {
    public:
        void bind(pf_runtime::RuntimeCoordinator& runtime)
        {
            runtime_ = &runtime;
        }

        void unbind()
        {
            runtime_ = nullptr;
        }

        bool try_publish(
            const pf_runtime::RuntimeCommand& command) override
        {
            return runtime_ != nullptr &&
                   runtime_->try_submit_command(command);
        }

    private:
        pf_runtime::RuntimeCoordinator* runtime_ = nullptr;
    };

    class EpdPanel final : public DisplayPanel {
    public:
        explicit EpdPanel(EspIdfPanelTransport& transport)
            : driver_(transport)
        {
        }

        DriverResult refresh_and_sleep(
            const std::uint8_t* frame,
            std::size_t length) override
        {
            return driver_.refresh_and_sleep(frame, length);
        }

        PanelState state() const override
        {
            return driver_.state();
        }

    private:
        Epd7in3eDriver driver_;
    };

    DisplayTask();
    DisplayTask(const DisplayTask&) = delete;
    DisplayTask& operator=(const DisplayTask&) = delete;

    static void task_entry(void* context);
    void rollback_start();
    void run();
    void process_command(const pf_runtime::RuntimeCommand& command);
    void publish_result(const pf_runtime::RuntimeResult& result);

    DisplayFramePool pool_;
    RuntimePublisher publisher_;
    DisplaySubmitter submitter_;
    EspIdfPanelTransport transport_;
    EpdPanel panel_;
    DisplayCommandProcessor processor_;
    pf_runtime::RuntimeCoordinator* runtime_ = nullptr;
    std::uint8_t* frame_slots_[DisplayFramePool::kSlotCount]{};
    std::atomic<bool> transport_ready_{false};
    StaticTask_t task_control_{};
    StackType_t task_stack_[kTaskStackWords]{};
    TaskHandle_t task_handle_ = nullptr;

    friend DisplayTask& display_task();
};

DisplayTask& display_task();

}  // namespace pf_display
