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
    };
    TEST_ASSERT_EQUAL(ESP_OK, runtime.initialize(initial));
    TEST_ASSERT_TRUE(runtime.read_snapshot(observed));
    TEST_ASSERT_EQUAL_UINT32(initial.sequence, observed.sequence);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(initial.imagefs),
        static_cast<int>(observed.imagefs));

    for (std::uint32_t index = 0; index < 4; ++index) {
        const pf_runtime::RuntimeCommand command{
            .request_id = index,
            .kind = pf_runtime::CommandKind::refresh_display,
        };
        TEST_ASSERT_TRUE(runtime.try_submit_command(command));
    }
    const pf_runtime::RuntimeCommand overflow_command{
        .request_id = 4,
        .kind = pf_runtime::CommandKind::refresh_display,
    };
    TEST_ASSERT_FALSE(runtime.try_submit_command(overflow_command));

    pf_runtime::RuntimeSnapshot updated = initial;
    updated.sequence = 8;
    runtime.publish_snapshot(updated);
    TEST_ASSERT_TRUE(runtime.read_snapshot(observed));
    TEST_ASSERT_EQUAL_UINT32(updated.sequence, observed.sequence);

    for (std::uint32_t index = 0; index < 4; ++index) {
        pf_runtime::RuntimeCommand command{};
        TEST_ASSERT_TRUE(runtime.try_receive_command(command));
        TEST_ASSERT_EQUAL_UINT32(index, command.request_id);
    }

    for (std::uint32_t index = 0; index < 4; ++index) {
        const pf_runtime::RuntimeResult result{
            .request_id = index,
            .status = pf_runtime::ResultStatus::completed,
            .error = ESP_OK,
        };
        TEST_ASSERT_TRUE(runtime.try_publish_result(result));
    }
    const pf_runtime::RuntimeResult overflow_result{
        .request_id = 4,
        .status = pf_runtime::ResultStatus::failed,
        .error = ESP_FAIL,
    };
    TEST_ASSERT_FALSE(runtime.try_publish_result(overflow_result));

    for (std::uint32_t index = 0; index < 4; ++index) {
        pf_runtime::RuntimeResult result{};
        TEST_ASSERT_TRUE(runtime.try_receive_result(result));
        TEST_ASSERT_EQUAL_UINT32(index, result.request_id);
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
