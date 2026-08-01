#include <cstdint>
#include <cstring>

#include <unity.h>

#include "pf_storage/catalog.hpp"

extern "C" void setUp() {}
extern "C" void tearDown() {}

namespace {

void write_u32(std::uint8_t* const destination, const std::uint32_t value)
{
    for (std::uint8_t index = 0U; index < 4U; ++index) {
        destination[index] = static_cast<std::uint8_t>(value >> (index * 8U));
    }
}

pf_storage::CatalogEntry make_entry(
    const char* const name,
    const pf_image::Orientation orientation =
        pf_image::Orientation::landscape)
{
    pf_storage::CatalogEntry entry{};
    entry.created_at_epoch_s = 1720000000U;
    entry.orientation = orientation;
    entry.width = orientation == pf_image::Orientation::landscape
                      ? pf_display::kPanelWidth
                      : pf_display::kPortraitImageWidth;
    entry.height = orientation == pf_image::Orientation::landscape
                       ? pf_display::kLandscapeImageHeight
                       : pf_display::kPortraitImageHeight;
    entry.name_length = static_cast<std::uint16_t>(std::strlen(name));
    std::memcpy(entry.name, name, entry.name_length);
    entry.payload_bytes = pf_image::expected_payload_length(
        entry.width,
        entry.height);
    entry.file_bytes = static_cast<std::uint32_t>(
        pf_image::kPfr1HeaderSize + entry.name_length + entry.payload_bytes);
    return entry;
}

void test_catalog_adds_and_round_trips_entries()
{
    pf_storage::Catalog catalog{};
    TEST_ASSERT_TRUE(pf_storage::initialize_catalog(catalog));

    std::uint32_t first_id = 0U;
    pf_storage::CatalogError error = pf_storage::CatalogError::none;
    TEST_ASSERT_TRUE(pf_storage::add_catalog_entry(
        catalog,
        make_entry("welcome.pfr1"),
        first_id,
        error));
    TEST_ASSERT_EQUAL_UINT32(1U, first_id);

    std::uint32_t second_id = 0U;
    TEST_ASSERT_TRUE(pf_storage::add_catalog_entry(
        catalog,
        make_entry("portrait.pfr1", pf_image::Orientation::portrait),
        second_id,
        error));
    TEST_ASSERT_EQUAL_UINT32(2U, second_id);
    TEST_ASSERT_EQUAL_UINT16(2U, catalog.count);
    TEST_ASSERT_EQUAL_UINT16(0U, catalog.entries[0].order);
    TEST_ASSERT_EQUAL_UINT16(1U, catalog.entries[1].order);

    TEST_ASSERT_TRUE(pf_storage::set_catalog_current(
        catalog,
        second_id,
        error));
    TEST_ASSERT_EQUAL_UINT8(0U, catalog.entries[0].flags &
                                   pf_storage::kCatalogCurrent);
    TEST_ASSERT_NOT_EQUAL(0U, catalog.entries[1].flags &
                               pf_storage::kCatalogCurrent);

    std::uint8_t serialized[pf_storage::kCatalogMaxBytes]{};
    std::size_t written = 0U;
    TEST_ASSERT_TRUE(pf_storage::serialize_catalog(
        catalog,
        serialized,
        sizeof(serialized),
        written,
        error));
    TEST_ASSERT_EQUAL_UINT32(
        pf_storage::kCatalogHeaderSize +
            2U * pf_storage::kCatalogEntrySize,
        written);

    pf_storage::Catalog restored{};
    const pf_storage::CatalogParseResult parsed =
        pf_storage::parse_catalog(serialized, written, restored);
    TEST_ASSERT_TRUE(parsed.ok());
    TEST_ASSERT_EQUAL_UINT32(written, parsed.bytes_consumed);
    TEST_ASSERT_EQUAL_UINT32(catalog.generation, restored.generation);
    TEST_ASSERT_EQUAL_UINT32(catalog.next_id, restored.next_id);
    TEST_ASSERT_EQUAL_UINT16(catalog.count, restored.count);
    TEST_ASSERT_EQUAL_UINT32(second_id, restored.entries[1].id);
    TEST_ASSERT_EQUAL_STRING("portrait.pfr1", restored.entries[1].name);
}

void test_catalog_rejects_duplicates_and_unsafe_mutations()
{
    pf_storage::Catalog catalog{};
    TEST_ASSERT_TRUE(pf_storage::initialize_catalog(catalog));
    pf_storage::CatalogError error = pf_storage::CatalogError::none;
    std::uint32_t id = 0U;
    TEST_ASSERT_TRUE(pf_storage::add_catalog_entry(
        catalog,
        make_entry("one.pfr1"),
        id,
        error));

    std::uint32_t duplicate_id = 0U;
    pf_storage::CatalogEntry duplicate = make_entry("two.pfr1");
    duplicate.id = id;
    TEST_ASSERT_FALSE(pf_storage::add_catalog_entry(
        catalog,
        duplicate,
        duplicate_id,
        error));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_storage::CatalogError::duplicate_id),
        static_cast<int>(error));

    TEST_ASSERT_FALSE(pf_storage::add_catalog_entry(
        catalog,
        make_entry("one.pfr1"),
        duplicate_id,
        error));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_storage::CatalogError::duplicate_name),
        static_cast<int>(error));

    TEST_ASSERT_FALSE(pf_storage::set_catalog_current(
        catalog,
        999U,
        error));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_storage::CatalogError::invalid_current),
        static_cast<int>(error));

    TEST_ASSERT_TRUE(pf_storage::set_catalog_enabled(
        catalog,
        id,
        false,
        error));
    TEST_ASSERT_FALSE(pf_storage::set_catalog_current(
        catalog,
        id,
        error));

    catalog.entries[0].flags = static_cast<std::uint8_t>(
        catalog.entries[0].flags | pf_storage::kCatalogCurrent);
    TEST_ASSERT_FALSE(pf_storage::validate_catalog(catalog, error));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_storage::CatalogError::invalid_current),
        static_cast<int>(error));
    catalog.entries[0].flags = static_cast<std::uint8_t>(
        catalog.entries[0].flags &
        static_cast<std::uint8_t>(~pf_storage::kCatalogCurrent));
}

