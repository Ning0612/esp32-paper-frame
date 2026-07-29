#pragma once

#include <cstddef>
#include <cstdint>

namespace pf_network {

inline constexpr std::size_t kAccessPointEntropyBytes = 12U;
inline constexpr std::size_t
    kFormattedAccessPointPasswordCapacity = 16U;

inline bool format_access_point_password(
    const std::uint8_t* const entropy,
    const std::size_t entropy_length,
    char* const destination,
    const std::size_t destination_capacity)
{
    if (entropy == nullptr ||
        entropy_length < kAccessPointEntropyBytes ||
        destination == nullptr ||
        destination_capacity <
            kFormattedAccessPointPasswordCapacity) {
        return false;
    }

    constexpr char kAlphabet[] =
        "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    destination[0] = 'P';
    destination[1] = 'F';
    destination[2] = '-';
    for (std::size_t index = 0U;
         index < kAccessPointEntropyBytes;
         ++index) {
        destination[index + 3U] =
            kAlphabet[entropy[index] & 0x1FU];
    }
    destination[15] = '\0';
    return true;
}

}  // namespace pf_network
