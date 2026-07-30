#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "pf_auth/credentials.hpp"
#include "pf_auth/token_codec.hpp"

namespace pf_runtime {
class RuntimeCoordinator;
}

namespace pf_auth {

inline constexpr std::uint32_t kDefaultPbkdf2Iterations = 600000U;

enum class LoginStatus : std::uint8_t {
    authenticated,
    password_created,
    invalid_input,
    invalid_credentials,
    setup_forbidden,
    unavailable,
};

struct LoginResult {
    LoginStatus status = LoginStatus::unavailable;
    char session_token[kEncodedSecretCapacity]{};
    char csrf_token[kEncodedSecretCapacity]{};
    std::uint32_t hash_elapsed_ms = 0U;
};

enum class LoginSubmitStatus : std::uint8_t {
    accepted,
    busy,
    invalid,
    unavailable,
};

struct LoginSubmitResult {
    LoginSubmitStatus status = LoginSubmitStatus::unavailable;
    char request_token[kEncodedSecretCapacity]{};
};

enum class LoginOperationState : std::uint8_t {
    idle,
    verifying,
    authenticated,
    password_created,
    invalid_credentials,
    setup_forbidden,
    failed,
};

struct LoginOperationSnapshot {
    LoginOperationState state = LoginOperationState::idle;
    LoginResult result{};
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

    LoginSubmitResult submit_login(
        const char* username,
        const char* password,
        bool setup_allowed,
        std::uint64_t now_ms);

    bool login_status(
        const char* request_token,
        LoginOperationSnapshot& destination);

    bool acknowledge_login(const char* request_token);

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
    static constexpr UBaseType_t kTaskPriority = tskIDLE_PRIORITY;
    static constexpr std::uint32_t kTaskStackWords = 4096U;
    static constexpr TickType_t kResultRetentionTicks =
        pdMS_TO_TICKS(180000U);

    struct QueuedLogin {
        SessionSecret request_token{};
        char password[kMaximumPasswordBytes + 1U]{};
        bool setup_allowed = false;
        std::uint64_t now_ms = 0U;
    };

    struct OperationStatus {
        SessionSecret request_token{};
        LoginOperationState state = LoginOperationState::idle;
        LoginResult result{};
    };

    static void task_entry(void* context);
    void task_main();
    LoginResult perform_login(
        const char* password,
        bool setup_allowed,
        std::uint64_t now_ms);

    pf_runtime::RuntimeCoordinator* runtime_ = nullptr;
    QueueHandle_t queue_ = nullptr;
    TaskHandle_t task_handle_ = nullptr;
    SemaphoreHandle_t state_mutex_ = nullptr;
    SemaphoreHandle_t operation_mutex_ = nullptr;
    StaticQueue_t queue_control_{};
    std::uint8_t queue_storage_[sizeof(QueuedLogin)]{};
    StaticSemaphore_t state_mutex_control_{};
    StaticSemaphore_t operation_mutex_control_{};
    StaticTask_t task_control_{};
    StackType_t task_stack_[kTaskStackWords]{};
    bool initialized_ = false;
    bool password_configured_ = false;
    pf_config::ManagementPasswordHash password_hash_{};
    SessionManager session_{};
    OperationStatus operation_{};
};

AuthService& auth_service();

constexpr bool login_operation_terminal(
    const LoginOperationState state)
{
    return state != LoginOperationState::idle &&
           state != LoginOperationState::verifying;
}

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
        case LoginStatus::unavailable:
            return "unavailable";
    }
    return "unavailable";
}

constexpr const char* to_string(const LoginOperationState state)
{
    switch (state) {
        case LoginOperationState::idle:
            return "idle";
        case LoginOperationState::verifying:
            return "verifying";
        case LoginOperationState::authenticated:
            return "authenticated";
        case LoginOperationState::password_created:
            return "password_created";
        case LoginOperationState::invalid_credentials:
            return "invalid_credentials";
        case LoginOperationState::setup_forbidden:
            return "setup_forbidden";
        case LoginOperationState::failed:
            return "failed";
    }
    return "failed";
}

}  // namespace pf_auth
