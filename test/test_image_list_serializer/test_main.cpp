#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <unity.h>

#include "pf_web/image_list_serializer.hpp"

extern "C" void setUp() {}
extern "C" void tearDown() {}

namespace {

void test_image_entry_serializes_metadata_and_flags()
{
    pf_storage::CatalogEntry entry{};
    entry.id = 7U;
    entry.created_at_epoch_s = 123456789ULL;
    entry.file_bytes = 182500U;
    entry.payload_bytes = 182400U;
    entry.width = 480U;
    entry.height = 760U;
    entry.orientation = pf_image::Orientation::portrait;
    entry.flags = pf_storage::kCatalogCurrent | pf_storage::kCatalogCorrupt;
    entry.order = 3U;
    const char name[] = "wall\\quote\".pfr1";
    entry.name_length = static_cast<std::uint16_t>(std::strlen(name));
    std::memcpy(entry.name, name, entry.name_length + 1U);

    char output[512]{};
    std::size_t written = 0U;
    TEST_ASSERT_TRUE(pf_web::serialize_image_entry(
        entry,
        output,
        sizeof(output),
        written));
    TEST_ASSERT_EQUAL_STRING(
        "{\"id\":7,\"name\":\"wall\\\\quote\\\".pfr1\","
        "\"created_at_epoch_s\":123456789,\"file_bytes\":182500,"
        "\"payload_bytes\":182400,\"width\":480,\"height\":760,"
        "\"orientation\":\"portrait\",\"enabled\":false,"
        "\"current\":true,\"corrupt\":true,\"order\":3}",
        output);
    TEST_ASSERT_EQUAL_UINT(std::strlen(output), written);

    char disposition[256]{};
    written = 0U;
    TEST_ASSERT_TRUE(pf_web::serialize_image_content_disposition(
        entry,
        disposition,
        sizeof(disposition),
        written));
    TEST_ASSERT_EQUAL_STRING(
        "attachment; filename=\"wall\\\\quote\\\".pfr1\"",
        disposition);
    TEST_ASSERT_EQUAL_UINT(std::strlen(disposition), written);

    pf_storage::CatalogEntry quote_heavy{};
    quote_heavy.name_length =
        static_cast<std::uint16_t>(pf_image::kPfr1MaxFilenameBytes);
    for (std::size_t index = 0U;
         index < quote_heavy.name_length;
         ++index) {
        quote_heavy.name[index] = index % 2U == 0U ? '"' : '\\';
    }
    TEST_ASSERT_TRUE(pf_web::serialize_image_content_disposition(
        quote_heavy,
        disposition,
        sizeof(disposition),
        written));
    TEST_ASSERT_GREATER_THAN_UINT(
        pf_image::kPfr1MaxFilenameBytes,
        written);
    char too_small[128]{};
    TEST_ASSERT_FALSE(pf_web::serialize_image_content_disposition(
        quote_heavy,
        too_small,
        sizeof(too_small),
        written));
}

void test_image_entry_rejects_invalid_output_arguments()
{
    pf_storage::CatalogEntry entry{};
    entry.name_length = 1U;
    entry.name[0] = 'x';
    char output[8]{};
    std::size_t written = 99U;
    TEST_ASSERT_FALSE(pf_web::serialize_image_entry(
        entry,
        output,
        sizeof(output),
        written));
    TEST_ASSERT_EQUAL_UINT(0U, written);
    TEST_ASSERT_FALSE(pf_web::serialize_image_entry(
        entry,
        nullptr,
        0U,
        written));
}

void test_image_list_envelope_closes_the_root_object()
{
    char envelope[sizeof(pf_web::kImageListJsonPrefix) +
                  sizeof(pf_web::kImageListJsonSuffix) - 1U]{};
    const int written = std::snprintf(
        envelope,
        sizeof(envelope),
        "%s%s",
        pf_web::kImageListJsonPrefix,
        pf_web::kImageListJsonSuffix);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(sizeof(envelope) - 1U),
        written);
    TEST_ASSERT_EQUAL_STRING(
        "{\"ok\":true,\"data\":{\"images\":[]}}",
        envelope);
}

}  // namespace

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_image_entry_serializes_metadata_and_flags);
    RUN_TEST(test_image_entry_rejects_invalid_output_arguments);
    RUN_TEST(test_image_list_envelope_closes_the_root_object);
    return UNITY_END();
}
