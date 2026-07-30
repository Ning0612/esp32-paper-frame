#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <unity.h>

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
    return UNITY_END();
}
