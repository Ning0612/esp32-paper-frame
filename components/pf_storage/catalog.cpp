#include "pf_storage/catalog.hpp"

#include <cstring>

namespace pf_storage {
namespace {

constexpr std::uint8_t kCatalogVersion = 1U;
constexpr std::uint8_t kCatalogHeaderSizeByte =
    static_cast<std::uint8_t>(kCatalogHeaderSize);
constexpr std::uint16_t kKnownEntryFlags =
    static_cast<std::uint16_t>(
        kCatalogEnabled | kCatalogCurrent | kCatalogCorrupt);

void write_u16(std::uint8_t* const destination, const std::uint16_t value)
{
    destination[0] = static_cast<std::uint8_t>(value & 0xFFU);
    destination[1] = static_cast<std::uint8_t>(value >> 8U);
}

void write_u32(std::uint8_t* const destination, const std::uint32_t value)
{
    for (std::uint8_t index = 0U; index < 4U; ++index) {
        destination[index] = static_cast<std::uint8_t>(value >> (index * 8U));
    }
}

void write_u64(std::uint8_t* const destination, const std::uint64_t value)
{
    for (std::uint8_t index = 0U; index < 8U; ++index) {
        destination[index] = static_cast<std::uint8_t>(value >> (index * 8U));
    }
}

std::uint64_t read_u64(const std::uint8_t* const source)
{
    std::uint64_t value = 0U;
    for (std::uint8_t index = 0U; index < 8U; ++index) {
        value |= static_cast<std::uint64_t>(source[index]) << (index * 8U);
    }
    return value;
}

std::size_t serialized_size(const Catalog& catalog)
{
    return kCatalogHeaderSize +
           static_cast<std::size_t>(catalog.count) * kCatalogEntrySize;
}

bool same_name(
    const CatalogEntry& left,
    const CatalogEntry& right)
{
    return left.name_length == right.name_length &&
           std::memcmp(left.name, right.name, left.name_length) == 0;
}

void normalize_orders(Catalog& catalog)
{
    for (std::uint16_t index = 0U; index < catalog.count; ++index) {
        catalog.entries[index].order = index;
    }
}

bool find_index(
    const Catalog& catalog,
    const std::uint32_t id,
    std::size_t& index)
{
    for (std::size_t candidate = 0U; candidate < catalog.count; ++candidate) {
        if (catalog.entries[candidate].id == id) {
            index = candidate;
            return true;
        }
    }
    return false;
}

}  // namespace

const char* to_string(const CatalogError error)
{
    switch (error) {
        case CatalogError::none:
            return "none";
        case CatalogError::invalid_argument:
            return "invalid_argument";
        case CatalogError::output_too_small:
            return "output_too_small";
        case CatalogError::invalid_magic:
            return "invalid_magic";
        case CatalogError::unsupported_version:
            return "unsupported_version";
        case CatalogError::invalid_header_size:
            return "invalid_header_size";
        case CatalogError::invalid_entry_size:
            return "invalid_entry_size";
        case CatalogError::invalid_flags:
            return "invalid_flags";
        case CatalogError::invalid_count:
            return "invalid_count";
        case CatalogError::invalid_payload_length:
            return "invalid_payload_length";
        case CatalogError::header_crc_mismatch:
            return "header_crc_mismatch";
        case CatalogError::payload_crc_mismatch:
            return "payload_crc_mismatch";
        case CatalogError::invalid_entry:
            return "invalid_entry";
        case CatalogError::duplicate_id:
            return "duplicate_id";
        case CatalogError::duplicate_name:
            return "duplicate_name";
        case CatalogError::invalid_current:
            return "invalid_current";
        case CatalogError::trailing_data:
            return "trailing_data";
    }
    return "invalid_entry";
}

bool validate_catalog_entry(
    const CatalogEntry& entry,
    CatalogError& error)
{
    error = CatalogError::none;
    if (entry.id == 0U ||
        entry.name_length == 0U ||
        entry.name_length >= kCatalogNameCapacity ||
        !pf_image::valid_filename(
            reinterpret_cast<const std::uint8_t*>(entry.name),
            entry.name_length) ||
        (entry.flags &
         static_cast<std::uint8_t>(~kKnownEntryFlags)) != 0U ||
        !pf_image::valid_dimensions(
            entry.width,
            entry.height,
            static_cast<std::uint8_t>(entry.orientation)) ||
        entry.payload_bytes !=
            pf_image::expected_payload_length(entry.width, entry.height) ||
        entry.file_bytes !=
            pf_image::kPfr1HeaderSize + entry.name_length +
                entry.payload_bytes) {
        error = CatalogError::invalid_entry;
        return false;
    }
    if ((entry.flags & kCatalogCurrent) != 0U &&
        ((entry.flags & kCatalogEnabled) == 0U ||
         (entry.flags & kCatalogCorrupt) != 0U)) {
        error = CatalogError::invalid_current;
        return false;
    }
    return true;
}

bool validate_catalog(
    const Catalog& catalog,
    CatalogError& error)
{
    error = CatalogError::none;
    if (catalog.count > kCatalogMaxEntries || catalog.next_id == 0U) {
        error = CatalogError::invalid_count;
        return false;
    }

    std::size_t current_count = 0U;
    for (std::size_t index = 0U; index < catalog.count; ++index) {
        const CatalogEntry& entry = catalog.entries[index];
        if (!validate_catalog_entry(entry, error)) {
            return false;
        }
        if (entry.order >= catalog.count) {
            error = CatalogError::invalid_entry;
            return false;
        }
        if ((entry.flags & kCatalogCurrent) != 0U) {
            ++current_count;
        }
        for (std::size_t previous = 0U; previous < index; ++previous) {
            if (catalog.entries[previous].id == entry.id) {
                error = CatalogError::duplicate_id;
                return false;
            }
            if (same_name(catalog.entries[previous], entry)) {
                error = CatalogError::duplicate_name;
                return false;
            }
            if (catalog.entries[previous].order == entry.order) {
                error = CatalogError::invalid_entry;
                return false;
            }
        }
        if (entry.id == kCatalogExhaustedNextId ||
            (entry.id >= catalog.next_id &&
             catalog.next_id != kCatalogExhaustedNextId)) {
            error = CatalogError::invalid_entry;
            return false;
        }
    }
    if (current_count > 1U) {
        error = CatalogError::invalid_current;
        return false;
    }
    return true;
}

bool initialize_catalog(Catalog& catalog)
{
    catalog = Catalog{};
    return true;
}

const CatalogEntry* find_catalog_entry(
    const Catalog& catalog,
    const std::uint32_t id)
{
    std::size_t index = 0U;
    return find_index(catalog, id, index) ? &catalog.entries[index] : nullptr;
}

CatalogEntry* find_catalog_entry(
    Catalog& catalog,
    const std::uint32_t id)
{
    std::size_t index = 0U;
    return find_index(catalog, id, index) ? &catalog.entries[index] : nullptr;
}

const CatalogEntry* find_catalog_entry_by_name(
    const Catalog& catalog,
    const char* const name,
    const std::size_t length)
{
    if (name == nullptr || length == 0U) {
        return nullptr;
    }
    for (std::size_t index = 0U; index < catalog.count; ++index) {
        if (catalog.entries[index].name_length == length &&
            std::memcmp(catalog.entries[index].name, name, length) == 0) {
            return &catalog.entries[index];
        }
    }
    return nullptr;
}

bool add_catalog_entry(
    Catalog& catalog,
    CatalogEntry entry,
    std::uint32_t& assigned_id,
    CatalogError& error)
{
    assigned_id = 0U;
    if (catalog.count >= kCatalogMaxEntries) {
        error = CatalogError::invalid_count;
        return false;
    }
    if (entry.id == 0U) {
        if (catalog.next_id == 0U ||
            catalog.next_id == kCatalogExhaustedNextId) {
            error = CatalogError::invalid_count;
            return false;
        }
        entry.id = catalog.next_id;
    }
    if (find_catalog_entry(catalog, entry.id) != nullptr) {
        error = CatalogError::duplicate_id;
        return false;
    }
    if (find_catalog_entry_by_name(
            catalog,
            entry.name,
            entry.name_length) != nullptr) {
        error = CatalogError::duplicate_name;
        return false;
    }
    entry.order = catalog.count;
    if (!validate_catalog_entry(entry, error)) {
        return false;
    }
    catalog.entries[catalog.count] = entry;
    ++catalog.count;
    if (catalog.next_id <= entry.id && entry.id != UINT32_MAX) {
        catalog.next_id = entry.id + 1U;
    }
    assigned_id = entry.id;
    ++catalog.generation;
    return validate_catalog(catalog, error);
}

bool remove_catalog_entry(
    Catalog& catalog,
    const std::uint32_t id,
    CatalogError& error)
{
    std::size_t index = 0U;
    if (!find_index(catalog, id, index)) {
        error = CatalogError::invalid_argument;
        return false;
    }
    for (std::size_t move = index + 1U; move < catalog.count; ++move) {
        catalog.entries[move - 1U] = catalog.entries[move];
    }
    --catalog.count;
    catalog.entries[catalog.count] = CatalogEntry{};
    normalize_orders(catalog);
    ++catalog.generation;
    return validate_catalog(catalog, error);
}

bool set_catalog_enabled(
    Catalog& catalog,
    const std::uint32_t id,
    const bool enabled,
    CatalogError& error)
{
    CatalogEntry* const entry = find_catalog_entry(catalog, id);
    if (entry == nullptr) {
        error = CatalogError::invalid_argument;
        return false;
    }
    if (!enabled) {
        entry->flags = static_cast<std::uint8_t>(
            entry->flags & static_cast<std::uint8_t>(~kCatalogEnabled));
        if ((entry->flags & kCatalogCurrent) != 0U) {
            entry->flags = static_cast<std::uint8_t>(
                entry->flags & static_cast<std::uint8_t>(~kCatalogCurrent));
        }
    } else {
        entry->flags = static_cast<std::uint8_t>(
            entry->flags | kCatalogEnabled);
    }
    ++catalog.generation;
    return validate_catalog(catalog, error);
}

bool set_catalog_current(
    Catalog& catalog,
    const std::uint32_t id,
    CatalogError& error)
{
    CatalogEntry* const selected = find_catalog_entry(catalog, id);
    if (selected == nullptr ||
        (selected->flags & kCatalogEnabled) == 0U ||
        (selected->flags & kCatalogCorrupt) != 0U) {
        error = CatalogError::invalid_current;
        return false;
    }
    for (std::size_t index = 0U; index < catalog.count; ++index) {
        catalog.entries[index].flags = static_cast<std::uint8_t>(
            catalog.entries[index].flags &
            static_cast<std::uint8_t>(~kCatalogCurrent));
    }
    selected->flags = static_cast<std::uint8_t>(
        selected->flags | kCatalogCurrent);
    ++catalog.generation;
    return validate_catalog(catalog, error);
}

bool reorder_catalog(
    Catalog& catalog,
    const std::uint32_t* const ids,
    const std::size_t count,
    CatalogError& error)
{
    if (count != catalog.count || (ids == nullptr && count != 0U)) {
        error = CatalogError::invalid_argument;
        return false;
    }
    Catalog reordered{};
    reordered.generation = catalog.generation;
    reordered.next_id = catalog.next_id;
    reordered.count = catalog.count;
    for (std::size_t order = 0U; order < count; ++order) {
        const CatalogEntry* const entry =
            find_catalog_entry(catalog, ids[order]);
        if (entry == nullptr) {
            error = CatalogError::invalid_argument;
            return false;
        }
        for (std::size_t previous = 0U; previous < order; ++previous) {
            if (reordered.entries[previous].id == entry->id) {
                error = CatalogError::duplicate_id;
                return false;
            }
        }
        reordered.entries[order] = *entry;
        reordered.entries[order].order = static_cast<std::uint16_t>(order);
    }
    ++reordered.generation;
    catalog = reordered;
    return validate_catalog(catalog, error);
}

bool serialize_catalog(
    const Catalog& catalog,
    std::uint8_t* const output,
    const std::size_t capacity,
    std::size_t& written,
    CatalogError& error)
{
    written = 0U;
    if (output == nullptr) {
        error = CatalogError::invalid_argument;
        return false;
    }
    if (!validate_catalog(catalog, error)) {
        return false;
    }
    const std::size_t required = serialized_size(catalog);
    if (capacity < required) {
        error = CatalogError::output_too_small;
        return false;
    }
    std::memset(output, 0, required);
    output[0] = 'P';
    output[1] = 'F';
    output[2] = 'C';
    output[3] = '1';
    output[4] = kCatalogVersion;
    output[5] = kCatalogHeaderSizeByte;
    write_u32(output + 8U, catalog.generation);
    write_u32(output + 12U, catalog.next_id);
    write_u16(output + 16U, catalog.count);
    write_u16(output + 18U, static_cast<std::uint16_t>(kCatalogEntrySize));
    write_u32(
        output + 20U,
        static_cast<std::uint32_t>(
            static_cast<std::size_t>(catalog.count) * kCatalogEntrySize));
    const std::size_t payload_offset = kCatalogHeaderSize;
    for (std::size_t index = 0U; index < catalog.count; ++index) {
        const CatalogEntry& entry = catalog.entries[index];
        std::uint8_t* const target =
            output + payload_offset + index * kCatalogEntrySize;
        write_u32(target, entry.id);
        write_u64(target + 4U, entry.created_at_epoch_s);
        write_u32(target + 12U, entry.file_bytes);
        write_u32(target + 16U, entry.payload_bytes);
        write_u16(target + 20U, entry.width);
        write_u16(target + 22U, entry.height);
        target[24] = static_cast<std::uint8_t>(entry.orientation);
        target[25] = entry.flags;
        write_u16(target + 26U, entry.order);
        write_u16(target + 28U, entry.name_length);
        std::memcpy(target + 32U, entry.name, entry.name_length);
    }
    write_u32(
        output + 24U,
        pf_image::crc32(
            output + payload_offset,
            required - payload_offset));
    write_u32(output + 28U, pf_image::crc32(output, 24U));
    written = required;
    error = CatalogError::none;
    return true;
}

CatalogParseResult parse_catalog(
    const std::uint8_t* const input,
    const std::size_t length,
    Catalog& destination)
{
    CatalogParseResult result{};
    if (input == nullptr || length < kCatalogHeaderSize) {
        result.error = CatalogError::invalid_argument;
        return result;
    }
    if (input[0] != 'P' || input[1] != 'F' ||
        input[2] != 'C' || input[3] != '1') {
        result.error = CatalogError::invalid_magic;
        return result;
    }
    if (input[4] != kCatalogVersion) {
        result.error = CatalogError::unsupported_version;
        return result;
    }
    if (input[5] != kCatalogHeaderSizeByte) {
        result.error = CatalogError::invalid_header_size;
        return result;
    }
    if (pf_image::read_u16(input + 6U) != 0U) {
        result.error = CatalogError::invalid_flags;
        return result;
    }
    if (pf_image::crc32(input, 24U) != pf_image::read_u32(input + 28U)) {
        result.error = CatalogError::header_crc_mismatch;
        return result;
    }
    const std::uint16_t count = pf_image::read_u16(input + 16U);
    const std::uint16_t entry_size = pf_image::read_u16(input + 18U);
    if (entry_size != kCatalogEntrySize) {
        result.error = CatalogError::invalid_entry_size;
        return result;
    }
    if (count > kCatalogMaxEntries) {
        result.error = CatalogError::invalid_count;
        return result;
    }
    const std::uint32_t payload_length = pf_image::read_u32(input + 20U);
    if (payload_length !=
        static_cast<std::uint32_t>(
            static_cast<std::size_t>(count) * kCatalogEntrySize)) {
        result.error = CatalogError::invalid_payload_length;
        return result;
    }
    const std::size_t expected_length =
        kCatalogHeaderSize + static_cast<std::size_t>(payload_length);
    if (length < expected_length) {
        result.error = CatalogError::invalid_payload_length;
        return result;
    }
    if (length != expected_length) {
        result.error = CatalogError::trailing_data;
        return result;
    }
    if (pf_image::crc32(input + kCatalogHeaderSize, payload_length) !=
        pf_image::read_u32(input + 24U)) {
        result.error = CatalogError::payload_crc_mismatch;
        return result;
    }

    Catalog parsed_catalog{};
    parsed_catalog.generation = pf_image::read_u32(input + 8U);
    parsed_catalog.next_id = pf_image::read_u32(input + 12U);
    parsed_catalog.count = count;
    for (std::size_t index = 0U; index < count; ++index) {
        const std::uint8_t* const source =
            input + kCatalogHeaderSize + index * kCatalogEntrySize;
        CatalogEntry& entry = parsed_catalog.entries[index];
        entry.id = pf_image::read_u32(source);
        entry.created_at_epoch_s = read_u64(source + 4U);
        entry.file_bytes = pf_image::read_u32(source + 12U);
        entry.payload_bytes = pf_image::read_u32(source + 16U);
        entry.width = pf_image::read_u16(source + 20U);
        entry.height = pf_image::read_u16(source + 22U);
        entry.orientation = static_cast<pf_image::Orientation>(source[24]);
        entry.flags = source[25];
        entry.order = pf_image::read_u16(source + 26U);
        entry.name_length = pf_image::read_u16(source + 28U);
        if (entry.name_length >= kCatalogNameCapacity) {
            result.error = CatalogError::invalid_entry;
            return result;
        }
        if (source[30] != 0U || source[31] != 0U) {
            result.error = CatalogError::invalid_entry;
            return result;
        }
        for (std::size_t padding = 32U + entry.name_length;
             padding < kCatalogEntrySize;
             ++padding) {
            if (source[padding] != 0U) {
                result.error = CatalogError::invalid_entry;
                return result;
            }
        }
        std::memcpy(entry.name, source + 32U, entry.name_length);
        entry.name[entry.name_length] = '\0';
    }
    if (!validate_catalog(parsed_catalog, result.error)) {
        return result;
    }
    destination = parsed_catalog;
    result.bytes_consumed = expected_length;
    return result;
}

}  // namespace pf_storage
