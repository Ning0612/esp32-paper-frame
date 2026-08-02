#include "pf_auth/auth_service.hpp"

#include <cinttypes>
#include <cstdint>
#include <cstring>

#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "pf_config/config_manager.hpp"
#include "pf_config/secure_memory.hpp"
#include "pf_runtime/runtime_coordinator.hpp"
#include "psa/crypto.h"

namespace pf_auth {
namespace {

constexpr char kTag[] = "pf_auth";

class MutexGuard {
public:
    explicit MutexGuard(
        const SemaphoreHandle_t mutex,
        const TickType_t wait = portMAX_DELAY)
        : mutex_(mutex),
          locked_(
              mutex_ != nullptr &&
              xSemaphoreTake(mutex_, wait) == pdTRUE)
    {
    }

    ~MutexGuard()
    {
        if (locked_) {
            xSemaphoreGive(mutex_);
        }
    }

    bool locked() const
    {
        return locked_;
    }

private:
    SemaphoreHandle_t mutex_ = nullptr;
    bool locked_ = false;
};

bool derive_password(
    const char* const password,
    const std::uint8_t* const salt,
    const std::size_t salt_length,
    const std::uint32_t iterations,
    std::uint8_t* const output,
    const std::size_t output_length)
{
    if (password == nullptr || salt == nullptr || output == nullptr) {
        return false;
    }

    const std::size_t password_length = std::strlen(password);
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_key_derivation_operation_t operation =
        PSA_KEY_DERIVATION_OPERATION_INIT;
    psa_key_id_t key = PSA_KEY_ID_NULL;
    const psa_algorithm_t algorithm =
        PSA_ALG_PBKDF2_HMAC(PSA_ALG_SHA_256);

    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_DERIVE);
    psa_set_key_lifetime(
        &attributes,
        PSA_KEY_LIFETIME_VOLATILE);
    psa_set_key_algorithm(&attributes, algorithm);
    psa_set_key_type(&attributes, PSA_KEY_TYPE_PASSWORD);
    psa_set_key_bits(
        &attributes,
        PSA_BYTES_TO_BITS(password_length));

    psa_status_t status = psa_import_key(
        &attributes,
        reinterpret_cast<const std::uint8_t*>(password),
        password_length,
        &key);
    if (status == PSA_SUCCESS) {
        status = psa_key_derivation_setup(&operation, algorithm);
    }
    if (status == PSA_SUCCESS) {
        status = psa_key_derivation_input_integer(
            &operation,
            PSA_KEY_DERIVATION_INPUT_COST,
            iterations);
    }
    if (status == PSA_SUCCESS) {
        status = psa_key_derivation_input_bytes(
            &operation,
            PSA_KEY_DERIVATION_INPUT_SALT,
            salt,
            salt_length);
    }
    if (status == PSA_SUCCESS) {
        status = psa_key_derivation_input_key(
            &operation,
            PSA_KEY_DERIVATION_INPUT_PASSWORD,
            key);
    }
    if (status == PSA_SUCCESS) {
        status = psa_key_derivation_output_bytes(
            &operation,
            output,
            output_length);
    }

    psa_reset_key_attributes(&attributes);
    psa_key_derivation_abort(&operation);
    if (key != PSA_KEY_ID_NULL) {
        psa_destroy_key(key);
    }
    return status == PSA_SUCCESS;
}

bool fill_nonzero_secret(SessionSecret& destination)
{
    for (std::uint8_t attempt = 0U; attempt < 2U; ++attempt) {
        esp_fill_random(destination.data(), destination.size());
        std::uint8_t combined = 0U;
        for (const std::uint8_t value : destination) {
            combined |= value;
        }
        if (combined != 0U) {
            return true;
        }
    }
    return false;
}

}  // namespace

