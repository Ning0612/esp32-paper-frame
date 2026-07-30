#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <unity.h>

#include "pf_storage/image_store.hpp"
#include "pf_storage/recovery.hpp"

extern "C" void setUp() {}
extern "C" void tearDown() {}

namespace {

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
    bool fail = false;
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
        buffer == nullptr || capacity == 0U || reader->fail) {
        return pf_storage::StorageReadResult::error;
    }
    if (reader->offset == reader->bytes->size()) {
        return pf_storage::StorageReadResult::eof;
    }
    const std::size_t remaining = reader->bytes->size() - reader->offset;
    const std::size_t amount = std::min(
        std::min(reader->chunk, capacity),
        remaining);
    std::memcpy(buffer, reader->bytes->data() + reader->offset, amount);
    reader->offset += amount;
    bytes_read = amount;
    return pf_storage::StorageReadResult::data;
}

class FakeStorageFileSystem final : public pf_storage::StorageFileSystem {
public:
    struct OpenFile {
        std::string path;
        std::vector<std::uint8_t> bytes;
    };

    struct OpenRead {
        std::vector<std::uint8_t> bytes;
        std::size_t offset = 0U;
    };

    std::uint64_t available = 2U * 1024U * 1024U;
    bool fail_open = false;
    bool fail_write = false;
    bool fail_close = false;
    bool fail_list = false;
    std::string fail_close_path;
    std::string corrupt_on_close_path;
    std::string fail_rename_from;
    std::map<std::string, std::vector<std::uint8_t>> files;
    std::unique_ptr<OpenFile> active;
    std::unique_ptr<OpenRead> active_read;

