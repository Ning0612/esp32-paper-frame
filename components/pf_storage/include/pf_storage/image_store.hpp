#pragma once

#include <cstddef>
#include <cstdint>

#include "pf_image/pfr1.hpp"
#include "pf_storage/catalog.hpp"

namespace pf_storage {

inline constexpr std::size_t kImageStoreChunkBytes = 1024U;
inline constexpr const char* kImageStoreRootPath = "/images";

enum class StorageReadResult : std::uint8_t {
    data = 0U,
    eof,
    error,
};

using StorageReadChunk = StorageReadResult (*)(
    void* context,
    std::uint8_t* buffer,
    std::size_t capacity,
    std::size_t& bytes_read);

struct StorageStreamReader {
    StorageReadChunk read = nullptr;
    void* context = nullptr;
};

struct StorageFileHandle {
    void* opaque = nullptr;
};

using StorageFileVisitor = bool (*)(void* context, const char* path);

// A single StorageWorker owns the backend and performs one mutation at a time.
// Implementations must make remove_if_exists idempotent and close_write flush
// all bytes before returning.
class StorageFileSystem {
public:
    virtual ~StorageFileSystem() = default;

    virtual std::uint64_t free_bytes() const = 0;
    virtual bool exists(const char* path) const = 0;
    virtual bool remove_if_exists(const char* path) = 0;
    virtual bool rename(const char* from, const char* to) = 0;
    virtual bool open_write(const char* path, StorageFileHandle& handle) = 0;
    virtual bool write(
        StorageFileHandle& handle,
        const std::uint8_t* data,
        std::size_t length) = 0;
    virtual bool close_write(StorageFileHandle& handle) = 0;
    virtual bool open_read(const char* path, StorageFileHandle& handle) = 0;
    virtual bool read(
        StorageFileHandle& handle,
        std::uint8_t* buffer,
        std::size_t capacity,
        std::size_t& bytes_read) = 0;
    virtual bool close_read(StorageFileHandle& handle) = 0;
    // Visit regular files below directory. Returning false stops traversal
    // successfully; the backend returns false only for an enumeration error.
    virtual bool for_each_file(
        const char* directory,
        StorageFileVisitor visitor,
        void* context) = 0;
};

enum class ImageStoreError : std::uint8_t {
    none = 0U,
    invalid_argument,
    not_ready,
    busy,
    no_space,
    stream_failed,
    write_failed,
    close_failed,
    catalog_read_failed,
    invalid_image,
    image_conflict,
    catalog_invalid,
    path_too_long,
    remove_failed,
    rename_failed,
    rollback_failed,
};

struct ImageStoreResult {
    ImageStoreError error = ImageStoreError::none;
    pf_image::ValidationError validation_error =
        pf_image::ValidationError::none;
    CatalogError catalog_error = CatalogError::none;
    std::size_t bytes_received = 0U;
    std::uint32_t assigned_id = 0U;
    bool catalog_committed = false;
    // Catalog generation as of this mutation's completion (0 if the
    // mutation never touched the catalog, e.g. an early failure). Only
    // StorageWorker::store_image/remove_image populate this today. It is
    // monotonically increasing and assigned while the caller's
    // OperationGuard still serializes access, so callers can pass it
    // through to RuntimeCoordinator::update_imagefs_used_bytes to reject a
    // stale, out-of-order publish from a slower concurrent mutation.
    std::uint32_t catalog_generation = 0U;
    // Filesystem free-byte count sampled while OperationGuard was still
    // held, i.e. after this mutation's writes landed but before any other
    // mutation on the same StorageWorker could begin -- so it reflects
    // exactly this mutation's effect, never a concurrent one's in-progress
    // write. Only StorageWorker::store_image/remove_image populate this.
    std::uint64_t imagefs_free_bytes_after = 0U;

    bool ok() const
    {
        return error == ImageStoreError::none;
    }
};

const char* to_string(ImageStoreError error);

// Stream one complete PFR1 request into imagefs. The current catalog is never
// modified. The two catalog references must not alias. updated_catalog is the
// committed output on success; its contents are unspecified after a failure
// because it is also used as read-back scratch.
// The catalog_buffer is caller-owned scratch storage of at least kCatalogMaxBytes
// and the operation is single-owner rather than re-entrant.
// inflate_buffers is required only to accept an upload whose PFR1 header sets
// Pfr1Flags::kCompressed; passing nullptr rejects such uploads with
// pf_image::ValidationError::unsupported_compression (uncompressed uploads
// are unaffected either way).
ImageStoreResult store_image_transactionally(
    StorageFileSystem& filesystem,
    const Catalog& current_catalog,
    Catalog& updated_catalog,
    std::uint8_t* catalog_buffer,
    std::size_t catalog_capacity,
    const StorageStreamReader& reader,
    std::size_t content_length,
    const pf_image::Pfr1InflateBuffers* inflate_buffers = nullptr);

// Publishes a validated catalog-only mutation using the same .part/.bak
// transaction as image uploads. No image file is touched.
ImageStoreResult persist_catalog_transactionally(
    StorageFileSystem& filesystem,
    const Catalog& current_catalog,
    const Catalog& candidate_catalog,
    std::uint8_t* catalog_buffer,
    std::size_t catalog_capacity);

}  // namespace pf_storage