esp_err_t AuthService::start(
    pf_runtime::RuntimeCoordinator& runtime)
{
    if (initialized_) {
        return ESP_ERR_INVALID_STATE;
    }
    const psa_status_t crypto_result = psa_crypto_init();
    if (crypto_result != PSA_SUCCESS) {
        return ESP_FAIL;
    }
    state_mutex_ =
        xSemaphoreCreateMutexStatic(&state_mutex_control_);
    login_mutex_ =
        xSemaphoreCreateMutexStatic(&login_mutex_control_);
    if (state_mutex_ == nullptr || login_mutex_ == nullptr) {
        state_mutex_ = nullptr;
        login_mutex_ = nullptr;
        return ESP_ERR_NO_MEM;
    }

    pf_config::ManagementPasswordLoadResult loaded =
        pf_config::load_management_password();
    if (loaded.error != ESP_OK) {
        pf_config::secure_zero(loaded.record);
        state_mutex_ = nullptr;
        login_mutex_ = nullptr;
        return loaded.error;
    }
    if (loaded.configured) {
        password_hash_ = loaded.record;
    }
    pf_config::secure_zero(loaded.record);
    password_configured_ = loaded.configured;
    runtime_ = &runtime;
    initialized_ = true;
    return ESP_OK;
}

bool AuthService::password_configured() const
{
    if (!initialized_ || state_mutex_ == nullptr) {
        return true;
    }
    MutexGuard guard(
        state_mutex_,
        pdMS_TO_TICKS(50U));
    return guard.locked() ? password_configured_ : true;
}

LoginResult AuthService::login(
    const char* const username,
    const char* const password,
    const bool setup_allowed,
    const std::uint64_t now_ms)
{
    if (username == nullptr ||
        std::strcmp(username, kManagementUsername) != 0 ||
        !password_valid(password)) {
        return {LoginStatus::invalid_input, {}, {}, 0U};
    }
    if (!initialized_ || login_mutex_ == nullptr) {
        return {LoginStatus::unavailable, {}, {}, 0U};
    }

    MutexGuard guard(login_mutex_, 0U);
    if (!guard.locked()) {
        return {LoginStatus::busy, {}, {}, 0U};
    }

    LoginResult result = perform_login(password, setup_allowed, now_ms);
    ESP_LOGI(
        kTag,
        "authentication_result=%s hash_elapsed_ms=%" PRIu32,
        to_string(result.status),
        result.hash_elapsed_ms);
    return result;
}

PasswordChangeResult AuthService::change_password(
    const char* const new_password,
    const char* const session_token,
    const char* const csrf_token,
    const std::uint64_t now_ms)
{
    PasswordChangeResult result{};
    if (!password_valid(new_password)) {
        result.status = PasswordChangeStatus::invalid_input;
        return result;
    }
    if (!initialized_ ||
        login_mutex_ == nullptr ||
        state_mutex_ == nullptr) {
        result.status = PasswordChangeStatus::unavailable;
        return result;
    }

    MutexGuard login_guard(login_mutex_, 0U);
    if (!login_guard.locked()) {
        result.status = PasswordChangeStatus::busy;
        return result;
    }

    SessionSecret decoded_session{};
    SessionSecret decoded_csrf{};
    const pf_config::SecureZeroGuard session_guard(decoded_session);
    const pf_config::SecureZeroGuard csrf_guard(decoded_csrf);
    if (!decode_secret(session_token, decoded_session) ||
        !decode_secret(csrf_token, decoded_csrf)) {
        result.status = PasswordChangeStatus::authentication_failed;
        return result;
    }
    {
        MutexGuard state_guard(state_mutex_, pdMS_TO_TICKS(50U));
        if (!state_guard.locked()) {
            result.status = PasswordChangeStatus::unavailable;
            return result;
        }
        if (!password_configured_ ||
            session_.authenticate(
                &decoded_session,
                now_ms,
                false) != SessionCheck::valid ||
            !session_.validate_csrf(&decoded_csrf)) {
            result.status = PasswordChangeStatus::authentication_failed;
            return result;
        }
    }

    pf_config::ManagementPasswordHash candidate{};
    const pf_config::SecureZeroGuard candidate_guard(candidate);
    candidate.iterations = kDefaultPbkdf2Iterations;
    esp_fill_random(candidate.salt, sizeof(candidate.salt));
    if (!derive_password(
            new_password,
            candidate.salt,
            sizeof(candidate.salt),
            candidate.iterations,
            candidate.hash,
            sizeof(candidate.hash))) {
        result.status = PasswordChangeStatus::unavailable;
        return result;
    }
    candidate.crc32 = pf_config::management_password_crc32(candidate);

    esp_err_t saved = ESP_ERR_INVALID_STATE;
    if (runtime_ != nullptr) {
        if (runtime_->lock_flash_display(0U)) {
            saved = pf_config::save_management_password(candidate);
            runtime_->unlock_flash_display();
        } else {
            saved = ESP_ERR_TIMEOUT;
        }
    }
    if (saved != ESP_OK) {
        ESP_LOGE(
            kTag,
            "password_change_commit_failed=%s",
            esp_err_to_name(saved));
        result.status = PasswordChangeStatus::unavailable;
        return result;
    }

    {
        // NVS has already committed at this point. Do not return a false
        // failure while leaving RAM and the durable password record out of
        // sync; this mutex is internal and its holders use bounded work.
        MutexGuard state_guard(state_mutex_, portMAX_DELAY);
        if (!state_guard.locked()) {
            result.status = PasswordChangeStatus::unavailable;
            return result;
        }
        password_hash_ = candidate;
        password_configured_ = true;
        session_.revoke();
    }
    result.status = PasswordChangeStatus::changed;
    return result;
}

