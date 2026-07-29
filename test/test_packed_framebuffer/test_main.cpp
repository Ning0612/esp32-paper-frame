#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>

#include <unity.h>

#include "pf_display/packed_framebuffer.hpp"

extern "C" void setUp() {}
extern "C" void tearDown() {}

namespace {

using pf_display::Color;
using pf_display::PackedFramebufferView;

void test_geometry_matches_phase2_payload_sizes()
{
    TEST_ASSERT_EQUAL_UINT32(400, pf_display::packed_row_bytes(800));
    TEST_ASSERT_EQUAL_UINT32(192000, pf_display::kFullFramebufferBytes);
    TEST_ASSERT_EQUAL_UINT32(176000, pf_display::kLandscapeImageBytes);
    TEST_ASSERT_EQUAL_UINT32(182400, pf_display::kPortraitImageBytes);
}

void test_geometry_reports_size_overflow()
{
    const pf_display::PackedSize overflow =
        pf_display::checked_packed_buffer_bytes(
            std::numeric_limits<std::size_t>::max(),
            2);
    TEST_ASSERT_FALSE(overflow.ok);
    TEST_ASSERT_EQUAL_UINT32(0, overflow.bytes);

    const pf_display::PackedSize empty =
        pf_display::checked_packed_buffer_bytes(0, 0);
    TEST_ASSERT_TRUE(empty.ok);
    TEST_ASSERT_EQUAL_UINT32(0, empty.bytes);
}

void test_palette_v1_uses_only_six_native_codes()
{
    TEST_ASSERT_EQUAL_UINT8(1, pf_display::kPaletteVersion);
    TEST_ASSERT_EQUAL_HEX8(0x0, pf_display::native_code(Color::black));
    TEST_ASSERT_EQUAL_HEX8(0x1, pf_display::native_code(Color::white));
    TEST_ASSERT_EQUAL_HEX8(0x2, pf_display::native_code(Color::yellow));
    TEST_ASSERT_EQUAL_HEX8(0x3, pf_display::native_code(Color::red));
    TEST_ASSERT_EQUAL_HEX8(0x5, pf_display::native_code(Color::blue));
    TEST_ASSERT_EQUAL_HEX8(0x6, pf_display::native_code(Color::green));
    TEST_ASSERT_FALSE(pf_display::is_valid_native_code(0x4));
    TEST_ASSERT_FALSE(pf_display::is_valid_native_code(0x7));
    TEST_ASSERT_FALSE(pf_display::is_valid_native_code(0xF));
}

void test_golden_pairs_pack_even_pixel_into_high_nibble()
{
    std::uint8_t packed = 0xFF;
    TEST_ASSERT_TRUE(
        pf_display::pack_colors(Color::black, Color::white, packed));
    TEST_ASSERT_EQUAL_HEX8(0x01, packed);
    TEST_ASSERT_TRUE(
        pf_display::pack_colors(Color::yellow, Color::red, packed));
    TEST_ASSERT_EQUAL_HEX8(0x23, packed);
    TEST_ASSERT_TRUE(
        pf_display::pack_colors(Color::blue, Color::green, packed));
    TEST_ASSERT_EQUAL_HEX8(0x56, packed);
}

void test_set_pixel_preserves_its_neighbor_and_addresses_boundaries()
{
    static std::array<std::uint8_t, pf_display::kFullFramebufferBytes> buffer{};
    std::fill(buffer.begin(), buffer.end(), 0xAA);
    PackedFramebufferView framebuffer{
        buffer.data(),
        buffer.size(),
        pf_display::kPanelWidth,
        pf_display::kPanelHeight,
    };

    TEST_ASSERT_TRUE(framebuffer.set_pixel(0, 0, Color::black));
    TEST_ASSERT_EQUAL_HEX8(0x0A, buffer[0]);
    TEST_ASSERT_TRUE(framebuffer.set_pixel(1, 0, Color::white));
    TEST_ASSERT_EQUAL_HEX8(0x01, buffer[0]);

    const std::size_t last_offset = buffer.size() - 1U;
    TEST_ASSERT_TRUE(framebuffer.set_pixel(798, 479, Color::blue));
    TEST_ASSERT_EQUAL_HEX8(0x5A, buffer[last_offset]);
    TEST_ASSERT_TRUE(framebuffer.set_pixel(799, 479, Color::green));
    TEST_ASSERT_EQUAL_HEX8(0x56, buffer[last_offset]);
}

void test_read_and_fill_use_the_same_palette_contract()
{
    std::array<std::uint8_t, 4> buffer{};
    PackedFramebufferView framebuffer{
        buffer.data(),
        buffer.size(),
        4,
        2,
    };

    TEST_ASSERT_TRUE(framebuffer.fill(Color::white));
    for (const std::uint8_t value : buffer) {
        TEST_ASSERT_EQUAL_HEX8(0x11, value);
    }

    TEST_ASSERT_TRUE(framebuffer.set_pixel(2, 1, Color::red));
    Color output = Color::black;
    TEST_ASSERT_TRUE(framebuffer.get_pixel(2, 1, output));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(Color::red),
        static_cast<int>(output));
    TEST_ASSERT_TRUE(framebuffer.get_pixel(3, 1, output));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(Color::white),
        static_cast<int>(output));
}

void test_invalid_inputs_do_not_modify_the_buffer_or_output()
{
    std::array<std::uint8_t, 4> buffer{0x42, 0x34, 0x56, 0x78};
    const auto original = buffer;
    PackedFramebufferView framebuffer{
        buffer.data(),
        buffer.size(),
        4,
        2,
    };
    const Color invalid = static_cast<Color>(0x4);

    TEST_ASSERT_FALSE(framebuffer.set_pixel(4, 0, Color::black));
    TEST_ASSERT_FALSE(framebuffer.set_pixel(0, 2, Color::black));
    TEST_ASSERT_FALSE(framebuffer.set_pixel(0, 0, invalid));
    TEST_ASSERT_FALSE(framebuffer.fill(invalid));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(original.data(), buffer.data(), buffer.size());

    std::uint8_t packed = 0xA5;
    TEST_ASSERT_FALSE(
        pf_display::pack_colors(invalid, Color::white, packed));
    TEST_ASSERT_EQUAL_HEX8(0xA5, packed);

    Color output = Color::green;
    TEST_ASSERT_FALSE(framebuffer.get_pixel(0, 0, output));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(Color::green),
        static_cast<int>(output));

    PackedFramebufferView undersized{
        buffer.data(),
        buffer.size() - 1U,
        4,
        2,
    };
    TEST_ASSERT_FALSE(undersized.valid());
    TEST_ASSERT_FALSE(undersized.set_pixel(0, 0, Color::black));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(original.data(), buffer.data(), buffer.size());
}

}  // namespace

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_geometry_matches_phase2_payload_sizes);
    RUN_TEST(test_geometry_reports_size_overflow);
    RUN_TEST(test_palette_v1_uses_only_six_native_codes);
    RUN_TEST(test_golden_pairs_pack_even_pixel_into_high_nibble);
    RUN_TEST(test_set_pixel_preserves_its_neighbor_and_addresses_boundaries);
    RUN_TEST(test_read_and_fill_use_the_same_palette_contract);
    RUN_TEST(test_invalid_inputs_do_not_modify_the_buffer_or_output);
    return UNITY_END();
}
