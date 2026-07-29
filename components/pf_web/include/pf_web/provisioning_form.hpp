#pragma once

#include <cstddef>
#include <cstring>

#include "pf_config/network_credentials.hpp"
#include "pf_config/secure_memory.hpp"

namespace pf_web {

enum class ProvisioningParseStatus {
    ok,
    missing_field,
    duplicate_field,
    unknown_field,
    bad_encoding,
    invalid_value,
};

struct ProvisioningForm {
    char ssid[pf_config::kNetworkSsidCapacity]{};
    char password[pf_config::kNetworkPasswordCapacity]{};
};

enum class FormDecodeStatus {
    ok,
    bad_encoding,
    invalid_value,
};

inline int hex_value(const char value)
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

template <std::size_t Capacity>
inline FormDecodeStatus decode_form_value(
    const char* const source,
    const std::size_t source_length,
    char (&destination)[Capacity])
{
    std::size_t output = 0U;
    for (std::size_t input = 0U;
         input < source_length;
         ++input) {
        std::uint8_t value =
            static_cast<std::uint8_t>(source[input]);
        if (value == '%') {
            if (input + 2U >= source_length) {
                return FormDecodeStatus::bad_encoding;
            }
            const int high = hex_value(source[input + 1U]);
            const int low = hex_value(source[input + 2U]);
            if (high < 0 || low < 0) {
                return FormDecodeStatus::bad_encoding;
            }
            value = static_cast<std::uint8_t>(
                (high << 4U) | low);
            input += 2U;
        } else if (value == '+') {
            value = ' ';
        }
        if (value == 0U ||
            value < 0x20U ||
            value == 0x7FU ||
            output + 1U >= Capacity) {
            return FormDecodeStatus::invalid_value;
        }
        destination[output++] = static_cast<char>(value);
    }
    destination[output] = '\0';
    return pf_config::valid_utf8_text(destination, output) ||
                   output == 0U
               ? FormDecodeStatus::ok
               : FormDecodeStatus::invalid_value;
}

inline ProvisioningParseStatus parse_provisioning_form(
    const char* const body,
    const std::size_t length,
    ProvisioningForm& destination)
{
    if (body == nullptr || length == 0U) {
        return ProvisioningParseStatus::missing_field;
    }

    ProvisioningForm candidate{};
    const pf_config::SecureZeroGuard candidate_guard(candidate);
    bool ssid_seen = false;
    bool password_seen = false;
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
            return ProvisioningParseStatus::bad_encoding;
        }
        const std::size_t name_length = equals - field_start;
        const char* const value = body + equals + 1U;
        const std::size_t value_length =
            field_end - equals - 1U;

        if (name_length == 4U &&
            std::memcmp(body + field_start, "ssid", 4U) == 0) {
            if (ssid_seen) {
                return ProvisioningParseStatus::duplicate_field;
            }
            ssid_seen = true;
            const FormDecodeStatus decoded =
                decode_form_value(
                    value,
                    value_length,
                    candidate.ssid);
            if (decoded != FormDecodeStatus::ok) {
                return decoded == FormDecodeStatus::bad_encoding
                           ? ProvisioningParseStatus::bad_encoding
                           : ProvisioningParseStatus::invalid_value;
            }
        } else if (
            name_length == 8U &&
            std::memcmp(
                body + field_start,
                "password",
                8U) == 0) {
            if (password_seen) {
                return ProvisioningParseStatus::duplicate_field;
            }
            password_seen = true;
            const FormDecodeStatus decoded =
                decode_form_value(
                    value,
                    value_length,
                    candidate.password);
            if (decoded != FormDecodeStatus::ok) {
                return decoded == FormDecodeStatus::bad_encoding
                           ? ProvisioningParseStatus::bad_encoding
                           : ProvisioningParseStatus::invalid_value;
            }
        } else {
            return ProvisioningParseStatus::unknown_field;
        }
    }

    if (!ssid_seen || !password_seen) {
        return ProvisioningParseStatus::missing_field;
    }
    pf_config::NetworkCredentials credentials{};
    const pf_config::SecureZeroGuard credentials_guard(credentials);
    std::memcpy(
        credentials.ssid,
        candidate.ssid,
        sizeof(candidate.ssid));
    std::memcpy(
        credentials.password,
        candidate.password,
        sizeof(candidate.password));
    if (!pf_config::network_credentials_valid(credentials)) {
        return ProvisioningParseStatus::invalid_value;
    }
    destination = candidate;
    return ProvisioningParseStatus::ok;
}

constexpr const char* to_string(
    const ProvisioningParseStatus status)
{
    switch (status) {
        case ProvisioningParseStatus::ok:
            return "ok";
        case ProvisioningParseStatus::missing_field:
            return "missing_field";
        case ProvisioningParseStatus::duplicate_field:
            return "duplicate_field";
        case ProvisioningParseStatus::unknown_field:
            return "unknown_field";
        case ProvisioningParseStatus::bad_encoding:
            return "bad_encoding";
        case ProvisioningParseStatus::invalid_value:
            return "invalid_value";
    }
    return "invalid_value";
}

}  // namespace pf_web
