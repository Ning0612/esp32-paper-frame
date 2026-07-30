#pragma once

#include <cstddef>
#include <cstdint>

#include "pf_auth/session_manager.hpp"

namespace pf_auth {

inline constexpr std::size_t kEncodedSecretLength =
    kSessionSecretBytes * 2U;
inline constexpr std::size_t kEncodedSecretCapacity =
    kEncodedSecretLength + 1U;

inline void encode_secret(
    const SessionSecret& secret,
    char (&destination)[kEncodedSecretCapacity])
{
    constexpr char kHex[] = "0123456789abcdef";
    for (std::size_t index = 0U; index < secret.size(); ++index) {
        destination[index * 2U] = kHex[secret[index] >> 4U];
        destination[index * 2U + 1U] =
            kHex[secret[index] & 0x0FU];
    }
    destination[kEncodedSecretLength] = '\0';
}

inline int decode_hex_digit(const char value)
{
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return 10 + value - 'a';
    }
    if (value >= 'A' && value <= 'F') {
        return 10 + value - 'A';
    }
    return -1;
}

inline bool decode_secret(
    const char* const encoded,
    SessionSecret& destination)
{
    if (encoded == nullptr) {
        return false;
    }
    std::size_t length = 0U;
    while (length < kEncodedSecretCapacity &&
           encoded[length] != '\0') {
        ++length;
    }
    if (length != kEncodedSecretLength) {
        return false;
    }
    SessionSecret candidate{};
    for (std::size_t index = 0U;
         index < kEncodedSecretLength;
         index += 2U) {
        const int high = decode_hex_digit(encoded[index]);
        const int low = decode_hex_digit(encoded[index + 1U]);
        if (high < 0 || low < 0) {
            return false;
        }
        candidate[index / 2U] = static_cast<std::uint8_t>(
            (high << 4U) | low);
    }
    destination = candidate;
    return true;
}

}  // namespace pf_auth