void test_catalog_detects_header_payload_and_trailing_corruption()
{
    pf_storage::Catalog catalog{};
    TEST_ASSERT_TRUE(pf_storage::initialize_catalog(catalog));
    pf_storage::CatalogError error = pf_storage::CatalogError::none;
    std::uint32_t id = 0U;
    TEST_ASSERT_TRUE(pf_storage::add_catalog_entry(
        catalog,
        make_entry("safe.pfr1"),
        id,
        error));
    std::uint8_t serialized[pf_storage::kCatalogMaxBytes]{};
    std::size_t written = 0U;
    TEST_ASSERT_TRUE(pf_storage::serialize_catalog(
        catalog,
        serialized,
        sizeof(serialized),
        written,
        error));

    pf_storage::Catalog parsed_catalog{};
    TEST_ASSERT_TRUE(pf_storage::parse_catalog(
        serialized,
        written,
        parsed_catalog)
                         .ok());
    const pf_storage::Catalog preserved_catalog = parsed_catalog;
    serialized[8] ^= 0x01U;
    pf_storage::CatalogParseResult parsed =
        pf_storage::parse_catalog(serialized, written, parsed_catalog);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_storage::CatalogError::header_crc_mismatch),
        static_cast<int>(parsed.error));
    TEST_ASSERT_EQUAL_MEMORY(
        &preserved_catalog,
        &parsed_catalog,
        sizeof(preserved_catalog));

    TEST_ASSERT_TRUE(pf_storage::serialize_catalog(
        catalog,
        serialized,
        sizeof(serialized),
        written,
        error));
    serialized[pf_storage::kCatalogHeaderSize + 32U] ^= 0x01U;
    parsed = pf_storage::parse_catalog(serialized, written, parsed_catalog);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_storage::CatalogError::payload_crc_mismatch),
        static_cast<int>(parsed.error));

    TEST_ASSERT_TRUE(pf_storage::serialize_catalog(
        catalog,
        serialized,
        sizeof(serialized),
        written,
        error));
    serialized[written] = 0xAAU;
    parsed = pf_storage::parse_catalog(serialized, written + 1U, parsed_catalog);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_storage::CatalogError::trailing_data),
        static_cast<int>(parsed.error));

    TEST_ASSERT_TRUE(pf_storage::serialize_catalog(
        catalog,
        serialized,
        sizeof(serialized),
        written,
        error));
    serialized[pf_storage::kCatalogHeaderSize + 32U + 9U] ^= 0x01U;
    write_u32(
        serialized + 24U,
        pf_image::crc32(
            serialized + pf_storage::kCatalogHeaderSize,
            written - pf_storage::kCatalogHeaderSize));
    write_u32(serialized + 28U, pf_image::crc32(serialized, 24U));
    parsed = pf_storage::parse_catalog(serialized, written, parsed_catalog);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_storage::CatalogError::invalid_entry),
        static_cast<int>(parsed.error));
}

