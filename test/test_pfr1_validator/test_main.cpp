#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <unity.h>
#include <zlib.h>

#include "pf_image/pfr1.hpp"

extern "C" void setUp() {}
extern "C" void tearDown() {}

namespace {

using pf_image::Pfr1Validator;

void write_u16(std::vector<std::uint8_t>& output, const std::size_t offset,
               const std::uint16_t value)
{
    output[offset] = static_cast<std::uint8_t>(value & 0xFFU);
    output[offset + 1U] = static_cast<std::uint8_t>(value >> 8U);
}

void write_u32(std::vector<std::uint8_t>& output, const std::size_t offset,
               const std::uint32_t value)
{
    output[offset] = static_cast<std::uint8_t>(value & 0xFFU);
    output[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    output[offset + 2U] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
    output[offset + 3U] = static_cast<std::uint8_t>(value >> 24U);
}

std::vector<std::uint8_t> make_file(
    const std::uint8_t orientation,
    const std::string& filename,
    const std::uint8_t payload_byte = 0x11U)
{
    const std::uint16_t width = orientation == 0U ? 800U : 480U;
    const std::uint16_t height = orientation == 0U ? 440U : 760U;
    const std::size_t payload_length =
        pf_image::expected_payload_length(width, height);
    std::vector<std::uint8_t> output(
        pf_image::kPfr1HeaderSize + filename.size() + payload_length,
        payload_byte);
    output[0] = 'P';
    output[1] = 'F';
    output[2] = 'R';
    output[3] = '1';
    output[4] = 1U;
    output[5] = static_cast<std::uint8_t>(pf_image::kPfr1HeaderSize);
    write_u16(output, 6U, pf_image::kMirrorX);
    write_u16(output, 8U, width);
    write_u16(output, 10U, height);
    output[12] = orientation;
    output[13] = 1U;
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
    const std::uint32_t payload_crc =
        pf_image::crc32(output.data() + payload_offset, payload_length);
    write_u32(output, 24U, payload_crc);
    write_u32(output, 28U, pf_image::crc32(output.data(), 24U));
    return output;
}

// Test-only raw-DEFLATE compressor (the production Pfr1Validator only ever
// decompresses; fixtures for the compressed-payload tests are built here
// with the same system zlib that pf_image/pfr1.hpp uses on this platform).
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

std::vector<std::uint8_t> make_compressed_file(
    const std::uint8_t orientation,
    const std::string& filename,
    const std::vector<std::uint8_t>& raw_payload)
{
    const std::uint16_t width = orientation == 0U ? 800U : 480U;
    const std::uint16_t height = orientation == 0U ? 440U : 760U;
    TEST_ASSERT_EQUAL_UINT32(
        pf_image::expected_payload_length(width, height), raw_payload.size());
    const auto compressed = deflate_raw(raw_payload);
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
    output[13] = 1U;
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
    const std::uint32_t payload_crc = pf_image::crc32(
        output.data() + payload_offset, compressed.size());
    write_u32(output, 24U, payload_crc);
    write_u32(output, 28U, pf_image::crc32(output.data(), 24U));
    return output;
}

// Recomputes both CRC32 fields after a test has mutated header or payload
// bytes in place, so corruption tests can target a single field without
// tripping an unrelated CRC mismatch first.
void refresh_crcs(std::vector<std::uint8_t>& file, const std::size_t payload_offset,
                   const std::size_t payload_length)
{
    write_u32(
        file, 24U,
        pf_image::crc32(file.data() + payload_offset, payload_length));
    write_u32(file, 28U, pf_image::crc32(file.data(), 24U));
}

struct InflateFixture {
    std::array<std::uint8_t, pf_image::kPfr1MaxPayloadBytes> compressed{};
    std::array<std::uint8_t, pf_image::kPfr1MaxPayloadBytes> output{};

    pf_image::Pfr1InflateBuffers buffers()
    {
        pf_image::Pfr1InflateBuffers result;
        result.compressed = compressed.data();
        result.compressed_capacity = compressed.size();
        result.output = output.data();
        result.output_capacity = output.size();
        return result;
    }
};

struct SinkState {
    std::vector<std::uint8_t> bytes;
    std::vector<std::uint32_t> offsets;
};

bool collect_payload(
    void* const context,
    const std::uint32_t offset,
    const std::uint8_t* const data,
    const std::size_t length)
{
    auto& state = *static_cast<SinkState*>(context);
    state.offsets.push_back(offset);
    state.bytes.insert(state.bytes.end(), data, data + length);
    return true;
}

void test_crc32_known_vector()
{
    constexpr std::array<std::uint8_t, 9U> input{
        '1', '2', '3', '4', '5', '6', '7', '8', '9'};
    TEST_ASSERT_EQUAL_HEX32(
        0xCBF43926U,
        pf_image::crc32(input.data(), input.size()));
}

void test_valid_file_accepts_arbitrary_chunks_and_sinks_payload()
{
    const auto file = make_file(0U, "sunset.pfr1", 0x11U);
    SinkState sink{};
    Pfr1Validator validator(collect_payload, &sink);
    std::size_t offset = 0U;
    const std::array<std::size_t, 7U> chunks{1U, 7U, 24U, 3U, 512U, 4096U, 8191U};
    for (const std::size_t chunk : chunks) {
        if (offset == file.size()) {
            break;
        }
        const std::size_t amount =
            (chunk < (file.size() - offset)) ? chunk : (file.size() - offset);
        TEST_ASSERT_TRUE(validator.feed(file.data() + offset, amount));
        offset += amount;
    }
    while (offset < file.size()) {
        const std::size_t amount =
            ((file.size() - offset) > 777U) ? 777U : (file.size() - offset);
        TEST_ASSERT_TRUE(validator.feed(file.data() + offset, amount));
        offset += amount;
    }
    TEST_ASSERT_TRUE(validator.finish());
    TEST_ASSERT_TRUE(validator.finalized());
    TEST_ASSERT_EQUAL_UINT16(800U, validator.header().width);
    TEST_ASSERT_EQUAL_UINT16(440U, validator.header().height);
    TEST_ASSERT_EQUAL_UINT32(176000U, validator.header().payload_length);
    TEST_ASSERT_EQUAL_UINT32(176000U, sink.bytes.size());
    TEST_ASSERT_TRUE(sink.offsets.size() > 1U);
    TEST_ASSERT_EQUAL_UINT32(0U, sink.offsets.front());
    TEST_ASSERT_EQUAL_UINT8(0x11U, sink.bytes.front());
    TEST_ASSERT_EQUAL_UINT8(0x11U, sink.bytes.back());
}

void test_portrait_dimensions_and_filename_are_preserved()
{
    const auto file = make_file(1U, "相框.pfr1", 0x65U);
    Pfr1Validator validator{};
    TEST_ASSERT_TRUE(validator.feed(file.data(), file.size()));
    TEST_ASSERT_TRUE(validator.finish());
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<std::uint8_t>(pf_image::Orientation::portrait),
        static_cast<std::uint8_t>(validator.header().orientation));
    TEST_ASSERT_EQUAL_UINT16(480U, validator.header().width);
    TEST_ASSERT_EQUAL_UINT16(760U, validator.header().height);
    TEST_ASSERT_EQUAL_UINT16(
        static_cast<std::uint16_t>(std::string("相框.pfr1").size()),
        validator.header().filename_length);
    TEST_ASSERT_EQUAL_UINT8(0xE7U, validator.filename()[0]);
}

void test_rejects_bad_header_and_reserved_fields()
{
    auto file = make_file(0U, "ok.pfr1");
    file[0] = 'X';
    Pfr1Validator bad_magic{};
    TEST_ASSERT_FALSE(bad_magic.feed(file.data(), file.size()));
    TEST_ASSERT_EQUAL(
        static_cast<int>(pf_image::ValidationError::bad_magic),
        static_cast<int>(bad_magic.error()));

    file = make_file(0U, "ok.pfr1");
    file[22] = 1U;
    write_u32(file, 28U, pf_image::crc32(file.data(), 24U));
    Pfr1Validator bad_reserved{};
    TEST_ASSERT_FALSE(bad_reserved.feed(file.data(), file.size()));
    TEST_ASSERT_EQUAL(
        static_cast<int>(pf_image::ValidationError::invalid_reserved),
        static_cast<int>(bad_reserved.error()));
}

void test_rejects_invalid_palette_filename_and_crc()
{
    auto file = make_file(0U, "ok.pfr1", 0x44U);
    write_u32(file, 28U, pf_image::crc32(file.data(), 24U));
    Pfr1Validator invalid_palette{};
    TEST_ASSERT_FALSE(invalid_palette.feed(file.data(), file.size()));
    TEST_ASSERT_EQUAL(
        static_cast<int>(pf_image::ValidationError::invalid_payload_palette),
        static_cast<int>(invalid_palette.error()));

    file = make_file(0U, "../escape.pfr1");
    Pfr1Validator invalid_filename{};
    TEST_ASSERT_FALSE(invalid_filename.feed(file.data(), file.size()));
    TEST_ASSERT_EQUAL(
        static_cast<int>(pf_image::ValidationError::invalid_filename),
        static_cast<int>(invalid_filename.error()));

    file = make_file(0U, "ok.pfr1");
    file.back() ^= 0x01U;
    Pfr1Validator bad_crc{};
    TEST_ASSERT_TRUE(bad_crc.feed(file.data(), file.size()));
    TEST_ASSERT_FALSE(bad_crc.finish());
    TEST_ASSERT_EQUAL(
        static_cast<int>(pf_image::ValidationError::payload_crc_mismatch),
        static_cast<int>(bad_crc.error()));
}

void test_rejects_trailing_and_truncated_files()
{
    auto file = make_file(0U, "ok.pfr1");
    file.push_back(0U);
    Pfr1Validator trailing{};
    TEST_ASSERT_FALSE(trailing.feed(file.data(), file.size()));
    TEST_ASSERT_EQUAL(
        static_cast<int>(pf_image::ValidationError::trailing_data),
        static_cast<int>(trailing.error()));

    file = make_file(0U, "ok.pfr1");
    file.resize(file.size() - 1U);
    Pfr1Validator truncated{};
    TEST_ASSERT_TRUE(truncated.feed(file.data(), file.size()));
    TEST_ASSERT_FALSE(truncated.finish());
    TEST_ASSERT_EQUAL(
        static_cast<int>(pf_image::ValidationError::incomplete),
        static_cast<int>(truncated.error()));
}

void test_cross_language_golden_vector_matches_documented_crcs()
{
    const auto file = make_file(0U, "golden.pfr1", 0x11U);
    Pfr1Validator validator{};
    TEST_ASSERT_TRUE(validator.feed(file.data(), file.size()));
    TEST_ASSERT_TRUE(validator.finish());
    TEST_ASSERT_EQUAL_HEX32(0xAF00B5BDU, validator.header().payload_crc32);
    TEST_ASSERT_EQUAL_HEX32(0xC96F698BU, validator.header().header_crc32);
}

void test_compressed_payload_round_trips_through_streaming_validator()
{
    std::vector<std::uint8_t> raw_payload(176000U, 0x11U);
    // Give the compressor some structure beyond a single repeated byte so
    // the round-trip exercises more than a degenerate one-symbol stream.
    for (std::size_t index = 0U; index < raw_payload.size(); index += 97U) {
        raw_payload[index] = 0x66U;
    }
    const auto file = make_compressed_file(0U, "photo.pfr1", raw_payload);

    InflateFixture fixture{};
    const auto buffers = fixture.buffers();
    SinkState sink{};
    Pfr1Validator validator(collect_payload, &sink, &buffers);

    std::size_t offset = 0U;
    const std::array<std::size_t, 5U> chunks{1U, 7U, 24U, 3U, 4096U};
    while (offset < file.size()) {
        for (const std::size_t chunk : chunks) {
            if (offset == file.size()) {
                break;
            }
            const std::size_t amount =
                (chunk < (file.size() - offset)) ? chunk : (file.size() - offset);
            TEST_ASSERT_TRUE(validator.feed(file.data() + offset, amount));
            offset += amount;
        }
    }
    TEST_ASSERT_TRUE(validator.finish());
    TEST_ASSERT_TRUE(validator.finalized());
    TEST_ASSERT_EQUAL_UINT32(176000U, sink.bytes.size());
    TEST_ASSERT_TRUE(sink.bytes.size() == raw_payload.size() &&
                      std::equal(raw_payload.begin(), raw_payload.end(), sink.bytes.begin()));
    TEST_ASSERT_EQUAL_UINT32(0U, sink.offsets.front());
}

void test_compressed_payload_rejects_when_scratch_buffer_missing()
{
    const std::vector<std::uint8_t> raw_payload(176000U, 0x11U);
    const auto file = make_compressed_file(0U, "photo.pfr1", raw_payload);

    Pfr1Validator validator{};
    TEST_ASSERT_FALSE(validator.feed(file.data(), file.size()));
    TEST_ASSERT_EQUAL(
        static_cast<int>(pf_image::ValidationError::unsupported_compression),
        static_cast<int>(validator.error()));
}

void test_compressed_payload_length_bounds_are_enforced()
{
    const std::vector<std::uint8_t> raw_payload(176000U, 0x11U);
    InflateFixture fixture{};
    const auto buffers = fixture.buffers();

    auto zero_length = make_compressed_file(0U, "photo.pfr1", raw_payload);
    write_u32(zero_length, 16U, 0U);
    write_u32(zero_length, 28U, pf_image::crc32(zero_length.data(), 24U));
    Pfr1Validator zero_validator(nullptr, nullptr, &buffers);
    TEST_ASSERT_FALSE(zero_validator.feed(zero_length.data(), pf_image::kPfr1HeaderSize));
    TEST_ASSERT_EQUAL(
        static_cast<int>(pf_image::ValidationError::invalid_payload_length),
        static_cast<int>(zero_validator.error()));

    auto too_long = make_compressed_file(0U, "photo.pfr1", raw_payload);
    write_u32(too_long, 16U, 176001U);
    write_u32(too_long, 28U, pf_image::crc32(too_long.data(), 24U));
    Pfr1Validator too_long_validator(nullptr, nullptr, &buffers);
    TEST_ASSERT_FALSE(
        too_long_validator.feed(too_long.data(), pf_image::kPfr1HeaderSize));
    TEST_ASSERT_EQUAL(
        static_cast<int>(pf_image::ValidationError::invalid_payload_length),
        static_cast<int>(too_long_validator.error()));
}

void test_compressed_payload_rejects_corrupt_deflate_stream()
{
    const std::vector<std::uint8_t> raw_payload(176000U, 0x11U);
    auto file = make_compressed_file(0U, "photo.pfr1", raw_payload);
    const std::size_t payload_offset =
        pf_image::kPfr1HeaderSize + std::string("photo.pfr1").size();
    const std::size_t payload_length = file.size() - payload_offset;
    // Flip a bit inside the compressed stream (not just the trailing byte)
    // so the deflate bitstream itself becomes invalid, then recompute both
    // CRCs so the corruption is only caught by the inflate step, not CRC.
    file[payload_offset + 2U] ^= 0xFFU;
    refresh_crcs(file, payload_offset, payload_length);

    InflateFixture fixture{};
    const auto buffers = fixture.buffers();
    Pfr1Validator validator(nullptr, nullptr, &buffers);
    TEST_ASSERT_TRUE(validator.feed(file.data(), file.size()));
    TEST_ASSERT_FALSE(validator.finish());
    TEST_ASSERT_EQUAL(
        static_cast<int>(pf_image::ValidationError::payload_inflate_failed),
        static_cast<int>(validator.error()));
}

void test_compressed_payload_rejects_trailing_garbage_after_valid_stream()
{
    const std::vector<std::uint8_t> raw_payload(176000U, 0x11U);
    auto file = make_compressed_file(0U, "photo.pfr1", raw_payload);
    const std::string filename = "photo.pfr1";
    const std::size_t payload_offset =
        pf_image::kPfr1HeaderSize + filename.size();
    const std::size_t compressed_length = file.size() - payload_offset;

    // Append bytes after the otherwise-complete, valid deflate stream and
    // declare them as part of the payload (grow payload_length to match),
    // recomputing both CRCs so only the "was every payload byte consumed
    // by the deflate stream" check can catch this.
    file.insert(file.end(), {0xDEU, 0xADU, 0xBEU, 0xEFU});
    const std::size_t padded_length = compressed_length + 4U;
    write_u32(file, 16U, static_cast<std::uint32_t>(padded_length));
    refresh_crcs(file, payload_offset, padded_length);

    InflateFixture fixture{};
    const auto buffers = fixture.buffers();
    Pfr1Validator validator(nullptr, nullptr, &buffers);
    TEST_ASSERT_TRUE(validator.feed(file.data(), file.size()));
    TEST_ASSERT_FALSE(validator.finish());
    TEST_ASSERT_EQUAL(
        static_cast<int>(pf_image::ValidationError::payload_inflate_failed),
        static_cast<int>(validator.error()));
}

void test_cross_language_golden_compressed_vector_matches_documented_crcs()
{
    // Matches the "Cross-language golden vector (compressed)" section of
    // docs/formats/PFR1.md: 800x440 landscape, all-white raw payload
    // (176,000 bytes of 0x11), raw-DEFLATE compressed with zlib level 9.
    static constexpr std::array<std::uint8_t, 188U> kCompressedPayload{
        0xedU, 0xc1U, 0x81U, 0x00U, 0x00U, 0x00U, 0x00U, 0xc3U, 0x20U, 0x86U,
        0xf9U, 0xcbU, 0x5eU, 0xe1U, 0x00U, 0x55U, 0x01U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0xc0U, 0x6fU};
    const std::string filename = "golden-compressed.pfr1";
    std::vector<std::uint8_t> file(
        pf_image::kPfr1HeaderSize + filename.size() + kCompressedPayload.size(),
        0U);
    file[0] = 'P';
    file[1] = 'F';
    file[2] = 'R';
    file[3] = '1';
    file[4] = 1U;
    file[5] = static_cast<std::uint8_t>(pf_image::kPfr1HeaderSize);
    write_u16(file, 6U, pf_image::kCompressed);
    write_u16(file, 8U, 800U);
    write_u16(file, 10U, 440U);
    file[12] = 0U;
    file[13] = 1U;
    file[14] = static_cast<std::uint8_t>(pf_image::Dithering::nearest);
    file[15] = 0U;
    write_u32(file, 16U, static_cast<std::uint32_t>(kCompressedPayload.size()));
    write_u16(file, 20U, static_cast<std::uint16_t>(filename.size()));
    write_u16(file, 22U, 0U);
    for (std::size_t index = 0U; index < filename.size(); ++index) {
        file[pf_image::kPfr1HeaderSize + index] =
            static_cast<std::uint8_t>(filename[index]);
    }
    const std::size_t payload_offset =
        pf_image::kPfr1HeaderSize + filename.size();
    for (std::size_t index = 0U; index < kCompressedPayload.size(); ++index) {
        file[payload_offset + index] = kCompressedPayload[index];
    }
    refresh_crcs(file, payload_offset, kCompressedPayload.size());
    TEST_ASSERT_EQUAL_HEX32(0xBFA93827U, pf_image::read_u32(file.data() + 24U));
    TEST_ASSERT_EQUAL_HEX32(0xBD56BA9AU, pf_image::read_u32(file.data() + 28U));

    InflateFixture fixture{};
    const auto buffers = fixture.buffers();
    SinkState sink{};
    Pfr1Validator validator(collect_payload, &sink, &buffers);
    TEST_ASSERT_TRUE(validator.feed(file.data(), file.size()));
    TEST_ASSERT_TRUE(validator.finish());
    TEST_ASSERT_EQUAL_UINT32(176000U, sink.bytes.size());
    TEST_ASSERT_EQUAL_UINT8(0x11U, sink.bytes.front());
    TEST_ASSERT_EQUAL_UINT8(0x11U, sink.bytes.back());
}

}  // namespace

void setup()
{
}

void loop()
{
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_crc32_known_vector);
    RUN_TEST(test_valid_file_accepts_arbitrary_chunks_and_sinks_payload);
    RUN_TEST(test_portrait_dimensions_and_filename_are_preserved);
    RUN_TEST(test_rejects_bad_header_and_reserved_fields);
    RUN_TEST(test_rejects_invalid_palette_filename_and_crc);
    RUN_TEST(test_rejects_trailing_and_truncated_files);
    RUN_TEST(test_cross_language_golden_vector_matches_documented_crcs);
    RUN_TEST(test_compressed_payload_round_trips_through_streaming_validator);
    RUN_TEST(test_compressed_payload_rejects_when_scratch_buffer_missing);
    RUN_TEST(test_compressed_payload_length_bounds_are_enforced);
    RUN_TEST(test_compressed_payload_rejects_corrupt_deflate_stream);
    RUN_TEST(test_compressed_payload_rejects_trailing_garbage_after_valid_stream);
    RUN_TEST(test_cross_language_golden_compressed_vector_matches_documented_crcs);
    return UNITY_END();
}
