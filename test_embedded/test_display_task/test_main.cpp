#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pf_display/display_task_esp_idf.hpp"
#include "pf_runtime/runtime_coordinator.hpp"
#include "unity.h"

extern "C" void setUp() {}
extern "C" void tearDown() {}

namespace {

pf_runtime::RuntimeSnapshot initial_snapshot()
{
    return {
        .sequence = 1,
        .flash = pf_runtime::ServiceState::ready,
        .psram = pf_runtime::ServiceState::ready,
        .config = pf_runtime::ServiceState::ready,
        .imagefs = pf_runtime::ServiceState::ready,
        .wifi = pf_runtime::WifiState::unknown,
        .internet = pf_runtime::InternetState::unknown,
        .time_sync = pf_runtime::TimeSyncState::unsynced,
        .display = pf_runtime::DisplayState::unknown,
        .active_display_request_id = 0,
        .queued_display_count = 0,
        .last_display_request_id = 0,
        .last_display_outcome = pf_runtime::DisplayOutcome::none,
        .last_display_stage = 0,
    };
}

void fill_six_color_bands(pf_display::FrameWriteLease& frame)
{
    constexpr std::array<std::uint8_t, 6> kPackedColors{
        0x00,
        0x11,
        0x22,
        0x33,
        0x55,
        0x66,
    };
    constexpr std::size_t kBandBytes =
        pf_display::kFullFramebufferBytes / kPackedColors.size();
    static_assert(
        kBandBytes * kPackedColors.size() ==
        pf_display::kFullFramebufferBytes);
    for (std::size_t index = 0; index < kPackedColors.size(); ++index) {
        std::memset(
            frame.data() + index * kBandBytes,
            kPackedColors[index],
            kBandBytes);
    }
}

void test_display_task_owns_refresh_and_reports_deep_sleep()
{
    pf_runtime::RuntimeCoordinator& runtime =
        pf_runtime::coordinator();
    TEST_ASSERT_EQUAL(ESP_OK, runtime.initialize(initial_snapshot()));

    pf_display::DisplayTask& task = pf_display::display_task();
    TEST_ASSERT_EQUAL(ESP_OK, task.start(runtime));

    auto frame = task.try_acquire_frame();
    TEST_ASSERT_TRUE(frame.valid());
    TEST_ASSERT_EQUAL_UINT32(
        pf_display::kFullFramebufferBytes,
        frame.size());
    fill_six_color_bands(frame);

    const TickType_t submit_started = xTaskGetTickCount();
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_display::SubmitStatus::accepted),
        static_cast<int>(task.try_submit_refresh(73, frame)));
    const TickType_t submit_elapsed =
        xTaskGetTickCount() - submit_started;
    TEST_ASSERT_LESS_THAN_UINT32(pdMS_TO_TICKS(100), submit_elapsed + 1U);
    TEST_ASSERT_FALSE(frame.valid());

    pf_runtime::RuntimeResult result{};
    constexpr TickType_t kResultTimeout = pdMS_TO_TICKS(180000U);
    const TickType_t wait_started = xTaskGetTickCount();
    while (!runtime.try_receive_result(result) &&
           xTaskGetTickCount() - wait_started < kResultTimeout) {
        vTaskDelay(pdMS_TO_TICKS(20U));
    }

    TEST_ASSERT_EQUAL_UINT32(73, result.request_id);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_runtime::ResultStatus::completed),
        static_cast<int>(result.status));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_runtime::RuntimeError::none),
        static_cast<int>(result.error));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(
            pf_runtime::DisplayOutcome::refreshed_and_slept),
        static_cast<int>(result.display_outcome));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_display::DriverStage::deep_sleep),
        result.driver_stage);

    pf_runtime::RuntimeSnapshot snapshot{};
    TEST_ASSERT_TRUE(runtime.read_snapshot(snapshot));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_runtime::DisplayState::deep_sleep),
        static_cast<int>(snapshot.display));
    TEST_ASSERT_EQUAL_UINT32(0, snapshot.active_display_request_id);
    TEST_ASSERT_EQUAL_UINT8(0, snapshot.queued_display_count);
    TEST_ASSERT_EQUAL_UINT32(73, snapshot.last_display_request_id);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(
            pf_runtime::DisplayOutcome::refreshed_and_slept),
        static_cast<int>(snapshot.last_display_outcome));
}

}  // namespace

extern "C" void app_main()
{
    vTaskDelay(pdMS_TO_TICKS(2000U));
    UNITY_BEGIN();
    RUN_TEST(test_display_task_owns_refresh_and_reports_deep_sleep);
    UNITY_END();

    while (true) {
        vTaskDelay(portMAX_DELAY);
    }
}
