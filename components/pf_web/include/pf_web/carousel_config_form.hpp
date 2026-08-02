#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace pf_web {

enum class CarouselConfigParseStatus : std::uint8_t {
    ok,
    missing_field,
    duplicate_field,
    unknown_field,
    invalid_value,
};

struct CarouselConfigForm {
    bool random = false;
};

inline CarouselConfigParseStatus parse_carousel_config_form(
    const char* const body,
    const std::size_t length,
    CarouselConfigForm& destination)
{
    if (body == nullptr || length == 0U) {
        return CarouselConfigParseStatus::missing_field;
    }
    CarouselConfigForm candidate{};
    bool random_seen = false;
    std::size_t cursor = 0U;
    while (cursor < length) {
        const std::size_t field_start = cursor;
        while (cursor < length && body[cursor] != '&') {
            ++cursor;
        }
        const std::size_t field_end = cursor;
        if (cursor < length) {
            ++cursor;
        }
        std::size_t equals = field_start;
        while (equals < field_end && body[equals] != '=') {
            ++equals;
        }
        if (equals == field_end) {
            return CarouselConfigParseStatus::invalid_value;
        }
        const std::size_t name_length = equals - field_start;
        if (name_length != 6U ||
            std::memcmp(body + field_start, "random", 6U) != 0) {
            return CarouselConfigParseStatus::unknown_field;
        }
        if (random_seen) {
            return CarouselConfigParseStatus::duplicate_field;
        }
        random_seen = true;
        const char* const value = body + equals + 1U;
        const std::size_t value_length = field_end - equals - 1U;
        if (value_length == 4U && std::memcmp(value, "true", 4U) == 0) {
            candidate.random = true;
        } else if (
            value_length == 5U && std::memcmp(value, "false", 5U) == 0) {
            candidate.random = false;
        } else {
            return CarouselConfigParseStatus::invalid_value;
        }
    }
    if (!random_seen) {
        return CarouselConfigParseStatus::missing_field;
    }
    destination = candidate;
    return CarouselConfigParseStatus::ok;
}

constexpr const char* to_string(const CarouselConfigParseStatus status)
{
    switch (status) {
        case CarouselConfigParseStatus::ok:
            return "ok";
        case CarouselConfigParseStatus::missing_field:
            return "missing_field";
        case CarouselConfigParseStatus::duplicate_field:
            return "duplicate_field";
        case CarouselConfigParseStatus::unknown_field:
            return "unknown_field";
        case CarouselConfigParseStatus::invalid_value:
            return "invalid_value";
    }
    return "invalid_value";
}

}  // namespace pf_web