    std::uint64_t free_bytes() const override
    {
        return available;
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
        if (from == nullptr || to == nullptr || from == fail_rename_from) {
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
        if (path == nullptr || fail_open || active != nullptr) {
            return false;
        }
        active = std::make_unique<OpenFile>();
        active->path = path;
        handle.opaque = active.get();
        return true;
    }

    bool write(
        pf_storage::StorageFileHandle& handle,
        const std::uint8_t* const data,
        const std::size_t length) override
    {
        if (handle.opaque == nullptr || active.get() != handle.opaque ||
            (data == nullptr && length != 0U) || fail_write) {
            return false;
        }
        active->bytes.insert(active->bytes.end(), data, data + length);
        return true;
    }

    bool close_write(pf_storage::StorageFileHandle& handle) override
    {
        if (handle.opaque == nullptr) {
            return true;
        }
        if (active.get() != handle.opaque) {
            return false;
        }
        const bool success =
            !fail_close && active->path != fail_close_path;
        files[active->path] = std::move(active->bytes);
        if (active->path == corrupt_on_close_path &&
            !files[active->path].empty()) {
            files[active->path][0] ^= 0x01U;
        }
        active.reset();
        handle.opaque = nullptr;
        return success;
    }

    bool open_read(
        const char* const path,
        pf_storage::StorageFileHandle& handle) override
    {
        handle.opaque = nullptr;
        if (path == nullptr || active_read != nullptr) {
            return false;
        }
        const auto found = files.find(path);
        if (found == files.end()) {
            return false;
        }
        active_read = std::make_unique<OpenRead>();
        active_read->bytes = found->second;
        handle.opaque = active_read.get();
        return true;
    }

    bool read(
        pf_storage::StorageFileHandle& handle,
        std::uint8_t* const buffer,
        const std::size_t capacity,
        std::size_t& bytes_read) override
    {
        bytes_read = 0U;
        if (handle.opaque == nullptr || active_read.get() != handle.opaque ||
            buffer == nullptr || capacity == 0U) {
            return false;
        }
        if (active_read->offset == active_read->bytes.size()) {
            return true;
        }
        const std::size_t remaining =
            active_read->bytes.size() - active_read->offset;
        const std::size_t amount = std::min(remaining, capacity);
        std::memcpy(
            buffer,
            active_read->bytes.data() + active_read->offset,
            amount);
        active_read->offset += amount;
        bytes_read = amount;
        return true;
    }

    bool close_read(pf_storage::StorageFileHandle& handle) override
    {
        if (handle.opaque == nullptr) {
            return true;
        }
        if (active_read.get() != handle.opaque) {
            return false;
        }
        active_read.reset();
        handle.opaque = nullptr;
        return true;
    }

    bool for_each_file(
        const char* const directory,
        pf_storage::StorageFileVisitor visitor,
        void* const context) override
    {
        if (directory == nullptr || visitor == nullptr) {
            return false;
        }
        if (fail_list) {
            return false;
        }
        const std::string prefix =
            std::string(directory) + "/";
        for (const auto& file : files) {
            if (file.first.rfind(prefix, 0U) != 0U ||
                file.first.find('/', prefix.size()) != std::string::npos) {
                continue;
            }
            if (!visitor(context, file.first.c_str())) {
                break;
            }
        }
        return true;
    }
};

pf_storage::Catalog make_existing_catalog(const char* const filename)
{
    pf_storage::Catalog catalog{};
    TEST_ASSERT_TRUE(pf_storage::initialize_catalog(catalog));
    pf_storage::CatalogError error = pf_storage::CatalogError::none;
    pf_storage::CatalogEntry entry{};
    entry.width = pf_display::kPanelWidth;
    entry.height = pf_display::kLandscapeImageHeight;
    entry.payload_bytes = pf_image::expected_payload_length(
        entry.width,
        entry.height);
    entry.file_bytes = static_cast<std::uint32_t>(
        pf_image::kPfr1HeaderSize + std::strlen(filename) +
        entry.payload_bytes);
    entry.name_length = static_cast<std::uint16_t>(std::strlen(filename));
    std::memcpy(entry.name, filename, entry.name_length);
    std::uint32_t id = 0U;
    TEST_ASSERT_TRUE(pf_storage::add_catalog_entry(
        catalog,
        entry,
        id,
        error));
    return catalog;
}

pf_storage::Catalog make_catalog_with_count(const std::size_t count)
{
    pf_storage::Catalog catalog{};
    TEST_ASSERT_TRUE(pf_storage::initialize_catalog(catalog));
    for (std::size_t index = 0U; index < count; ++index) {
        const std::string filename =
            "seed-" + std::to_string(index) + ".pfr1";
        pf_storage::CatalogEntry entry{};
        entry.width = pf_display::kPanelWidth;
        entry.height = pf_display::kLandscapeImageHeight;
        entry.payload_bytes = pf_image::expected_payload_length(
            entry.width,
            entry.height);
        entry.file_bytes = static_cast<std::uint32_t>(
            pf_image::kPfr1HeaderSize + filename.size() +
            entry.payload_bytes);
        entry.name_length = static_cast<std::uint16_t>(filename.size());
        std::memcpy(entry.name, filename.data(), entry.name_length);
        std::uint32_t id = 0U;
        pf_storage::CatalogError error = pf_storage::CatalogError::none;
        TEST_ASSERT_TRUE(pf_storage::add_catalog_entry(
            catalog,
            entry,
            id,
            error));
    }
    return catalog;
}

pf_storage::Catalog append_catalog_entry(
    const pf_storage::Catalog& base,
    const char* const filename)
{
    pf_storage::Catalog candidate = base;
    pf_storage::CatalogEntry entry{};
    entry.width = pf_display::kPanelWidth;
    entry.height = pf_display::kLandscapeImageHeight;
    entry.payload_bytes = pf_image::expected_payload_length(
        entry.width,
        entry.height);
    entry.file_bytes = static_cast<std::uint32_t>(
        pf_image::kPfr1HeaderSize + std::strlen(filename) +
        entry.payload_bytes);
    entry.name_length = static_cast<std::uint16_t>(std::strlen(filename));
    std::memcpy(entry.name, filename, entry.name_length);
    std::uint32_t id = 0U;
    pf_storage::CatalogError error = pf_storage::CatalogError::none;
    TEST_ASSERT_TRUE(pf_storage::add_catalog_entry(
        candidate,
        entry,
        id,
        error));
    return candidate;
}

pf_storage::ImageStoreResult upload(
    FakeStorageFileSystem& filesystem,
    const pf_storage::Catalog& current_catalog,
    pf_storage::Catalog& updated_catalog,
    const std::vector<std::uint8_t>& image,
    const std::size_t content_length,
    const std::size_t chunk = 257U)
{
    VectorReader vector_reader{&image, 0U, chunk, false};
    const pf_storage::StorageStreamReader reader{
        read_vector,
        &vector_reader,
    };
    std::array<std::uint8_t, pf_storage::kCatalogMaxBytes> catalog_buffer{};
    return pf_storage::store_image_transactionally(
        filesystem,
        current_catalog,
        updated_catalog,
        catalog_buffer.data(),
        catalog_buffer.size(),
        reader,
        content_length);
}

void seed_catalog_at(
    FakeStorageFileSystem& filesystem,
    const char* const path,
    const pf_storage::Catalog& catalog)
{
    std::array<std::uint8_t, pf_storage::kCatalogMaxBytes> buffer{};
    std::size_t written = 0U;
    pf_storage::CatalogError error = pf_storage::CatalogError::none;
    TEST_ASSERT_TRUE(pf_storage::serialize_catalog(
        catalog,
        buffer.data(),
        buffer.size(),
        written,
        error));
    filesystem.files[path] = std::vector<std::uint8_t>(
        buffer.begin(),
        buffer.begin() + static_cast<std::ptrdiff_t>(written));
}

void seed_catalog_file(
    FakeStorageFileSystem& filesystem,
    const pf_storage::Catalog& catalog)
{
    seed_catalog_at(filesystem, "/images/.catalog.pfc1", catalog);
}

void seed_image_file(
    FakeStorageFileSystem& filesystem,
    const char* const filename,
    const char* const suffix = "")
{
    const std::vector<std::uint8_t> image = make_pfr1(filename);
    filesystem.files[
        std::string("/images/") + filename + suffix] = image;
}

void assert_no_transaction_files(const FakeStorageFileSystem& filesystem)
{
    TEST_ASSERT_FALSE(filesystem.exists("/images/.upload.part"));
    TEST_ASSERT_FALSE(filesystem.exists("/images/.catalog.pfc1.part"));
    TEST_ASSERT_FALSE(filesystem.exists("/images/.catalog.pfc1.bak"));
}

void test_valid_upload_commits_image_and_catalog()
{
    FakeStorageFileSystem filesystem;
    const std::vector<std::uint8_t> image = make_pfr1("new.pfr1");
    pf_storage::Catalog current{};
    TEST_ASSERT_TRUE(pf_storage::initialize_catalog(current));
    pf_storage::Catalog updated{};
    const pf_storage::ImageStoreResult result = upload(
        filesystem,
        current,
        updated,
        image,
        image.size());
    TEST_ASSERT_TRUE(result.ok());
    TEST_ASSERT_EQUAL_UINT32(1U, result.assigned_id);
    TEST_ASSERT_EQUAL_UINT32(image.size(), result.bytes_received);
    TEST_ASSERT_TRUE(filesystem.exists("/images/new.pfr1"));
    TEST_ASSERT_TRUE(filesystem.exists("/images/.catalog.pfc1"));
    TEST_ASSERT_EQUAL_MEMORY(
        image.data(),
        filesystem.files.at("/images/new.pfr1").data(),
        image.size());
    TEST_ASSERT_EQUAL_UINT16(1U, updated.count);
    TEST_ASSERT_EQUAL_STRING("new.pfr1", updated.entries[0].name);
    assert_no_transaction_files(filesystem);
}

void test_upload_preflights_space_before_reading()
{
    FakeStorageFileSystem filesystem;
    const std::vector<std::uint8_t> image = make_pfr1("space.pfr1");
    filesystem.available = image.size() + pf_storage::kCatalogMaxBytes - 1U;
    pf_storage::Catalog current{};
    TEST_ASSERT_TRUE(pf_storage::initialize_catalog(current));
    pf_storage::Catalog updated{};
    const pf_storage::ImageStoreResult result = upload(
        filesystem,
        current,
        updated,
        image,
        image.size());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_storage::ImageStoreError::no_space),
        static_cast<int>(result.error));
    TEST_ASSERT_TRUE(filesystem.files.empty());
}

