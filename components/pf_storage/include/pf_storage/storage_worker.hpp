#pragma once

#include <cstdint>
#include <cstddef>
#include <atomic>

#include "pf_storage/recovery.hpp"

namespace pf_storage {

enum class StorageWorkerError : std::uint8_t {
    none = 0U,
    invalid_argument,
    already_started,
    recovery_failed,
    // Distinct from recovery_failed: the transient RecoveryWorkspace
    // allocation itself failed (needs a contiguous ~33.7 KB internal-RAM
    // block at kCatalogMaxEntries=48) before recover_image_transactions()
    // ever ran, so StorageWorkerResult::recovery stays default
    // (RecoveryError::none) -- callers that want to log heap-diagnostic
    // detail (largest free internal block, etc.) can key off this value
    // specifically rather than guessing from a default-valued recovery
    // field.
    recovery_workspace_alloc_failed,
};

struct StorageWorkerResult {
    StorageWorkerError error = StorageWorkerError::none;
    RecoveryResult recovery{};

    bool ok() const
    {
        return error == StorageWorkerError::none;
    }
};

const char* to_string(StorageWorkerError error);

enum class ImageStreamError : std::uint8_t {
    none = 0U,
    invalid_argument,
    not_ready,
    busy,
    not_found,
    corrupt,
    path_too_long,
    open_failed,
    read_failed,
    visitor_failed,
    close_failed,
};

const char* to_string(ImageStreamError error);

struct ImageStreamResult {
    ImageStreamError error = ImageStreamError::none;
    std::size_t bytes_sent = 0U;

    bool ok() const
    {
        return error == ImageStreamError::none;
    }
};

using ImageChunkVisitor = bool (*)(
    void* context,
    const std::uint8_t* data,
    std::size_t length);

using CatalogEntryVisitor = bool (*)(
    void* context,
    const CatalogEntry& entry);

// Owns access to the imagefs backend and its persistent recovery workspace.
// The backend must outlive this worker. Startup is intentionally synchronous so
// callers can publish the imagefs runtime state before exposing HTTP routes;
// image mutations are serialized by this same owner instead of running on an
// HTTP handler stack. A failed startup is fail-closed and requires reboot
// before recovery is attempted again.
class StorageWorker final {
public:
    explicit StorageWorker(StorageFileSystem& filesystem)
        : filesystem_(&filesystem)
    {
    }

    ~StorageWorker();
    StorageWorker(const StorageWorker&) = delete;
    StorageWorker& operator=(const StorageWorker&) = delete;

    StorageWorkerResult start();

    bool started() const
    {
        return started_;
    }

    bool ready() const
    {
        return started_ && last_result_.ok();
    }

    // Read-only iteration avoids copying the bounded catalog onto an HTTP
    // handler stack. Returning false from the visitor stops successfully.
    bool visit_catalog(
        CatalogEntryVisitor visitor,
        void* context) const;

    bool find_catalog_entry_by_name(
        const char* name,
        std::size_t name_length,
        CatalogEntry& destination) const;

    bool find_catalog_entry_by_id(
        std::uint32_t id,
        CatalogEntry& destination) const;

    // Stores one complete PFR1 stream and publishes the catalog only after the
    // image/catalog transaction has read back and validated its candidate.
    ImageStoreResult store_image(
        const StorageStreamReader& reader,
        std::size_t content_length);

    ImageStoreResult activate_image(std::uint32_t id);
    ImageStoreResult remove_image(std::uint32_t id);
    ImageStoreResult reorder_images(
        const std::uint32_t* ids,
        std::size_t count);

    ImageStreamResult stream_image(
        const char* name,
        std::size_t name_length,
        ImageChunkVisitor visitor,
        void* context);

    std::uint64_t free_bytes() const;

    bool operation_busy() const
    {
        return operation_busy_.load(std::memory_order_acquire);
    }

    // False when both PSRAM and internal-RAM allocation of the compressed-
    // payload scratch buffers failed at start() (or start() hasn't run
    // yet): compressed PFR1 uploads are then rejected with
    // pf_image::ValidationError::unsupported_compression while uncompressed
    // uploads remain unaffected. Callers may want to log this once at boot.
    bool compression_supported() const
    {
        return inflate_compressed_scratch_ != nullptr &&
               inflate_output_scratch_ != nullptr;
    }

    const StorageWorkerResult& last_result() const
    {
        return last_result_;
    }

private:
    StorageFileSystem* filesystem_ = nullptr;
    // RecoveryWorkspace (4 Catalog copies + a serialization buffer, ~33.7 KB
    // at kCatalogMaxEntries=48) is deliberately NOT a permanent member: it is
    // only ever used once, synchronously, inside start() (see recovery.cpp's
    // recover_image_transactions()), before any HTTP route or other task
    // exists. Keeping it permanently resident was the direct cause of the
    // on-demand image-upload/mutation task-stack allocation failures
    // recorded in docs/adr/0010-revert-catalog-cap-raise-ram-constraint.md.
    // start() now allocates it transiently (internal RAM, RAII-freed before
    // start() returns on every path) instead.
    StorageWorkerResult last_result_{};
    Catalog catalog_{};
    std::uint8_t catalog_buffer_[kCatalogMaxBytes]{};
    mutable std::atomic<bool> operation_busy_{false};
    bool catalog_available_ = false;
    bool started_ = false;

    // Scratch for validating a compressed PFR1 payload (Pfr1Flags::
    // kCompressed): allocated once in start(), shared between ingest
    // (store_image) and boot-time recovery since the two never overlap
    // (recovery finishes before HTTP routes -- and therefore uploads --
    // become reachable). Allocation failure degrades gracefully: with
    // nullptr buffers, Pfr1Validator rejects compressed uploads with
    // unsupported_compression while uncompressed uploads are unaffected.
    std::uint8_t* inflate_compressed_scratch_ = nullptr;
    std::uint8_t* inflate_output_scratch_ = nullptr;
    pf_image::Pfr1InflateBuffers inflate_buffers_{};

    void allocate_inflate_scratch();
    void release_inflate_scratch();
};

}  // namespace pf_storage
