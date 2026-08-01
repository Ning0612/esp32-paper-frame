#pragma once

#include <cstddef>
#include <cstdint>

#include "pf_storage/image_store.hpp"

namespace pf_storage {

inline constexpr std::size_t kRecoveryPathCapacity = 160U;
inline constexpr std::size_t kRecoveryMaxImageParts = 8U;

enum class RecoveryError : std::uint8_t {
    none = 0U,
    invalid_argument,
    list_failed,
    read_failed,
    catalog_invalid,
    image_invalid,
    ambiguous,
    rename_failed,
    rollback_failed,
};

enum class RecoveryAction : std::uint8_t {
    no_change = 0U,
    promoted_candidate,
    restored_backup,
    discarded_stale_state,
    manual_intervention,
};

struct RecoveryWorkspace {
    Catalog canonical{};
    Catalog backup{};
    Catalog candidate{};
    Catalog recovered{};
    std::uint8_t catalog_buffer[kCatalogMaxBytes]{};
    char image_part_paths[kRecoveryMaxImageParts][kRecoveryPathCapacity]{};
    std::size_t image_part_count = 0U;
    // Caller-owned scratch for re-validating a compressed PFR1 candidate
    // during recovery (see Pfr1InflateBuffers); left default-constructed
    // (nullptr buffers) a compressed candidate image fails closed with
    // pf_image::ValidationError::unsupported_compression, same as ingest.
    pf_image::Pfr1InflateBuffers inflate_buffers{};
};

struct RecoveryResult {
    RecoveryError error = RecoveryError::none;
    RecoveryAction action = RecoveryAction::no_change;
    CatalogError catalog_error = CatalogError::none;
    pf_image::ValidationError image_error = pf_image::ValidationError::none;
    bool has_catalog = false;
    bool cleanup_pending = false;

    bool ok() const
    {
        return error == RecoveryError::none;
    }
};

const char* to_string(RecoveryError error);
const char* to_string(RecoveryAction action);

// Recover one interrupted image/catalog transaction after imagefs is mounted.
// The workspace must be task-owned persistent storage; it is intentionally
// explicit because four catalogs and the read buffer do not belong on an HTTP
// handler stack. On success, workspace.recovered is the catalog to publish and
// has_catalog indicates whether a canonical catalog exists.
RecoveryResult recover_image_transactions(
    StorageFileSystem& filesystem,
    RecoveryWorkspace& workspace);

}  // namespace pf_storage
