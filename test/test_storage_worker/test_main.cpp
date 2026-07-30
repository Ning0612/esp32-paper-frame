#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <unity.h>

#include "pf_storage/storage_worker.hpp"

extern "C" void setUp() {}
extern "C" void tearDown() {}

namespace {

class FakeStorageFileSystem final : public pf_storage::StorageFileSystem {
public:
    struct WriteHandle {
        std::string path;
        std::vector<std::uint8_t> bytes;
    };

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
        const char* const path,
        pf_storage::StorageFileHandle& handle) override
    {
        handle.opaque = nullptr;
        if (path == nullptr || active_write != nullptr) {
            return false;
        }
        active_write = std::make_unique<WriteHandle>();
        active_write->path = path;
        handle.opaque = active_write.get();
        return true;
    }

    bool write(
        pf_storage::StorageFileHandle& handle,
        const std::uint8_t* const data,
        const std::size_t length) override
    {
        if (handle.opaque == nullptr ||
            active_write.get() != handle.opaque ||
            (data == nullptr && length != 0U)) {
            return false;
        }
        active_write->bytes.insert(
            active_write->bytes.end(),
            data,
            data + length);
        return true;
    }

    bool close_write(pf_storage::StorageFileHandle& handle) override
    {
        if (handle.opaque == nullptr) {
            return true;
        }
        if (active_write.get() != handle.opaque) {
            return false;
        }
        files[active_write->path] = std::move(active_write->bytes);
        active_write.reset();
        handle.opaque = nullptr;
        return true;
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
        if (fail_read) {
            return false;
        }
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
        return !fail_close;
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
    bool fail_read = false;
    bool fail_close = false;
    std::unique_ptr<WriteHandle> active_write;
    std::map<std::string, std::vector<std::uint8_t>> files;
};

void seed_catalog(
    FakeStorageFileSystem& filesystem,
    const pf_storage::Catalog& catalog)
{
    std::vector<std::uint8_t> bytes(pf_storage::kCatalogMaxBytes, 0U);
    std::size_t written = 0U;
    pf_storage::CatalogError error = pf_storage::CatalogError::none;
    TEST_ASSERT_TRUE(pf_storage::serialize_catalog(
        catalog,
        bytes.data(),
        bytes.size(),
        written,
        error));
    bytes.resize(written);
    filesystem.files["/images/.catalog.pfc1"] = std::move(bytes);
}

void write_u16(
    std::vector<std::uint8_t>& output,
    const std::size_t offset,
    const std::uint16_t value)
{
    output[offset] = static_cast<std::uint8_t>(value & 0xFFU);
    output[offset + 1U] = static_cast<std::uint8_t>(value >> 8U);
}

void write_u32(
    std::vector<std::uint8_t>& output,
    const std::size_t offset,
    const std::uint32_t value)
{
    for (std::uint8_t index = 0U; index < 4U; ++index) {
        output[offset + index] =
            static_cast<std::uint8_t>(value >> (index * 8U));
    }
}

std::vector<std::uint8_t> make_pfr1(const char* const filename)
{
    const std::size_t filename_length = std::strlen(filename);
    const std::uint16_t width = pf_display::kPanelWidth;
    const std::uint16_t height = pf_display::kLandscapeImageHeight;
    const std::uint32_t payload_length =
        pf_image::expected_payload_length(width, height);
    std::vector<std::uint8_t> output(
        pf_image::kPfr1HeaderSize + filename_length + payload_length,
        0U);
    output[0] = 'P';
    output[1] = 'F';
    output[2] = 'R';
    output[3] = '1';
    output[4] = 1U;
    output[5] = static_cast<std::uint8_t>(pf_image::kPfr1HeaderSize);
    write_u16(output, 6U, 0U);
    write_u16(output, 8U, width);
    write_u16(output, 10U, height);
    output[12] = static_cast<std::uint8_t>(pf_image::Orientation::landscape);
    output[13] = pf_display::kPaletteVersion;
    output[14] = static_cast<std::uint8_t>(pf_image::Dithering::nearest);
    output[15] = 0U;
    write_u32(output, 16U, payload_length);
    write_u16(output, 20U, static_cast<std::uint16_t>(filename_length));
    write_u16(output, 22U, 0U);
    std::memcpy(
        output.data() + pf_image::kPfr1HeaderSize,
        filename,
        filename_length);
    const std::size_t payload_offset =
        pf_image::kPfr1HeaderSize + filename_length;
    std::fill(
        output.begin() + static_cast<std::ptrdiff_t>(payload_offset),
        output.end(),
        0x11U);
    write_u32(
        output,
        24U,
        pf_image::crc32(output.data() + payload_offset, payload_length));
    write_u32(output, 28U, pf_image::crc32(output.data(), 24U));
    return output;
}

struct VectorReader {
    const std::vector<std::uint8_t>* bytes = nullptr;
    std::size_t offset = 0U;
    std::size_t chunk = 257U;
};

pf_storage::StorageReadResult read_vector(
    void* const context,
    std::uint8_t* const buffer,
    const std::size_t capacity,
    std::size_t& bytes_read)
{
    auto* const reader = static_cast<VectorReader*>(context);
    bytes_read = 0U;
    if (reader == nullptr || reader->bytes == nullptr ||
        buffer == nullptr || capacity == 0U) {
        return pf_storage::StorageReadResult::error;
    }
    if (reader->offset == reader->bytes->size()) {
        return pf_storage::StorageReadResult::eof;
    }
    const std::size_t remaining = reader->bytes->size() - reader->offset;
    const std::size_t amount = std::min(
        std::min(reader->chunk, capacity),
        remaining);
    std::memcpy(
        buffer,
        reader->bytes->data() + reader->offset,
        amount);
    reader->offset += amount;
    bytes_read = amount;
    return pf_storage::StorageReadResult::data;
}

struct ImageCollector {
    std::vector<std::uint8_t> bytes;
    bool accept = true;
};

bool collect_image_bytes(
    void* const context,
    const std::uint8_t* const data,
    const std::size_t length)
{
    auto* const collector = static_cast<ImageCollector*>(context);
    if (collector == nullptr || data == nullptr || length == 0U) {
        return false;
    }
    if (!collector->accept) {
        return false;
    }
    collector->bytes.insert(
        collector->bytes.end(),
        data,
        data + length);
    return true;
}

pf_storage::Catalog make_download_catalog(
    const char* const name,
    const std::size_t file_bytes)
{
    pf_storage::Catalog catalog{};
    TEST_ASSERT_TRUE(pf_storage::initialize_catalog(catalog));
    pf_storage::CatalogEntry entry{};
    entry.file_bytes = static_cast<std::uint32_t>(file_bytes);
    entry.payload_bytes = static_cast<std::uint32_t>(
        pf_image::expected_payload_length(
            pf_display::kPanelWidth,
            pf_display::kLandscapeImageHeight));
    entry.width = pf_display::kPanelWidth;
    entry.height = pf_display::kLandscapeImageHeight;
    entry.orientation = pf_image::Orientation::landscape;
    entry.name_length = static_cast<std::uint16_t>(std::strlen(name));
    std::memcpy(entry.name, name, entry.name_length + 1U);
    std::uint32_t assigned_id = 0U;
    pf_storage::CatalogError error = pf_storage::CatalogError::none;
    TEST_ASSERT_TRUE(pf_storage::add_catalog_entry(
        catalog,
        entry,
        assigned_id,
        error));
    return catalog;
}

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

void test_stream_image_reads_only_catalogued_file()
{
    constexpr char kName[] = "demo.pfr1";
    const std::size_t file_bytes =
        pf_image::kPfr1HeaderSize +
        std::strlen(kName) +
        pf_image::expected_payload_length(
            pf_display::kPanelWidth,
            pf_display::kLandscapeImageHeight);
    FakeStorageFileSystem filesystem;
    const std::vector<std::uint8_t> image(file_bytes, 0xA5U);
    filesystem.files["/images/demo.pfr1"] = image;
    seed_catalog(
        filesystem,
        make_download_catalog(kName, file_bytes));
    pf_storage::StorageWorker worker(filesystem);
    TEST_ASSERT_TRUE(worker.start().ok());

    ImageCollector collector{};
    const pf_storage::ImageStreamResult result = worker.stream_image(
        kName,
        std::strlen(kName),
        collect_image_bytes,
        &collector);
    TEST_ASSERT_TRUE(result.ok());
    TEST_ASSERT_EQUAL_UINT(file_bytes, result.bytes_sent);
    TEST_ASSERT_EQUAL_UINT(image.size(), collector.bytes.size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(
        image.data(),
        collector.bytes.data(),
        image.size());
}

void test_stream_image_fails_closed_for_missing_or_rejected_reads()
{
    FakeStorageFileSystem filesystem;
    pf_storage::StorageWorker worker(filesystem);
    TEST_ASSERT_TRUE(worker.start().ok());
    ImageCollector collector{};
    pf_storage::ImageStreamResult result = worker.stream_image(
        "missing.pfr1",
        std::strlen("missing.pfr1"),
        collect_image_bytes,
        &collector);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_storage::ImageStreamError::not_found),
        static_cast<int>(result.error));

