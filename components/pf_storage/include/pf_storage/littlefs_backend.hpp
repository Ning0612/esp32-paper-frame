#pragma once

#include "pf_storage/image_store.hpp"

namespace pf_storage {

class LittleFsStorageFileSystem final : public StorageFileSystem {
public:
    explicit LittleFsStorageFileSystem(const char* mount_path);

    std::uint64_t free_bytes() const override;
    bool exists(const char* path) const override;
    bool remove_if_exists(const char* path) override;
    bool rename(const char* from, const char* to) override;
    bool open_write(const char* path, StorageFileHandle& handle) override;
    bool write(
        StorageFileHandle& handle,
        const std::uint8_t* data,
        std::size_t length) override;
    bool close_write(StorageFileHandle& handle) override;
    bool open_read(const char* path, StorageFileHandle& handle) override;
    bool read(
        StorageFileHandle& handle,
        std::uint8_t* buffer,
        std::size_t capacity,
        std::size_t& bytes_read) override;
    bool close_read(StorageFileHandle& handle) override;
    bool for_each_file(
        const char* directory,
        StorageFileVisitor visitor,
        void* context) override;

private:
    const char* mount_path_ = nullptr;
};

}  // namespace pf_storage
