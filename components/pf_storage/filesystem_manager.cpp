#include "pf_storage/filesystem_manager.hpp"

#include "esp_littlefs.h"

namespace pf_storage {
namespace {

FileSystemStatus mount_one(const char* label, const char* base_path)
{
    const esp_vfs_littlefs_conf_t configuration{
        .base_path = base_path,
        .partition_label = label,
        .partition = nullptr,
        .format_if_mount_failed = false,
        .read_only = false,
        .dont_mount = false,
        .grow_on_mount = false,
    };

    const esp_err_t mount_result =
        esp_vfs_littlefs_register(&configuration);
    if (mount_result != ESP_OK) {
        return {
            false,
            mount_result,
            ESP_ERR_INVALID_STATE,
            0,
            0,
        };
    }

    std::size_t total_bytes = 0;
    std::size_t used_bytes = 0;
    const esp_err_t info_result =
        esp_littlefs_info(label, &total_bytes, &used_bytes);
    return {
        true,
        ESP_OK,
        info_result,
        total_bytes,
        used_bytes,
    };
}

}  // namespace

FileSystemSnapshot mount_all()
{
    return {
        .imagefs = mount_one("imagefs", "/images"),
    };
}

}  // namespace pf_storage
