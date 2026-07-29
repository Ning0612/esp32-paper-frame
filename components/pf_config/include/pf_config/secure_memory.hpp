#pragma once

#include <cstddef>
#include <cstdint>

namespace pf_config {

inline void secure_zero(
    void* const memory,
    const std::size_t length)
{
    auto* cursor =
        static_cast<volatile std::uint8_t*>(memory);
    for (std::size_t index = 0U;
         cursor != nullptr && index < length;
         ++index) {
        cursor[index] = 0U;
    }
}

template <typename Value>
inline void secure_zero(Value& value)
{
    secure_zero(&value, sizeof(value));
}

template <typename Value>
class SecureZeroGuard {
public:
    explicit SecureZeroGuard(Value& value) : value_(value) {}

    SecureZeroGuard(const SecureZeroGuard&) = delete;
    SecureZeroGuard& operator=(const SecureZeroGuard&) = delete;

    ~SecureZeroGuard()
    {
        secure_zero(value_);
    }

private:
    Value& value_;
};

}  // namespace pf_config
