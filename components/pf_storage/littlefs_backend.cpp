#include "pf_storage/littlefs_backend.hpp"

#include <cerrno>
#include <cstdio>
#include <dirent.h>
#include <cstring>
#include <sys/stat.h>
#include <sys/statvfs.h>

#include "esp_littlefs.h"

namespace pf_storage {

LittleFsStorageFileSystem::LittleFsStorageFileSystem(
    const char* const partition_label,
    const char* const mount_path)
    : partition_label_(partition_label),
      mount_path_(mount_path)
{
}

std::uint64_t LittleFsStorageFileSystem::free_bytes() const
{
    if (partition_label_ != nullptr) {
        std::size_t total_bytes = 0U;
        std::size_t used_bytes = 0U;
        if (esp_littlefs_info(
                partition_label_,
                &total_bytes,
                &used_bytes) == ESP_OK &&
            used_bytes <= total_bytes) {
            return static_cast<std::uint64_t>(total_bytes - used_bytes);
        }
    }
    if (mount_path_ == nullptr) {
        return 0U;
    }
    struct statvfs info{};
    if (statvfs(mount_path_, &info) != 0) {
        return 0U;
    }
    const std::uint64_t free_blocks =
        info.f_bavail != 0U
            ? static_cast<std::uint64_t>(info.f_bavail)
            : static_cast<std::uint64_t>(info.f_bfree);
    return free_blocks *
           static_cast<std::uint64_t>(info.f_frsize);
}

bool LittleFsStorageFileSystem::exists(const char* const path) const
{
    struct stat status{};
    return path != nullptr && stat(path, &status) == 0;
}

bool LittleFsStorageFileSystem::remove_if_exists(const char* const path)
{
    if (path == nullptr) {
        return false;
    }
    if (std::remove(path) == 0) {
        return true;
    }
    return errno == ENOENT;
}

bool LittleFsStorageFileSystem::rename(
    const char* const from,
    const char* const to)
{
    return from != nullptr && to != nullptr && std::rename(from, to) == 0;
}

bool LittleFsStorageFileSystem::open_write(
    const char* const path,
    StorageFileHandle& handle)
{
    handle.opaque = nullptr;
    if (path == nullptr) {
        return false;
    }
    handle.opaque = std::fopen(path, "wb");
    return handle.opaque != nullptr;
}

bool LittleFsStorageFileSystem::write(
    StorageFileHandle& handle,
    const std::uint8_t* const data,
    const std::size_t length)
{
    if (handle.opaque == nullptr || (data == nullptr && length != 0U)) {
        return false;
    }
    return std::fwrite(data, 1U, length, static_cast<FILE*>(handle.opaque)) ==
           length;
}

bool LittleFsStorageFileSystem::close_write(StorageFileHandle& handle)
{
    if (handle.opaque == nullptr) {
        return true;
    }
    FILE* const file = static_cast<FILE*>(handle.opaque);
    handle.opaque = nullptr;
    return std::fclose(file) == 0;
}

bool LittleFsStorageFileSystem::open_read(
    const char* const path,
    StorageFileHandle& handle)
{
    handle.opaque = nullptr;
    if (path == nullptr) {
        return false;
    }
    handle.opaque = std::fopen(path, "rb");
    return handle.opaque != nullptr;
}

bool LittleFsStorageFileSystem::read(
    StorageFileHandle& handle,
    std::uint8_t* const buffer,
    const std::size_t capacity,
    std::size_t& bytes_read)
{
    bytes_read = 0U;
    if (handle.opaque == nullptr || buffer == nullptr || capacity == 0U) {
        return false;
    }
    FILE* const file = static_cast<FILE*>(handle.opaque);
    bytes_read = std::fread(buffer, 1U, capacity, file);
    return std::ferror(file) == 0;
}

bool LittleFsStorageFileSystem::close_read(StorageFileHandle& handle)
{
    if (handle.opaque == nullptr) {
        return true;
    }
    FILE* const file = static_cast<FILE*>(handle.opaque);
    handle.opaque = nullptr;
    return std::fclose(file) == 0;
}

bool LittleFsStorageFileSystem::for_each_file(
    const char* const directory,
    const StorageFileVisitor visitor,
    void* const context)
{
    if (directory == nullptr || visitor == nullptr) {
        return false;
    }
    DIR* const stream = opendir(directory);
    if (stream == nullptr) {
        return false;
    }
    bool success = true;
    struct dirent* entry = nullptr;
    char path[160]{};
    while (true) {
        errno = 0;
        entry = readdir(stream);
        if (entry == nullptr) {
            break;
        }
        if (std::strcmp(entry->d_name, ".") == 0 ||
            std::strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        const int written = std::snprintf(
            path,
            sizeof(path),
            "%s/%s",
            directory,
            entry->d_name);
        if (written < 0 || static_cast<std::size_t>(written) >= sizeof(path)) {
            success = false;
            break;
        }
        struct stat status{};
        if (stat(path, &status) != 0) {
            if (errno == ENOENT) {
                continue;
            }
            success = false;
            break;
        }
        if (!S_ISREG(status.st_mode)) {
            continue;
        }
        if (!visitor(context, path)) {
            break;
        }
    }
    if (entry == nullptr && errno != 0) {
        success = false;
    }
    if (closedir(stream) != 0) {
        success = false;
    }
    return success;
}

}  // namespace pf_storage
