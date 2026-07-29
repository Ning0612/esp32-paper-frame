#pragma once

#include <cstddef>

#include "esp_err.h"

namespace pf_storage {

struct FileSystemStatus {
    bool mounted;
    esp_err_t mount_error;
    esp_err_t info_error;
    std::size_t total_bytes;
    std::size_t used_bytes;
};

struct FileSystemSnapshot {
    FileSystemStatus webfs;
    FileSystemStatus imagefs;
};

FileSystemSnapshot mount_all();

}  // namespace pf_storage
