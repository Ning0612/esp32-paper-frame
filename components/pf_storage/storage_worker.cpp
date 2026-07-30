#include "pf_storage/storage_worker.hpp"

namespace pf_storage {

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
    }
    return last_result_;
}

}  // namespace pf_storage