    constexpr char kName[] = "reject.pfr1";
    const std::size_t file_bytes =
        pf_image::kPfr1HeaderSize +
        std::strlen(kName) +
        pf_image::expected_payload_length(
            pf_display::kPanelWidth,
            pf_display::kLandscapeImageHeight);
    filesystem.files["/images/reject.pfr1"] =
        std::vector<std::uint8_t>(file_bytes, 0x5AU);
    seed_catalog(
        filesystem,
        make_download_catalog(kName, file_bytes));
    pf_storage::StorageWorker second_worker(filesystem);
    TEST_ASSERT_TRUE(second_worker.start().ok());
    collector.accept = false;
    result = second_worker.stream_image(
        kName,
        std::strlen(kName),
        collect_image_bytes,
        &collector);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_storage::ImageStreamError::visitor_failed),
        static_cast<int>(result.error));
}

void test_stream_image_rejects_size_and_filesystem_failures()
{
    constexpr char kName[] = "integrity.pfr1";
    const std::size_t kFileBytes =
        pf_image::kPfr1HeaderSize +
        std::strlen(kName) +
        pf_image::expected_payload_length(
            pf_display::kPanelWidth,
            pf_display::kLandscapeImageHeight);

    {
        FakeStorageFileSystem filesystem;
        filesystem.files["/images/integrity.pfr1"] =
            std::vector<std::uint8_t>(kFileBytes - 1U, 0x11U);
        seed_catalog(
            filesystem,
            make_download_catalog(kName, kFileBytes));
        pf_storage::StorageWorker worker(filesystem);
        TEST_ASSERT_TRUE(worker.start().ok());
        ImageCollector collector{};
        const pf_storage::ImageStreamResult result = worker.stream_image(
            kName,
            std::strlen(kName),
            collect_image_bytes,
            &collector);
        TEST_ASSERT_EQUAL_INT(
            static_cast<int>(pf_storage::ImageStreamError::read_failed),
            static_cast<int>(result.error));
    }

    {
        FakeStorageFileSystem filesystem;
        filesystem.files["/images/integrity.pfr1"] =
            std::vector<std::uint8_t>(kFileBytes + 1U, 0x22U);
        seed_catalog(
            filesystem,
            make_download_catalog(kName, kFileBytes));
        pf_storage::StorageWorker worker(filesystem);
        TEST_ASSERT_TRUE(worker.start().ok());
        ImageCollector collector{};
        const pf_storage::ImageStreamResult result = worker.stream_image(
            kName,
            std::strlen(kName),
            collect_image_bytes,
            &collector);
        TEST_ASSERT_EQUAL_INT(
            static_cast<int>(pf_storage::ImageStreamError::read_failed),
            static_cast<int>(result.error));
    }

    {
        FakeStorageFileSystem filesystem;
        filesystem.files["/images/integrity.pfr1"] =
            std::vector<std::uint8_t>(kFileBytes, 0x33U);
        seed_catalog(
            filesystem,
            make_download_catalog(kName, kFileBytes));
        pf_storage::StorageWorker worker(filesystem);
        TEST_ASSERT_TRUE(worker.start().ok());
        filesystem.fail_read = true;
        ImageCollector collector{};
        const pf_storage::ImageStreamResult result = worker.stream_image(
            kName,
            std::strlen(kName),
            collect_image_bytes,
            &collector);
        TEST_ASSERT_EQUAL_INT(
            static_cast<int>(pf_storage::ImageStreamError::read_failed),
            static_cast<int>(result.error));
    }

    {
        FakeStorageFileSystem filesystem;
        filesystem.files["/images/integrity.pfr1"] =
            std::vector<std::uint8_t>(kFileBytes, 0x44U);
        seed_catalog(
            filesystem,
            make_download_catalog(kName, kFileBytes));
        pf_storage::StorageWorker worker(filesystem);
        TEST_ASSERT_TRUE(worker.start().ok());
        filesystem.fail_close = true;
        ImageCollector collector{};
        const pf_storage::ImageStreamResult result = worker.stream_image(
            kName,
            std::strlen(kName),
            collect_image_bytes,
            &collector);
        TEST_ASSERT_EQUAL_INT(
            static_cast<int>(pf_storage::ImageStreamError::close_failed),
            static_cast<int>(result.error));
    }
}

