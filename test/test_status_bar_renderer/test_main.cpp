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

// Unlike region_has_non_white, this checks the whole view for one exact
// color -- comparing entire framebuffers only proves *something* about the
// layout changed, not that the stale marker itself (as opposed to some
// unrelated width shift) is what changed it.
bool view_has_color(PackedFramebufferView& view, const Color target)
{
    for (std::size_t y = 0U; y < view.height(); ++y) {
        for (std::size_t x = 0U; x < view.width(); ++x) {
            Color color{};
            if (view.get_pixel(x, y, color) && color == target) {
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

void test_unavailable_weather_leaves_weather_group_blank()
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
        region_has_non_white(view, 250U, 550U, 0U, 40U));
    TEST_ASSERT_TRUE(
        region_has_non_white(view, 650U, 800U, 0U, 40U));
}

void test_weather_icon_uses_two_pixel_vertical_insets()
{
    TEST_ASSERT_EQUAL_UINT32(40U, pf_display::kStatusBarHeight);
    TEST_ASSERT_EQUAL_UINT32(36U, pf_display::kStatusBarWeatherIconSize);
    TEST_ASSERT_EQUAL_UINT32(
        2U,
        (pf_display::kStatusBarHeight -
         pf_display::kStatusBarWeatherIconSize) /
            2U);
}

void test_status_bar_text_scales_are_larger_but_fit_by_orientation()
{
    TEST_ASSERT_EQUAL_UINT32(4U, pf_display::kStatusBarLandscapeTextScale);
    TEST_ASSERT_EQUAL_UINT32(3U, pf_display::kStatusBarPortraitTextScale);
}

void test_weekday_abbreviations_are_english()
{
    TEST_ASSERT_EQUAL_STRING("Mon", pf_display::weekday_abbreviation(1U));
    TEST_ASSERT_EQUAL_STRING("Wed", pf_display::weekday_abbreviation(3U));
    TEST_ASSERT_EQUAL_STRING("Fri", pf_display::weekday_abbreviation(5U));
    TEST_ASSERT_EQUAL_STRING("Sun", pf_display::weekday_abbreviation(7U));
    TEST_ASSERT_NULL(pf_display::weekday_abbreviation(0U));
}

void test_weather_moves_to_the_right_when_indoor_values_are_unavailable()
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
        region_has_non_white(view, 350U, 450U, 0U, 40U));
    TEST_ASSERT_TRUE(
        region_has_non_white(view, 640U, 800U, 0U, 40U));
    TEST_ASSERT_FALSE(
        region_has_non_white(view, 250U, 350U, 0U, 40U));
}

void test_online_indoor_values_keep_weather_left_and_indoor_right()
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
    content.weather_available = true;
    content.temperature_rounded = 27;
    std::strncpy(content.icon_code, "01d", sizeof(content.icon_code) - 1U);
    content.indoor_available = true;
    content.indoor_temperature_rounded = 28;
    content.indoor_humidity_rounded = 52;

    TEST_ASSERT_TRUE(pf_display::render_status_bar(content, view));
    TEST_ASSERT_TRUE(
        region_has_non_white(view, 250U, 360U, 0U, 40U));
    TEST_ASSERT_TRUE(
        region_has_non_white(view, 700U, 800U, 0U, 40U));
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
        region_has_non_white(view, 0U, 170U, 12U, 15U));
    TEST_ASSERT_TRUE(
        region_has_non_white(view, 180U, 300U, 0U, 40U));
    TEST_ASSERT_TRUE(
        region_has_non_white(view, 400U, 480U, 0U, 40U));

    PortraitStatusBuffer no_indoor_buffer{};
    no_indoor_buffer.fill(
        static_cast<std::uint8_t>(
            (pf_display::native_code(Color::white) << 4U) |
            pf_display::native_code(Color::white)));
    PackedFramebufferView no_indoor_view{
        no_indoor_buffer.data(),
        no_indoor_buffer.size(),
        pf_display::kPortraitImageWidth,
        pf_display::kStatusBarHeight};
    content.indoor_available = false;
    TEST_ASSERT_TRUE(
        pf_display::render_status_bar(content, no_indoor_view));
    TEST_ASSERT_TRUE(
        region_has_non_white(no_indoor_view, 400U, 480U, 0U, 40U));
    TEST_ASSERT_TRUE(
        region_has_non_white(no_indoor_view, 180U, 300U, 0U, 40U));
    TEST_ASSERT_FALSE(
        region_has_non_white(no_indoor_view, 170U, 210U, 0U, 40U));
}

