#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pf_runtime/runtime_coordinator.hpp"
#include "unity.h"

extern "C" void setUp() {}
extern "C" void tearDown() {}

namespace {

void test_runtime_queues_and_snapshot()
{
    pf_runtime::RuntimeCoordinator& runtime = pf_runtime::coordinator();
    pf_runtime::RuntimeSnapshot observed{};
    TEST_ASSERT_FALSE(runtime.read_snapshot(observed));

    const pf_runtime::RuntimeSnapshot initial{
        .sequence = 7,
        .flash = pf_runtime::ServiceState::ready,
        .psram = pf_runtime::ServiceState::degraded,
        .config = pf_runtime::ServiceState::ready,
        .webfs = pf_runtime::ServiceState::ready,
        .imagefs = pf_runtime::ServiceState::degraded,
        .display = pf_runtime::DisplayState::unknown,
        .active_display_request_id = 0,
        .queued_display_count = 0,
        .last_display_request_id = 0,
        .last_display_outcome = pf_runtime::DisplayOutcome::none,
        .last_display_stage = 0,
    };
    TEST_ASSERT_EQUAL(ESP_OK, runtime.initialize(initial));
    TEST_ASSERT_TRUE(runtime.read_snapshot(observed));
    TEST_ASSERT_EQUAL_UINT32(initial.sequence, observed.sequence);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(initial.imagefs),
        static_cast<int>(observed.imagefs));

    pf_runtime::RuntimeSnapshot updated = initial;
    updated.sequence = 8;
    runtime.publish_snapshot(updated);
    TEST_ASSERT_TRUE(runtime.read_snapshot(observed));
    TEST_ASSERT_EQUAL_UINT32(updated.sequence, observed.sequence);

    for (std::uint32_t index = 0; index < 4; ++index) {
        const pf_runtime::RuntimeCommand command{
            .request_id = index,
            .kind = pf_runtime::CommandKind::refresh_display,
            .frame = pf_runtime::FrameToken{
                static_cast<std::uint8_t>(index % 2U),
                index + 1U,
            },
        };
        TEST_ASSERT_TRUE(runtime.try_submit_command(command));
    }
    const pf_runtime::RuntimeCommand overflow_command{
        .request_id = 4,
        .kind = pf_runtime::CommandKind::refresh_display,
        .frame = pf_runtime::FrameToken{0, 5},
    };
    TEST_ASSERT_FALSE(runtime.try_submit_command(overflow_command));
    TEST_ASSERT_TRUE(runtime.read_snapshot(observed));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_runtime::DisplayState::queued),
        static_cast<int>(observed.display));
    TEST_ASSERT_EQUAL_UINT32(0, observed.active_display_request_id);
    TEST_ASSERT_EQUAL_UINT8(4, observed.queued_display_count);

    for (std::uint32_t index = 0; index < 4; ++index) {
        pf_runtime::RuntimeCommand command{};
        TEST_ASSERT_TRUE(runtime.try_receive_command(command));
        TEST_ASSERT_EQUAL_UINT32(index, command.request_id);
    }

    runtime.update_display_started(0);
    const pf_runtime::RuntimeCommand queued_behind_zero{
        .request_id = 99,
        .kind = pf_runtime::CommandKind::refresh_display,
        .frame = pf_runtime::FrameToken{0, 100},
    };
    TEST_ASSERT_TRUE(runtime.try_submit_command(queued_behind_zero));
    TEST_ASSERT_TRUE(runtime.read_snapshot(observed));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_runtime::DisplayState::refreshing),
        static_cast<int>(observed.display));
    TEST_ASSERT_EQUAL_UINT32(0, observed.active_display_request_id);
    TEST_ASSERT_EQUAL_UINT8(4, observed.queued_display_count);
    pf_runtime::RuntimeCommand received_behind_zero{};
    TEST_ASSERT_TRUE(runtime.try_receive_command(received_behind_zero));
    TEST_ASSERT_EQUAL_UINT32(99, received_behind_zero.request_id);
    runtime.update_display_finished(
        0,
        pf_runtime::DisplayOutcome::busy_timeout,
        7);

    TEST_ASSERT_TRUE(runtime.lock_flash_display(0));
    TEST_ASSERT_FALSE(runtime.lock_flash_display(0));
    runtime.unlock_flash_display();
    TEST_ASSERT_TRUE(runtime.lock_flash_display(0));
    runtime.unlock_flash_display();

    runtime.update_display_started(3);
    TEST_ASSERT_TRUE(runtime.read_snapshot(observed));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_runtime::DisplayState::refreshing),
        static_cast<int>(observed.display));
    TEST_ASSERT_EQUAL_UINT32(3, observed.active_display_request_id);
    TEST_ASSERT_EQUAL_UINT8(3, observed.queued_display_count);

    runtime.update_display_finished(
        3,
        pf_runtime::DisplayOutcome::busy_timeout,
        7);
    TEST_ASSERT_TRUE(runtime.read_snapshot(observed));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_runtime::DisplayState::queued),
        static_cast<int>(observed.display));
    TEST_ASSERT_EQUAL_UINT32(0, observed.active_display_request_id);
    TEST_ASSERT_EQUAL_UINT32(3, observed.last_display_request_id);
    TEST_ASSERT_EQUAL_UINT8(3, observed.queued_display_count);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_runtime::DisplayOutcome::busy_timeout),
        static_cast<int>(observed.last_display_outcome));
    TEST_ASSERT_EQUAL_UINT8(7, observed.last_display_stage);

    for (std::uint32_t index = 0; index < 4; ++index) {
        const pf_runtime::RuntimeResult result{
            .request_id = index,
            .status = pf_runtime::ResultStatus::completed,
            .error = pf_runtime::RuntimeError::none,
            .display_outcome =
                pf_runtime::DisplayOutcome::refreshed_and_slept,
            .driver_stage = 10,
        };
        TEST_ASSERT_TRUE(runtime.try_publish_result(result));
    }
    const pf_runtime::RuntimeResult overflow_result{
        .request_id = 4,
        .status = pf_runtime::ResultStatus::failed,
        .error = pf_runtime::RuntimeError::transport,
        .display_outcome = pf_runtime::DisplayOutcome::transport_error,
        .driver_stage = 5,
    };
    TEST_ASSERT_FALSE(runtime.try_publish_result(overflow_result));

    for (std::uint32_t index = 0; index < 4; ++index) {
        pf_runtime::RuntimeResult result{};
        TEST_ASSERT_TRUE(runtime.try_receive_result(result));
        TEST_ASSERT_EQUAL_UINT32(index, result.request_id);
        TEST_ASSERT_EQUAL_INT(
            static_cast<int>(
                pf_runtime::DisplayOutcome::refreshed_and_slept),
            static_cast<int>(result.display_outcome));
    }
}

}  // namespace

extern "C" void app_main()
{
    vTaskDelay(pdMS_TO_TICKS(2000));
    UNITY_BEGIN();
    RUN_TEST(test_runtime_queues_and_snapshot);
    UNITY_END();

    while (true) {
        vTaskDelay(portMAX_DELAY);
    }
}
