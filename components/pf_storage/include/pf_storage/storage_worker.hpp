#pragma once

#include <cstdint>

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

// Owns access to the imagefs backend and its persistent recovery workspace.
// The backend must outlive this worker. Startup is intentionally synchronous so
// callers can publish the imagefs runtime state before exposing HTTP routes;
// future mutations will be serialized by this same owner instead of running on
// an HTTP handler stack. A failed startup is fail-closed and requires reboot
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

    const StorageWorkerResult& last_result() const
    {
        return last_result_;
    }

private:
    StorageFileSystem* filesystem_ = nullptr;
    RecoveryWorkspace workspace_{};
    StorageWorkerResult last_result_{};
    bool started_ = false;
};

}  // namespace pf_storage