void test_catalog_reorder_and_remove_keep_invariants()
{
    pf_storage::Catalog catalog{};
    TEST_ASSERT_TRUE(pf_storage::initialize_catalog(catalog));
    pf_storage::CatalogError error = pf_storage::CatalogError::none;
    std::uint32_t ids[3]{};
    TEST_ASSERT_TRUE(pf_storage::add_catalog_entry(
        catalog,
        make_entry("a.pfr1"),
        ids[0],
        error));
    TEST_ASSERT_TRUE(pf_storage::add_catalog_entry(
        catalog,
        make_entry("b.pfr1"),
        ids[1],
        error));
    TEST_ASSERT_TRUE(pf_storage::add_catalog_entry(
        catalog,
        make_entry("c.pfr1"),
        ids[2],
        error));

    const std::uint32_t order[] = {ids[2], ids[0], ids[1]};
    TEST_ASSERT_TRUE(pf_storage::reorder_catalog(
        catalog,
        order,
        3U,
        error));
    TEST_ASSERT_EQUAL_UINT32(ids[2], catalog.entries[0].id);
    TEST_ASSERT_EQUAL_UINT16(0U, catalog.entries[0].order);
    TEST_ASSERT_TRUE(pf_storage::remove_catalog_entry(
        catalog,
        ids[0],
        error));
    TEST_ASSERT_EQUAL_UINT16(2U, catalog.count);
    TEST_ASSERT_EQUAL_UINT16(0U, catalog.entries[0].order);
    TEST_ASSERT_EQUAL_UINT16(1U, catalog.entries[1].order);
    TEST_ASSERT_TRUE(pf_storage::validate_catalog(catalog, error));
}

void test_catalog_id_exhaustion_is_explicit_and_safe()
{
    pf_storage::Catalog catalog{};
    TEST_ASSERT_TRUE(pf_storage::initialize_catalog(catalog));
    catalog.next_id = pf_storage::kCatalogExhaustedNextId - 1U;
    pf_storage::CatalogError error = pf_storage::CatalogError::none;
    std::uint32_t assigned_id = 0U;
    TEST_ASSERT_TRUE(pf_storage::add_catalog_entry(
        catalog,
        make_entry("last.pfr1"),
        assigned_id,
        error));
    TEST_ASSERT_EQUAL_UINT32(
        pf_storage::kCatalogExhaustedNextId - 1U,
        assigned_id);
    TEST_ASSERT_EQUAL_UINT32(
        pf_storage::kCatalogExhaustedNextId,
        catalog.next_id);
    TEST_ASSERT_FALSE(pf_storage::add_catalog_entry(
        catalog,
        make_entry("overflow.pfr1"),
        assigned_id,
        error));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_storage::CatalogError::invalid_count),
        static_cast<int>(error));

    pf_storage::CatalogEntry invalid = make_entry("reserved-id.pfr1");
    invalid.id = pf_storage::kCatalogExhaustedNextId;
    TEST_ASSERT_FALSE(pf_storage::add_catalog_entry(
        catalog,
        invalid,
        assigned_id,
        error));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_storage::CatalogError::invalid_entry),
        static_cast<int>(error));
}

