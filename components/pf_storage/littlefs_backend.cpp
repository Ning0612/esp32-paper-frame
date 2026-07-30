#include "pf_storage/littlefs_backend.hpp"

#include <cerrno>
#include <cstdio>
#include <sys/stat.h>
#include <sys/statvfs.h>

namespace pf_storage {

LittleFsStorageFileSystem::LittleFsStorageFileSystem(
    const char* const mount_path)
    : mount_path_(mount_path)
{
}

std::uint64_t LittleFsStorageFileSystem::free_bytes() const
{
    if (mount_path_ == nullptr) {
        return 0U;
    }
    struct statvfs info{};
    if (statvfs(mount_path_, &info) != 0) {
        return 0U;
    }
    return static_cast<std::uint64_t>(info.f_bavail) *
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

}  // namespace pf_storage