void test_store_image_updates_worker_catalog_after_commit()
{
    FakeStorageFileSystem filesystem;
    pf_storage::StorageWorker worker(filesystem);
    TEST_ASSERT_TRUE(worker.start().ok());
    const std::vector<std::uint8_t> image = make_pfr1("worker-upload.pfr1");
    VectorReader vector_reader{&image};
    const pf_storage::StorageStreamReader reader{
        read_vector,
        &vector_reader,
    };

    const pf_storage::ImageStoreResult result = worker.store_image(
        reader,
        image.size());
    TEST_ASSERT_TRUE(result.ok());
    TEST_ASSERT_EQUAL_UINT32(1U, result.assigned_id);
    TEST_ASSERT_EQUAL_UINT(image.size(), result.bytes_received);
    TEST_ASSERT_FALSE(worker.operation_busy());

    pf_storage::CatalogEntry entry{};
    TEST_ASSERT_TRUE(worker.find_catalog_entry_by_name(
        "worker-upload.pfr1",
        std::strlen("worker-upload.pfr1"),
        entry));
    TEST_ASSERT_EQUAL_UINT32(1U, entry.id);
    TEST_ASSERT_TRUE(filesystem.exists("/images/worker-upload.pfr1"));
}

}  // namespace

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_start_runs_recovery_before_ready);
    RUN_TEST(test_recovery_failure_keeps_worker_not_ready);
    RUN_TEST(test_stream_image_reads_only_catalogued_file);
    RUN_TEST(test_stream_image_fails_closed_for_missing_or_rejected_reads);
    RUN_TEST(test_stream_image_rejects_size_and_filesystem_failures);
    RUN_TEST(test_store_image_updates_worker_catalog_after_commit);
    return UNITY_END();
}
