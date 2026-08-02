#include <array>
#include <cstdint>
#include <cstring>

#include <unity.h>

#include "pf_display/status_bar_renderer.hpp"

using pf_display::Color;
using pf_display::PackedFramebufferView;
using pf_display::StatusBarContent;

extern "C" void setUp() {}
extern "C" void tearDown() {}

namespace {

using StatusBuffer =
    std::array<std::uint8_t, pf_display::kLandscapeStatusBytes>;
using PortraitStatusBuffer =
    std::array<std::uint8_t, pf_display::kPortraitStatusBytes>;

StatusBuffer make_white_buffer()
{
    StatusBuffer buffer{};
    buffer.fill(
        static_cast<std::uint8_t>(
            (pf_display::native_code(Color::white) << 4U) |
            pf_display::native_code(Color::white)));
    return buffer;
}

bool region_has_non_white(
    PackedFramebufferView& view,
    const std::size_t x0,
    const std::size_t x1,
    const std::size_t y0,
    const std::size_t y1)
{
    for (std::size_t y = y0; y < y1; ++y) {
        for (std::size_t x = x0; x < x1; ++x) {
            Color color{};
            if (view.get_pixel(x, y, color) && color != Color::white) {
                return true;
            }
        }
    }
    return false;
}

void test_unsynced_time_renders_placeholder_not_epoch_date()
{
    StatusBuffer buffer = make_white_buffer();
    PackedFramebufferView view{
        buffer.data(),
        buffer.size(),
        pf_display::kPanelWidth,
        pf_display::kStatusBarHeight};

    StatusBarContent content{};
    content.time_valid = false;
    content.weather_available = false;

    TEST_ASSERT_TRUE(pf_display::render_status_bar(content, view));
    // The date area (left margin) must still show the fixed placeholder
    // glyphs, never a fabricated year/month/day.
    TEST_ASSERT_TRUE(region_has_non_white(view, 0U, 60U, 0U, 40U));
}

void test_unavailable_weather_leaves_icon_region_blank()
{
    StatusBuffer buffer = make_white_buffer();
    PackedFramebufferView view{
        buffer.data(),
        buffer.size(),
        pf_display::kPanelWidth,
        pf_display::kStatusBarHeight};

    StatusBarContent content{};
    content.time_valid = true;
    content.year = 2026;
    content.month = 7;
    content.day = 31;
    content.iso_weekday = 5;
    content.weather_available = false;

    TEST_ASSERT_TRUE(pf_display::render_status_bar(content, view));
    TEST_ASSERT_FALSE(
        region_has_non_white(view, 700U, 800U, 0U, 40U));
}

void test_weekday_abbreviations_are_english()
{
    TEST_ASSERT_EQUAL_STRING("Mon", pf_display::weekday_abbreviation(1U));
    TEST_ASSERT_EQUAL_STRING("Wed", pf_display::weekday_abbreviation(3U));
    TEST_ASSERT_EQUAL_STRING("Fri", pf_display::weekday_abbreviation(5U));
    TEST_ASSERT_EQUAL_STRING("Sun", pf_display::weekday_abbreviation(7U));
    TEST_ASSERT_NULL(pf_display::weekday_abbreviation(0U));
}

void test_available_weather_and_ip_are_drawn_in_left_and_center_regions()
{
    StatusBuffer buffer = make_white_buffer();
    PackedFramebufferView view{
        buffer.data(),
        buffer.size(),
        pf_display::kPanelWidth,
        pf_display::kStatusBarHeight};

    StatusBarContent content{};
    content.time_valid = true;
    content.year = 2026;
    content.month = 7;
    content.day = 31;
    content.iso_weekday = 5;
    content.weather_available = true;
    content.weather_stale = false;
    content.temperature_rounded = 27;
    std::strncpy(content.icon_code, "01d", sizeof(content.icon_code) - 1U);
    content.device_ip_available = true;
    std::strncpy(
        content.device_ip,
        "192.168.1.20",
        sizeof(content.device_ip) - 1U);

    TEST_ASSERT_TRUE(pf_display::render_status_bar(content, view));
    TEST_ASSERT_TRUE(
        region_has_non_white(view, 165U, 250U, 0U, 40U));
    TEST_ASSERT_TRUE(
        region_has_non_white(view, 300U, 500U, 0U, 40U));
}

void test_online_indoor_values_are_drawn_on_the_right()
{
    StatusBuffer buffer = make_white_buffer();
    PackedFramebufferView view{
        buffer.data(),
        buffer.size(),
        pf_display::kPanelWidth,
        pf_display::kStatusBarHeight};

    StatusBarContent content{};
    content.time_valid = true;
    content.year = 2026;
    content.month = 8;
    content.day = 3;
    content.iso_weekday = 1;
    content.device_ip_available = true;
    std::strncpy(
        content.device_ip,
        "192.168.1.20",
        sizeof(content.device_ip) - 1U);
    content.indoor_available = true;
    content.indoor_temperature_rounded = 28;
    content.indoor_humidity_rounded = 52;

    TEST_ASSERT_TRUE(pf_display::render_status_bar(content, view));
    TEST_ASSERT_TRUE(
        region_has_non_white(view, 690U, 800U, 0U, 40U));
}

void test_portrait_layout_uses_compact_scale_and_keeps_right_values()
{
    PortraitStatusBuffer buffer{};
    buffer.fill(
        static_cast<std::uint8_t>(
            (pf_display::native_code(Color::white) << 4U) |
            pf_display::native_code(Color::white)));
    PackedFramebufferView view{
        buffer.data(),
        buffer.size(),
        pf_display::kPortraitImageWidth,
        pf_display::kStatusBarHeight};

    StatusBarContent content{};
    content.time_valid = true;
    content.year = 2026;
    content.month = 8;
    content.day = 3;
    content.iso_weekday = 1;
    content.weather_available = true;
    content.temperature_rounded = 30;
    std::strncpy(content.icon_code, "01d", sizeof(content.icon_code) - 1U);
    content.device_ip_available = true;
    std::strncpy(
        content.device_ip,
        "192.168.1.20",
        sizeof(content.device_ip) - 1U);
    content.indoor_available = true;
    content.indoor_temperature_rounded = 28;
    content.indoor_humidity_rounded = 52;

    TEST_ASSERT_TRUE(pf_display::render_status_bar(content, view));
    TEST_ASSERT_TRUE(
        region_has_non_white(view, 180U, 300U, 0U, 40U));
    TEST_ASSERT_TRUE(
        region_has_non_white(view, 400U, 480U, 0U, 40U));
}

void test_stale_weather_draws_marker_that_fresh_weather_does_not()
{
    StatusBarContent content{};
    content.time_valid = true;
    content.year = 2026;
    content.month = 7;
    content.day = 31;
    content.iso_weekday = 5;
    content.weather_available = true;
    content.temperature_rounded = 10;
    std::strncpy(content.icon_code, "01d", sizeof(content.icon_code) - 1U);

    StatusBuffer fresh_buffer = make_white_buffer();
    PackedFramebufferView fresh_view{
        fresh_buffer.data(),
        fresh_buffer.size(),
        pf_display::kPanelWidth,
        pf_display::kStatusBarHeight};
    content.weather_stale = false;
    TEST_ASSERT_TRUE(pf_display::render_status_bar(content, fresh_view));

    StatusBuffer stale_buffer = make_white_buffer();
    PackedFramebufferView stale_view{
        stale_buffer.data(),
        stale_buffer.size(),
        pf_display::kPanelWidth,
        pf_display::kStatusBarHeight};
    content.weather_stale = true;
    TEST_ASSERT_TRUE(pf_display::render_status_bar(content, stale_view));

    TEST_ASSERT_FALSE(fresh_buffer == stale_buffer);
}

void test_render_status_bar_rejects_invalid_view()
{
    PackedFramebufferView invalid_view{nullptr, 0U, 0U, 0U};
    StatusBarContent content{};
    TEST_ASSERT_FALSE(
        pf_display::render_status_bar(content, invalid_view));
}

}  // namespace

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_unsynced_time_renders_placeholder_not_epoch_date);
    RUN_TEST(test_unavailable_weather_leaves_icon_region_blank);
    RUN_TEST(test_weekday_abbreviations_are_english);
    RUN_TEST(test_available_weather_and_ip_are_drawn_in_left_and_center_regions);
    RUN_TEST(test_online_indoor_values_are_drawn_on_the_right);
    RUN_TEST(test_portrait_layout_uses_compact_scale_and_keeps_right_values);
    RUN_TEST(test_stale_weather_draws_marker_that_fresh_weather_does_not);
    RUN_TEST(test_render_status_bar_rejects_invalid_view);
    return UNITY_END();
}