void test_invalid_or_incomplete_upload_leaves_no_image()
{
    FakeStorageFileSystem filesystem;
    std::vector<std::uint8_t> invalid = make_pfr1("bad.pfr1");
    invalid[28] ^= 0x01U;
    pf_storage::Catalog current{};
    TEST_ASSERT_TRUE(pf_storage::initialize_catalog(current));
    pf_storage::Catalog updated{};
    pf_storage::ImageStoreResult result = upload(
        filesystem,
        current,
        updated,
        invalid,
        invalid.size());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_storage::ImageStoreError::invalid_image),
        static_cast<int>(result.error));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_image::ValidationError::header_crc_mismatch),
        static_cast<int>(result.validation_error));
    TEST_ASSERT_TRUE(filesystem.files.empty());

    FakeStorageFileSystem incomplete_filesystem;
    const std::vector<std::uint8_t> complete = make_pfr1("short.pfr1");
    std::vector<std::uint8_t> truncated = complete;
    truncated.pop_back();
    result = upload(
        incomplete_filesystem,
        current,
        updated,
        truncated,
        complete.size());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_storage::ImageStoreError::invalid_image),
        static_cast<int>(result.error));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_image::ValidationError::incomplete),
        static_cast<int>(result.validation_error));
    TEST_ASSERT_TRUE(incomplete_filesystem.files.empty());
}