LoginResult AuthService::perform_login(
    const char* const password,
    const bool setup_allowed,
    const std::uint64_t now_ms)
{
    LoginResult result{};
    if (!password_valid(password) ||
        state_mutex_ == nullptr) {
        result.status = LoginStatus::invalid_input;
        return result;
    }

    bool configured = true;
    pf_config::ManagementPasswordHash stored{};
    const pf_config::SecureZeroGuard stored_guard(stored);
    {
        MutexGuard guard(state_mutex_);
        if (!guard.locked()) {
            return result;
        }
        configured = password_configured_;
        if (configured) {
            stored = password_hash_;
        }
    }

    const std::int64_t started_us = esp_timer_get_time();
    if (!configured) {
        if (!setup_allowed) {
            result.status = LoginStatus::setup_forbidden;
            return result;
        }
        pf_config::ManagementPasswordHash candidate{};
        const pf_config::SecureZeroGuard candidate_guard(candidate);
        candidate.iterations = kDefaultPbkdf2Iterations;
        esp_fill_random(candidate.salt, sizeof(candidate.salt));
        if (!derive_password(
                password,
                candidate.salt,
                sizeof(candidate.salt),
                candidate.iterations,
                candidate.hash,
                sizeof(candidate.hash))) {
            return result;
        }
        candidate.crc32 =
            pf_config::management_password_crc32(candidate);

        // DisplayTask holds this same lock for the full duration of a
        // panel refresh (display_task_esp_idf.cpp), which can run well
        // past a minute. login() runs on the HTTP handler's own task,
        // so any wait here -- even a bounded one -- blocks that
        // request; try once, non-blocking, and fail closed instead
        // (mirrors login_mutex_'s 0-tick try-lock above).
        esp_err_t saved = ESP_ERR_INVALID_STATE;
        if (runtime_ != nullptr) {
            if (runtime_->lock_flash_display(0U)) {
                saved = pf_config::save_management_password(candidate);
                runtime_->unlock_flash_display();
            } else {
                saved = ESP_ERR_TIMEOUT;
            }
        }
        if (saved != ESP_OK) {
            ESP_LOGE(
                kTag,
                "password_hash_commit_failed=%s",
                esp_err_to_name(saved));
            return result;
        }
        {
            MutexGuard guard(state_mutex_);
            if (!guard.locked()) {
                return result;
            }
            password_hash_ = candidate;
            password_configured_ = true;
        }
        result.status = LoginStatus::password_created;
    } else {
        std::uint8_t derived[
            pf_config::kManagementPasswordHashBytes]{};
        const pf_config::SecureZeroGuard derived_guard(derived);
        if (!derive_password(
                password,
                stored.salt,
                sizeof(stored.salt),
                stored.iterations,
                derived,
                sizeof(derived))) {
            return result;
        }
        if (!pf_config::constant_time_equal(
                derived,
                stored.hash,
                sizeof(derived))) {
            result.status = LoginStatus::invalid_credentials;
            return result;
        }
        result.status = LoginStatus::authenticated;
    }

    SessionSecrets secrets{};
    const pf_config::SecureZeroGuard secrets_guard(secrets);
    if (!fill_nonzero_secret(secrets.token) ||
        !fill_nonzero_secret(secrets.csrf)) {
        result.status = LoginStatus::unavailable;
        return result;
    }
    {
        MutexGuard guard(state_mutex_);
        if (!guard.locked() ||
            !session_.issue(secrets, now_ms)) {
            result.status = LoginStatus::unavailable;
            return result;
        }
    }
    encode_secret(secrets.token, result.session_token);
    encode_secret(secrets.csrf, result.csrf_token);
    const std::int64_t elapsed_us =
        esp_timer_get_time() - started_us;
    result.hash_elapsed_ms =
        elapsed_us > 0
            ? static_cast<std::uint32_t>(
                  elapsed_us / 1000)
            : 0U;
    return result;
}

