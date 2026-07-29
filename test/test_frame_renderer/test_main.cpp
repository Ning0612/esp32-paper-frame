#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include <unity.h>

#include "pf_display/frame_renderer.hpp"

extern "C" void setUp() {}
extern "C" void tearDown() {}

namespace {

using pf_display::Color;
using pf_display::PackedFramebufferView;
using pf_display::PackedRegion;
using pf_display::PortraitRotation;
using pf_display::RenderStatus;
using pf_display::StatusPlacement;

std::array<std::uint8_t, pf_display::kLandscapeStatusBytes> landscape_status{};
std::array<std::uint8_t, pf_display::kLandscapeImageBytes> landscape_image{};
std::array<std::uint8_t, pf_display::kPortraitStatusBytes> portrait_status{};
std::array<std::uint8_t, pf_display::kPortraitImageBytes> portrait_image{};
std::array<std::uint8_t, pf_display::kFullFramebufferBytes> output{};
std::array<std::uint8_t, pf_display::kFullFramebufferBytes> original{};

PackedRegion landscape_status_region()
{
    return {
        landscape_status.data(),
        landscape_status.size(),
        pf_display::kPanelWidth,
        pf_display::kStatusBarHeight,
    };
}

PackedRegion landscape_image_region()
{
    return {
        landscape_image.data(),
        landscape_image.size(),
        pf_display::kPanelWidth,
        pf_display::kLandscapeImageHeight,
    };
}

PackedRegion portrait_status_region()
{
    return {
        portrait_status.data(),
        portrait_status.size(),
        pf_display::kPortraitImageWidth,
        pf_display::kStatusBarHeight,
    };
}

PackedRegion portrait_image_region()
{
    return {
        portrait_image.data(),
        portrait_image.size(),
        pf_display::kPortraitImageWidth,
        pf_display::kPortraitImageHeight,
    };
}

Color output_pixel(const std::size_t x, const std::size_t y)
{
    PackedFramebufferView frame{
        output.data(),
        output.size(),
        pf_display::kPanelWidth,
        pf_display::kPanelHeight,
    };
    Color color = Color::black;
    TEST_ASSERT_TRUE(frame.get_pixel(x, y, color));
    return color;
}

void assert_output_pixel(
    const std::size_t x,
    const std::size_t y,
    const Color expected)
{
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(expected),
        static_cast<int>(output_pixel(x, y)));
}

void fill_landscape_inputs()
{
    PackedFramebufferView status{
        landscape_status.data(),
        landscape_status.size(),
        pf_display::kPanelWidth,
        pf_display::kStatusBarHeight,
    };
    PackedFramebufferView image{
        landscape_image.data(),
        landscape_image.size(),
        pf_display::kPanelWidth,
        pf_display::kLandscapeImageHeight,
    };
    TEST_ASSERT_TRUE(status.fill(Color::yellow));
    TEST_ASSERT_TRUE(image.fill(Color::white));
}

void fill_portrait_inputs()
{
    PackedFramebufferView status{
        portrait_status.data(),
        portrait_status.size(),
        pf_display::kPortraitImageWidth,
        pf_display::kStatusBarHeight,
    };
    PackedFramebufferView image{
        portrait_image.data(),
        portrait_image.size(),
        pf_display::kPortraitImageWidth,
        pf_display::kPortraitImageHeight,
    };
    TEST_ASSERT_TRUE(status.fill(Color::yellow));
    TEST_ASSERT_TRUE(image.fill(Color::white));
    TEST_ASSERT_TRUE(image.set_pixel(0, 0, Color::red));
    TEST_ASSERT_TRUE(image.set_pixel(479, 0, Color::blue));
    TEST_ASSERT_TRUE(image.set_pixel(0, 759, Color::green));
    TEST_ASSERT_TRUE(image.set_pixel(479, 759, Color::black));
}

struct NativePoint {
    std::size_t x;
    std::size_t y;
};

NativePoint rotate_point(
    const std::size_t logical_x,
    const std::size_t logical_y,
    const PortraitRotation rotation)
{
    if (rotation == PortraitRotation::clockwise) {
        return {799U - logical_y, logical_x};
    }
    return {logical_y, 479U - logical_x};
}

void assert_rotated_pixel(
    const std::size_t logical_x,
    const std::size_t logical_y,
    const PortraitRotation rotation,
    const Color expected)
{
    const NativePoint point = rotate_point(logical_x, logical_y, rotation);
    assert_output_pixel(point.x, point.y, expected);
}

void test_landscape_composes_explicit_top_or_bottom_status()
{
    fill_landscape_inputs();

    auto result = pf_display::compose_landscape(
        landscape_status_region(),
        landscape_image_region(),
        StatusPlacement::top,
        output.data(),
        output.size());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(RenderStatus::ok),
        static_cast<int>(result.status));
    assert_output_pixel(0, 0, Color::yellow);
    assert_output_pixel(799, 39, Color::yellow);
    assert_output_pixel(0, 40, Color::white);
    assert_output_pixel(799, 479, Color::white);

    result = pf_display::compose_landscape(
        landscape_status_region(),
        landscape_image_region(),
        StatusPlacement::bottom,
        output.data(),
        output.size());
    TEST_ASSERT_TRUE(result.succeeded());
    assert_output_pixel(0, 0, Color::white);
    assert_output_pixel(799, 439, Color::white);
    assert_output_pixel(0, 440, Color::yellow);
    assert_output_pixel(799, 479, Color::yellow);
}

