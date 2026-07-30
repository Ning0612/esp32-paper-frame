#pragma once

#include <cstddef>

namespace pf_auth {

inline constexpr char kManagementUsername[] = "admin";
inline constexpr std::size_t kMinimumPasswordBytes = 8U;
inline constexpr std::size_t kMaximumPasswordBytes = 128U;

inline bool password_valid(const char* const password)
{
    if (password == nullptr) {
        return false;
    }
    std::size_t length = 0U;
    while (length <= kMaximumPasswordBytes &&
           password[length] != '\0') {
        ++length;
    }
    return length >= kMinimumPasswordBytes &&
           length <= kMaximumPasswordBytes;
}

}  // namespace pf_auth