void test_duplicate_name_is_rejected_without_replacing_existing_catalog()
{
    FakeStorageFileSystem filesystem;
    const pf_storage::Catalog current = make_existing_catalog("same.pfr1");
    seed_catalog_file(filesystem, current);
    const std::vector<std::uint8_t> image = make_pfr1("same.pfr1");
    pf_storage::Catalog updated{};
    const pf_storage::ImageStoreResult result = upload(
        filesystem,
        current,
        updated,
        image,
        image.size());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_storage::ImageStoreError::image_conflict),
        static_cast<int>(result.error));
    TEST_ASSERT_TRUE(filesystem.exists("/images/.catalog.pfc1"));
    TEST_ASSERT_FALSE(filesystem.exists("/images/same.pfr1"));
    assert_no_transaction_files(filesystem);
}

void test_catalog_readback_rejects_silent_corruption()
{
    FakeStorageFileSystem filesystem;
    filesystem.corrupt_on_close_path = "/images/.catalog.pfc1.part";
    const std::vector<std::uint8_t> image = make_pfr1("corrupt.pfr1");
    pf_storage::Catalog current{};
    TEST_ASSERT_TRUE(pf_storage::initialize_catalog(current));
    pf_storage::Catalog updated{};
    const pf_storage::ImageStoreResult result = upload(
        filesystem,
        current,
        updated,
        image,
        image.size());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_storage::ImageStoreError::catalog_invalid),
        static_cast<int>(result.error));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_storage::CatalogError::invalid_magic),
        static_cast<int>(result.catalog_error));
    TEST_ASSERT_FALSE(filesystem.exists("/images/corrupt.pfr1"));
    assert_no_transaction_files(filesystem);
}

