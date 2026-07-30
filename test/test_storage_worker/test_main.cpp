#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <unity.h>

#include "pf_storage/storage_worker.hpp"

extern "C" void setUp() {}
extern "C" void tearDown() {}

namespace {

class FakeStorageFileSystem final : public pf_storage::StorageFileSystem {
public:
    struct ReadHandle {
        const std::vector<std::uint8_t>* bytes = nullptr;
        std::size_t offset = 0U;
    };

    std::uint64_t free_bytes() const override
    {
        return 2U * 1024U * 1024U;
    }

    bool exists(const char* const path) const override
    {
        return path != nullptr && files.find(path) != files.end();
    }

    bool remove_if_exists(const char* const path) override
    {
        if (path == nullptr) {
            return false;
        }
        files.erase(path);
        return true;
    }

    bool rename(
        const char* const from,
        const char* const to) override
    {
        if (from == nullptr || to == nullptr) {
            return false;
        }
        const auto found = files.find(from);
        if (found == files.end()) {
            return false;
        }
        files[to] = std::move(found->second);
        files.erase(found);
        return true;
    }

    bool open_write(
        const char*,
        pf_storage::StorageFileHandle& handle) override
    {
        handle.opaque = nullptr;
        return false;
    }

    bool write(
        pf_storage::StorageFileHandle&,
        const std::uint8_t*,
        std::size_t) override
    {
        return false;
    }

    bool close_write(pf_storage::StorageFileHandle&) override
    {
        return false;
    }

    bool open_read(
        const char* const path,
        pf_storage::StorageFileHandle& handle) override
    {
        handle.opaque = nullptr;
        const auto found = files.find(path == nullptr ? "" : path);
        if (found == files.end()) {
            return false;
        }
        handle.opaque = new ReadHandle{&found->second, 0U};
        return true;
    }

    bool read(
        pf_storage::StorageFileHandle& handle,
        std::uint8_t* const buffer,
        const std::size_t capacity,
        std::size_t& bytes_read) override
    {
        bytes_read = 0U;
        if (handle.opaque == nullptr || buffer == nullptr || capacity == 0U) {
            return false;
        }
        auto* const reader = static_cast<ReadHandle*>(handle.opaque);
        const std::size_t remaining = reader->bytes->size() - reader->offset;
        const std::size_t amount = std::min(capacity, remaining);
        std::copy_n(
            reader->bytes->data() + reader->offset,
            amount,
            buffer);
        reader->offset += amount;
        bytes_read = amount;
        return true;
    }

    bool close_read(pf_storage::StorageFileHandle& handle) override
    {
        delete static_cast<ReadHandle*>(handle.opaque);
        handle.opaque = nullptr;
        return true;
    }

    bool for_each_file(
        const char* const,
        const pf_storage::StorageFileVisitor visitor,
        void* const context) override
    {
        if (fail_list || visitor == nullptr) {
            return false;
        }
        for (const auto& file : files) {
            if (!visitor(context, file.first.c_str())) {
                break;
            }
        }
        return true;
    }

    bool fail_list = false;
    std::map<std::string, std::vector<std::uint8_t>> files;
};

void test_start_runs_recovery_before_ready()
{
    FakeStorageFileSystem filesystem;
    pf_storage::StorageWorker worker(filesystem);

    const pf_storage::StorageWorkerResult result = worker.start();
    TEST_ASSERT_TRUE(result.ok());
    TEST_ASSERT_TRUE(worker.started());
    TEST_ASSERT_TRUE(worker.ready());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_storage::RecoveryAction::no_change),
        static_cast<int>(result.recovery.action));
    TEST_ASSERT_EQUAL_STRING("none", pf_storage::to_string(result.error));

    const pf_storage::StorageWorkerResult repeated = worker.start();
    TEST_ASSERT_FALSE(repeated.ok());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_storage::StorageWorkerError::already_started),
        static_cast<int>(repeated.error));
    TEST_ASSERT_TRUE(worker.last_result().ok());
}

void test_recovery_failure_keeps_worker_not_ready()
{
    FakeStorageFileSystem filesystem;
    filesystem.fail_list = true;
    pf_storage::StorageWorker worker(filesystem);

    const pf_storage::StorageWorkerResult result = worker.start();
    TEST_ASSERT_FALSE(result.ok());
    TEST_ASSERT_TRUE(worker.started());
    TEST_ASSERT_FALSE(worker.ready());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_storage::StorageWorkerError::recovery_failed),
        static_cast<int>(result.error));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_storage::RecoveryError::list_failed),
        static_cast<int>(result.recovery.error));
}

}  // namespace

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_start_runs_recovery_before_ready);
    RUN_TEST(test_recovery_failure_keeps_worker_not_ready);
    return UNITY_END();
}
