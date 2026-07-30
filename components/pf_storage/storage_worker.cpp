#include "pf_storage/storage_worker.hpp"

#include <cstdio>
#include <cstring>

namespace pf_storage {

namespace {

class OperationGuard final {
public:
    explicit OperationGuard(std::atomic<bool>& busy)
        : busy_(&busy)
    {
        bool expected = false;
        acquired_ = busy_->compare_exchange_strong(
            expected,
            true,
            std::memory_order_acquire,
            std::memory_order_relaxed);
    }

    ~OperationGuard()
    {
        if (acquired_) {
            busy_->store(false, std::memory_order_release);
        }
    }

    bool acquired() const
    {
        return acquired_;
    }

private:
    std::atomic<bool>* busy_ = nullptr;
    bool acquired_ = false;
};

}  // namespace

const char* to_string(const StorageWorkerError error)
{
    switch (error) {
        case StorageWorkerError::none:
            return "none";
        case StorageWorkerError::invalid_argument:
            return "invalid_argument";
        case StorageWorkerError::already_started:
            return "already_started";
        case StorageWorkerError::recovery_failed:
            return "recovery_failed";
    }
    return "invalid_argument";
}

const char* to_string(const ImageStreamError error)
{
    switch (error) {
        case ImageStreamError::none:
            return "none";
        case ImageStreamError::invalid_argument:
            return "invalid_argument";
        case ImageStreamError::not_ready:
            return "not_ready";
        case ImageStreamError::busy:
            return "busy";
        case ImageStreamError::not_found:
            return "not_found";
        case ImageStreamError::corrupt:
            return "corrupt";
        case ImageStreamError::path_too_long:
            return "path_too_long";
        case ImageStreamError::open_failed:
            return "open_failed";
        case ImageStreamError::read_failed:
            return "read_failed";
        case ImageStreamError::visitor_failed:
            return "visitor_failed";
        case ImageStreamError::close_failed:
            return "close_failed";
    }
    return "invalid_argument";
}

StorageWorkerResult StorageWorker::start()
{
    if (started_) {
        StorageWorkerResult result{};
        result.error = StorageWorkerError::already_started;
        result.recovery = last_result_.recovery;
        return result;
    }
    started_ = true;
    last_result_ = {};
    if (filesystem_ == nullptr) {
        last_result_.error = StorageWorkerError::invalid_argument;
        return last_result_;
    }

    last_result_.recovery = recover_image_transactions(
        *filesystem_,
        workspace_);
    if (!last_result_.recovery.ok()) {
        last_result_.error = StorageWorkerError::recovery_failed;
        catalog_available_ = false;
        return last_result_;
    }
    if (last_result_.recovery.has_catalog) {
        catalog_ = workspace_.recovered;
    } else if (!initialize_catalog(catalog_)) {
        last_result_.error = StorageWorkerError::recovery_failed;
        catalog_available_ = false;
        return last_result_;
    }
    catalog_available_ = true;
    return last_result_;
}

bool StorageWorker::visit_catalog(
    const CatalogEntryVisitor visitor,
    void* const context) const
{
    if (!ready() || !catalog_available_ || visitor == nullptr) {
        return false;
    }
    OperationGuard guard(operation_busy_);
    if (!guard.acquired()) {
        return false;
    }
    for (std::size_t index = 0U; index < catalog_.count; ++index) {
        if (!visitor(context, catalog_.entries[index])) {
            break;
        }
    }
    return true;
}

bool StorageWorker::find_catalog_entry_by_name(
    const char* const name,
    const std::size_t name_length,
    CatalogEntry& destination) const
{
    if (!ready() || !catalog_available_ || name == nullptr ||
        name_length == 0U || name_length > pf_image::kPfr1MaxFilenameBytes) {
        return false;
    }
    OperationGuard guard(operation_busy_);
    if (!guard.acquired()) {
        return false;
    }
    const CatalogEntry* const entry = pf_storage::find_catalog_entry_by_name(
        catalog_,
        name,
        name_length);
    if (entry == nullptr) {
        return false;
    }
    destination = *entry;
    return true;
}

ImageStreamResult StorageWorker::stream_image(
    const char* const name,
    const std::size_t name_length,
    const ImageChunkVisitor visitor,
    void* const context)
{
    ImageStreamResult result{};
    if (!ready() || filesystem_ == nullptr) {
        result.error = ImageStreamError::not_ready;
        return result;
    }
    OperationGuard guard(operation_busy_);
    if (!guard.acquired()) {
        result.error = ImageStreamError::busy;
        return result;
    }
    if (name == nullptr || name_length == 0U || visitor == nullptr ||
        name_length > pf_image::kPfr1MaxFilenameBytes ||
        !pf_image::valid_filename(
            reinterpret_cast<const std::uint8_t*>(name),
            name_length)) {
        result.error = ImageStreamError::invalid_argument;
        return result;
    }
    const CatalogEntry* entry = nullptr;
    for (std::size_t index = 0U; index < catalog_.count; ++index) {
        const CatalogEntry& candidate = catalog_.entries[index];
        if (candidate.name_length == name_length &&
            std::memcmp(candidate.name, name, name_length) == 0) {
            entry = &candidate;
            break;
        }
    }
    if (entry == nullptr) {
        result.error = ImageStreamError::not_found;
        return result;
    }
    if ((entry->flags & kCatalogCorrupt) != 0U) {
        result.error = ImageStreamError::corrupt;
        return result;
    }

    char path[kRecoveryPathCapacity]{};
    const int path_length = std::snprintf(
        path,
        sizeof(path),
        "%s/%.*s",
        kImageStoreRootPath,
        static_cast<int>(name_length),
        name);
    if (path_length <= 0 ||
        static_cast<std::size_t>(path_length) >= sizeof(path)) {
        result.error = ImageStreamError::path_too_long;
        return result;
    }

    StorageFileHandle handle{};
    if (!filesystem_->open_read(path, handle)) {
        result.error = ImageStreamError::open_failed;
        return result;
    }
    std::uint8_t buffer[kImageStoreChunkBytes]{};
    std::size_t remaining = entry->file_bytes;
    while (remaining > 0U) {
        std::size_t bytes_read = 0U;
        const std::size_t capacity =
            remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        if (!filesystem_->read(
                handle,
                buffer,
                capacity,
                bytes_read) ||
            bytes_read == 0U ||
            bytes_read > capacity) {
            result.error = ImageStreamError::read_failed;
            filesystem_->close_read(handle);
            return result;
        }
        if (!visitor(context, buffer, bytes_read)) {
            result.error = ImageStreamError::visitor_failed;
            filesystem_->close_read(handle);
            return result;
        }
        result.bytes_sent += bytes_read;
        remaining -= bytes_read;
    }

    std::size_t extra_bytes = 0U;
    if (!filesystem_->read(handle, buffer, 1U, extra_bytes) ||
        extra_bytes != 0U) {
        result.error = ImageStreamError::read_failed;
        filesystem_->close_read(handle);
        return result;
    }
    if (!filesystem_->close_read(handle)) {
        result.error = ImageStreamError::close_failed;
        return result;
    }
    return result;
}

ImageStoreResult StorageWorker::store_image(
    const StorageStreamReader& reader,
    const std::size_t content_length)
{
    ImageStoreResult result{};
    if (!ready() || filesystem_ == nullptr || !catalog_available_) {
        result.error = ImageStoreError::not_ready;
        return result;
    }
    OperationGuard guard(operation_busy_);
    if (!guard.acquired()) {
        result.error = ImageStoreError::busy;
        return result;
    }

    Catalog updated{};
    result = store_image_transactionally(
        *filesystem_,
        catalog_,
        updated,
        catalog_buffer_,
        sizeof(catalog_buffer_),
        reader,
        content_length);
    if (result.ok()) {
        catalog_ = updated;
    }
    return result;
}

std::uint64_t StorageWorker::free_bytes() const
{
    return filesystem_ == nullptr ? 0U : filesystem_->free_bytes();
}

}  // namespace pf_storage