void assert_portrait_combination(
    const StatusPlacement placement,
    const PortraitRotation rotation)
{
    fill_portrait_inputs();
    const auto result = pf_display::compose_portrait(
        portrait_status_region(),
        portrait_image_region(),
        placement,
        rotation,
        output.data(),
        output.size());
    TEST_ASSERT_TRUE(result.succeeded());

    const std::size_t image_y = placement == StatusPlacement::top ? 40U : 0U;
    const std::size_t status_y = placement == StatusPlacement::top ? 0U : 760U;
    assert_rotated_pixel(0, image_y, rotation, Color::red);
    assert_rotated_pixel(479, image_y, rotation, Color::blue);
    assert_rotated_pixel(0, image_y + 759U, rotation, Color::green);
    assert_rotated_pixel(479, image_y + 759U, rotation, Color::black);
    assert_rotated_pixel(0, status_y, rotation, Color::yellow);
    assert_rotated_pixel(479, status_y + 39U, rotation, Color::yellow);
}

void test_portrait_rotates_all_placement_and_direction_combinations()
{
    assert_portrait_combination(
        StatusPlacement::top,
        PortraitRotation::clockwise);
    assert_portrait_combination(
        StatusPlacement::bottom,
        PortraitRotation::clockwise);
    assert_portrait_combination(
        StatusPlacement::top,
        PortraitRotation::counter_clockwise);
    assert_portrait_combination(
        StatusPlacement::bottom,
        PortraitRotation::counter_clockwise);
}

void assert_output_unchanged()
{
    TEST_ASSERT_EQUAL_UINT8_ARRAY(
        original.data(),
        output.data(),
        output.size());
}

void reset_output()
{
    std::fill(output.begin(), output.end(), 0xA5);
    original = output;
}

void test_invalid_landscape_inputs_leave_output_unchanged()
{
    fill_landscape_inputs();
    reset_output();

    PackedRegion invalid = landscape_status_region();
    invalid.length -= 1U;
    auto result = pf_display::compose_landscape(
        invalid,
        landscape_image_region(),
        StatusPlacement::top,
        output.data(),
        output.size());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(RenderStatus::invalid_length),
        static_cast<int>(result.status));
    assert_output_unchanged();

    invalid = landscape_status_region();
    invalid.height += 1U;
    result = pf_display::compose_landscape(
        invalid,
        landscape_image_region(),
        StatusPlacement::top,
        output.data(),
        output.size());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(RenderStatus::invalid_geometry),
        static_cast<int>(result.status));
    assert_output_unchanged();

    landscape_status[0] = 0x41;
    result = pf_display::compose_landscape(
        landscape_status_region(),
        landscape_image_region(),
        StatusPlacement::top,
        output.data(),
        output.size());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(RenderStatus::invalid_palette),
        static_cast<int>(result.status));
    assert_output_unchanged();
}

void test_invalid_output_and_enums_leave_output_unchanged()
{
    fill_portrait_inputs();
    reset_output();

    auto result = pf_display::compose_portrait(
        portrait_status_region(),
        portrait_image_region(),
        StatusPlacement::top,
        PortraitRotation::clockwise,
        output.data(),
        output.size() - 1U);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(RenderStatus::invalid_length),
        static_cast<int>(result.status));
    assert_output_unchanged();

    result = pf_display::compose_portrait(
        portrait_status_region(),
        portrait_image_region(),
        static_cast<StatusPlacement>(0xFF),
        PortraitRotation::clockwise,
        output.data(),
        output.size());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(RenderStatus::invalid_argument),
        static_cast<int>(result.status));
    assert_output_unchanged();

    result = pf_display::compose_portrait(
        portrait_status_region(),
        portrait_image_region(),
        StatusPlacement::top,
        static_cast<PortraitRotation>(0xFF),
        output.data(),
        output.size());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(RenderStatus::invalid_argument),
        static_cast<int>(result.status));
    assert_output_unchanged();
}

void test_null_and_overlapping_buffers_are_rejected()
{
    fill_landscape_inputs();
    reset_output();

    PackedRegion invalid = landscape_status_region();
    invalid.data = nullptr;
    auto result = pf_display::compose_landscape(
        invalid,
        landscape_image_region(),
        StatusPlacement::top,
        output.data(),
        output.size());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(RenderStatus::invalid_argument),
        static_cast<int>(result.status));
    assert_output_unchanged();

    result = pf_display::compose_landscape(
        landscape_status_region(),
        landscape_image_region(),
        StatusPlacement::top,
        nullptr,
        output.size());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(RenderStatus::invalid_argument),
        static_cast<int>(result.status));
    assert_output_unchanged();

    std::fill(output.begin(), output.end(), 0x11);
    original = output;
    const PackedRegion overlapping_status{
        output.data(),
        pf_display::kLandscapeStatusBytes,
        pf_display::kPanelWidth,
        pf_display::kStatusBarHeight,
    };
    result = pf_display::compose_landscape(
        overlapping_status,
        landscape_image_region(),
        StatusPlacement::top,
        output.data(),
        output.size());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(RenderStatus::invalid_argument),
        static_cast<int>(result.status));
    assert_output_unchanged();
}

}  // namespace

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_landscape_composes_explicit_top_or_bottom_status);
    RUN_TEST(test_portrait_rotates_all_placement_and_direction_combinations);
    RUN_TEST(test_invalid_landscape_inputs_leave_output_unchanged);
    RUN_TEST(test_invalid_output_and_enums_leave_output_unchanged);
    RUN_TEST(test_null_and_overlapping_buffers_are_rejected);
    return UNITY_END();
}
