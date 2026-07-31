#include <array>
#include <cstdint>

#include <unity.h>

#include "pf_display/bitmap_font.hpp"

using pf_display::Color;
using pf_display::Glyph;
using pf_display::PackedFramebufferView;

extern "C" void setUp() {}
extern "C" void tearDown() {}

namespace {

void test_glyph_for_known_characters_matches_authored_table()
{
    Glyph glyph{};
    TEST_ASSERT_TRUE(pf_display::glyph_for('0', glyph));
    TEST_ASSERT_EQUAL_HEX8(0b111, glyph.rows[0]);
    TEST_ASSERT_EQUAL_HEX8(0b101, glyph.rows[1]);
    TEST_ASSERT_EQUAL_HEX8(0b111, glyph.rows[4]);

    TEST_ASSERT_TRUE(pf_display::glyph_for('-', glyph));
    TEST_ASSERT_EQUAL_HEX8(0b000, glyph.rows[0]);
    TEST_ASSERT_EQUAL_HEX8(0b111, glyph.rows[2]);

    TEST_ASSERT_TRUE(pf_display::glyph_for(' ', glyph));
    for (const std::uint8_t row : glyph.rows) {
        TEST_ASSERT_EQUAL_HEX8(0U, row);
    }
}

void test_glyph_for_unsupported_character_returns_false()
{
    Glyph glyph{};
    TEST_ASSERT_FALSE(pf_display::glyph_for('A', glyph));
}

void test_draw_text_paints_exact_glyph_pixels_at_scale_one()
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

    TEST_ASSERT_TRUE(
        pf_display::draw_text(view, 0U, 0U, "1", Color::black, 1U));

    // Glyph '1' rows: 010, 110, 010, 010, 111 (see bitmap_font.hpp).
    Color color{};
    TEST_ASSERT_TRUE(view.get_pixel(0U, 0U, color));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Color::white), static_cast<int>(color));
    TEST_ASSERT_TRUE(view.get_pixel(1U, 0U, color));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Color::black), static_cast<int>(color));
    TEST_ASSERT_TRUE(view.get_pixel(0U, 1U, color));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Color::black), static_cast<int>(color));
    TEST_ASSERT_TRUE(view.get_pixel(0U, 4U, color));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Color::black), static_cast<int>(color));
    TEST_ASSERT_TRUE(view.get_pixel(2U, 4U, color));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Color::black), static_cast<int>(color));
}

void test_draw_text_rejects_invalid_arguments()
{
    static std::array<
        std::uint8_t,
        pf_display::checked_packed_buffer_bytes(8U, 8U).bytes>
        buffer{};
    PackedFramebufferView view{buffer.data(), buffer.size(), 8U, 8U};

    TEST_ASSERT_FALSE(
        pf_display::draw_text(view, 0U, 0U, nullptr, Color::black, 1U));
    TEST_ASSERT_FALSE(
        pf_display::draw_text(view, 0U, 0U, "0", Color::black, 0U));
}

void test_text_width_accounts_for_inter_glyph_spacing()
{
    TEST_ASSERT_EQUAL_UINT(0U, pf_display::text_width(0U, 3U));
    TEST_ASSERT_EQUAL_UINT(9U, pf_display::text_width(1U, 3U));
    TEST_ASSERT_EQUAL_UINT(21U, pf_display::text_width(2U, 3U));
}

}  // namespace

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_glyph_for_known_characters_matches_authored_table);
    RUN_TEST(test_glyph_for_unsupported_character_returns_false);
    RUN_TEST(test_draw_text_paints_exact_glyph_pixels_at_scale_one);
    RUN_TEST(test_draw_text_rejects_invalid_arguments);
    RUN_TEST(test_text_width_accounts_for_inter_glyph_spacing);
    return UNITY_END();
}