RequestAuthentication AuthService::authenticate_request(
    const char* const session_token,
    const char* const csrf_token,
    const std::uint64_t now_ms,
    const bool touch)
{
    RequestAuthentication result{};
    if (!initialized_ || state_mutex_ == nullptr) {
        result.password_configured = true;
        return result;
    }
    MutexGuard guard(
        state_mutex_,
        pdMS_TO_TICKS(50U));
    if (!guard.locked()) {
        result.password_configured = true;
        return result;
    }
    result.password_configured = password_configured_;

    SessionSecret decoded_session{};
    const pf_config::SecureZeroGuard session_guard(decoded_session);
    if (!decode_secret(session_token, decoded_session) ||
        session_.authenticate(
            &decoded_session,
            now_ms,
            touch) != SessionCheck::valid) {
        return result;
    }
    result.authenticated = true;

    const SessionSecret* const active_csrf = session_.csrf();
    if (active_csrf != nullptr) {
        encode_secret(*active_csrf, result.csrf_token);
    }
    SessionSecret decoded_csrf{};
    const pf_config::SecureZeroGuard csrf_guard(decoded_csrf);
    result.csrf_valid =
        decode_secret(csrf_token, decoded_csrf) &&
        session_.validate_csrf(&decoded_csrf);
    return result;
}

bool AuthService::logout(
    const char* const session_token,
    const char* const csrf_token,
    const std::uint64_t now_ms)
{
    if (!initialized_ || state_mutex_ == nullptr) {
        return false;
    }
    MutexGuard guard(
        state_mutex_,
        pdMS_TO_TICKS(50U));
    if (!guard.locked()) {
        return false;
    }
    SessionSecret decoded_session{};
    SessionSecret decoded_csrf{};
    const pf_config::SecureZeroGuard session_guard(decoded_session);
    const pf_config::SecureZeroGuard csrf_guard(decoded_csrf);
    if (!decode_secret(session_token, decoded_session) ||
        !decode_secret(csrf_token, decoded_csrf) ||
        session_.authenticate(
            &decoded_session,
            now_ms,
            false) != SessionCheck::valid ||
        !session_.validate_csrf(&decoded_csrf)) {
        return false;
    }
    session_.revoke();
    return true;
}

AuthService& auth_service()
{
    static AuthService instance{};
    return instance;
}

}  // namespace pf_auth
