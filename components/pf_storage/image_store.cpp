#include "pf_storage/image_store.hpp"

#include <cstring>

namespace pf_storage {
namespace {

constexpr char kUploadPartPath[] = "/images/.upload.part";
constexpr char kCatalogPath[] = "/images/.catalog.pfc1";
constexpr char kCatalogPartPath[] = "/images/.catalog.pfc1.part";
constexpr char kCatalogBakPath[] = "/images/.catalog.pfc1.bak";
constexpr char kCatalogMutationMarkerPath[] = "/images/.catalog.mutation";
constexpr std::size_t kPathCapacity = 160U;

bool catalogs_equal(const Catalog& left, const Catalog& right)
{
    if (left.generation != right.generation ||
        left.next_id != right.next_id || left.count != right.count) {
        return false;
    }
    for (std::size_t index = 0U; index < left.count; ++index) {
        const CatalogEntry& left_entry = left.entries[index];
        const CatalogEntry& right_entry = right.entries[index];
        if (left_entry.id != right_entry.id ||
            left_entry.created_at_epoch_s != right_entry.created_at_epoch_s ||
            left_entry.file_bytes != right_entry.file_bytes ||
            left_entry.payload_bytes != right_entry.payload_bytes ||
            left_entry.width != right_entry.width ||
            left_entry.height != right_entry.height ||
            left_entry.orientation != right_entry.orientation ||
            left_entry.flags != right_entry.flags ||
            left_entry.order != right_entry.order ||
            left_entry.name_length != right_entry.name_length ||
            std::memcmp(
                left_entry.name,
                right_entry.name,
                left_entry.name_length) != 0) {
            return false;
        }
    }
    return true;
}

bool make_image_path(
    char* const output,
    const std::size_t capacity,
    const char* const filename,
    const char* const suffix)
{
    if (output == nullptr || filename == nullptr || suffix == nullptr) {
        return false;
    }
    const std::size_t root_length = std::strlen(kImageStoreRootPath);
    const std::size_t filename_length = std::strlen(filename);
    const std::size_t suffix_length = std::strlen(suffix);
    const std::size_t required =
        root_length + 1U + filename_length + suffix_length + 1U;
    if (required > capacity) {
        return false;
    }
    std::memcpy(output, kImageStoreRootPath, root_length);
    output[root_length] = '/';
    std::memcpy(
        output + root_length + 1U,
        filename,
        filename_length);
    std::memcpy(
        output + root_length + 1U + filename_length,
        suffix,
        suffix_length);
    output[required - 1U] = '\0';
    return true;
}

ImageStoreResult failure(const ImageStoreError error)
{
    ImageStoreResult result{};
    result.error = error;
    return result;
}

void cleanup_path(StorageFileSystem& filesystem, const char* const path)
{
    (void)filesystem.remove_if_exists(path);
}

bool restore_catalog(
    StorageFileSystem& filesystem,
    const bool had_catalog)
{
    return !had_catalog ||
           filesystem.rename(kCatalogBakPath, kCatalogPath);
}

bool readback_catalog(
    StorageFileSystem& filesystem,
    const char* const path,
    std::uint8_t* const buffer,
    const std::size_t capacity,
    Catalog& parsed_catalog,
    CatalogError& catalog_error)
{
    StorageFileHandle handle{};
    if (!filesystem.open_read(path, handle)) {
        return false;
    }
    std::size_t total = 0U;
    bool read_ok = true;
    std::uint8_t extra_byte = 0U;
    while (true) {
        if (total == capacity) {
            // A catalog with the maximum number of entries is exactly
            // kCatalogMaxBytes. Probe one extra byte so that an exact-size
            // blob is accepted while a truncated caller buffer is rejected.
            std::size_t extra_amount = 0U;
            if (!filesystem.read(
                    handle,
                    &extra_byte,
                    sizeof(extra_byte),
                    extra_amount)) {
                read_ok = false;
                break;
            }
            if (extra_amount != 0U) {
                read_ok = false;
            }
            break;
        }
        std::size_t amount = 0U;
        if (!filesystem.read(
                handle,
                buffer + total,
                capacity - total,
                amount)) {
            read_ok = false;
            break;
        }
        if (amount == 0U) {
            break;
        }
        if (amount > capacity - total) {
            read_ok = false;
            break;
        }
        total += amount;
    }
    if (!filesystem.close_read(handle)) {
        read_ok = false;
    }
    if (!read_ok) {
        return false;
    }
    const CatalogParseResult parsed =
        parse_catalog(buffer, total, parsed_catalog);
    catalog_error = parsed.error;
    return parsed.ok();
}

}  // namespace

const char* to_string(const ImageStoreError error)
{
    switch (error) {
        case ImageStoreError::none:
            return "none";
        case ImageStoreError::invalid_argument:
            return "invalid_argument";
        case ImageStoreError::not_ready:
            return "not_ready";
        case ImageStoreError::busy:
            return "busy";
        case ImageStoreError::no_space:
            return "no_space";
        case ImageStoreError::stream_failed:
            return "stream_failed";
        case ImageStoreError::write_failed:
            return "write_failed";
        case ImageStoreError::close_failed:
            return "close_failed";
        case ImageStoreError::catalog_read_failed:
            return "catalog_read_failed";
        case ImageStoreError::invalid_image:
            return "invalid_image";
        case ImageStoreError::image_conflict:
            return "image_conflict";
        case ImageStoreError::catalog_invalid:
            return "catalog_invalid";
        case ImageStoreError::path_too_long:
            return "path_too_long";
        case ImageStoreError::remove_failed:
            return "remove_failed";
        case ImageStoreError::rename_failed:
            return "rename_failed";
        case ImageStoreError::rollback_failed:
            return "rollback_failed";
    }
    return "invalid_argument";
}

ImageStoreResult store_image_transactionally(
    StorageFileSystem& filesystem,
    const Catalog& current_catalog,
    Catalog& updated_catalog,
    std::uint8_t* const catalog_buffer,
    const std::size_t catalog_capacity,
    const StorageStreamReader& reader,
    const std::size_t content_length)
{
    ImageStoreResult result{};
    if (&current_catalog == &updated_catalog || reader.read == nullptr ||
        catalog_buffer == nullptr ||
        content_length == 0U || content_length > pf_image::kPfr1MaxFileBytes) {
        return failure(ImageStoreError::invalid_argument);
    }

    CatalogError catalog_error = CatalogError::none;
    if (!validate_catalog(current_catalog, catalog_error)) {
        result.error = ImageStoreError::catalog_invalid;
        result.catalog_error = catalog_error;
        return result;
    }
    const std::uint64_t required_bytes =
        static_cast<std::uint64_t>(content_length) + kCatalogMaxBytes;
    if (filesystem.free_bytes() < required_bytes) {
        return failure(ImageStoreError::no_space);
    }

    cleanup_path(filesystem, kUploadPartPath);
    StorageFileHandle handle{};
    if (!filesystem.open_write(kUploadPartPath, handle)) {
        return failure(ImageStoreError::write_failed);
    }
    pf_image::Pfr1Validator validator{};
    std::uint8_t buffer[kImageStoreChunkBytes]{};
    std::size_t received = 0U;
    while (true) {
        std::size_t amount = 0U;
        const StorageReadResult read_result = reader.read(
            reader.context,
            buffer,
            sizeof(buffer),
            amount);
        if (read_result == StorageReadResult::eof) {
            if (amount != 0U) {
                (void)filesystem.close_write(handle);
                cleanup_path(filesystem, kUploadPartPath);
                return failure(ImageStoreError::stream_failed);
            }
            break;
        }
        if (read_result != StorageReadResult::data || amount == 0U ||
            amount > sizeof(buffer) || amount > content_length ||
            received > content_length - amount) {
            (void)filesystem.close_write(handle);
            cleanup_path(filesystem, kUploadPartPath);
            result = failure(ImageStoreError::stream_failed);
            result.bytes_received = received;
            return result;
        }
        if (!validator.feed(buffer, amount)) {
            (void)filesystem.close_write(handle);
            cleanup_path(filesystem, kUploadPartPath);
            result = failure(ImageStoreError::invalid_image);
            result.validation_error = validator.error();
            result.bytes_received = received;
            return result;
        }
        if (!filesystem.write(handle, buffer, amount)) {
            (void)filesystem.close_write(handle);
            cleanup_path(filesystem, kUploadPartPath);
            result = failure(ImageStoreError::write_failed);
            result.bytes_received = received;
            return result;
        }
        received += amount;
    }
    result.bytes_received = received;
    if (received != content_length) {
        (void)filesystem.close_write(handle);
        cleanup_path(filesystem, kUploadPartPath);
        result.error = ImageStoreError::invalid_image;
        result.validation_error = pf_image::ValidationError::incomplete;
        return result;
    }
    if (!validator.finish()) {
        (void)filesystem.close_write(handle);
        cleanup_path(filesystem, kUploadPartPath);
        result.error = ImageStoreError::invalid_image;
        result.validation_error = validator.error();
        return result;
    }
    if (!filesystem.close_write(handle)) {
        cleanup_path(filesystem, kUploadPartPath);
        result.error = ImageStoreError::close_failed;
        return result;
    }

    const pf_image::Pfr1Header& header = validator.header();
    const std::size_t filename_length = header.filename_length;
    char filename[kCatalogNameCapacity]{};
    std::memcpy(filename, validator.filename(), filename_length);
    filename[filename_length] = '\0';

    char image_path[kPathCapacity]{};
    char image_part_path[kPathCapacity]{};
    if (!make_image_path(image_path, sizeof(image_path), filename, "") ||
        !make_image_path(
            image_part_path,
            sizeof(image_part_path),
            filename,
            ".part")) {
        cleanup_path(filesystem, kUploadPartPath);
        return failure(ImageStoreError::path_too_long);
    }
    if (find_catalog_entry_by_name(
            current_catalog,
            filename,
            filename_length) != nullptr ||
        filesystem.exists(image_path)) {
        cleanup_path(filesystem, kUploadPartPath);
        return failure(ImageStoreError::image_conflict);
    }

    Catalog candidate = current_catalog;
    CatalogEntry entry{};
    entry.created_at_epoch_s = 0U;
    entry.file_bytes = static_cast<std::uint32_t>(content_length);
    entry.payload_bytes = header.payload_length;
    entry.width = header.width;
    entry.height = header.height;
    entry.orientation = header.orientation;
    entry.flags = kCatalogEnabled;
    entry.name_length = static_cast<std::uint16_t>(filename_length);
    std::memcpy(entry.name, filename, filename_length);
    std::uint32_t assigned_id = 0U;
    if (!add_catalog_entry(candidate, entry, assigned_id, catalog_error)) {
        cleanup_path(filesystem, kUploadPartPath);
        result.error = ImageStoreError::catalog_invalid;
        result.catalog_error = catalog_error;
        return result;
    }
    std::size_t catalog_bytes = 0U;
    if (!serialize_catalog(
            candidate,
            catalog_buffer,
            catalog_capacity,
            catalog_bytes,
            catalog_error)) {
        cleanup_path(filesystem, kUploadPartPath);
        result.error = ImageStoreError::catalog_invalid;
        result.catalog_error = catalog_error;
        return result;
    }

    cleanup_path(filesystem, image_part_path);
    if (!filesystem.rename(kUploadPartPath, image_part_path)) {
        cleanup_path(filesystem, kUploadPartPath);
        return failure(ImageStoreError::rename_failed);
    }

    cleanup_path(filesystem, kCatalogPartPath);
    StorageFileHandle catalog_handle{};
    if (!filesystem.open_write(kCatalogPartPath, catalog_handle)) {
        cleanup_path(filesystem, image_part_path);
        return failure(ImageStoreError::write_failed);
    }
    if (!filesystem.write(catalog_handle, catalog_buffer, catalog_bytes)) {
        (void)filesystem.close_write(catalog_handle);
        cleanup_path(filesystem, image_part_path);
        cleanup_path(filesystem, kCatalogPartPath);
        return failure(ImageStoreError::write_failed);
    }
    if (!filesystem.close_write(catalog_handle)) {
        cleanup_path(filesystem, image_part_path);
        cleanup_path(filesystem, kCatalogPartPath);
        return failure(ImageStoreError::close_failed);
    }

    if (!readback_catalog(
            filesystem,
            kCatalogPartPath,
            catalog_buffer,
            catalog_capacity,
            updated_catalog,
            catalog_error)) {
        cleanup_path(filesystem, image_part_path);
        cleanup_path(filesystem, kCatalogPartPath);
        if (catalog_error != CatalogError::none) {
            result.error = ImageStoreError::catalog_invalid;
            result.catalog_error = catalog_error;
        } else {
            result.error = ImageStoreError::catalog_read_failed;
        }
        return result;
    }
    if (!catalogs_equal(candidate, updated_catalog)) {
        cleanup_path(filesystem, image_part_path);
        cleanup_path(filesystem, kCatalogPartPath);
        result.error = ImageStoreError::catalog_invalid;
        result.catalog_error = CatalogError::invalid_entry;
        return result;
    }

    const bool had_catalog = filesystem.exists(kCatalogPath);
    cleanup_path(filesystem, kCatalogBakPath);
    if (had_catalog &&
        !filesystem.rename(kCatalogPath, kCatalogBakPath)) {
        cleanup_path(filesystem, image_part_path);
        cleanup_path(filesystem, kCatalogPartPath);
        return failure(ImageStoreError::rename_failed);
    }
    if (!filesystem.rename(image_part_path, image_path)) {
        const bool restored = restore_catalog(filesystem, had_catalog);
        cleanup_path(filesystem, image_part_path);
        cleanup_path(filesystem, kCatalogPartPath);
        result.error = restored
                          ? ImageStoreError::rename_failed
                          : ImageStoreError::rollback_failed;
        return result;
    }
    if (!filesystem.rename(kCatalogPartPath, kCatalogPath)) {
        const bool image_removed = filesystem.remove_if_exists(image_path);
        const bool restored = restore_catalog(filesystem, had_catalog);
        cleanup_path(filesystem, kCatalogPartPath);
        result.error = image_removed && restored
                          ? ImageStoreError::rename_failed
                          : ImageStoreError::rollback_failed;
        return result;
    }
    if (had_catalog) {
        // A stale .bak is safe to leave for boot recovery if cleanup is
        // interrupted; the canonical catalog is already the new complete blob.
        (void)filesystem.remove_if_exists(kCatalogBakPath);
    }
    updated_catalog = candidate;
    result.assigned_id = assigned_id;
    return result;
}

ImageStoreResult persist_catalog_transactionally(
    StorageFileSystem& filesystem,
    const Catalog& current_catalog,
    const Catalog& candidate_catalog,
    std::uint8_t* const catalog_buffer,
    const std::size_t catalog_capacity)
{
    ImageStoreResult result{};
    if (&current_catalog == &candidate_catalog ||
        catalog_buffer == nullptr ||
        catalog_capacity < kCatalogMaxBytes) {
        result.error = ImageStoreError::invalid_argument;
        return result;
    }

    CatalogError catalog_error = CatalogError::none;
    if (!validate_catalog(current_catalog, catalog_error) ||
        !validate_catalog(candidate_catalog, catalog_error)) {
        result.error = ImageStoreError::catalog_invalid;
        result.catalog_error = catalog_error;
        return result;
    }
    if (filesystem.free_bytes() < kCatalogMaxBytes) {
        result.error = ImageStoreError::no_space;
        return result;
    }

    std::size_t catalog_bytes = 0U;
    if (!serialize_catalog(
            candidate_catalog,
            catalog_buffer,
            catalog_capacity,
            catalog_bytes,
            catalog_error)) {
        result.error = ImageStoreError::catalog_invalid;
        result.catalog_error = catalog_error;
        return result;
    }

    cleanup_path(filesystem, kCatalogPartPath);
    StorageFileHandle catalog_handle{};
    if (!filesystem.open_write(kCatalogPartPath, catalog_handle)) {
        result.error = ImageStoreError::write_failed;
        return result;
    }
    if (!filesystem.write(catalog_handle, catalog_buffer, catalog_bytes)) {
        (void)filesystem.close_write(catalog_handle);
        cleanup_path(filesystem, kCatalogPartPath);
        result.error = ImageStoreError::write_failed;
        return result;
    }
    if (!filesystem.close_write(catalog_handle)) {
        cleanup_path(filesystem, kCatalogPartPath);
        result.error = ImageStoreError::close_failed;
        return result;
    }

    Catalog readback{};
    if (!readback_catalog(
            filesystem,
            kCatalogPartPath,
            catalog_buffer,
            catalog_capacity,
            readback,
            catalog_error)) {
        cleanup_path(filesystem, kCatalogPartPath);
        result.error = catalog_error == CatalogError::none
                           ? ImageStoreError::catalog_read_failed
                           : ImageStoreError::catalog_invalid;
        result.catalog_error = catalog_error;
        return result;
    }
    if (!catalogs_equal(candidate_catalog, readback)) {
        cleanup_path(filesystem, kCatalogPartPath);
        result.error = ImageStoreError::catalog_invalid;
        result.catalog_error = CatalogError::invalid_entry;
        return result;
    }

    cleanup_path(filesystem, kCatalogMutationMarkerPath);
    StorageFileHandle marker_handle{};
    if (!filesystem.open_write(
            kCatalogMutationMarkerPath,
            marker_handle) ||
        !filesystem.close_write(marker_handle)) {
        (void)filesystem.close_write(marker_handle);
        cleanup_path(filesystem, kCatalogMutationMarkerPath);
        cleanup_path(filesystem, kCatalogPartPath);
        result.error = ImageStoreError::write_failed;
        return result;
    }

    const bool had_catalog = filesystem.exists(kCatalogPath);
    cleanup_path(filesystem, kCatalogBakPath);
    if (had_catalog &&
        !filesystem.rename(kCatalogPath, kCatalogBakPath)) {
        cleanup_path(filesystem, kCatalogMutationMarkerPath);
        cleanup_path(filesystem, kCatalogPartPath);
        result.error = ImageStoreError::rename_failed;
        return result;
    }
    if (!filesystem.rename(kCatalogPartPath, kCatalogPath)) {
        const bool restored = restore_catalog(filesystem, had_catalog);
        cleanup_path(filesystem, kCatalogMutationMarkerPath);
        cleanup_path(filesystem, kCatalogPartPath);
        result.error = restored
                           ? ImageStoreError::rename_failed
                           : ImageStoreError::rollback_failed;
        return result;
    }
    if (had_catalog) {
        (void)filesystem.remove_if_exists(kCatalogBakPath);
    }
    cleanup_path(filesystem, kCatalogMutationMarkerPath);
    return result;
}

}  // namespace pf_storage