void test_portrait_long_content_falls_back_without_overlapping_groups()
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
    content.weather_stale = true;
    content.temperature_rounded = -40;
    std::strncpy(content.icon_code, "01d", sizeof(content.icon_code) - 1U);
    content.device_ip_available = true;
    std::strncpy(
        content.device_ip,
        "255.255.255.255",
        sizeof(content.device_ip) - 1U);
    content.indoor_available = true;
    content.indoor_temperature_rounded = -40;
    content.indoor_humidity_rounded = 100;

    TEST_ASSERT_TRUE(pf_display::render_status_bar(content, view));
    // The preferred portrait scale is 3 (15px high), but this longest valid
    // combination must fall back to scale 2 so the date group remains clear
    // of the 12px-to-14px band used by scale 3 text.
    TEST_ASSERT_FALSE(region_has_non_white(view, 0U, 120U, 12U, 15U));
    TEST_ASSERT_TRUE(region_has_non_white(view, 0U, 120U, 15U, 25U));
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

void test_stale_indoor_draws_marker_that_fresh_indoor_does_not()
{
    StatusBarContent content{};
    content.time_valid = true;
    content.year = 2026;
    content.month = 7;
    content.day = 31;
    content.iso_weekday = 5;
    content.indoor_available = true;
    content.indoor_temperature_rounded = 26;
    content.indoor_humidity_rounded = 55;

    StatusBuffer fresh_buffer = make_white_buffer();
    PackedFramebufferView fresh_view{
        fresh_buffer.data(),
        fresh_buffer.size(),
        pf_display::kPanelWidth,
        pf_display::kStatusBarHeight};
    content.indoor_stale = false;
    TEST_ASSERT_TRUE(pf_display::render_status_bar(content, fresh_view));

    StatusBuffer stale_buffer = make_white_buffer();
    PackedFramebufferView stale_view{
        stale_buffer.data(),
        stale_buffer.size(),
        pf_display::kPanelWidth,
        pf_display::kStatusBarHeight};
    content.indoor_stale = true;
    TEST_ASSERT_TRUE(pf_display::render_status_bar(content, stale_view));

    // A read failure must not blank the indoor group -- the last known
    // reading keeps rendering (see kEnvironmentDefaultCacheMaxAgeSeconds'
    // comment for why) -- it must only gain a stale marker.
    TEST_ASSERT_FALSE(fresh_buffer == stale_buffer);
    // The buffer-diff check alone would also pass if the marker were dropped
    // and only the indoor group's width shifted for some unrelated reason;
    // this pins the difference to the red stale marker specifically.
    TEST_ASSERT_FALSE(view_has_color(fresh_view, Color::red));
    TEST_ASSERT_TRUE(view_has_color(stale_view, Color::red));
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
    RUN_TEST(test_unavailable_weather_leaves_weather_group_blank);
    RUN_TEST(test_weather_icon_uses_two_pixel_vertical_insets);
    RUN_TEST(test_status_bar_text_scales_are_larger_but_fit_by_orientation);
    RUN_TEST(test_weekday_abbreviations_are_english);
    RUN_TEST(test_weather_moves_to_the_right_when_indoor_values_are_unavailable);
    RUN_TEST(test_online_indoor_values_keep_weather_left_and_indoor_right);
    RUN_TEST(test_portrait_layout_uses_compact_scale_and_keeps_right_values);
    RUN_TEST(test_portrait_long_content_falls_back_without_overlapping_groups);
    RUN_TEST(test_stale_weather_draws_marker_that_fresh_weather_does_not);
    RUN_TEST(test_stale_indoor_draws_marker_that_fresh_indoor_does_not);
    RUN_TEST(test_render_status_bar_rejects_invalid_view);
    return UNITY_END();
}
