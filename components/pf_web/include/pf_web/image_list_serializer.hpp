#pragma once

#include <cstddef>
#include <cstdio>

#include "pf_storage/catalog.hpp"

namespace pf_web {

inline constexpr char kImageListJsonPrefix[] =
    "{\"ok\":true,\"data\":{\"images\":[";
inline constexpr char kImageListJsonSuffix[] = "]}}";

inline bool serialize_image_content_disposition(
    const pf_storage::CatalogEntry& entry,
    char* const output,
    const std::size_t capacity,
    std::size_t& written)
{
    written = 0U;
    if (output == nullptr || capacity == 0U ||
        entry.name_length == 0U ||
        entry.name_length >= sizeof(entry.name)) {
        return false;
    }
    const char prefix[] = "attachment; filename=\"";
    const char suffix[] = "\"";
    std::size_t offset = 0U;
    const auto append = [&](const char value) {
        if (offset + 1U >= capacity) {
            return false;
        }
        output[offset++] = value;
        return true;
    };
    for (std::size_t index = 0U; index + 1U < sizeof(prefix); ++index) {
        if (!append(prefix[index])) {
            return false;
        }
    }
    for (std::size_t index = 0U; index < entry.name_length; ++index) {
        const char value = entry.name[index];
        if (value == '"' || value == '\\') {
            if (!append('\\')) {
                return false;
            }
        }
        if (!append(value)) {
            return false;
        }
    }
    for (std::size_t index = 0U; index + 1U < sizeof(suffix); ++index) {
        if (!append(suffix[index])) {
            return false;
        }
    }
    if (offset >= capacity) {
        return false;
    }
    output[offset] = '\0';
    written = offset;
    return true;
}

inline bool serialize_image_entry(
    const pf_storage::CatalogEntry& entry,
    char* const output,
    const std::size_t capacity,
    std::size_t& written)
{
    written = 0U;
    if (output == nullptr || capacity == 0U ||
        entry.name_length == 0U ||
        entry.name_length >= sizeof(entry.name)) {
        return false;
    }

    char escaped_name[sizeof(entry.name) * 2U]{};
    std::size_t escaped_length = 0U;
    for (std::size_t index = 0U;
         index < entry.name_length;
         ++index) {
        const char value = entry.name[index];
        if (value == '"' || value == '\\') {
            if (escaped_length + 2U >= sizeof(escaped_name)) {
                return false;
            }
            escaped_name[escaped_length++] = '\\';
        } else {
            if (escaped_length + 1U >= sizeof(escaped_name)) {
                return false;
            }
        }
        escaped_name[escaped_length++] = value;
    }
    escaped_name[escaped_length] = '\0';

    const char* const orientation =
        entry.orientation == pf_image::Orientation::portrait
            ? "portrait"
            : "landscape";
    const int result = std::snprintf(
        output,
        capacity,
        "{\"id\":%lu,\"name\":\"%s\","
        "\"created_at_epoch_s\":%llu,\"file_bytes\":%lu,"
        "\"payload_bytes\":%lu,\"width\":%u,\"height\":%u,"
        "\"orientation\":\"%s\",\"enabled\":%s,"
        "\"current\":%s,\"corrupt\":%s,\"order\":%u}",
        static_cast<unsigned long>(entry.id),
        escaped_name,
        static_cast<unsigned long long>(entry.created_at_epoch_s),
        static_cast<unsigned long>(entry.file_bytes),
        static_cast<unsigned long>(entry.payload_bytes),
        static_cast<unsigned>(entry.width),
        static_cast<unsigned>(entry.height),
        orientation,
        (entry.flags & pf_storage::kCatalogEnabled) != 0U
            ? "true"
            : "false",
        (entry.flags & pf_storage::kCatalogCurrent) != 0U
            ? "true"
            : "false",
        (entry.flags & pf_storage::kCatalogCorrupt) != 0U
            ? "true"
            : "false",
        static_cast<unsigned>(entry.order));
    if (result <= 0 || static_cast<std::size_t>(result) >= capacity) {
        return false;
    }
    written = static_cast<std::size_t>(result);
    return true;
}

}  // namespace pf_web
