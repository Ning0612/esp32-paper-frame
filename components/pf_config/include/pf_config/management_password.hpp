#pragma once

#include <cstddef>
#include <cstdint>

namespace pf_config {

inline constexpr std::uint32_t kManagementPasswordMagic = 0x31414850U;
inline constexpr std::uint16_t kManagementPasswordVersion = 1U;
inline constexpr std::uint16_t kManagementPasswordAlgorithmPbkdf2Sha256 = 1U;
inline constexpr std::uint32_t kManagementPasswordMinimumIterations = 10000U;
inline constexpr std::uint32_t kManagementPasswordMaximumIterations = 2000000U;
inline constexpr std::size_t kManagementPasswordSaltBytes = 16U;
inline constexpr std::size_t kManagementPasswordHashBytes = 32U;

struct ManagementPasswordHash {
    std::uint32_t magic = kManagementPasswordMagic;
    std::uint16_t version = kManagementPasswordVersion;
    std::uint16_t algorithm =
        kManagementPasswordAlgorithmPbkdf2Sha256;
    std::uint32_t iterations = 0U;
    std::uint8_t salt[kManagementPasswordSaltBytes]{};
    std::uint8_t hash[kManagementPasswordHashBytes]{};
    std::uint32_t crc32 = 0U;
};

inline std::uint32_t management_password_crc32(
    const ManagementPasswordHash& record)
{
    std::uint32_t crc = 0xFFFFFFFFU;
    const auto update = [&crc](const std::uint8_t value) {
        crc ^= value;
        for (std::uint8_t bit = 0U; bit < 8U; ++bit) {
            const std::uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
    };
    const auto update_u16 = [&update](const std::uint16_t value) {
        update(static_cast<std::uint8_t>(value));
        update(static_cast<std::uint8_t>(value >> 8U));
    };
    const auto update_u32 = [&update](const std::uint32_t value) {
        update(static_cast<std::uint8_t>(value));
        update(static_cast<std::uint8_t>(value >> 8U));
        update(static_cast<std::uint8_t>(value >> 16U));
        update(static_cast<std::uint8_t>(value >> 24U));
    };

    update_u32(record.magic);
    update_u16(record.version);
    update_u16(record.algorithm);
    update_u32(record.iterations);
    for (const std::uint8_t value : record.salt) {
        update(value);
    }
    for (const std::uint8_t value : record.hash) {
        update(value);
    }
    return ~crc;
}

inline bool management_password_hash_valid(
    const ManagementPasswordHash& record)
{
    return record.magic == kManagementPasswordMagic &&
           record.version == kManagementPasswordVersion &&
           record.algorithm ==
               kManagementPasswordAlgorithmPbkdf2Sha256 &&
           record.iterations >=
               kManagementPasswordMinimumIterations &&
           record.iterations <=
               kManagementPasswordMaximumIterations &&
           record.crc32 == management_password_crc32(record);
}

inline bool constant_time_equal(
    const std::uint8_t* const left,
    const std::uint8_t* const right,
    const std::size_t length)
{
    if (left == nullptr || right == nullptr) {
        return false;
    }
    std::uint8_t difference = 0U;
    for (std::size_t index = 0U; index < length; ++index) {
        difference |= left[index] ^ right[index];
    }
    return difference == 0U;
}

}  // namespace pf_config