void test_maximum_catalog_size_reads_back_at_exact_buffer_capacity()
{
    FakeStorageFileSystem filesystem;
    const pf_storage::Catalog current = make_catalog_with_count(
        pf_storage::kCatalogMaxEntries - 1U);
    const std::vector<std::uint8_t> image = make_pfr1("last.pfr1");
    pf_storage::Catalog updated{};
    const pf_storage::ImageStoreResult result = upload(
        filesystem,
        current,
        updated,
        image,
        image.size());
    TEST_ASSERT_TRUE(result.ok());
    TEST_ASSERT_EQUAL_UINT16(
        static_cast<std::uint16_t>(pf_storage::kCatalogMaxEntries),
        updated.count);
    TEST_ASSERT_TRUE(filesystem.exists("/images/last.pfr1"));
    assert_no_transaction_files(filesystem);
}

void test_catalog_output_alias_is_rejected()
{
    FakeStorageFileSystem filesystem;
    pf_storage::Catalog catalog{};
    TEST_ASSERT_TRUE(pf_storage::initialize_catalog(catalog));
    const std::vector<std::uint8_t> image = make_pfr1("alias.pfr1");
    const pf_storage::ImageStoreResult result = upload(
        filesystem,
        catalog,
        catalog,
        image,
        image.size());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_storage::ImageStoreError::invalid_argument),
        static_cast<int>(result.error));
    TEST_ASSERT_TRUE(filesystem.files.empty());
}

void test_catalog_close_failure_has_a_distinct_error()
{
    FakeStorageFileSystem filesystem;
    filesystem.fail_close_path = "/images/.catalog.pfc1.part";
    pf_storage::Catalog current{};
    TEST_ASSERT_TRUE(pf_storage::initialize_catalog(current));
    pf_storage::Catalog updated{};
    const std::vector<std::uint8_t> image = make_pfr1("close.pfr1");
    const pf_storage::ImageStoreResult result = upload(
        filesystem,
        current,
        updated,
        image,
        image.size());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_storage::ImageStoreError::close_failed),
        static_cast<int>(result.error));
    TEST_ASSERT_FALSE(filesystem.exists("/images/close.pfr1"));
    assert_no_transaction_files(filesystem);
}

void test_recovery_promotes_complete_catalog_and_image_parts()
{
    FakeStorageFileSystem filesystem;
    pf_storage::Catalog empty{};
    TEST_ASSERT_TRUE(pf_storage::initialize_catalog(empty));
    seed_catalog_file(filesystem, empty);
    const pf_storage::Catalog candidate = append_catalog_entry(
        empty,
        "next.pfr1");
    seed_catalog_at(
        filesystem,
        "/images/.catalog.pfc1.part",
        candidate);
    seed_image_file(filesystem, "next.pfr1", ".part");

    pf_storage::RecoveryWorkspace workspace{};
    const pf_storage::RecoveryResult result =
        pf_storage::recover_image_transactions(filesystem, workspace);
    TEST_ASSERT_TRUE(result.ok());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_storage::RecoveryAction::promoted_candidate),
        static_cast<int>(result.action));
    TEST_ASSERT_TRUE(result.has_catalog);
    TEST_ASSERT_EQUAL_UINT16(1U, workspace.recovered.count);
    TEST_ASSERT_TRUE(filesystem.exists("/images/next.pfr1"));
    assert_no_transaction_files(filesystem);
    TEST_ASSERT_FALSE(filesystem.exists("/images/next.pfr1.part"));
}

void test_recovery_finishes_after_image_rename_and_backup_creation()
{
    FakeStorageFileSystem filesystem;
    const pf_storage::Catalog old_catalog = make_existing_catalog("old.pfr1");
    const pf_storage::Catalog candidate = append_catalog_entry(
        old_catalog,
        "next.pfr1");
    seed_catalog_at(
        filesystem,
        "/images/.catalog.pfc1.bak",
        old_catalog);
    seed_catalog_at(
        filesystem,
        "/images/.catalog.pfc1.part",
        candidate);
    seed_image_file(filesystem, "next.pfr1");

    pf_storage::RecoveryWorkspace workspace{};
    const pf_storage::RecoveryResult result =
        pf_storage::recover_image_transactions(filesystem, workspace);
    TEST_ASSERT_TRUE(result.ok());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_storage::RecoveryAction::promoted_candidate),
        static_cast<int>(result.action));
    TEST_ASSERT_EQUAL_UINT16(2U, workspace.recovered.count);
    TEST_ASSERT_TRUE(filesystem.exists("/images/next.pfr1"));
    TEST_ASSERT_FALSE(filesystem.exists("/images/.catalog.pfc1.bak"));
    assert_no_transaction_files(filesystem);
}

