#include <array>
#include <cstdint>

#include <unity.h>

#include "pf_display/weather_icons.hpp"

using pf_display::Color;
using pf_display::PackedFramebufferView;
using pf_display::WeatherIconId;

extern "C" void setUp() {}
extern "C" void tearDown() {}

namespace {

void test_classify_icon_code_maps_every_owm_group()
{
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(WeatherIconId::clear),
        static_cast<int>(pf_display::classify_icon_code("01d")));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(WeatherIconId::few_clouds),
        static_cast<int>(pf_display::classify_icon_code("02n")));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(WeatherIconId::clouds),
        static_cast<int>(pf_display::classify_icon_code("03d")));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(WeatherIconId::overcast),
        static_cast<int>(pf_display::classify_icon_code("04d")));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(WeatherIconId::shower_rain),
        static_cast<int>(pf_display::classify_icon_code("09d")));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(WeatherIconId::rain),
        static_cast<int>(pf_display::classify_icon_code("10n")));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(WeatherIconId::thunderstorm),
        static_cast<int>(pf_display::classify_icon_code("11d")));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(WeatherIconId::snow),
        static_cast<int>(pf_display::classify_icon_code("13d")));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(WeatherIconId::mist),
        static_cast<int>(pf_display::classify_icon_code("50d")));
}

void test_classify_icon_code_rejects_unknown_null_and_short_codes()
{
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(WeatherIconId::unknown),
        static_cast<int>(pf_display::classify_icon_code("99d")));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(WeatherIconId::unknown),
        static_cast<int>(pf_display::classify_icon_code(nullptr)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(WeatherIconId::unknown),
        static_cast<int>(pf_display::classify_icon_code("")));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(WeatherIconId::unknown),
        static_cast<int>(pf_display::classify_icon_code("1")));
}

std::size_t count_non_white(PackedFramebufferView& view)
{
    std::size_t count = 0U;
    for (std::size_t y = 0U; y < view.height(); ++y) {
        for (std::size_t x = 0U; x < view.width(); ++x) {
            Color color{};
            if (view.get_pixel(x, y, color) && color != Color::white) {
                ++count;
            }
        }
    }
    return count;
}

void test_unknown_icon_draws_nothing()
{
    static std::array<
        std::uint8_t,
        pf_display::checked_packed_buffer_bytes(32U, 32U).bytes>
        buffer{};
    buffer.fill(
        static_cast<std::uint8_t>(
            (pf_display::native_code(Color::white) << 4U) |
            pf_display::native_code(Color::white)));
    PackedFramebufferView view{buffer.data(), buffer.size(), 32U, 32U};

    pf_display::draw_weather_icon(
        view, 0U, 0U, WeatherIconId::unknown, 32U);
    TEST_ASSERT_EQUAL_UINT(0U, count_non_white(view));
}

void test_known_icons_draw_within_bounds_and_differ_by_category()
{
    static std::array<
        std::uint8_t,
        pf_display::checked_packed_buffer_bytes(32U, 32U).bytes>
        clear_buffer{};
    clear_buffer.fill(
        static_cast<std::uint8_t>(
            (pf_display::native_code(Color::white) << 4U) |
            pf_display::native_code(Color::white)));
    PackedFramebufferView clear_view{
        clear_buffer.data(), clear_buffer.size(), 32U, 32U};
    pf_display::draw_weather_icon(
        clear_view, 0U, 0U, WeatherIconId::clear, 32U);
    const std::size_t clear_count = count_non_white(clear_view);
    TEST_ASSERT_GREATER_THAN_UINT32(0U, clear_count);

    static std::array<
        std::uint8_t,
        pf_display::checked_packed_buffer_bytes(32U, 32U).bytes>
        rain_buffer{};
    rain_buffer.fill(
        static_cast<std::uint8_t>(
            (pf_display::native_code(Color::white) << 4U) |
            pf_display::native_code(Color::white)));
    PackedFramebufferView rain_view{
        rain_buffer.data(), rain_buffer.size(), 32U, 32U};
    pf_display::draw_weather_icon(
        rain_view, 0U, 0U, WeatherIconId::rain, 32U);
    const std::size_t rain_count = count_non_white(rain_view);
    TEST_ASSERT_GREATER_THAN_UINT32(0U, rain_count);

    TEST_ASSERT_NOT_EQUAL(clear_count, rain_count);
}

void test_zero_size_icon_does_not_crash_or_draw()
{
    static std::array<
        std::uint8_t,
        pf_display::checked_packed_buffer_bytes(8U, 8U).bytes>
        buffer{};
    buffer.fill(
        static_cast<std::uint8_t>(
            (pf_display::native_code(Color::white) << 4U) |
            pf_display::native_code(Color::white)));
    PackedFramebufferView view{buffer.data(), buffer.size(), 8U, 8U};

    pf_display::draw_weather_icon(view, 0U, 0U, WeatherIconId::clear, 0U);
    TEST_ASSERT_EQUAL_UINT(0U, count_non_white(view));
}

}  // namespace

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_classify_icon_code_maps_every_owm_group);
    RUN_TEST(test_classify_icon_code_rejects_unknown_null_and_short_codes);
    RUN_TEST(test_unknown_icon_draws_nothing);
    RUN_TEST(test_known_icons_draw_within_bounds_and_differ_by_category);
    RUN_TEST(test_zero_size_icon_does_not_crash_or_draw);
    return UNITY_END();
}
