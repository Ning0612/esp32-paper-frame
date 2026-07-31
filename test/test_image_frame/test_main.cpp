#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <unity.h>

#include "pf_carousel/image_frame.hpp"

extern "C" void setUp() {}
extern "C" void tearDown() {}

namespace {

void write_u16(
    std::vector<std::uint8_t>& output,
    const std::size_t offset,
    const std::uint16_t value)
{
    output[offset] = static_cast<std::uint8_t>(value & 0xFFU);
    output[offset + 1U] = static_cast<std::uint8_t>(value >> 8U);
}

void write_u32(
    std::vector<std::uint8_t>& output,
    const std::size_t offset,
    const std::uint32_t value)
{
    output[offset] = static_cast<std::uint8_t>(value & 0xFFU);
    output[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    output[offset + 2U] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
    output[offset + 3U] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
}

std::vector<std::uint8_t> make_file(const std::uint8_t orientation)
{
    const std::uint16_t width = orientation == 0U ? 800U : 480U;
    const std::uint16_t height = orientation == 0U ? 440U : 760U;
    const std::string filename = "test.pfr1";
    const std::size_t payload_length =
        pf_image::expected_payload_length(width, height);
    std::vector<std::uint8_t> output(
        pf_image::kPfr1HeaderSize + filename.size() + payload_length,
        0x11U);
    output[0] = 'P';
    output[1] = 'F';
    output[2] = 'R';
    output[3] = '1';
    output[4] = 1U;
    output[5] = static_cast<std::uint8_t>(pf_image::kPfr1HeaderSize);
    write_u16(output, 6U, 0U);
    write_u16(output, 8U, width);
    write_u16(output, 10U, height);
    output[12] = orientation;
    output[13] = pf_display::kPaletteVersion;
    output[14] = static_cast<std::uint8_t>(pf_image::Dithering::nearest);
    output[15] = 0U;
    write_u32(output, 16U, static_cast<std::uint32_t>(payload_length));
    write_u16(output, 20U, static_cast<std::uint16_t>(filename.size()));
    write_u16(output, 22U, 0U);
    for (std::size_t index = 0U; index < filename.size(); ++index) {
        output[pf_image::kPfr1HeaderSize + index] =
            static_cast<std::uint8_t>(filename[index]);
    }
    const std::size_t payload_offset =
        pf_image::kPfr1HeaderSize + filename.size();
    write_u32(
        output,
        24U,
        pf_image::crc32(output.data() + payload_offset, payload_length));
    write_u32(output, 28U, pf_image::crc32(output.data(), 24U));
    return output;
}

bool feed_file(
    pf_carousel::Pfr1FrameDecoder& decoder,
    const std::vector<std::uint8_t>& file)
{
    std::size_t offset = 0U;
    while (offset < file.size()) {
        const std::size_t amount =
            (file.size() - offset) > 733U ? 733U : file.size() - offset;
        if (!decoder.feed(file.data() + offset, amount)) {
            return false;
        }
        offset += amount;
    }
    return true;
}

pf_display::StatusBarContent make_status_content()
{
    pf_display::StatusBarContent content{};
    content.time_valid = true;
    content.year = 2026;
    content.month = 7;
    content.day = 31;
    content.iso_weekday = 5;
    content.weather_available = true;
    content.weather_stale = false;
    content.temperature_rounded = 27;
    content.icon_code[0] = '0';
    content.icon_code[1] = '1';
    content.icon_code[2] = 'd';
    return content;
}

bool any_byte_differs_from(
    const std::vector<std::uint8_t>& data,
    const std::uint8_t value)
{
    for (const std::uint8_t byte : data) {
        if (byte != value) {
            return true;
        }
    }
    return false;
}

void test_landscape_file_composes_rendered_status_and_preserves_image()
{
    const auto file = make_file(0U);
    std::vector<std::uint8_t> payload(pf_image::kPfr1MaxPayloadBytes);
    std::vector<std::uint8_t> status(pf_display::kLandscapeStatusBytes);
    std::vector<std::uint8_t> frame(
        pf_display::kFullFramebufferBytes,
        0x55U);
    pf_carousel::Pfr1FrameDecoder decoder(
        payload.data(),
        payload.size());
    const pf_display::StatusBarContent content = make_status_content();

    TEST_ASSERT_TRUE(feed_file(decoder, file));
    TEST_ASSERT_TRUE(decoder.finish_and_compose(
        status.data(),
        status.size(),
        frame.data(),
        frame.size(),
        content));

    // The image band (everything below the top status strip, since
    // placement defaults to top) is untouched all-white payload data.
    for (std::size_t index = pf_display::kLandscapeStatusBytes;
         index < frame.size();
         ++index) {
        TEST_ASSERT_EQUAL_HEX8(0x11U, frame[index]);
    }
    // The status strip now carries rendered date/weather glyphs rather
    // than a blank white fill.
    bool status_has_content = false;
    for (std::size_t index = 0U;
         index < pf_display::kLandscapeStatusBytes;
         ++index) {
        if (frame[index] != 0x11U) {
            status_has_content = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(status_has_content);
}

void test_portrait_file_composes_and_rejects_small_payload_buffer()
{
    const auto file = make_file(1U);
    std::vector<std::uint8_t> payload(pf_image::kPfr1MaxPayloadBytes);
    std::vector<std::uint8_t> status(pf_display::kLandscapeStatusBytes);
    std::vector<std::uint8_t> frame(
        pf_display::kFullFramebufferBytes,
        0x55U);
    pf_carousel::Pfr1FrameDecoder decoder(
        payload.data(),
        payload.size());
    const pf_display::StatusBarContent content = make_status_content();

    TEST_ASSERT_TRUE(feed_file(decoder, file));
    TEST_ASSERT_TRUE(decoder.finish_and_compose(
        status.data(),
        status.size(),
        frame.data(),
        frame.size(),
        content));
    // Rendered status content means the composed frame is no longer a
    // uniform all-white buffer.
    TEST_ASSERT_TRUE(any_byte_differs_from(frame, 0x11U));

    std::vector<std::uint8_t> too_small(1U);
    pf_carousel::Pfr1FrameDecoder rejected(
        too_small.data(),
        too_small.size());
    TEST_ASSERT_FALSE(rejected.feed(file.data(), file.size()));
}

void test_unsynced_time_still_composes_a_safe_placeholder()
{
    const auto file = make_file(0U);
    std::vector<std::uint8_t> payload(pf_image::kPfr1MaxPayloadBytes);
    std::vector<std::uint8_t> status(pf_display::kLandscapeStatusBytes);
    std::vector<std::uint8_t> frame(
        pf_display::kFullFramebufferBytes,
        0x55U);
    pf_carousel::Pfr1FrameDecoder decoder(
        payload.data(),
        payload.size());
    const pf_display::StatusBarContent content{};  // time_valid=false

    TEST_ASSERT_TRUE(feed_file(decoder, file));
    TEST_ASSERT_TRUE(decoder.finish_and_compose(
        status.data(),
        status.size(),
        frame.data(),
        frame.size(),
        content));
    for (std::size_t index = pf_display::kLandscapeStatusBytes;
         index < frame.size();
         ++index) {
        TEST_ASSERT_EQUAL_HEX8(0x11U, frame[index]);
    }
}

}  // namespace

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_landscape_file_composes_rendered_status_and_preserves_image);
    RUN_TEST(test_portrait_file_composes_and_rejects_small_payload_buffer);
    RUN_TEST(test_unsynced_time_still_composes_a_safe_placeholder);
    return UNITY_END();
}
