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

// Only imagefs is mounted. The WebUI is compiled into the app image
// (docs/adr/0016-embed-webui-assets-in-firmware.md), so the webfs partition
// is reserved: never mounted, written or reported.
struct FileSystemSnapshot {
    FileSystemStatus imagefs;
};

FileSystemSnapshot mount_all();

}  // namespace pf_storage
