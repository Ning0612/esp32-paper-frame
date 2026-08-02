#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "pf_auth/credentials.hpp"
#include "pf_auth/token_codec.hpp"

namespace pf_runtime {
class RuntimeCoordinator;
}

namespace pf_auth {

// Home-LAN threat model: the realistic attacker is someone with physical
// access to the flash chip, not a networked brute-force attempt (login is
// synchronous and there is no remote rate limit beyond the hash cost
// itself). 10000 iterations keeps login latency in the low seconds on
// ESP32-S3 real hardware; see docs/adr/0007 for the full rationale.
inline constexpr std::uint32_t kDefaultPbkdf2Iterations = 10000U;

enum class LoginStatus : std::uint8_t {
    authenticated,
    password_created,
    invalid_input,
    invalid_credentials,
    setup_forbidden,
    busy,
    unavailable,
};

struct LoginResult {
    LoginStatus status = LoginStatus::unavailable;
    char session_token[kEncodedSecretCapacity]{};
    char csrf_token[kEncodedSecretCapacity]{};
    std::uint32_t hash_elapsed_ms = 0U;
};

enum class PasswordChangeStatus : std::uint8_t {
    changed,
    invalid_input,
    authentication_failed,
    busy,
    unavailable,
};

struct PasswordChangeResult {
    PasswordChangeStatus status = PasswordChangeStatus::unavailable;
};

struct RequestAuthentication {
    bool password_configured = false;
    bool authenticated = false;
    bool csrf_valid = false;
    char csrf_token[kEncodedSecretCapacity]{};
};

class AuthService {
public:
    esp_err_t start(pf_runtime::RuntimeCoordinator& runtime);

    bool password_configured() const;

    // Synchronous: runs the PBKDF2 derivation inline on the caller's
    // (HTTP handler) task. Returns LoginStatus::busy instead of blocking
    // if another login/password-setup is already in flight, so two
    // concurrent submissions can't race on password_hash_.
    LoginResult login(
        const char* username,
        const char* password,
        bool setup_allowed,
        std::uint64_t now_ms);

    // Synchronous: verifies the current session and CSRF token, derives the
    // replacement hash inline, commits it to NVS, and revokes the session.
    PasswordChangeResult change_password(
        const char* new_password,
        const char* session_token,
        const char* csrf_token,
        std::uint64_t now_ms);

    RequestAuthentication authenticate_request(
        const char* session_token,
        const char* csrf_token,
        std::uint64_t now_ms,
        bool touch = true);

    bool logout(
        const char* session_token,
        const char* csrf_token,
        std::uint64_t now_ms);

private:
    LoginResult perform_login(
        const char* password,
        bool setup_allowed,
        std::uint64_t now_ms);

    pf_runtime::RuntimeCoordinator* runtime_ = nullptr;
    SemaphoreHandle_t state_mutex_ = nullptr;
    SemaphoreHandle_t login_mutex_ = nullptr;
    StaticSemaphore_t state_mutex_control_{};
    StaticSemaphore_t login_mutex_control_{};
    bool initialized_ = false;
    bool password_configured_ = false;
    pf_config::ManagementPasswordHash password_hash_{};
    SessionManager session_{};
};

AuthService& auth_service();

constexpr const char* to_string(const LoginStatus status)
{
    switch (status) {
        case LoginStatus::authenticated:
            return "authenticated";
        case LoginStatus::password_created:
            return "password_created";
        case LoginStatus::invalid_input:
            return "invalid_input";
        case LoginStatus::invalid_credentials:
            return "invalid_credentials";
        case LoginStatus::setup_forbidden:
            return "setup_forbidden";
        case LoginStatus::busy:
            return "busy";
        case LoginStatus::unavailable:
            return "unavailable";
    }
    return "unavailable";
}

constexpr const char* to_string(const PasswordChangeStatus status)
{
    switch (status) {
        case PasswordChangeStatus::changed:
            return "changed";
        case PasswordChangeStatus::invalid_input:
            return "invalid_input";
        case PasswordChangeStatus::authentication_failed:
            return "authentication_failed";
        case PasswordChangeStatus::busy:
            return "busy";
        case PasswordChangeStatus::unavailable:
            return "unavailable";
    }
    return "unavailable";
}

}  // namespace pf_auth
