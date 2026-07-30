#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "pf_image/pfr1.hpp"
#include "pf_storage/catalog.hpp"

namespace pf_web {

inline constexpr char kImageDownloadPrefix[] = "/api/v1/images/";
inline constexpr char kImageDownloadSuffix[] = "/download";

inline int image_download_hex_digit(const char value)
{
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

inline bool decode_image_download_uri(
    const char* const uri,
    char (&destination)[pf_storage::kCatalogNameCapacity],
    std::size_t& written)
{
    written = 0U;
    if (uri == nullptr) {
        return false;
    }
    const std::size_t prefix_length = std::strlen(kImageDownloadPrefix);
    const std::size_t suffix_length = std::strlen(kImageDownloadSuffix);
    if (std::strncmp(uri, kImageDownloadPrefix, prefix_length) != 0) {
        return false;
    }
    const char* const encoded = uri + prefix_length;
    const char* const suffix = std::strstr(encoded, kImageDownloadSuffix);
    if (suffix == nullptr || suffix == encoded ||
        ((suffix + suffix_length)[0] != '\0' &&
         (suffix + suffix_length)[0] != '?')) {
        return false;
    }
    const std::size_t encoded_length =
        static_cast<std::size_t>(suffix - encoded);
    for (std::size_t index = 0U; index < encoded_length; ++index) {
        std::uint8_t byte = static_cast<std::uint8_t>(encoded[index]);
        if (encoded[index] == '%') {
            if (index + 2U >= encoded_length) {
                return false;
            }
            const int high = image_download_hex_digit(encoded[index + 1U]);
            const int low = image_download_hex_digit(encoded[index + 2U]);
            if (high < 0 || low < 0) {
                return false;
            }
            byte = static_cast<std::uint8_t>((high << 4) | low);
            index += 2U;
        }
        if (byte == '/' || byte == '\\' || byte == '?' ||
            written + 1U >= sizeof(destination)) {
            return false;
        }
        destination[written++] = static_cast<char>(byte);
    }
    destination[written] = '\0';
    return pf_image::valid_filename(
        reinterpret_cast<const std::uint8_t*>(destination),
        written);
}

}  // namespace pf_web
