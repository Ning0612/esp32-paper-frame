#include "pf_web/health_server.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "pf_auth/auth_service.hpp"
#include "pf_config/config_manager.hpp"
#include "pf_config/network_credentials.hpp"
#include "pf_config/secure_memory.hpp"
#include "pf_network/network_service_esp_idf.hpp"
#include "pf_runtime/runtime_coordinator.hpp"
#include "pf_storage/storage_worker.hpp"
#include "pf_web/access_policy.hpp"
#include "pf_web/auth_form.hpp"
#include "pf_web/dashboard_serializer.hpp"
#include "pf_web/health_serializer.hpp"
#include "pf_web/image_download_path.hpp"
#include "pf_web/image_list_serializer.hpp"
#include "pf_web/http_receive_policy.hpp"
#include "pf_web/provisioning_form.hpp"
#include "pf_web/provisioning_service.hpp"
#include "pf_web/weather_config_form.hpp"

namespace pf_web {
namespace {

constexpr char kFirmwareVersion[] = "phase3-dev";

struct StaticAsset {
    const char* path;
    const char* content_type;
};

constexpr StaticAsset kIndexAsset{
    "/web/index.html",
    "text/html; charset=utf-8",
};
constexpr StaticAsset kStyleAsset{
    "/web/style.css",
    "text/css; charset=utf-8",
};
constexpr StaticAsset kScriptAsset{
    "/web/ui.js",
    "application/javascript; charset=utf-8",
};
constexpr StaticAsset kFaviconAsset{
    "/web/favicon.svg",
    "image/svg+xml",
};
constexpr std::uint32_t kMaximumBodyReceiveMs = 15000U;
constexpr std::uint8_t kMaximumBodyTimeouts = 3U;
constexpr char kSessionCookieName[] = "pf_session";
constexpr char kCsrfHeaderName[] = "X-CSRF-Token";
constexpr char kAuthRequestHeaderName[] = "X-Auth-Request";
constexpr UBaseType_t kImageDownloadTaskPriority = 3U;
constexpr std::uint32_t kImageDownloadTaskStackWords = 4096U;
constexpr std::size_t kImageDownloadContentDispositionCapacity =
    (pf_storage::kCatalogNameCapacity * 2U) + 32U;
constexpr UBaseType_t kImageUploadTaskPriority = 3U;
constexpr std::uint32_t kImageUploadTaskStackWords = 8192U;
constexpr UBaseType_t kImageMutationTaskPriority = 3U;
constexpr std::uint32_t kImageMutationTaskStackWords = 4096U;
constexpr UBaseType_t kWeatherConfigTaskPriority = 3U;
constexpr std::uint32_t kWeatherConfigTaskStackWords = 4096U;
constexpr std::size_t kWeatherConfigBodyCapacity = 512U;

HealthServerAccessConfig server_access_config{};

struct ImageDownloadRequest {
    httpd_req_t* request = nullptr;
    pf_storage::StorageWorker* worker = nullptr;
    std::size_t name_length = 0U;
    std::uint32_t file_bytes = 0U;
    char name[pf_storage::kCatalogNameCapacity]{};
    char content_disposition[
        kImageDownloadContentDispositionCapacity]{};
};

QueueHandle_t image_download_queue = nullptr;
StaticQueue_t image_download_queue_control{};
std::uint8_t image_download_queue_storage[
    sizeof(ImageDownloadRequest)]{};
StaticTask_t image_download_task_control{};
StackType_t image_download_task_stack[
    kImageDownloadTaskStackWords]{};

struct ImageUploadRequest {
    httpd_req_t* request = nullptr;
    pf_storage::StorageWorker* worker = nullptr;
    std::size_t content_length = 0U;
};

struct ImageUploadReceiveContext {
    httpd_req_t* request = nullptr;
    std::size_t remaining = 0U;
    std::uint8_t timeout_count = 0U;
    std::uint64_t started_ms = 0U;
};

QueueHandle_t image_upload_queue = nullptr;
StaticQueue_t image_upload_queue_control{};
std::uint8_t image_upload_queue_storage[
    sizeof(ImageUploadRequest)]{};
StaticTask_t image_upload_task_control{};
StackType_t image_upload_task_stack[
    kImageUploadTaskStackWords]{};

enum class ImageMutationKind : std::uint8_t {
    activate = 0U,
    remove,
    reorder,
};

struct ImageMutationRequest {
    httpd_req_t* request = nullptr;
    pf_storage::StorageWorker* worker = nullptr;
    ImageMutationKind kind = ImageMutationKind::activate;
    std::uint32_t image_id = 0U;
    std::uint16_t order_count = 0U;
    std::uint32_t order_ids[pf_storage::kCatalogMaxEntries]{};
};

QueueHandle_t image_mutation_queue = nullptr;
StaticQueue_t image_mutation_queue_control{};
std::uint8_t image_mutation_queue_storage[
    sizeof(ImageMutationRequest)]{};
StaticTask_t image_mutation_task_control{};
StackType_t image_mutation_task_stack[
    kImageMutationTaskStackWords]{};

struct WeatherConfigRequest {
    httpd_req_t* request = nullptr;
};

QueueHandle_t weather_config_queue = nullptr;
StaticQueue_t weather_config_queue_control{};
std::uint8_t weather_config_queue_storage[
    sizeof(WeatherConfigRequest)]{};
StaticTask_t weather_config_task_control{};
StackType_t weather_config_task_stack[
    kWeatherConfigTaskStackWords]{};
SemaphoreHandle_t weather_config_mutex = nullptr;
StaticSemaphore_t weather_config_mutex_control{};

esp_err_t set_common_headers(
    httpd_req_t* const request,
    const char* const content_type)
{
    esp_err_t result =
        httpd_resp_set_type(request, content_type);
    if (result == ESP_OK) {
        result = httpd_resp_set_hdr(
            request,
            "Cache-Control",
            "no-store");
    }
    if (result == ESP_OK) {
        result = httpd_resp_set_hdr(
            request,
            "X-Content-Type-Options",
            "nosniff");
    }
    if (result == ESP_OK) {
        result = httpd_resp_set_hdr(
            request,
            "Content-Security-Policy",
            "default-src 'self'; connect-src 'self'; "
            "img-src 'self'; style-src 'self'; script-src 'self'");
    }
    return result;
}

esp_err_t send_json(
    httpd_req_t* const request,
    const char* const status,
    const char* const body)
{
    esp_err_t result =
        set_common_headers(
            request,
            "application/json; charset=utf-8");
    if (result == ESP_OK && status != nullptr) {
        result = httpd_resp_set_status(request, status);
    }
    if (result == ESP_OK) {
        result = httpd_resp_sendstr(request, body);
    }
    return result;
}

void close_request_session(httpd_req_t* const request)
{
    if (request == nullptr) {
        return;
    }
    const int socket = httpd_req_to_sockfd(request);
    if (socket >= 0) {
        (void)httpd_sess_trigger_close(request->handle, socket);
    }
}

esp_err_t finish_async_upload_request(
    httpd_req_t* const request,
    const esp_err_t response_result,
    const bool close_session)
{
    if (close_session) {
        close_request_session(request);
    }
    const esp_err_t complete_result =
        httpd_req_async_handler_complete(request);
    return response_result == ESP_OK && complete_result == ESP_OK
               ? ESP_OK
               : ESP_FAIL;
}

bool provisioning_mode_active()
{
    pf_runtime::RuntimeSnapshot snapshot{};
    return pf_runtime::coordinator().read_snapshot(snapshot) &&
           snapshot.wifi ==
               pf_runtime::WifiState::provisioning;
}

std::uint64_t monotonic_ms()
{
    return static_cast<std::uint64_t>(esp_timer_get_time()) / 1000U;
}

bool request_session_token(
    httpd_req_t* const request,
    char (&destination)[pf_auth::kEncodedSecretCapacity])
{
    std::size_t size = sizeof(destination);
    // ESP-IDF reports the cookie value size including its terminator.
    return httpd_req_get_cookie_val(
               request,
               kSessionCookieName,
               destination,
               &size) == ESP_OK &&
           size == pf_auth::kEncodedSecretCapacity;
}

bool request_csrf_token(
    httpd_req_t* const request,
    char (&destination)[pf_auth::kEncodedSecretCapacity])
{
    const std::size_t length =
        httpd_req_get_hdr_value_len(request, kCsrfHeaderName);
    return length == pf_auth::kEncodedSecretLength &&
           httpd_req_get_hdr_value_str(
               request,
               kCsrfHeaderName,
               destination,
               sizeof(destination)) == ESP_OK;
}

bool request_auth_operation_token(
    httpd_req_t* const request,
    char (&destination)[pf_auth::kEncodedSecretCapacity])
{
    const std::size_t length =
        httpd_req_get_hdr_value_len(
            request,
            kAuthRequestHeaderName);
    return length == pf_auth::kEncodedSecretLength &&
           httpd_req_get_hdr_value_str(
               request,
               kAuthRequestHeaderName,
               destination,
               sizeof(destination)) == ESP_OK;
}

pf_auth::RequestAuthentication request_authentication(
    httpd_req_t* const request,
    const bool touch = true)
{
    char session_token[pf_auth::kEncodedSecretCapacity]{};
    char csrf_token[pf_auth::kEncodedSecretCapacity]{};
    const pf_config::SecureZeroGuard session_guard(session_token);
    const pf_config::SecureZeroGuard csrf_guard(csrf_token);
    const bool has_session =
        request_session_token(request, session_token);
    const bool has_csrf =
        request_csrf_token(request, csrf_token);
    return pf_auth::auth_service().authenticate_request(
        has_session ? session_token : nullptr,
        has_csrf ? csrf_token : nullptr,
        monotonic_ms(),
        touch);
}

AccessContext current_access_context(httpd_req_t* const request)
{
    pf_auth::RequestAuthentication authentication =
        request_authentication(request);
    const pf_config::SecureZeroGuard authentication_guard(
        authentication);
    return {
        .provisioning_ap = provisioning_mode_active(),
        .initial_bootstrap =
            server_access_config.initial_bootstrap,
        .password_bootstrap =
            server_access_config.password_bootstrap,
        .management_password_configured =
            authentication.password_configured,
        .authenticated = authentication.authenticated,
        .csrf_valid = authentication.csrf_valid,
    };
}

esp_err_t reject_management_request(
    httpd_req_t* const request,
    const AccessContext& context,
    const bool mutation)
{
    if (!context.authenticated) {
        return send_json(
            request,
            "401 Unauthorized",
            "{\"ok\":false,\"error\":\"authentication_required\"}");
    }
    if (mutation && !context.csrf_valid) {
        return send_json(
            request,
            "403 Forbidden",
            "{\"ok\":false,\"error\":\"csrf_required\"}");
    }
    return send_json(
        request,
        "403 Forbidden",
        "{\"ok\":false,\"error\":\"forbidden\"}");
}

esp_err_t static_asset_handler(httpd_req_t* request)
{
    const auto* const asset =
        static_cast<const StaticAsset*>(request->user_ctx);
    if (asset == nullptr) {
        return send_json(
            request,
            "500 Internal Server Error",
            "{\"ok\":false,\"error\":\"asset_context\"}");
    }

    std::FILE* const file =
        std::fopen(asset->path, "rb");
    if (file == nullptr) {
        return send_json(
            request,
            "503 Service Unavailable",
            "{\"ok\":false,\"error\":\"webfs_unavailable\"}");
    }
    esp_err_t result =
        set_common_headers(request, asset->content_type);
    char chunk[512]{};
    while (result == ESP_OK) {
        const std::size_t length =
            std::fread(chunk, 1U, sizeof(chunk), file);
        if (length == 0U) {
            break;
        }
        result = httpd_resp_send_chunk(
            request,
            chunk,
            static_cast<ssize_t>(length));
    }
    std::fclose(file);
    if (result == ESP_OK) {
        result = httpd_resp_send_chunk(request, nullptr, 0);
    }
    return result;
}

esp_err_t health_handler(httpd_req_t* request)
{
    pf_runtime::RuntimeSnapshot snapshot{};
    const bool snapshot_valid =
        pf_runtime::coordinator().read_snapshot(snapshot);

    char response[320]{};
    const std::uint64_t uptime_ms =
        static_cast<std::uint64_t>(esp_timer_get_time()) / 1000U;
    const SerializeResult serialized =
        serialize_health(
            snapshot,
            snapshot_valid,
            uptime_ms,
            response,
            sizeof(response));
    if (!serialized.ok) {
        return send_json(
            request,
            "500 Internal Server Error",
            "{\"ok\":false,\"error\":\"health_serialization\"}");
    }
    return send_json(request, nullptr, response);
}

esp_err_t device_handler(httpd_req_t* request)
{
    pf_runtime::RuntimeSnapshot snapshot{};
    const bool snapshot_valid =
        pf_runtime::coordinator().read_snapshot(snapshot);
    const DeviceInfo device{
        .product = "PaperFrame",
        .model = "ESP32-S3-N16R8",
        .firmware = kFirmwareVersion,
    };
    char response[768]{};
    const SerializeResult serialized = serialize_device(
        device,
        snapshot,
        snapshot_valid,
        response,
        sizeof(response));
    if (!serialized.ok) {
        return send_json(
            request,
            "500 Internal Server Error",
            "{\"ok\":false,\"error\":\"device_serialization\"}");
    }
    return send_json(request, nullptr, response);
}

esp_err_t status_handler(httpd_req_t* request)
{
    const AccessContext access = current_access_context(request);
    if (!access.authenticated) {
        return reject_management_request(request, access, false);
    }

    pf_runtime::RuntimeSnapshot snapshot{};
    const bool snapshot_valid =
        pf_runtime::coordinator().read_snapshot(snapshot);
    char response[2048]{};
    const SerializeResult serialized = serialize_status(
        snapshot,
        snapshot_valid,
        monotonic_ms(),
        response,
        sizeof(response));
    if (!serialized.ok) {
        return send_json(
            request,
            "500 Internal Server Error",
            "{\"ok\":false,\"error\":\"status_serialization\"}");
    }
    return send_json(request, nullptr, response);
}

esp_err_t config_handler(httpd_req_t* request)
{
    const AccessContext access = current_access_context(request);
    if (!access.authenticated) {
        return reject_management_request(request, access, false);
    }

    if (weather_config_mutex == nullptr ||
        xSemaphoreTake(
            weather_config_mutex,
            pdMS_TO_TICKS(1000U)) != pdTRUE) {
        return send_json(
            request,
            "503 Service Unavailable",
            "{\"ok\":false,\"error\":\"weather_config_busy\"}");
    }

    const MaskedConfig config{
        .wifi_configured = server_access_config.wifi_configured,
        .wifi_password_configured =
            server_access_config.wifi_password_configured,
        .management_password_configured =
            server_access_config.management_password_configured,
        .refresh_minutes = server_access_config.refresh_minutes,
        .timezone = server_access_config.timezone,
        .weather_configured = server_access_config.weather_configured,
        .weather_api_key_set =
            server_access_config.weather_settings.api_key[0] != '\0',
        .weather_latitude_e6 = server_access_config.weather_settings.latitude_e6,
        .weather_longitude_e6 = server_access_config.weather_settings.longitude_e6,
        .weather_interval_minutes =
            server_access_config.weather_settings.update_interval_minutes,
        .weather_location = server_access_config.weather_settings.location,
        .weather_units = server_access_config.weather_settings.units,
        .weather_language = server_access_config.weather_settings.language,
        .weather_ntp_server = server_access_config.weather_settings.ntp_server,
    };
    char response[1024]{};
    const SerializeResult serialized = serialize_masked_config(
        config,
        response,
        sizeof(response));
    xSemaphoreGive(weather_config_mutex);
    if (!serialized.ok) {
        return send_json(
            request,
            "500 Internal Server Error",
            "{\"ok\":false,\"error\":\"config_serialization\"}");
    }
    return send_json(request, nullptr, response);
}

bool receive_weather_config_body(
    httpd_req_t* const request,
    char* const body,
    const std::size_t capacity,
    std::size_t& received)
{
    if (request == nullptr || body == nullptr || capacity == 0U ||
        request->content_len <= 0 ||
        static_cast<std::size_t>(request->content_len) >= capacity) {
        return false;
    }
    received = 0U;
    std::uint8_t timeout_count = 0U;
    const std::uint64_t started_ms = monotonic_ms();
    while (received < static_cast<std::size_t>(request->content_len)) {
        if (body_receive_deadline_expired(
                monotonic_ms(),
                started_ms,
                kMaximumBodyReceiveMs)) {
            return false;
        }
        const int result = httpd_req_recv(
            request,
            body + received,
            request->content_len - static_cast<int>(received));
        if (result == HTTPD_SOCK_ERR_TIMEOUT) {
            ++timeout_count;
            if (body_receive_idle_limit_reached(
                    timeout_count,
                    kMaximumBodyTimeouts)) {
                return false;
            }
            continue;
        }
        if (result <= 0) {
            return false;
        }
        received += static_cast<std::size_t>(result);
    }
    body[received] = '\0';
    return true;
}

esp_err_t process_weather_config(
    httpd_req_t* const request,
    const char* const body,
    const std::size_t received)
{
    WeatherConfigForm form{};
    const pf_config::SecureZeroGuard form_guard(form);
    const WeatherConfigParseStatus parsed =
        parse_weather_config_form(body, received, form);
    if (parsed != WeatherConfigParseStatus::ok) {
        char response[112]{};
        std::snprintf(
            response,
            sizeof(response),
            "{\"ok\":false,\"error\":\"%s\"}",
            to_string(parsed));
        return send_json(request, "400 Bad Request", response);
    }

    pf_config::WeatherSettings candidate{};
    if (weather_config_mutex == nullptr ||
        xSemaphoreTake(
            weather_config_mutex,
            pdMS_TO_TICKS(1000U)) != pdTRUE) {
        return send_json(
            request,
            "503 Service Unavailable",
            "{\"ok\":false,\"error\":\"weather_config_busy\"}");
    }
    candidate = server_access_config.weather_settings;
    xSemaphoreGive(weather_config_mutex);
    const pf_config::SecureZeroGuard candidate_guard(candidate);
    if (!parse_weather_i32(form.latitude_e6, candidate.latitude_e6) ||
        !parse_weather_i32(form.longitude_e6, candidate.longitude_e6) ||
        !parse_weather_u32(
            form.interval_minutes,
            candidate.update_interval_minutes)) {
        return send_json(
            request,
            "422 Unprocessable Entity",
            "{\"ok\":false,\"error\":\"invalid_value\"}");
    }
    std::memcpy(candidate.location, form.location, sizeof(candidate.location));
    std::memcpy(candidate.units, form.units, sizeof(candidate.units));
    std::memcpy(candidate.language, form.language, sizeof(candidate.language));
    std::memcpy(candidate.ntp_server, form.ntp_server, sizeof(candidate.ntp_server));
    if (form.api_key_seen) {
        std::memcpy(candidate.api_key, form.api_key, sizeof(candidate.api_key));
    }
    if (!pf_config::weather_settings_valid(candidate)) {
        return send_json(
            request,
            "422 Unprocessable Entity",
            "{\"ok\":false,\"error\":\"invalid_value\"}");
    }
    if (!pf_runtime::coordinator().lock_flash_display(
            pdMS_TO_TICKS(10000U))) {
        return send_json(
            request,
            "503 Service Unavailable",
            "{\"ok\":false,\"error\":\"flash_busy\"}");
    }
    const esp_err_t saved = pf_config::save_weather_settings(candidate);
    pf_runtime::coordinator().unlock_flash_display();
    if (saved != ESP_OK) {
        return send_json(
            request,
            "503 Service Unavailable",
            "{\"ok\":false,\"error\":\"storage_unavailable\"}");
    }
    if (weather_config_mutex == nullptr ||
        xSemaphoreTake(
            weather_config_mutex,
            pdMS_TO_TICKS(1000U)) != pdTRUE) {
        return send_json(
            request,
            "503 Service Unavailable",
            "{\"ok\":false,\"error\":\"weather_config_busy\"}");
    }
    server_access_config.weather_settings = candidate;
    server_access_config.weather_configured = true;
    const bool api_key_set =
        server_access_config.weather_settings.api_key[0] != '\0';
    xSemaphoreGive(weather_config_mutex);
    return send_json(
        request,
        nullptr,
        api_key_set
            ? "{\"ok\":true,\"data\":{\"saved\":true,\"api_key_set\":true}}"
            : "{\"ok\":true,\"data\":{\"saved\":true,\"api_key_set\":false}}");
}

void weather_config_task_entry(void* const context)
{
    (void)context;
    while (true) {
        WeatherConfigRequest queued{};
        if (weather_config_queue == nullptr ||
            xQueueReceive(
                weather_config_queue,
                &queued,
                portMAX_DELAY) != pdTRUE) {
            continue;
        }
        char body[kWeatherConfigBodyCapacity]{};
        const pf_config::SecureZeroGuard body_guard(body);
        std::size_t received = 0U;
        esp_err_t response_result = ESP_OK;
        if (queued.request == nullptr) {
            continue;
        }
        if (!receive_weather_config_body(
                queued.request,
                body,
                sizeof(body),
                received)) {
            response_result = send_json(
                queued.request,
                "400 Bad Request",
                "{\"ok\":false,\"error\":\"body_receive_failed\"}");
        } else {
            response_result = process_weather_config(
                queued.request,
                body,
                received);
        }
        finish_async_upload_request(
            queued.request,
            response_result,
            true);
    }
}

esp_err_t start_weather_config_task()
{
    if (weather_config_queue != nullptr) {
        return ESP_OK;
    }
    weather_config_queue = xQueueCreateStatic(
        1U,
        sizeof(WeatherConfigRequest),
        weather_config_queue_storage,
        &weather_config_queue_control);
    if (weather_config_queue == nullptr ||
        xTaskCreateStatic(
            weather_config_task_entry,
            "pf_weather_cfg",
            kWeatherConfigTaskStackWords,
            nullptr,
            kWeatherConfigTaskPriority,
            weather_config_task_stack,
            &weather_config_task_control) == nullptr) {
        weather_config_queue = nullptr;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t weather_config_post_handler(httpd_req_t* const request)
{
    httpd_req_t* async_request = nullptr;
    if (httpd_req_async_handler_begin(request, &async_request) != ESP_OK) {
        close_request_session(request);
        return ESP_FAIL;
    }
    const auto send_async_error = [&](const char* const status,
                                      const char* const body) {
        const esp_err_t response_result = send_json(
            async_request,
            status,
            body);
        return finish_async_upload_request(
            async_request,
            response_result,
            true);
    };
    const AccessContext access = current_access_context(async_request);
    if (!access.authenticated || !access.csrf_valid) {
        const esp_err_t response_result =
            reject_management_request(async_request, access, true);
        return finish_async_upload_request(
            async_request,
            response_result,
            true);
    }
    if (async_request->content_len <= 0 ||
        static_cast<std::size_t>(async_request->content_len) >=
            kWeatherConfigBodyCapacity) {
        return send_async_error(
            "413 Payload Too Large",
            "{\"ok\":false,\"error\":\"invalid_body\"}");
    }
    if (weather_config_queue == nullptr ||
        uxQueueSpacesAvailable(weather_config_queue) == 0U) {
        return send_async_error(
            "503 Service Unavailable",
            "{\"ok\":false,\"error\":\"weather_config_busy\"}");
    }
    const WeatherConfigRequest queued{async_request};
    if (xQueueSend(weather_config_queue, &queued, 0U) != pdTRUE) {
        return send_async_error(
            "503 Service Unavailable",
            "{\"ok\":false,\"error\":\"weather_config_busy\"}");
    }
    return ESP_OK;
}

bool query_requests_refresh(httpd_req_t* request)
{
    const std::size_t query_length =
        httpd_req_get_url_query_len(request);
    if (query_length == 0U || query_length >= 48U) {
        return false;
    }
    char query[48]{};
    char value[8]{};
    return httpd_req_get_url_query_str(
               request,
               query,
               sizeof(query)) == ESP_OK &&
           httpd_query_key_value(
               query,
               "refresh",
               value,
               sizeof(value)) == ESP_OK &&
           std::strcmp(value, "1") == 0;
}

bool escape_json_string(
    const char* const source,
    char* const destination,
    const std::size_t capacity)
{
    if (source == nullptr || destination == nullptr ||
        capacity == 0U) {
        return false;
    }
    std::size_t output = 0U;
    for (std::size_t input = 0U;
         source[input] != '\0';
         ++input) {
        const auto value =
            static_cast<std::uint8_t>(source[input]);
        if (value < 0x20U) {
            if (output + 6U >= capacity) {
                return false;
            }
            constexpr char kHex[] = "0123456789abcdef";
            destination[output++] = '\\';
            destination[output++] = 'u';
            destination[output++] = '0';
            destination[output++] = '0';
            destination[output++] = kHex[value >> 4U];
            destination[output++] = kHex[value & 0x0FU];
            continue;
        }
        const bool escaped = value == '"' || value == '\\';
        if (output + (escaped ? 2U : 1U) >= capacity) {
            return false;
        }
        if (escaped) {
            destination[output++] = '\\';
        }
        destination[output++] = static_cast<char>(value);
    }
    destination[output] = '\0';
    return true;
}

esp_err_t scan_handler(httpd_req_t* request)
{
    const AccessContext access = current_access_context(request);
    if (!wifi_scan_allowed(access)) {
        return reject_management_request(request, access, false);
    }

    pf_network::ScanSnapshot snapshot{};
    if (!pf_network::network_service().scan_snapshot(snapshot)) {
        return send_json(
            request,
            "503 Service Unavailable",
            "{\"ok\":false,\"error\":\"scan_unavailable\"}");
    }

    if (query_requests_refresh(request) ||
        snapshot.state == pf_network::ScanState::idle ||
        snapshot.state == pf_network::ScanState::failed) {
        if (!pf_network::network_service().request_scan()) {
            return send_json(
                request,
                "503 Service Unavailable",
                "{\"ok\":false,\"error\":\"scan_queue_full\"}");
        }
        return send_json(
            request,
            "202 Accepted",
            "{\"ok\":true,\"data\":{\"state\":\"scanning\"}}");
    }
    if (snapshot.state == pf_network::ScanState::scanning) {
        return send_json(
            request,
            "202 Accepted",
            "{\"ok\":true,\"data\":{\"state\":\"scanning\"}}");
    }

    esp_err_t result =
        set_common_headers(
            request,
            "application/json; charset=utf-8");
    if (result == ESP_OK) {
        result = httpd_resp_send_chunk(
            request,
            "{\"ok\":true,\"data\":{\"state\":\"ready\","
            "\"networks\":[",
            HTTPD_RESP_USE_STRLEN);
    }
    for (std::size_t index = 0U;
         result == ESP_OK && index < snapshot.count;
         ++index) {
        char escaped_ssid[200]{};
        char item[304]{};
        if (!escape_json_string(
                snapshot.results[index].ssid,
                escaped_ssid,
                sizeof(escaped_ssid))) {
            return ESP_FAIL;
        }
        const int written = std::snprintf(
            item,
            sizeof(item),
            "%s{\"ssid\":\"%s\",\"rssi\":%d,\"security\":\"%s\"}",
            index == 0U ? "" : ",",
            escaped_ssid,
            static_cast<int>(snapshot.results[index].rssi),
            pf_network::to_string(
                snapshot.results[index].security));
        if (written <= 0 ||
            static_cast<std::size_t>(written) >= sizeof(item)) {
            return ESP_FAIL;
        }
        result = httpd_resp_send_chunk(
            request,
            item,
            written);
    }
    if (result == ESP_OK) {
        result = httpd_resp_send_chunk(
            request,
            "]}}",
            HTTPD_RESP_USE_STRLEN);
    }
    if (result == ESP_OK) {
        result = httpd_resp_send_chunk(request, nullptr, 0);
    }
    return result;
}

esp_err_t wifi_config_handler(httpd_req_t* request)
{
    const AccessContext access = current_access_context(request);
    if (!wifi_config_allowed(access)) {
        return reject_management_request(request, access, true);
    }
    if (request->content_len <= 0 ||
        request->content_len >= 320) {
        return send_json(
            request,
            "413 Payload Too Large",
            "{\"ok\":false,\"error\":\"invalid_body\"}");
    }

    char body[320]{};
    const pf_config::SecureZeroGuard body_guard(body);
    std::size_t received = 0U;
    std::uint8_t timeout_count = 0U;
    const std::uint64_t receive_started_ms =
        static_cast<std::uint64_t>(esp_timer_get_time()) / 1000U;
    while (received <
           static_cast<std::size_t>(request->content_len)) {
        const std::uint64_t now_ms =
            static_cast<std::uint64_t>(esp_timer_get_time()) / 1000U;
        if (body_receive_deadline_expired(
                now_ms,
                receive_started_ms,
                kMaximumBodyReceiveMs)) {
            return send_json(
                request,
                "408 Request Timeout",
                "{\"ok\":false,\"error\":\"body_timeout\"}");
        }
        const int result = httpd_req_recv(
            request,
            body + received,
            request->content_len -
                static_cast<int>(received));
        if (result == HTTPD_SOCK_ERR_TIMEOUT) {
            ++timeout_count;
            if (body_receive_idle_limit_reached(
                    timeout_count,
                    kMaximumBodyTimeouts)) {
                return send_json(
                    request,
                    "408 Request Timeout",
                    "{\"ok\":false,\"error\":\"body_timeout\"}");
            }
            continue;
        }
        if (result <= 0) {
            return send_json(
                request,
                "400 Bad Request",
                "{\"ok\":false,\"error\":\"body_receive_failed\"}");
        }
        const std::uint64_t received_at_ms =
            static_cast<std::uint64_t>(esp_timer_get_time()) / 1000U;
        if (body_receive_deadline_expired(
                received_at_ms,
                receive_started_ms,
                kMaximumBodyReceiveMs)) {
            return send_json(
                request,
                "408 Request Timeout",
                "{\"ok\":false,\"error\":\"body_timeout\"}");
        }
        received += static_cast<std::size_t>(result);
    }

    ProvisioningForm form{};
    const pf_config::SecureZeroGuard form_guard(form);
    const ProvisioningParseStatus parsed =
        parse_provisioning_form(body, received, form);
    if (parsed != ProvisioningParseStatus::ok) {
        char response[112]{};
        std::snprintf(
            response,
            sizeof(response),
            "{\"ok\":false,\"error\":\"%s\"}",
            to_string(parsed));
        return send_json(
            request,
            "400 Bad Request",
            response);
    }

    pf_config::NetworkCredentials credentials{};
    const pf_config::SecureZeroGuard credentials_guard(credentials);
    std::memcpy(
        credentials.ssid,
        form.ssid,
        sizeof(form.ssid));
    std::memcpy(
        credentials.password,
        form.password,
        sizeof(form.password));
    const ProvisioningSubmitResult submitted =
        provisioning_service().submit(credentials);
    if (submitted.status == ProvisioningSubmitStatus::busy) {
        return send_json(
            request,
            "409 Conflict",
            "{\"ok\":false,\"error\":\"provisioning_busy\"}");
    }
    if (submitted.status != ProvisioningSubmitStatus::accepted) {
        return send_json(
            request,
            "503 Service Unavailable",
            "{\"ok\":false,\"error\":\"storage_unavailable\"}");
    }
    char response[160]{};
    const ProvisioningOperationStatus status{
        .request_id = submitted.request_id,
        .state = ProvisioningOperationState::saving,
    };
    if (!serialize_provisioning_status(
            status,
            response,
            sizeof(response))) {
        return send_json(
            request,
            "500 Internal Server Error",
            "{\"ok\":false,\"error\":\"status_serialization\"}");
    }
    return send_json(
        request,
        "202 Accepted",
        response);
}

bool query_request_id(
    httpd_req_t* const request,
    std::uint32_t& request_id)
{
    const std::size_t query_length =
        httpd_req_get_url_query_len(request);
    if (query_length == 0U || query_length >= 64U) {
        return false;
    }
    char query[64]{};
    char value[16]{};
    if (httpd_req_get_url_query_str(
            request,
            query,
            sizeof(query)) != ESP_OK ||
        httpd_query_key_value(
            query,
            "request_id",
            value,
            sizeof(value)) != ESP_OK ||
        value[0] == '\0') {
        return false;
    }

    std::uint32_t parsed = 0U;
    for (std::size_t index = 0U;
         value[index] != '\0';
         ++index) {
        if (value[index] < '0' || value[index] > '9') {
            return false;
        }
        const std::uint32_t digit =
            static_cast<std::uint32_t>(value[index] - '0');
        if (parsed >
            (UINT32_MAX - digit) / 10U) {
            return false;
        }
        parsed = parsed * 10U + digit;
    }
    request_id = parsed;
    return request_id != 0U;
}

esp_err_t wifi_config_status_handler(httpd_req_t* request)
{
    const AccessContext access = current_access_context(request);
    if (!wifi_scan_allowed(access)) {
        return reject_management_request(request, access, false);
    }

    std::uint32_t request_id = 0U;
    if (!query_request_id(request, request_id)) {
        return send_json(
            request,
            "400 Bad Request",
            "{\"ok\":false,\"error\":\"invalid_request_id\"}");
    }
    ProvisioningOperationStatus status{};
    if (!provisioning_service().status(status)) {
        return send_json(
            request,
            "503 Service Unavailable",
            "{\"ok\":false,\"error\":\"storage_unavailable\"}");
    }
    if (!provisioning_status_matches(status, request_id)) {
        return send_json(
            request,
            "404 Not Found",
            "{\"ok\":false,\"error\":\"request_not_found\"}");
    }

    char response[160]{};
    if (!serialize_provisioning_status(
            status,
            response,
            sizeof(response))) {
        return send_json(
            request,
            "500 Internal Server Error",
            "{\"ok\":false,\"error\":\"status_serialization\"}");
    }
    const char* const http_status =
        status.state == ProvisioningOperationState::saving
            ? "202 Accepted"
            : status.state == ProvisioningOperationState::failed
                  ? "500 Internal Server Error"
                  : nullptr;
    const esp_err_t result =
        send_json(request, http_status, response);
    if (result == ESP_OK &&
        (status.state ==
             ProvisioningOperationState::committed ||
         status.state ==
             ProvisioningOperationState::failed)) {
        provisioning_service().acknowledge_terminal(
            request_id);
    }
    return result;
}

esp_err_t auth_status_handler(httpd_req_t* request)
{
    pf_auth::RequestAuthentication authentication =
        request_authentication(request);
    const pf_config::SecureZeroGuard authentication_guard(
        authentication);
    char response[256]{};
    const pf_config::SecureZeroGuard response_guard(response);
    const int written = std::snprintf(
        response,
        sizeof(response),
        "{\"ok\":true,\"data\":{\"username\":\"admin\","
        "\"password_configured\":%s,\"authenticated\":%s,"
        "\"csrf_token\":%s%s%s}}",
        authentication.password_configured ? "true" : "false",
        authentication.authenticated ? "true" : "false",
        authentication.authenticated ? "\"" : "",
        authentication.authenticated
            ? authentication.csrf_token
            : "null",
        authentication.authenticated ? "\"" : "");
    if (written <= 0 ||
        static_cast<std::size_t>(written) >= sizeof(response)) {
        return send_json(
            request,
            "500 Internal Server Error",
            "{\"ok\":false,\"error\":\"auth_serialization\"}");
    }
    return send_json(request, nullptr, response);
}

esp_err_t receive_auth_body(
    httpd_req_t* const request,
    char* const body,
    const std::size_t capacity,
    std::size_t& received)
{
    if (request->content_len <= 0 ||
        static_cast<std::size_t>(request->content_len) >= capacity) {
        return ESP_ERR_INVALID_SIZE;
    }
    received = 0U;
    std::uint8_t timeout_count = 0U;
    const std::uint64_t started_ms = monotonic_ms();
    while (received <
           static_cast<std::size_t>(request->content_len)) {
        if (body_receive_deadline_expired(
                monotonic_ms(),
                started_ms,
                kMaximumBodyReceiveMs)) {
            return ESP_ERR_TIMEOUT;
        }
        const int result = httpd_req_recv(
            request,
            body + received,
            request->content_len -
                static_cast<int>(received));
        if (result == HTTPD_SOCK_ERR_TIMEOUT) {
            ++timeout_count;
            if (body_receive_idle_limit_reached(
                    timeout_count,
                    kMaximumBodyTimeouts)) {
                return ESP_ERR_TIMEOUT;
            }
            continue;
        }
        if (result <= 0) {
            return ESP_FAIL;
        }
        received += static_cast<std::size_t>(result);
    }
    return ESP_OK;
}

esp_err_t auth_login_handler(httpd_req_t* request)
{
    char body[512]{};
    const pf_config::SecureZeroGuard body_guard(body);
    std::size_t received = 0U;
    const esp_err_t receive_result =
        receive_auth_body(
            request,
            body,
            sizeof(body),
            received);
    if (receive_result == ESP_ERR_INVALID_SIZE) {
        return send_json(
            request,
            "413 Payload Too Large",
            "{\"ok\":false,\"error\":\"invalid_body\"}");
    }
    if (receive_result == ESP_ERR_TIMEOUT) {
        return send_json(
            request,
            "408 Request Timeout",
            "{\"ok\":false,\"error\":\"body_timeout\"}");
    }
    if (receive_result != ESP_OK) {
        return send_json(
            request,
            "400 Bad Request",
            "{\"ok\":false,\"error\":\"body_receive_failed\"}");
    }

    AuthForm form{};
    const pf_config::SecureZeroGuard form_guard(form);
    const AuthParseStatus parsed =
        parse_auth_form(body, received, form);
    if (parsed != AuthParseStatus::ok) {
        char response[112]{};
        std::snprintf(
            response,
            sizeof(response),
            "{\"ok\":false,\"error\":\"%s\"}",
            to_string(parsed));
        return send_json(request, "400 Bad Request", response);
    }

    const bool setup_allowed =
        password_setup_allowed(current_access_context(request));
    pf_auth::LoginSubmitResult login =
        pf_auth::auth_service().submit_login(
        form.username,
        form.password,
        setup_allowed,
        monotonic_ms());
    const pf_config::SecureZeroGuard login_guard(login);
    if (login.status == pf_auth::LoginSubmitStatus::invalid) {
        return send_json(
            request,
            "400 Bad Request",
            "{\"ok\":false,\"error\":\"invalid_credentials\"}");
    }
    if (login.status == pf_auth::LoginSubmitStatus::busy) {
        return send_json(
            request,
            "409 Conflict",
            "{\"ok\":false,\"error\":\"authentication_busy\"}");
    }
    if (login.status != pf_auth::LoginSubmitStatus::accepted) {
        return send_json(
            request,
            "503 Service Unavailable",
            "{\"ok\":false,\"error\":\"authentication_unavailable\"}");
    }

    char response[192]{};
    const pf_config::SecureZeroGuard response_guard(response);
    const int written = std::snprintf(
        response,
        sizeof(response),
        "{\"ok\":true,\"data\":{\"state\":\"verifying\","
        "\"request_token\":\"%s\"}}",
        login.request_token);
    if (written <= 0 ||
        static_cast<std::size_t>(written) >= sizeof(response)) {
        return ESP_FAIL;
    }
    return send_json(request, "202 Accepted", response);
}

esp_err_t auth_login_status_handler(httpd_req_t* request)
{
    char request_token[pf_auth::kEncodedSecretCapacity]{};
    const pf_config::SecureZeroGuard request_token_guard(
        request_token);
    if (!request_auth_operation_token(request, request_token)) {
        return send_json(
            request,
            "400 Bad Request",
            "{\"ok\":false,\"error\":\"invalid_auth_request\"}");
    }

    pf_auth::LoginOperationSnapshot snapshot{};
    const pf_config::SecureZeroGuard snapshot_guard(snapshot);
    if (!pf_auth::auth_service().login_status(
            request_token,
            snapshot)) {
        return send_json(
            request,
            "404 Not Found",
            "{\"ok\":false,\"error\":\"auth_request_not_found\"}");
    }
    if (snapshot.state ==
        pf_auth::LoginOperationState::verifying) {
        return send_json(
            request,
            "202 Accepted",
            "{\"ok\":true,\"data\":{\"state\":\"verifying\"}}");
    }

    const char* http_status = nullptr;
    const char* error_response = nullptr;
    switch (snapshot.state) {
        case pf_auth::LoginOperationState::invalid_credentials:
            http_status = "401 Unauthorized";
            error_response =
                "{\"ok\":false,\"error\":\"invalid_credentials\"}";
            break;
        case pf_auth::LoginOperationState::setup_forbidden:
            http_status = "403 Forbidden";
            error_response =
                "{\"ok\":false,\"error\":\"password_setup_forbidden\"}";
            break;
        case pf_auth::LoginOperationState::failed:
            http_status = "503 Service Unavailable";
            error_response =
                "{\"ok\":false,\"error\":\"authentication_unavailable\"}";
            break;
        case pf_auth::LoginOperationState::authenticated:
        case pf_auth::LoginOperationState::password_created:
            break;
        case pf_auth::LoginOperationState::idle:
        case pf_auth::LoginOperationState::verifying:
            return send_json(
                request,
                "500 Internal Server Error",
                "{\"ok\":false,\"error\":\"auth_state_invalid\"}");
    }

    if (error_response != nullptr) {
        const esp_err_t result =
            send_json(request, http_status, error_response);
        if (result == ESP_OK) {
            pf_auth::auth_service().acknowledge_login(
                request_token);
        }
        return result;
    }

    char cookie[128]{};
    const pf_config::SecureZeroGuard cookie_guard(cookie);
    const int cookie_length = std::snprintf(
        cookie,
        sizeof(cookie),
        "%s=%s; Path=/; HttpOnly; SameSite=Strict",
        kSessionCookieName,
        snapshot.result.session_token);
    if (cookie_length <= 0 ||
        static_cast<std::size_t>(cookie_length) >= sizeof(cookie) ||
        httpd_resp_set_hdr(
            request,
            "Set-Cookie",
            cookie) != ESP_OK) {
        return ESP_FAIL;
    }

    char response[192]{};
    const pf_config::SecureZeroGuard response_guard(response);
    const int written = std::snprintf(
        response,
        sizeof(response),
        "{\"ok\":true,\"data\":{\"username\":\"admin\","
        "\"authenticated\":true,\"password_created\":%s,"
        "\"csrf_token\":\"%s\"}}",
        snapshot.state ==
                pf_auth::LoginOperationState::password_created
            ? "true"
            : "false",
        snapshot.result.csrf_token);
    if (written <= 0 ||
        static_cast<std::size_t>(written) >= sizeof(response)) {
        return ESP_FAIL;
    }
    const esp_err_t result =
        send_json(request, nullptr, response);
    if (result == ESP_OK) {
        pf_auth::auth_service().acknowledge_login(
            request_token);
    }
    return result;
}

esp_err_t auth_logout_handler(httpd_req_t* request)
{
    pf_auth::RequestAuthentication authentication =
        request_authentication(request, false);
    const pf_config::SecureZeroGuard authentication_guard(
        authentication);
    if (!authentication.authenticated) {
        return send_json(
            request,
            "401 Unauthorized",
            "{\"ok\":false,\"error\":\"authentication_required\"}");
    }
    if (!authentication.csrf_valid) {
        return send_json(
            request,
            "403 Forbidden",
            "{\"ok\":false,\"error\":\"csrf_required\"}");
    }

    char session_token[pf_auth::kEncodedSecretCapacity]{};
    char csrf_token[pf_auth::kEncodedSecretCapacity]{};
    const pf_config::SecureZeroGuard session_guard(session_token);
    const pf_config::SecureZeroGuard csrf_guard(csrf_token);
    if (!request_session_token(request, session_token) ||
        !request_csrf_token(request, csrf_token) ||
        !pf_auth::auth_service().logout(
            session_token,
            csrf_token,
            monotonic_ms())) {
        return send_json(
            request,
            "401 Unauthorized",
            "{\"ok\":false,\"error\":\"authentication_required\"}");
    }
    if (httpd_resp_set_hdr(
            request,
            "Set-Cookie",
            "pf_session=; Path=/; Max-Age=0; HttpOnly; "
            "SameSite=Strict") != ESP_OK) {
        return ESP_FAIL;
    }
    return send_json(
        request,
        nullptr,
        "{\"ok\":true,\"data\":{\"authenticated\":false}}");
}

struct ImageListStreamContext {
    httpd_req_t* request = nullptr;
    bool first = true;
    bool failed = false;
};

bool send_image_list_entry(
    void* const raw_context,
    const pf_storage::CatalogEntry& entry)
{
    auto* const context = static_cast<ImageListStreamContext*>(raw_context);
    if (context == nullptr || context->request == nullptr) {
        return false;
    }
    char serialized[640]{};
    std::size_t written = 0U;
    if (!serialize_image_entry(
            entry,
            serialized,
            sizeof(serialized),
            written)) {
        context->failed = true;
        return false;
    }
    if (!context->first &&
        httpd_resp_sendstr_chunk(context->request, ",") != ESP_OK) {
        context->failed = true;
        return false;
    }
    if (httpd_resp_send_chunk(
            context->request,
            serialized,
            written) != ESP_OK) {
        context->failed = true;
        return false;
    }
    context->first = false;
    return true;
}

esp_err_t image_list_handler(httpd_req_t* request)
{
    const AccessContext access = current_access_context(request);
    if (!access.authenticated) {
        return reject_management_request(request, access, false);
    }
    pf_storage::StorageWorker* const worker =
        server_access_config.storage_worker;
    if (worker == nullptr || !worker->ready()) {
        return send_json(
            request,
            "503 Service Unavailable",
            "{\"ok\":false,\"error\":\"storage_unavailable\"}");
    }
    if (worker->operation_busy()) {
        return send_json(
            request,
            "503 Service Unavailable",
            "{\"ok\":false,\"error\":\"storage_busy\"}");
    }
    if (set_common_headers(
            request,
            "application/json; charset=utf-8") != ESP_OK ||
        httpd_resp_set_status(request, "200 OK") != ESP_OK ||
        httpd_resp_sendstr_chunk(
            request,
            kImageListJsonPrefix) != ESP_OK) {
        return ESP_FAIL;
    }

    ImageListStreamContext context{request};
    if (!worker->visit_catalog(send_image_list_entry, &context) ||
        context.failed ||
        httpd_resp_sendstr_chunk(request, kImageListJsonSuffix) != ESP_OK ||
        httpd_resp_sendstr_chunk(request, nullptr) != ESP_OK) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

struct ImageDownloadStreamContext {
    httpd_req_t* request = nullptr;
    bool failed = false;
};

bool send_image_download_chunk(
    void* const raw_context,
    const std::uint8_t* const data,
    const std::size_t length)
{
    auto* const context =
        static_cast<ImageDownloadStreamContext*>(raw_context);
    if (context == nullptr || context->request == nullptr ||
        data == nullptr || length == 0U) {
        return false;
    }
    if (httpd_resp_send_chunk(
            context->request,
            reinterpret_cast<const char*>(data),
            length) != ESP_OK) {
        context->failed = true;
        return false;
    }
    return true;
}

void image_download_task_entry(void* const)
{
    for (;;) {
        ImageDownloadRequest queued{};
        if (xQueueReceive(
                image_download_queue,
                &queued,
                portMAX_DELAY) != pdTRUE) {
            continue;
        }
        ImageDownloadStreamContext stream_context{queued.request};
        const bool headers_ready =
            queued.request != nullptr &&
            set_common_headers(
                queued.request,
                "application/vnd.paperframe.pfr1") == ESP_OK &&
            httpd_resp_set_status(queued.request, "200 OK") == ESP_OK &&
            httpd_resp_set_hdr(
                queued.request,
                "Content-Disposition",
                queued.content_disposition) == ESP_OK;
        pf_storage::ImageStreamResult result{
            pf_storage::ImageStreamError::not_ready,
            0U};
        if (headers_ready && queued.worker != nullptr) {
            result = queued.worker->stream_image(
                queued.name,
                queued.name_length,
                send_image_download_chunk,
                &stream_context);
        }
        const bool completed =
            headers_ready &&
            result.ok() &&
            !stream_context.failed &&
            result.bytes_sent == queued.file_bytes &&
            httpd_resp_send_chunk(
                queued.request,
                nullptr,
                0U) == ESP_OK;
        if (!completed && queued.request != nullptr) {
            const int socket = httpd_req_to_sockfd(queued.request);
            if (socket >= 0) {
                httpd_sess_trigger_close(
                    queued.request->handle,
                    socket);
            }
        }
        if (queued.request != nullptr) {
            httpd_req_async_handler_complete(queued.request);
        }
    }
}

esp_err_t start_image_download_task()
{
    if (image_download_queue != nullptr) {
        return ESP_OK;
    }
    image_download_queue = xQueueCreateStatic(
        1U,
        sizeof(ImageDownloadRequest),
        image_download_queue_storage,
        &image_download_queue_control);
    if (image_download_queue == nullptr ||
        xTaskCreateStatic(
            image_download_task_entry,
            "pf_image_dl",
            kImageDownloadTaskStackWords,
            nullptr,
            kImageDownloadTaskPriority,
            image_download_task_stack,
            &image_download_task_control) == nullptr) {
        image_download_queue = nullptr;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t image_download_handler(httpd_req_t* request)
{
    const AccessContext access = current_access_context(request);
    if (!access.authenticated) {
        return reject_management_request(request, access, false);
    }

    char name[pf_storage::kCatalogNameCapacity]{};
    std::size_t name_length = 0U;
    if (!decode_image_download_uri(request->uri, name, name_length)) {
        return send_json(
            request,
            "400 Bad Request",
            "{\"ok\":false,\"error\":\"invalid_image_name\"}");
    }
    pf_storage::StorageWorker* const worker =
        server_access_config.storage_worker;
    if (worker == nullptr || !worker->ready()) {
        return send_json(
            request,
            "503 Service Unavailable",
            "{\"ok\":false,\"error\":\"storage_unavailable\"}");
    }
    pf_storage::CatalogEntry entry{};
    if (!worker->find_catalog_entry_by_name(
            name,
            name_length,
            entry)) {
        if (worker->operation_busy()) {
            return send_json(
                request,
                "503 Service Unavailable",
                "{\"ok\":false,\"error\":\"storage_busy\"}");
        }
        return send_json(
            request,
            "404 Not Found",
            "{\"ok\":false,\"error\":\"image_not_found\"}");
    }
    if ((entry.flags & pf_storage::kCatalogCorrupt) != 0U) {
        return send_json(
            request,
            "409 Conflict",
            "{\"ok\":false,\"error\":\"image_corrupt\"}");
    }

    ImageDownloadRequest queued{};
    queued.worker = worker;
    queued.name_length = name_length;
    queued.file_bytes = entry.file_bytes;
    std::memcpy(queued.name, name, name_length + 1U);
    std::size_t disposition_written = 0U;
    if (!serialize_image_content_disposition(
            entry,
            queued.content_disposition,
            sizeof(queued.content_disposition),
            disposition_written) ||
        image_download_queue == nullptr) {
        return ESP_FAIL;
    }

    if (uxQueueSpacesAvailable(image_download_queue) == 0U) {
        return send_json(
            request,
            "503 Service Unavailable",
            "{\"ok\":false,\"error\":\"download_busy\"}");
    }
    httpd_req_t* async_request = nullptr;
    if (httpd_req_async_handler_begin(request, &async_request) != ESP_OK) {
        return send_json(
            request,
            "503 Service Unavailable",
            "{\"ok\":false,\"error\":\"download_unavailable\"}");
    }
    queued.request = async_request;
    if (xQueueSend(image_download_queue, &queued, 0U) != pdTRUE) {
        const esp_err_t busy_result = send_json(
            async_request,
            "503 Service Unavailable",
            "{\"ok\":false,\"error\":\"download_busy\"}");
        httpd_req_async_handler_complete(async_request);
        return busy_result == ESP_OK ? ESP_OK : ESP_FAIL;
    }
    return ESP_OK;
}

pf_storage::StorageReadResult receive_image_upload_chunk(
    void* const raw_context,
    std::uint8_t* const buffer,
    const std::size_t capacity,
    std::size_t& bytes_read)
{
    auto* const context =
        static_cast<ImageUploadReceiveContext*>(raw_context);
    bytes_read = 0U;
    if (context == nullptr || context->request == nullptr ||
        buffer == nullptr || capacity == 0U) {
        return pf_storage::StorageReadResult::error;
    }
    if (context->remaining == 0U) {
        return pf_storage::StorageReadResult::eof;
    }
    while (true) {
        if (body_receive_deadline_expired(
                monotonic_ms(),
                context->started_ms,
                kMaximumBodyReceiveMs)) {
            return pf_storage::StorageReadResult::error;
        }
        const std::size_t amount =
            context->remaining < capacity ? context->remaining : capacity;
        const int result = httpd_req_recv(
            context->request,
            reinterpret_cast<char*>(buffer),
            static_cast<int>(amount));
        if (result == HTTPD_SOCK_ERR_TIMEOUT) {
            ++context->timeout_count;
            if (body_receive_idle_limit_reached(
                    context->timeout_count,
                    kMaximumBodyTimeouts)) {
                return pf_storage::StorageReadResult::error;
            }
            continue;
        }
        if (result <= 0) {
            return pf_storage::StorageReadResult::error;
        }
        const std::size_t received = static_cast<std::size_t>(result);
        if (received > context->remaining) {
            return pf_storage::StorageReadResult::error;
        }
        context->remaining -= received;
        bytes_read = received;
        return pf_storage::StorageReadResult::data;
    }
}

const char* image_store_http_status(const pf_storage::ImageStoreError error)
{
    switch (error) {
        case pf_storage::ImageStoreError::invalid_argument:
            return "400 Bad Request";
        case pf_storage::ImageStoreError::not_ready:
        case pf_storage::ImageStoreError::busy:
            return "503 Service Unavailable";
        case pf_storage::ImageStoreError::no_space:
            return "507 Insufficient Storage";
        case pf_storage::ImageStoreError::invalid_image:
            return "422 Unprocessable Entity";
        case pf_storage::ImageStoreError::image_conflict:
            return "409 Conflict";
        case pf_storage::ImageStoreError::stream_failed:
            return "400 Bad Request";
        case pf_storage::ImageStoreError::none:
            return nullptr;
        case pf_storage::ImageStoreError::write_failed:
        case pf_storage::ImageStoreError::close_failed:
        case pf_storage::ImageStoreError::catalog_read_failed:
        case pf_storage::ImageStoreError::catalog_invalid:
        case pf_storage::ImageStoreError::path_too_long:
        case pf_storage::ImageStoreError::remove_failed:
        case pf_storage::ImageStoreError::rename_failed:
        case pf_storage::ImageStoreError::rollback_failed:
            return "500 Internal Server Error";
    }
    return "500 Internal Server Error";
}

esp_err_t send_image_upload_result(
    httpd_req_t* const request,
    const pf_storage::ImageStoreResult& result)
{
    char response[224]{};
    if (result.ok()) {
        const int written = std::snprintf(
            response,
            sizeof(response),
            "{\"ok\":true,\"data\":{\"id\":%u,\"bytes\":%u}}",
            static_cast<unsigned>(result.assigned_id),
            static_cast<unsigned>(result.bytes_received));
        if (written <= 0 ||
            static_cast<std::size_t>(written) >= sizeof(response)) {
            return send_json(
                request,
                "500 Internal Server Error",
                "{\"ok\":false,\"error\":\"response_serialization\"}");
        }
        return send_json(request, "201 Created", response);
    }
    const int written = std::snprintf(
        response,
        sizeof(response),
        "{\"ok\":false,\"error\":\"%s\",\"bytes_received\":%u}",
        to_string(result.error),
        static_cast<unsigned>(result.bytes_received));
    if (written <= 0 ||
        static_cast<std::size_t>(written) >= sizeof(response)) {
        return send_json(
            request,
            "500 Internal Server Error",
            "{\"ok\":false,\"error\":\"response_serialization\"}");
    }
    return send_json(
        request,
        image_store_http_status(result.error),
        response);
}

void image_upload_task_entry(void* const)
{
    for (;;) {
        ImageUploadRequest queued{};
        if (xQueueReceive(
                image_upload_queue,
                &queued,
                portMAX_DELAY) != pdTRUE) {
            continue;
        }
        pf_storage::ImageStoreResult result{};
        bool close_session = true;
        if (queued.request == nullptr || queued.worker == nullptr) {
            result.error = pf_storage::ImageStoreError::invalid_argument;
        } else {
            ImageUploadReceiveContext receive_context{
                queued.request,
                queued.content_length,
                0U,
                monotonic_ms(),
            };
            const pf_storage::StorageStreamReader reader{
                receive_image_upload_chunk,
                &receive_context,
            };
            result = queued.worker->store_image(
                reader,
                queued.content_length);
            close_session = !result.ok() || receive_context.remaining != 0U;
        }
        if (queued.request != nullptr) {
            const esp_err_t response_result =
                send_image_upload_result(queued.request, result);
            if (response_result != ESP_OK) {
                close_session = true;
            }
            finish_async_upload_request(
                queued.request,
                response_result,
                close_session);
        }
    }
}

esp_err_t start_image_upload_task()
{
    if (image_upload_queue != nullptr) {
        return ESP_OK;
    }
    image_upload_queue = xQueueCreateStatic(
        1U,
        sizeof(ImageUploadRequest),
        image_upload_queue_storage,
        &image_upload_queue_control);
    if (image_upload_queue == nullptr ||
        xTaskCreateStatic(
            image_upload_task_entry,
            "pf_image_up",
            kImageUploadTaskStackWords,
            nullptr,
            kImageUploadTaskPriority,
            image_upload_task_stack,
            &image_upload_task_control) == nullptr) {
        image_upload_queue = nullptr;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

const char* image_mutation_http_status(
    const pf_storage::ImageStoreError error)
{
    switch (error) {
        case pf_storage::ImageStoreError::invalid_argument:
        case pf_storage::ImageStoreError::catalog_invalid:
            return "400 Bad Request";
        case pf_storage::ImageStoreError::not_ready:
        case pf_storage::ImageStoreError::busy:
            return "503 Service Unavailable";
        case pf_storage::ImageStoreError::no_space:
            return "507 Insufficient Storage";
        case pf_storage::ImageStoreError::remove_failed:
        case pf_storage::ImageStoreError::write_failed:
        case pf_storage::ImageStoreError::close_failed:
        case pf_storage::ImageStoreError::catalog_read_failed:
        case pf_storage::ImageStoreError::path_too_long:
        case pf_storage::ImageStoreError::rename_failed:
        case pf_storage::ImageStoreError::rollback_failed:
            return "500 Internal Server Error";
        case pf_storage::ImageStoreError::none:
        case pf_storage::ImageStoreError::stream_failed:
        case pf_storage::ImageStoreError::invalid_image:
        case pf_storage::ImageStoreError::image_conflict:
            return "400 Bad Request";
    }
    return "500 Internal Server Error";
}

esp_err_t send_image_mutation_result(
    httpd_req_t* const request,
    const pf_storage::ImageStoreResult& result)
{
    char response[224]{};
    if (result.ok()) {
        const int written = std::snprintf(
            response,
            sizeof(response),
            "{\"ok\":true,\"data\":{\"id\":%u}}",
            static_cast<unsigned>(result.assigned_id));
        if (written <= 0 ||
            static_cast<std::size_t>(written) >= sizeof(response)) {
            return send_json(
                request,
                "500 Internal Server Error",
                "{\"ok\":false,\"error\":\"response_serialization\"}");
        }
        return send_json(request, "200 OK", response);
    }
    const int written = std::snprintf(
        response,
        sizeof(response),
        result.catalog_committed
            ? "{\"ok\":false,\"error\":\"%s\","
              "\"catalog_committed\":true,\"id\":%u}"
            : "{\"ok\":false,\"error\":\"%s\"}",
        pf_storage::to_string(result.error),
        static_cast<unsigned>(result.assigned_id));
    if (written <= 0 ||
        static_cast<std::size_t>(written) >= sizeof(response)) {
        return send_json(
            request,
            "500 Internal Server Error",
            "{\"ok\":false,\"error\":\"response_serialization\"}");
    }
    return send_json(
        request,
        image_mutation_http_status(result.error),
        response);
}

bool receive_image_order_body(
    httpd_req_t* request,
    std::uint32_t (&ids)[pf_storage::kCatalogMaxEntries],
    std::uint16_t& count);

void image_mutation_task_entry(void* const)
{
    for (;;) {
        ImageMutationRequest queued{};
        if (xQueueReceive(
                image_mutation_queue,
                &queued,
                portMAX_DELAY) != pdTRUE) {
            continue;
        }
        pf_storage::ImageStoreResult result{};
        if (queued.worker == nullptr) {
            result.error = pf_storage::ImageStoreError::invalid_argument;
        } else if (queued.kind == ImageMutationKind::activate) {
            result = queued.worker->activate_image(queued.image_id);
        } else if (queued.kind == ImageMutationKind::remove) {
            result = queued.worker->remove_image(queued.image_id);
        } else {
            if (!receive_image_order_body(
                    queued.request,
                    queued.order_ids,
                    queued.order_count)) {
                result.error = pf_storage::ImageStoreError::invalid_argument;
            } else {
                result = queued.worker->reorder_images(
                    queued.order_ids,
                    queued.order_count);
            }
        }
        if (queued.request != nullptr) {
            const esp_err_t response_result =
                send_image_mutation_result(queued.request, result);
            finish_async_upload_request(
                queued.request,
                response_result,
                !result.ok() || response_result != ESP_OK);
        }
    }
}

esp_err_t start_image_mutation_task()
{
    if (image_mutation_queue != nullptr) {
        return ESP_OK;
    }
    image_mutation_queue = xQueueCreateStatic(
        1U,
        sizeof(ImageMutationRequest),
        image_mutation_queue_storage,
        &image_mutation_queue_control);
    if (image_mutation_queue == nullptr ||
        xTaskCreateStatic(
            image_mutation_task_entry,
            "pf_image_mut",
            kImageMutationTaskStackWords,
            nullptr,
            kImageMutationTaskPriority,
            image_mutation_task_stack,
            &image_mutation_task_control) == nullptr) {
        image_mutation_queue = nullptr;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

bool receive_image_order_body(
    httpd_req_t* const request,
    std::uint32_t (&ids)[pf_storage::kCatalogMaxEntries],
    std::uint16_t& count)
{
    count = 0U;
    if (request == nullptr || request->content_len <= 0 ||
        request->content_len >= 512) {
        return false;
    }
    char body[512]{};
    std::size_t received = 0U;
    std::uint8_t timeout_count = 0U;
    const std::uint64_t started_ms = monotonic_ms();
    while (received < static_cast<std::size_t>(request->content_len)) {
        if (body_receive_deadline_expired(
                monotonic_ms(),
                started_ms,
                kMaximumBodyReceiveMs)) {
            return false;
        }
        const int result = httpd_req_recv(
            request,
            body + received,
            request->content_len - static_cast<int>(received));
        if (result == HTTPD_SOCK_ERR_TIMEOUT) {
            ++timeout_count;
            if (body_receive_idle_limit_reached(
                    timeout_count,
                    kMaximumBodyTimeouts)) {
                return false;
            }
            continue;
        }
        if (result <= 0) {
            return false;
        }
        received += static_cast<std::size_t>(result);
    }
    body[received] = '\0';

    const char* cursor = std::strchr(body, '[');
    if (cursor == nullptr) {
        return false;
    }
    ++cursor;
    while (*cursor != '\0') {
        while (*cursor == ' ' || *cursor == '\t' ||
               *cursor == '\r' || *cursor == '\n') {
            ++cursor;
        }
        if (*cursor == ']') {
            return count == 0U;
        }
        if (count >= pf_storage::kCatalogMaxEntries ||
            *cursor < '0' || *cursor > '9') {
            return false;
        }
        std::uint64_t value = 0U;
        while (*cursor >= '0' && *cursor <= '9') {
            value = (value * 10U) + static_cast<unsigned>(*cursor - '0');
            if (value > UINT32_MAX) {
                return false;
            }
            ++cursor;
        }
        if (value == 0U) {
            return false;
        }
        ids[count++] = static_cast<std::uint32_t>(value);
        while (*cursor == ' ' || *cursor == '\t' ||
               *cursor == '\r' || *cursor == '\n') {
            ++cursor;
        }
        if (*cursor == ',') {
            ++cursor;
            continue;
        }
        if (*cursor == ']') {
            return true;
        }
        return false;
    }
    return false;
}

esp_err_t image_mutation_handler(
    httpd_req_t* const request,
    const ImageMutationKind kind)
{
    httpd_req_t* async_request = nullptr;
    if (httpd_req_async_handler_begin(request, &async_request) != ESP_OK) {
        (void)send_json(
            request,
            "503 Service Unavailable",
            "{\"ok\":false,\"error\":\"mutation_unavailable\"}");
        return ESP_FAIL;
    }
    const auto send_error = [&](const char* const status,
                                const char* const body) {
        const esp_err_t response_result =
            send_json(async_request, status, body);
        return finish_async_upload_request(
            async_request,
            response_result,
            true);
    };
    const AccessContext access = current_access_context(async_request);
    if (!access.authenticated || !access.csrf_valid) {
        const esp_err_t response_result =
            reject_management_request(async_request, access, true);
        return finish_async_upload_request(
            async_request,
            response_result,
            true);
    }
    pf_storage::StorageWorker* const worker =
        server_access_config.storage_worker;
    if (worker == nullptr || !worker->ready()) {
        return send_error(
            "503 Service Unavailable",
            "{\"ok\":false,\"error\":\"storage_unavailable\"}");
    }
    if (image_mutation_queue == nullptr ||
        uxQueueSpacesAvailable(image_mutation_queue) == 0U ||
        worker->operation_busy()) {
        return send_error(
            "503 Service Unavailable",
            "{\"ok\":false,\"error\":\"storage_busy\"}");
    }

    if (kind != ImageMutationKind::reorder &&
        async_request->content_len != 0) {
        return send_error(
            "400 Bad Request",
            "{\"ok\":false,\"error\":\"unexpected_body\"}");
    }
    if (kind == ImageMutationKind::reorder &&
        (async_request->content_len <= 0 ||
         async_request->content_len >= 512)) {
        return send_error(
            "400 Bad Request",
            "{\"ok\":false,\"error\":\"invalid_order\"}");
    }

    ImageMutationRequest queued{};
    queued.request = async_request;
    queued.worker = worker;
    queued.kind = kind;
    if (kind != ImageMutationKind::reorder) {
        const char* const suffix =
            kind == ImageMutationKind::activate ? "/activate" : "";
        char name[pf_storage::kCatalogNameCapacity]{};
        std::size_t name_length = 0U;
        if (!decode_image_action_uri(
                async_request->uri,
                suffix,
                name,
                name_length)) {
            return send_error(
                "400 Bad Request",
                "{\"ok\":false,\"error\":\"invalid_image_name\"}");
        }
        pf_storage::CatalogEntry entry{};
        if (!worker->find_catalog_entry_by_name(
                name,
                name_length,
                entry)) {
            return send_error(
                worker->operation_busy()
                    ? "503 Service Unavailable"
                    : "404 Not Found",
                worker->operation_busy()
                    ? "{\"ok\":false,\"error\":\"storage_busy\"}"
                    : "{\"ok\":false,\"error\":\"image_not_found\"}");
        }
        queued.image_id = entry.id;
    }
    if (xQueueSend(image_mutation_queue, &queued, 0U) != pdTRUE) {
        return send_error(
            "503 Service Unavailable",
            "{\"ok\":false,\"error\":\"storage_busy\"}");
    }
    return ESP_OK;
}

esp_err_t image_activate_handler(httpd_req_t* const request)
{
    return image_mutation_handler(request, ImageMutationKind::activate);
}

esp_err_t image_remove_handler(httpd_req_t* const request)
{
    return image_mutation_handler(request, ImageMutationKind::remove);
}

esp_err_t image_reorder_handler(httpd_req_t* const request)
{
    return image_mutation_handler(request, ImageMutationKind::reorder);
}

esp_err_t image_upload_handler(httpd_req_t* request)
{
    httpd_req_t* async_request = nullptr;
    if (httpd_req_async_handler_begin(request, &async_request) != ESP_OK) {
        (void)send_json(
            request,
            "503 Service Unavailable",
            "{\"ok\":false,\"error\":\"upload_unavailable\"}");
        return ESP_FAIL;
    }
    const auto send_async_error = [&](
        const char* const status,
        const char* const body) {
        const esp_err_t response_result = send_json(
            async_request,
            status,
            body);
        return finish_async_upload_request(
            async_request,
            response_result,
            true);
    };

    const AccessContext access = current_access_context(async_request);
    if (!access.authenticated || !access.csrf_valid) {
        const esp_err_t response_result =
            reject_management_request(async_request, access, true);
        return finish_async_upload_request(
            async_request,
            response_result,
            true);
    }
    if (async_request->content_len <= 0 ||
        static_cast<std::size_t>(async_request->content_len) >
            pf_image::kPfr1MaxFileBytes) {
        return send_async_error(
            "413 Payload Too Large",
            "{\"ok\":false,\"error\":\"invalid_body\"}");
    }
    pf_storage::StorageWorker* const worker =
        server_access_config.storage_worker;
    if (worker == nullptr || !worker->ready()) {
        return send_async_error(
            "503 Service Unavailable",
            "{\"ok\":false,\"error\":\"storage_unavailable\"}");
    }
    if (worker->operation_busy() || image_upload_queue == nullptr ||
        uxQueueSpacesAvailable(image_upload_queue) == 0U) {
        return send_async_error(
            "503 Service Unavailable",
            "{\"ok\":false,\"error\":\"storage_busy\"}");
    }
    const ImageUploadRequest queued{
        async_request,
        worker,
        static_cast<std::size_t>(async_request->content_len),
    };
    if (xQueueSend(image_upload_queue, &queued, 0U) != pdTRUE) {
        return send_async_error(
            "503 Service Unavailable",
            "{\"ok\":false,\"error\":\"storage_busy\"}");
    }
    return ESP_OK;
}

const httpd_uri_t kHealthRoute{
    .uri = "/api/v1/health",
    .method = HTTP_GET,
    .handler = health_handler,
    .user_ctx = nullptr,
};
const httpd_uri_t kDeviceRoute{
    .uri = "/api/v1/device",
    .method = HTTP_GET,
    .handler = device_handler,
    .user_ctx = nullptr,
};
const httpd_uri_t kStatusRoute{
    .uri = "/api/v1/status",
    .method = HTTP_GET,
    .handler = status_handler,
    .user_ctx = nullptr,
};
const httpd_uri_t kConfigRoute{
    .uri = "/api/v1/config",
    .method = HTTP_GET,
    .handler = config_handler,
    .user_ctx = nullptr,
};
const httpd_uri_t kWeatherConfigRoute{
    .uri = "/api/v1/weather/config",
    .method = HTTP_POST,
    .handler = weather_config_post_handler,
    .user_ctx = nullptr,
};
const httpd_uri_t kWeatherConfigGetRoute{
    .uri = "/api/v1/weather/config",
    .method = HTTP_GET,
    .handler = config_handler,
    .user_ctx = nullptr,
};
const httpd_uri_t kScanRoute{
    .uri = "/api/v1/wifi/scan",
    .method = HTTP_GET,
    .handler = scan_handler,
    .user_ctx = nullptr,
};
const httpd_uri_t kWifiConfigRoute{
    .uri = "/api/v1/wifi/config",
    .method = HTTP_POST,
    .handler = wifi_config_handler,
    .user_ctx = nullptr,
};
const httpd_uri_t kWifiConfigStatusRoute{
    .uri = "/api/v1/wifi/config/status",
    .method = HTTP_GET,
    .handler = wifi_config_status_handler,
    .user_ctx = nullptr,
};
const httpd_uri_t kAuthStatusRoute{
    .uri = "/api/v1/auth/status",
    .method = HTTP_GET,
    .handler = auth_status_handler,
    .user_ctx = nullptr,
};
const httpd_uri_t kAuthLoginRoute{
    .uri = "/api/v1/auth/login",
    .method = HTTP_POST,
    .handler = auth_login_handler,
    .user_ctx = nullptr,
};
const httpd_uri_t kAuthLoginStatusRoute{
    .uri = "/api/v1/auth/login/status",
    .method = HTTP_GET,
    .handler = auth_login_status_handler,
    .user_ctx = nullptr,
};
const httpd_uri_t kAuthLogoutRoute{
    .uri = "/api/v1/auth/logout",
    .method = HTTP_POST,
    .handler = auth_logout_handler,
    .user_ctx = nullptr,
};
const httpd_uri_t kImageListRoute{
    .uri = "/api/v1/images",
    .method = HTTP_GET,
    .handler = image_list_handler,
    .user_ctx = nullptr,
};
const httpd_uri_t kImageUploadRoute{
    .uri = "/api/v1/images",
    .method = HTTP_POST,
    .handler = image_upload_handler,
    .user_ctx = nullptr,
};
const httpd_uri_t kImageActivateRoute{
    .uri = "/api/v1/images/*",
    .method = HTTP_POST,
    .handler = image_activate_handler,
    .user_ctx = nullptr,
};
const httpd_uri_t kImageRemoveRoute{
    .uri = "/api/v1/images/*",
    .method = HTTP_DELETE,
    .handler = image_remove_handler,
    .user_ctx = nullptr,
};
const httpd_uri_t kImageReorderRoute{
    .uri = "/api/v1/images/order",
    .method = HTTP_PUT,
    .handler = image_reorder_handler,
    .user_ctx = nullptr,
};
const httpd_uri_t kImageDownloadRoute{
    .uri = "/api/v1/images/*",
    .method = HTTP_GET,
    .handler = image_download_handler,
    .user_ctx = nullptr,
};
const httpd_uri_t kIndexRoute{
    .uri = "/",
    .method = HTTP_GET,
    .handler = static_asset_handler,
    .user_ctx = const_cast<StaticAsset*>(&kIndexAsset),
};
const httpd_uri_t kStyleRoute{
    .uri = "/style.css",
    .method = HTTP_GET,
    .handler = static_asset_handler,
    .user_ctx = const_cast<StaticAsset*>(&kStyleAsset),
};
const httpd_uri_t kScriptRoute{
    .uri = "/ui.js",
    .method = HTTP_GET,
    .handler = static_asset_handler,
    .user_ctx = const_cast<StaticAsset*>(&kScriptAsset),
};
const httpd_uri_t kFaviconRoute{
    .uri = "/favicon.svg",
    .method = HTTP_GET,
    .handler = static_asset_handler,
    .user_ctx = const_cast<StaticAsset*>(&kFaviconAsset),
};

}  // namespace

esp_err_t start_health_server(
    httpd_handle_t* server,
    const HealthServerAccessConfig& access)
{
    if (server == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    httpd_config_t configuration = HTTPD_DEFAULT_CONFIG();
    configuration.max_uri_handlers = 24;
    configuration.uri_match_fn = httpd_uri_match_wildcard;
    configuration.recv_wait_timeout = 5;
    server_access_config = access;
    weather_config_mutex = xSemaphoreCreateMutexStatic(
        &weather_config_mutex_control);
    if (weather_config_mutex == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t result = httpd_start(server, &configuration);
    if (result != ESP_OK) {
        return result;
    }
    result = start_image_download_task();
    if (result != ESP_OK) {
        httpd_stop(*server);
        *server = nullptr;
        return result;
    }
    result = start_image_upload_task();
    if (result != ESP_OK) {
        httpd_stop(*server);
        *server = nullptr;
        return result;
    }
    result = start_image_mutation_task();
    if (result != ESP_OK) {
        httpd_stop(*server);
        *server = nullptr;
        return result;
    }
    result = start_weather_config_task();
    if (result != ESP_OK) {
        httpd_stop(*server);
        *server = nullptr;
        return result;
    }

    const httpd_uri_t* const routes[] = {
        &kHealthRoute,
        &kDeviceRoute,
        &kStatusRoute,
        &kConfigRoute,
        &kWeatherConfigRoute,
        &kWeatherConfigGetRoute,
        &kScanRoute,
        &kWifiConfigRoute,
        &kWifiConfigStatusRoute,
        &kAuthStatusRoute,
        &kAuthLoginRoute,
        &kAuthLoginStatusRoute,
        &kAuthLogoutRoute,
        &kImageListRoute,
        &kImageUploadRoute,
        &kImageActivateRoute,
        &kImageRemoveRoute,
        &kImageReorderRoute,
        &kImageDownloadRoute,
        &kIndexRoute,
        &kStyleRoute,
        &kScriptRoute,
        &kFaviconRoute,
    };
    for (const httpd_uri_t* const route : routes) {
        result = httpd_register_uri_handler(*server, route);
        if (result != ESP_OK) {
            httpd_stop(*server);
            *server = nullptr;
            return result;
        }
    }
    return ESP_OK;
}

}  // namespace pf_web
