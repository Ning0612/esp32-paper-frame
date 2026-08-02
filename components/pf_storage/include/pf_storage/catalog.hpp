#pragma once

#include <cstddef>
#include <cstdint>

#include "pf_image/pfr1.hpp"

namespace pf_storage {

inline constexpr std::size_t kCatalogHeaderSize = 32U;
inline constexpr std::size_t kCatalogEntrySize = 128U;
inline constexpr std::size_t kCatalogNameCapacity =
    pf_image::kPfr1MaxFilenameBytes + 1U;
inline constexpr std::size_t kCatalogMaxEntries = 64U;
inline constexpr std::size_t kCatalogMaxBytes =
    kCatalogHeaderSize + kCatalogEntrySize * kCatalogMaxEntries;
// UINT32_MAX is a durable exhaustion marker: no automatic id can be assigned
// once next_id reaches this value, while all stored entry ids remain below it.
inline constexpr std::uint32_t kCatalogExhaustedNextId = UINT32_MAX;

enum CatalogEntryFlags : std::uint8_t {
    kCatalogEnabled = 0x01U,
    kCatalogCurrent = 0x02U,
    kCatalogCorrupt = 0x04U,
};

enum class CatalogError : std::uint8_t {
    none = 0U,
    invalid_argument,
    output_too_small,
    invalid_magic,
    unsupported_version,
    invalid_header_size,
    invalid_entry_size,
    invalid_flags,
    invalid_count,
    invalid_payload_length,
    header_crc_mismatch,
    payload_crc_mismatch,
    invalid_entry,
    duplicate_id,
    duplicate_name,
    invalid_current,
    trailing_data,
};

struct CatalogEntry {
    std::uint32_t id = 0U;
    std::uint64_t created_at_epoch_s = 0U;
    std::uint32_t file_bytes = 0U;
    std::uint32_t payload_bytes = 0U;
    std::uint16_t width = 0U;
    std::uint16_t height = 0U;
    pf_image::Orientation orientation = pf_image::Orientation::landscape;
    std::uint8_t flags = kCatalogEnabled;
    std::uint16_t order = 0U;
    std::uint16_t name_length = 0U;
    char name[kCatalogNameCapacity]{};
};

struct Catalog {
    std::uint32_t generation = 0U;
    std::uint32_t next_id = 1U;
    std::uint16_t count = 0U;
    CatalogEntry entries[kCatalogMaxEntries]{};
};

struct CatalogParseResult {
    CatalogError error = CatalogError::none;
    std::size_t bytes_consumed = 0U;

    bool ok() const
    {
        return error == CatalogError::none;
    }
};

const char* to_string(CatalogError error);

bool validate_catalog_entry(const CatalogEntry& entry, CatalogError& error);
bool validate_catalog(const Catalog& catalog, CatalogError& error);

bool initialize_catalog(Catalog& catalog);

const CatalogEntry* find_catalog_entry(
    const Catalog& catalog,
    std::uint32_t id);
CatalogEntry* find_catalog_entry(Catalog& catalog, std::uint32_t id);

const CatalogEntry* find_catalog_entry_by_name(
    const Catalog& catalog,
    const char* name,
    std::size_t length);

bool add_catalog_entry(
    Catalog& catalog,
    CatalogEntry entry,
    std::uint32_t& assigned_id,
    CatalogError& error);
// Removing an entry only mutates catalog metadata. A higher-level transaction
// must choose a valid successor and update RuntimeCoordinator before commit.
bool remove_catalog_entry(
    Catalog& catalog,
    std::uint32_t id,
    CatalogError& error);
bool set_catalog_enabled(
    Catalog& catalog,
    std::uint32_t id,
    bool enabled,
    CatalogError& error);
bool set_catalog_current(
    Catalog& catalog,
    std::uint32_t id,
    CatalogError& error);
bool reorder_catalog(
    Catalog& catalog,
    const std::uint32_t* ids,
    std::size_t count,
    CatalogError& error);

bool serialize_catalog(
    const Catalog& catalog,
    std::uint8_t* output,
    std::size_t capacity,
    std::size_t& written,
    CatalogError& error);

CatalogParseResult parse_catalog(
    const std::uint8_t* input,
    std::size_t length,
    Catalog& destination);

}  // namespace pf_storage
