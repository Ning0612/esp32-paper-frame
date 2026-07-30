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

    // Stores one complete PFR1 stream and publishes the catalog only after the
    // image/catalog transaction has read back and validated its candidate.
    ImageStoreResult store_image(
        const StorageStreamReader& reader,
        std::size_t content_length);

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

    const StorageWorkerResult& last_result() const
    {
        return last_result_;
    }

private:
    StorageFileSystem* filesystem_ = nullptr;
    RecoveryWorkspace workspace_{};
    StorageWorkerResult last_result_{};
    Catalog catalog_{};
    std::uint8_t catalog_buffer_[kCatalogMaxBytes]{};
    mutable std::atomic<bool> operation_busy_{false};
    bool catalog_available_ = false;
    bool started_ = false;
};

}  // namespace pf_storage