void test_recovery_restores_backup_when_candidate_is_corrupt()
{
    FakeStorageFileSystem filesystem;
    const pf_storage::Catalog old_catalog = make_existing_catalog("old.pfr1");
    seed_catalog_at(
        filesystem,
        "/images/.catalog.pfc1.bak",
        old_catalog);
    filesystem.files["/images/.catalog.pfc1.part"] = {0x00U, 0x01U};
    seed_image_file(filesystem, "next.pfr1", ".part");

    pf_storage::RecoveryWorkspace workspace{};
    const pf_storage::RecoveryResult result =
        pf_storage::recover_image_transactions(filesystem, workspace);
    TEST_ASSERT_TRUE(result.ok());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_storage::RecoveryAction::restored_backup),
        static_cast<int>(result.action));
    TEST_ASSERT_TRUE(result.has_catalog);
    TEST_ASSERT_EQUAL_UINT16(1U, workspace.recovered.count);
    TEST_ASSERT_TRUE(filesystem.exists("/images/.catalog.pfc1"));
    TEST_ASSERT_FALSE(filesystem.exists("/images/.catalog.pfc1.bak"));
    TEST_ASSERT_FALSE(filesystem.exists("/images/next.pfr1.part"));
    assert_no_transaction_files(filesystem);
}

void test_recovery_keeps_canonical_and_cleans_stale_backup_parts()
{
    FakeStorageFileSystem filesystem;
    const pf_storage::Catalog catalog = make_existing_catalog("old.pfr1");
    seed_catalog_file(filesystem, catalog);
    seed_catalog_at(
        filesystem,
        "/images/.catalog.pfc1.bak",
        catalog);
    filesystem.files["/images/.upload.part"] = {0x01U};
    seed_image_file(filesystem, "orphan.pfr1", ".part");

    pf_storage::RecoveryWorkspace workspace{};
    const pf_storage::RecoveryResult result =
        pf_storage::recover_image_transactions(filesystem, workspace);
    TEST_ASSERT_TRUE(result.ok());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_storage::RecoveryAction::no_change),
        static_cast<int>(result.action));
    TEST_ASSERT_TRUE(result.has_catalog);
    TEST_ASSERT_EQUAL_UINT16(1U, workspace.recovered.count);
    TEST_ASSERT_FALSE(filesystem.exists("/images/.catalog.pfc1.bak"));
    TEST_ASSERT_FALSE(filesystem.exists("/images/orphan.pfr1.part"));
    TEST_ASSERT_FALSE(filesystem.exists("/images/.upload.part"));
}

void test_recovery_rejects_non_append_candidate_without_mutation()
{
    FakeStorageFileSystem filesystem;
    const pf_storage::Catalog old_catalog = make_existing_catalog("old.pfr1");
    const pf_storage::Catalog unrelated = make_existing_catalog("other.pfr1");
    seed_catalog_file(filesystem, old_catalog);
    seed_catalog_at(
        filesystem,
        "/images/.catalog.pfc1.part",
        unrelated);

    pf_storage::RecoveryWorkspace workspace{};
    const pf_storage::RecoveryResult result =
        pf_storage::recover_image_transactions(filesystem, workspace);
    TEST_ASSERT_FALSE(result.ok());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_storage::RecoveryError::ambiguous),
        static_cast<int>(result.error));
    TEST_ASSERT_TRUE(filesystem.exists("/images/.catalog.pfc1"));
    TEST_ASSERT_TRUE(filesystem.exists("/images/.catalog.pfc1.part"));
}