void test_catalog_accepts_smaller_payload_bytes_but_rejects_zero_or_oversized()
{
    // A compressed PFR1 payload is smaller than the profile's uncompressed
    // size; the catalog entry's payload_bytes/file_bytes reflect the
    // as-stored (compressed) size, not the fixed profile size.
    pf_storage::Catalog catalog{};
    TEST_ASSERT_TRUE(pf_storage::initialize_catalog(catalog));
    pf_storage::CatalogError error = pf_storage::CatalogError::none;

    pf_storage::CatalogEntry compressed = make_entry("compressed.pfr1");
    const std::uint32_t uncompressed_bytes = compressed.payload_bytes;
    compressed.payload_bytes = uncompressed_bytes - 1U;
    compressed.file_bytes = static_cast<std::uint32_t>(
        pf_image::kPfr1HeaderSize + compressed.name_length +
        compressed.payload_bytes);
    std::uint32_t assigned_id = 0U;
    TEST_ASSERT_TRUE(pf_storage::add_catalog_entry(
        catalog, compressed, assigned_id, error));

    // Upper bound is inclusive: an entry whose payload_bytes equals the
    // profile's uncompressed size (i.e. compression didn't shrink it) is
    // still valid.
    pf_storage::CatalogEntry uncompressed = make_entry("uncompressed.pfr1");
    TEST_ASSERT_EQUAL_UINT32(uncompressed_bytes, uncompressed.payload_bytes);
    TEST_ASSERT_TRUE(pf_storage::add_catalog_entry(
        catalog, uncompressed, assigned_id, error));

    pf_storage::CatalogEntry zero_payload = make_entry("zero.pfr1");
    zero_payload.payload_bytes = 0U;
    zero_payload.file_bytes = static_cast<std::uint32_t>(
        pf_image::kPfr1HeaderSize + zero_payload.name_length);
    TEST_ASSERT_FALSE(pf_storage::add_catalog_entry(
        catalog, zero_payload, assigned_id, error));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_storage::CatalogError::invalid_entry),
        static_cast<int>(error));

    pf_storage::CatalogEntry oversized = make_entry("oversized.pfr1");
    oversized.payload_bytes = uncompressed_bytes + 1U;
    oversized.file_bytes = static_cast<std::uint32_t>(
        pf_image::kPfr1HeaderSize + oversized.name_length +
        oversized.payload_bytes);
    TEST_ASSERT_FALSE(pf_storage::add_catalog_entry(
        catalog, oversized, assigned_id, error));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_storage::CatalogError::invalid_entry),
        static_cast<int>(error));
}

}  // namespace

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_catalog_adds_and_round_trips_entries);
    RUN_TEST(test_catalog_rejects_duplicates_and_unsafe_mutations);
    RUN_TEST(test_catalog_detects_header_payload_and_trailing_corruption);
    RUN_TEST(test_catalog_reorder_and_remove_keep_invariants);
    RUN_TEST(test_catalog_id_exhaustion_is_explicit_and_safe);
    RUN_TEST(test_catalog_accepts_smaller_payload_bytes_but_rejects_zero_or_oversized);
    return UNITY_END();
}
