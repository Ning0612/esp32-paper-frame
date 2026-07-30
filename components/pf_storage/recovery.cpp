#include "pf_storage/recovery.hpp"

#include <cstring>

namespace pf_storage {
namespace {

constexpr char kUploadPartPath[] = "/images/.upload.part";
constexpr char kCatalogPath[] = "/images/.catalog.pfc1";
constexpr char kCatalogPartPath[] = "/images/.catalog.pfc1.part";
constexpr char kCatalogBakPath[] = "/images/.catalog.pfc1.bak";
constexpr char kCatalogMutationMarkerPath[] = "/images/.catalog.mutation";
constexpr std::size_t kImageChunkBytes = 1024U;

struct CatalogSlot {
    bool present = false;
    bool valid = false;
    CatalogError error = CatalogError::none;
};

struct ImageInfo {
    std::size_t file_bytes = 0U;
    pf_image::Pfr1Header header{};
    std::uint16_t filename_length = 0U;
    char filename[kCatalogNameCapacity]{};
};

enum class ImageReadStatus : std::uint8_t {
    missing = 0U,
    valid,
    invalid,
    read_failed,
};

struct PartCollector {
    RecoveryWorkspace* workspace = nullptr;
    bool overflow = false;
};

struct PromotionContext {
    StorageFileSystem* filesystem = nullptr;
    RecoveryWorkspace* workspace = nullptr;
    const Catalog* candidate = nullptr;
    const CatalogEntry* added = nullptr;
    bool canonical_present = false;
    bool canonical_valid = false;
    bool backup_present = false;
    bool backup_valid = false;
};

bool ends_with(const char* const text, const char* const suffix)
{
    if (text == nullptr || suffix == nullptr) {
        return false;
    }
    const std::size_t text_length = std::strlen(text);
    const std::size_t suffix_length = std::strlen(suffix);
    return text_length >= suffix_length &&
           std::memcmp(
               text + text_length - suffix_length,
               suffix,
               suffix_length) == 0;
}

bool collect_image_part(void* const context, const char* const path)
{
    PartCollector& collector = *static_cast<PartCollector*>(context);
    if (!ends_with(path, ".pfr1.part")) {
        return true;
    }
    if (collector.workspace->image_part_count >= kRecoveryMaxImageParts ||
        std::strlen(path) >= kRecoveryPathCapacity) {
        collector.overflow = true;
        return true;
    }
    char* const destination = collector.workspace->image_part_paths[
        collector.workspace->image_part_count];
    std::strcpy(destination, path);
    ++collector.workspace->image_part_count;
    return true;
}

bool read_catalog(
    StorageFileSystem& filesystem,
    const char* const path,
    std::uint8_t* const buffer,
    Catalog& destination,
    CatalogSlot& slot)
{
    slot = CatalogSlot{};
    slot.present = filesystem.exists(path);
    if (!slot.present) {
        return true;
    }
    StorageFileHandle handle{};
    if (!filesystem.open_read(path, handle)) {
        return false;
    }
    std::size_t total = 0U;
    bool read_ok = true;
    while (true) {
        if (total == kCatalogMaxBytes) {
            std::uint8_t extra = 0U;
            std::size_t amount = 0U;
            if (!filesystem.read(handle, &extra, 1U, amount) ||
                amount != 0U) {
                read_ok = false;
            }
            break;
        }
        std::size_t amount = 0U;
        if (!filesystem.read(
                handle,
                buffer + total,
                kCatalogMaxBytes - total,
                amount)) {
            read_ok = false;
            break;
        }
        if (amount == 0U) {
            break;
        }
        if (amount > kCatalogMaxBytes - total) {
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
        parse_catalog(buffer, total, destination);
    slot.error = parsed.error;
    slot.valid = parsed.ok();
    return true;
}

ImageReadStatus read_image(
    StorageFileSystem& filesystem,
    const char* const path,
    ImageInfo& image,
    pf_image::ValidationError& error)
{
    error = pf_image::ValidationError::none;
    if (!filesystem.exists(path)) {
        return ImageReadStatus::missing;
    }
    StorageFileHandle handle{};
    if (!filesystem.open_read(path, handle)) {
        return ImageReadStatus::read_failed;
    }
    pf_image::Pfr1Validator validator{};
    std::uint8_t buffer[kImageChunkBytes]{};
    std::size_t total = 0U;
    bool read_ok = true;
    while (true) {
        std::size_t amount = 0U;
        if (!filesystem.read(
                handle,
                buffer,
                sizeof(buffer),
                amount)) {
            read_ok = false;
            break;
        }
        if (amount == 0U) {
            break;
        }
        if (amount > sizeof(buffer) ||
            total > pf_image::kPfr1MaxFileBytes - amount ||
            !validator.feed(buffer, amount)) {
            error = validator.error();
            read_ok = false;
            break;
        }
        total += amount;
    }
    if (!filesystem.close_read(handle)) {
        read_ok = false;
    }
    if (!read_ok) {
        if (error == pf_image::ValidationError::none) {
            error = validator.error();
        }
        return error == pf_image::ValidationError::none
                   ? ImageReadStatus::read_failed
                   : ImageReadStatus::invalid;
    }
    if (!validator.finish()) {
        error = validator.error();
        return ImageReadStatus::invalid;
    }
    image.file_bytes = total;
    image.header = validator.header();
    image.filename_length = image.header.filename_length;
    std::memcpy(
        image.filename,
        validator.filename(),
        image.filename_length);
    return ImageReadStatus::valid;
}

bool image_matches_entry(
    const ImageInfo& image,
    const CatalogEntry& entry)
{
    return image.file_bytes == entry.file_bytes &&
           image.header.payload_length == entry.payload_bytes &&
           image.header.width == entry.width &&
           image.header.height == entry.height &&
           image.header.orientation == entry.orientation &&
           image.filename_length == entry.name_length &&
           std::memcmp(
               image.filename,
               entry.name,
               entry.name_length) == 0;
}

bool entries_equal(const CatalogEntry& left, const CatalogEntry& right)
{
    return left.id == right.id &&
           left.created_at_epoch_s == right.created_at_epoch_s &&
           left.file_bytes == right.file_bytes &&
           left.payload_bytes == right.payload_bytes &&
           left.width == right.width &&
           left.height == right.height &&
           left.orientation == right.orientation &&
           left.flags == right.flags &&
           left.order == right.order &&
           left.name_length == right.name_length &&
           std::memcmp(left.name, right.name, left.name_length) == 0;
}

bool catalogs_equal(const Catalog& left, const Catalog& right)
{
    if (left.generation != right.generation ||
        left.next_id != right.next_id ||
        left.count != right.count) {
        return false;
    }
    for (std::size_t index = 0U; index < left.count; ++index) {
        if (!entries_equal(left.entries[index], right.entries[index])) {
            return false;
        }
    }
    return true;
}

bool catalog_is_append(
    const Catalog& base,
    const Catalog& candidate,
    CatalogEntry& added)
{
    if (base.count >= kCatalogMaxEntries ||
        candidate.count != base.count + 1U ||
        candidate.generation != base.generation + 1U ||
        base.next_id == kCatalogExhaustedNextId ||
        candidate.entries[base.count].id != base.next_id ||
        candidate.entries[base.count].order != base.count) {
        return false;
    }
    for (std::size_t index = 0U; index < base.count; ++index) {
        if (!entries_equal(base.entries[index], candidate.entries[index])) {
            return false;
        }
    }
    added = candidate.entries[base.count];
    return candidate.next_id == base.next_id + 1U;
}

bool make_image_path(
    char* const output,
    const std::size_t capacity,
    const CatalogEntry& entry,
    const char* const suffix)
{
    if (output == nullptr || suffix == nullptr ||
        entry.name_length >= kCatalogNameCapacity) {
        return false;
    }
    const std::size_t root_length = std::strlen(kImageStoreRootPath);
    const std::size_t suffix_length = std::strlen(suffix);
    const std::size_t required =
        root_length + 1U + entry.name_length + suffix_length + 1U;
    if (required > capacity) {
        return false;
    }
    std::memcpy(output, kImageStoreRootPath, root_length);
    output[root_length] = '/';
    std::memcpy(
        output + root_length + 1U,
        entry.name,
        entry.name_length);
    std::memcpy(
        output + root_length + 1U + entry.name_length,
        suffix,
        suffix_length);
    output[required - 1U] = '\0';
    return true;
}

bool remove_path(StorageFileSystem& filesystem, const char* const path)
{
    return filesystem.remove_if_exists(path);
}

bool cleanup_parts(
    StorageFileSystem& filesystem,
    RecoveryWorkspace& workspace,
    const char* const keep_path = nullptr)
{
    bool success = remove_path(filesystem, kUploadPartPath);
    if (!remove_path(filesystem, kCatalogPartPath)) {
        success = false;
    }
    if (!remove_path(filesystem, kCatalogMutationMarkerPath)) {
        success = false;
    }
    for (std::size_t index = 0U;
         index < workspace.image_part_count;
         ++index) {
        if (keep_path != nullptr &&
            std::strcmp(workspace.image_part_paths[index], keep_path) == 0) {
            continue;
        }
        if (!remove_path(
                filesystem,
                workspace.image_part_paths[index])) {
            success = false;
        }
    }
    return success;
}

bool rollback_image_promotion(
    StorageFileSystem& filesystem,
    const char* const image_path,
    const char* const image_part_path,
    const bool created_final)
{
    return !created_final ||
           filesystem.rename(image_path, image_part_path);
}

RecoveryResult restore_backup(
    StorageFileSystem& filesystem,
    RecoveryWorkspace& workspace,
    const bool canonical_present)
{
    RecoveryResult result{};
    if (canonical_present && !remove_path(filesystem, kCatalogPath)) {
        result.error = RecoveryError::rename_failed;
        return result;
    }
    if (!filesystem.rename(kCatalogBakPath, kCatalogPath)) {
        result.error = RecoveryError::rename_failed;
        return result;
    }
    workspace.recovered = workspace.backup;
    result.action = RecoveryAction::restored_backup;
    result.has_catalog = true;
    if (!cleanup_parts(filesystem, workspace)) {
        result.cleanup_pending = true;
    }
    return result;
}

RecoveryResult promote_candidate(PromotionContext& context)
{
    StorageFileSystem& filesystem = *context.filesystem;
    RecoveryWorkspace& workspace = *context.workspace;
    RecoveryResult result{};
    char image_path[kRecoveryPathCapacity]{};
    char image_part_path[kRecoveryPathCapacity]{};
    if (!make_image_path(
            image_path,
            sizeof(image_path),
            *context.added,
            "") ||
        !make_image_path(
            image_part_path,
            sizeof(image_part_path),
            *context.added,
            ".part")) {
        result.error = RecoveryError::ambiguous;
        return result;
    }

    const bool final_present = filesystem.exists(image_path);
    const bool part_present = filesystem.exists(image_part_path);
    ImageInfo image{};
    pf_image::ValidationError image_error = pf_image::ValidationError::none;
    bool final_valid = false;
    if (final_present) {
        const ImageReadStatus status = read_image(
            filesystem,
            image_path,
            image,
            image_error);
        if (status == ImageReadStatus::read_failed) {
            result.error = RecoveryError::read_failed;
            return result;
        }
        final_valid = status == ImageReadStatus::valid &&
                      image_matches_entry(image, *context.added);
        if (!final_valid && status == ImageReadStatus::valid) {
            result.error = RecoveryError::ambiguous;
            return result;
        }
        if (!final_valid) {
            result.error = RecoveryError::image_invalid;
            result.image_error = image_error;
            return result;
        }
    }

    bool created_final = false;
    if (!final_valid) {
        if (!part_present) {
            result.error = RecoveryError::image_invalid;
            return result;
        }
        const ImageReadStatus status = read_image(
            filesystem,
            image_part_path,
            image,
            image_error);
        if (status == ImageReadStatus::read_failed) {
            result.error = RecoveryError::read_failed;
            return result;
        }
        if (status != ImageReadStatus::valid ||
            !image_matches_entry(image, *context.added)) {
            result.error = RecoveryError::image_invalid;
            result.image_error = image_error;
            return result;
        }
        if (!filesystem.rename(image_part_path, image_path)) {
            result.error = RecoveryError::rename_failed;
            return result;
        }
        created_final = true;
    } else if (part_present && !remove_path(filesystem, image_part_path)) {
        result.cleanup_pending = true;
    }

    bool moved_canonical = false;
    if (context.canonical_present && context.canonical_valid) {
        if (!remove_path(filesystem, kCatalogBakPath) ||
            !filesystem.rename(kCatalogPath, kCatalogBakPath)) {
            const bool rollback_ok = rollback_image_promotion(
                filesystem,
                image_path,
                image_part_path,
                created_final);
            result.error = rollback_ok
                               ? RecoveryError::rename_failed
                               : RecoveryError::rollback_failed;
            return result;
        }
        moved_canonical = true;
    } else if (context.canonical_present &&
               !remove_path(filesystem, kCatalogPath)) {
        const bool rollback_ok = rollback_image_promotion(
            filesystem,
            image_path,
            image_part_path,
            created_final);
        result.error = rollback_ok
                           ? RecoveryError::rename_failed
                           : RecoveryError::rollback_failed;
        return result;
    }

    if (!filesystem.rename(kCatalogPartPath, kCatalogPath)) {
        bool rollback_ok = rollback_image_promotion(
            filesystem,
            image_path,
            image_part_path,
            created_final);
        if (moved_canonical) {
            rollback_ok = filesystem.rename(kCatalogBakPath, kCatalogPath) &&
                          rollback_ok;
        } else if (context.backup_present && context.backup_valid &&
                   !filesystem.exists(kCatalogPath)) {
            rollback_ok = filesystem.rename(kCatalogBakPath, kCatalogPath) &&
                          rollback_ok;
        }
        result.error = rollback_ok
                           ? RecoveryError::rename_failed
                           : RecoveryError::rollback_failed;
        return result;
    }

    workspace.recovered = *context.candidate;
    result.action = RecoveryAction::promoted_candidate;
    result.has_catalog = true;
    if (!remove_path(filesystem, kCatalogBakPath)) {
        result.cleanup_pending = true;
    }
    if (!cleanup_parts(filesystem, workspace)) {
        result.cleanup_pending = true;
    }
    return result;
}

RecoveryResult promote_catalog_only(
    StorageFileSystem& filesystem,
    RecoveryWorkspace& workspace,
    const bool canonical_present,
    const bool backup_present)
{
    RecoveryResult result{};
    bool moved_canonical = false;
    if (canonical_present) {
        if (backup_present && !remove_path(filesystem, kCatalogBakPath)) {
            result.error = RecoveryError::rename_failed;
            return result;
        }
        if (!filesystem.rename(kCatalogPath, kCatalogBakPath)) {
            result.error = RecoveryError::rename_failed;
            return result;
        }
        moved_canonical = true;
    }
    if (!filesystem.rename(kCatalogPartPath, kCatalogPath)) {
        bool rollback_ok = true;
        if (moved_canonical) {
            rollback_ok = filesystem.rename(kCatalogBakPath, kCatalogPath);
        }
        result.error = rollback_ok
                           ? RecoveryError::rename_failed
                           : RecoveryError::rollback_failed;
        return result;
    }
    workspace.recovered = workspace.candidate;
    result.action = RecoveryAction::promoted_candidate;
    result.has_catalog = true;
    if (moved_canonical || backup_present) {
        if (!remove_path(filesystem, kCatalogBakPath)) {
            result.cleanup_pending = true;
        }
    }
    if (!cleanup_parts(filesystem, workspace)) {
        result.cleanup_pending = true;
    }
    return result;
}

}  // namespace

const char* to_string(const RecoveryError error)
{
    switch (error) {
        case RecoveryError::none:
            return "none";
        case RecoveryError::invalid_argument:
            return "invalid_argument";
        case RecoveryError::list_failed:
            return "list_failed";
        case RecoveryError::read_failed:
            return "read_failed";
        case RecoveryError::catalog_invalid:
            return "catalog_invalid";
        case RecoveryError::image_invalid:
            return "image_invalid";
        case RecoveryError::ambiguous:
            return "ambiguous";
        case RecoveryError::rename_failed:
            return "rename_failed";
        case RecoveryError::rollback_failed:
            return "rollback_failed";
    }
    return "invalid_argument";
}

const char* to_string(const RecoveryAction action)
{
    switch (action) {
        case RecoveryAction::no_change:
            return "no_change";
        case RecoveryAction::promoted_candidate:
            return "promoted_candidate";
        case RecoveryAction::restored_backup:
            return "restored_backup";
        case RecoveryAction::discarded_stale_state:
            return "discarded_stale_state";
        case RecoveryAction::manual_intervention:
            return "manual_intervention";
    }
    return "manual_intervention";
}

RecoveryResult recover_image_transactions(
    StorageFileSystem& filesystem,
    RecoveryWorkspace& workspace)
{
    workspace.image_part_count = 0U;
    PartCollector collector{&workspace, false};
    if (!filesystem.for_each_file(
            kImageStoreRootPath,
            collect_image_part,
            &collector) ||
        collector.overflow) {
        RecoveryResult result{};
        result.error = RecoveryError::list_failed;
        return result;
    }

    CatalogSlot canonical_slot{};
    CatalogSlot backup_slot{};
    CatalogSlot candidate_slot{};
    if (!read_catalog(
            filesystem,
            kCatalogPath,
            workspace.catalog_buffer,
            workspace.canonical,
            canonical_slot) ||
        !read_catalog(
            filesystem,
            kCatalogBakPath,
            workspace.catalog_buffer,
            workspace.backup,
            backup_slot) ||
        !read_catalog(
            filesystem,
            kCatalogPartPath,
            workspace.catalog_buffer,
            workspace.candidate,
            candidate_slot)) {
        RecoveryResult result{};
        result.error = RecoveryError::read_failed;
        return result;
    }

    const bool canonical_valid =
        canonical_slot.present && canonical_slot.valid;
    const bool backup_valid = backup_slot.present && backup_slot.valid;
    const bool candidate_valid =
        candidate_slot.present && candidate_slot.valid;
    const bool mutation_marker_present =
        filesystem.exists(kCatalogMutationMarkerPath);

    if (canonical_valid) {
        workspace.recovered = workspace.canonical;
        RecoveryResult result{};
        result.has_catalog = true;
        if (!candidate_slot.present) {
            if (backup_slot.present &&
                !remove_path(filesystem, kCatalogBakPath)) {
                result.cleanup_pending = true;
            }
            if (!cleanup_parts(filesystem, workspace)) {
                result.cleanup_pending = true;
            }
            return result;
        }
        if (!candidate_valid) {
            result.action = RecoveryAction::discarded_stale_state;
            result.catalog_error = candidate_slot.error;
            if (!remove_path(filesystem, kCatalogPartPath) ||
                !cleanup_parts(filesystem, workspace)) {
                result.cleanup_pending = true;
            }
            return result;
        }
        if (catalogs_equal(workspace.canonical, workspace.candidate)) {
            if (!cleanup_parts(filesystem, workspace)) {
                result.cleanup_pending = true;
            }
            result.action = RecoveryAction::discarded_stale_state;
            return result;
        }
        if (mutation_marker_present && workspace.image_part_count == 0U) {
            return promote_catalog_only(
                filesystem,
                workspace,
                canonical_slot.present,
                backup_slot.present);
        }
        CatalogEntry added{};
        if (!catalog_is_append(
                workspace.canonical,
                workspace.candidate,
                added)) {
            result.error = RecoveryError::ambiguous;
            result.action = RecoveryAction::manual_intervention;
            return result;
        }
        PromotionContext context{
            &filesystem,
            &workspace,
            &workspace.candidate,
            &added,
            canonical_slot.present,
            canonical_valid,
            backup_slot.present,
            backup_valid,
        };
        return promote_candidate(context);
    }

    if (candidate_valid) {
        if (mutation_marker_present && workspace.image_part_count == 0U) {
            return promote_catalog_only(
                filesystem,
                workspace,
                canonical_slot.present,
                backup_slot.present);
        }
        Catalog base{};
        if (backup_valid) {
            base = workspace.backup;
        } else {
            CatalogError error = CatalogError::none;
            if (!initialize_catalog(base) ||
                !validate_catalog(base, error)) {
                RecoveryResult result{};
                result.error = RecoveryError::catalog_invalid;
                result.catalog_error = error;
                return result;
            }
        }
        CatalogEntry added{};
        if (!catalog_is_append(base, workspace.candidate, added)) {
            if (backup_valid &&
                catalogs_equal(workspace.backup, workspace.candidate)) {
                RecoveryResult result = restore_backup(
                    filesystem,
                    workspace,
                    canonical_slot.present);
                return result;
            }
            RecoveryResult result{};
            result.error = RecoveryError::ambiguous;
            result.action = RecoveryAction::manual_intervention;
            return result;
        }
        PromotionContext context{
            &filesystem,
            &workspace,
            &workspace.candidate,
            &added,
            canonical_slot.present,
            canonical_valid,
            backup_slot.present,
            backup_valid,
        };
        return promote_candidate(context);
    }

    if (backup_valid) {
        return restore_backup(
            filesystem,
            workspace,
            canonical_slot.present);
    }

    RecoveryResult result{};
    result.catalog_error = canonical_slot.error != CatalogError::none
                               ? canonical_slot.error
                               : candidate_slot.error;
    if (canonical_slot.present && !canonical_slot.valid) {
        result.error = RecoveryError::catalog_invalid;
        result.action = RecoveryAction::manual_intervention;
        return result;
    }
    result.action = candidate_slot.present || workspace.image_part_count != 0U
                        ? RecoveryAction::discarded_stale_state
                        : RecoveryAction::no_change;
    if (!cleanup_parts(filesystem, workspace)) {
        result.cleanup_pending = true;
    }
    return result;
}

}  // namespace pf_storage
