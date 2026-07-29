#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pf_display/epd7in3e.hpp"
#include "pf_display/epd7in3e_esp_idf.hpp"
#include "pf_display/packed_framebuffer.hpp"
#include "unity.h"

extern "C" void setUp() {}
extern "C" void tearDown() {}

namespace {

bool fill_six_color_bars(std::uint8_t* const frame)
{
    constexpr std::array<pf_display::Color, 6> kColors = {
        pf_display::Color::black,
        pf_display::Color::yellow,
        pf_display::Color::red,
        pf_display::Color::blue,
        pf_display::Color::green,
        pf_display::Color::white,
    };

    for (std::size_t y = 0; y < pf_display::kPanelHeight; ++y) {
        for (std::size_t packed_x = 0;
             packed_x < pf_display::packed_row_bytes(pf_display::kPanelWidth);
             ++packed_x) {
            const std::size_t even_x = packed_x * 2U;
            const std::size_t odd_x = even_x + 1U;
            const pf_display::Color even_color =
                kColors[(even_x * kColors.size()) / pf_display::kPanelWidth];
            const pf_display::Color odd_color =
                kColors[(odd_x * kColors.size()) / pf_display::kPanelWidth];
            std::uint8_t packed = 0;
            if (!pf_display::pack_colors(even_color, odd_color, packed)) {
                return false;
            }
            frame[
                y * pf_display::packed_row_bytes(pf_display::kPanelWidth) +
                packed_x] = packed;
        }
    }
    return true;
}

void test_panel_refreshes_six_color_bars_and_enters_deep_sleep()
{
    auto* const frame = static_cast<std::uint8_t*>(heap_caps_malloc(
        pf_display::kFullFramebufferBytes,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    TEST_ASSERT_NOT_NULL(frame);

    const bool pattern_ready = fill_six_color_bars(frame);
    pf_display::EspIdfPanelTransport transport;
    const esp_err_t transport_result = transport.initialize();
    pf_display::DriverResult driver_result{
        pf_display::DriverStatus::transport_error,
        pf_display::DriverStage::validate,
    };
    if (pattern_ready && transport_result == ESP_OK) {
        pf_display::Epd7in3eDriver driver{transport};
        driver_result =
            driver.refresh_and_sleep(frame, pf_display::kFullFramebufferBytes);
    }

    const esp_err_t shutdown_result = transport.shutdown();
    heap_caps_free(frame);

    TEST_ASSERT_TRUE(pattern_ready);
    TEST_ASSERT_EQUAL(ESP_OK, transport_result);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_display::DriverStatus::ok),
        static_cast<int>(driver_result.status));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_display::DriverStage::deep_sleep),
        static_cast<int>(driver_result.stage));
    TEST_ASSERT_EQUAL(ESP_OK, shutdown_result);
}

}  // namespace

extern "C" void app_main()
{
    vTaskDelay(pdMS_TO_TICKS(2000));
    UNITY_BEGIN();
    RUN_TEST(test_panel_refreshes_six_color_bars_and_enters_deep_sleep);
    UNITY_END();

    while (true) {
        vTaskDelay(portMAX_DELAY);
    }
}
