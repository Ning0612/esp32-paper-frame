#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <unity.h>
#include <zlib.h>

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

std::vector<std::uint8_t> deflate_raw(const std::vector<std::uint8_t>& input)
{
    z_stream strm{};
    TEST_ASSERT_EQUAL(
        Z_OK,
        deflateInit2(
            &strm, Z_BEST_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8,
            Z_DEFAULT_STRATEGY));
    std::vector<std::uint8_t> output(input.size() + 64U);
    strm.next_in = const_cast<Bytef*>(input.data());
    strm.avail_in = static_cast<uInt>(input.size());
    strm.next_out = output.data();
    strm.avail_out = static_cast<uInt>(output.size());
    TEST_ASSERT_EQUAL(Z_STREAM_END, deflate(&strm, Z_FINISH));
    output.resize(output.size() - strm.avail_out);
    deflateEnd(&strm);
    return output;
}

std::vector<std::uint8_t> make_compressed_file(const std::uint8_t orientation)
{
    const std::uint16_t width = orientation == 0U ? 800U : 480U;
    const std::uint16_t height = orientation == 0U ? 440U : 760U;
    const std::string filename = "test.pfr1";
    const std::size_t raw_payload_length =
        pf_image::expected_payload_length(width, height);
    const std::vector<std::uint8_t> raw_payload(raw_payload_length, 0x11U);
    const auto compressed = deflate_raw(raw_payload);
    TEST_ASSERT_TRUE(compressed.size() < raw_payload.size());

    std::vector<std::uint8_t> output(
        pf_image::kPfr1HeaderSize + filename.size() + compressed.size(), 0U);
    output[0] = 'P';
    output[1] = 'F';
    output[2] = 'R';
    output[3] = '1';
    output[4] = 1U;
    output[5] = static_cast<std::uint8_t>(pf_image::kPfr1HeaderSize);
    write_u16(output, 6U, pf_image::kCompressed);
    write_u16(output, 8U, width);
    write_u16(output, 10U, height);
    output[12] = orientation;
    output[13] = pf_display::kPaletteVersion;
    output[14] = static_cast<std::uint8_t>(pf_image::Dithering::nearest);
    output[15] = 0U;
    write_u32(output, 16U, static_cast<std::uint32_t>(compressed.size()));
    write_u16(output, 20U, static_cast<std::uint16_t>(filename.size()));
    write_u16(output, 22U, 0U);
    for (std::size_t index = 0U; index < filename.size(); ++index) {
        output[pf_image::kPfr1HeaderSize + index] =
            static_cast<std::uint8_t>(filename[index]);
    }
    const std::size_t payload_offset =
        pf_image::kPfr1HeaderSize + filename.size();
    for (std::size_t index = 0U; index < compressed.size(); ++index) {
        output[payload_offset + index] = compressed[index];
    }
    write_u32(
        output,
        24U,
        pf_image::crc32(output.data() + payload_offset, compressed.size()));
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

bool compose_portrait_frame(
    std::vector<std::uint8_t>& frame,
    const std::vector<std::uint8_t>& file,
    const pf_display::StatusBarContent& content,
    const bool use_default_rotation,
    const pf_display::PortraitRotation rotation)
{
    std::vector<std::uint8_t> payload(pf_image::kPfr1MaxPayloadBytes);
    std::vector<std::uint8_t> status(pf_display::kLandscapeStatusBytes);
    frame.assign(pf_display::kFullFramebufferBytes, 0x55U);
    pf_carousel::Pfr1FrameDecoder decoder(payload.data(), payload.size());
    if (!feed_file(decoder, file)) {
        return false;
    }
    if (use_default_rotation) {
        return decoder.finish_and_compose(
            status.data(),
            status.size(),
            frame.data(),
            frame.size(),
            content);
    }
    return decoder.finish_and_compose(
        status.data(),
        status.size(),
        frame.data(),
        frame.size(),
        content,
        pf_display::StatusPlacement::top,
        rotation);
}

// The panel is mounted so that a portrait frame rotated clockwise comes out
// upside down; reported from hardware on 2026-08-19. The two rotations map
// the logical canvas point-symmetrically, so they differ by exactly 180
// degrees and the default has to be the counter-clockwise one for what the
// panel shows to match how the device is actually placed.
void test_portrait_default_rotation_matches_physical_mounting()
{
    const auto file = make_file(1U);
    const pf_display::StatusBarContent content = make_status_content();

    std::vector<std::uint8_t> by_default;
    std::vector<std::uint8_t> counter_clockwise;
    std::vector<std::uint8_t> clockwise;

    TEST_ASSERT_TRUE(compose_portrait_frame(
        by_default,
        file,
        content,
        true,
        pf_display::PortraitRotation::clockwise));
    TEST_ASSERT_TRUE(compose_portrait_frame(
        counter_clockwise,
        file,
        content,
        false,
        pf_display::PortraitRotation::counter_clockwise));
    TEST_ASSERT_TRUE(compose_portrait_frame(
        clockwise,
        file,
        content,
        false,
        pf_display::PortraitRotation::clockwise));

    TEST_ASSERT_EQUAL_MEMORY(
        counter_clockwise.data(),
        by_default.data(),
        by_default.size());

    // Without this the assertion above would still pass if the two rotations
    // ever became equivalent, making the test vacuous.
    TEST_ASSERT_NOT_EQUAL(
        0,
        std::memcmp(
            clockwise.data(),
            counter_clockwise.data(),
            counter_clockwise.size()));
}

void test_compressed_file_decodes_and_composes_same_as_uncompressed()
{
    const auto compressed_file = make_compressed_file(0U);
    const auto uncompressed_file = make_file(0U);
    const pf_display::StatusBarContent content = make_status_content();

    std::vector<std::uint8_t> compressed_scratch(pf_image::kPfr1MaxPayloadBytes);
    std::vector<std::uint8_t> output_scratch(pf_image::kPfr1MaxPayloadBytes);
    pf_image::Pfr1InflateBuffers inflate_buffers;
    inflate_buffers.compressed = compressed_scratch.data();
    inflate_buffers.compressed_capacity = compressed_scratch.size();
    inflate_buffers.output = output_scratch.data();
    inflate_buffers.output_capacity = output_scratch.size();

    std::vector<std::uint8_t> payload_a(pf_image::kPfr1MaxPayloadBytes);
    std::vector<std::uint8_t> status_a(pf_display::kLandscapeStatusBytes);
    std::vector<std::uint8_t> frame_a(pf_display::kFullFramebufferBytes, 0x55U);
    pf_carousel::Pfr1FrameDecoder compressed_decoder(
        payload_a.data(), payload_a.size(), &inflate_buffers);
    TEST_ASSERT_TRUE(feed_file(compressed_decoder, compressed_file));
    TEST_ASSERT_TRUE(compressed_decoder.finish_and_compose(
        status_a.data(), status_a.size(), frame_a.data(), frame_a.size(),
        content));

    std::vector<std::uint8_t> payload_b(pf_image::kPfr1MaxPayloadBytes);
    std::vector<std::uint8_t> status_b(pf_display::kLandscapeStatusBytes);
    std::vector<std::uint8_t> frame_b(pf_display::kFullFramebufferBytes, 0x55U);
    pf_carousel::Pfr1FrameDecoder uncompressed_decoder(
        payload_b.data(), payload_b.size());
    TEST_ASSERT_TRUE(feed_file(uncompressed_decoder, uncompressed_file));
    TEST_ASSERT_TRUE(uncompressed_decoder.finish_and_compose(
        status_b.data(), status_b.size(), frame_b.data(), frame_b.size(),
        content));

    TEST_ASSERT_EQUAL_UINT32(frame_b.size(), frame_a.size());
    TEST_ASSERT_EQUAL_MEMORY(frame_b.data(), frame_a.data(), frame_b.size());
}

void test_compressed_file_without_inflate_buffers_fails_closed()
{
    const auto compressed_file = make_compressed_file(0U);
    std::vector<std::uint8_t> payload(pf_image::kPfr1MaxPayloadBytes);
    pf_carousel::Pfr1FrameDecoder decoder(payload.data(), payload.size());
    TEST_ASSERT_FALSE(feed_file(decoder, compressed_file));
    TEST_ASSERT_EQUAL(
        static_cast<int>(pf_image::ValidationError::unsupported_compression),
        static_cast<int>(decoder.error()));
}

void test_corrupt_compressed_stream_fails_closed_without_partial_frame()
{
    auto compressed_file = make_compressed_file(0U);
    const std::size_t filename_length = std::string("test.pfr1").size();
    const std::size_t payload_offset =
        pf_image::kPfr1HeaderSize + filename_length;
    compressed_file[payload_offset + 2U] ^= 0xFFU;
    write_u32(
        compressed_file,
        24U,
        pf_image::crc32(
            compressed_file.data() + payload_offset,
            compressed_file.size() - payload_offset));
    write_u32(
        compressed_file, 28U, pf_image::crc32(compressed_file.data(), 24U));

    std::vector<std::uint8_t> compressed_scratch(pf_image::kPfr1MaxPayloadBytes);
    std::vector<std::uint8_t> output_scratch(pf_image::kPfr1MaxPayloadBytes);
    pf_image::Pfr1InflateBuffers inflate_buffers;
    inflate_buffers.compressed = compressed_scratch.data();
    inflate_buffers.compressed_capacity = compressed_scratch.size();
    inflate_buffers.output = output_scratch.data();
    inflate_buffers.output_capacity = output_scratch.size();

    std::vector<std::uint8_t> payload(pf_image::kPfr1MaxPayloadBytes);
    std::vector<std::uint8_t> status(pf_display::kLandscapeStatusBytes);
    std::vector<std::uint8_t> frame(pf_display::kFullFramebufferBytes, 0x55U);
    pf_carousel::Pfr1FrameDecoder decoder(
        payload.data(), payload.size(), &inflate_buffers);
    const pf_display::StatusBarContent content = make_status_content();

    TEST_ASSERT_TRUE(feed_file(decoder, compressed_file));
    TEST_ASSERT_FALSE(decoder.finish_and_compose(
        status.data(), status.size(), frame.data(), frame.size(), content));
    TEST_ASSERT_EQUAL(
        static_cast<int>(pf_image::ValidationError::payload_inflate_failed),
        static_cast<int>(decoder.error()));
    // No partial/garbage framebuffer write on failure.
    for (const std::uint8_t byte : frame) {
        TEST_ASSERT_EQUAL_HEX8(0x55U, byte);
    }
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
    RUN_TEST(test_portrait_default_rotation_matches_physical_mounting);
    RUN_TEST(test_compressed_file_decodes_and_composes_same_as_uncompressed);
    RUN_TEST(test_compressed_file_without_inflate_buffers_fails_closed);
    RUN_TEST(test_corrupt_compressed_stream_fails_closed_without_partial_frame);
    RUN_TEST(test_unsynced_time_still_composes_a_safe_placeholder);
    return UNITY_END();
}