void test_recovery_rename_failure_removes_uncommitted_new_image()
{
    FakeStorageFileSystem filesystem;
    const pf_storage::Catalog old_catalog = make_existing_catalog("old.pfr1");
    const pf_storage::Catalog candidate = append_catalog_entry(
        old_catalog,
        "next.pfr1");
    seed_catalog_file(filesystem, old_catalog);
    seed_catalog_at(
        filesystem,
        "/images/.catalog.pfc1.part",
        candidate);
    seed_image_file(filesystem, "next.pfr1", ".part");
    const std::vector<std::uint8_t> candidate_bytes =
        filesystem.files.at("/images/.catalog.pfc1.part");
    filesystem.fail_rename_from = "/images/.catalog.pfc1";

    pf_storage::RecoveryWorkspace workspace{};
    const pf_storage::RecoveryResult result =
        pf_storage::recover_image_transactions(filesystem, workspace);
    TEST_ASSERT_FALSE(result.ok());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_storage::RecoveryError::rename_failed),
        static_cast<int>(result.error));
    TEST_ASSERT_TRUE(filesystem.exists("/images/.catalog.pfc1"));
    TEST_ASSERT_FALSE(filesystem.exists("/images/next.pfr1"));
    TEST_ASSERT_TRUE(filesystem.exists("/images/.catalog.pfc1.part"));
    TEST_ASSERT_TRUE(filesystem.exists("/images/next.pfr1.part"));

    filesystem.fail_rename_from.clear();
    pf_storage::RecoveryWorkspace retry_workspace{};
    const pf_storage::RecoveryResult retry_result =
        pf_storage::recover_image_transactions(filesystem, retry_workspace);
    TEST_ASSERT_TRUE(retry_result.ok());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_storage::RecoveryAction::promoted_candidate),
        static_cast<int>(retry_result.action));
    TEST_ASSERT_TRUE(filesystem.exists("/images/next.pfr1"));
    TEST_ASSERT_FALSE(filesystem.exists("/images/next.pfr1.part"));
    TEST_ASSERT_FALSE(filesystem.exists("/images/.catalog.pfc1.part"));
    TEST_ASSERT_EQUAL_MEMORY(
        candidate_bytes.data(),
        filesystem.files.at("/images/.catalog.pfc1").data(),
        candidate_bytes.size());
}

void test_recovery_catalog_commit_failure_restores_checkpoint()
{
    FakeStorageFileSystem filesystem;
    const pf_storage::Catalog old_catalog = make_existing_catalog("old.pfr1");
    const pf_storage::Catalog candidate = append_catalog_entry(
        old_catalog,
        "next.pfr1");
    seed_catalog_file(filesystem, old_catalog);
    seed_catalog_at(
        filesystem,
        "/images/.catalog.pfc1.part",
        candidate);
    seed_image_file(filesystem, "next.pfr1", ".part");
    const std::vector<std::uint8_t> old_catalog_bytes =
        filesystem.files.at("/images/.catalog.pfc1");
    filesystem.fail_rename_from = "/images/.catalog.pfc1.part";

    pf_storage::RecoveryWorkspace workspace{};
    const pf_storage::RecoveryResult result =
        pf_storage::recover_image_transactions(filesystem, workspace);
    TEST_ASSERT_FALSE(result.ok());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_storage::RecoveryError::rename_failed),
        static_cast<int>(result.error));
    TEST_ASSERT_EQUAL_MEMORY(
        old_catalog_bytes.data(),
        filesystem.files.at("/images/.catalog.pfc1").data(),
        old_catalog_bytes.size());
    TEST_ASSERT_FALSE(filesystem.exists("/images/next.pfr1"));
    TEST_ASSERT_TRUE(filesystem.exists("/images/next.pfr1.part"));
    TEST_ASSERT_TRUE(filesystem.exists("/images/.catalog.pfc1.part"));
    TEST_ASSERT_FALSE(filesystem.exists("/images/.catalog.pfc1.bak"));

    filesystem.fail_rename_from.clear();
    pf_storage::RecoveryWorkspace retry_workspace{};
    const pf_storage::RecoveryResult retry_result =
        pf_storage::recover_image_transactions(filesystem, retry_workspace);
    TEST_ASSERT_TRUE(retry_result.ok());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_storage::RecoveryAction::promoted_candidate),
        static_cast<int>(retry_result.action));
    TEST_ASSERT_TRUE(filesystem.exists("/images/next.pfr1"));
    TEST_ASSERT_FALSE(filesystem.exists("/images/next.pfr1.part"));
    TEST_ASSERT_FALSE(filesystem.exists("/images/.catalog.pfc1.part"));
    TEST_ASSERT_FALSE(filesystem.exists("/images/.catalog.pfc1.bak"));
}

