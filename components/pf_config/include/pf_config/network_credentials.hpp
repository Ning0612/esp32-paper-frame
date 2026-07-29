#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "pf_config/secure_memory.hpp"

namespace pf_config {

inline constexpr std::size_t kNetworkSsidCapacity = 33U;
inline constexpr std::size_t kNetworkPasswordCapacity = 65U;
inline constexpr std::uint32_t kNetworkCredentialMagic = 0x31434650U;
inline constexpr std::uint16_t kNetworkCredentialVersion = 1U;

struct NetworkCredentials {
    char ssid[kNetworkSsidCapacity]{};
    char password[kNetworkPasswordCapacity]{};
};

struct NetworkCredentialBlob {
    std::uint32_t magic = kNetworkCredentialMagic;
    std::uint16_t version = kNetworkCredentialVersion;
    std::uint8_t ssid_length = 0U;
    std::uint8_t password_length = 0U;
    std::uint8_t payload[
        (kNetworkSsidCapacity - 1U) +
        (kNetworkPasswordCapacity - 1U)]{};
    std::uint32_t crc32 = 0U;
};

inline std::size_t bounded_text_length(
    const char* const text,
    const std::size_t capacity)
{
    if (text == nullptr || capacity == 0U) {
        return capacity;
    }
    std::size_t length = 0U;
    while (length < capacity && text[length] != '\0') {
        ++length;
    }
    return length;
}

inline bool valid_utf8_text(
    const char* const text,
    const std::size_t length)
{
    if (text == nullptr) {
        return false;
    }
    std::size_t index = 0U;
    while (index < length) {
        const auto first =
            static_cast<std::uint8_t>(text[index]);
        if (first <= 0x7FU) {
            if (first < 0x20U || first == 0x7FU) {
                return false;
            }
            ++index;
            continue;
        }

        std::size_t continuation_count = 0U;
        std::uint32_t codepoint = 0U;
        if (first >= 0xC2U && first <= 0xDFU) {
            continuation_count = 1U;
            codepoint = first & 0x1FU;
        } else if (first >= 0xE0U && first <= 0xEFU) {
            continuation_count = 2U;
            codepoint = first & 0x0FU;
        } else if (first >= 0xF0U && first <= 0xF4U) {
            continuation_count = 3U;
            codepoint = first & 0x07U;
        } else {
            return false;
        }
        if (index + continuation_count >= length) {
            return false;
        }
        for (std::size_t offset = 1U;
             offset <= continuation_count;
             ++offset) {
            const auto next =
                static_cast<std::uint8_t>(text[index + offset]);
            if ((next & 0xC0U) != 0x80U) {
                return false;
            }
            codepoint = (codepoint << 6U) | (next & 0x3FU);
        }
        if ((continuation_count == 2U &&
             (codepoint < 0x800U ||
              (codepoint >= 0xD800U && codepoint <= 0xDFFFU))) ||
            (continuation_count == 3U &&
             (codepoint < 0x10000U || codepoint > 0x10FFFFU))) {
            return false;
        }
        index += continuation_count + 1U;
    }
    return true;
}

template <std::size_t Capacity>
inline bool copy_network_credential(
    char (&destination)[Capacity],
    const char* const source)
{
    const std::size_t length =
        bounded_text_length(source, Capacity);
    if (source == nullptr || length >= Capacity) {
        destination[0] = '\0';
        return false;
    }
    std::memcpy(destination, source, length + 1U);
    return true;
}

inline bool network_credentials_valid(
    const NetworkCredentials& credentials)
{
    const std::size_t ssid_length =
        bounded_text_length(
            credentials.ssid,
            sizeof(credentials.ssid));
    const std::size_t password_length =
        bounded_text_length(
            credentials.password,
            sizeof(credentials.password));
    return ssid_length >= 1U &&
           ssid_length <= 32U &&
           password_length < sizeof(credentials.password) &&
           (password_length == 0U ||
            (password_length >= 8U &&
             password_length <= 63U)) &&
           valid_utf8_text(credentials.ssid, ssid_length) &&
           (password_length == 0U ||
            valid_utf8_text(
                credentials.password,
                password_length));
}

inline std::uint32_t network_credential_crc32(
    const NetworkCredentialBlob& blob)
{
    std::uint32_t crc = 0xFFFFFFFFU;
    const auto update = [&crc](const std::uint8_t value) {
        crc ^= value;
        for (std::uint8_t bit = 0U; bit < 8U; ++bit) {
            const std::uint32_t mask =
                0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
    };
    update(static_cast<std::uint8_t>(blob.magic));
    update(static_cast<std::uint8_t>(blob.magic >> 8U));
    update(static_cast<std::uint8_t>(blob.magic >> 16U));
    update(static_cast<std::uint8_t>(blob.magic >> 24U));
    update(static_cast<std::uint8_t>(blob.version));
    update(static_cast<std::uint8_t>(blob.version >> 8U));
    update(blob.ssid_length);
    update(blob.password_length);
    const std::size_t used =
        static_cast<std::size_t>(blob.ssid_length) +
        static_cast<std::size_t>(blob.password_length);
    for (std::size_t index = 0U;
         index < used && index < sizeof(blob.payload);
         ++index) {
        update(blob.payload[index]);
    }
    return ~crc;
}

inline bool encode_network_credentials(
    const NetworkCredentials& credentials,
    NetworkCredentialBlob& destination)
{
    if (!network_credentials_valid(credentials)) {
        return false;
    }
    NetworkCredentialBlob candidate{};
    const SecureZeroGuard candidate_guard(candidate);
    candidate.ssid_length = static_cast<std::uint8_t>(
        bounded_text_length(
            credentials.ssid,
            sizeof(credentials.ssid)));
    candidate.password_length = static_cast<std::uint8_t>(
        bounded_text_length(
            credentials.password,
            sizeof(credentials.password)));
    std::memcpy(
        candidate.payload,
        credentials.ssid,
        candidate.ssid_length);
    std::memcpy(
        candidate.payload + candidate.ssid_length,
        credentials.password,
        candidate.password_length);
    candidate.crc32 = network_credential_crc32(candidate);
    destination = candidate;
    return true;
}

inline bool decode_network_credentials(
    const NetworkCredentialBlob& blob,
    NetworkCredentials& destination)
{
    if (blob.magic != kNetworkCredentialMagic ||
        blob.version != kNetworkCredentialVersion ||
        blob.ssid_length == 0U ||
        blob.ssid_length >= kNetworkSsidCapacity ||
        blob.password_length >= kNetworkPasswordCapacity ||
        (static_cast<std::size_t>(blob.ssid_length) +
         static_cast<std::size_t>(blob.password_length)) >
            sizeof(blob.payload) ||
        blob.crc32 != network_credential_crc32(blob)) {
        return false;
    }

    NetworkCredentials candidate{};
    const SecureZeroGuard candidate_guard(candidate);
    std::memcpy(
        candidate.ssid,
        blob.payload,
        blob.ssid_length);
    std::memcpy(
        candidate.password,
        blob.payload + blob.ssid_length,
        blob.password_length);
    if (!network_credentials_valid(candidate)) {
        return false;
    }
    destination = candidate;
    return true;
}

}  // namespace pf_config
