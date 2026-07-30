#pragma once

#include <cstddef>
#include <cstring>

#include "pf_auth/credentials.hpp"
#include "pf_config/secure_memory.hpp"
#include "pf_web/provisioning_form.hpp"

namespace pf_web {

inline constexpr std::size_t kAuthUsernameCapacity = 16U;
inline constexpr std::size_t kAuthPasswordCapacity =
    pf_auth::kMaximumPasswordBytes + 1U;

struct AuthForm {
    char username[kAuthUsernameCapacity]{};
    char password[kAuthPasswordCapacity]{};
};

enum class AuthParseStatus {
    ok,
    missing_field,
    duplicate_field,
    unknown_field,
    bad_encoding,
    invalid_value,
};

inline AuthParseStatus parse_auth_form(
    const char* const body,
    const std::size_t length,
    AuthForm& destination)
{
    if (body == nullptr || length == 0U) {
        return AuthParseStatus::missing_field;
    }

    AuthForm candidate{};
    const pf_config::SecureZeroGuard candidate_guard(candidate);
    bool username_seen = false;
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
            return AuthParseStatus::bad_encoding;
        }
        const std::size_t name_length = equals - field_start;
        const char* const value = body + equals + 1U;
        const std::size_t value_length =
            field_end - equals - 1U;

        char* destination_value = nullptr;
        std::size_t capacity = 0U;
        if (name_length == 8U &&
            std::memcmp(
                body + field_start,
                "username",
                8U) == 0) {
            if (username_seen) {
                return AuthParseStatus::duplicate_field;
            }
            username_seen = true;
            destination_value = candidate.username;
            capacity = sizeof(candidate.username);
        } else if (
            name_length == 8U &&
            std::memcmp(
                body + field_start,
                "password",
                8U) == 0) {
            if (password_seen) {
                return AuthParseStatus::duplicate_field;
            }
            password_seen = true;
            destination_value = candidate.password;
            capacity = sizeof(candidate.password);
        } else {
            return AuthParseStatus::unknown_field;
        }

        FormDecodeStatus decoded = FormDecodeStatus::invalid_value;
        if (capacity == sizeof(candidate.username)) {
            decoded = decode_form_value(
                value,
                value_length,
                candidate.username);
        } else {
            decoded = decode_form_value(
                value,
                value_length,
                candidate.password);
        }
        if (destination_value == nullptr ||
            decoded != FormDecodeStatus::ok) {
            return decoded == FormDecodeStatus::bad_encoding
                       ? AuthParseStatus::bad_encoding
                       : AuthParseStatus::invalid_value;
        }
    }

    if (!username_seen || !password_seen) {
        return AuthParseStatus::missing_field;
    }
    if (std::strcmp(
            candidate.username,
            pf_auth::kManagementUsername) != 0 ||
        !pf_auth::password_valid(candidate.password)) {
        return AuthParseStatus::invalid_value;
    }
    destination = candidate;
    return AuthParseStatus::ok;
}

constexpr const char* to_string(const AuthParseStatus status)
{
    switch (status) {
        case AuthParseStatus::ok:
            return "ok";
        case AuthParseStatus::missing_field:
            return "missing_field";
        case AuthParseStatus::duplicate_field:
            return "duplicate_field";
        case AuthParseStatus::unknown_field:
            return "unknown_field";
        case AuthParseStatus::bad_encoding:
            return "bad_encoding";
        case AuthParseStatus::invalid_value:
            return "invalid_value";
    }
    return "invalid_value";
}

}  // namespace pf_web