void test_recovery_fails_closed_when_directory_listing_fails()
{
    FakeStorageFileSystem filesystem;
    filesystem.fail_list = true;
    pf_storage::RecoveryWorkspace workspace{};
    const pf_storage::RecoveryResult result =
        pf_storage::recover_image_transactions(filesystem, workspace);
    TEST_ASSERT_FALSE(result.ok());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(pf_storage::RecoveryError::list_failed),
        static_cast<int>(result.error));
}

void test_rename_boundaries_roll_back_to_the_old_catalog()
{
    const pf_storage::Catalog current = make_existing_catalog("old.pfr1");
    const std::vector<std::uint8_t> image = make_pfr1("next.pfr1");
    const std::array<const char*, 4U> failure_points{
        "/images/.upload.part",
        "/images/.catalog.pfc1",
        "/images/next.pfr1.part",
        "/images/.catalog.pfc1.part",
    };
    for (const char* const failure_point : failure_points) {
        FakeStorageFileSystem filesystem;
        seed_catalog_file(filesystem, current);
        const std::vector<std::uint8_t> old_catalog =
            filesystem.files.at("/images/.catalog.pfc1");
        filesystem.fail_rename_from = failure_point;
        pf_storage::Catalog updated{};
        const pf_storage::ImageStoreResult result = upload(
            filesystem,
            current,
            updated,
            image,
            image.size());
        TEST_ASSERT_FALSE(result.ok());
        TEST_ASSERT_EQUAL_MEMORY(
            old_catalog.data(),
            filesystem.files.at("/images/.catalog.pfc1").data(),
            old_catalog.size());
        TEST_ASSERT_FALSE(filesystem.exists("/images/next.pfr1"));
        assert_no_transaction_files(filesystem);
    }
}

}  // namespace

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_valid_upload_commits_image_and_catalog);
    RUN_TEST(test_upload_preflights_space_before_reading);
    RUN_TEST(test_invalid_or_incomplete_upload_leaves_no_image);
    RUN_TEST(test_duplicate_name_is_rejected_without_replacing_existing_catalog);
    RUN_TEST(test_catalog_readback_rejects_silent_corruption);
    RUN_TEST(test_maximum_catalog_size_reads_back_at_exact_buffer_capacity);
    RUN_TEST(test_catalog_output_alias_is_rejected);
    RUN_TEST(test_catalog_close_failure_has_a_distinct_error);
    RUN_TEST(test_recovery_promotes_complete_catalog_and_image_parts);
    RUN_TEST(test_recovery_finishes_after_image_rename_and_backup_creation);
    RUN_TEST(test_recovery_restores_backup_when_candidate_is_corrupt);
    RUN_TEST(test_recovery_keeps_canonical_and_cleans_stale_backup_parts);
    RUN_TEST(test_recovery_rejects_non_append_candidate_without_mutation);
    RUN_TEST(test_recovery_rename_failure_removes_uncommitted_new_image);
    RUN_TEST(test_recovery_catalog_commit_failure_restores_checkpoint);
    RUN_TEST(test_recovery_fails_closed_when_directory_listing_fails);
    RUN_TEST(test_rename_boundaries_roll_back_to_the_old_catalog);
    return UNITY_END();
}
